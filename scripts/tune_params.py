#!/usr/bin/env python3
"""
tune_params.py -- sweeps autoremesher-cli's parameters and scores every trial
against the Rhino target (M4, plan/roadmap/04_M4_tuning_harness.md)

    tune_params.py oat    [--vary NAME=V1,V2,...] [--only NAME,...] [common]
    tune_params.py grid   --vary NAME=V1,V2,... [--vary ...] [common]
    tune_params.py random --vary NAME=V1,V2,... [--vary ...] -n N [--seed S] [common]
    tune_params.py list   [--out FILE]

  oat     one parameter at a time: the centre, then each value of each swept
          parameter with the others at the centre. The values default to the
          plan's (DEFAULT_SWEEP); --vary replaces one parameter's, --only
          limits the sweep to some parameters.
  grid    every combination of the --vary values, the others at the centre
  random  N distinct combinations drawn from that grid, with --seed
  list    one line per recorded trial

Common options: --cli, --source, --target, --out (default
tuning_runs/unsplit/sweep_results.jsonl), --work (tuning_runs/unsplit/trials),
--timeout seconds per CLI run (default 600), --center NAME=VALUE (the centre is
the CLI's defaults with target-quads 11054), --samples and --seed-score (passed to the
scorer), --label (stored in each record), --rerun, --dry-run.

NAME is a CLI flag without its dashes, e.g. edge-scaling (or edge_scaling).
Each trial runs the CLI once, with a timeout, writing out.obj, report.txt and
cli.log under --work/<trial id>/, then scores out.obj with score_remesh.score()
and appends one JSON line to --out: the parameters, the outcome ('ok',
'timeout', 'cli_failed' or 'score_failed'), the CLI's exit code, wall time and
output hash, and the scorer's whole record. A trial already recorded as 'ok' or
'cli_failed', or as 'timeout' with at least this timeout, is not run again
unless --rerun: the CLI is deterministic (M3.2).

Exit codes: 0 when the sweep ran, even if trials failed; 1 if the CLI, a mesh
or --out cannot be used; 2 for a usage error. Needs numpy and scipy (the scorer).
"""

import argparse
import datetime
import hashlib
import itertools
import json
import math
import os
import random
import signal
import subprocess
import sys
import time
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import score_remesh  # noqa: E402

# Bumped when a field of the record changes meaning
TUNER_VERSION = 1

Param = namedtuple('Param', 'name flag kind minimum maximum default short')

# The CLI's parameters, ranges and defaults (src/cli/cli_options.h)
PARAMS = (
    Param('target_quads', '--target-quads', int, 1, 2147483647, 50000, 'tq'),
    Param('edge_scaling', '--edge-scaling', float, 1.0, 4.0, 1.0, 'es'),
    Param('sharp_edge', '--sharp-edge', float, 30.0, 180.0, 90.0, 'se'),
    Param('smooth_normal', '--smooth-normal', float, 0.0, 180.0, 0.0, 'sn'),
    Param('adaptivity', '--adaptivity', float, 0.0, 1.0, 1.0, 'ad'),
    Param('anisotropy', '--anisotropy', float, 0.0, 1.0, 1.0, 'an'),
)
BY_NAME = {p.name: p for p in PARAMS}
CLI_DEFAULTS = {p.name: p.default for p in PARAMS}

# The CLI writes the core's own polygon mesh (D6 reversed, M2.4), so the centre
# is the target's own 11,054 quads. M4.1/M4.2 swept the split output, which has
# about 4x --target-quads quads, around 2764
CENTER = dict(CLI_DEFAULTS, target_quads=11054)

# The plan's values: target-quads around the target's count, the others each
# parameter's documented range
DEFAULT_SWEEP = {
    'target_quads': [8000, 9500, 11054, 12500, 14000],
    'edge_scaling': [1.0, 1.5, 2.0, 2.5],
    'sharp_edge': [60.0, 90.0, 120.0, 150.0],
    'smooth_normal': [0.0, 30.0, 60.0],
    'adaptivity': [0.0, 0.5, 1.0],
    'anisotropy': [0.0, 0.5, 1.0],
}

STATUSES = ('ok', 'timeout', 'cli_failed', 'score_failed')


def param_name(text):
    name = text.strip().replace('-', '_')
    if name not in BY_NAME:
        raise ValueError('unknown parameter %r (one of %s)'
                         % (text, ', '.join(p.flag[2:] for p in PARAMS)))
    return name


