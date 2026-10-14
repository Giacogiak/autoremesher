# Handoff — M2 Task 3: progress + report output

Unit `M2.3`, plan `plan/roadmap/02_M2_cli-tool.md`, section "Task 3". Branch `wrapper`.
This is the last unit of M2.

**Status: blocked, on one decision that is Giacomo's.** Task 3 itself is done and committed
(`16bfb2e9`):

- **Progress.** `autoremesher-cli` prints `status NN%` lines on stdout while the core works.
- **Stdout report.** When it finishes, it prints the GUI headless mode's stdout report block.
- **Report file.** `--report` writes the GUI's report file, line for line.

M2's exit criteria do not both hold yet:

- **No Qt: holds.**
- **"All-quad": holds only under M2.1's provisional reading.** That reading counts a mesh
  with 2.1 % non-quads as meeting the bullet. Giacomo has not yet accepted or rejected it
  (M2.1 Open #1, unanswered through M2.2 and M2.3). It is the one question returned with this
  unit. See "M2 exit criteria" below.

## What changed and why

| File | Change |
|---|---|
| `src/cli/report.{h,cpp}` (new) | Static library `autoremesher_cli_report`. It has three parts, described below: `ProgressLines`, `reportSummary`/`reportFile`, and `writeReport`. |
| `src/cli/main.cpp` | Three changes. The progress handler now prints status lines and still records the last status for the failure message. The `--report` refusal is gone. M2.1's one-line summary is replaced by the GUI's block. Timing now follows the GUI's rule (below). |
| `src/cli/obj_io.{h,cpp}` | Exposes `formatQtDouble(double)`, the writer's existing QTextStream-style number formatting. The report file reuses it. Nothing else changed, and the OBJ output is unchanged. |
| `src/cli/cli_options.cpp` | Help text only. It now mentions the `status NN%` lines, and says that the report file includes the paths and the parameters. |
| `CMakeLists.txt` | Adds the `autoremesher_cli_report` library and the `test_report` test, and links the library into `autoremesher-cli`. |
| `tests/test_report.cpp` (new) | Unit test (CTest `test_report`). It checks the exact text of both reports, the two number formats (including a tie, written `123457` in the file and `123456` on stdout), `writeReport` truncating an older file and failing on a missing directory, and `ProgressLines` on repeats, going backwards, 1.0, negative, NaN and over-range input. |
| `tests/test_cli.cpp` | End to end. The main run now also passes `--report` and captures stdout. Several checks are new, listed below. **One existing check was changed** (below). |
| `plan/roadmap/02_M2_cli-tool.md` | An "M2.3 measured" note under Task 3, and a phase-end status note under the exit criteria. |

New checks in `test_cli`:

- Stdout is status lines that count up to exactly `status 100%`, then the 8-line block. The
  block's counts must equal those of the OBJ that was written.
- The report file is exactly the 17 expected lines, with every non-default parameter, and its
  time equals the stdout time.
- A run without `--report` writes no report.
- A run that fails writes no report and prints nothing on stdout.
- An unwritable report path exits 1, after the OBJ has been written.

### The changed test check

`test_cli` had `CHECK(2 == run(... --report '<scratch>/r.txt'))`. It asserted M2.1's
**provisional** refusal ("--report is not implemented yet"), which this task exists to
remove. That check could not stay. It is now `CHECK(2 == run(... --report))`: a `--report`
with no value is still a usage error. The other checks in the file are unchanged.

### The formats

These are taken from `src/main.cpp:182-215` and `MainWindow::quadMeshReady` /
`runHeadless` (`mainwindow.cpp:1049-1186`).

- **Progress lines.**
  - **Format.** Bare `status NN%`, with no step name, as the plan wrote it. NN is
    `(int)(progress * 100)` computed in `float`, as `MainWindow::updateProgressDetailed`
    computes it. In `double`, `0.01f` would give 0, not 1.
  - **When a line is printed.** Only when the percent rises above the last line's, so the
    lines are strictly increasing and there are at most 101.
  - **Range.** The core's own progress is capped at 99. `status 100%` is printed only after
    the OBJ and the report are written. The cap exists because the core also reports progress
    1.0 together with a failure reason (`autoremesher_core_api.h`), and a failed run must not
    print 100 %.
  - **Flushing.** Each line is flushed, so a script reading through a pipe sees it live
    (measured below).
- **Stdout block.** The GUI's `std::cout` block, 8 lines from `=== AutoRemesher Report ===` to
  `===========================`, with `Time:` formatted as `std::cout` formats a double
  (`%g`).
  - It replaces M2.1's one-line summary, so that both tools print the same block.
  - The GUI prints it always, whether or not `--report` is given, and so does the CLI.
- **Report file.** The GUI's 17 lines: header, the input and output paths as given on the
  command line, target quads, the five real parameters, then quads, non-quads, vertices and
  total time.
  - Numbers are formatted as `QTextStream` formats them, which is the same rule
    `writeObj` uses.
  - In the GUI, the file goes through `QTextStream` and the block goes through `std::cout`,
    and the two round an exact six-digit tie differently. Each output here follows its own GUI
    counterpart.
  - It is written in binary mode with `\n`. The GUI's `QIODevice::Text` is also `\n` on
    Linux (D3: Linux only).
- **Time.**
  - **Unit.** Whole milliseconds divided by 1000, as `QElapsedTimer::elapsed() / 1000.0`.
  - **Span.** From before the input is read to the end of the remesh. Like the GUI, this
    includes reading (and, here, welding) and **excludes** writing the OBJ.
  - **What changed.** M2.1's summary included the write and printed `%.1f`.
- **Failure behaviour.**
  - **Exit codes are unchanged:** 1 for a runtime failure, 2 for a usage error.
  - **No report on failure.** A failed read, weld or remesh writes no report and no stdout
    block. This differs from the GUI, which on "Remeshing produced no result" prints and
    writes a report of zeros and still exits 0. A script should rely on the exit code.
  - **Unwritable report.** An unwritable `--report` path exits 1 with
    `cannot write <path>: <strerror>`. By then the OBJ has already been written. The GUI
    silently skips a report file it cannot open.

## Measured

All measured in the toolbox `instant-meshes-build` (Fedora, GCC 16.2.1, CMake 4.4.3, `nproc` = 8),
Release, on 2026-10-14. `source.obj` md5 is `e290fdb7ad6e507395e9c5c35387c453`.

- **The driver's CI command, after `rm -rf build`:** exit **0** in 64 s wall. **9/9** tests pass:
  `autoremesher_core_no_qt` 0.01 s, `test_core_api` 0.23 s, `test_weld` 0.00 s, `test_core`
  37.01 s, `autoremesher_cli_no_qt` 0.01 s, `test_cli_options` 0.00 s, `test_obj_io` 0.05 s,
  `test_report` 0.00 s, `test_cli` 0.19 s (it was 0.11 s; it now runs the sphere twice).
  - All 30 compiler warnings in the log are from `thirdparty/tbb`.
  - The changed and new files also compile cleanly under
    `-std=c++14 -Wall -Wextra -Wpedantic -fsyntax-only`.
- **Red first.** Against a stub `report.cpp`, with the new tests in place:
  - `test_report` reported `16 check(s) failed`;
  - `test_cli`, run against the binary that still refused `--report`, reported
    `13 check(s) failed`.

  With the implementation, both pass.
- **The exit-criterion command, with `--report`:**
  `autoremesher-cli -i source.obj -o /tmp/m23_out.obj --target-quads 11054 --report
  /tmp/m23_report.txt`, run from `/tmp` with `DISPLAY`, `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM`
  unset, under `/usr/bin/time -v`:
  - exit 0, wall 15.15 s, 106 % CPU, 285 MB maximum RSS (M2.2: 15.19 s, 290 MB);
  - stdout: 66 lines, 58 status lines from `status 0%` to `status 100%`, then the block:
    `Quads: 7913`, `Non-quads: 171`, `Vertices: 8060`, `Time: 15.124 seconds`;
  - the report file carried the same counts and time, `Target quads: 11054`, and
    `Edge scaling: 1`, `Sharp edge degrees: 90`, `Smooth normal degrees: 0`, `Adaptivity: 1`,
    `Anisotropy: 1`;
  - the OBJ has 8,060 vertices and 8,084 faces (7,913 quads, 165 pentagons, 6 triangles), with
    0 invalid indices. It spans x ∈ [-65.9775, 117.889], y ∈ [-38.4004, 37.2365],
    z ∈ [-39.6777, 47.7157]. These are M2.1's and M2.2's numbers exactly.
- **Progress through a pipe**, with every line timestamped by the reader as it arrived (second
  run, same command without `--report`):
  - The run produced **58** status lines, the same as the first run: from `status 0%` at
    0.09 s to `status 99%` at 15.27 s, then `status 100%` and the block at 15.33 s.
  - The lines arrive spread over the run, not all at the end. So the flush works.
  - **The progress is uneven in time.** It reaches 70 % at 7.7 s, then climbs about 1 % every
    0.45 s from 73 % to 82 % (9.4–13.2 s).
  - **Determinism.** This second run's OBJ is byte-identical (`cmp`) to the first's.
  - `Time:` was `15.31 seconds`.
- **`ldd build/autoremesher-cli`:** `linux-vdso`, `libautoremesher_core.so`, `libstdc++`, `libm`,
  `libgcc_s`, `libc` and `ld-linux`. `grep -ic qt` gives **0**.
- **The synthetic sphere in `test_cli`** produced 57 stdout lines: 49 status lines and the
  8-line block.

## What I got wrong on the way

- **A Python-scripted edit to `test_cli.cpp` turned a `\n` inside a C string into a real
  newline**, and the first build failed on it. Because the build failed, ctest ran the old
  `test_cli` binary and reported it as "Passed". I caught this from the compiler error, not
  from the test result. A stale binary can pass after a failed build, so a pass only counts
  when the build also succeeded.
- **In the plan note's first draft I referred to Giacomo as "he".** I corrected it before
  committing.

## M2 exit criteria (phase-wide): status at phase end

- [x] **Bullet 1: runs to completion with no Qt/X11/display dependency.** **Met.**
  - **Evidence:** `ldd` lists no Qt library (count 0 above).
  - **Guard:** CTest `autoremesher_cli_no_qt` passes in the CI run.
  - **Run:** the 11,054-quad run above completed with `DISPLAY`, `WAYLAND_DISPLAY` and
    `QT_QPA_PLATFORM` unset.
- [ ] **Bullet 2: output OBJ is all-quad, non-empty, positions within `source.obj`'s
  bounding box.** **Not closed, because a decision is needed.**
  - **Non-empty: met.** 8,060 vertices and 8,084 faces, every index valid.
  - **Bounding box: met.** Every side of the output's box is inside `source.obj`'s
    x ∈ [-67.63, 118.88], y ∈ [-38.89, 37.80], z ∈ [-39.88, 47.87], with no margin needed.
  - **All-quad: not met as written.** 7,913 of the 8,084 faces are quads (97.9 %); 165 are
    pentagons and 6 are triangles.
  - **Under M2.1's reading it is met.** M2.1's correction note in the plan reads the bullet as
    "a non-empty polygon mesh, mostly quads (≈98 % here), every face index valid", and on
    that reading the bullet holds.
  - **Why this is returned as blocked.** M2.1 marked that reading provisional, and it stays
    provisional until Giacomo chooses. The question is below and is the reason this unit
    returns `blocked`.

## Open

1. **For Giacomo: accept polygon output, or require all-quad?** This is M2.1 Open #1, restated
   with this phase's evidence.
   - **(a) Accept polygons.**
     - **Change:** amend the exit criterion to "mostly quads; non-quads counted and
       reported".
     - **Cost:** none. No code changes: the report already has a `Non-quads:` line in both
       outputs, and the GUI's export keeps polygons too.
     - **For M3:** the scorer must handle 3- to 7-gons. As M3's plan defines it, the
       "degenerate-quad" metric (fewer than 4 distinct indices) would count every valid
       triangle as degenerate, so M3 must say how non-quads are scored.
   - **(b) Split every face into quads in the CLI**, as one Catmull–Clark-style step: an
     n-gon becomes n quads.
     - **Result:** always all-quad.
     - **Cost:** about 4× the face count, so `--target-quads` would no longer mean what it
       says. The output would also differ from the GUI's.
     - **Variant:** split only the non-quads. A pentagon cannot become all quads without
       changing its neighbours, so this adds T-junctions or forces neighbours to be split
       too.
   - **(c) Fix `QuadExtractor` in the core.** This is an algorithm change of unknown size and
     is not in this plan.
2. **The stdout summary changed format.** M2.1's one-line summary,
   `<out>: N vertices, N faces (N quads, N non-quads), N.N s`, is gone, replaced by the GUI's
   block. Nothing in the repository parsed it. Earlier handoffs quote it, so quotes of it in
   them are historical.
3. **A stale report survives a failed run.** On failure the CLI writes no report, but it does
   not delete a report file left at that path by an earlier run either. M4's harness should
   key on the exit code, or delete the report path before each trial. Deleting it in the CLI
   would also be easy, if Giacomo prefers that.
4. **The progress percent is not a time estimate.** It is uneven in time (measured above),
   and percents are skipped (58 lines, not 101). Use it as a liveness signal, not to predict
   the end of a trial. Base M4's per-trial timeout on wall time (≈15 s at 11,054 quads, ≈37 s at
   the 50,000 default).
