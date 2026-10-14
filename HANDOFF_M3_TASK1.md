# Handoff — M3 Task 1: `scripts/score_remesh.py`

Unit `M3.1`, plan `plan/roadmap/03_M3_scoring_script.md`, section "Task 1". Branch `wrapper`.

**Status: done.** The scorer exists, is tested by a new CTest test, and gives an all-zero
self-score on `quad-remeshed.obj`. Task 2 (the sanity checks, and recording the baseline) is
the next unit's job. The numbers below for CLI outputs are measurements of the script, not the
official baseline.

## What changed and why

| File | Change |
|---|---|
| `scripts/score_remesh.py` (new) | The scorer, described below. It is executable and needs only numpy and scipy. |
| `tests/test_score_remesh.py` (new) | `unittest` tests, 26 of them (pytest is not installed). They run on synthetic meshes whose metrics are known by hand, and on a self-score of `quad-remeshed.obj` where the file exists. |
| `CMakeLists.txt` | Adds CTest `test_score_remesh`, which runs the test file with `find_package(Python3 COMPONENTS Interpreter)`'s interpreter. `AUTOREMESHER_TARGET_OBJ` points it at `quad-remeshed.obj`. If no interpreter is found, the test is not added and CMake prints a warning. |
| `.gitignore` | Adds `__pycache__/`. The test imports the script, which writes `scripts/__pycache__/` and would otherwise leave the tree dirty after every CI run. |
| `plan/roadmap/03_M3_scoring_script.md` | A note under Task 1: the command line, the exact-surface fit, the extra `fit_reverse`, and that the target's quad count is read from the file. No criterion changed. |

### Command line

```
scripts/score_remesh.py CANDIDATE.obj TARGET.obj [--out runs.jsonl] [--samples N] [--seed S]
```

- **Output.** It prints exactly one JSON line on stdout. With `--out`, it also appends the same
  line to that file, so `>>` and `--out` both accumulate a `.jsonl` file.
- **Exit codes.** 0 on success. 1 if a mesh cannot be read or parsed, or `--out` cannot be
  written; the message goes to stderr and stdout stays empty. 2 for a usage error (argparse),
  including a negative `--samples`.
- **Sampling.** `--samples 0`, the default, measures from every vertex that a face uses.
  `--samples N` picks N of them at random with `--seed`, the same way on every run.
- **As a module.** `score(candidate_path, target_path, samples=0, seed=0)` returns the record as
  a dict. M4 can import it rather than start a process per trial, although each call reloads
  both meshes.

### The record

Every metric is reported for both meshes and the delta is candidate − target. The fields are:

