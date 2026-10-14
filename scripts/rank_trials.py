#!/usr/bin/env python3
"""
rank_trials.py -- ranks the sweep's trials per D5 (plan/01_decisions.md) and
reports the top N (M4, plan/roadmap/04_M4_tuning_harness.md)

    rank_trials.py [--results FILE] [--band PCT] [--top N] [--fit-decimals D]
                   [--baseline FILE:LINE ...] [--json FILE]

D5, in order:
  1. drop every trial that did not finish ('timeout', 'cli_failed',
     'score_failed'), then every trial with a degenerate quad;
  2. gate on quad count: drop every trial whose quad count is more than PCT %
     from the target's (|quads.delta_pct| > PCT; default 15);
  3. rank the survivors by combined fit, mean(fit.mean, fit_reverse.mean),
     lower first;
  4. break ties by the edge-length CV's distance from the target's own CV,
     |edge_length_cv.candidate - edge_length_cv.target|, lower first.

Ties are exact unless --fit-decimals D rounds the combined fit to D decimals
first. A trial recorded more than once (tune_params.py --rerun) is ranked once,
from its last 'ok' record.

--results is tune_params.py's file (default tuning_runs/sweep_results.jsonl).
--baseline names one line (from 1) of a file of score_remesh.py records, such
as plan/baselines/m3_baseline.jsonl:2, M3's default-parameter baseline; it is
put through the same steps and reported next to the winner. --json writes the
whole ranking, the excluded trials and the baselines as one JSON object, with
no timestamp, so the same inputs give the same file.

Exit codes: 0 ranked; 1 a file cannot be read; 2 usage error; 3 no trial
survives the gate (nothing is printed or written: widen --band, as the error
says -- an ungated ranking would just pick the densest mesh, which is what D5
exists to prevent).
"""

import argparse
import json
import math
import os
import sys

# Bumped when a field of the --json output changes meaning
RANKER_VERSION = 1

DEFAULT_RESULTS = os.path.join('tuning_runs', 'sweep_results.jsonl')
DEFAULT_BAND = 15.0

STATISTIC = 'mean(fit.mean, fit_reverse.mean)'
TIE_BREAK = 'abs(edge_length_cv.candidate - edge_length_cv.target)'

# tune_params.py's parameters, in its trial-id order, and their CLI flags
PARAM_FLAGS = (('target_quads', '--target-quads'), ('edge_scaling', '--edge-scaling'),
               ('sharp_edge', '--sharp-edge'), ('smooth_normal', '--smooth-normal'),
               ('adaptivity', '--adaptivity'), ('anisotropy', '--anisotropy'))


def combined_fit(s):
    return (s['fit']['mean'] + s['fit_reverse']['mean']) / 2.0


def cv_distance(s):
    cv = s['edge_length_cv']
    return abs(cv['candidate'] - cv['target'])


def gate(s, band_pct):
    """Why a scorer record is out under D5 ('degenerate', 'outside_band'), or None"""
    if s['degenerate_quads']['candidate'] != 0:
        return 'degenerate'
    if abs(s['quads']['delta_pct']) > band_pct:
        return 'outside_band'
    return None


def fmt(value):
    return '%g' % value


def cli_flags(params):
    return ' '.join('%s %s' % (flag, fmt(params[name])) for name, flag in PARAM_FLAGS)


def latest(records):
    """One record per trial, in first-seen order: its last 'ok' one, else its last"""
    order, by_id = [], {}
    for r in records:
        t = r['trial']
        if t not in by_id:
            order.append(t)
        if t not in by_id or r['status'] == 'ok' or by_id[t]['status'] != 'ok':
            by_id[t] = r
    return [by_id[t] for t in order]


def entry(r):
    s = r['score']
    return {
        'trial': r['trial'],
        'params': r['params'],
        'cli_flags': cli_flags(r['params']),
        'quads': s['quads']['candidate'],
        'delta_pct': s['quads']['delta_pct'],
        'combined_fit': combined_fit(s),
        'fit_mean': s['fit']['mean'],
        'fit_reverse_mean': s['fit_reverse']['mean'],
        'edge_length_cv': s['edge_length_cv']['candidate'],
        'cv_distance': cv_distance(s),
        'output_sha256': r['cli']['output_sha256'],
    }


