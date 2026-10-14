#!/usr/bin/env python3
"""
Unit tests for scripts/rank_trials.py (M4.2), D5's ranking of the sweep's
trials, on synthetic records, plus a check that the committed ranking
(tuning_runs/ranking.json) is what the script makes of the committed sweep.

    python3 tests/test_rank_trials.py

CTest runs this file as `test_rank_trials`.
"""

import io
import json
import os
import re
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
SCRIPT = os.path.join(ROOT, 'scripts', 'rank_trials.py')
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import rank_trials as R  # noqa: E402

SWEEP = os.path.join(ROOT, 'tuning_runs', 'sweep_results.jsonl')
RANKING = os.path.join(ROOT, 'tuning_runs', 'ranking.json')
BASELINE = os.path.join(ROOT, 'plan', 'baselines', 'm3_baseline.jsonl')

TARGET_QUADS = 11054
TARGET_CV = 0.5551583668863462


def score(quads, fit, fit_rev, cv, degenerate=0):
    """A scorer record (score_remesh.py, scorer_version 1) with the fields D5 uses"""
    def dist(mean):
        return {'samples': 100, 'mean': mean, 'rms': mean, 'p95': mean, 'max': mean,
                'mean_rel': mean, 'rms_rel': mean, 'p95_rel': mean, 'max_rel': mean}
    return {
        'scorer_version': 1, 'candidate': 'c.obj', 'target': 't.obj',
        'quads': {'candidate': quads, 'target': TARGET_QUADS, 'delta': quads - TARGET_QUADS,
                  'delta_pct': 100.0 * (quads - TARGET_QUADS) / TARGET_QUADS},
        'non_quad_faces': {'candidate': 0, 'target': 0},
        'vertices': {'candidate': quads, 'target': 11082},
        'edge_length_cv': {'candidate': cv, 'target': TARGET_CV, 'delta': cv - TARGET_CV},
        'target_bbox_diagonal': 219.3,
        'fit': dist(fit), 'fit_reverse': dist(fit_rev),
        'degenerate_quads': {'candidate': degenerate, 'target': 0, 'delta': degenerate}}


def rec(trial, quads, fit, fit_rev, cv=0.45, degenerate=0, status='ok'):
    """A tune_params.py record (tuner_version 1)"""
    ok = status == 'ok'
    return {
        'tuner_version': 1, 'trial': trial, 'sweep': 'grid', 'label': None,
        'params': {'target_quads': 2764, 'edge_scaling': 1.0, 'sharp_edge': 90.0,
                   'smooth_normal': 0.0, 'adaptivity': 1.0, 'anisotropy': 1.0},
        'status': status, 'error': None if ok else 'failed',
        'source': 'source.obj', 'source_sha256': 'ab',
        'cli': {'argv': [], 'exit_code': 0 if ok else 1, 'wall_s': 1.0, 'timeout_s': 600.0,
                'output_sha256': 'cd' if ok else None},
        'score_wall_s': 1.0 if ok else None,
        'score': score(quads, fit, fit_rev, cv, degenerate) if ok else None,
        'time': '2026-10-14T12:00:00'}


def write_jsonl(path, records):
    with open(path, 'w') as f:
        for r in records:
            f.write(json.dumps(r) + '\n')


def run_main(argv):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = R.main(argv)
    return code, out.getvalue(), err.getvalue()


class Metrics(unittest.TestCase):
    def test_combined_fit_is_the_mean_of_both_directions(self):
        self.assertAlmostEqual(R.combined_fit(score(11054, 0.2, 0.6, 0.5)), 0.4)

    def test_cv_distance_is_closeness_to_the_targets_cv(self):
        # D5: scored by closeness to the target's own CV, not by minimising it
        self.assertAlmostEqual(R.cv_distance(score(11054, 1, 1, 0.40)), TARGET_CV - 0.40)
        self.assertAlmostEqual(R.cv_distance(score(11054, 1, 1, 0.60)), 0.60 - TARGET_CV)


