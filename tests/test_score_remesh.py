#!/usr/bin/env python3
"""
Unit tests for scripts/score_remesh.py (M3), on small synthetic meshes whose
metrics are known by hand, plus a self-score of the Rhino target where it exists.

    python3 tests/test_score_remesh.py

CTest runs this file as `test_score_remesh`. The target mesh is taken from
$AUTOREMESHER_TARGET_OBJ, else quad-remeshed.obj at the repository root; its
checks are skipped where the file is missing (*.obj is not in git).
"""

import io
import json
import math
import os
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, '..', 'scripts', 'score_remesh.py')
sys.path.insert(0, os.path.join(HERE, '..', 'scripts'))
import score_remesh as S  # noqa: E402

TARGET_OBJ = os.environ.get('AUTOREMESHER_TARGET_OBJ',
                            os.path.join(HERE, '..', 'quad-remeshed.obj'))


def grid(nx, ny, xs=None, ys=None, z=0.0):
    """nx x ny quads in the plane at height z, as (V, faces). xs and ys are the
    grid lines' coordinates (default 0, 1, 2, ...)."""
    xs = list(range(nx + 1)) if xs is None else xs
    ys = list(range(ny + 1)) if ys is None else ys
    V = [(float(x), float(y), float(z)) for y in ys for x in xs]
    F = [[j * (nx + 1) + i, j * (nx + 1) + i + 1, (j + 1) * (nx + 1) + i + 1,
          (j + 1) * (nx + 1) + i] for j in range(ny) for i in range(nx)]
    return V, F


def write_obj(path, V, F, style='plain'):
    with open(path, 'w') as f:
        f.write('# test\n')
        for v in V:
            if style == 'rhino':        # like quad-remeshed.obj: "v x y z 0 0 0"
                f.write('v %r %r %r 0 0 0\n' % tuple(v))
            else:
                f.write('v %r %r %r\n' % tuple(v))
        if style == 'slashes':
            f.write('vt 0 0\nvn 0 0 1\n')
        for face in F:
            if style == 'slashes':
                f.write('f ' + ' '.join('%d/1/1' % (i + 1) for i in face) + '\n')
            elif style == 'negative':
                f.write('f ' + ' '.join(str(i - len(V)) for i in face) + '\n')
            else:
                f.write('f ' + ' '.join(str(i + 1) for i in face) + '\n')


