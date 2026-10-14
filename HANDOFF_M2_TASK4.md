# Handoff — M2 Task 4: revert the mandatory quad-split (D6 reversed)

Unit `M2.4`, plan `plan/roadmap/02_M2_cli-tool.md`, section "Task 4". Branch `wrapper`.
This is now the last unit of M2.

**Status: done.** `autoremesher-cli` no longer splits its output into quads by default. It
writes the core's own polygon mesh again, which is mostly quads plus a few triangles and
pentagons. The split is still available behind a new switch, `--split-to-quads`. Both M2 exit
criteria hold as they are now worded (evidence below).

The code change is commit `81127402`. It also carries the plan notes. This handoff is
committed separately.

## What changed and why

| File | Change |
|---|---|
| `src/cli/cli_options.h` | `Options` gets `bool splitToQuads = false`. The comment on `parse` mentions the switch. |
| `src/cli/cli_options.cpp` | Parses `--split-to-quads` as a switch with no value. `--split-to-quads=…` is refused with `--split-to-quads takes no value` (exit 2). Repeating it is harmless. The help text describes the default output ("mostly quads, with a few triangles and 5- to 7-gons, counted as non-quads") and the new flag. `--target-quads` loses M2.3's "before the split" wording. |
| `src/cli/main.cpp` | The split, the stderr `remeshed to …, split into N quads` line, and the split-failure exit 1 now all sit inside `if (options.splitToQuads)`. When the flag is set, the split mesh replaces `result` (it takes over the arrays), so the write and both reports go through one path. Without the flag, the code is what M2.3 part 1 ran. |
| `tests/test_cli_options.cpp` | The default is off; the switch works between other flags and when repeated; `=yes`, `=` and a trailing `yes` are refused. |
| `tests/test_cli.cpp` | Restructured around a `readMesh()` helper. Details below. |
| `tests/test_tune_params.py` | The real-CLI check now runs the CLI through a wrapper that adds `--split-to-quads`. Details below. |
| `plan/roadmap/02_M2_cli-tool.md` | An "M2.4 measured" note under Task 4. M2.3's notes are marked as superseded or reverted. M2.1's open question is marked settled. An "M2.4 (phase end)" note is added under the exit criteria. |

The library is unchanged, as the plan requires. `autoremesher_core_split_into_quads()` and
`test_split` stay exactly as they were. Their comments still cite "decision D6", which is
historically accurate.

### Decision taken: an opt-in flag, not leaving the split unexposed

The plan left this choice to the unit. I chose **`--split-to-quads`, off by default**, for
four reasons:

- **It is cheap.** The cost is one `if` in `main.cpp` and about ten lines of parser and help
  text.
- **It keeps the library function tested end to end.** Without a flag, the CLI's call to
  `autoremesher_core_split_into_quads()` would be dead code, and only `test_split` would cover
  the function.
- **M5's parity check may want it.** M2.3 put the split in the C API so the C# demo and the
  CLI could make the same call.
- **It lets old results be reproduced exactly.** M3.2's baseline and M4.1/M4.2's sweep were
  all recorded on split output. With the flag, the current binary gives the same bytes. That
  is how `test_tune_params` keeps its check (below).

If Giacomo prefers the split fully unexposed, the change is small: remove the flag, the
`if`, and the second `test_cli` run, and give `test_tune_params` a new baseline. The last
step is M3.3's work anyway.

### The changed test checks

Only checks that encode D6's all-quad requirement were changed, and that requirement is what
this task reverts. None of them was weakened. Each check moved to the run that still has the
property.

- **`test_cli`, main run (every flag, no split).** These checks are back to their pre-D6
  form, which `test_cli` had at M2.3 part 1:
  - every output vertex lies at radius 0.98–1.001 from the sphere's centre;
  - the faces are mostly quads (at least 90 %);
  - every index is valid.

  `quadCount == faceCount` is removed from this run, because the default output is no longer
  required to be all quads. The stdout-block, report-file, no-report, help, usage-error and
  failure checks are unchanged.
