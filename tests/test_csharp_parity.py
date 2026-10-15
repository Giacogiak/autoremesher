#!/usr/bin/env python3
"""
M5's parity check: the C# P/Invoke demo (csharp-demo/) and the native
autoremesher-cli, given the same input and the same parameters, must write the
same mesh. Both call the same library, so any difference is a marshaling bug
in the demo (array layout, struct packing, a lost setting), not the algorithm.

    python3 tests/test_csharp_parity.py

CTest runs this file as `test_csharp_parity`, with
  $AUTOREMESHER_CLI           the autoremesher-cli executable
  $AUTOREMESHER_CSHARP_DEMO   build/csharp-demo/.../AutoRemesherDemo.dll
  $AUTOREMESHER_CORE_LIBRARY  the libautoremesher_core.so both must load
  $DOTNET                     the dotnet host (default: dotnet on the path)
  $AUTOREMESHER_SOURCE_OBJ    source.obj, and $AUTOREMESHER_TARGET_OBJ,
                              quad-remeshed.obj (both default to the repo root's)

Each pair of outputs is compared byte for byte, then scored against each other
with M3's scorer, scripts/score_remesh.py, which must find them identical. On
source.obj, both outputs are also scored against the Rhino target, and the two
records must agree. The synthetic cases always run; the source.obj case is
skipped where the mesh is missing (*.obj is not in git).
"""

import hashlib
import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
sys.path.insert(0, os.path.join(ROOT, 'scripts'))
import score_remesh as S  # noqa: E402

CLI = os.environ.get('AUTOREMESHER_CLI', '')
DEMO = os.environ.get('AUTOREMESHER_CSHARP_DEMO', '')
LIBRARY = os.environ.get('AUTOREMESHER_CORE_LIBRARY', '')
DOTNET = os.environ.get('DOTNET') or shutil.which('dotnet') or ''
SOURCE_OBJ = os.environ.get('AUTOREMESHER_SOURCE_OBJ', os.path.join(ROOT, 'source.obj'))
TARGET_OBJ = os.environ.get('AUTOREMESHER_TARGET_OBJ', os.path.join(ROOT, 'quad-remeshed.obj'))

# M4.3's winner (HANDOFF_M4_TASK3.md), the demo's defaults. Every run passes all
# six settings, because the CLI's defaults are not the demo's.
WINNER = {'target-quads': 30000, 'edge-scaling': 1.25, 'sharp-edge': 90,
          'smooth-normal': 0, 'adaptivity': 0.5, 'anisotropy': 1}


def args_for(settings, split=False):
    a = []
    for name, value in settings.items():
        a += ['--' + name, str(value)]
    return a + (['--split-to-quads'] if split else [])


def sphere_soup(path, segments=32, rings=16):
    """test_cli.cpp's UV sphere of radius 1, as a triangle soup: every triangle
    has its own three vertices, so both callers have to weld it."""
    P = [(0.0, 1.0, 0.0)]
    for r in range(1, rings):
        t = math.pi * r / rings
        for s in range(segments):
            p = 2.0 * math.pi * s / segments
            P.append((math.sin(t) * math.cos(p), math.cos(t), math.sin(t) * math.sin(p)))
    P.append((0.0, -1.0, 0.0))
    south = len(P) - 1

    def R(r, s):
        return 1 + (r - 1) * segments + s % segments
    T = []
    for s in range(segments):
        T += [(0, R(1, s + 1), R(1, s)), (south, R(rings - 1, s), R(rings - 1, s + 1))]
    for r in range(1, rings - 1):
        for s in range(segments):
            T += [(R(r, s), R(r, s + 1), R(r + 1, s)), (R(r, s + 1), R(r + 1, s + 1), R(r + 1, s))]
    with open(path, 'w') as f:
        for k, tri in enumerate(T):
            for i in tri:
                f.write('v %r %r %r\n' % P[i])
            f.write('f %d %d %d\n' % (3 * k + 1, 3 * k + 2, 3 * k + 3))


