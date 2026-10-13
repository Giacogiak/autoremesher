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

## Task 2 — sanity checks

Running the script on `quad-remeshed.obj` scored against itself must report
zero delta on every metric (self-score sanity check). Running it on M2's CLI
output (default parameters) against `quad-remeshed.obj` establishes the
baseline score this plan is trying to improve on in M4.

## Exit criteria

- Self-score sanity check passes (all deltas zero).
- A documented baseline score exists for the CLI's default-parameter output,
  recorded in this phase's handoff, to compare M4's tuned result against.
