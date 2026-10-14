# Handoff — M3 Task 2: sanity checks and the baseline

Unit `M3.2`, plan `plan/roadmap/03_M3_scoring_script.md`, section "Task 2". Branch `wrapper`.
This is the last unit of M3.

**Status: done.** The self-score of `quad-remeshed.obj` is exactly zero on every field, and
CTest now checks every field of it. The CLI's default-parameter baseline is measured and
recorded below and in `plan/baselines/m3_baseline.jsonl`. Both M3 exit criteria hold.

## What changed and why

| File | Change |
|---|---|
| `tests/test_score_remesh.py` | Adds `self_score_mismatches(r)` and two tests that use it (28 tests now, up from 26). See below. |
| `plan/baselines/m3_baseline.jsonl` (new) | The scorer's full-precision records: line 1 the self-score, line 2 the **baseline** (CLI defaults), line 3 `--target-quads 2764` for reference. M4 can read line 2 instead of copying numbers by hand. |
| `plan/roadmap/03_M3_scoring_script.md` | A "Done in M3.2" note under Task 2 that gives the baseline and points to the file. No criterion changed. |

### The test change

M3.1's self-score check, `assert_self_score`, lists fields by hand. It misses `rms_rel` and
`p95_rel` in both `fit` and `fit_reverse`. It also misses `non_quad_faces` and `vertices`, which
have no `delta` field, only `candidate` and `target`. The plan asks for "zero delta on every
metric", so I added a check that walks the record instead of listing fields:

- **`self_score_mismatches(r)`** returns every field that a self-score must have at zero and
  doesn't. That covers every `delta*` key, every `fit`/`fit_reverse` stat except `samples`, and
  `candidate != target` in any metric that has both. A field added to the record later is
  covered without editing the test.
- **`RhinoTarget.test_self_score_every_field`** checks that this list is empty for
  `quad-remeshed.obj` against itself. It also checks that `fit` measured every vertex.
- **`Metrics.test_self_score_mismatches_sees_every_metric`** checks the helper itself on a
  synthetic pair that differs in every metric. It must flag all seven metrics and all eight
  distance stats, and must not flag `fit.samples`.
- **No existing test was edited.** `assert_self_score` is unchanged.

**The new test catches a bug the old one did not.** I made `p95_rel` wrong (a constant 1e-9) in
a temporary copy of the scorer. Only `test_self_score_every_field` failed (1 failure out of 28).
M3.1's `test_self_score` and the synthetic `test_self_score_is_zero` both passed. I then
restored the scorer, which is unchanged in this unit.

## Measured