def parse_value(name, text):
    p = BY_NAME[name]
    try:
        v = p.kind(text)
    except ValueError:
        raise ValueError('%s: not a %snumber: %r'
                         % (p.flag[2:], 'whole ' if p.kind is int else '', text)) from None
    if not (math.isfinite(v) and p.minimum <= v <= p.maximum):
        raise ValueError('%s must be from %g to %g, not %r'
                         % (p.flag[2:], p.minimum, p.maximum, text))
    return v


def parse_vary(text):
    """'edge-scaling=1,1.5' -> ('edge_scaling', [1.0, 1.5])"""
    name, eq, values = text.partition('=')
    if not eq:
        raise ValueError('expected NAME=V1,V2,..., not %r' % text)
    name = param_name(name)
    vs = [parse_value(name, v) for v in values.split(',')] if values else []
    if not vs:
        raise ValueError('no values for %s' % name)
    if len(set(vs)) != len(vs):
        raise ValueError('repeated value in %r' % text)
    return name, vs


def parse_center(text):
    """'sharp-edge=120' -> ('sharp_edge', 120.0)"""
    name, eq, value = text.partition('=')
    if not eq:
        raise ValueError('expected NAME=VALUE, not %r' % text)
    name = param_name(name)
    return name, parse_value(name, value)


def oat_trials(sweep, center):
    """The centre, then each parameter's other values in turn"""
    trials = [dict(center)]
    for name, values in sweep.items():
        trials += [dict(center, **{name: v}) for v in values if v != center[name]]
    return trials


def grid_trials(vary, center):
    names = list(vary)
    return [dict(center, **dict(zip(names, combo)))
            for combo in itertools.product(*(vary[n] for n in names))]


def random_trials(vary, center, n, seed):
    """n distinct trials of the grid, in random order; all of it if n is larger"""
    grid = grid_trials(vary, center)
    return random.Random(seed).sample(grid, min(n, len(grid)))


def fmt(value):
    return '%g' % value if isinstance(value, float) else str(value)


def trial_id(params):
    return '_'.join(p.short + fmt(params[p.name]) for p in PARAMS)


def cli_argv(cli, source, out_obj, report, params):
    argv = [cli, '-i', source, '-o', out_obj, '--report', report]
    for p in PARAMS:
        argv += [p.flag, repr(params[p.name])]
    return argv


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def run_cli(argv, timeout, log_path):
    """Runs the CLI in its own process group, killed whole on a timeout; returns
    (exit code or None on a timeout, wall seconds, stderr)"""
    env = {k: v for k, v in os.environ.items() if k not in ('DISPLAY', 'WAYLAND_DISPLAY')}
    t0 = time.monotonic()
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env, start_new_session=True)
    try:
        out, err = proc.communicate(timeout=timeout)
        code = proc.returncode
    except subprocess.TimeoutExpired:
        os.killpg(proc.pid, signal.SIGKILL)
        out, err = proc.communicate()
        code = None
    except BaseException:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
        raise
    wall = time.monotonic() - t0
    with open(log_path, 'w') as f:
        f.write(out + ('\n--- stderr ---\n' if err else '') + err)
    return code, wall, err


def tail(text, lines=5):
    return '\n'.join(text.strip().split('\n')[-lines:])


def run_trial(params, ctx):
    tid = trial_id(params)
    d = os.path.join(ctx['work'], tid)
    os.makedirs(d, exist_ok=True)
    out_obj = os.path.join(d, 'out.obj')
    if os.path.exists(out_obj):     # never score a previous run's mesh
        os.remove(out_obj)
    argv = cli_argv(ctx['cli'], ctx['source'], out_obj, os.path.join(d, 'report.txt'), params)
    code, wall, err = run_cli(argv, ctx['timeout'], os.path.join(d, 'cli.log'))
    rec = {
        'tuner_version': TUNER_VERSION,
        'trial': tid,
        'sweep': ctx['sweep'],
        'label': ctx['label'],
        'params': params,
        'status': None,
        'error': None,
        'source': ctx['source'],
        'source_sha256': ctx['source_sha256'],
        'cli': {'argv': argv, 'exit_code': code, 'wall_s': round(wall, 3),
                'timeout_s': ctx['timeout'], 'output_sha256': None},
        'score_wall_s': None,
        'score': None,
        'time': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
    }
    if code is None:
        rec['status'] = 'timeout'
        rec['error'] = 'killed after the %g s timeout' % ctx['timeout']
    elif code != 0:
        rec['status'] = 'cli_failed'
        rec['error'] = 'exit %d: %s' % (code, tail(err))
    else:
        t0 = time.monotonic()
        try:
            rec['score'] = score_remesh.score(out_obj, ctx['target'], ctx['samples'],
                                              ctx['score_seed'])
            rec['cli']['output_sha256'] = sha256(out_obj)
            rec['status'] = 'ok'
        except Exception as e:  # one bad trial must not end the sweep
            rec['score'] = None
            rec['status'] = 'score_failed'
            rec['error'] = '%s: %s' % (type(e).__name__, e)
        rec['score_wall_s'] = round(time.monotonic() - t0, 3)
    return rec


