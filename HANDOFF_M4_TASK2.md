# Handoff — M4 Task 2: ranking

Unit `M4.2`, plan `plan/roadmap/04_M4_tuning_harness.md`, section "Task 2". Branch `wrapper`.
This is the last unit of M4.

**Status: blocked on one decision (Open #1).** The ranking is done, tested and committed, and the
winner is recorded below for M5. M4's first exit criterion still cannot be closed.
"Materially better than M3's default-parameter baseline" has no meaning D5 settles:

- **Under D5's gate,** the baseline is disqualified (+841 % quads).
- **On fit alone,** the baseline is 3.4× better than the winner.

M4.1 left this to Giacomo (its Open #1), and this unit could not settle it from the data either.
The options are under Open #1.

**A person should look at the winning mesh** before M5 uses it. See Open #3.

## What changed and why

| File | Change |
|---|---|
| `scripts/rank_trials.py` (new) | D5's ranking, described below. It reads `tune_params.py`'s file and needs no numpy. |
| `tests/test_rank_trials.py` (new) | 24 `unittest` tests. 22 use synthetic records; 2 check the committed sweep and ranking. |
| `CMakeLists.txt` | Adds CTest `test_rank_trials` in the existing Python block. |
| `tuning_runs/ranking.json` (new) | The whole ranking of the committed sweep at ±15 %, about 43 KB. It holds all 54 ranked trials, the 36 excluded ones with a reason, and both M3 records for comparison. |
| `plan/roadmap/04_M4_tuning_harness.md` | A "Done in M4.2" note under Task 2. No criterion changed. |

### `scripts/rank_trials.py`

```
scripts/rank_trials.py [--results FILE] [--band PCT] [--top N] [--fit-decimals D]
                       [--baseline FILE:LINE ...] [--json FILE]
```

It applies D5's steps in order:

1. **Drop unfinished trials:** any `status` other than `ok`. Then drop any trial with a nonzero
   `degenerate_quads.candidate`. A degenerate trial is dropped whatever its quad count or fit.
2. **Gate on quad count:** drop any trial with `|quads.delta_pct| > PCT`. A trial exactly on the
   edge stays in. The default is 15.
3. **Rank by combined fit,** `mean(fit.mean, fit_reverse.mean)`. Lower is better.
4. **Break ties by closeness to the target's CV,** `|edge_length_cv.candidate − edge_length_cv.target|`.
   Lower is better. The target value comes from the record: 0.555158, the target's own CV.
   Minimising the CV itself is not the rule, and a test checks this.

Other behaviour:

- **Ties.** By default a tie needs an exactly equal combined fit. `--fit-decimals D` rounds the
  fit to D decimals first, so nearly equal fits tie and the CV decides.
- **Repeated trials.** A trial recorded more than once (`tune_params.py --rerun`) is ranked once,
  from its last `ok` record.
- **`--baseline FILE:LINE`** takes one score_remesh record, such as
  `plan/baselines/m3_baseline.jsonl:2`. It goes through the same gate and is printed with the
  ratio winner / baseline.
- **`--json FILE`** writes the whole ranking: every trial, not just `--top`. It has no
  timestamp, so the same inputs give the same file.
- **Exit codes:**
  - 0: ranked.
  - 1: a file cannot be read.
  - 2: usage error.
  - **3: no trial survives the gate.** Nothing is printed or written. The error names the
    smallest band that would let one trial in. The script never falls back to ungated
    fit-ranking, as the plan requires.

The command that produced the committed file, run from the repository root:

```
scripts/rank_trials.py --band 15 --top 15 \
  --baseline plan/baselines/m3_baseline.jsonl:2 --baseline plan/baselines/m3_baseline.jsonl:3 \
  --json tuning_runs/ranking.json
```

### Tests

The tests are in `tests/test_rank_trials.py`, 24 in all.

- **Metrics:** the combined fit is the mean of both directions. The CV distance is measured from
  the target's CV on both sides.
- **Rank, 13 tests:**
  - fit order;
  - degenerate trials are disqualified even with the best fit, and before the gate;
  - the gate keeps the 104k-quad "densest mesh" out;
  - the band edge is inclusive, and widening the band changes who survives;
  - `timeout` and `cli_failed` trials are excluded;
  - the CV tie-break prefers 0.60 over 0.40 when the fits are equal, from either input order;
  - the CV does not override a better fit;
  - `--fit-decimals` turns near-equal fits into ties;
  - a repeated trial is ranked once;
  - no survivors, and `band_needed`.
- **Baseline:** reading `FILE:LINE`, the gate verdict and the combined fit, a line past the end
  of the file, and malformed specs.
- **CLI:**
  - the table, the `--top` cut and the winner's flags;
  - the JSON's fields and counts;
  - two runs give byte-identical JSON;
  - with no survivors: exit 3, empty stdout and no JSON written;
  - a missing file gives exit 1;
  - 6 usage errors give exit 2.
- **Committed** (skipped where the files are missing; **in CTest both ran**, `ctest -V`):
  - `test_ranking_json_is_up_to_date` reruns the script with the band and baselines recorded in
    `tuning_runs/ranking.json`, and requires the result to be identical to the committed file.
    A stale ranking, or a change to the ranking rules, fails it.
  - `test_winner` pins the winner at ±15 %.
- **Red first.** Before the script existed, the file failed on import
  (`ModuleNotFoundError: rank_trials`).
- **Mutations.** I applied 6, one at a time, and **all 6 were caught**:

  | mutation | tests failed |
  |---|---|
  | rank by the raw CV (minimising it) | 4 |
  | make the band edge exclusive | 1 |
  | let the first record of a repeated trial win | 1 |
  | remove the degenerate check | 3 |
  | ignore `--fit-decimals` | 1 |
  | rank on `fit.mean` alone | 6 |

## Measured

On 2026-10-14, on the host: Python 3.14.3. The toolbox's Python, which CTest uses, is 3.14.8.

- **Data:** `tuning_runs/sweep_results.jsonl` as committed by M4.1 (`1d9a038c`). It has 90
  records, 90 distinct trials, all `ok`, and none with degenerate quads.
- **No new trials were run.** The only CLI run in this unit was a regeneration of the baseline
  mesh, for the density check in Open #1.

### The band: ±15 %, and why

D5 suggests ±15–20 %. I chose **±15 %**: 54 of 90 trials survive and 36 are excluded. No trial is
excluded for any other reason.

- **The winner does not depend on the exact band.** It is first for any band from ±10.2 % (its
  own Δ) up to just under ±17.6 %. I checked 10, 12.5, 14.5, 15, 16, 17.5 and 20.
  - At ±10 % the winner falls out, and `tq45000_es3_se90_sn0_ad1_an1` (+6.4 %, 0.2325) leads.
  - At ±20 %, `tq95000_es3.5_se60_sn0_ad1_an1` leads: +17.6 %, 0.2084. It has 0.7 % lower fit
    than this winner, but 7.4 points more quad-count error.
- **±15 % sits in a gap in the sweep.**
  - On the upper side, no trial lies between +14.5 % and +16.0 %.
  - On the lower side, the nearest trials outside are −15.3 % and −16.1 %. Both have a combined
    fit above 0.61, far from the top.
- **The top of the band favours density.** Inside the gate, fit-ranking still rewards the trials
  near the band's upper edge (M4.1 Open #2). At ±20 % that effect alone picks the winner, for a
  0.7 % gain. ±15 % is the tighter end of D5's range, and stops that.
- **The ±15 % winner is not simply the densest survivor.** It is at +10.2 %, and 7 survivors have
  more quads: for example +13.0 % at 0.2163, and +14.5 % at 0.3399.

### The statistic: mean of the two means, and why

D5 says "`fit` + `fit_reverse`". I rank by `mean(fit.mean, fit_reverse.mean)`.

- **The order is the same as the sum's,** and the number stays in OBJ units. It is the
  "combined fit" M4.1 already used, so the tables in both handoffs can be compared.
- **Both directions count equally.** `fit_reverse` catches a candidate that leaves part of the
  target uncovered (M3.1).
- **The mean of the per-vertex distances** is less driven by single outliers than p95 or max.
- **The choice does not change the winner.** Using rms, p95 or max in place of the mean,
  `tq65000_es3.5_se120_sn0_ad1_an1` is still first.
  - The runner-up is `tq52000_es3_se90…` for rms and p95, and `tq65000_es3.5_se120_sn30…` for
    max.
  - Ranking on `fit.mean` alone would pick `tq80000_es3.5_se90…`, with the winner third. That
    statistic ignores `fit_reverse`, so it is not D5's.

### The CV tie-break in practice

**No tie occurred:** all 54 combined fits are distinct, and the smallest gap between neighbours
is 0.00021. So the CV decided nothing at the default exact precision.

- **`--fit-decimals 2` or `3`:** the top 3 are unchanged.
- **`--fit-decimals 1`:** every fit from 0.15 to 0.25 rounds to 0.2, so the CV decides among
  them, and `tq65000_es3.5_se120_sn0_ad0.5_an1` (CV 0.4068) comes first. That is far coarser
  than the differences the sweep resolves, so I left the default exact.

### The ranking (±15 %, top 15)

All 54 entries are in `tuning_runs/ranking.json`. Every trial below has 0 degenerate quads and
0 non-quad faces.

| rank | trial | quads | Δ | combined | fit | fit_rev | CV | \|ΔCV\| |
|---|---|---|---|---|---|---|---|---|
| 1 | `tq65000_es3.5_se120_sn0_ad1_an1` | 12180 | +10.2 % | **0.2098** | 0.1715 | 0.2480 | 0.3844 | 0.1708 |
| 2 | `tq52000_es3_se90_sn0_ad1_an1` | 12496 | +13.0 % | 0.2163 | 0.1759 | 0.2566 | 0.3866 | 0.1686 |
| 3 | `tq80000_es3.5_se90_sn0_ad1_an1` | 12318 | +11.4 % | 0.2265 | 0.1662 | 0.2869 | 0.3877 | 0.1674 |
| 4 | `tq45000_es3_se90_sn0_ad1_an1` | 11760 | +6.4 % | 0.2325 | 0.1848 | 0.2802 | 0.3877 | 0.1675 |
| 5 | `tq65000_es3.5_se120_sn30_ad1_an1` | 11272 | +2.0 % | 0.2330 | 0.1886 | 0.2773 | 0.3767 | 0.1784 |
| 6 | `tq80000_es3.5_se60_sn0_ad1_an1` | 11945 | +8.1 % | 0.2350 | 0.1782 | 0.2919 | 0.3881 | 0.1671 |
| 7 | `tq38000_es3_se120_sn0_ad1_an1` | 11300 | +2.2 % | 0.2364 | 0.1924 | 0.2804 | 0.3873 | 0.1678 |
| 8 | `tq65000_es3.5_se120_sn0_ad1_an0.5` | 11844 | +7.1 % | 0.2366 | 0.1759 | 0.2973 | 0.3649 | 0.1902 |
| 9 | `tq65000_es3.5_se120_sn0_ad0.5_an1` | 11806 | +6.8 % | 0.2388 | 0.1802 | 0.2973 | 0.4068 | 0.1483 |
| 10 | `tq30000_es2.5_se90_sn0_ad1_an1` | 12402 | +12.2 % | 0.2443 | 0.1857 | 0.3029 | 0.3733 | 0.1819 |
| 11 | `tq28000_es2.5_se90_sn0_ad1_an1` | 12196 | +10.3 % | 0.2465 | 0.1851 | 0.3079 | 0.3896 | 0.1656 |
| 12 | `tq65000_es3.5_se90_sn0_ad1_an1` | 11144 | +0.8 % | 0.2532 | 0.1923 | 0.3140 | 0.3942 | 0.1610 |
| 13 | `tq52000_es3_se45_sn0_ad1_an1` | 12498 | +13.1 % | 0.2570 | 0.1712 | 0.3428 | 0.4465 | 0.1086 |
| 14 | `tq65000_es3.5_se120_sn60_ad1_an1` | 11324 | +2.4 % | 0.2618 | 0.1814 | 0.3421 | 0.3837 | 0.1714 |
| 15 | `tq24000_es2.5_se90_sn0_ad1_an1` | 11489 | +3.9 % | 0.2714 | 0.1972 | 0.3456 | 0.3892 | 0.1660 |

The top 15 are all at edge-scaling 2.5 or above. The best survivor at edge-scaling 1 is
`tq2764_es1_se60_sn0_ad1_an1`, with combined fit 0.8896.

### The winning parameter set (for M5)

**`tq65000_es3.5_se120_sn0_ad1_an1`**:

```
autoremesher-cli -i source.obj -o out.obj --target-quads 65000 --edge-scaling 3.5 \
  --sharp-edge 120 --smooth-normal 0 --adaptivity 1 --anisotropy 1
```

| parameter | value | the CLI's default |
|---|---|---|
| target quads | 65000 | 50000 |
| edge scaling | 3.5 | 1.0 |
| sharp edge | 120 | 90 |
| smooth normal | 0 | 0 |
| adaptivity | 1 | 1 |
| anisotropy | 1 | 1 |

What it produces from `source.obj` (sha256 `3e2e44ee…b617c2`):

- **The output:** 12,180 quads (+10.19 % over 11,054), 12,095 vertices, 0 non-quad faces and
  0 degenerate quads. Its sha256 is `f4fb212b03ca9af49ec2898a7afd3631d85e02b0a41f0abd9d9eea67e8a132c4`.
  - The file `tuning_runs/trials/tq65000_es3.5_se120_sn0_ad1_an1/out.obj` still has this hash
    today; I checked with `sha256sum`.
  - M4.1 re-ran the trial and got the same bytes.
- **`fit`:** mean 0.17149, rms 0.30932, p95 0.69712, max 3.11058.
- **`fit_reverse`:** mean 0.24801, rms 0.45605, p95 0.94465, max 3.96939.
- **Combined fit** 0.209751.
- **Edge CV** 0.38437, which is 0.17079 below the target's 0.55516.
- **Time:** the CLI took 18.5 s, measured by M4.1.
- **For M5's parity check:** these are the C API's parameters, passed through by the CLI
  unchanged. `--target-quads 65000` is the pre-split target, as with every CLI run since D6. M5
  should expect about 12,180 quads only if it applies the same quad split
  (`autoremesher_core_split_into_quads()`), as the CLI does.

### Against M3's records

| record | quads | Δ | under D5 at ±15 % | combined fit | winner ÷ record |
|---|---|---|---|---|---|
| **M3 baseline**, CLI defaults (`m3_baseline.jsonl:2`) | 104,000 | +840.8 % | **excluded: outside the band** | 0.06129 | 3.42 (the winner is worse) |
| M3 reference, `--target-quads 2764` (`:3`) | 11,526 | +4.3 % | survives: rank 48 of 54 | 1.17657 | **0.178 (5.6× better)** |
| **winner** | 12,180 | +10.2 % | rank 1 | 0.20975 | — |

How the comparison above was made:

- The `:3` record is the same trial as the sweep's centre, `tq2764_es1_se90_sn0_ad1_an1`, with
  the same output sha256 (`bc40aff3…`). The centre is ranked 48th of 54 at ±15 %.
- **I regenerated the baseline mesh** for the density check below:
  `build/autoremesher-cli -i source.obj -o /tmp/m42/default.obj`, with no parameter flags and with
  `DISPLAY` and `WAYLAND_DISPLAY` unset. Its sha256 is `2f4a1fb8…aa74c6`, the same as M3.2's,
  so the regeneration is exact.
- **The density check** divides each mesh's combined fit by its own mean edge length, computed
  over its unique edges with a numpy script in `/tmp`:

  | mesh | quads | mean edge | combined ÷ edge | combined ÷ edge² |
  |---|---|---|---|---|
  | baseline | 104,000 | 0.6481 | 0.0946 | 0.1459 |
  | winner | 12,180 | 1.8767 | 0.1118 | 0.0596 |
  | reference 2764 | 11,526 | 1.5147 | 0.7768 | 0.5129 |
  | target | 11,054 | 2.1426 | — | — |

## M4 exit criteria (phase-wide)

- [ ] **A ranked list of trials exists, with the best trial's combined score materially better
  than M3's documented default-parameter baseline.**
  - **The first half holds.** The ranked list is `tuning_runs/ranking.json`: 54 trials at
    ±15 %. CTest `test_rank_trials` checks it against the sweep on every run.
  - **The second half has no answer under D5, so this criterion is not closed (Open #1).**
    - Under D5's gate, the baseline is disqualified (+840.8 % quads), so it has no rank to
      beat.
    - On combined fit alone, the baseline (0.0613) beats the winner (0.2098) by 3.42×.
    - The best result inside the gate cannot plausibly reach 0.0613: the best of 63 trials
      within ±20 % is 0.208.
- [x] **The winning parameter set is recorded in this phase's handoff for M5 to consume
  directly.** See "The winning parameter set" above: the flags, the values next to the
  defaults, the output's sha256 and every score field.
  - This holds under any answer to Open #1, because none of them changes the ranking.
  - Open #3 (a person looking at the mesh) is the one caveat.

### The CI command

The driver's command was run on the working tree with the new script, tests and CMake entry,
before the first commit:

- Exit **0**, 56 s wall (the build was incremental).
- **13/13** tests pass: `test_rank_trials` 0.34 s, `test_tune_params` 9.81 s,
  `test_score_remesh` 6.15 s and `test_core` 37.88 s.
- In `ctest -V`, both `Committed` tests in `test_rank_trials` ran; neither was skipped.

## What I got wrong on the way

- **A test expected the wrong function.** One baseline test expected `load_baseline()` to
  return a gate verdict. But the verdict depends on the band, so the gate belongs in
  `baseline_entry()`. I fixed the test before the script had ever passed it; no passing test
  was edited.
- **A first draft of one assertion was a fragile string split.** I replaced it with
  line-anchored regexes before the first run.
- **The plan note missed the first commit.** My first edit of the plan file did not match its
  line wrapping, so the note was not in commit `cc842670`. It is in the handoff commit instead.

## Open

1. **Exit criterion 1 needs Giacomo's reading of "materially better than the baseline".**
   M4.1 raised this as its Open #1, and this unit's measurements do not settle it. The options:

   - **(a) Gated: the baseline is disqualified, so any survivor beats it.**
     - The criterion closes now.
     - It shows only that the gate works, not that tuning helped.
   - **(b) Against M3's matched-count reference** (`m3_baseline.jsonl:3`, `--target-quads 2764`
     with every other parameter at its default).
     - The winner is **5.6× better**: 0.2098 against 1.1766, at a similar count (+10.2 %
       against +4.3 %).
     - It is also better on each direction separately: `fit` 3.3× and `fit_reverse` 7.2×.
     - The criterion closes now.
     - The plan text would change from "default-parameter baseline" to "the default parameters
       at the target's quad count". I recommend this reading. It is what D5's reasoning
       implies, and it measures what the tuning actually changed.
   - **(c) Fit normalised by mesh resolution.** This depends on how fit is assumed to scale with
     edge length h.
     - Dividing by h, the baseline still wins: 0.0946 against 0.1118.
     - Dividing by h², the error order of a piecewise-linear surface, the winner is 2.4× better:
       0.0596 against 0.1459.
     - The exponent is an assumption, so this reading would need its own decision.
   - **(d) Literal: compare fit numbers directly.**
     - The criterion fails, by 3.42×, and no in-band trial can be expected to pass it.
     - M4 would then need a different target or a new decision. D5 itself says density is what
       makes the baseline's fit good.

   Whichever is chosen, the plan's `## Exit criteria` bullet should be reworded to match. The
   ranking and the winner do not change under any of the options.
2. **The band upper edge matters only above +17.6 %.** At ±20 %, the 95,000/es 3.5/se 60 trial
   would win by 0.7 % fit at +17.6 % quads. If Giacomo prefers ±20 %, rerun with `--band 20`;
   `test_ranking_json_is_up_to_date` then needs the regenerated `ranking.json` committed, since it
   reads the band from that file.
3. **No one has looked at the winning mesh.** This is carried from M4.1 Open #7, and is the
   reason this unit reports `needsHumanCheck: true`.
   - The metrics do not judge edge flow, pole placement, or how the quads look next to the
     Rhino target.
   - At edge-scaling 3.5, the output is the CLI's coarsest setting re-densified by target-quads.
     Its edge CV, 0.384, is further from the target's 0.555 than the es 1 trials' are.
   - **To look at it:** open `tuning_runs/trials/tq65000_es3.5_se120_sn0_ad1_an1/out.obj` next
     to `quad-remeshed.obj`, in Rhino or Blender. If the file is gone, regenerate it with the
     command above; it is byte-reproducible. The runner-up, `tq52000_es3_se90_sn0_ad1_an1`, is
     in the same directory.
4. **The CV tie-break never fired.** All 54 fits are distinct at full precision. If Giacomo
   meant CV to decide among "practically equal" fits, he needs to say how close "equal" is.
   `--fit-decimals` provides this: at 2 or 3 decimals the top 3 are unchanged, and at 1 decimal
   the ad 0.5 variant wins.
5. **Carried over from M4.1:**
   - edge-scaling 4 at a matched count, and random search, are still unexplored;
   - numpy and scipy come from `~/.local`;
   - the meshes are not in git.

## For the next unit (M5)

- **Use the winning flags above.** The CLI passes them to the C API unchanged
  (`src/cli/main.cpp:89-94`). The calls are:
  - `autoremesher_core_set_target_quad_count(h, 65000)`
  - `_set_scaling(h, 3.5)`
  - `_set_sharp_edge_degrees(h, 120)`
  - `_set_smooth_normal_degrees(h, 0)`
  - `_set_adaptivity(h, 1)`
  - `_set_anisotropy(h, 1)`

  Then call `autoremesher_core_split_into_quads()` on the result, as `main.cpp:111` does.
- **The expected output is deterministic.** It has 12,180 quads and sha256 `f4fb212b…8a132c4`
  when the quad split is applied, as the CLI does. That makes a byte-exact parity target for the
  C# demo, if it writes OBJ the same way.
- **It takes about 18.5 s** on 8 cores, against 3 s for the 2764 reference. Allow for this in any
  test timeout.
- **To re-rank** after more trials, run `scripts/rank_trials.py` with the command above. Commit
  the new `ranking.json`, or `test_rank_trials` fails.
- **Open #1 must be answered** before M4 can be called complete. M5 does not depend on the
  answer.