- **`test_cli`, new second run (the same parameters plus `--split-to-quads`).** It keeps every
  check D6 added, and makes them stricter:
  - **All quads, every index valid.**
  - **Face count.** It must equal the corner count of the main run's unsplit output. An n-gon
    becomes n quads, so this ties the two runs together.
  - **The remesh's vertices are kept.** The first V vertices must equal the unsplit output's,
    coordinate for coordinate. This replaces the old corner-0 test for "is this a remesh
    vertex".
  - **Vertex roles.** Corner 0 of each quad must index a remesh vertex (≤ V), and corners 1–3
    must index an added vertex (> V).
  - **Added vertices** must lie at radius 0.9–1.001. The measured lowest is still 0.9526.
  - **Stdout block.** It reports the split mesh: `Non-quads: 0`, and the written mesh's quad
    and vertex counts.
- **`test_cli`, usage errors.** Added: `--split-to-quads=yes` exits 2.
- **`test_tune_params.RealCli.test_reproduces_m3s_reference_trial`.** This check passed before
  this change, and failed after it without an edit:
  - **Why it failed.** It checks that the harness reproduces line 3 of
    `plan/baselines/m3_baseline.jsonl` to every digit, and that the output's sha256 starts
    with `bc40aff3`. M3.2 recorded that line from **split** output at `--target-quads 2764`.
    The unsplit output is a different mesh, so got 2,737 quads where the baseline says 11,526.
  - **What changed.** The test now writes a two-line `/bin/sh` wrapper in its temp directory
    that runs the real CLI with `--split-to-quads` added, and passes the wrapper as `--cli`.
  - **Nothing was loosened.** The reference, the exact comparison and the sha prefix are all
    unchanged, and the test passes. So the current binary reproduces M3.2's split output
    **byte for byte**.
  - **When to undo it.** M3.3 is due to re-record the baseline on unsplit output. It should
    then drop the wrapper and update the line and the sha prefix together. The test's comment
    says so.

## Measured

Measured in the toolbox `instant-meshes-build` (Fedora, GCC 16.2.1, CMake 4.4.3, `nproc` = 8),
Release, on 2026-10-14. `source.obj` md5 is `e290fdb7ad6e507395e9c5c35387c453`, unchanged.

- **The driver's CI command, after `rm -rf build`, at `81127402`:** exit **0**, 81 s wall.
  **13/13** tests pass:
  - `autoremesher_core_no_qt` 0.01 s, `test_core_api` 0.23 s, `test_weld` 0.00 s,
    `test_split` 0.00 s;
  - `test_core` 37.08 s;
  - `autoremesher_cli_no_qt` 0.01 s, `test_cli_options` 0.01 s, `test_obj_io` 0.05 s,
    `test_report` 0.00 s, `test_cli` 0.24 s;
  - `test_score_remesh` 6.50 s, `test_tune_params` 10.07 s, `test_rank_trials` 0.35 s.

  All 30 compiler warnings come from `thirdparty/tbb`. The four changed C++ files also pass
  `g++ -std=c++17 -Wall -Wextra -Wpedantic -fsyntax-only` with no output.
- **Red first.** The new tests ran against the old binary, which always split. A
  `splitToQuads` field had been added to `Options` so the tests would compile, but the parser
  and `main.cpp` were not yet changed:
  - `test_cli_options`: **5 check(s) failed** (the switch was unknown);
  - `test_cli`: **7 check(s) failed** (`offSphere`, because the default output was split, and
    six checks on the `--split-to-quads` run, which exited 2).

  Both pass with the implementation.
