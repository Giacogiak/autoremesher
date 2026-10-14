#!/usr/bin/env python3
"""
Unit tests for scripts/tune_params.py (M4), against a fake autoremesher-cli
written by the test, plus one real trial where the real CLI and meshes exist.

    python3 tests/test_tune_params.py

CTest runs this file as `test_tune_params`. The real trial needs
$AUTOREMESHER_CLI, $AUTOREMESHER_SOURCE_OBJ and $AUTOREMESHER_TARGET_OBJ (the
meshes default to the repository root's) and is skipped where any is missing.
"""

import io
import json
import os
import stat
import subprocess
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
SCRIPT = os.path.join(ROOT, 'scripts', 'tune_params.py')
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import tune_params as T  # noqa: E402

REAL_CLI = os.environ.get('AUTOREMESHER_CLI', '')
SOURCE_OBJ = os.environ.get('AUTOREMESHER_SOURCE_OBJ', os.path.join(ROOT, 'source.obj'))
TARGET_OBJ = os.environ.get('AUTOREMESHER_TARGET_OBJ', os.path.join(ROOT, 'quad-remeshed.obj'))
BASELINE = os.path.join(ROOT, 'plan', 'baselines', 'm3_baseline.jsonl')

# Stands in for autoremesher-cli. It logs its argv, then writes a strip of
# target-quads quads at height edge-scaling - 1, and a report if asked. Some
# target-quads values misbehave on purpose: 13 hangs, 17 fails, 19 exits 0
# without writing the output, as does any run with $FAKE_CLI_NO_OUTPUT set.
FAKE_CLI = r'''
import json, os, sys, time
args = sys.argv[1:]
opts = dict(zip(args[0::2], args[1::2]))
with open(os.environ['FAKE_CLI_LOG'], 'a') as f:
    f.write(json.dumps(args) + '\n')
n = int(opts.get('--target-quads', '50000'))
z = float(opts.get('--edge-scaling', '1.0')) - 1.0
print('status 50%', flush=True)
if n == 13:
    time.sleep(60)
if n == 17:
    print('fake-cli: remeshing failed', file=sys.stderr)
    sys.exit(1)
if n == 19 or os.environ.get('FAKE_CLI_NO_OUTPUT'):
    sys.exit(0)
with open(opts['-o'], 'w') as f:
    for i in range(n + 1):
        f.write('v %d 0 %r\nv %d 1 %r\n' % (i, z, i, z))
    for i in range(n):
        f.write('f %d %d %d %d\n' % (2 * i + 1, 2 * i + 3, 2 * i + 4, 2 * i + 2))
if '--report' in opts:
    with open(opts['--report'], 'w') as f:
        f.write('Output quads: %d\n' % n)
print('status 100%')
print('autoremesher-cli: fake', file=sys.stderr)
'''


def strip_obj(path, n, z=0.0):
    with open(path, 'w') as f:
        for i in range(n + 1):
            f.write('v %d 0 %r\nv %d 1 %r\n' % (i, z, i, z))
        for i in range(n):
            f.write('f %d %d %d %d\n' % (2 * i + 1, 2 * i + 3, 2 * i + 4, 2 * i + 2))


def read_jsonl(path):
    with open(path) as f:
        return [json.loads(line) for line in f]


