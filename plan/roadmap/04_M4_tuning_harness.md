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

> **Done in M4.1** (`HANDOFF_M4_TASK1.md`): `scripts/tune_params.py
> {oat,grid,random,list}`, tested by CTest `test_tune_params`. It ran 90
> trials into `tuning_runs/sweep_results.jsonl`: one-at-a-time around the
> centre above, then a combined grid over edge-scaling × target-quads ×
> sharp-edge. All 90 came back ok, and none has degenerate quads.
> **The ≈2,760 centre holds only at `--edge-scaling 1`.** Edge-scaling coarsens
> the quads, so a larger target-quads has to make up for it. At a matched
> quad count, a higher edge-scaling fits much better: combined fit ≈ 1.18 at
> es 1, ≈ 0.21 at es 3–3.5, with target-quads 50,000–95,000. A one-at-a-time
> sweep at fixed target-quads hides this, because there edge-scaling only
> looks like it removes quads. The plan's sharp-edge set {60…150} is not the
> CLI's full range (30–180); 30 and 45 were swept too.

## Task 2 — ranking

A ranking step (`scripts/rank_trials.py`, or inline in the sweep script) that
combines M3's separate metrics into one ordering and reports the top N trials,
per **D5** (`plan/01_decisions.md`), decided 2026-10-14:

1. **Disqualify** any trial with a nonzero degenerate-quad count.
2. **Gate on quad count:** discard any surviving trial whose quad count falls
   outside a tolerance band around the target's 11,054 quads. Pick the exact
   band (±15–20% suggested) from the sweep's actual quad-count spread —
   record whatever value is chosen and why.
3. **Rank the survivors by combined spatial fit** — `fit` and `fit_reverse`
   from M3's scorer (mean, or another single statistic of them; record which
   and why), lower is better.
4. **Tie-break by edge-length CV**, scored by closeness to the *target's*
   CV (0.5552), not by minimizing it — a trial whose CV is further from
   0.5552 ranks worse, even if its CV is numerically lower.

If the quad-count gate leaves zero survivors, widen it and say so in the
handoff rather than silently falling back to ungated fit-ranking (ungated
fit-ranking just picks the densest mesh in the sweep — this is exactly what
D5 exists to prevent; see `HANDOFF_M3_TASK2.md` Open #1 for the measurement
that motivated it).

> **Done in M4.2** (`HANDOFF_M4_TASK2.md`): `scripts/rank_trials.py`, tested by
> CTest `test_rank_trials`; the whole ranking is committed as
> `tuning_runs/ranking.json`. Band **±15 %** (54 of the 90 trials survive; the
> winner is the same for any band from ±10.2 % to ±17.5 %). Statistic: the
> **mean of `fit.mean` and `fit_reverse.mean`** (the same order as D5's sum; the
> winner is also first by rms, p95 and max). CV ties are exact unless
> `--fit-decimals` is given; no tie occurred. Winner:
> `tq65000_es3.5_se120_sn0_ad1_an1`, combined fit 0.2098 at 12,180 quads
> (+10.2 %), 5.6× better than the default parameters at the target's quad
> count (exit criterion 1, as reworded below).

## Exit criteria

- A ranked list of trials exists, with the best trial's combined score
  materially better than the default parameters at the target's quad count:
  M3's reference record, `plan/baselines/m3_baseline.jsonl` line 3
  (`--target-quads 2764`, every other parameter at its default).

  > **Reworded in M4.2** (Giacomo Forcina, 2026-10-14). It used to say "M3's
  > documented default-parameter baseline". That baseline (line 2, 104,000
  > quads, +841 %) falls outside D5's quad-count gate, so it is not a fair
  > comparison: on fit alone it beats every trial near the target's count. See
  > `HANDOFF_M4_TASK2.md`.
- The winning parameter set is recorded in this phase's handoff for M5 to
  consume directly.