- **The exit-criterion command.** `autoremesher-cli -i source.obj -o /tmp/m24_out.obj
  --target-quads 11054 --report /tmp/m24_report.txt`, run from `/tmp` under `env -u DISPLAY
  -u WAYLAND_DISPLAY -u QT_QPA_PLATFORM /usr/bin/time -v`:
  - **Run:** exit 0, wall 15.14 s, 106 % CPU, 290 MB maximum RSS.
  - **Output:** 58 status lines, then `Quads: 7913`, `Non-quads: 171`, `Vertices: 8060`,
    `Time: 15.114 seconds`. The report file has the same values and every parameter. Nothing
    is printed on stderr from the CLI itself; the `remeshed to …` line now comes only with the
    flag.
  - **The OBJ:** 8,060 vertices and 8,084 faces: 7,913 quads (**97.88 %**), 165 pentagons and
    6 triangles. 0 indices are out of range, and no face repeats an index.
    - It spans x ∈ [-65.9775, 117.889], y ∈ [-38.4004, 37.2365], z ∈ [-39.6777, 47.7157].
    - These are exactly M2.1's numbers, and the file is 393,382 bytes, the size M2.3 gave for
      the unsplit output.
  - **Determinism:** a second run's OBJ is `cmp`-identical (sha256 `7bfeaed5…`).
- **`--split-to-quads` at 11,054:**
  - stderr: `remeshed to 8060 vertices, 8084 faces (7913 quads, 171 non-quads), split into
    32495 quads`;
  - stdout: `Quads: 32495`, `Non-quads: 0`, `Vertices: 32377`, `Time: 15.088`;
  - the OBJ is all quads, 1,680,835 bytes, the same counts, box and size as M2.3's after-D6
    output.
- **Defaults** (`-i source.obj -o …`): 25,920 vertices and 25,988 faces (25,932 quads =
  99.78 %, 52 pentagons, 4 triangles), `Time: 36.896`. It spans x ∈ [-67.5181, 118.874],
  y ∈ [-38.8495, 37.7458], z ∈ [-39.7503, 47.8017], inside the box.
- **`--target-quads 2764`:** 2,811 vertices and 2,855 faces (2,737 quads = 95.87 %, 112
  pentagons, 6 triangles), 3.1 s.
- **`ldd build/autoremesher-cli | grep -ic qt`:** **0**.
- **`test_cli`'s sphere** (target 300, non-default parameters): the core makes 106 vertices
  and 104 faces, **all quads**. Split, that is 418 vertices and 416 quads, with the lowest
  added point at radius 0.9526.

## What I got wrong on the way

- **I first ran only the `test_cli*` tests after the implementation**, and treated the change
  as green. The full suite then showed `test_tune_params` failing. That test runs the real
  CLI against M3.2's split-output baseline, which I had not connected to this change. The
  fix and its reasoning are above. Run the whole suite before calling a change green.

## M2 exit criteria (phase-wide): status at phase end

- [x] **Bullet 1: runs to completion with no Qt/X11/display dependency.** **Met.**
  - **`ldd`:** it lists no Qt library (count 0).
  - **Guard:** CTest `autoremesher_cli_no_qt` passes.
  - **Run:** the 11,054-quad run exited 0 with `DISPLAY`, `WAYLAND_DISPLAY` and
    `QT_QPA_PLATFORM` unset.
- [x] **Bullet 2: non-empty polygon mesh, mostly quads (the core's ratio, ≈98 % on
  `source.obj`), every face index valid, positions inside `source.obj`'s box.** **Met as
  worded** after D6's reversal:
  - **Non-empty:** 8,084 faces and 8,060 vertices.
  - **Mostly quads:** 97.88 % (7,913 quads, 165 pentagons, 6 triangles).
  - **Indices:** every index is valid.
  - **Inside the box:** x ∈ [-65.9775, 117.889] ⊂ [-67.63, 118.88],
    y ∈ [-38.4004, 37.2365] ⊂ [-38.89, 37.80], z ∈ [-39.6777, 47.7157] ⊂ [-39.88, 47.87].
  - **Test coverage:** `test_cli` checks the "mostly quads, valid indices, on the surface"
    part on every CI run.

  The plan file needed no correction here. Giacomo had already reworded bullet 2 when
  reverting D6. I marked M2.3's "all-quad" phase-end note as superseded and added an M2.4 one.