class Tmp(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def obj(self, name, V, F, style='plain'):
        path = os.path.join(self.tmp.name, name)
        write_obj(path, V, F, style)
        return path


class LoadObj(Tmp):
    def test_styles_read_the_same_mesh(self):
        V, F = grid(2, 1)
        for style in ('plain', 'rhino', 'slashes', 'negative'):
            m = S.load_obj(self.obj(style + '.obj', V, F, style))
            np.testing.assert_array_equal(m.V, np.array(V))
            self.assertEqual([list(f) for f in m.faces], F, style)

    def test_mixed_face_sizes(self):
        V = [(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0), (2, 0, 0)]
        m = S.load_obj(self.obj('m.obj', V, [[0, 1, 2, 3], [1, 4, 2]]))
        self.assertEqual(list(m.sizes), [4, 3])

    def test_refusals(self):
        V = [(0, 0, 0), (1, 0, 0), (1, 1, 0)]
        with self.assertRaises(ValueError):
            S.load_obj(self.obj('noface.obj', V, []))
        with self.assertRaises(ValueError):
            S.load_obj(self.obj('range.obj', V, [[0, 1, 5]]))
        with self.assertRaises(ValueError):
            S.load_obj(self.obj('two.obj', V, [[0, 1]]))
        with self.assertRaises(OSError):
            S.load_obj(os.path.join(self.tmp.name, 'missing.obj'))


class ClosestPoint(unittest.TestCase):
    A, B, C = np.array([0., 0, 0]), np.array([2., 0, 0]), np.array([0., 2, 0])

    def closest(self, p, a=None, b=None, c=None):
        a = self.A if a is None else np.asarray(a, float)
        b = self.B if b is None else np.asarray(b, float)
        c = self.C if c is None else np.asarray(c, float)
        return S.closest_points(np.array([p], float), a[None], b[None], c[None])[0]

    def test_every_region(self):
        cases = [
            ((0.5, 0.5, 3), (0.5, 0.5, 0)),       # face
            ((-1, -1, 0), (0, 0, 0)),             # vertex A
            ((3, -1, 1), (2, 0, 0)),              # vertex B
            ((-1, 3, -1), (0, 2, 0)),             # vertex C
            ((1, -1, 0), (1, 0, 0)),              # edge AB
            ((-1, 1, 0), (0, 1, 0)),              # edge AC
            ((2, 2, 0), (1, 1, 0)),               # edge BC
        ]
        for p, want in cases:
            np.testing.assert_allclose(self.closest(p), want, atol=1e-12, err_msg=str(p))

    def test_corners_are_exact(self):
        for v in (self.A, self.B, self.C):
            self.assertTrue(np.array_equal(self.closest(v), v))

    def test_degenerate_triangles(self):
        # all three corners on a line, and two corners equal
        np.testing.assert_allclose(self.closest((1, 1, 0), (0, 0, 0), (1, 0, 0), (2, 0, 0)),
                                   (1, 0, 0), atol=1e-12)
        np.testing.assert_allclose(self.closest((3, 1, 0), (0, 0, 0), (0, 0, 0), (2, 0, 0)),
                                   (2, 0, 0), atol=1e-12)
        np.testing.assert_allclose(self.closest((5, 5, 5), (1, 1, 1), (1, 1, 1), (1, 1, 1)),
                                   (1, 1, 1), atol=1e-12)

    def test_random_against_dense_sampling(self):
        rng = np.random.default_rng(1)
        n = 300
        a, b, c = (rng.normal(size=(n, 3)) for _ in range(3))
        p = rng.normal(size=(n, 3)) * 2
        q = S.closest_points(p, a, b, c)
        d = np.linalg.norm(q - p, axis=1)
        # q lies in its triangle: barycentric coordinates are all >= 0
        for i in range(n):
            M = np.column_stack([b[i] - a[i], c[i] - a[i]])
            uv, *_ = np.linalg.lstsq(M, q[i] - a[i], rcond=None)
            self.assertGreaterEqual(uv.min(), -1e-9)
            self.assertLessEqual(uv.sum(), 1 + 1e-9)
            np.testing.assert_allclose(a[i] + M @ uv, q[i], atol=1e-9)
        # and no point of a dense barycentric sample is closer
        k = 60
        u, v = np.meshgrid(np.arange(k + 1), np.arange(k + 1))
        keep = u + v <= k
        u, v = u[keep] / k, v[keep] / k
        for i in range(n):
            pts = a[i] + np.outer(u, b[i] - a[i]) + np.outer(v, c[i] - a[i])
            self.assertLessEqual(d[i], np.linalg.norm(pts - p[i], axis=1).min() + 1e-12)


class SurfaceDistance(unittest.TestCase):
    def test_matches_brute_force(self):
        rng = np.random.default_rng(2)
        # a bumpy 20 x 20 height field with uneven triangles, and points around it
        V, F = grid(20, 20)
        V = np.array(V) + rng.normal(scale=[0.3, 0.3, 0.5], size=(len(V), 3))
        T = S.PolyMesh(V, [np.array(f) for f in F]).triangles()
        p = rng.uniform([-3, -3, -3], [23, 23, 3], size=(500, 3))
        got = S.SurfaceDistance(V, T)(p)
        tris = V[T]
        want = np.array([np.linalg.norm(
            S.closest_points(np.repeat(q[None], len(T), 0), tris[:, 0], tris[:, 1],
                             tris[:, 2]) - q, axis=1).min() for q in p])
        np.testing.assert_allclose(got, want, rtol=0, atol=1e-12)

    def test_nearest_triangle_beyond_the_first_k(self):
        # One big triangle under the point, and more than K small ones whose
        # centroids are all nearer to it than the big triangle's centroid
        big = [(-300., -300, 0), (600, -300, 0), (-300, 600, 0)]
        small = [(100. + i, 100, 50) for i in range(2 * S.SurfaceDistance.K)]
        V = big + [v for x, y, z in small
                   for v in ((x, y, z), (x + .1, y, z), (x, y + .1, z))]
        T = [[0, 1, 2]] + [[3 + 3 * i, 4 + 3 * i, 5 + 3 * i] for i in range(len(small))]
        d = S.SurfaceDistance(np.array(V), np.array(T))(np.array([[100., 100, 1]]))
        np.testing.assert_allclose(d, [1.0])

    def test_single_triangle(self):
        V = np.array([[0., 0, 0], [1, 0, 0], [0, 1, 0]])
        d = S.SurfaceDistance(V, np.array([[0, 1, 2]]))(np.array([[0.2, 0.2, -2.0]]))
        np.testing.assert_allclose(d, [2.0])


class Metrics(Tmp):
    def score(self, cand, target, **kw):
        return S.score(self.obj('c.obj', *cand), self.obj('t.obj', *target), **kw)

    def test_quad_counts(self):
        r = self.score(grid(3, 2), grid(4, 2))
        self.assertEqual(r['quads'], {'candidate': 6, 'target': 8, 'delta': -2,
                                      'delta_pct': -25.0})
        self.assertEqual(r['non_quad_faces'], {'candidate': 0, 'target': 0})
        self.assertEqual(r['vertices'], {'candidate': 12, 'target': 15})

    def test_non_quads_are_not_quads(self):
        V, F = grid(2, 1)
        V = V + [(3.0, 0.0, 0.0)]
        F = F + [[1 + 1, 6, 2 + 3]]      # a triangle on the right edge
        r = self.score((V, F), grid(2, 1))
        self.assertEqual(r['quads']['candidate'], 2)
        self.assertEqual(r['non_quad_faces']['candidate'], 1)
        self.assertEqual(r['quads']['delta_pct'], 0.0)

    def test_edge_length_cv(self):
        # 1 x 1 grid of a 1 x 3 rectangle: edges 1, 1, 3, 3 -> mean 2, std 1
        r = self.score(grid(1, 1, xs=[0, 1], ys=[0, 3]), grid(2, 2))
        cv = r['edge_length_cv']
        self.assertAlmostEqual(cv['candidate'], 0.5, places=12)
        self.assertEqual(cv['target'], 0.0)
        self.assertAlmostEqual(cv['delta'], 0.5, places=12)

    def test_edge_length_cv_counts_shared_edges_once(self):
        # 2 x 1 grid with x lines at 0, 1, 3: unique edges are
        # horizontal 1, 2 (twice each: bottom, top) and vertical 1 (three times)
        r = self.score(grid(2, 1, xs=[0, 1, 3]), grid(1, 1))
        L = np.array([1, 2, 1, 2, 1, 1, 1], float)
        self.assertAlmostEqual(r['edge_length_cv']['candidate'], L.std() / L.mean(),
                               places=12)

    def test_edge_length_cv_ignores_non_quad_edges(self):
        V, F = grid(1, 1)
        V = V + [(5.0, 0.0, 0.0)]
        F = F + [[1, 4, 3]]               # long triangle edges must not count
        r = self.score((V, F), grid(1, 1))
        self.assertEqual(r['edge_length_cv']['candidate'], 0.0)

    def test_degenerate_quads(self):
        V, F = grid(3, 1)
        F[0] = [F[0][0], F[0][1], F[0][0], F[0][3]]      # revisits a vertex
        F[2] = [F[2][0], F[2][0], F[2][2], F[2][3]]      # repeated corner
        r = self.score((V, F), grid(3, 1))
        self.assertEqual(r['degenerate_quads'], {'candidate': 2, 'target': 0, 'delta': 2})
        self.assertEqual(r['quads']['candidate'], 3)
        # the edge from the repeated corner to itself is not an edge; the
        # remaining distinct edges are seven of length 1 and one diagonal
        L = np.array([1] * 7 + [math.sqrt(2)])
        self.assertAlmostEqual(r['edge_length_cv']['candidate'], L.std() / L.mean(),
                               places=12)

    def test_degenerate_rule_spares_valid_polygons(self):
        # M3.3: the CLI's unsplit output has triangles and 5- to 7-gons. Each has
        # its 3, 5, 6 or 7 distinct indices, none repeated, so none is a
        # degenerate quad -- "fewer than 4 distinct indices" applies to quads only
        V, F = grid(3, 1)
        V = V + [(4.0, 0.0, 0.0), (4.0, 1.0, 0.0), (0.5, -1.0, 0.0), (1.5, -1.5, 0.0),
                 (2.5, -1.0, 0.0), (1.5, 2.0, 0.0), (2.5, 2.5, 0.0)]
        F = F + [[3, 8, 7],                   # triangle
                 [0, 10, 11, 12, 2, 1],       # hexagon
                 [4, 5, 6, 7, 9],             # pentagon
                 [9, 7, 6, 5, 4, 13, 14]]     # 7-gon
        r = self.score((V, F), grid(3, 1))
        self.assertEqual(r['non_quad_faces']['candidate'], 4)
        self.assertEqual(r['quads']['candidate'], 3)
        self.assertEqual(r['degenerate_quads'], {'candidate': 0, 'target': 0, 'delta': 0})
        # and a degenerate quad next to them is still counted, alone
        F[1] = [F[1][0], F[1][1], F[1][1], F[1][3]]
        r = self.score((V, F), grid(3, 1))
        self.assertEqual(r['degenerate_quads']['candidate'], 1)

    def test_self_score_with_polygons_is_zero(self):
        # the unsplit CLI output scored against itself: non-quads included
        V, F = grid(3, 1)
        V = V + [(4.0, 0.0, 0.0), (0.5, -1.0, 0.0), (1.5, -1.5, 0.0), (2.5, -1.0, 0.0)]
        F = F + [[3, 8, 7], [0, 9, 10, 11, 2, 1]]
        r = self.score((V, F), (V, F))
        self.assertEqual(self_score_mismatches(r), [])
        self.assertEqual(r['non_quad_faces'], {'candidate': 2, 'target': 2})
        self.assertEqual(r['degenerate_quads']['candidate'], 0)

    def test_fit_offset_plane(self):
        # candidate floats 0.5 above the target plane and overhangs it by 1 in x
        r = self.score(grid(3, 2, z=0.5), grid(2, 2))
        fit = r['fit']
        self.assertEqual(fit['samples'], 12)
        # 9 vertices above the target: 0.5; 3 beyond its x = 2 edge: sqrt(1 + 0.25)
        d = np.array([0.5] * 9 + [math.sqrt(1.25)] * 3)
        self.assertAlmostEqual(fit['mean'], d.mean(), places=12)
        self.assertAlmostEqual(fit['rms'], math.sqrt((d * d).mean()), places=12)
        self.assertAlmostEqual(fit['max'], math.sqrt(1.25), places=12)
        self.assertAlmostEqual(fit['p95'], np.percentile(d, 95), places=12)
        diag = math.sqrt(2 * 2 + 2 * 2)
        self.assertAlmostEqual(r['target_bbox_diagonal'], diag, places=12)
        self.assertAlmostEqual(fit['mean_rel'], d.mean() / diag, places=12)
        # back from the target's 9 vertices, all under the candidate: 0.5
        self.assertEqual(r['fit_reverse']['samples'], 9)
        self.assertAlmostEqual(r['fit_reverse']['max'], 0.5, places=12)
        self.assertAlmostEqual(r['fit_reverse']['mean'], 0.5, places=12)

    def test_fit_between_vertices(self):
        # candidate vertices on the target's faces, not its vertices: distance 0
        r = self.score(grid(4, 4, xs=[0, .5, 1, 1.5, 2], ys=[0, .5, 1, 1.5, 2]),
                       grid(2, 2))
        self.assertAlmostEqual(r['fit']['max'], 0.0, places=15)

    def test_fit_skips_unreferenced_vertices(self):
        V, F = grid(1, 1)
        r = self.score((V + [(0.0, 0.0, 100.0)], F), grid(1, 1))
        self.assertEqual(r['fit']['samples'], 4)
        self.assertEqual(r['fit']['max'], 0.0)

    def test_sampling(self):
        cand, target = grid(10, 10, z=0.25), grid(10, 10)
        r = self.score(cand, target, samples=30, seed=7)
        self.assertEqual(r['fit']['samples'], 30)
        self.assertAlmostEqual(r['fit']['max'], 0.25, places=12)
        self.assertEqual(r, self.score(cand, target, samples=30, seed=7))
        # more samples than vertices means all of them
        self.assertEqual(self.score(cand, target, samples=10 ** 6)['fit']['samples'], 121)

    def test_self_score_is_zero(self):
        rng = np.random.default_rng(3)
        V, F = grid(6, 5)
        V = [tuple(float(x) for x in v)
         for v in np.array(V) + rng.normal(scale=0.2, size=(len(V), 3))]
        F[4] = [F[4][0], F[4][1], F[4][1], F[4][3]]
        r = self.score((V, F), (V, F))
        assert_self_score(self, r)
        self.assertEqual(r['degenerate_quads']['candidate'], 1)

    def test_self_score_mismatches_sees_every_metric(self):
        V, F = grid(3, 2, xs=[0, 1, 2, 4], z=0.5)
        F = F + [[0, 1, 5], [1, 2, 2, 6]]   # a triangle and a degenerate quad
        r = self.score((V, F), grid(2, 2))
        bad = self_score_mismatches(r)
        self.assertEqual({b.split('.')[0] for b in bad},
                         {'quads', 'non_quad_faces', 'vertices', 'edge_length_cv',
                          'fit', 'fit_reverse', 'degenerate_quads'})
        for stat in ('mean', 'rms', 'p95', 'max', 'mean_rel', 'rms_rel', 'p95_rel',
                     'max_rel'):
            self.assertIn('fit.' + stat, bad)
            self.assertIn('fit_reverse.' + stat, bad)
        self.assertNotIn('fit.samples', bad)


def self_score_mismatches(r):
    """The fields of a score record that a self-score must have zero, and
    don't: every delta, every distance, and candidate == target in every
    metric that has both. Walks the record, so a field added later is covered."""
    bad = []
    for key, m in r.items():
        if not isinstance(m, dict):
            continue
        if 'candidate' in m and m['candidate'] != m['target']:
            bad.append(key + '.candidate')
        for k, v in m.items():
            if k.startswith('delta') or (key.startswith('fit') and k != 'samples'):
                if v != 0:
                    bad.append(key + '.' + k)
    return bad


def assert_self_score(test, r):
    test.assertEqual(r['quads']['delta'], 0)
    test.assertEqual(r['quads']['delta_pct'], 0.0)
    test.assertEqual(r['edge_length_cv']['delta'], 0.0)
    test.assertEqual(r['degenerate_quads']['delta'], 0)
    for key in ('fit', 'fit_reverse'):
        for stat in ('mean', 'rms', 'p95', 'max', 'mean_rel', 'max_rel'):
            test.assertEqual(r[key][stat], 0.0, key + '.' + stat)


class CommandLine(Tmp):
    def run_script(self, *args):
        return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True,
                              text=True)

    def test_one_json_line(self):
        c = self.obj('c.obj', *grid(3, 2, z=0.5))
        t = self.obj('t.obj', *grid(2, 2))
        p = self.run_script(c, t)
        self.assertEqual(p.returncode, 0, p.stderr)
        lines = p.stdout.split('\n')
        self.assertEqual(len(lines), 2)
        self.assertEqual(lines[1], '')
        r = json.loads(lines[0])
        self.assertEqual(r, json.loads(json.dumps(S.score(c, t))))
        self.assertEqual(r['candidate'], c)
        self.assertEqual(r['target'], t)

    def test_out_appends(self):
        c = self.obj('c.obj', *grid(3, 2))
        t = self.obj('t.obj', *grid(2, 2))
        out = os.path.join(self.tmp.name, 'runs.jsonl')
        for _ in range(2):
            p = self.run_script(c, t, '--out', out, '--samples', '5', '--seed', '1')
            self.assertEqual(p.returncode, 0, p.stderr)
        with open(out) as f:
            lines = f.read().split('\n')
        self.assertEqual(len(lines), 3)
        self.assertEqual(lines[0], lines[1])
        self.assertEqual(json.loads(lines[0])['fit']['samples'], 5)
        self.assertEqual(json.loads(lines[0]), json.loads(p.stdout))

    def test_failures(self):
        t = self.obj('t.obj', *grid(2, 2))
        missing = os.path.join(self.tmp.name, 'missing.obj')
        p = self.run_script(missing, t)
        self.assertEqual(p.returncode, 1)
        self.assertEqual(p.stdout, '')
        self.assertIn('missing.obj', p.stderr)
        bad = self.obj('bad.obj', [(0, 0, 0)], [[0, 1, 2]])
        self.assertEqual(self.run_script(bad, t).returncode, 1)
        self.assertEqual(self.run_script(t).returncode, 2)
        self.assertEqual(self.run_script(t, t, '--samples', '-1').returncode, 2)

    def test_main_in_process(self):
        t = self.obj('t.obj', *grid(2, 2))
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            self.assertEqual(S.main([t, t]), 0)
        assert_self_score(self, json.loads(out.getvalue()))


@unittest.skipUnless(os.path.exists(TARGET_OBJ), TARGET_OBJ + ' not found')
class RhinoTarget(unittest.TestCase):
    def test_self_score(self):
        r = S.score(TARGET_OBJ, TARGET_OBJ)
        assert_self_score(self, r)
        self.assertEqual(r['quads']['target'], 11054)
        self.assertEqual(r['non_quad_faces']['target'], 0)
        self.assertEqual(r['degenerate_quads']['target'], 0)

    def test_self_score_every_field(self):
        # M3.2's sanity check: zero delta on every metric of the record
        r = S.score(TARGET_OBJ, TARGET_OBJ)
        self.assertEqual(self_score_mismatches(r), [])
        self.assertEqual(r['fit']['samples'], r['vertices']['target'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