Commit `77cad833` plus the test change, 2026-10-14. On the host: Python 3.14.3, numpy 2.5.4,
scipy 1.18.1. (M3.1's handoff says 3.14.8; `sys.version` here says 3.14.3.) Input hashes:

- `source.obj`: sha256 `3e2e44ee…b617c2`
- `quad-remeshed.obj`: sha256 `1a7c51f3…5dbba1`. It has 11,054 quads, 11,082 vertices, and its
  bounding-box diagonal is 219.305.

### Exit criterion 1: the self-score is zero

`scripts/score_remesh.py quad-remeshed.obj quad-remeshed.obj` takes 1.68 s and 514 MB max RSS.

- **Quads:** 11,054 / 11,054, `delta` 0, `delta_pct` 0.0.
- **Counts:** `non_quad_faces` 0 / 0, `vertices` 11,082 / 11,082.
- **Edge CV:** 0.5551583668863462 on both sides, `delta` 0.0.
- **Distances:** `fit` and `fit_reverse` each measure 11,082 samples. All eight stats are exactly
  `0.0`: mean, rms, p95 and max, and the same four as `*_rel`.
- **Degenerate quads:** 0 / 0, `delta` 0.

That is exact zero with no tolerance. CTest checks it on every run where `quad-remeshed.obj`
exists. In `ctest -V`, `RhinoTarget.test_self_score ... ok` and
`RhinoTarget.test_self_score_every_field ... ok` both ran; neither was skipped.

### Exit criterion 2: the baseline

**The baseline is the CLI's default-parameter output** against `quad-remeshed.obj`. The
command was

```
build/autoremesher-cli -i source.obj -o /tmp/m32/default.obj --report /tmp/m32/default.txt
scripts/score_remesh.py /tmp/m32/default.obj quad-remeshed.obj --out plan/baselines/m3_baseline.jsonl
```

with `DISPLAY` and `WAYLAND_DISPLAY` unset. The CLI was built in the toolbox (Release) by the CI
command at this commit. `--samples` was 0, so every vertex was measured.

The parameters are the defaults, as the report shows them: target quads 50000, edge scaling 1,
sharp edge 90, smooth normal 0, adaptivity 1, anisotropy 1. The CLI took 37.0 s wall and
669 MB max RSS, and scoring took 8.31 s and 894 MB.

| metric | candidate | target | delta |
|---|---|---|---|
| quads | 104,000 | 11,054 | +92,946 (**+840.84 %**) |
| non-quad faces | 0 | 0 | — |
| vertices | 103,908 | 11,082 | — |
| edge-length CV | **0.36808** | 0.55516 | **−0.18708** |
| degenerate quads | **0** | 0 | 0 |

| distance (OBJ units; `_rel` = ÷ 219.305) | samples | mean | rms | p95 | max |
|---|---|---|---|---|---|
| `fit` (candidate → target surface) | 103,908 | **0.07344** (3.35e-4) | 0.11301 (5.15e-4) | 0.23293 (1.06e-3) | 1.20232 (5.48e-3) |
| `fit_reverse` (target → candidate surface) | 11,082 | 0.04914 (2.24e-4) | 0.07833 (3.57e-4) | 0.15901 (7.25e-4) | 0.69947 (3.19e-3) |

The full-precision record is line 2 of `plan/baselines/m3_baseline.jsonl`. Its `candidate`
field is `/tmp/m32/default.obj`, which no longer exists; the mesh itself is not in git, since
`*.obj` is ignored. It can be regenerated byte for byte (next point).

**It repeats exactly.** The CLI output's sha256 is `2f4a1fb8…aa74c6`, the same as M3.1's
`/tmp/m31/default.obj`. The score record's metric values match M3.1's to every digit; only the
path strings differ.

### For reference: `--target-quads 2764`

This is the CLI with only `--target-quads 2764` changed, M2.3's suggested centre for M4's
quad-count dimension. It is line 3 of the `.jsonl`, and is **not** the baseline. The CLI took
3.19 s and 108 MB, and scoring took 2.40 s and 747 MB. The output's sha256 `bc40aff3…39ce17`
equals M3.1's.

- **Quads:** 11,526, +4.27 %. 11,415 vertices.
- **Edge CV:** 0.45519, Δ −0.09997. 0 degenerate quads.
- **`fit`:** mean 0.57295, rms 0.78172, p95 1.66152, max 2.62438.
- **`fit_reverse`:** mean 1.78018, rms 2.55124, p95 5.55768, max 11.28936.

### The CI command

The driver's command, run on the working tree with the test change, before committing:

- Exit **0**, 45 s wall (the build was incremental).
- **11/11** tests pass. `test_score_remesh` took 5.79 s and `test_core` 37.04 s.

## What I got wrong on the way

Nothing that needed undoing. One note on the measurements: the first diff of my records against
M3.1's `runs.jsonl` "failed", but only because the paths and the line order differ. The metric
values are identical.

## M3 exit criteria (phase-wide)

- [x] **Self-score sanity check passes (all deltas zero).** It is exactly zero on every field of
  the record; see "Exit criterion 1" above. CTest `test_score_remesh` checks it field by field
  on every run (`test_self_score_every_field`).
- [x] **A documented baseline score exists for the CLI's default-parameter output.** It is
  recorded here, under "Exit criterion 2", with its conditions, and machine-readable in
  `plan/baselines/m3_baseline.jsonl`, line 2.

## Open

1. **The baseline is a poor match on quad count and a good one on fit.** At 104,000 quads it
   is +841 % over the target. A trial near the target's count, such as 2,764, beats it by far
   on quad count and edge CV, but has about 8× its mean fit and 36× its mean `fit_reverse`.
   Whether an M4 trial is "materially better than the baseline" (M4's exit criterion) depends
   entirely on the D5 weighting. M4 is already a stop unit for D5; this is the concrete reason
   it matters.
2. **The edge-CV direction is still D5's to settle** (M3.1 Open #2). The target's CV is 0.555,
   and every CLI output is more uniform than that. "Match the target" and "be uniform" rank
   trials differently.
3. **Carried over from M3.1, unchanged.**
   - numpy and scipy come from `~/.local`.
   - About 0.9 GB peak RSS when scoring the 104,000-quad mesh.
   - The target's triangulation uses a fixed diagonal.
   - The `.obj` inputs are not in git, so the real-file checks skip on a fresh clone.
4. **`/tmp` paths in the `.jsonl`.** They record what was run, but the files are gone. To
   reproduce them, rerun the commands above.

## For the next unit (M4)

- **The baseline record** is `plan/baselines/m3_baseline.jsonl` line 2. Read it with
  `json.loads(open(...).readlines()[1])`. Its fields are `score_remesh.py`'s, `scorer_version` 1.
- **Defaults cost about 37 s to remesh and 8 s to score.** Near the target's count
  (`--target-quads` ≈ 2,764) it is about 3 s and 2.4 s per trial.
- **The CLI is deterministic.** The same parameters give a byte-identical OBJ, confirmed
  across M3.1 and M3.2. Repeat runs of a trial are unnecessary.
- **D5 is open** and blocks M4's ranking (Task 2). See Open #1 for what the baseline numbers
  imply for it.