5. **The report formats were derived from the GUI's source, not from a GUI run.** The GUI
   cannot be built or run here, because the toolbox has no Qt and the host has no compiler.
   The number formatting relies on M2.2's measurement against the real Qt 6.11
   `QTextStream`. The texts were copied from `src/main.cpp`. A byte comparison against a real
   GUI report file has not been done.
6. **Unchanged from earlier units:**
   - `source.obj` is still untracked (M1.4 Open #1);
   - a failed write leaves a partial OBJ (M2.2 Open #6); this now applies to a partial
     report too;
   - the output paths are checked only after the remesh (M2.1 Open #4).

## For the next unit (M3.1, `scripts/score_remesh.py`)

- **The CLI's command line is final for M2:** `autoremesher-cli -i in.obj -o out.obj [--report
  r.txt] [--target-quads N] [--edge-scaling X] [--sharp-edge X] [--smooth-normal X]
  [--adaptivity X] [--anisotropy X]`.
  - Exit codes: 0 on success, 1 for a runtime failure, 2 for a usage error.
  - Out-of-range values are refused with exit 2.
- **To parse a run:**
  - Ignore stdout lines starting with `status `. Then read `Quads:`, `Non-quads:`,
    `Vertices:` and `Time: <x> seconds` from the block.
  - Or read the `--report` file instead, which has the same values (indented two spaces under
    `Results:`) and every parameter.
  - The core's diagnostic chatter is all on stderr.
- **The output OBJ has polygons, not only quads** (Open #1, until Giacomo decides). The face
  sizes seen are 3 to 7.
- **Expected numbers on `source.obj`, unchanged since M1:**
  - 11,054 target quads: 8,060 vertices, 8,084 faces, 7,913 quads, about 15 s;
  - defaults: 25,920 vertices, 25,988 faces, 25,932 quads, about 37 s.
- **The CI command is unchanged.** It takes about 64 s from clean, of which `test_core` is
  37 s.