class Plan(unittest.TestCase):
    def test_defaults_are_the_plans(self):
        self.assertEqual(T.CLI_DEFAULTS, {
            'target_quads': 50000, 'edge_scaling': 1.0, 'sharp_edge': 90.0,
            'smooth_normal': 0.0, 'adaptivity': 1.0, 'anisotropy': 1.0})
        # The centre is the CLI's defaults with target-quads re-centred (M2.3's D6
        # warning in the M4 plan): about 11,054 / 4
        self.assertEqual(T.CENTER, dict(T.CLI_DEFAULTS, target_quads=2764))
        self.assertEqual(T.DEFAULT_SWEEP, {
            'target_quads': [2000, 2400, 2764, 3100, 3500],
            'edge_scaling': [1.0, 1.5, 2.0, 2.5],
            'sharp_edge': [60.0, 90.0, 120.0, 150.0],
            'smooth_normal': [0.0, 30.0, 60.0],
            'adaptivity': [0.0, 0.5, 1.0],
            'anisotropy': [0.0, 0.5, 1.0]})

    def test_one_at_a_time(self):
        trials = T.oat_trials(T.DEFAULT_SWEEP, T.CENTER)
        self.assertEqual(trials[0], T.CENTER)
        # every value the centre does not already have, one parameter at a time
        self.assertEqual(len(trials), 1 + 4 + 3 + 3 + 2 + 2 + 2)
        ids = [T.trial_id(p) for p in trials]
        self.assertEqual(len(set(ids)), len(ids))
        for p in trials[1:]:
            changed = [k for k in p if p[k] != T.CENTER[k]]
            self.assertEqual(len(changed), 1, p)
            self.assertIn(p[changed[0]], T.DEFAULT_SWEEP[changed[0]])
        self.assertEqual(trials[1], dict(T.CENTER, target_quads=2000))

    def test_one_at_a_time_value_off_the_centre(self):
        trials = T.oat_trials({'adaptivity': [0.0, 0.5]}, T.CENTER)
        self.assertEqual([p['adaptivity'] for p in trials], [1.0, 0.0, 0.5])

    def test_grid(self):
        vary = {'target_quads': [10, 20], 'anisotropy': [0.0, 0.5, 1.0]}
        trials = T.grid_trials(vary, T.CENTER)
        self.assertEqual(len(trials), 6)
        self.assertEqual(trials[0], dict(T.CENTER, target_quads=10, anisotropy=0.0))
        self.assertEqual(trials[-1], dict(T.CENTER, target_quads=20, anisotropy=1.0))
        self.assertEqual(len({T.trial_id(p) for p in trials}), 6)

    def test_random(self):
        vary = {'target_quads': [10, 20, 30], 'adaptivity': [0.0, 0.5, 1.0]}
        a = T.random_trials(vary, T.CENTER, 4, seed=1)
        self.assertEqual(a, T.random_trials(vary, T.CENTER, 4, seed=1))
        self.assertEqual(len({T.trial_id(p) for p in a}), 4)
        grid = T.grid_trials(vary, T.CENTER)
        for p in a:
            self.assertIn(p, grid)
        others = [T.random_trials(vary, T.CENTER, 4, seed=s) for s in range(2, 6)]
        self.assertTrue(any(o != a for o in others))
        # more than the grid holds: the whole grid, once each
        everything = T.random_trials(vary, T.CENTER, 50, seed=1)
        self.assertEqual(sorted(map(T.trial_id, everything)), sorted(map(T.trial_id, grid)))

    def test_parse_vary(self):
        self.assertEqual(T.parse_vary('edge-scaling=1,1.5'), ('edge_scaling', [1.0, 1.5]))
        self.assertEqual(T.parse_vary('target_quads=2000,2764'),
                         ('target_quads', [2000, 2764]))
        for bad in ('edge-scaling', 'colour=1', 'edge-scaling=', 'edge-scaling=0.5',
                    'edge-scaling=1,x', 'target-quads=2.5', 'target-quads=0',
                    'adaptivity=1.5', 'sharp-edge=nan', 'smooth-normal=1,1'):
            with self.assertRaises(ValueError, msg=bad):
                T.parse_vary(bad)

    def test_parse_center(self):
        self.assertEqual(T.parse_center('sharp-edge=120'), ('sharp_edge', 120.0))
        with self.assertRaises(ValueError):
            T.parse_center('sharp-edge=120,150')

    def test_trial_id(self):
        self.assertEqual(T.trial_id(T.CENTER), 'tq2764_es1_se90_sn0_ad1_an1')
        self.assertEqual(T.trial_id(dict(T.CENTER, edge_scaling=1.5, adaptivity=0.25)),
                         'tq2764_es1.5_se90_sn0_ad0.25_an1')

    def test_cli_argv(self):
        argv = T.cli_argv('cli', 'in.obj', 'out.obj', 'r.txt',
                          dict(T.CENTER, edge_scaling=1.5))
        self.assertEqual(argv, [
            'cli', '-i', 'in.obj', '-o', 'out.obj', '--report', 'r.txt',
            '--target-quads', '2764', '--edge-scaling', '1.5', '--sharp-edge', '90.0',
            '--smooth-normal', '0.0', '--adaptivity', '1.0', '--anisotropy', '1.0'])


