# Handoff — M4 Task 1: `scripts/tune_params.py`

Unit `M4.1`, plan `plan/roadmap/04_M4_tuning_harness.md`, section "Task 1". Branch `wrapper`.

**Status: done.** The sweep harness exists and CTest `test_tune_params` tests it. The harness has
run 90 trials into `tuning_runs/sweep_results.jsonl`. All 90 are ok and none has degenerate quads.
Ranking them is Task 2's job, so this handoff names a **leading trial**, not a winner.

**Read Open #1 first.** It is a conflict between D5 and M4's exit criterion. Task 2 and Giacomo
need to settle it.

## What changed and why

| File | Change |
|---|---|
| `scripts/tune_params.py` (new) | The harness, described below. It is executable and imports `score_remesh.score()`. |
| `tests/test_tune_params.py` (new) | 22 `unittest` tests. 21 run against a fake CLI that the test writes. 1 runs the real CLI on `source.obj`. |
| `CMakeLists.txt` | Adds CTest `test_tune_params` in the existing Python block. It passes `AUTOREMESHER_CLI=$<TARGET_FILE:autoremesher-cli>` and the paths of both meshes. |
| `.gitignore` | Adds `tuning_runs/trials/`: the per-trial meshes, reports and logs (52 MB after this sweep). `sweep_results.jsonl` is committed. |
| `tuning_runs/sweep_results.jsonl` (new) | 90 trial records, about 187 KB. |
| `plan/roadmap/04_M4_tuning_harness.md` | A "Done in M4.1" note under Task 1. No criterion changed. It records that the plan's ≈2,760 target-quads centre holds only at edge-scaling 1. It also records that the plan's sharp-edge set is not the CLI's full range. |

### Command line

```
scripts/tune_params.py oat    [--vary NAME=V1,V2,...] [--only NAME,...] [common]
scripts/tune_params.py grid   --vary NAME=V1,V2,... [--vary ...]        [common]
scripts/tune_params.py random --vary NAME=V1,V2,... [--vary ...] -n N [--seed S] [common]
scripts/tune_params.py list   [--out FILE]
```

- **`oat`: one parameter at a time.**
  - It runs the centre first. Then, for each swept parameter, it runs each of its values with every other parameter at the centre. Values the centre already has are not repeated.
  - The values default to the plan's: `DEFAULT_SWEEP`, with target-quads re-centred to {2000, 2400, 2764, 3100, 3500} as M2.3's D6 warning says. That gives 17 trials.
  - `--vary` replaces one parameter's values. `--only` limits which parameters are swept.
- **`grid`.** Every combination of the `--vary` values, with the other parameters at the centre.
- **`random`.** N distinct combinations of that grid, drawn with `random.Random(seed).sample`.
- **`list`.** One table line per recorded trial.
- **The centre** is the CLI's defaults with target-quads 2764: `tq2764_es1_se90_sn0_ad1_an1`. `--center NAME=VALUE` moves it.
- **Common options:**
  - `--cli` (default `build/autoremesher-cli`), `--source` (`source.obj`), `--target` (`quad-remeshed.obj`).
  - `--out` (`tuning_runs/sweep_results.jsonl`) and `--work` (`tuning_runs/trials`).
  - `--timeout` seconds per CLI run (default 600).
  - `--samples` and `--seed-score`, passed to the scorer (default 0: every vertex).
  - `--label`, which is stored in every record.
  - `--rerun` and `--dry-run`.
- **Parameter names** are the CLI flags without the dashes: `target-quads`, or `target_quads`. Values are checked against the CLI's own ranges (`src/cli/cli_options.h`) before anything runs.
- **Exit codes.** 0 when the sweep ran, even if trials failed. 1 if the CLI, a mesh or `--out` cannot be used. 2 for a usage error.

### What one trial does