class Rank(unittest.TestCase):
    def test_ranks_survivors_by_combined_fit(self):
        records = [rec('a', 11054, 0.5, 0.5), rec('b', 11500, 0.1, 0.3),
                   rec('c', 10500, 0.3, 0.3)]
        ranked, excluded = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['b', 'c', 'a'])
        self.assertEqual([e['rank'] for e in ranked], [1, 2, 3])
        self.assertEqual(excluded, [])
        self.assertAlmostEqual(ranked[0]['combined_fit'], 0.2)

    def test_degenerate_quads_disqualify_whatever_the_fit(self):
        records = [rec('best_but_degenerate', 11054, 0.01, 0.01, degenerate=1),
                   rec('worse', 11054, 0.5, 0.5)]
        ranked, excluded = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['worse'])
        self.assertEqual([(e['trial'], e['reason']) for e in excluded],
                         [('best_but_degenerate', 'degenerate')])

    def test_degenerate_is_checked_before_the_gate(self):
        _, excluded = R.rank([rec('x', 50000, 0.1, 0.1, degenerate=3)], band_pct=15.0)
        self.assertEqual(excluded[0]['reason'], 'degenerate')

    def test_gate_on_quad_count(self):
        # the densest mesh fits best; the gate is what keeps it out (D5)
        lo = int(TARGET_QUADS * 0.85) + 1   # inside
        hi = int(TARGET_QUADS * 1.15)       # inside
        records = [rec('dense', 104000, 0.07, 0.05), rec('lo', lo, 0.4, 0.4),
                   rec('hi', hi, 0.3, 0.3), rec('too_few', lo - 2, 0.2, 0.2),
                   rec('too_many', hi + 2, 0.2, 0.2)]
        ranked, excluded = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['hi', 'lo'])
        self.assertEqual(sorted((e['trial'], e['reason']) for e in excluded),
                         [('dense', 'outside_band'), ('too_few', 'outside_band'),
                          ('too_many', 'outside_band')])

    def test_band_edge_is_inside(self):
        r = rec('edge', 12000, 0.3, 0.3)
        r['score']['quads']['delta_pct'] = 15.0
        ranked, _ = R.rank([r], band_pct=15.0)
        self.assertEqual(len(ranked), 1)

    def test_band_width_changes_the_survivors(self):
        records = [rec('a', 11054, 0.5, 0.5), rec('b', 13000, 0.2, 0.2)]   # b is +17.6 %
        self.assertEqual([e['trial'] for e in R.rank(records, band_pct=15.0)[0]], ['a'])
        self.assertEqual([e['trial'] for e in R.rank(records, band_pct=20.0)[0]], ['b', 'a'])

    def test_failed_trials_are_excluded(self):
        records = [rec('t', 0, 0, 0, status='timeout'), rec('ok', 11054, 0.5, 0.5),
                   rec('f', 0, 0, 0, status='cli_failed')]
        ranked, excluded = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['ok'])
        self.assertEqual([(e['trial'], e['reason']) for e in excluded],
                         [('t', 'timeout'), ('f', 'cli_failed')])

    def test_tie_break_by_closeness_to_the_targets_cv(self):
        # equal fit: 0.60 is 0.045 from the target's 0.555, 0.40 is 0.155 away, so
        # 0.60 ranks first although 0.40 is the lower CV
        records = [rec('low_cv', 11054, 0.3, 0.3, cv=0.40),
                   rec('near_cv', 11054, 0.3, 0.3, cv=0.60)]
        ranked, _ = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['near_cv', 'low_cv'])
        ranked, _ = R.rank(list(reversed(records)), band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['near_cv', 'low_cv'])

    def test_cv_only_breaks_ties(self):
        records = [rec('near_cv', 11054, 0.31, 0.31, cv=0.555),
                   rec('better_fit', 11054, 0.30, 0.30, cv=0.30)]
        ranked, _ = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['better_fit', 'near_cv'])

    def test_fit_decimals_turns_near_equal_fits_into_ties(self):
        records = [rec('a', 11054, 0.2101, 0.2101, cv=0.40),
                   rec('b', 11054, 0.2104, 0.2104, cv=0.55)]
        ranked, _ = R.rank(records, band_pct=15.0)
        self.assertEqual([e['trial'] for e in ranked], ['a', 'b'])
        ranked, _ = R.rank(records, band_pct=15.0, fit_decimals=3)
        self.assertEqual([e['trial'] for e in ranked], ['b', 'a'])

    def test_a_trial_recorded_twice_is_ranked_once(self):
        # tune_params.py --rerun appends a second record of the same trial; the
        # last ok one counts
        records = [rec('a', 11054, 0.5, 0.5), rec('a', 11054, 0.4, 0.4),
                   rec('a', 0, 0, 0, status='timeout'), rec('b', 11054, 0.45, 0.45)]
        ranked, excluded = R.rank(records, band_pct=15.0)
        self.assertEqual([(e['trial'], e['combined_fit']) for e in ranked],
                         [('a', 0.4), ('b', 0.45)])
        self.assertEqual(excluded, [])

    def test_no_survivors(self):
        ranked, excluded = R.rank([rec('far', 20000, 0.1, 0.1)], band_pct=15.0)
        self.assertEqual(ranked, [])
        self.assertEqual(len(excluded), 1)

    def test_band_needed(self):
        records = [rec('a', 20000, 0.1, 0.1), rec('b', 8000, 0.1, 0.1),
                   rec('c', 11054, 0.1, 0.1, degenerate=1)]
        # c does not count: degenerate quads disqualify it at any band
        self.assertAlmostEqual(R.band_needed(records),
                               100.0 * (TARGET_QUADS - 8000) / TARGET_QUADS)
        self.assertIsNone(R.band_needed([rec('c', 11054, 0.1, 0.1, degenerate=1)]))