class Sweep(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.dir = tmp.name
        self.cli = os.path.join(self.dir, 'fake-cli')
        with open(self.cli, 'w') as f:
            f.write('#!' + sys.executable + '\n' + FAKE_CLI)
        os.chmod(self.cli, os.stat(self.cli).st_mode | stat.S_IXUSR)
        self.log = os.path.join(self.dir, 'cli.log')
        os.environ['FAKE_CLI_LOG'] = self.log
        self.source = os.path.join(self.dir, 'source.obj')
        strip_obj(self.source, 2)
        self.target = os.path.join(self.dir, 'target.obj')
        strip_obj(self.target, 10)
        self.out = os.path.join(self.dir, 'runs', 'sweep.jsonl')
        self.work = os.path.join(self.dir, 'runs', 'trials')

    def run_main(self, *args):
        out, err = io.StringIO(), io.StringIO()
        common = (['--out', self.out] if args[0] == 'list' else
                  ['--cli', self.cli, '--source', self.source, '--target', self.target,
                   '--out', self.out, '--work', self.work])
        with redirect_stdout(out), redirect_stderr(err):
            try:
                code = T.main([args[0], *common, *args[1:]])
            except SystemExit as e:     # argparse's usage errors
                code = e.code
        return code, out.getvalue(), err.getvalue()

    def cli_calls(self):
        return read_jsonl(self.log) if os.path.exists(self.log) else []

    def test_records_every_trial(self):
        code, _, err = self.run_main('grid', '--vary', 'target-quads=8,10',
                                     '--vary', 'edge-scaling=1,1.5', '--label', 'g1')
        self.assertEqual(code, 0, err)
        recs = read_jsonl(self.out)
        self.assertEqual(len(recs), 4)
        self.assertEqual(len(self.cli_calls()), 4)
        for r in recs:
            self.assertEqual(r['status'], 'ok', r)
            self.assertEqual(r['tuner_version'], T.TUNER_VERSION)
            self.assertEqual(r['sweep'], 'grid')
            self.assertEqual(r['label'], 'g1')
            self.assertEqual(r['trial'], T.trial_id(r['params']))
            p = r['params']
            self.assertEqual(r['cli']['exit_code'], 0)
            self.assertIsNone(r['error'])
            self.assertEqual(r['cli']['argv'][1:3], ['-i', self.source])
            # the fake writes target-quads quads at height edge-scaling - 1, all
            # of them over the target's 10
            self.assertEqual(r['score']['quads']['candidate'], p['target_quads'])
            self.assertEqual(r['score']['quads']['target'], 10)
            self.assertAlmostEqual(r['score']['fit']['max'], p['edge_scaling'] - 1.0)
            self.assertEqual(r['score']['degenerate_quads']['candidate'], 0)
            d = os.path.join(self.work, r['trial'])
            self.assertEqual(r['score']['candidate'], os.path.join(d, 'out.obj'))
            self.assertEqual(r['cli']['output_sha256'], T.sha256(os.path.join(d, 'out.obj')))
            with open(os.path.join(d, 'report.txt')) as f:
                self.assertEqual(f.read(), 'Output quads: %d\n' % p['target_quads'])
            with open(os.path.join(d, 'cli.log')) as f:
                self.assertIn('status 100%', f.read())
            self.assertEqual(r['source_sha256'], T.sha256(self.source))
        self.assertEqual([r['params']['target_quads'] for r in recs], [8, 8, 10, 10])
        self.assertIn('4 trials', err)

    def test_other_parameters_stay_at_the_centre(self):
        code, _, err = self.run_main('grid', '--vary', 'adaptivity=0.5',
                                     '--center', 'target-quads=11', '--center', 'sharp-edge=60')
        self.assertEqual(code, 0, err)
        (r,) = read_jsonl(self.out)
        self.assertEqual(r['params'], dict(T.CENTER, target_quads=11, sharp_edge=60.0,
                                           adaptivity=0.5))
        args = self.cli_calls()[0]
        self.assertEqual(args[args.index('--sharp-edge') + 1], '60.0')
        self.assertEqual(args[args.index('--adaptivity') + 1], '0.5')

    def test_failed_trials_are_recorded_and_the_sweep_goes_on(self):
        t0 = time.monotonic()
        code, _, err = self.run_main('grid', '--vary', 'target-quads=13,17,19,11',
                                     '--timeout', '1')
        self.assertEqual(code, 0, err)
        self.assertLess(time.monotonic() - t0, 30)
        hang, fail, empty, good = read_jsonl(self.out)
        self.assertEqual(hang['status'], 'timeout')
        self.assertIsNone(hang['cli']['exit_code'])
        self.assertGreaterEqual(hang['cli']['wall_s'], 1.0)
        self.assertLess(hang['cli']['wall_s'], 20.0)
        self.assertEqual(hang['cli']['timeout_s'], 1.0)
        self.assertIsNone(hang['score'])
        self.assertIn('1 s', hang['error'])
        self.assertEqual(fail['status'], 'cli_failed')
        self.assertEqual(fail['cli']['exit_code'], 1)
        self.assertIn('remeshing failed', fail['error'])
        self.assertIsNone(fail['score'])
        self.assertEqual(empty['status'], 'score_failed')
        self.assertEqual(empty['cli']['exit_code'], 0)
        self.assertIn('out.obj', empty['error'])
        self.assertIsNone(empty['score'])
        self.assertEqual(good['status'], 'ok')
        self.assertIn('1 ok', err)

    def test_a_hung_cli_is_killed(self):
        self.run_main('grid', '--vary', 'target-quads=13', '--timeout', '0.5')
        (r,) = read_jsonl(self.out)
        self.assertEqual(r['status'], 'timeout')
        # the fake's whole process group is gone: nothing still holds its log open
        p = subprocess.run(['pgrep', '-f', self.cli], capture_output=True, text=True)
        self.assertEqual(p.stdout, '')

    def test_resume_skips_finished_trials(self):
        self.run_main('grid', '--vary', 'target-quads=10,17,13', '--timeout', '0.5')
        self.assertEqual(len(self.cli_calls()), 3)
        # ok and cli_failed are not run again; a timeout is, with a longer timeout
        code, _, err = self.run_main('grid', '--vary', 'target-quads=10,17,13,12',
                                     '--timeout', '0.5')
        self.assertEqual(code, 0, err)
        self.assertEqual([c[c.index('--target-quads') + 1] for c in self.cli_calls()[3:]],
                         ['12'])
        self.assertIn('3 already recorded', err)
        self.run_main('grid', '--vary', 'target-quads=13', '--timeout', '0.7')
        self.assertEqual(len(self.cli_calls()), 5)
        self.run_main('grid', '--vary', 'target-quads=10', '--rerun')
        self.assertEqual(len(self.cli_calls()), 6)
        self.assertEqual(len(read_jsonl(self.out)), 6)

    def test_a_previous_runs_mesh_is_not_scored(self):
        self.run_main('grid', '--vary', 'target-quads=10')
        os.environ['FAKE_CLI_NO_OUTPUT'] = '1'
        self.addCleanup(os.environ.pop, 'FAKE_CLI_NO_OUTPUT')
        self.run_main('grid', '--vary', 'target-quads=10', '--rerun')
        self.assertEqual([r['status'] for r in read_jsonl(self.out)], ['ok', 'score_failed'])

    def test_one_at_a_time_command(self):
        code, _, err = self.run_main('oat', '--center', 'target-quads=10',
                                     '--vary', 'target-quads=10,12',
                                     '--vary', 'edge-scaling=1,2',
                                     '--only', 'target-quads,edge-scaling')
        self.assertEqual(code, 0, err)
        recs = read_jsonl(self.out)
        self.assertEqual([r['trial'] for r in recs], [
            'tq10_es1_se90_sn0_ad1_an1', 'tq12_es1_se90_sn0_ad1_an1',
            'tq10_es2_se90_sn0_ad1_an1'])
        self.assertTrue(all(r['sweep'] == 'oat' for r in recs))

    def test_random_command(self):
        code, _, err = self.run_main('random', '--vary', 'target-quads=10,11,12',
                                     '--vary', 'edge-scaling=1,2', '-n', '3', '--seed', '4')
        self.assertEqual(code, 0, err)
        recs = read_jsonl(self.out)
        self.assertEqual([r['params'] for r in recs], T.random_trials(
            {'target_quads': [10, 11, 12], 'edge_scaling': [1.0, 2.0]}, T.CENTER, 3, 4))

    def test_dry_run(self):
        code, out, err = self.run_main('oat', '--dry-run')
        self.assertEqual(code, 0, err)
        self.assertEqual(len(out.strip().split('\n')), 17)
        self.assertIn('tq2764_es1_se90_sn0_ad1_an1', out)
        self.assertEqual(self.cli_calls(), [])
        self.assertFalse(os.path.exists(self.out))

    def test_list(self):
        self.run_main('grid', '--vary', 'target-quads=10,17')
        code, out, err = self.run_main('list')
        self.assertEqual(code, 0, err)
        lines = out.strip().split('\n')
        self.assertEqual(len(lines), 3)
        self.assertIn('tq10_', lines[1])
        self.assertIn('ok', lines[1])
        self.assertIn('cli_failed', lines[2])

    def test_usage_and_setup_errors(self):
        for args in (('grid',), ('grid', '--vary', 'colour=1'),
                     ('grid', '--vary', 'edge-scaling=9'), ('random', '--vary', 'adaptivity=0'),
                     ('oat', '--only', 'colour'), ('grid', '--vary', 'adaptivity=0',
                                                   '--timeout', '0')):
            with self.subTest(args=args):
                self.assertEqual(self.run_main(*args)[0], 2)
        self.cli = os.path.join(self.dir, 'missing-cli')
        code, _, err = self.run_main('grid', '--vary', 'adaptivity=0')
        self.assertEqual(code, 1)
        self.assertIn('missing-cli', err)
        self.assertFalse(os.path.exists(self.out))

    def test_command_line(self):
        p = subprocess.run([sys.executable, SCRIPT, 'oat', '--dry-run'],
                           capture_output=True, text=True)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(len(p.stdout.strip().split('\n')), 17)
        p = subprocess.run([sys.executable, SCRIPT, 'grid'], capture_output=True, text=True)
        self.assertEqual(p.returncode, 2)


@unittest.skipUnless(REAL_CLI and os.path.exists(REAL_CLI) and os.path.exists(SOURCE_OBJ)
                     and os.path.exists(TARGET_OBJ) and os.path.exists(BASELINE),
                     'needs $AUTOREMESHER_CLI, source.obj, quad-remeshed.obj')
class RealCli(unittest.TestCase):
    def test_reproduces_m3s_reference_trial(self):
        # M3.2 recorded --target-quads 2764 with the other defaults as line 3 of
        # the baseline file; the harness must give the same score to every digit
        with tempfile.TemporaryDirectory() as d:
            out = os.path.join(d, 'sweep.jsonl')
            err = io.StringIO()
            with redirect_stderr(err):
                code = T.main(['grid', '--cli', REAL_CLI, '--source', SOURCE_OBJ,
                               '--target', TARGET_OBJ, '--out', out,
                               '--work', os.path.join(d, 'trials'),
                               '--vary', 'target-quads=2764', '--center', 'adaptivity=1'])
            self.assertEqual(code, 0, err.getvalue())
            (r,) = read_jsonl(out)
        self.assertEqual(r['status'], 'ok', r)
        with open(BASELINE) as f:
            ref = json.loads(f.readlines()[2])
        got = dict(r['score'])
        for k in ('candidate', 'target'):
            del got[k], ref[k]
        self.assertEqual(got, ref)
        self.assertEqual(r['cli']['output_sha256'][:8], 'bc40aff3')


if __name__ == '__main__':
    unittest.main(verbosity=2)
