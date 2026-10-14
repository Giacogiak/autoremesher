# Handoff — M3 Task 3: rebaseline against the unsplit CLI output (D6 reversed)

Unit `M3.3`, plan `plan/roadmap/03_M3_scoring_script.md`, section "Task 3". Branch `wrapper`.
This is now the last unit of M3.

**Status: done.** The self-score check and the CLI's default-parameter baseline were rerun the
way M3.2 ran them, this time on the CLI's unsplit output. Both are recorded below and in
`plan/baselines/m3_baseline_unsplit.jsonl`. The degenerate-quad rule was already correct: it
looks only at 4-gons. A new test now pins that a valid triangle, pentagon, hexagon or 7-gon is
never counted as degenerate. Both M3 exit criteria hold (evidence at the end).

The code, data and plan changes are commit `517a182c`. This handoff is committed separately.

## What changed and why

| File | Change |
|---|---|
| `plan/baselines/m3_baseline_unsplit.jsonl` (new) | The scorer's full-precision records. Line 1: the self-score of `quad-remeshed.obj`. Line 2: the **baseline** (CLI defaults, unsplit). Line 3: `--target-quads 11054`, for reference. Line 4: the unsplit default output scored against itself. |
| `tests/test_score_remesh.py` | Two new tests (30 tests now, up from 28). See below. No existing test was edited. |
| `tests/test_tune_params.py` | A new `RealCli.test_reproduces_m3s_unsplit_reference_trial`, which runs the real CLI unwrapped. The old split test keeps its wrapper; only its comment changed. See below. |
| `scripts/score_remesh.py` | Docstrings only: `degenerate_quads` tests 4-gons only, so a valid non-quad never counts. No behaviour change. |
| `plan/roadmap/03_M3_scoring_script.md` | A "Done in M3.3" note under Task 3, and a "Met in M3.3" line under exit criterion 2. No criterion changed. |

### Decision taken: a new baseline file, not a rewrite of M3.2's

M2.4's handoff suggested rewriting line 3 of `m3_baseline.jsonl` and dropping the
`test_tune_params` wrapper. I left that file **byte for byte unchanged** and wrote the unsplit
records to a new file with the same layout, for three reasons:

- **The plan says to keep it.** M3.2's split baseline "is kept for the record".
- **Other code reads it.** `test_rank_trials.test_winner_beats_the_defaults_at_the_targets_quad_count`
  reads `m3_baseline.jsonl:3`. `tuning_runs/ranking.json` embeds `:2` and `:3`, and
  `test_rank_trials` regenerates that ranking and compares it with the committed file. Rewriting
  the lines would break M4.2's recorded results, which are M4.3's to redo, not mine.
- **The wrapped split test is still useful.** It shows that `--split-to-quads` reproduces
  M3.2's record to every digit (sha prefix `bc40aff3`). That is the only end-to-end check of
  the split path against a real mesh.

**Line 3 is `--target-quads 11054`, not 2764.** M3.2's line 3 was 2764 because it centred the
split output on the target. Without the split, M4's plan (Task 3) re-centres on the target's
own 11,054. So line 3 is the centre trial M4.3 will run, with every other parameter at its
default.

### The test changes

- **`Metrics.test_degenerate_rule_spares_valid_polygons`.** The input is a 3×1 quad grid plus
  four valid non-quads: a triangle, a pentagon, a hexagon and a 7-gon. None of them repeats
  an index. The test checks:
  - 4 non-quads, 3 quads, and `degenerate_quads` equal to `{0, 0, 0}`;
  - with one quad then made degenerate, the count is exactly 1. So the rule still fires next to
    non-quads, and only on the quad.
- **`Metrics.test_self_score_with_polygons_is_zero`.** A mesh with a triangle and a hexagon,
  scored against itself, must give an empty `self_score_mismatches`. This is the synthetic
  version of line 4.