class Baseline(unittest.TestCase):
    def test_gate_verdict_and_ratio(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, 'b.jsonl')
            write_jsonl(path, [score(11054, 0, 0, TARGET_CV), score(104000, 0.07, 0.05, 0.37),
                               score(11526, 0.6, 1.8, 0.45)])
            dense = R.baseline_entry(R.load_baseline(path + ':2'), 15.0)
            ref = R.baseline_entry(R.load_baseline(path + ':3'), 15.0)
            with self.assertRaises(ValueError):
                R.load_baseline(path + ':4')
        self.assertEqual((dense['file'], dense['line'], dense['quads']), (path, 2, 104000))
        self.assertEqual(dense['gate'], 'outside_band')
        self.assertAlmostEqual(dense['combined_fit'], 0.06)
        self.assertEqual(ref['gate'], None)
        self.assertAlmostEqual(ref['combined_fit'], 1.2)

    def test_bad_spec(self):
        for bad in ('nofile', 'x.jsonl:0', 'x.jsonl:a'):
            with self.assertRaises(ValueError, msg=bad):
                R.parse_baseline_spec(bad)


class Cli(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.sweep = os.path.join(self.d.name, 'sweep.jsonl')
        write_jsonl(self.sweep, [rec('a', 11054, 0.5, 0.5), rec('b', 11500, 0.1, 0.3),
                                 rec('dense', 104000, 0.07, 0.05),
                                 rec('deg', 11054, 0.01, 0.01, degenerate=2)])
        self.base = os.path.join(self.d.name, 'base.jsonl')
        write_jsonl(self.base, [score(11054, 0, 0, TARGET_CV), score(104000, 0.07, 0.05, 0.37)])

    def tearDown(self):
        self.d.cleanup()

    def test_table_and_json(self):
        js = os.path.join(self.d.name, 'ranking.json')
        code, out, err = run_main(['--results', self.sweep, '--band', '15', '--top', '1',
                                   '--json', js, '--baseline', self.base + ':2'])
        self.assertEqual(code, 0, err)
        self.assertRegex(out, re.compile(r'^\s*1\s+b\s', re.M))
        self.assertNotRegex(out, re.compile(r'^\s*2\s+a\s', re.M))   # --top 1
        self.assertIn('--target-quads 2764 --edge-scaling 1 --sharp-edge 90', out)
        self.assertIn('outside_band', out)
        with open(js) as f:
            j = json.load(f)
        self.assertEqual(j['ranker_version'], R.RANKER_VERSION)
        self.assertEqual(j['band_pct'], 15.0)
        self.assertIsNone(j['fit_decimals'])
        self.assertEqual(j['counts'], {'records': 4, 'trials': 4, 'not_ok': 0,
                                       'degenerate': 1, 'outside_band': 1, 'ranked': 2})
        # the JSON keeps the whole ranking, not just --top
        self.assertEqual([e['trial'] for e in j['ranked']], ['b', 'a'])
        self.assertEqual(j['ranked'][0]['cli_flags'],
                         '--target-quads 2764 --edge-scaling 1 --sharp-edge 90 '
                         '--smooth-normal 0 --adaptivity 1 --anisotropy 1')
        self.assertEqual(sorted((e['trial'], e['reason']) for e in j['excluded']),
                         [('deg', 'degenerate'), ('dense', 'outside_band')])
        (b,) = j['baselines']
        self.assertEqual((b['line'], b['gate']), (2, 'outside_band'))
        self.assertAlmostEqual(b['combined_fit'], 0.06)

    def test_json_is_deterministic(self):
        a, b = (os.path.join(self.d.name, n) for n in ('a.json', 'b.json'))
        for path in (a, b):
            self.assertEqual(run_main(['--results', self.sweep, '--json', path])[0], 0)
        with open(a) as fa, open(b) as fb:
            self.assertEqual(fa.read(), fb.read())

    def test_no_survivors_is_an_error_not_an_ungated_ranking(self):
        far = os.path.join(self.d.name, 'far.jsonl')
        write_jsonl(far, [rec('x', 20000, 0.1, 0.1), rec('y', 9000, 0.2, 0.2)])
        js = os.path.join(self.d.name, 'r.json')
        code, out, err = run_main(['--results', far, '--band', '15', '--json', js])
        self.assertEqual(code, 3)
        self.assertEqual(out, '')
        self.assertFalse(os.path.exists(js))
        self.assertIn('no trial survives', err)
        self.assertIn('18.6', err)   # the band that would let y in: (11054-9000)/11054

    def test_missing_results(self):
        code, _, err = run_main(['--results', os.path.join(self.d.name, 'nope.jsonl')])
        self.assertEqual(code, 1)
        self.assertIn('nope.jsonl', err)

    def test_usage_errors(self):
        for argv in (['--band', '0'], ['--band', '-5'], ['--band', 'x'], ['--top', '0'],
                     ['--fit-decimals', '-1'], ['--baseline', 'nofile']):
            p = subprocess.run([sys.executable, SCRIPT, '--results', self.sweep] + argv,
                               capture_output=True, text=True)
            self.assertEqual(p.returncode, 2, argv)


@unittest.skipUnless(os.path.exists(SWEEP) and os.path.exists(RANKING),
                     'needs tuning_runs/sweep_results.jsonl and tuning_runs/ranking.json')
class Committed(unittest.TestCase):
    def test_ranking_json_is_up_to_date(self):
        # tuning_runs/ranking.json is the ranking HANDOFF_M4_TASK2.md reports; it
        # must be what this script makes of the committed sweep, with the options
        # it records
        with open(RANKING) as f:
            committed = json.load(f)
        argv = ['--results', os.path.relpath(SWEEP, ROOT),
                '--band', repr(committed['band_pct'])]
        for b in committed['baselines']:
            argv += ['--baseline', '%s:%d' % (b['file'], b['line'])]
        with tempfile.TemporaryDirectory() as d:
            js = os.path.join(d, 'ranking.json')
            cwd = os.getcwd()
            os.chdir(ROOT)
            try:
                code, _, err = run_main(argv + ['--json', js])
            finally:
                os.chdir(cwd)
            self.assertEqual(code, 0, err)
            with open(js) as f:
                fresh = json.load(f)
        self.assertEqual(fresh, committed)

    def test_winner(self):
        with open(SWEEP) as f:
            records = [json.loads(line) for line in f if line.strip()]
        ranked, _ = R.rank(records, band_pct=15.0)
        self.assertEqual(ranked[0]['trial'], 'tq65000_es3.5_se120_sn0_ad1_an1')

    @unittest.skipUnless(os.path.exists(BASELINE), 'needs plan/baselines/m3_baseline.jsonl')
    def test_winner_beats_the_defaults_at_the_targets_quad_count(self):
        # M4's exit criterion 1, as Giacomo read it for M4.2: the winner's combined
        # fit must be materially (here: at least 2x) better than M3's reference, the
        # CLI's defaults at the target's quad count (--target-quads 2764, line 3)
        with open(SWEEP) as f:
            records = [json.loads(line) for line in f if line.strip()]
        ranked, _ = R.rank(records, band_pct=15.0)
        ref = R.baseline_entry(R.load_baseline(BASELINE + ':3'), 15.0)
        self.assertEqual(ref['gate'], None)
        self.assertEqual(ref['quads'], 11526)
        self.assertLess(ranked[0]['combined_fit'], ref['combined_fit'] / 2)
        self.assertLess(ranked[0]['fit_mean'], ref['fit_mean'])
        self.assertLess(ranked[0]['fit_reverse_mean'], ref['fit_reverse_mean'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