def rank(records, band_pct, fit_decimals=None):
    """D5's ranking: (ranked survivors with 'rank' from 1, excluded with 'reason')"""
    survivors, excluded = [], []
    for r in latest(records):
        if r['status'] != 'ok':
            excluded.append({'trial': r['trial'], 'reason': r['status']})
            continue
        why = gate(r['score'], band_pct)
        if why:
            excluded.append({'trial': r['trial'], 'reason': why,
                             'delta_pct': r['score']['quads']['delta_pct']})
            continue
        survivors.append(entry(r))

    def key(e):
        fit = e['combined_fit']
        if fit_decimals is not None:
            fit = round(fit, fit_decimals)
        return (fit, e['cv_distance'], e['combined_fit'], e['trial'])

    survivors.sort(key=key)
    return [dict(e, rank=i) for i, e in enumerate(survivors, 1)], excluded


def band_needed(records):
    """The smallest band that lets one finished, non-degenerate trial in, or None"""
    deltas = [abs(r['score']['quads']['delta_pct']) for r in latest(records)
              if r['status'] == 'ok' and r['score']['degenerate_quads']['candidate'] == 0]
    return min(deltas) if deltas else None


def parse_baseline_spec(text):
    path, sep, line = text.rpartition(':')
    if not sep or not path:
        raise ValueError('expected FILE:LINE, got %r' % text)
    try:
        n = int(line)
    except ValueError:
        raise ValueError('expected FILE:LINE with LINE a number, got %r' % text)
    if n < 1:
        raise ValueError('LINE counts from 1: %r' % text)
    return path, n


def load_baseline(text):
    path, n = parse_baseline_spec(text)
    with open(path) as f:
        lines = [line for line in f if line.strip()]
    if n > len(lines):
        raise ValueError('%s has %d records, not %d' % (path, len(lines), n))
    s = json.loads(lines[n - 1])
    return {'file': path, 'line': n, 'score': s}


def baseline_entry(b, band_pct):
    s = b['score']
    return {
        'file': b['file'], 'line': b['line'], 'candidate': s['candidate'],
        'gate': gate(s, band_pct),
        'quads': s['quads']['candidate'], 'delta_pct': s['quads']['delta_pct'],
        'combined_fit': combined_fit(s),
        'fit_mean': s['fit']['mean'], 'fit_reverse_mean': s['fit_reverse']['mean'],
        'edge_length_cv': s['edge_length_cv']['candidate'], 'cv_distance': cv_distance(s),
    }


def load_records(path):
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def counts(records, ranked, excluded):
    c = {'records': len(records), 'trials': len(ranked) + len(excluded),
         'not_ok': 0, 'degenerate': 0, 'outside_band': 0, 'ranked': len(ranked)}
    for e in excluded:
        reason = e['reason'] if e['reason'] in ('degenerate', 'outside_band') else 'not_ok'
        c[reason] += 1
    return c


def print_report(result, top):
    c = result['counts']
    print('D5 ranking of %s: %d trials (%d records); excluded %d not ok, %d degenerate, '
          '%d outside +-%g %% of %d quads; %d ranked'
          % (result['results'], c['trials'], c['records'], c['not_ok'], c['degenerate'],
             c['outside_band'], result['band_pct'], result['target_quads'], c['ranked']))
    print('ranked by %s%s, ties by %s'
          % (STATISTIC, '' if result['fit_decimals'] is None
             else ' rounded to %d decimals' % result['fit_decimals'], TIE_BREAK))
    print()
    print('%4s  %-34s %7s %8s %9s %9s %9s %7s %7s'
          % ('rank', 'trial', 'quads', 'delta%', 'combined', 'fit', 'fit_rev', 'cv', '|dcv|'))
    for e in result['ranked'][:top]:
        print('%4d  %-34s %7d %+8.1f %9.4f %9.4f %9.4f %7.4f %7.4f'
              % (e['rank'], e['trial'], e['quads'], e['delta_pct'], e['combined_fit'],
                 e['fit_mean'], e['fit_reverse_mean'], e['edge_length_cv'], e['cv_distance']))
    win = result['ranked'][0]
    print()
    print('winner: %s' % win['trial'])
    print('  autoremesher-cli %s' % win['cli_flags'])
    for b in result['baselines']:
        print('baseline %s:%d (%s): %d quads (%+.1f %%), combined fit %.4f, %s; '
              'winner / baseline = %.3f'
              % (b['file'], b['line'], b['candidate'], b['quads'], b['delta_pct'],
                 b['combined_fit'], 'passes the gate' if b['gate'] is None
                 else 'excluded: %s' % b['gate'], win['combined_fit'] / b['combined_fit']))


