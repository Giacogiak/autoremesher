# Handoff — M4 Task 3: rerun the sweep and ranking on the unsplit CLI output (D6 reversed)

Unit `M4.3`, plan `plan/roadmap/04_M4_tuning_harness.md`, section "Task 3". Branch `wrapper`.
This is the last unit of M4.

**Status: done on the numbers, waiting on Giacomo's eyes.** The sweep was rerun on the CLI's
unsplit output: 127 trials, all ok, none with degenerate quads. They were ranked under D5, with
p95 and max fit now shown next to the mean. Both are committed and CTest checks them.

- **The winner** is `tq30000_es1.25_se90_sn0_ad0.5_an1`: 11,065 quads (+0.1 %), combined fit
  **0.0694**.
- **Against M4.2's split winner** (0.2098), it is 3.0× better on combined fit and 2.2–4× better
  on every other fit statistic.
- **Against the defaults at the target's quad count** it is 0.71× (29 % lower).

**The plan requires a visual check before this result counts** (exit criterion 1's D6 note).
This unit therefore reports `needsHumanCheck: true`. What to look at is under "Visual check"
below. Read that section first.

Commits: `f9d9eeae` (scripts and tests), `4a620bee` (sweep, ranking and the CTest checks), and the
plan note with this handoff.

## Visual check (for Giacomo)

**Open these side by side** in Rhino or Blender:

| file | what it is |
|---|---|
| `quad-remeshed.obj` | the target |
| `tuning_runs/unsplit/trials/tq30000_es1.25_se90_sn0_ad0.5_an1/out.obj` | **the winner** (sha256 `fdbaf07b…62e0f2`) |
| `tuning_runs/unsplit/trials/tq50000_es1.5_se90_sn0_ad0.5_an1/out.obj` | rank 2. Its fit is only 0.00012 worse, a tie in practice; see "Ties" below. |
| `tuning_runs/unsplit/trials/tq15500_es1_se90_sn0_ad1_an1/out.obj` | the CLI's defaults at the target's quad count: the exit-criterion reference |
| `tuning_runs/trials/tq65000_es3.5_se120_sn0_ad1_an1/out.obj` | M4.2's split winner, the one you rejected |

The meshes are gitignored. If one is missing, regenerate it with
`scripts/tune_params.py grid --vary target-quads=30000 --center edge-scaling=1.25 --center adaptivity=0.5`.
The CLI is deterministic, and I re-ran the winner and got the same sha256.

**What to look for:**

1. **Overall shape.** Do the two straps on the forearm, the small strap near the hand, the
   thumb loop and the rim keep their form? Do the ~25 holes stay round, rather than turning
   into polygons? M4.2's winner failed exactly here.
2. **Edge flow.** The target has concentric edge loops around every hole, and loops running
   along the straps. Do the winner's quads follow the features, or cross them?
3. **Density.** The winner has the target's quad count (11,065 against 11,054). Check that it
   is not visibly denser in some zones and coarser in others.
4. **The 54 non-quads** (53 pentagons and 1 triangle). Where do they sit, and do they matter
   for the use M5 makes of the mesh?

**What I saw in my own renders.** These are not a substitute for your check. They are flat-shaded,
hidden-line orthographic renders from a 150-line numpy script, in
`tuning_runs/unsplit/trials/_renders/` (gitignored): `{target,winner,defaults15500,m42winner}_{top,top_zoom,bottom,side}.png`,
with non-quad faces marked by red dots. The script is `render.py` in the same directory.

- **M4.2's winner** shows what you described. The holes are hexagons, the forearm straps have
  collapsed into ridges, and the rim is jagged. **Unsplit, its parameters give 2,938 quads
  (−73 %).** It was a coarse mesh that the split brought up to 12,180 quads. Its density did
  not come from the core.
- **The winner** keeps the overall shape. The holes are round, both forearm straps are crisp
  boxes like the target's, and the thumb loop and rim follow the target.
  - **It does not reproduce the target's concentric loops around the holes.** Its quads are a
    near-uniform grid that bends around the holes. The target's ring loops look cleaner than
    this.
  - The small strap near the hand is rounder than the target's.
- **The defaults at the same count** are visibly worse than the winner. The left forearm strap
  is partly merged into the surface with a blob at its end, and the right one is wavy.
- **Irregular vertices** (interior valence ≠ 4) are one numeric proxy for edge flow. I computed
  them with `_renders/valence.py`; they are not part of the ranking.

  | mesh | irregular | valence 3 | valence 5 | valence ≥ 6 |
  |---|---|---|---|---|
  | target | 3.37 % | 10 | 350 | 5 |
  | **winner** | 3.71 % | 53 | 341 | 16 |
  | rank 2 | 3.54 % | 54 | 322 | 20 |
  | defaults at tq 15500 | 5.33 % | 166 | 382 | 33 |

  M4.2's winner scores 3.66 %, but that number means little: the split makes every vertex it
  adds regular.

## What changed and why

| File | Change |
|---|---|
| `scripts/tune_params.py` | `CENTER` is now target-quads **11054**, not 2764. `DEFAULT_SWEEP` target-quads is **{8000, 9500, 11054, 12500, 14000}**, the plan's original set. `--out` and `--work` default to **`tuning_runs/unsplit/`**, so new trials never mix with M4.1's split sweep. The stale D6 comment is replaced. |
| `scripts/rank_trials.py` | **`ranker_version` 2.** Every trial and baseline also carries `fit_p95`, `fit_max`, `fit_reverse_p95`, `fit_reverse_max` and `non_quad_faces`. The table prints them. A new `spread` block gives the min, median and max of combined fit and the four p95/max fields over the ranked trials. **None of these rank.** `DEFAULT_RESULTS` is now the unsplit sweep. |
| `tests/test_tune_params.py` | The plan's values, the trial id, the argv and the dry-run id are moved from 2764 to 11054. A new test pins the `tuning_runs/unsplit/` defaults. Details below. |
| `tests/test_rank_trials.py` | 11 new tests (36 now). Class `Spread` (5) covers the new fields. `Cli.test_table_shows_p95_and_max` covers the table. Class `CommittedUnsplit` (5) covers the committed sweep. One assertion block was added to `Cli.test_table_and_json` for the new `spread` key. |
| `.gitignore` | Adds `tuning_runs/unsplit/trials/`, which holds 68 MB of meshes, logs and the renders. |
| `tuning_runs/unsplit/sweep_results.jsonl` (new) | 127 trial records, 270 KB. |
| `tuning_runs/unsplit/ranking.json` (new) | The whole D5 ranking at ±15 %: 75 ranked, 52 excluded, M3.3's lines 2 and 3 as baselines. |
| `tuning_runs/ranking.json` | **M4.2's ranking, regenerated in the v2 format.** I checked that every v1 field is identical: the same 54 trials, order and values. Only the new fields and the `spread` block are added. `Committed.test_ranking_json_is_up_to_date` re-derives it, so a v2 script needed a v2 file. |
| `plan/roadmap/04_M4_tuning_harness.md` | A "Done in M4.3" note under Task 3, and an "M4.3 measured" note under exit criterion 1 naming the reference actually used. No criterion was reworded. |

### Decisions taken

- **A separate directory, not overwriting M4.1's files.**
  - The plan keeps M4.1/M4.2 "for the record", and `test_rank_trials.Committed` still checks
    them.
  - M4.1's `tuning_runs/trials/` holds meshes under the same trial ids as some unsplit trials,
    such as `tq65000_es3.5…`. Sharing the directory would have overwritten M4.2's winner, the
    mesh Giacomo reviewed.
- **The test edits in `test_tune_params.py` follow the plan, not convenience.**
  - `Plan.test_defaults_are_the_plans`, `test_one_at_a_time`, `test_trial_id`, `test_cli_argv`
    and `Sweep.test_dry_run` each encoded M4.1's split-compensation centre. Its comment cites
    "M2.3's D6 warning", which Task 3 tells this unit to reverse.
  - I changed the expected values first and saw 6 failures: the 5 above, plus the new defaults
    test. Then I changed the script, and they pass.
  - No check was dropped or loosened: the counts, orders and argv layout are unchanged.
- **The quad-count gate still counts 4-gons only** (M3.3 Open #2). D5 says "quad count", and
  `quads.delta_pct` is what the scorer and M4.2 use.
  - The winner has 54 non-quads, so counting all faces would put it at +0.6 % instead of
    +0.1 %. Every ranked trial has 32–132 non-quads.
  - `non_quad_faces` is now in every ranking entry, so the other reading can be computed. It
    would not change the winner at ±15 %.
- **The exit criterion's reference.** M3.3's rebaseline line 3 (the defaults at
  `--target-quads 11054`) lands at **7,913 quads (−28.4 %), outside D5's gate**. The core
  undershoots its target unsplit, which M3.3 Open #1 predicted.
  - So the like-for-like reference is the sweep's **defaults-only trial nearest the target's
    count**: `tq15500_es1_se90_sn0_ad1_an1`, every parameter at the CLI's default except
    target-quads. It has 10,901 quads (−1.4 %).
  - The winner is compared with both; see the exit criteria.

## Measured

Measured 2026-10-14, 17:25–18:15 local time, on the host. Python 3.14.3, numpy 2.5.4,
scipy 1.18.1.

- **The CLI:** `build/autoremesher-cli`, Release, built in the toolbox by the CI command at
  `b378936a`. The CLI's source has not changed since M2.4 (`81127402`).
- **No `--split-to-quads` anywhere.** CTest checks every record's argv for it.
- **The meshes:** `source.obj` (sha256 `3e2e44ee…b617c2`) and `quad-remeshed.obj` (sha256
  `1a7c51f3…5dbba1`), both unchanged.
- **Scoring:** every vertex was measured (`--samples 0`), with `scorer_version` 1.
- **Trials ran one at a time.** For a few minutes my renders shared the CPU with the sweep, so
  some `wall_s` values are a little high. That does not affect the outputs.
- **The winner is deterministic.** Re-run into `/tmp`, it gave the same sha256, and its score
  record was identical apart from the candidate path.

### Approach, and why

The plan asks for fresh one-at-a-time trials before a combined grid. M4.1's split findings were
to be treated as provisional, and they did not hold. I ran five stages, all with the `label` below
in each record:

| stage (`label`) | trials | in ±15 % | CLI s | score s |
|---|---|---|---|---|
| `M4.3 one-at-a-time (unsplit)`: centre tq 11054, the plan's values, plus tq 15500/17000 and se 30/45 | 21 | 7 | 341 | 21 |
| `M4.3 one-at-a-time at the matched-count centre (tq 15500)` | 14 | 9 | 246 | 15 |
| `M4.3 probe: edge-scaling with target-quads compensating` | 16 | 12 | 377 | 16 |
| `M4.3 combined grid: adaptivity x sharp-edge x smooth-normal x target-quads` (es 1) | 48 | 22 | 797 | 52 |
| `M4.3 combined grid: edge-scaling x target-quads (paired) x adaptivity x smooth-normal x sharp-edge` | 28 | 25 | 643 | 30 |
| **total** | **127** | **75** | **2,404** | **134** |

The slowest trial took 62.7 s (se 30). Most took 10–30 s.

**Why a second one-at-a-time stage.** At the plan's centre (tq 11054), the core makes only 7,913
quads (−28.4 %). Every parameter also moves the quad count. So the first stage mixes each
parameter's effect on quality with its effect on density, which is M4.1 Open #4 again. The
defaults reach the target's count at about tq 15,500 (10,901 quads), so I re-ran one-at-a-time
there.

### What the one-at-a-time stages showed

Combined fit is the mean of `fit.mean` and `fit_reverse.mean`, the D5 statistic M4.2 chose.

| trial | quads (Δ) | non-quads | CV | fit | fit_rev | combined | CLI s |
|---|---|---|---|---|---|---|---|
| `tq11054_es1_se90_sn0_ad1_an1` (plan centre) | 7913 (−28.4 %) | 171 | 0.4102 | 0.1055 | 0.1740 | 0.1397 | 15.0 |
| `tq8000_…` | 6317 (−42.9 %) | 132 | 0.4326 | 0.1056 | 0.2167 | 0.1612 | 10.7 |
| `tq9500_…` | 6998 (−36.7 %) | 132 | 0.4408 | 0.0788 | 0.1478 | 0.1133 | 10.1 |
| `tq12500_…` | 9282 (−16.0 %) | 137 | 0.4030 | 0.0895 | 0.1407 | 0.1151 | 16.1 |
| `tq14000_…` | 10110 (−8.5 %) | 126 | 0.4167 | 0.0881 | 0.1266 | 0.1073 | 17.4 |
| **`tq15500_es1_se90_sn0_ad1_an1`** (matched centre) | 10901 (−1.4 %) | 132 | 0.4067 | 0.0825 | 0.1123 | **0.0974** | 19.3 |
| `tq17000_…` | 12055 (+9.1 %) | 103 | 0.4131 | 0.0787 | 0.0920 | 0.0854 | 19.5 |
| `tq15500_es1.25_…` | 7107 (−35.7 %) | 142 | 0.4050 | 0.0782 | 0.1662 | 0.1222 | 15.1 |
| `tq15500_es1.5_…` | 4929 (−55.4 %) | 146 | 0.3812 | 0.0746 | 0.2218 | 0.1482 | 12.5 |
| `tq15500_es2_…` | 2974 (−73.1 %) | 140 | 0.3905 | 0.0714 | 0.5404 | 0.3059 | 11.2 |
| `tq11054_es1_se30_…` | 12098 (+9.4 %) | 497 | **1.0217** | 0.1361 | 0.2491 | 0.1926 | 62.7 |
| `tq15500_es1_se45_…` | 9053 (−18.1 %) | 213 | 0.4493 | 0.0753 | 0.1550 | 0.1151 | 27.7 |
| `tq15500_es1_se60_…` | 9190 (−16.9 %) | 152 | 0.4093 | 0.0767 | 0.1220 | 0.0994 | 21.1 |
| `tq15500_es1_se75_…` | 9446 (−14.5 %) | 160 | 0.3934 | 0.0728 | 0.1167 | 0.0947 | 18.6 |
| `tq15500_es1_se120_…` | 13034 (+17.9 %) | 100 | 0.4402 | 0.0834 | 0.0981 | 0.0907 | 19.2 |
| `tq15500_es1_se150_…` | 13336 (+20.6 %) | 100 | 0.4510 | 0.0843 | 0.0971 | 0.0907 | 20.2 |
| `tq15500_…_sn30_…` | 11136 (+0.7 %) | 112 | 0.3976 | 0.0826 | 0.1018 | 0.0922 | 19.9 |
| `tq15500_…_sn60_…` | 11226 (+1.6 %) | 110 | 0.4086 | 0.0813 | 0.1024 | 0.0918 | 19.9 |
| `tq15500_…_ad0_…` | 12834 (+16.1 %) | 32 | 0.3368 | 0.0572 | 0.1044 | 0.0808 | 9.8 |
| `tq15500_…_ad0.5_…` | 10586 (−4.2 %) | 68 | 0.3807 | 0.0701 | 0.1108 | 0.0905 | 14.9 |
| `tq15500_…_an0` | 10768 (−2.6 %) | 92 | **0.2888** | 0.0783 | 0.1181 | 0.0982 | 17.6 |
| `tq15500_…_an0.5` | 11085 (+0.3 %) | 106 | 0.3594 | 0.0792 | 0.1103 | 0.0947 | 18.7 |

The other 13 trials of stage 1, at tq 11054, are in the `.jsonl`. They all sit at −8 % to
−43 %, except se 30.

**Reading it at a matched count.** Fit falls roughly with edge length, so I compared trials by
eye at similar quad counts:

- **Edge-scaling.** At a fixed target-quads it only removes quads, as in M4.1. With target-quads
  compensating, it improves fit up to es 1.25, then levels off (third stage):

  | edge-scaling | best near the target's count | quads Δ | combined |
  |---|---|---|---|
  | 1.0 | `tq15500_es1…` (defaults) | −1.4 % | 0.0974 |
  | 1.25 | `tq27500_es1.25…` | +1.5 % | 0.0750 |
  | 1.5 | `tq50000_es1.5…` | +4.0 % | 0.0704 |
  | 1.75 | `tq75000_es1.75…` | +3.9 % | 0.0706 |
  | 2.0 | `tq100000_es2…` | −1.1 % | 0.0708 |

  M4.1 found the gain flattening at es 2.5–3.5. **Unsplit it flattens from 1.25**, and needs
  2–6× the target-quads to stay at the count. Above es 1.25, `fit` rises while `fit_reverse`
  falls, and the two cancel out.
- **Adaptivity 0.5 helps** (0.0905 at −4.2 %). **Adaptivity 0 is misleading.** Its `fit` is the
  best, but its `fit_reverse` is poor at a matched count: `tq13000_…_ad0` gives 0.0624 / 0.1305
  at +0.1 %. Its CV is the furthest from the target's (0.33–0.34).
- **Sharp-edge 75 and smooth-normal 30/60** help by a few percent. **Sharp-edge 30 breaks the
  output:** CV 1.02, 497 non-quads and 63 s. Sharp-edge 120 and 150 add density, and fit no
  better for it.
- **Anisotropy below 1 makes things worse,** and 0 pulls the CV down to 0.29. It was left at 1.

**So the combined search covered edge-scaling {1, 1.25, 1.5}, adaptivity {0, 0.5, 1},
sharp-edge {60, 75, 90} and smooth-normal {0, 30}.** Target-quads was paired with edge-scaling to
stay near the band: es 1 → {13000, 15500, 18000}, es 1.25 → {27500, 30000}, es 1.5 →
{45000, 50000}. Es 1 was crossed with all three of the others. Es 1.25/1.5 were crossed with
adaptivity {0.5, 1}, sharp-edge {75, 90} and smooth-normal {0, 30}, since adaptivity 0 and
sharp-edge 60 had already lost at es 1.

### The ranking (±15 %, top 15)

All 75 entries are in `tuning_runs/unsplit/ranking.json`. The command, from the repository root:

```
scripts/rank_trials.py --band 15 --top 15 \
  --baseline plan/baselines/m3_baseline_unsplit.jsonl:2 --baseline plan/baselines/m3_baseline_unsplit.jsonl:3 \
  --json tuning_runs/unsplit/ranking.json
```

The p95 and max columns are new. Every trial has 0 degenerate quads.

| # | trial | quads | Δ | nonq | combined | fit | fit p95 | fit max | fit_rev | rev p95 | rev max | CV | \|ΔCV\| |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | `tq30000_es1.25_se90_sn0_ad0.5_an1` | 11065 | +0.1 % | 54 | **0.0694** | 0.0653 | 0.2021 | 0.8494 | 0.0736 | 0.2378 | 1.8303 | 0.3631 | 0.1920 |
| 2 | `tq50000_es1.5_se90_sn0_ad0.5_an1` | 11176 | +1.1 % | 60 | 0.0695 | 0.0685 | 0.2111 | 1.1313 | 0.0704 | 0.2354 | 1.4436 | 0.3709 | 0.1842 |
| 3 | `tq50000_es1.5_se75_sn0_ad1_an1` | 10994 | −0.5 % | 92 | 0.0696 | 0.0711 | 0.2280 | 1.1555 | 0.0681 | 0.2309 | 1.2026 | 0.3601 | 0.1950 |
| 4 | `tq30000_es1.25_se90_sn0_ad1_an1` | 12053 | +9.0 % | 84 | 0.0698 | 0.0696 | 0.2164 | 1.0552 | 0.0699 | 0.2272 | 1.2403 | 0.3737 | 0.1815 |
| 5 | `tq50000_es1.5_se90_sn0_ad1_an1` | 11501 | +4.0 % | 100 | 0.0704 | 0.0723 | 0.2301 | 1.2227 | 0.0685 | 0.2266 | 1.3048 | 0.3714 | 0.1837 |
| 6 | `tq75000_es1.75_se90_sn0_ad1_an1` | 11484 | +3.9 % | 74 | 0.0706 | 0.0763 | 0.2408 | 0.9049 | 0.0648 | 0.2135 | 1.2806 | 0.3808 | 0.1744 |
| 7 | `tq30000_es1.25_se90_sn30_ad0.5_an1` | 11121 | +0.6 % | 76 | 0.0706 | 0.0680 | 0.2091 | 0.9644 | 0.0731 | 0.2350 | 1.3569 | 0.3631 | 0.1921 |
| 8 | `tq30000_es1.25_se75_sn0_ad0.5_an1` | 10754 | −2.7 % | 68 | 0.0706 | 0.0652 | 0.2000 | 1.0493 | 0.0760 | 0.2514 | 1.4828 | 0.3747 | 0.1804 |
| 9 | `tq100000_es2_se90_sn0_ad1_an1` | 10936 | −1.1 % | 52 | 0.0708 | 0.0779 | 0.2431 | 1.0093 | 0.0637 | 0.2058 | 1.4719 | 0.3820 | 0.1731 |
| 10 | `tq45000_es1.5_se90_sn0_ad0.5_an1` | 10352 | −6.4 % | 72 | 0.0708 | 0.0671 | 0.2101 | 0.8996 | 0.0746 | 0.2324 | 1.3010 | 0.3720 | 0.1832 |
| 11 | `tq50000_es1.5_se75_sn0_ad0.5_an1` | 10414 | −5.8 % | 68 | 0.0709 | 0.0666 | 0.2004 | 1.0987 | 0.0752 | 0.2479 | 2.6706 | 0.3712 | 0.1840 |
| 12 | `tq27500_es1.25_se90_sn0_ad0.5_an1` | 10754 | −2.7 % | 70 | 0.0721 | 0.0643 | 0.2018 | 0.9364 | 0.0800 | 0.2621 | 1.9095 | 0.3758 | 0.1793 |
| 13 | `tq30000_es1.25_se75_sn30_ad0.5_an1` | 10624 | −3.9 % | 64 | 0.0724 | 0.0676 | 0.2051 | 1.1194 | 0.0772 | 0.2531 | 1.4865 | 0.3698 | 0.1854 |
| 14 | `tq50000_es1.5_se90_sn30_ad1_an1` | 11647 | +5.4 % | 70 | 0.0725 | 0.0770 | 0.2386 | 1.0034 | 0.0680 | 0.2187 | 1.2330 | 0.3709 | 0.1843 |
| 15 | `tq27500_es1.25_se75_sn0_ad1_an1` | 10524 | −4.8 % | 82 | 0.0726 | 0.0701 | 0.2184 | 0.8643 | 0.0750 | 0.2573 | 1.0740 | 0.3682 | 0.1869 |

The top 15 are all at edge-scaling 1.25 to 2. The best survivor at edge-scaling 1 ranks 40th:
`tq18000_es1_se90_sn30_ad0.5_an1` at 0.0767.

### p95 / max spread across the 75 gated survivors

| field | min | median | max | winner |
|---|---|---|---|---|
| combined fit | 0.0694 | 0.0757 | 0.1926 | 0.0694 |
| `fit` p95 | 0.1913 | 0.2253 | 0.5569 | 0.2021 |
| `fit` max | 0.7996 | 1.0535 | 1.4908 | 0.8494 |
| `fit_reverse` p95 | 0.2058 | 0.2739 | 1.0643 | 0.2378 |
| `fit_reverse` max | 1.0740 | 1.9095 | 5.6758 | 1.8303 |

- **The winner is near the best on every statistic,** with one exception.
  - Its `fit` p95 and max are 8th and 7th of 75.
  - Its `fit_reverse` p95 is 13th of 75.
  - **The exception is its `fit_reverse` max of 1.83,** 36th of 75, at the median. Ranks 3 and
    15 have 1.20 and 1.07. One spot on the target is 1.8 units (0.8 % of the bounding-box
    diagonal) from the winner's surface. The visual check should look for it.
- **The worst survivors.** `tq11054_es1_se30…`, the broken sharp-edge 30 output, sets the
  maximum of `fit` p95 and both `fit_reverse` columns. The largest `fit` max, 1.491, is the
  defaults trial at tq 15500, the exit-criterion reference.
- **M4.2's split winner** has `fit` p95 0.697, max 3.11, and `fit_reverse` p95 0.945, max 3.97.
  - Its `fit` p95 and max are worse than every unsplit survivor's.
  - Its `fit_reverse` p95 and max are worse than all but 1 and 4 of the 75.

### Band and ties

- **Band.** ±15 % is M4.2's choice and D5's lower suggestion. 75 of 127 trials survive.
  - **The winner is first at every band I tried:** ±10, 12.5, 15, 17.5 and 20 %. It is at +0.1 %,
    so the band's upper edge cannot pick it. That was M4.2's problem (Open #2), and it is gone
    here.
- **Ties.** The top three are within 0.0003: 0.06945, 0.06946 and 0.06960.
  - At exact precision, the D5 default, the order is as above.
  - **`--fit-decimals 3`** makes ranks 1 and 2 tie, and CV then puts `tq50000_es1.5_se90_sn0_ad0.5_an1`
    first (|ΔCV| 0.184 against 0.192).
  - **`--fit-decimals 2`** puts `tq27500_es1.25_se90_sn30_ad1_an1` first, at 0.0740.
  - The smallest gap between any two neighbours is 1.2e-6.
  - **The sweep resolves a plateau, not a peak.** Rank 1 and rank 2 are the same family:
    edge-scaling 1.25–1.5, target-quads compensating, adaptivity 0.5. The visual check should
    decide between them if they look different. The committed ranking uses exact fits, as in
    M4.2.

### The winning parameter set (for M5)

**`tq30000_es1.25_se90_sn0_ad0.5_an1`**:

```
autoremesher-cli -i source.obj -o out.obj --target-quads 30000 --edge-scaling 1.25 \
  --sharp-edge 90 --smooth-normal 0 --adaptivity 0.5 --anisotropy 1
```

No `--split-to-quads`.

| parameter | value | the CLI's default | M4.2's (rejected) winner |
|---|---|---|---|
| target quads | 30000 | 50000 | 65000 |
| edge scaling | 1.25 | 1.0 | 3.5 |
| sharp edge | 90 | 90 | 120 |
| smooth normal | 0 | 0 | 0 |
| adaptivity | **0.5** | 1 | 1 |
| anisotropy | 1 | 1 | 1 |

What it produces from `source.obj`:

- **The output:** 11,050 vertices and 11,119 faces, as 11,065 quads (+0.10 %), 53 pentagons and
  1 triangle. 0 degenerate quads. Its sha256 is
  `fdbaf07b73754938822b964e64d62f389afad3b0407855e6043b15260962e0f2`, reproduced by a re-run.
- **`fit`:** mean 0.06528, rms 0.09694, p95 0.20207, max 0.84937.
- **`fit_reverse`:** mean 0.07362, rms 0.14323, p95 0.23776, max 1.83032.
- **Combined fit** 0.069449.
- **Edge CV** 0.36313: 0.19203 below the target's 0.55516, the furthest of the top 3 (see Open #3).
- **Time:** 16.8 s for the CLI (`Time: 16.754 seconds` in its log) and 1.0 s to score.
- **Topology:** 8 non-manifold edges and 0 boundary edges. The defaults at tq 15500 have 24
  non-manifold edges. The target has 0 non-manifold edges and 242 boundary edges. The core's
  output is closed where the target is open; see M2.3 Open #3.

### Against the old winner and M3's unsplit records

| record | quads | Δ | under D5 at ±15 % | combined | fit mean / p95 / max | fit_rev mean / p95 / max | winner ÷ record |
|---|---|---|---|---|---|---|---|
| **winner** | 11,065 | +0.1 % | rank 1 | **0.0694** | 0.0653 / 0.202 / 0.849 | 0.0736 / 0.238 / 1.830 | — |
| **defaults at the target's count** (`tq15500_es1_se90_sn0_ad1_an1`) | 10,901 | −1.4 % | rank 68 of 75 | 0.0974 | 0.0825 / 0.291 / 1.491 | 0.1123 / 0.408 / 3.963 | **0.713** |
| M3.3 line 3: defaults at `--target-quads 11054` (`m3_baseline_unsplit.jsonl:3`) | 7,913 | −28.4 % | excluded: outside the band | 0.1397 | 0.1055 / 0.454 / 1.904 | 0.1740 / 0.675 / 4.468 | **0.497** |
| M3.3 baseline: CLI defaults (`m3_baseline_unsplit.jsonl:2`) | 25,932 | +134.6 % | excluded: outside the band | 0.0631 | 0.0768 / 0.240 / 1.202 | 0.0495 / 0.159 / 0.693 | 1.100 (the winner is worse) |
| M4.2's winner (split, `tuning_runs/ranking.json`) | 12,180 | +10.2 % | rank 1 of the split sweep | 0.2098 | 0.1715 / 0.697 / 3.111 | 0.2480 / 0.945 / 3.969 | **0.331** |
| M4.2's winner's parameters, unsplit (`/tmp`, not committed) | 2,938 | −73.4 % | excluded: outside the band | 0.1548 | 0.0590 / — / — | 0.2506 / — / — | — |

How to read this table:

- **Line 2 beats the winner on fit only because it is 2.3× denser.** That is the case D5's gate
  exists to rule out, as with M3.2's line 2 in M4.2.
- **M4.2's winner's parameters, unsplit,** were one run into `/tmp/m43_old_unsplit.jsonl`. It
  shows how coarse that winner really was.

## M4 exit criteria (phase-wide)

- [x] **A ranked list of trials exists, with the best trial's combined score materially better
  than the default parameters at the target's quad count.** Met numerically. **The visual part
  is pending Giacomo's check,** which the bullet requires ("does not count … until a human has
  visually compared it").
  - **The ranked list** is `tuning_runs/unsplit/ranking.json`: 75 trials at ±15 %, from 127
    unsplit trials. CTest `CommittedUnsplit.test_ranking_json_is_up_to_date` re-derives it on
    every run.
  - **Against the defaults at the target's count:** 0.0694 against 0.0974, a ratio of **0.713**.
    That is 29 % lower combined fit.
    - `fit` mean is 21 % lower and `fit_reverse` mean 34 % lower.
    - `fit` p95 is 0.202 against 0.291, and `fit_reverse` p95 0.238 against 0.408.
    - Both trials are near the count: +0.1 % against −1.4 %.
    - The winner also beats every defaults-only trial in the gate, including the denser
      `tq18000` (+14.6 %, 0.0832).
    - CTest `test_winner_beats_the_defaults_at_the_targets_quad_count` requires a ratio under
      0.8, and both means and both p95s lower. **I checked it can fail:** with the threshold
      at 0.7, it does.
  - **Against M3.3's rebaseline, as the D6 note names it:** line 3 gives a ratio of **0.497**.
    Line 3 is outside the gate (−28.4 %), so this is not like for like. It is reported, and
    tested (under 0.55), because the plan names it.
  - **"Materially"** is my judgement. 29 % is far less than M4.2's 5.6×. That is because the
    unsplit defaults are already much better than the split ones were: 0.097 against 1.18 at
    the target's count. See Open #1.
- [x] **The winning parameter set is recorded in this phase's handoff for M5 to consume
  directly.** See "The winning parameter set" above: the flags, the values next to the defaults,
  the output's sha256 and every score field. It carries the same caveat: Giacomo's visual check.

### The CI command

The driver's command, run on the tree at `4a620bee`, which includes every code and data change:

- Exit **0**, 71.6 s wall (the build was incremental).
- **13/13** tests pass: `test_core` 37.29 s, `test_tune_params` 25.92 s, `test_score_remesh`
  5.86 s and `test_rank_trials` 0.41 s. The other nine take under 0.25 s each.
- **Nothing was skipped.** In `ctest -V`, both `RealCli` tests, all 3 `Committed` tests (M4.2's
  split ranking) and all 5 `CommittedUnsplit` tests report `ok`.

**Mutations.** Each was applied one at a time and reverted:

| mutation | result |
|---|---|
| the exit-criterion threshold at 0.7 instead of 0.8 | `CommittedUnsplit.test_winner_beats_…` fails |
| rank on `fit.mean` alone | 10 tests fail, including both `test_winner`s and both `test_ranking_json_is_up_to_date`s |

**Red first:**

- `test_rank_trials`: before the script changed, 5 of the 6 new `Spread` and table tests failed
  (3 errors, 2 failures), and the extended `test_table_and_json` errored.
  `test_p95_and_max_do_not_rank` passed from the start; it pins behaviour that already existed.
- `test_tune_params`: 6 failures before the script changed.
- The 5 `CommittedUnsplit` tests were written after the sweep, so they passed at once. The
  mutations above show that they can fail.

## What I got wrong on the way

- **My first table test found the wrong header line.** It looked for a line starting with
  `rank`, and `ranked by …` matched first. I fixed the test (`'rank '`) before the script had
  ever passed it.
- **A first plan note overstated a ratio.** It said M4.2's winner is "3× worse on every fit
  statistic". Checked field by field, the range is 2.2–4×. Fixed before the commit.
- **The first stage spent 13 trials at a centre 28 % under the target's count.** The plan's
  centre is now known to be off unsplit. I added the matched-count stage rather than read those
  13 trials as parameter effects.

## Open

1. **Is 29 % "materially better"?** This is for Giacomo, together with the visual check.
   - At the target's quad count, the unsplit defaults already fit at 0.097. The plateau that
     tuning reaches is about 0.070, and no trial got below 0.069.
   - Visually, the gain is larger than the number suggests (straps, see above).
   - If Giacomo wants a higher bar, the sweep shows it is not reachable with these six
     parameters.
2. **The winner is one point of a plateau.** Ranks 1–11 are within 2.1 %. If the visual check
   prefers rank 2 or 3, swap them in with no other change.
   - `test_rank_trials.CommittedUnsplit.test_winner` pins the id and sha prefix. Change both,
     and say why in that unit's handoff.
   - Do not move the ranking rule to make a different trial win.
3. **The edge-length CV moves away from the target's** as the fit improves, as in M4.1 Open #3.
   - The target's CV is 0.555, and every ranked trial is between 0.34 and 0.45, except the
     broken se 30 trial at 1.02.
   - The winner's 0.363 is among the further ones.
   - The target's edge lengths vary more, because its loops tighten around holes and features.
     Autoremesher's are more uniform. No parameter in the sweep closes this gap; adaptivity 1
     and anisotropy 1 come closest.
4. **The target's concentric loops around holes are not reproduced** by any trial I rendered.
   This may be a limit of the algorithm rather than of the parameters. Only Giacomo can judge
   whether it matters.
5. **Not swept:** edge-scaling above 2 at a matched count, which needs target-quads above
   100,000 and over 36 s per trial, past the plateau; anisotropy at the combined lead; any
   random search; sharp-edge between 30 and 45.
6. **Carried over:**
   - numpy and scipy come from `~/.local`;
   - the meshes are not in git, so `CommittedUnsplit` runs, but `RealCli` skips on a fresh
     clone;
   - `source.obj` is untracked;
   - the core's output has non-manifold edges (8 in the winner).

## For the next unit (M5)

- **Use the winning flags above, and do NOT call `autoremesher_core_split_into_quads()`.**
  M4.2's handoff told M5 to call it. That no longer applies: D6 is reversed, and the CLI ran
  every M4.3 trial without the split. The calls are:
  - `autoremesher_core_set_target_quad_count(h, 30000)`
  - `_set_scaling(h, 1.25)`
  - `_set_sharp_edge_degrees(h, 90)`
  - `_set_smooth_normal_degrees(h, 0)`
  - `_set_adaptivity(h, 0.5)`
  - `_set_anisotropy(h, 1)`

  The CLI passes these through unchanged (`src/cli/main.cpp`).
- **The expected output is deterministic:**
  - 11,050 vertices and 11,119 faces: 11,065 quads, 53 pentagons and 1 triangle;
  - sha256 `fdbaf07b…62e0f2` as the CLI writes it.

  That makes a byte-exact parity target, if the C# demo writes OBJ the same way. Otherwise
  compare the counts and the score. **The output has non-quads,** so the C# side must handle
  faces of 3–7 corners.
- **It takes about 17 s** on 8 cores. Allow for this in any test timeout.
- **If Giacomo picks another trial** after the visual check, take its flags from
  `tuning_runs/unsplit/ranking.json` (`cli_flags`), and its expected sha256 from
  `output_sha256`.
- **To re-rank** after more trials, run `scripts/rank_trials.py` with the command above, which
  is now the default `--results`. Commit the new `ranking.json`, or
  `CommittedUnsplit.test_ranking_json_is_up_to_date` fails.
- **M4's numeric work is complete.** Both exit criteria hold numerically. The one gate left is
  the human visual comparison.