- **Red check.** A temporary copy of the scorer counted *every* face with fewer than 4 distinct
  indices (the miscount the plan asks about). Run against that copy:
  - **both** new tests fail (2 failures out of 30);
  - **no** older test fails.

  So until this unit, nothing pinned the rule. The real scorer is unchanged and passes.
- **`RealCli.test_reproduces_m3s_unsplit_reference_trial`.** `tune_params.py grid --vary
  target-quads=11054` runs the real CLI with no wrapper. The test checks:
  - its score equals line 3 of the new file to every digit (only the paths are left out);
  - `non_quad_faces.candidate` is 171, so the run really is unsplit;
  - the output sha256 starts with `7bfeaed5`.

  It adds about 16 s to `test_tune_params`, which now takes 25.8 s instead of 10.1 s. Its skip
  condition is the class's existing one, plus the new file existing.

## Measured

Commit `517a182c`, 2026-10-14. The CLI was built Release by the CI command in the toolbox
`instant-meshes-build`. It ran on the host with `DISPLAY`, `WAYLAND_DISPLAY` and
`QT_QPA_PLATFORM` unset. Scoring ran on the host: Python 3.14.3, numpy 2.5.4, scipy 1.18.1.
`--samples` was 0, so every vertex was measured.

Input hashes are unchanged from M3.2:

- `source.obj`: sha256 `3e2e44ee…b617c2`
- `quad-remeshed.obj`: sha256 `1a7c51f3…5dbba1`. It has 11,054 quads, 11,082 vertices, and its
  bounding-box diagonal is 219.305.

```
build/autoremesher-cli -i source.obj -o /tmp/m33/default.obj --report /tmp/m33/default.txt
build/autoremesher-cli -i source.obj -o /tmp/m33/t11054.obj --target-quads 11054 --report /tmp/m33/t11054.txt
F=plan/baselines/m3_baseline_unsplit.jsonl
scripts/score_remesh.py quad-remeshed.obj quad-remeshed.obj --out $F        # line 1
scripts/score_remesh.py /tmp/m33/default.obj quad-remeshed.obj --out $F     # line 2
scripts/score_remesh.py /tmp/m33/t11054.obj quad-remeshed.obj --out $F      # line 3
scripts/score_remesh.py /tmp/m33/default.obj /tmp/m33/default.obj --out $F  # line 4
```

**The two CLI runs ran at the same time**, so their wall times are a little high:

- **Defaults:** 38.8 s, 675 MB max RSS. M2.4 measured 36.9 s alone.
- **11,054:** 18.4 s, 288 MB. M2.4 measured 15.1 s alone.

**Both outputs match M2.4's.** The 11,054 output's sha256 is `7bfeaed5…77026a`, the same as
M2.4's. The defaults output's is `14305187…ecc474`; M2.4 recorded only that output's counts
and bounding box, and they agree.

### Exit criterion 1: the self-score is zero

- **`quad-remeshed.obj` against itself (line 1).** 1.69 s, 514 MB.
  - Every delta and all eight distance stats in `fit` and `fit_reverse` are exactly `0`/`0.0`.
  - `self_score_mismatches` returns `[]`, and `fit` measured all 11,082 vertices.
  - The line is **byte-identical to M3.2's line 1**; `diff` printed nothing. The target and the
    scorer are unchanged, so this is expected.
- **The unsplit default output against itself (line 4).** 2.70 s, 599 MB. This is new: it is the
  first real-mesh self-score with non-quads in it.
  - Quads 25,932 / 25,932; `non_quad_faces` 56 / 56; vertices 25,920 / 25,920.
  - Edge CV 0.36908 on both sides, Δ 0.0.
  - `fit` and `fit_reverse` measured 25,920 samples each, and all 16 distance stats are `0.0`.
  - Degenerate quads 0 / 0.
  - `self_score_mismatches` returns `[]`.
- **CI coverage.** CTest checks line 1's case on every run where `quad-remeshed.obj` exists
  (`RhinoTarget.test_self_score*`; it is not skipped, see `ctest -V` below). It checks line 4's
  case synthetically (`test_self_score_with_polygons_is_zero`).

