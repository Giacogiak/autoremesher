# M3 — Score a candidate quad mesh against the Rhino target

## Task 1 — `scripts/score_remesh.py`

A Python script taking two OBJ paths (candidate, target) and reporting, as one
JSON line (so results accumulate into a `.jsonl` file across many invocations,
one per tuning trial in M4):

- quad count and its percent delta from the target's 11,054 quads;
- edge-length coefficient of variation (stddev/mean over every quad edge) for
  candidate vs. target, and their difference — a proxy for how uniform the
  remesh is, independent of absolute scale;
- a spatial-fit metric: for a sample of candidate vertices (or face
  centroids), nearest-point distance to the target mesh's surface (both
  derive from the same `source.obj`, so they already share one coordinate
  frame — no registration/ICP step needed, just nearest-neighbor distance,
  e.g. via a KD-tree over target vertices or triangulated target faces);
- degenerate-quad count (any face that revisits a vertex, i.e. fewer than 4
  distinct indices — mirrors how the sibling Instant Meshes project's own
  scorer treated this case).

> **Implemented in M3.1** (`HANDOFF_M3_TASK1.md`): `score_remesh.py CANDIDATE
> TARGET [--out runs.jsonl] [--samples N] [--seed S]`. The spatial fit measures
> every referenced candidate vertex to the exact nearest point of the target's
> triangulated quads (not just the nearest target vertex). It also reports the
> reverse direction, `fit_reverse` (target vertices to the candidate's surface),
> which this list does not ask for: a candidate covering only part of the
> target scores 0 one way. The target's quad count is read from the file, not
> hard-coded. Tested by CTest `test_score_remesh`.

## Task 2 — sanity checks

Running the script on `quad-remeshed.obj` scored against itself must report
zero delta on every metric (self-score sanity check). Running it on M2's CLI
output (default parameters) against `quad-remeshed.obj` establishes the
baseline score this plan is trying to improve on in M4.

> **Done in M3.2** (`HANDOFF_M3_TASK2.md`). The self-score is exactly zero on
> every field, and CTest `test_score_remesh` now checks every field of it, not
> a hand-picked list. The baseline (CLI defaults: 104,000 quads, +840.8 %;
> edge CV 0.3681, Δ −0.1871; fit mean 0.0734, p95 0.233; fit_reverse mean
> 0.0491; 0 degenerate) is in the handoff, with full-precision records in
> `plan/baselines/m3_baseline.jsonl`: line 1 the self-score, line 2 the
> defaults baseline, line 3 `--target-quads 2764` for reference.

## Task 3 — rebaseline against the unsplit CLI output (D6 reversed)

M2's Task 4 reverts the CLI's default from the split (all-quad) output back
to the natural polygon mesh. M3.2's baseline (`plan/baselines/m3_baseline.jsonl`
line 2) was measured on the **split** mesh and no longer reflects what the
CLI produces by default — re-run the self-score sanity check and the
default-parameter baseline exactly as M3.2 did, this time against the
unsplit output, and record both.

Also confirm the degenerate-quad rule ("fewer than 4 distinct indices") does
not miscount a valid triangle or pentagon as degenerate — it should not, since
a triangle/pentagon has `3`/`5` distinct indices, none repeated, but add an
explicit test case covering it (M3.1/M3.2 only ever ran inputs where this
happened to come out at 0, not a test that pins it). If it does miscount,
fix it.

## Exit criteria

- Self-score sanity check passes (all deltas zero).
- A documented baseline score exists for the CLI's default-parameter output,
  recorded in this phase's handoff, to compare M4's tuned result against.

  > **Superseded by D6's reversal (2026-10-15):** the baseline this phase
  > must produce is now against the CLI's unsplit default, per Task 3 above
  > — M3.2's split-mesh baseline is kept for the record but is no longer the
  > one M4 compares against.