| Field | Meaning |
|---|---|
| `scorer_version` | 1. Bump it when a field changes meaning. |
| `candidate`, `target` | The paths as given. |
| `quads` | `candidate`, `target`, `delta`, `delta_pct` = 100 · delta / target. |
| `non_quad_faces` | Faces with any other number of corners. 0 for both the CLI output (D6) and the Rhino target. |
| `vertices` | All `v` records, used or not. |
| `edge_length_cv` | stddev / mean of the lengths of the **distinct undirected quad edges**: a shared edge counts once, edges of non-quad faces are left out, and an edge from a vertex to itself (a repeated corner) is left out, because `degenerate_quads` counts that quad. Population stddev (numpy's default). `null` if there are no edges. |
| `target_bbox_diagonal` | The diagonal of the bounding box of the target's used vertices. |
| `fit` | Distance from each candidate vertex to the nearest point of the target's surface. Stats: `samples`, `mean`, `rms`, `p95`, `max`, in OBJ units, and the same four divided by the target's diagonal (`*_rel`). |
| `fit_reverse` | The same, from each target vertex to the candidate's surface. |
| `degenerate_quads` | Quads with fewer than 4 distinct indices: the plan's definition. |

### Decisions within the task

- **Exact nearest point on the surface, not the nearest target vertex.** The plan allowed
  either. Against the nearest vertex alone, a candidate vertex lying in the middle of a target
  face would score its distance to that face's corner. With the target's edges around 1–2 units,
  that error would be larger than the fit itself.
  - **The surface.** Each target quad becomes two triangles, by a fan from corner 0.
  - **The distance.** Ericson's closest-point-on-triangle, vectorised in numpy. A point in a
    corner's region gets that corner **exactly**, so a mesh's own vertices are at distance
    exactly 0.0 from it. That is why the self-score is exactly zero, with no tolerance.
  - **Degenerate triangles.** Collinear or coincident corners fall back to the nearest of the
    triangle's three edges and three corners.
  - **The search.** A KD-tree over the triangle centroids gives 16 candidates per point. Then
    every point whose answer the 16 do not settle (best > 16th centroid distance − the largest
    centroid-to-corner radius) is checked against every triangle in range, so the result is
    exact, not approximate. This is the scheme of the sibling Instant Meshes scorer, rewritten
    without trimesh.
- **`fit_reverse` was added; the plan does not ask for it.** `fit` alone cannot see a candidate
  that covers only part of the target: every vertex it has may lie on the target, so it scores
  0. The reverse direction catches that. It also comes out 0 on the self-score. D5 decides
  whether it is used.
- **Only used vertices are measured.** A stray unreferenced vertex is not part of the surface.
  Both meshes here use all their vertices.
- **The target's quad count is read from the target file.** It is not hard-coded as 11,054, so
  the script works for any pair of meshes.
- **Dependencies: numpy and scipy only.** trimesh is installed here too, but nothing needs it.
- **Non-quad faces.** They are counted, and left out of the quad count, the edge CV and the
  degenerate count. Their triangles still belong to the surface that the distances are measured
  to.

## Measured

Python 3.14.8, numpy 2.5.4 and scipy 1.18.1, on 2026-10-14. These are the same on the host and in
the toolbox (see Open #1). `quad-remeshed.obj` has 11,082 vertices and 11,054 faces, all quads.

- **The driver's CI command, after `rm -rf build`, at `f05250c8`:** exit **0** in 68 s wall.
  **11/11** tests pass. `test_score_remesh` takes 4.70 s and `test_core` 36.93 s; the other nine
  match M2.3's times.
  - CMake found `/usr/bin/python3.14`.
  - `ctest -V` shows `RhinoTarget.test_self_score ... ok`, so the real-file self-score ran in
    CTest and was not skipped.
- **Red first.**
  - **Against a stub** whose functions raise `NotImplementedError`: `FAILED (failures=3,
    errors=22)` out of 25.
  - **Mutations caught at once.** Disabling a corner region of the closest-point code fails 5
    tests. Checking only the first sorted pair in `degenerate_quads` fails 1.
  - **Two mutations first survived, so I added a test for each:**
    1. Turning off the exhaustive pass of the KD-tree search. The new test
       `test_nearest_triangle_beyond_the_first_k` now fails on that mutation.
    2. Keeping self-edges in the edge CV. `test_degenerate_quads` now checks the CV too, and
       fails on it.
- **Cross-check against an independent implementation:** the sibling project's
  `SurfaceDistance`, which uses trimesh's closest-point code, and its `quad_quality`. On
  `t2764.obj` and `default.obj` below, the per-vertex distances agree to within 1.6e-14. The
  edge CV and degenerate counts are identical.
- **Runs on real meshes**, scored against `quad-remeshed.obj` on the host with
  `/usr/bin/time`. The CLI outputs came from `build/autoremesher-cli -i source.obj` (the CLI's code is
  unchanged since `cad93b43`), with `DISPLAY` and `WAYLAND_DISPLAY` unset, written to `/tmp/m31/`.

| candidate | quads (Δ %) | edge CV (target 0.5552) | fit mean / p95 / max | fit_reverse mean / p95 / max | degenerate | wall, max RSS |
|---|---|---|---|---|---|---|
| `quad-remeshed.obj` itself | 11,054 (0 %) | 0.5552 (Δ 0.0) | 0 / 0 / 0 | 0 / 0 / 0 | 0 | 1.8 s, 514 MB |
| `--target-quads 2764` | 11,526 (+4.27 %) | 0.4552 (Δ −0.1000) | 0.573 / 1.662 / 2.624 | 1.780 / 5.558 / 11.289 | 0 | 2.6 s, 747 MB |
| `--target-quads 11054` | 32,495 (+193.97 %) | 0.4101 (Δ −0.1451) | 0.132 / 0.533 / 1.940 | 0.172 / 0.672 / 4.562 | 0 | 3.3 s, 815 MB |
| defaults | 104,000 (+840.84 %) | 0.3681 (Δ −0.1871) | 0.0734 / 0.233 / 1.202 | 0.0491 / 0.159 / 0.699 | 0 | 8.0 s, 892 MB |

- **The self-score is exactly zero** on every delta and every distance: `0`, `0.0`, with no
  tolerance.
- **Units.** Distances are in OBJ units, and the target's bounding-box diagonal is 219.305. So
  the defaults' mean fit is 3.3e-4 of the diagonal, and the 2,764 run's is 2.6e-3.
- **The CLI runs' quad and vertex counts match M2.3's.**
- **The defaults row is the command Task 2 is meant to record as the baseline.** The next unit
  should re-run it and record it there. I have not written it into the plan as the baseline.

## What I got wrong on the way

- **The first test run had one error, and it was in the test helper, not the script.**
  `write_obj` formatted `np.float64` values with `%r`, which writes `np.float64(0.408…)` into
  the OBJ. The test now converts the values to `float` first.
- **My first scoring runs on the real meshes used `python3 -I`, and failed with "No module named
  numpy".** numpy and scipy are installed in the user site-packages (`~/.local`), which `-I`
  ignores. I re-ran without `-I`; the script and the meshes are the repository's own.
- **My first test set let two mutations survive** (above). Both are now covered.

## M3 exit criteria (phase-wide): status after this unit

- [ ] **Self-score sanity check passes (all deltas zero).** This is Task 2's to record. It
  already holds: see the table, and CTest `test_score_remesh` checks it on every run where
  `quad-remeshed.obj` exists.
- [ ] **A documented baseline score exists for the CLI's default-parameter output.** Still
  open. That is Task 2's job.

## Open

1. **`test_score_remesh` relies on numpy and scipy from `~/.local`.**
   - **Where they are.** They are installed for Python 3.14 in
     `/home/giak/.local/lib/python3.14/site-packages`. The toolbox shares the home directory,
     so the CI command finds them.
   - **If they are removed**, or the toolbox's Python changes minor version, the test fails with
     an `ImportError`. It does not skip.
   - **No pinned requirements file** was added; the plan does not ask for one.
2. **What the metrics reward is for D5 to settle. Three things to know:**
   - **The edge CV is far from uniform in the target: 0.555.** Every CLI output is more uniform
     than that (0.37–0.46), so the delta is negative. The weighting has to choose between "match
     the target's CV" (|delta| → 0) and "be as uniform as possible" (the candidate's CV → 0).
     These two rank trials differently.
   - **The fit is measured to the target, not to `source.obj`.** The target is a coarse quad
     mesh, so its faces cut chords across curved parts of the source. A dense candidate that
     follows the source closely still sits a little off the target's chords. So the fit
     improves with density (defaults 0.073 against 0.573 at 2,764), but not to zero, and part
     of what remains is the target's own error.
   - **The quad-count delta and the fit pull in opposite directions** along `--target-quads`.
     The table shows it: the trial closest in count (2,764, +4 %) is the worst on fit.
3. **`fit_reverse` is an addition the plan did not ask for.** It is cheap, and a self-score
   gives 0. If Giacomo does not want it, it is one line in `score()` plus the tests.
4. **Memory.** About 0.9 GB of peak RSS on the 104,000-quad output, which is mostly the
   vectorised closest-point arrays. That is fine for M4 trials run one at a time, but not for
   many trials scored in parallel. `--samples` cuts it.
5. **The target triangulation is a fixed diagonal choice** (corner 0 to corner 2). The Rhino
   quads are not exactly planar, so the other diagonal gives a slightly different surface. The
   effect on the fit is on the order of the target's non-planarity. It was not measured.
6. **Unchanged from earlier units:** `source.obj` and `quad-remeshed.obj` are not in git
   (`*.obj` is ignored), so the real-file checks skip on a fresh clone.

## For the next unit (M3.2, sanity checks)

- **The self-score:** `scripts/score_remesh.py quad-remeshed.obj quad-remeshed.obj` prints
  `"delta": 0`, `"delta_pct": 0.0`, the CV `"delta": 0.0`, every `fit` and `fit_reverse` stat
  `0.0`, and degenerate `"delta": 0`. It takes about 1.8 s.
- **The baseline:** run the CLI with no parameters
  (`build/autoremesher-cli -i source.obj -o default.obj`, about 37 s), then score the output
  against `quad-remeshed.obj` (8 s). My run gave the defaults row above. The CLI's output is
  deterministic (M2.3), so the numbers should repeat exactly.
- **Also worth recording:** a baseline at `--target-quads 2764`, whose quad count is close to
  the target's (M2.3's suggestion). It is in the table too.
- **The CI command** now has 11 tests and takes about 68 s from clean.