1. **Runs the CLI.** It runs `autoremesher-cli -i SOURCE -o WORK/<id>/out.obj --report WORK/<id>/report.txt` with all six parameter flags given explicitly.
   - `DISPLAY` and `WAYLAND_DISPLAY` are removed from the CLI's environment, as in M3's measurements.
   - Stdout and stderr go to `WORK/<id>/cli.log`.
   - The CLI runs in its own session. **On a timeout, its whole process group is killed** with SIGKILL. Ctrl-C kills it too.
   - Any `out.obj` left by an earlier run is deleted first, so a failing run can never score a stale mesh. A test exists for this.
2. **Scores the mesh** with `score_remesh.score(out.obj, TARGET, samples, seed)`. The scorer runs in-process.
3. **Appends one JSON line to `--out`**, flushed after each trial. A sweep that is interrupted keeps every finished trial.

The trial id is the six values in a fixed order, e.g. `tq65000_es3.5_se120_sn0_ad1_an1`.

**The record** (`tuner_version` 1):

| Field | Meaning |
|---|---|
| `trial`, `sweep`, `label` | The id; `oat`, `grid` or `random`; the free text from `--label`. |
| `params` | All six values: `target_quads` (int) and the five floats. |
| `status` | `ok`, `timeout`, `cli_failed` (nonzero exit) or `score_failed` (the scorer raised, e.g. no output file). |
| `error` | `null` if ok. Otherwise the timeout, the exit code with the last 5 lines of stderr, or the scorer's exception. |
| `source`, `source_sha256` | The input mesh. |
| `cli` | `argv`, `exit_code` (`null` on a timeout), `wall_s`, `timeout_s`, and `output_sha256` (set only if ok). |
| `score_wall_s`, `score` | The scorer's time and its whole record, `scorer_version` 1. `null` unless the trial is ok. |
| `time` | When the trial ended, in local ISO time. |

**Resuming.** Some trials already have a record in `--out`, and running them again cannot change
the outcome, because the CLI is deterministic (M3.2, and rechecked below). Those trials are
skipped unless `--rerun` is given:

- a record with `ok` or `cli_failed`;
- a `timeout` record whose `timeout_s` is at least the current timeout.

`score_failed` trials are always run again, because a fix to the scorer could change their
outcome. The combined grid below relied on this rule: 3 of its 48 trials were already recorded
by the probes, so it ran 45.

### Tests

The tests are in `tests/test_tune_params.py`, 22 in all.

- **Plan.** `DEFAULT_SWEEP` and the centre are checked against the plan's values. The tests also cover:
  - the order and count of the `oat` trials (17);
  - grid products;
  - random draws: reproducible, distinct, a subset of the grid, and the whole grid when N is larger;
  - parsing `--vary` and `--center`, including 10 refusals;
  - trial ids and the exact argv sent to the CLI.
- **Sweep, against a fake CLI.** The fake is a Python script with a shebang. It logs its argv and writes a strip of `target-quads` quads at height `edge-scaling − 1`, so the scored `fit.max` is known exactly. The tests check:
  - every record field of an ok trial;
  - parameters that stay at the centre;
  - a hang (killed at the timeout), a failing exit, and exit 0 with no output, each recorded as the right status while the sweep goes on;
  - after a timeout, no fake process is left;
  - the resume rules, and `--rerun`;
  - that a stale mesh is never scored;
  - the `oat`, `random`, `list` and `--dry-run` commands;
  - usage errors (exit 2) and a missing CLI (exit 1, nothing written).