def quad_box(path, n=8):
    """Tests.cs's 2 x 1 x 1 box, as shared vertices and quad faces with negative
    indices and 'v x y z 0 0 0' lines (quad-remeshed.obj's style), so both
    readers fan-triangulate polygons rather than read triangles."""
    with open(path, 'w') as f:
        def side(o, u, w, nu, nw):
            for j in range(nw + 1):
                for i in range(nu + 1):
                    f.write('v %r %r %r 0 0 0\n' % tuple(o[k] + u[k] * i / nu + w[k] * j / nw
                                                         for k in range(3)))
            count = (nu + 1) * (nw + 1)
            for j in range(nw):
                for i in range(nu):
                    a = j * (nu + 1) + i
                    corners = (a, a + 1, a + nu + 2, a + nu + 1)
                    f.write('f ' + ' '.join(str(c - count) for c in corners) + '\n')
        o, x, y, z = (0, 0, 0), (2, 0, 0), (0, 1, 0), (0, 0, 1)
        side(o, y, x, n, 2 * n)
        side(z, x, y, 2 * n, n)
        side(o, x, z, 2 * n, n)
        side(y, z, x, n, 2 * n)
        side(o, z, y, n, n)
        side(x, y, z, n, n)


def sha256(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


def strip_paths(record):
    return {k: v for k, v in record.items() if k not in ('candidate', 'target')}


# Skipped where the paths are not given; where they are (CTest), a wrong one fails.
@unittest.skipUnless(CLI and DEMO and LIBRARY,
                     'needs $AUTOREMESHER_CLI, $AUTOREMESHER_CSHARP_DEMO and '
                     '$AUTOREMESHER_CORE_LIBRARY')
class Parity(unittest.TestCase):
    maxDiff = None

    def setUp(self):
        for what, path in (('autoremesher-cli', CLI), ('the demo', DEMO), ('the library', LIBRARY)):
            self.assertTrue(os.path.exists(path), what + ' not found: ' + path)
        self.assertTrue(DOTNET, 'dotnet not found')
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def path(self, name):
        return os.path.join(self.tmp.name, name)

    def run_both(self, name, input_path, args):
        """Runs the CLI and the demo on the same input with the same arguments;
        returns the two output paths and the demo's stdout."""
        cli_out, demo_out = self.path(name + '_cli.obj'), self.path(name + '_csharp.obj')
        env = dict(os.environ, AUTOREMESHER_CORE_LIBRARY=LIBRARY,
                   DOTNET_CLI_TELEMETRY_OPTOUT='1', DOTNET_NOLOGO='1')
        cli = subprocess.run([CLI, '-i', input_path, '-o', cli_out] + args,
                             capture_output=True, text=True, env=env)
        self.assertEqual(cli.returncode, 0, name + ': autoremesher-cli: ' + cli.stderr)
        demo = subprocess.run([DOTNET, DEMO, '-i', input_path, '-o', demo_out] + args,
                              capture_output=True, text=True, env=env)
        self.assertEqual(demo.returncode, 0, name + ': demo: ' + demo.stderr)
        # The demo loaded the library under test, and says so.
        self.assertIn('library: ' + os.path.realpath(LIBRARY) + '\n', demo.stdout, name)
        return cli_out, demo_out, demo.stdout

    def assert_parity(self, name, cli_out, demo_out):
        """M3's scorer, which must see one mesh twice, then byte equality. The
        scorer runs first so that a divergence is reported by its size."""
        problems = []
        for candidate, target in ((demo_out, cli_out), (cli_out, demo_out)):
            r = S.score(candidate, target)
            c, t = os.path.basename(candidate), os.path.basename(target)
            for field in ('quads', 'non_quad_faces', 'vertices', 'edge_length_cv',
                          'degenerate_quads'):
                if r[field]['candidate'] != r[field]['target']:
                    problems.append('%s vs %s: %s %r' % (c, t, field, r[field]))
            for direction in ('fit', 'fit_reverse'):
                stats = r[direction]
                if stats['max'] != 0.0 or stats['samples'] != r['vertices'][
                        'candidate' if direction == 'fit' else 'target']:
                    problems.append('%s vs %s: %s %r' % (c, t, direction, stats))
        self.assertEqual(problems, [], name + ': M3\'s scorer tells the outputs apart')
        self.assertEqual(sha256(demo_out), sha256(cli_out),
                         name + ': the demo and the CLI wrote different files')

    def test_both_load_the_same_library(self):
        # The CLI links the library by its soname; check it resolves to the
        # file the demo is given.
        ldd = subprocess.run(['ldd', CLI], capture_output=True, text=True)
        self.assertEqual(ldd.returncode, 0, ldd.stderr)
        lines = [l for l in ldd.stdout.splitlines() if 'libautoremesher_core' in l]
        self.assertEqual(len(lines), 1, ldd.stdout)
        self.assertEqual(os.path.realpath(lines[0].split('=>')[1].split('(')[0].strip()),
                         os.path.realpath(LIBRARY))

    def test_sphere_soup(self):
        src = self.path('sphere.obj')
        sphere_soup(src)
        cases = {
            'winner': args_for(dict(WINNER, **{'target-quads': 300})),
            # test_cli.cpp's settings: every flag away from the core's default
            'all_moved': args_for({'target-quads': 300, 'edge-scaling': 1.5, 'sharp-edge': 60,
                                   'smooth-normal': 10, 'adaptivity': 0.5, 'anisotropy': 0.5}),
            'split': args_for(dict(WINNER, **{'target-quads': 300}), split=True),
        }
        for name, args in cases.items():
            with self.subTest(name):
                cli_out, demo_out, _ = self.run_both('sphere_' + name, src, args)
                self.assert_parity('sphere ' + name, cli_out, demo_out)

    def test_quad_box(self):
        src = self.path('box.obj')
        quad_box(src)
        winner = dict(WINNER, **{'target-quads': 300})
        cases = {'winner': args_for(winner), 'split': args_for(winner, split=True)}
        # One case per setting moved away from the winner, as in Tests.cs's
        # EverySetterTakesEffect: a setting lost on one side would show here.
        for name, value in (('target-quads', 500), ('edge-scaling', 1.5), ('sharp-edge', 60),
                            ('smooth-normal', 30), ('adaptivity', 1), ('anisotropy', 0.5)):
            cases[name] = args_for(dict(winner, **{name: value}))
        outputs = {}
        for name, args in cases.items():
            with self.subTest(name):
                cli_out, demo_out, _ = self.run_both('box_' + name, src, args)
                self.assert_parity('box ' + name, cli_out, demo_out)
                outputs[name] = sha256(cli_out)
        # Every moved setting changes the output, so the cases above are distinct.
        self.assertEqual(len(set(outputs.values())), len(outputs), outputs)

    @unittest.skipUnless(os.path.exists(SOURCE_OBJ), 'needs ' + SOURCE_OBJ)
    def test_source_obj_at_m4_winner(self):
        cli_out, demo_out, printed = self.run_both('source', SOURCE_OBJ, args_for(WINNER))
        self.assertIn('output sha256: ' + sha256(demo_out) + '\n', printed)
        self.assert_parity('source.obj', cli_out, demo_out)
        if not os.path.exists(TARGET_OBJ):
            self.skipTest('needs ' + TARGET_OBJ + ' for the scores against the target')
        # Both outputs against the Rhino target: one and the same record.
        cli_score = S.score(cli_out, TARGET_OBJ)
        demo_score = S.score(demo_out, TARGET_OBJ)
        self.assertEqual(strip_paths(demo_score), strip_paths(cli_score))
        self.assertGreater(cli_score['fit']['max'], 0.0)   # a real comparison, not a self-score
        print('\nsource.obj at M4.3\'s winner: %d quads, %d non-quads, %d vertices, '
              'fit mean %.6g, fit_reverse mean %.6g, sha256 %s'
              % (demo_score['quads']['candidate'], demo_score['non_quad_faces']['candidate'],
                 demo_score['vertices']['candidate'], demo_score['fit']['mean'],
                 demo_score['fit_reverse']['mean'], sha256(demo_out)), file=sys.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
