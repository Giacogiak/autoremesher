# Handoff — M2 Task 3: progress + report output

Unit `M2.3`, plan `plan/roadmap/02_M2_cli-tool.md`, section "Task 3". Branch `wrapper`.
This is the last unit of M2.

**Status: done.** M2's two exit criteria both hold as written. The unit ran in two parts.

1. **Task 3** (commit `16bfb2e9`):
   - **Progress.** `autoremesher-cli` prints `status NN%` lines on stdout while the core works.
   - **Stdout report.** When it finishes, it prints the GUI headless mode's stdout report block.
   - **Report file.** `--report` writes the GUI's report file, line for line.

   The unit then returned `blocked`, because the "all-quad" exit criterion could not hold
   (M2.1 Open #1).
2. **Decision D6** (commit `cad93b43`). Giacomo chose **(b)**: post-process the core's result
   in the CLI, splitting every face into quads, Catmull–Clark style (an n-gon becomes n quads).
   - **The split.** It is a new core function, `autoremesher_core_split_into_quads()`, and the
     CLI always calls it after the remesh.
   - **The result.** The output is now all quads: 32,495 at `--target-quads 11054`, where the
     core makes 8,084 faces.
   - **Where it is recorded.** D6 is written into `plan/01_decisions.md`. M4's plan has a
     warning about `--target-quads`.

## What changed and why

### Part 1 — progress and reports (`16bfb2e9`)

| File | Change |
|---|---|
| `src/cli/report.{h,cpp}` (new) | Static library `autoremesher_cli_report`. It has three parts, described below: `ProgressLines`, `reportSummary`/`reportFile`, and `writeReport`. |
| `src/cli/main.cpp` | Three changes. The progress handler now prints status lines and still records the last status for the failure message. The `--report` refusal is gone. M2.1's one-line summary is replaced by the GUI's block. Timing now follows the GUI's rule (below). |
| `src/cli/obj_io.{h,cpp}` | Exposes `formatQtDouble(double)`, the writer's existing QTextStream-style number formatting, which the report file reuses. Nothing else changed. |
| `src/cli/cli_options.cpp` | Help text only. It now mentions the `status NN%` lines, and says that the report file includes the paths and the parameters. |
| `CMakeLists.txt` | Adds the `autoremesher_cli_report` library and the `test_report` test. |
| `tests/test_report.cpp` (new) | Unit test (CTest `test_report`). It checks the exact text of both reports, the two number formats (including a tie, written `123457` in the file and `123456` on stdout), `writeReport` truncating an older file and failing on a missing directory, and `ProgressLines` on repeats, going backwards, 1.0, negative, NaN and over-range input. |
| `tests/test_cli.cpp` | End to end. The main run now also passes `--report` and captures stdout. New checks below. **One existing check was changed** (below). |

New checks in `test_cli`:

- Stdout is status lines that count up to exactly `status 100%`, then the 8-line block. The
  block's counts must equal those of the OBJ that was written.
- The report file is exactly the 17 expected lines, with every non-default parameter, and its
  time equals the stdout time.
- A run without `--report` writes no report.
- A run that fails writes no report and prints nothing on stdout.
- An unwritable report path exits 1, after the OBJ has been written.

### Part 2 — all-quad output, D6 (`cad93b43`)

| File | Change |
|---|---|
| `src/quad_split.{h,cpp}` (new, in `autoremesher_core`) | `AutoRemesher::splitIntoQuads()`. |
| `src/autoremesher_core_api.{h,cpp}` | New `autoremesher_core_split_into_quads(const autoremesher_core_result* mesh, autoremesher_core_result* result)`. It returns 1 or 0, and its result is freed with `autoremesher_core_free_result()`. The header documents the exact layout. |
| `src/cli/main.cpp` | After `get_result`, it always splits, then writes and reports the **split** mesh. It prints one line to stderr with the core's own counts: `autoremesher-cli: remeshed to V vertices, F faces (Q quads, N non-quads), split into M quads`. If the split fails, it exits 1. The time now ends after the split. |
| `src/cli/cli_options.cpp` | Help text. It says every face is split into quads, and that `--target-quads` is the remesh's target before the split, giving about 4× as many faces. |
| `tests/test_split.c` (new) | C test (CTest `test_split`), described below. |
| `tests/test_cli.cpp` | The output must be all quads. The on-sphere check is split by vertex role (below). |
| `CMakeLists.txt` | Adds `src/quad_split.cpp` to the core and the `test_split` test. |
| `plan/01_decisions.md` | Adds D6, decided. |
| `plan/roadmap/02_M2_cli-tool.md` | Notes under Task 3 and the exit criteria. M2.1's "mostly quads" reading is marked superseded. |
| `plan/roadmap/04_M4_tuning_harness.md` | A warning under the `--target-quads` sweep values: re-centre them around ≈2,760. |

`test_split` covers:

- exact output for a quad, a triangle, two triangles sharing an edge, and an unreferenced
  vertex;
- 5-, 6- and 7-gons, including the centroid value;
- a closed cube, which must stay closed and consistently oriented: every directed edge used
  once, its reverse once, and V − E + F = 26 − 48 + 24;
- a face with a repeated corner;
- the empty mesh, and a double free;
- refusals: NULL arguments and arrays, a 2-corner face, an out-of-range index, and face sizes
  that do not sum to `index_count` in either direction.

#### The split, and why it is built this way

- **Topology.** It is one Catmull–Clark step without the smoothing. Each face gets a centroid,
  each edge a midpoint, and corner j of a face becomes the quad (corner j, midpoint of the edge
  leaving it, centroid, midpoint of the edge entering it). The winding is kept.
  - **Every face is split, quads included.** That is what Giacomo chose. Splitting only the
    non-quads would leave T-junctions.
  - **Shared edges stay shared.** An edge used by two faces gets one midpoint, so the result
    is as closed as the input.
  - **Measured on `source.obj`.** The core's output has 16,182 edges used twice, 40 used
    three times and 11 used once. After the split these are exactly double: 64,859, 80 and
    22. The split adds no new defect and repairs none.
- **Positions.** Plain midpoints and centroids, with no Catmull–Clark smoothing.
  - The remesher's own vertices keep their exact coordinates, which it projected onto the
    input surface. Smoothing would move them off that surface and shrink the shape.
  - Every new point is a mean of existing vertices, so the output cannot leave the core's
    bounding box.
  - **Cost:** the new points lie on chords, slightly inside a curved surface. On `test_cli`'s
    unit sphere, the lowest such point is at radius 0.953.
  - **If Giacomo wants the smoothing:** Catmull–Clark's vertex and edge rules are a local
    change in `quad_split.cpp`. Both choices count as "Catmull–Clark style".
- **Vertex order.** The input's vertices first, unchanged and in the same order, so the first
  8,060 lines of the 11,054-quad output are byte-identical to the unsplit output's. Then the
  edge midpoints, in order of first use. Then the centroids, in face order. The order is
  deterministic: the output is `cmp`-identical across runs.
- **It lives in the core's C API, not only in the CLI.** M5's parity check compares the C#
  demo's output with the CLI's.
  - If the split existed only in the CLI's C++, the C# demo would have to reimplement it, and
    any difference would look like a marshaling bug.
  - Now both callers can make the same call: weld, remesh, `get_result`, `split_into_quads`.
  - This follows M1.3's precedent for the weld: the CLI does the work, the library holds the
    code.
- **No flag.** The split always runs, as the weld does (D2). The plan does not ask for a way to
  turn it off.

### The changed test checks

Both changes encode a requirement that changed, not a weaker one.

- **`--report` refusal (part 1).** `test_cli` had `CHECK(2 == run(... --report
  '<scratch>/r.txt'))`. It asserted M2.1's **provisional** refusal, which Task 3 exists to
  remove. It is now `CHECK(2 == run(... --report))`: a `--report` with no value is still a
  usage error.
- **On-sphere check (part 2).** `test_cli` required every output vertex to lie at radius
  0.98–1.001.
  - **Why it had to change.** After the split, every midpoint and centroid sits on a chord,
    as low as radius 0.953. Against the split binary, this was the only check that failed.
  - **What the check is now.** The test uses the documented layout: corner 0 of each output
    quad is a remesher vertex, and corners 1–3 are added points.
  - **The original band still applies** to the remesher vertices, unchanged at 0.98–1.001.
  - **Added points** must lie in 0.9–1.001. The measured minimum is 0.953; a first bound of
    0.95 was too tight to be safe.
  - **New layout check.** No vertex may appear both as a corner-0 vertex and as an added point.
  - **New check:** `quadCount == faceCount`.

### The formats (part 1)

These are taken from `src/main.cpp:182-215` and `MainWindow::quadMeshReady` /
`runHeadless` (`mainwindow.cpp:1049-1186`).

- **Progress lines.**
  - **Format.** Bare `status NN%`, with no step name, as the plan wrote it. NN is
    `(int)(progress * 100)` computed in `float`, as `MainWindow::updateProgressDetailed`
    computes it. In `double`, `0.01f` would give 0, not 1.
  - **When a line is printed.** Only when the percent rises above the last line's, so the
    lines are strictly increasing and there are at most 101.
  - **Range.** The core's own progress is capped at 99. `status 100%` is printed only after
    the OBJ and the report are written. The cap exists because the core also reports 1.0
    together with a failure reason, and a failed run must not print 100 %.
  - **Flushing.** Each line is flushed, so a script reading through a pipe sees it live.
- **Stdout block.** The GUI's `std::cout` block, 8 lines from `=== AutoRemesher Report ===` to
  `===========================`, with `Time:` formatted as `std::cout` formats a double
  (`%g`).
  - It is always printed, as in the GUI.
  - It replaces M2.1's one-line summary.
- **Report file.** The GUI's 17 lines: header, the input and output paths as given, target
  quads, the five real parameters, then quads, non-quads, vertices and total time.
  - Numbers are formatted as `QTextStream` formats them, which is the same rule `writeObj`
    uses.
  - It is written in binary mode with `\n`.
- **What the counts describe (D6).** `Quads`, `Non-quads` and `Vertices` are those of the
  **written, split** OBJ, so `Non-quads` is always 0. `Target quads` is the value given to the
  core, before the split. The core's own counts go to stderr only, because the GUI's format
  has no field for them.
- **Time.**
  - **Unit.** Whole milliseconds divided by 1000, as `QElapsedTimer::elapsed() / 1000.0`.
  - **Span.** From before the input is read to the end of the split (a few milliseconds). It
    excludes writing the OBJ, as in the GUI.
- **Failure behaviour.**
  - **Exit codes are unchanged:** 1 for a runtime failure, 2 for a usage error.
  - **No report on failure.** A failed read, weld, remesh or split writes no report and no
    stdout block. The GUI instead writes a report of zeros and exits 0.
  - **Unwritable report.** An unwritable `--report` path exits 1 after the OBJ has been
    written.

## Measured

All measured in the toolbox `instant-meshes-build` (Fedora, GCC 16.2.1, CMake 4.4.3, `nproc` = 8),
Release, on 2026-10-14. `source.obj` md5 is `e290fdb7ad6e507395e9c5c35387c453`.

- **The driver's CI command, after `rm -rf build`, at `cad93b43`:** exit **0** in 64 s wall.
  **10/10** tests pass: `autoremesher_core_no_qt` 0.01 s, `test_core_api` 0.23 s, `test_weld`
  0.00 s, `test_split` 0.00 s, `test_core` 36.88 s, `autoremesher_cli_no_qt` 0.01 s,
  `test_cli_options` 0.00 s, `test_obj_io` 0.05 s, `test_report` 0.00 s, `test_cli` 0.20 s.
  - All 30 compiler warnings are from `thirdparty/tbb`.
  - The new and changed files also compile cleanly under `-Wall -Wextra -Wpedantic`
    (`test_split.c` as C99).
- **Red first:**
  - `test_report` against a stub `report.cpp`: `16 check(s) failed`.
  - `test_cli` against the binary that still refused `--report`: `13 check(s) failed`.
  - `test_split` against a `splitIntoQuads` that returned false: `36 check(s) failed`.
  - Each passes with its implementation.
- **The exit-criterion command, with `--report`, after D6:**
  `autoremesher-cli -i source.obj -o /tmp/m23b_out.obj --target-quads 11054 --report …`, run
  from `/tmp` with `DISPLAY`, `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM` unset, under
  `/usr/bin/time -v`:
  - **Run:** exit 0, wall 15.06 s, 106 % CPU, 285 MB maximum RSS.
  - **Stderr:** `remeshed to 8060 vertices, 8084 faces (7913 quads, 171 non-quads), split
    into 32495 quads`. That is 7,913 × 4 + 165 × 5 + 6 × 3 = 32,495.
  - **Stdout:** 58 status lines, then `Quads: 32495`, `Non-quads: 0`, `Vertices: 32377`,
    `Time: 15.004 seconds`. The report file has the same values.
  - **The OBJ:** 32,377 vertices and 32,495 faces, **all of size 4**, with 0 invalid indices
    and 0 quads with a repeated index.
    - It spans x ∈ [-65.9775, 117.889], y ∈ [-38.4004, 37.2365], z ∈ [-39.6777, 47.7157].
      These are the unsplit output's extremes exactly.
    - Its first 8,060 vertices equal the unsplit output's.
    - V − E + F is −89, before and after the split.
  - **Determinism:** a second run's OBJ is `cmp`-identical.
  - **Size:** 1,680,835 bytes, against 393,382 unsplit.
- **Defaults** (`-i source.obj -o …`): the core gave 25,920 vertices and 25,988 faces (25,932
  quads), the same as M1, and the split gave **104,000 quads**, 103,908 vertices, `Time:
  37.376`. Wall 37.55 s, 678 MB maximum RSS.
- **`--target-quads 2764`** (≈ 11,054 / 4): the core made 2,855 faces (2,737 quads, 118
  non-quads), split into **11,526 quads** and 11,415 vertices, in 3.1 s.
- **Progress through a pipe** (part 1, before D6, same command): 58 status lines spread over
  the run, from `status 0%` at 0.09 s to `status 99%` at 15.27 s, then `status 100%` and the
  block at 15.33 s. The flush works. Progress is uneven in time: 70 % at 7.7 s, then about
  1 % every 0.45 s from 73 % to 82 %.
- **`ldd build/autoremesher-cli`:** `grep -ic qt` gives **0**. `libautoremesher_core.so` is
  1,372,368 bytes.
- **`test_cli`'s sphere** at the non-default settings: the core made 104 faces, split into 416
  quads and 418 vertices. The lowest added point is at radius 0.9526.

## What I got wrong on the way

- **A scripted edit to `test_cli.cpp` turned a `\n` inside a C string into a real newline.**
  The build failed, and ctest then ran the **old** `test_cli` binary and reported "Passed". A
  pass only counts when the build also succeeded.
- **The first stub run of `test_split` showed no failure count.** I piped it through
  `tail` and the line was cut off. I re-ran it and counted the failures properly (36).
- **My first bound for the added points in `test_cli` (radius > 0.95) sat 0.003 from the
  measured minimum.** I widened it to 0.9 with the reason in a comment.
- **In part 1's handoff I first counted 63 status lines, and claimed the count varies between
  runs.** Both runs had 58; I corrected it.
- **In a plan note's first draft I referred to Giacomo as "he".** I corrected it before
  committing.

## M2 exit criteria (phase-wide): status at phase end

- [x] **Bullet 1: runs to completion with no Qt/X11/display dependency.** **Met.**
  - **Evidence:** `ldd` lists no Qt library (count 0).
  - **Guard:** CTest `autoremesher_cli_no_qt` passes.
  - **Run:** the 11,054-quad run completed with `DISPLAY`, `WAYLAND_DISPLAY` and
    `QT_QPA_PLATFORM` unset.
- [x] **Bullet 2: output OBJ is all-quad, non-empty, positions within `source.obj`'s bounding
  box.** **Met as written**, with D6:
  - **Non-empty:** 32,495 faces and 32,377 vertices, every index valid.
  - **All-quad:** every face has 4 indices, and none repeats an index.
  - **Inside the box:** x ∈ [-65.9775, 117.889] ⊂ [-67.63, 118.88],
    y ∈ [-38.4004, 37.2365] ⊂ [-38.89, 37.80], z ∈ [-39.6777, 47.7157] ⊂ [-39.88, 47.87].
  - **Test coverage:** `test_cli` checks the all-quad part on every CI run.

## Open

1. **For M4, and Giacomo to know: `--target-quads` now gives about 4× the quads.**
   - 11054 gives 32,495 quads, and 2764 gives 11,526.
   - M4's sweep values {8000 … 14000} would all overshoot the target's 11,054 by about
     3–5×. I added a warning to `04_M4_tuning_harness.md` that suggests ≈ {2000, 2400, 2764,
     3100, 3500}. Choosing the values is still M4's job.
   - Smaller targets are also much faster: 3.1 s against 15 s.
2. **The split's positions are linear: midpoints and centroids, no smoothing.**
   - This was my choice within "Catmull–Clark style" (reasons above).
   - **Cost:** the added points sit slightly inside curved surfaces, which may show in M3's
     spatial-fit metric.
   - **Alternative:** Catmull–Clark smoothing is a local change, if Giacomo wants it. But it
     moves the remesher's own vertices off the surface too.
3. **The core's output has 40 non-manifold edges (used three times) and 11 boundary edges** on
   `source.obj` at 11,054. The split keeps them, as 80 and 22. M3's scorer should not assume a
   closed manifold.
4. **The stdout summary changed format.** M2.1's one-line summary is gone, replaced by the
   GUI's block. Nothing in the repository parsed it.
5. **A stale report survives a failed run.** On failure the CLI writes no report, but it does
   not delete one left at that path by an earlier run. M4 should key on the exit code, or
   delete the report path before each trial.
6. **The progress percent is not a time estimate.** It is uneven, and percents are skipped
   (58 lines, not 101). Use it as a liveness signal. Base per-trial timeouts on wall time.
7. **The report formats were derived from the GUI's source, not from a GUI run.** Qt cannot be
   built here. No byte comparison against a real GUI report has been done. After D6, the
   counts in the two tools' reports also mean different things: the GUI reports its unsplit
   mesh.
8. **Unchanged from earlier units:**
   - `source.obj` is still untracked (M1.4 Open #1);
   - a failed write leaves a partial OBJ or report (M2.2 Open #6);
   - output paths are checked only after the remesh (M2.1 Open #4).

## For the next unit (M3.1, `scripts/score_remesh.py`)

- **The CLI's command line is final for M2:** `autoremesher-cli -i in.obj -o out.obj [--report
  r.txt] [--target-quads N] [--edge-scaling X] [--sharp-edge X] [--smooth-normal X]
  [--adaptivity X] [--anisotropy X]`.
  - Exit codes: 0 on success, 1 for a runtime failure, 2 for a usage error.
  - Out-of-range values are refused with exit 2.
- **The output OBJ is all quads** (D6). The degenerate-quad metric as M3's plan defines it
  (fewer than 4 distinct indices) applies directly; it measured 0 here.
- **To parse a run:**
  - Ignore stdout lines starting with `status `. Then read `Quads:`, `Non-quads:`,
    `Vertices:` and `Time: <x> seconds` from the block.
  - Or read the `--report` file, which has the same values and every parameter.
  - The core's pre-split counts are on stderr, in the `remeshed to …` line, among the core's
    other diagnostics.
- **Expected numbers on `source.obj`, after the split:**
  - 11,054 target: 32,377 vertices and 32,495 quads, about 15 s;
  - defaults: 103,908 vertices and 104,000 quads, about 37 s;
  - 2,764 target: 11,415 vertices and 11,526 quads, about 3 s.
- **M3's baseline** is "the CLI's default-parameter output". That is now 104,000 quads against
  the Rhino target's 11,054. The quad-count delta will be large, as D6 accepted. M3 may want
  to record a baseline at a target near 2,764 as well.
- **The CI command is unchanged.** It takes about 64 s from clean, of which `test_core` is
  37 s.