### Exit criterion 2: the baseline (line 2)

**This is the CLI's default-parameter output, unsplit**, against `quad-remeshed.obj`. The report
gives the parameters: target quads 50000, edge scaling 1, sharp edge 90, smooth normal 0,
adaptivity 1, anisotropy 1. Scoring took 2.75 s and 813 MB.

| metric | candidate | target | delta |
|---|---|---|---|
| quads | 25,932 | 11,054 | +14,878 (**+134.59 %**) |
| non-quad faces | 56 (52 pentagons, 4 triangles) | 0 | — |
| vertices | 25,920 | 11,082 | — |
| edge-length CV (quad edges only) | **0.36908** | 0.55516 | **−0.18608** |
| degenerate quads | **0** | 0 | 0 |

| distance (OBJ units; `_rel` = ÷ 219.305) | samples | mean | rms | p95 | max |
|---|---|---|---|---|---|
| `fit` (candidate → target surface) | 25,920 | **0.07676** (3.50e-4) | 0.11751 (5.36e-4) | 0.24049 (1.10e-3) | 1.20232 (5.48e-3) |
| `fit_reverse` (target → candidate surface) | 11,082 | 0.04951 (2.26e-4) | 0.07880 (3.59e-4) | 0.15935 (7.27e-4) | 0.69350 (3.16e-3) |

No face of the output repeats an index, and every vertex is used by some face.

**Compared with M3.2's split baseline** (same parameters, then split into 104,000 quads):

| | split (M3.2, `m3_baseline.jsonl:2`) | unsplit (this, `m3_baseline_unsplit.jsonl:2`) |
|---|---|---|
| quads (Δ %) | 104,000 (+840.8 %) | 25,932 (+134.6 %), plus 56 non-quads |
| edge CV | 0.36808 | 0.36908 |
| `fit` mean / p95 / max | 0.07344 / 0.23293 / 1.20232 | 0.07676 / 0.24049 / 1.20232 |
| `fit_reverse` mean / p95 / max | 0.04914 / 0.15901 / 0.69947 | 0.04951 / 0.15935 / 0.69350 |

The fit numbers barely move. The split adds vertices on the surface the core already made, so it
changes the quad count and little else. The 4× quad count was the split's main effect on the
score.

### For reference: `--target-quads 11054` (line 3)

This is the CLI with only `--target-quads 11054` changed. It is the centre M4.3 will run. It is
**not** the baseline. Scoring took 1.41 s and 396 MB.

- **Faces:** 7,913 quads (**−28.42 %**), plus 171 non-quads (165 pentagons, 6 triangles).
  8,060 vertices.
- **Edge CV:** 0.41016, Δ −0.14500. 0 degenerate quads.
- **`fit`:** mean 0.10551, rms 0.21477, p95 0.45411, max 1.90432.
- **`fit_reverse`:** mean 0.17397, rms 0.37317, p95 0.67480, max 4.46821.

### The CI command

The driver's command, run on the committed tree (`517a182c`):

- Exit **0**, 71 s wall (the build was incremental).
- **13/13** tests pass:
  - `test_core` 37.08 s, `test_tune_params` 25.83 s, `test_score_remesh` 5.83 s,
    `test_rank_trials` 0.35 s;
  - the other nine take under 0.25 s each.
- **Nothing was skipped.** In `ctest -V`, all of these report `ok`:
  - `RhinoTarget.test_self_score` and `test_self_score_every_field`;
  - both new `Metrics` tests;
  - both `RealCli` tests.

## What I got wrong on the way

Nothing needed undoing. One near-miss: I first meant to rewrite `m3_baseline.jsonl`, following
M2.4's suggestion. A grep showed that `test_rank_trials` and the committed
`tuning_runs/ranking.json` both read lines 2 and 3, so I used a new file instead (above).

## M3 exit criteria (phase-wide)

