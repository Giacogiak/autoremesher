# M4 — Parameter sweep and ranking

## Task 1 — `scripts/tune_params.py`

A sweep harness invoking `autoremesher-cli` once per parameter combination
(with a per-trial timeout), scoring each with M3's `score_remesh.py`, and
appending every trial's parameters + score to `tuning_runs/sweep_results.jsonl`.
Sweep dimensions and suggested ranges (`--target-quads` centered on the
target's 11,054, not the CLI's 50,000 default; the other ranges are each
parameter's full documented range from M2):

- `--target-quads`: {8000, 9500, 11054, 12500, 14000}

> **Warning from M2.3 (D6):** the CLI now splits every face of the core's
> result into quads, so the output has about **4×** as many quads as
> `--target-quads` asks: 11054 gives 32,495 quads, and 2764 gives 11,526.
> Re-centre this dimension before sweeping: about 11,054 / 4 ≈ 2,760 lands
> near the target's quad count, e.g. {2000, 2400, 2764, 3100, 3500}. The
> value written in each report's `Target quads:` line is the pre-split
> target. See `HANDOFF_M2_TASK3.md`.
- `--edge-scaling`: {1.0, 1.5, 2.0, 2.5}
- `--sharp-edge`: {60, 90, 120, 150}
- `--smooth-normal`: {0, 30, 60}
- `--adaptivity`: {0.0, 0.5, 1.0}
- `--anisotropy`: {0.0, 0.5, 1.0}

A full grid is 5×4×4×3×3×3 = 2,160 trials — likely too many at `source.obj`'s
size (97k verts). Start with a one-parameter-at-a-time sweep around the
defaults to find each parameter's individual effect, then a smaller combined
grid or random search over the most sensitive 2–3 parameters. Record which
approach was used and why in this phase's handoff.

## Task 2 — ranking

A ranking step (`scripts/rank_trials.py`, or inline in the sweep script) that
combines M3's separate metrics into one ordering and reports the top N trials.
**D5 — scoring metric weighting is an open decision**, to be decided only
after Task 1 produces real score distributions across trials (weighting
quad-count-delta vs. edge-length-CV vs. spatial-fit vs. degenerate-count
cannot be chosen sensibly before seeing how much each one actually varies
across the sweep). See `plan/01_decisions.md`.

## Exit criteria

- A ranked list of trials exists, with the best trial's combined score
  materially better than M3's documented default-parameter baseline.
- The winning parameter set is recorded in this phase's handoff for M5 to
  consume directly.