def positive(text):
    v = float(text)
    if not (math.isfinite(v) and v > 0):
        raise argparse.ArgumentTypeError('must be > 0: %s' % text)
    return v


def at_least(lowest):
    def check(text):
        n = int(text)
        if n < lowest:
            raise argparse.ArgumentTypeError('must be >= %d: %s' % (lowest, text))
        return n
    return check


def baseline_spec(text):
    try:
        parse_baseline_spec(text)
    except ValueError as e:
        raise argparse.ArgumentTypeError(str(e))
    return text


def build_parser():
    ap = argparse.ArgumentParser(
        description='Rank the sweep\'s trials per D5: drop degenerate trials, gate on '
                    'quad count, rank by combined fit, break ties by edge-length CV.')
    ap.add_argument('--results', default=DEFAULT_RESULTS,
                    help='tune_params.py\'s JSON-lines file (default %(default)s)')
    ap.add_argument('--band', type=positive, default=DEFAULT_BAND, metavar='PCT',
                    help='quad-count gate, %% either side of the target (default %(default)g)')
    ap.add_argument('--top', type=at_least(1), default=10,
                    help='trials to print (default %(default)d; --json keeps them all)')
    ap.add_argument('--fit-decimals', type=at_least(0), metavar='D',
                    help='round the combined fit to D decimals before ranking, so close '
                         'fits tie and the CV decides (default: exact)')
    ap.add_argument('--baseline', action='append', default=[], type=baseline_spec,
                    metavar='FILE:LINE', help='a score_remesh.py record to report against '
                                              'the winner; repeatable')
    ap.add_argument('--json', metavar='FILE', help='write the whole ranking here')
    return ap


def main(argv=None):
    args = build_parser().parse_args(argv)
    try:
        records = load_records(args.results)
        baselines = [load_baseline(b) for b in args.baseline]
    except (OSError, ValueError) as e:
        print('rank_trials.py: %s' % e, file=sys.stderr)
        return 1

    ranked, excluded = rank(records, args.band, args.fit_decimals)
    if not ranked:
        need = band_needed(records)
        print('rank_trials.py: no trial survives the +-%g %% quad-count gate%s; widen '
              '--band and record why (D5: no ungated fallback)'
              % (args.band, '' if need is None else
                 ' (the nearest finished, non-degenerate trial is %.1f %% off)' % need),
              file=sys.stderr)
        return 3

    some = ranked[0]
    result = {
        'ranker_version': RANKER_VERSION,
        'results': args.results,
        'band_pct': args.band,
        'statistic': STATISTIC,
        'tie_break': TIE_BREAK,
        'fit_decimals': args.fit_decimals,
        'target_quads': next(r['score']['quads']['target'] for r in records
                             if r['trial'] == some['trial'] and r['score']),
        'counts': counts(records, ranked, excluded),
        'ranked': ranked,
        'excluded': excluded,
        'baselines': [baseline_entry(b, args.band) for b in baselines],
    }
    print_report(result, args.top)
    if args.json:
        try:
            with open(args.json, 'w') as f:
                json.dump(result, f, indent=1, allow_nan=False)
                f.write('\n')
        except OSError as e:
            print('rank_trials.py: %s' % e, file=sys.stderr)
            return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