## Open

1. **The stale D6 assumptions in M3/M4 code are outside this unit's scope.** I left them alone:
   - **`scripts/tune_params.py`.** `CENTER` has `target_quads=2764`, and `DEFAULT_SWEEP` has
     {2000 … 3500}. Its comment says "The CLI splits every face into quads (D6)". Unsplit,
     2764 gives 2,737 quads, far below the target's 11,054. **M4.3 must re-centre**:
     11,054 target quads gives 7,913, and the defaults (50,000) give 25,932. The core
     undershoots its target here, so about 15,000 would land nearer 11,054. That is a guess,
     not a measurement.
   - **`plan/baselines/m3_baseline.jsonl`** and `tuning_runs/` were all recorded on split
     output. They are M3.3's and M4.3's to redo.
   - **The `test_tune_params` wrapper** (above) should go when M3.3 re-records the baseline.
2. **`test_cli`'s sphere produces no non-quads** (104 quads, 0 others), so CI never sees the
   default path write a triangle or a pentagon. `test_core` and the `source.obj` runs above
   do. The writer and the report count non-quads from the same arrays the split path uses,
   and both were checked on `source.obj`, where the counts match.
3. **The stderr `remeshed to …` line is printed only with `--split-to-quads`.** Without the
   flag the stdout block already carries the same counts. No script in the repository parses
   that line.
4. **Should the split be exposed at all?** It is opt-in here (reasons above). If Giacomo wants
   it unexposed, see "Decision taken" for what to remove.
5. **Unchanged from earlier units:**
   - `source.obj` is still untracked (M1.4 Open #1);
   - a failed write leaves a partial OBJ or report (M2.2 Open #6);
   - output paths are checked only after the remesh (M2.1 Open #4);
   - a failed run leaves an older report in place (M2.3 Open #5);
   - the core's output has 40 non-manifold and 11 boundary edges at 11,054 (M2.3 Open #3).

## For the next unit (M3.3, rebaseline on unsplit output)

- **The CLI's command line:** `autoremesher-cli -i in.obj -o out.obj [--report r.txt]
  [--target-quads N] [--edge-scaling X] [--sharp-edge X] [--smooth-normal X]
  [--adaptivity X] [--anisotropy X] [--split-to-quads]`.
  - Exit codes: 0 on success, 1 for a runtime failure, 2 for a usage error.
- **The default output is unsplit:** quads plus a few 3- and 5-gons, and 6- and 7-gons are
  possible.
  - **Degenerate-quad rule.** M3's rule, "fewer than 4 distinct indices", must apply only to
    4-gons. Otherwise every valid triangle counts. Check `score_remesh.py` for this, as M3.3's
    plan asks; I have not.
  - **Non-manifold edges.** M2.3 Open #3 still applies: the output is not a closed manifold.
- **Expected numbers on `source.obj`, unsplit:**

  | `--target-quads` | Vertices | Faces | Quads | Time |
  |---|---|---|---|---|
  | 11,054 | 8,060 | 8,084 | 7,913 | 15.1 s |
  | defaults (50,000) | 25,920 | 25,988 | 25,932 | 36.9 s |
  | 2,764 | 2,811 | 2,855 | 2,737 | 3.1 s |

- **To reproduce any earlier split-output result,** add `--split-to-quads`. It gives the same
  bytes as before (checked by `test_tune_params`' sha prefix `bc40aff3`, and by size and counts
  at 11,054).
- **To parse a run:** unchanged. Ignore the `status ` lines on stdout, then read the block's
  `Quads:`, `Non-quads:`, `Vertices:` and `Time:`, or read the `--report` file.
- **The CI command is unchanged.** It takes about 81 s from clean, of which `test_core` is
  37 s and `test_tune_params` 10 s.