- **Real CLI** (`RealCli`, skipped where the CLI or the meshes are missing). The harness runs `--target-quads 2764` on `source.obj`. Its score must equal line 3 of `plan/baselines/m3_baseline.jsonl`, M3.2's record of the same trial, on every field except the two paths. The output's sha256 must begin `bc40aff3`, as M3 recorded. **In CTest this test ran; it was not skipped** (`ctest -V`: `RealCli.test_reproduces_m3s_reference_trial ... ok`).
- **Red first.** Before the script existed, the test file failed on import (`ModuleNotFoundError: tune_params`).
- **Mutations.** I tried 4.
  - **3 were caught at once:**
    - ignoring `timeout_s` in the resume rule failed 1 test;
    - letting `oat` repeat the centre's value failed 4 tests;
    - running the CLI without its own process group gave 3 errors.
  - **1 survived at first:** removing the deletion of a stale `out.obj`. I added `test_a_previous_runs_mesh_is_not_scored`, which now fails on that mutation.

## Measured

On 2026-10-14, 10:59 to 11:28 local time, on the host.

- **Python and libraries:** Python 3.14.3, numpy 2.5.4, scipy 1.18.1.
- **The CLI:** `build/autoremesher-cli`, built in the toolbox (Release) by the CI command at `a9d26902`. The CLI's code is unchanged since `cad93b43`.
- **The meshes:** `source.obj` (sha256 `3e2e44ee…b617c2`, the same as M3.2's) and target `quad-remeshed.obj`.
- **Every trial measured every vertex** (`--samples 0`).
- **Trials ran one at a time.** The CLI uses all 8 cores through TBB.

The table uses a working statistic, **combined fit = mean(`fit.mean`, `fit_reverse.mean`)**. I
used it only to steer the sweep. The statistic, and the quad-count band, are Task 2's to choose
under D5.

### The CI command

The driver's command took 54.6 s wall and exited **0**. It was incremental; the build already
existed.

- **12/12** tests pass.
- `test_tune_params` takes 9.73 s, `test_score_remesh` 5.77 s and `test_core` 37.02 s.

### Approach, and why

The plan asks for a one-at-a-time sweep first, then a smaller combined search over the 2–3 most
sensitive parameters. That is what I ran, with two probe stages in between. The probes were added
because the one-at-a-time result was misleading.

| stage (record `label`) | trials | in ±20 % | in ±15 % | CLI s | score s |
|---|---|---|---|---|---|
| `M4.1 one-at-a-time`: `oat` with the plan's values | 17 | 12 | 11 | 46 | 28 |
| `… sharp-edge below the plan's range`: se 30, 45, 75 | 3 | 2 | 1 | 15 | 7 |
| `M4.1 probe: edge-scaling with target-quads compensating` | 19 | 12 | 8 | 176 | 24 |
| `M4.1 combined grid: edge-scaling x target-quads (paired) x sharp-edge` | 45 (+3 already recorded) | 37 | 29 | 777 | 61 |
| `… around the combined grid's lead`: `oat` of sn, ad, an at the lead | 6 | 5 | 5 | 118 | 8 |
| **total** | **90** | **63** | **54** | **1,133** | **127** |

The ±20 % and ±15 % columns count trials whose quad count is within that band of 11,054.

- **Why not the full grid.** It would be 2,160 trials. At the plan's values a trial costs 3–6 s, but the useful region turned out to need target-quads of 30,000–95,000. There a trial takes 13–40 s.
- **What the one-at-a-time stage showed** (the table below):
  - **At a fixed target-quads, edge-scaling only looks like it removes quads:** 11,526 quads at es 1, then 5,376, 949 and 322.
  - **Sharp-edge 60 looked best** at a matched count: combined fit 0.890, against 1.177 at the centre.
  - **Adaptivity, anisotropy and smooth-normal looked minor.** Each changed the combined fit by at most about 0.15. Lowering anisotropy made the fit worse.
- **The probes found the main effect.** Edge-scaling has to be paired with a larger target-quads to stay at the same count. **At a matched count, higher edge-scaling fits far better.** The best trial in ±20 % at each edge-scaling:

| edge-scaling | best trial in ±20 % | quads Δ | combined fit |
|---|---|---|---|
| 1.0 | `tq2764_es1_se45_sn0_ad1_an1` | +17.7 % | 0.619 |
| 1.25 | `tq5000_es1.25_se90_sn0_ad1_an1` | +9.5 % | 0.618 |
| 1.5 | `tq6000_es1.5_se90_sn0_ad1_an1` | −4.6 % | 0.695 |
| 1.75 | `tq12000_es1.75_se90_sn0_ad1_an1` | +19.4 % | 0.347 |
| 2.0 | `tq16000_es2_se90_sn0_ad1_an1` | +16.0 % | 0.276 |
| 2.5 | `tq32000_es2.5_se90_sn0_ad1_an1` | +19.4 % | 0.216 |
| 3.0 | `tq52000_es3_se90_sn0_ad1_an1` | +13.0 % | 0.216 |
| 3.5 | `tq95000_es3.5_se60_sn0_ad1_an1` | +17.6 % | 0.208 |

The gain levels off from es 2.5 to 3.5.

- **So the combined search covers edge-scaling, target-quads and sharp-edge.** Edge-scaling has the largest effect and sharp-edge the next largest. Target-quads is the dial that keeps each trial near the band.
  - **target-quads is paired with edge-scaling**, not crossed with it, so most trials land near the band: es 2 → {12k, 14k, 16k}, es 2.5 → {24k, 28k, 32k}, es 3 → {38k, 45k, 52k}, es 3.5 → {65k, 80k, 95k}.
  - **sharp-edge** is {45, 60, 90, 120}.
- **The last check re-ran one-at-a-time at the lead** for smooth-normal, adaptivity and anisotropy. None of them improved on the lead within ±20 %. Adaptivity 0 jumps to +72 % quads at this point; at es 1 it lowered the count to −20 %. Anisotropy 0 is clearly worse (0.424).
- **Not explored:**
  - **edge-scaling 4 at a matched count.** At es 4, tq 30,000 gives only 4,645 quads, so the band would need target-quads well above 100k, at 40 s or more per trial. The trend had already flattened at 3–3.5.
  - **sharp-edge 150 and above, and any random search.**

### The one-at-a-time stages

| trial | quads (Δ) | edge CV | fit mean | fit_rev mean | combined | CLI s |
|---|---|---|---|---|---|---|
| `tq2764_es1_se90_sn0_ad1_an1` (centre) | 11526 (+4.3 %) | 0.4552 | 0.5729 | 1.7802 | 1.177 | 3.2 |
| `tq2000_es1_se90_sn0_ad1_an1` | 7830 (−29.2 %) | 0.5083 | 0.6397 | 3.0837 | 1.862 | 1.9 |
| `tq2400_es1_se90_sn0_ad1_an1` | 9789 (−11.4 %) | 0.4669 | 0.6064 | 2.0541 | 1.330 | 2.1 |
| `tq3100_es1_se90_sn0_ad1_an1` | 11451 (+3.6 %) | 0.4689 | 0.5423 | 1.6804 | 1.111 | 3.2 |
| `tq3500_es1_se90_sn0_ad1_an1` | 13230 (+19.7 %) | 0.4347 | 0.4806 | 1.3605 | 0.921 | 4.0 |
| `tq2764_es1.5_se90_sn0_ad1_an1` | 5376 (−51.4 %) | 0.5573 | 0.6027 | 3.8294 | 2.216 | 2.3 |
| `tq2764_es2_se90_sn0_ad1_an1` | 949 (−91.4 %) | 0.6463 | 0.7242 | 32.2857 | 16.505 | 1.9 |
| `tq2764_es2.5_se90_sn0_ad1_an1` | 322 (−97.1 %) | 0.7966 | 0.4949 | 50.0587 | 25.277 | 1.9 |
| `tq2764_es1_se30_sn0_ad1_an1` | 16437 (+48.7 %) | 0.6725 | 0.2857 | 1.0352 | 0.660 | 8.0 |
| `tq2764_es1_se45_sn0_ad1_an1` | 13012 (+17.7 %) | 0.5004 | 0.3143 | 0.9242 | 0.619 | 4.5 |
| `tq2764_es1_se60_sn0_ad1_an1` | 11519 (+4.2 %) | 0.4845 | 0.4564 | 1.3229 | 0.890 | 3.9 |
| `tq2764_es1_se75_sn0_ad1_an1` | 9931 (−10.2 %) | 0.4809 | 0.5461 | 2.1665 | 1.356 | 2.9 |
| `tq2764_es1_se120_sn0_ad1_an1` | 11465 (+3.7 %) | 0.4243 | 0.5668 | 1.8006 | 1.184 | 2.7 |
| `tq2764_es1_se150_sn0_ad1_an1` | 11904 (+7.7 %) | 0.4215 | 0.5493 | 1.5688 | 1.059 | 2.5 |
| `tq2764_es1_se90_sn30_ad1_an1` | 10688 (−3.3 %) | 0.4714 | 0.5708 | 1.8713 | 1.221 | 2.8 |
| `tq2764_es1_se90_sn60_ad1_an1` | 10656 (−3.6 %) | 0.4353 | 0.5433 | 1.7977 | 1.170 | 2.8 |
| `tq2764_es1_se90_sn0_ad0_an1` | 8800 (−20.4 %) | 0.4230 | 0.4952 | 1.8440 | 1.170 | 1.4 |
| `tq2764_es1_se90_sn0_ad0.5_an1` | 11599 (+4.9 %) | 0.4064 | 0.5437 | 1.6782 | 1.111 | 2.4 |
| `tq2764_es1_se90_sn0_ad1_an0` | 11118 (+0.6 %) | 0.4315 | 0.5716 | 2.0850 | 1.328 | 3.3 |
| `tq2764_es1_se90_sn0_ad1_an0.5` | 11040 (−0.1 %) | 0.4622 | 0.5968 | 1.9743 | 1.286 | 3.4 |
| `tq65000_es3.5_se120_sn30_ad1_an1` | 11272 (+2.0 %) | 0.3767 | 0.1886 | 0.2773 | 0.233 | 20.2 |
| `tq65000_es3.5_se120_sn60_ad1_an1` | 11324 (+2.4 %) | 0.3837 | 0.1814 | 0.3421 | 0.262 | 20.6 |
| `tq65000_es3.5_se120_sn0_ad0_an1` | 19034 (+72.2 %) | 0.3644 | 0.1195 | 0.2297 | 0.175 | 23.8 |
| `tq65000_es3.5_se120_sn0_ad0.5_an1` | 11806 (+6.8 %) | 0.4068 | 0.1802 | 0.2973 | 0.239 | 18.9 |
| `tq65000_es3.5_se120_sn0_ad1_an0` | 10994 (−0.5 %) | 0.3200 | 0.2175 | 0.6297 | 0.424 | 16.6 |
| `tq65000_es3.5_se120_sn0_ad1_an0.5` | 11844 (+7.1 %) | 0.3649 | 0.1759 | 0.2973 | 0.237 | 18.0 |

### The best trials in ±15 % by combined fit

This is not D5's ranking; that is Task 2's. All of these trials have 0 degenerate quads.

| trial | quads (Δ) | edge CV | fit mean | fit_rev mean | combined | CLI s |
|---|---|---|---|---|---|---|
| `tq65000_es3.5_se120_sn0_ad1_an1` | 12180 (+10.2 %) | 0.3844 | 0.1715 | 0.2480 | **0.2098** | 18.5 |
| `tq52000_es3_se90_sn0_ad1_an1` | 12496 (+13.0 %) | 0.3866 | 0.1759 | 0.2566 | 0.2163 | 17.5 |
| `tq80000_es3.5_se90_sn0_ad1_an1` | 12318 (+11.4 %) | 0.3877 | 0.1662 | 0.2869 | 0.2265 | 24.7 |
| `tq45000_es3_se90_sn0_ad1_an1` | 11760 (+6.4 %) | 0.3877 | 0.1848 | 0.2802 | 0.2325 | 15.5 |
| `tq65000_es3.5_se120_sn30_ad1_an1` | 11272 (+2.0 %) | 0.3767 | 0.1886 | 0.2773 | 0.2330 | 20.2 |

Widening the band to ±20 % adds one trial above the lead: `tq95000_es3.5_se60_sn0_ad1_an1`, at
12,998 quads (+17.6 %) with combined fit 0.2084. The next is `tq32000_es2.5_se90_sn0_ad1_an1`,
at +19.4 % with 0.2164.

### The leading trial

**`tq65000_es3.5_se120_sn0_ad1_an1`**: `--target-quads 65000 --edge-scaling 3.5 --sharp-edge 120
--smooth-normal 0 --adaptivity 1 --anisotropy 1`.

- **Quads:** 12,180 (+10.19 %), with 12,095 vertices and 0 non-quad faces.
- **Edge CV:** 0.38437 (Δ −0.17079 from the target's 0.55516).
- **Degenerate quads:** 0.
- **`fit`:** mean 0.17149, rms 0.30932, p95 0.69712, max 3.11058.
- **`fit_reverse`:** mean 0.24801, rms 0.45605, p95 0.94465, max 3.96939.
- **Time:** the CLI took 18.5 s and scoring 1.2 s.
- **Output sha256:** `f4fb212b…8a132c4`.
- **It repeats exactly.** I re-ran it with `--rerun` into `/tmp`: the sha256 was the same, and the score record was identical apart from the candidate path.
- **Against the reference trial.** The reference is M3.2's `--target-quads 2764` trial, which passes the gate. The lead's combined fit is 5.6× lower (0.210 against 1.177), its `fit` 3.3× lower and its `fit_reverse` 7.2× lower, at a similar count (+10 % against +4 %).

## What I got wrong on the way

- **A test expectation was wrong.** My first grid test used target-quads 12 against a 10-quad target strip, so the fake's vertices at x = 11 and 12 overhang the target and `fit.max` is not `es − 1` there. I changed it to {8, 10} before the first run.
- **A typo in an error message:** "not awhole number". Fixed before the first commit.
- **A waiting loop that never ended.** It was `until ! pgrep -f m41_grid.sh`, and the pattern matched the loop's own command line. The sweep had already finished; I killed the waiter. No trial or record was affected.
- **I first read the one-at-a-time result the wrong way.** At a fixed target-quads, edge-scaling looks like it only destroys quad count, and I nearly left it out of the combined grid. The probes are what showed the opposite. See Open #4 for what this means for the plan's method.

## M4 exit criteria (phase-wide): status after this unit

- [ ] **A ranked list of trials exists, with the best trial's combined score materially better than M3's baseline.** Still open: the ranking is Task 2's. The trials to rank are in `tuning_runs/sweep_results.jsonl`. **This criterion may not be reachable as written** (Open #1).
- [ ] **The winning parameter set is recorded in this phase's handoff for M5.** Still open: Task 2 picks the winner. The leading trial above is a candidate, not a decision.

## Open

1. **The baseline beats every gated trial on fit, so "materially better than the baseline" conflicts with D5.**
   - **The baseline** is the CLI's defaults (104,000 quads, +841 %). Its combined fit is (0.07344 + 0.04914) / 2 = **0.0613**.
   - **The best trial in ±20 %** reaches 0.208.
   - **Gating the baseline under D5** disqualifies it, so it has no rank to beat.
   - **Comparing on fit alone,** no trial near the target's count can win. Fit improves with density: M3.1 Open #2, and here, Adaptivity 0 at 19,034 quads gives 0.175.
   - **The criterion needs one of three readings:**
     - (a) A gated baseline loses by definition, so any surviving trial is "better". This is cheap, but says nothing.
     - (b) Compare against the best gated trial at the CLI's defaults apart from target-quads. M3.2's `--target-quads 2764` record, combined fit 1.177, is the natural reference. The lead is 5.6× better than it.
     - (c) Compare the quad-count error and the fit separately.
   - **This is Giacomo's call.** Task 2 is where it bites.
2. **Inside the gate, fit-ranking still favours the top of the band.** All six of the best trials in ±20 % are between +10 % and +20 %. The band's upper limit therefore largely picks the winner's density: at ±15 % the lead is at +10 %, and at ±20 % the best is at +17.6 %. Task 2 should choose the band knowing this. A band that is not symmetric, or a fit normalised by quad count, would change the ranking.
3. **Edge CV moves away from the target as edge-scaling rises.** CV is D5's tie-breaker, scored by closeness to 0.5552.
   - Every trial at es ≥ 2 has a CV of 0.37–0.45, while es 1 gives 0.41–0.67.
   - The best fits are therefore among the worst on CV.
   - Under D5 this only breaks ties, but Giacomo may want to know.
4. **The plan's one-at-a-time method is misleading for this CLI.** Several parameters move the quad count as well as the quality: edge-scaling, sharp-edge and adaptivity, and adaptivity in a direction that depends on edge-scaling. A one-at-a-time sweep at a fixed target-quads mixes up the two effects. The combined search only worked because target-quads was paired with edge-scaling by hand.
5. **The core's quad count is not monotonic in target-quads.** At es 1, target-quads 3100 gives 11,451 quads, fewer than 2764's 11,526. The count is neither linear nor monotonic in edge-scaling at a matched count either. Hitting the band takes a small scan, not interpolation.
6. **Not swept:** edge-scaling 4 at a matched count, sharp-edge ≥ 150 outside es 1, and any random search (above). The `random` command exists and is tested, but has not been used on real data.
7. **No one has looked at the high-edge-scaling meshes.** The metrics do not judge edge flow or how the quads look against the Rhino target, and a person should check that before M5 uses the winner. The meshes are in `tuning_runs/trials/<id>/out.obj`, and are reproducible byte for byte from `params`. I set `needsHumanCheck: false` for this unit, because the harness is checked automatically. **Task 2's winner is the mesh that needs this check.**
8. **Carried over, unchanged.** numpy and scipy come from `~/.local`; the meshes are not in git, so `RealCli` skips on a fresh clone; the target triangulation uses a fixed diagonal.

## For the next unit (M4.2, ranking)

- **The data.** Read `tuning_runs/sweep_results.jsonl` one line at a time with `json.loads`. Keep `status == 'ok'`; all 90 are ok here. The scorer's record is `r['score']`, with the same fields as M3's.
  - **Quad count:** `r['score']['quads']['delta_pct']`.
  - **Fit:** `fit.mean` and `fit_reverse.mean`.
  - **CV:** `edge_length_cv.candidate`.
  - **Degenerate quads:** `degenerate_quads.candidate`, 0 in every trial.
- **The baseline** is line 2 of `plan/baselines/m3_baseline.jsonl`.
- **Band spread.** 63 of 90 trials are within ±20 %, and 54 within ±15 %. The overall range is −97 % to +72 %.
- **More trials.** Run `scripts/tune_params.py grid --vary … --label …`, or `oat` or `random`. It appends to the same file and skips trials already recorded. Use `--timeout 300` or more above target-quads 50,000; the slowest trial so far took 40 s. `scripts/tune_params.py list` prints the whole file as a table.
- **The ranking needs Open #1 settled first.**
- **Disk.** `tuning_runs/trials/` holds 52 MB of meshes and logs. It is gitignored and safe to delete, since every mesh can be regenerated from its record's `params`.
- **The CI command** now has 12 tests, and takes about 54 s on an incremental build.