def load_records(path):
    if not os.path.exists(path):
        return []
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def already_done(previous, timeout):
    """Whether one of a trial's earlier records makes running it again pointless"""
    for r in previous:
        if r['status'] in ('ok', 'cli_failed'):
            return True
        if r['status'] == 'timeout' and r['cli']['timeout_s'] >= timeout:
            return True
    return False


def summary(rec):
    s = rec.get('score')
    if not s:
        return rec['error'] or ''
    return ('quads %d (%+.1f%%)  cv %.4f  fit %.4f  fit_rev %.4f  degenerate %d'
            % (s['quads']['candidate'], s['quads']['delta_pct'],
               s['edge_length_cv']['candidate'] or 0.0, s['fit']['mean'],
               s['fit_reverse']['mean'], s['degenerate_quads']['candidate']))


def list_records(path):
    print('%-34s %-12s %8s %8s %7s %8s %8s %4s %7s %7s'
          % ('trial', 'status', 'quads', 'delta%', 'cv', 'fit', 'fit_rev', 'deg',
             'cli_s', 'score_s'))
    for r in load_records(path):
        s = r.get('score')
        if s:
            cols = ('%8d %8.1f %7.4f %8.4f %8.4f %4d'
                    % (s['quads']['candidate'], s['quads']['delta_pct'],
                       s['edge_length_cv']['candidate'] or 0.0, s['fit']['mean'],
                       s['fit_reverse']['mean'], s['degenerate_quads']['candidate']))
        else:
            cols = '%8s %8s %7s %8s %8s %4s' % (('-',) * 6)
        print('%-34s %-12s %s %7.1f %7s' % (
            r['trial'], r['status'], cols, r['cli']['wall_s'],
            '-' if r['score_wall_s'] is None else '%.1f' % r['score_wall_s']))


def positive(text):
    v = float(text)
    if not (math.isfinite(v) and v > 0):
        raise argparse.ArgumentTypeError('must be > 0: %s' % text)
    return v


def non_negative(text):
    n = int(text)
    if n < 0:
        raise argparse.ArgumentTypeError('must be >= 0: %s' % text)
    return n


def build_parser():
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument('--out', default=os.path.join('tuning_runs', 'unsplit',
                                                      'sweep_results.jsonl'),
                        help='JSON-lines file each trial is appended to (default %(default)s)')

    sweep = argparse.ArgumentParser(add_help=False, parents=[common])
    sweep.add_argument('--cli', default=os.path.join('build', 'autoremesher-cli'),
                       help='the autoremesher-cli executable (default %(default)s)')
    sweep.add_argument('--source', default='source.obj',
                       help='mesh to remesh (default %(default)s)')
    sweep.add_argument('--target', default='quad-remeshed.obj',
                       help='mesh to score against (default %(default)s)')
    sweep.add_argument('--work', default=os.path.join('tuning_runs', 'unsplit', 'trials'),
                       help='directory of the trials\' meshes and logs (default %(default)s)')
    sweep.add_argument('--timeout', type=positive, default=600.0,
                       help='seconds before a CLI run is killed (default %(default)g)')
    sweep.add_argument('--vary', action='append', default=[], metavar='NAME=V1,V2,...',
                       help='values of one parameter to sweep; repeatable')
    sweep.add_argument('--center', action='append', default=[], metavar='NAME=VALUE',
                       help='value of a parameter when it is not swept; repeatable '
                            '(default: the CLI\'s defaults, with target-quads 11054)')
    sweep.add_argument('--samples', type=non_negative, default=0,
                       help='scorer: vertices sampled per distance (default 0: all)')
    sweep.add_argument('--seed-score', type=int, default=0,
                       help='scorer: seed for --samples (default 0)')
    sweep.add_argument('--label', help='free text stored in every record')
    sweep.add_argument('--rerun', action='store_true',
                       help='run trials that are already recorded, too')
    sweep.add_argument('--dry-run', action='store_true',
                       help='print the trial ids and run nothing')

    ap = argparse.ArgumentParser(
        description='Sweep autoremesher-cli parameters and score each trial against '
                    'the target mesh.')
    sub = ap.add_subparsers(dest='command', required=True)
    oat = sub.add_parser('oat', parents=[sweep], help='one parameter at a time')
    oat.add_argument('--only', help='comma-separated parameters to sweep (default all)')
    sub.add_parser('grid', parents=[sweep], help='every combination of the --vary values')
    rnd = sub.add_parser('random', parents=[sweep], help='random combinations')
    rnd.add_argument('-n', type=non_negative, required=True, help='number of trials')
    rnd.add_argument('--seed', type=int, default=0, help='random seed (default 0)')
    sub.add_parser('list', parents=[common], help='list the recorded trials')
    return ap


