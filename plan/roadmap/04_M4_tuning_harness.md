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

## Task 3 — rerun the sweep and ranking on the unsplit CLI output (D6 reversed)

M4.1/M4.2's sweep and ranking ran against the CLI's old split-to-quads
default. Giacomo visually reviewed M4.2's winner against `quad-remeshed.obj`
and judged it messy, overly dense, and deformed in several zones — despite
it scoring best on every one of M3's numeric metrics (mean, rms, p95, max
fit) across all 90 trials swept. The metrics did not catch this; see D6 in
`plan/01_decisions.md`. With the split reverted (M2's Task 4) and M3
rebaselined (M3's Task 3), rerun both:

- **Re-run `tune_params.py`** against the natural (unsplit) CLI output.
  Re-centre `--target-quads` back on the target's actual 11,054 quads — the
  ≈4× split-compensation centring from M4.1 no longer applies. Use ranges
  close to this file's original suggestion, {8000, 9500, 11054, 12500,
  14000}, and treat M4.1's edge-scaling/sharp-edge findings as provisional
  only: they were measured under the split and may not hold without it.
  Re-derive which parameters matter most from fresh one-at-a-time trials
  before committing to a combined grid.
- **Re-run `rank_trials.py`** with the same D5 procedure (degenerate
  disqualification, quad-count gate, fit-primary ranking, CV tie-break to
  the target's value), but report each surviving trial's p95 and max fit
  alongside its mean in the ranking output, so a reviewer can see the spread
  before opening a viewer.
- **Mandatory visual check, up front this time:** before reporting this
  task done, set `needsHumanCheck: true` and name what to look for —
  compare the new winner's edge flow and overall shape against
  `quad-remeshed.obj`, not just its numeric score. M4.2's winner won on
  every metric and still failed visual review, so this task must not claim
  success on numbers alone.
- Record in the handoff: the new winner and its parameters, how it compares
  to the old (reverted) winner and to M3's new unsplit baseline, and the
  p95/max spread across the gated survivors.

> **Done in M4.3** (`HANDOFF_M4_TASK3.md`), pending Giacomo's visual check.
> `tune_params.py` is re-centred on target-quads 11054 and writes to
> `tuning_runs/unsplit/`; `rank_trials.py` (ranker_version 2) reports each
> trial's p95/max fit and non-quad count, and their spread over the survivors.
> 127 trials, all ok, none degenerate; ranking `tuning_runs/unsplit/ranking.json`
> at ±15 % (75 survive; the winner is the same from ±10 % to ±20 %). Winner:
> `tq30000_es1.25_se90_sn0_ad0.5_an1`, 11,065 quads (+0.1 %), combined fit
> 0.0694. **M4.1's findings did not carry over:** unsplit, the core undershoots
> its target (11054 gives 7,913 quads; the defaults reach the target's count at
> about 15,500), and edge-scaling helps only up to about 1.25–1.5 at a matched
> count — the fit is flat from 1.25 to 2 (0.069–0.071), against 0.21 for M4.2's
> es 3.5 split winner. M4.2's winner scores 2.2–4× worse than this one on every
> fit statistic (3.0× on combined fit), which matches Giacomo's visual verdict.

> **Visually confirmed (Giacomo Forcina, 2026-10-15):** the winner is good —
> clean, close to the input surface, only a couple of face-connectivity
> errors. Accepted as this plan's milestone result; M5 builds on it.
>
> **Scope note, same review:** the Rhino target isn't just a quad remesh —
> it reads as a feature-aware *retopology*, a geometric synthesis of the
> model at a deliberately minimal quad density, the kind of edge flow a
> human modeler would draw by hand. AutoRemesher's algorithm doesn't attempt
> that; it remeshes to quads without re-interpreting the model's features
> (consistent with M4.3 Open #4: no trial reproduced the target's concentric
> loops around holes). Closing that gap would mean a different algorithm,
> not a parameter choice, and is explicitly **out of this plan's scope**.
> This plan's goal — tune AutoRemesher's own algorithm as close to the
> target as it can go — is met by the winner above.

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

  > **Superseded by D6's reversal (2026-10-15):** M4.1/M4.2 ran against the
  > split output and are kept for the record, but do not satisfy this
  > criterion going forward. Task 3 reruns the sweep against the unsplit
  > output and its reference baseline is M3's Task 3 rebaseline, not the old
  > line 3. A trial does not count as meeting this criterion until a human
  > has visually compared it against `quad-remeshed.obj` — M4.2's prior
  > winner met the numeric criterion but failed that comparison.

  > **M4.3 measured:** M3.3's rebaseline records the defaults at
  > `--target-quads 11054` (`m3_baseline_unsplit.jsonl` line 3), but unsplit
  > that lands at 7,913 quads (−28.4 %), outside D5's gate. The like-for-like
  > reference is therefore the sweep's defaults-only trial nearest the
  > target's count, `tq15500_es1_se90_sn0_ad1_an1` (10,901 quads, −1.4 %,
  > combined fit 0.0974). The winner is 0.71× that (29 % lower; `fit` −21 %,
  > `fit_reverse` −34 %, both p95s lower) and 0.50× line 3. CTest
  > (`test_rank_trials`) checks both. The numeric part holds; the visual
  > comparison is Giacomo's (`HANDOFF_M4_TASK3.md`, "Visual check").
- The winning parameter set is recorded in this phase's handoff for M5 to
  consume directly.