- [x] **Self-score sanity check passes (all deltas zero).** `quad-remeshed.obj` against itself
  is exactly zero on every field (line 1, identical to M3.2's). The unsplit CLI output against
  itself, non-quads included, is also exactly zero (line 4). CTest checks the first field by
  field on every run. It checks the polygon case on a synthetic mesh.
- [x] **A documented baseline score exists for the CLI's default-parameter output.** As
  superseded by D6's reversal, this must be the **unsplit** default. It is recorded above under
  "Exit criterion 2", with its conditions. The machine-readable record is
  `plan/baselines/m3_baseline_unsplit.jsonl` line 2. It can be regenerated: the CLI is
  deterministic, and line 3's run is reproduced to every digit by `test_tune_params` on every
  CI run.

## Open

1. **Which record is M4.3's reference?** This is for Giacomo and M4.3; it does not block M3.
   M4's exit criterion compares against "the default parameters at the target's quad count".
   Its D6 note says the reference is now "M3's Task 3 rebaseline".
   - **Line 2** (defaults) is +134.6 %, outside a ±15 % quad-count gate.
   - **Line 3** (`--target-quads 11054`) is −28.4 %, also outside it, because the core
     undershoots its target.
   - So neither record passes D5's gate as it stands, the same problem M4.2 had with M3.2's
     line 2. M4.3 may need a reference run at a `--target-quads` that lands within ±15 % of
     11,054. M2.4 guessed about 15,000, which has not been measured.
2. **Quad count and edge CV ignore non-quads.** `quads` counts 4-gons only, so line 3's 8,084
   faces score as 7,913 quads. `edge_length_cv` uses quad edges only. That was M3.1's
   definition, and the plan did not change it. But the unsplit output has 0.2 % non-quads at
   the defaults and 2.1 % at 11,054. Whether D5's quad-count gate should use quads or all
   faces is M4.3's question. `non_quad_faces` is in every record, so either can be computed
   from the `.jsonl`.
3. **A non-quad that repeats an index is not counted anywhere.** For example, a pentagon
   `[a, b, c, b, d]` is not a "degenerate quad" under the plan's wording, and there is no other
   counter. No CLI output measured so far has such a face (0 in both outputs here; M2.4 also
   found none at 11,054). If one ever appears, it needs a new field and a `SCORER_VERSION` bump.
4. **`/tmp` paths in the `.jsonl`.** As in M3.2, the `candidate` fields name files that are
   gone. Rerun the commands above to regenerate them.
5. **Carried over from M3.1 and M3.2, unchanged:**
   - numpy and scipy come from `~/.local`;
   - the target's triangulation uses a fixed diagonal;
   - the `.obj` inputs are not in git, so the real-file checks skip on a fresh clone.

## For the next unit (M4.3)

- **The baseline record** is `plan/baselines/m3_baseline_unsplit.jsonl` line 2:
  `json.loads(open(...).readlines()[1])`, `scorer_version` 1. `rank_trials.py --baseline` takes
  `plan/baselines/m3_baseline_unsplit.jsonl:2` (and `:3`) the same way it took M3.2's.
- **M3.2's split records are untouched** in `plan/baselines/m3_baseline.jsonl`. When M4.3
  replaces `tuning_runs/` and the M4.2 ranking, it should also move
  `test_rank_trials.test_winner_beats_the_defaults_at_the_targets_quad_count` off
  `m3_baseline.jsonl:3`. That test is tied to M4.2's split sweep.
- **The centre trial is already recorded.** `--target-quads 11054`, the other parameters at their
  defaults, is line 3. It is 7,913 quads, so the core undershoots its target by about 28 % here.
  `scripts/tune_params.py` still has `CENTER` at 2764 and `DEFAULT_SWEEP` at {2000 … 3500}, with
  the stale D6 comment; re-centring them is M4.3's work.
- **Cost per trial, unsplit:** about 37 s to remesh and 2.8 s to score at the defaults, and about
  15 s and 1.4 s at 11,054.
- **The CLI is deterministic.** `7bfeaed5…` at 11,054 has now been reproduced by M2.4, this
  unit, and CTest.