def plan_trials(args):
    vary = dict(parse_vary(v) for v in args.vary)
    center = dict(CENTER, **dict(parse_center(c) for c in args.center))
    if args.command == 'oat':
        sweep = dict(DEFAULT_SWEEP, **vary)
        if args.only:
            only = [param_name(n) for n in args.only.split(',')]
            sweep = {n: sweep[n] for n in sweep if n in only}
        return oat_trials(sweep, center)
    if not vary:
        raise ValueError('%s needs at least one --vary' % args.command)
    if args.command == 'grid':
        return grid_trials(vary, center)
    return random_trials(vary, center, args.n, args.seed)


def main(argv=None):
    ap = build_parser()
    args = ap.parse_args(argv)
    if args.command == 'list':
        try:
            list_records(args.out)
        except (OSError, ValueError, KeyError) as e:
            print('tune_params.py: %s: %s' % (args.out, e), file=sys.stderr)
            return 1
        return 0
    try:
        trials = plan_trials(args)
    except ValueError as e:
        ap.error(str(e))

    if args.dry_run:
        for p in trials:
            print(trial_id(p))
        return 0

    for path, what in ((args.source, 'source'), (args.target, 'target')):
        if not os.path.isfile(path):
            print('tune_params.py: %s mesh not found: %s' % (what, path), file=sys.stderr)
            return 1
    if not (os.path.isfile(args.cli) and os.access(args.cli, os.X_OK)):
        print('tune_params.py: not an executable: %s' % args.cli, file=sys.stderr)
        return 1
    try:
        os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
        os.makedirs(args.work, exist_ok=True)
        previous = load_records(args.out)
        source_sha = sha256(args.source)
    except (OSError, ValueError) as e:
        print('tune_params.py: %s' % e, file=sys.stderr)
        return 1

    by_id = {}
    for r in previous:
        by_id.setdefault(r['trial'], []).append(r)
    todo = [p for p in trials
            if args.rerun or not already_done(by_id.get(trial_id(p), []), args.timeout)]
    print('tune_params.py: %d trials, %d already recorded, %d to run'
          % (len(trials), len(trials) - len(todo), len(todo)), file=sys.stderr)

    ctx = {'cli': args.cli, 'source': args.source, 'target': args.target,
           'work': args.work, 'timeout': args.timeout, 'samples': args.samples,
           'score_seed': args.seed_score, 'sweep': args.command, 'label': args.label,
           'source_sha256': source_sha}
    counts = dict.fromkeys(STATUSES, 0)
    for i, p in enumerate(todo, 1):
        rec = run_trial(p, ctx)
        try:
            with open(args.out, 'a') as f:
                f.write(json.dumps(rec, allow_nan=False) + '\n')
        except OSError as e:
            print('tune_params.py: %s' % e, file=sys.stderr)
            return 1
        counts[rec['status']] += 1
        print('[%d/%d] %s %s %.1fs+%.1fs  %s'
              % (i, len(todo), rec['trial'], rec['status'], rec['cli']['wall_s'],
                 rec['score_wall_s'] or 0.0, summary(rec)), file=sys.stderr, flush=True)
    print('tune_params.py: ' + ', '.join('%d %s' % (counts[s], s) for s in STATUSES),
          file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
