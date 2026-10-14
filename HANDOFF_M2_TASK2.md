# Handoff — M2 Task 2: OBJ I/O without Qt

Unit `M2.2`, plan `plan/roadmap/02_M2_cli-tool.md`, section "Task 2". Branch `wrapper`.

**Status: done.** `autoremesher-cli` now has a real OBJ reader and writer, with their own tests,
in place of M2.1's provisional copy of `test_core`'s loader.

- **Reader.** It takes `source.obj`'s `v x y z 0 0 0` lines. It discards the trailing tokens and
  keeps the exact coordinates.
- **Writer.** Its output is byte-identical to what the GUI's `MainWindow::saveMeshToFile` writes
  for the same mesh. This was checked against the real Qt `QTextStream`, not assumed.
- **No change to the mesh.** On `source.obj` the CLI produces exactly M2.1's numbers, and apart
  from the header its output file is byte-identical to M2.1's.
- **Measured, not fixed: the core is very sensitive to tiny input changes.** If the coordinates
  are rounded through `float`, as the GUI's loader rounds them, the 11,054-quad result moves from
  8,084 faces to 8,503. M4 needs to know this (Open #1).

## What changed and why

| File | Change |
|---|---|
| `src/cli/obj_io.{h,cpp}` | **Rewritten.** The `readObj`/`writeObj` signatures are unchanged, so `main.cpp`'s calls did not change. Details below. |
| `CMakeLists.txt` | `obj_io.cpp` is now the static library `autoremesher_cli_obj_io`, so the test can link it without running a remesh. CMake reads `QMAKE_TARGET_PRODUCT`, `HUMAN_VERSION` and `HOMEPAGE_URL` from `autoremesher.pro` and passes them in as `AUTOREMESHER_APP_*` definitions. If any of the three is missing, configuring fails. The new test `test_obj_io` is added. |
| `tests/test_obj_io.cpp` (new) | Unit test of the reader and writer (CTest `test_obj_io`). It is given `source.obj` as an optional argument and checks its counts when the file exists, as `test_core` does. |
| `tests/test_cli.cpp` | **Two checks added; none changed.** The output's first line must start with `# AutoRemesher `. An input whose face names a vertex that does not exist must exit 1 and write nothing. |
| `src/cli/main.cpp` | Only the weld-failure message changed. The reader now refuses bad indices and non-finite coordinates, so the remaining causes are a coordinate beyond about 9.2e13 or running out of memory. |
| `tests/manual/qtextstream_probe.cpp` (new) | Not built by CMake. A tool that prints doubles through the real Qt 6 `QTextStream`, to check the writer against Qt. How to run it is in its header, and why it exists is below. |
| `plan/roadmap/02_M2_cli-tool.md` | An "M2.2 measured" note under Task 2. |

### The reader

- **`v` lines.** The first three tokens must each be entirely a finite number, read with `strtod`
  as a double. Everything after them is discarded, numeric or not: `source.obj`'s `0 0 0` vertex
  colours, a `w`, anything else.
- **`f` lines.** Each corner may be `v`, `v/vt`, `v//vn` or `v/vt/vn`, and only `v` is used.
  - A negative index counts back from the last vertex read so far, as in the OBJ spec and
    tinyobjloader's `fixIndex`.
  - A positive index may name a vertex defined later in the file. It is checked against the final
    vertex count at the end of the file, and the error names the line where the largest index
    appears.
  - A polygon becomes a fan around its first corner. For a convex polygon, tinyobjloader's ear
    clipping gives exactly the same fan. For a non-convex one the two differ; see Open #4.
- **Other lines are ignored:** `vt`, `vn`, `vp`, `l`, `p`, `g`, `o`, `s`, `usemtl`, `mtllib` and
  comments. The reader also takes CRLF line ends, tabs, leading whitespace and a missing final
  newline.
- **It fails, with `path:line: reason`, on:**
  - a missing, non-numeric, partly numeric (`2.5x`), NaN or infinite coordinate, or one beyond
    the range of a double (`1e999`);
  - an index of 0, or one that is not a whole number (`two`, `2x`, `2.0`), or beyond 32 bits;
  - a positive index past the last vertex, or a negative one before the first;
  - a face with fewer than 3 corners. tinyobjloader drops such a face silently. I made it an
    error, in line with M2.1's choice to refuse bad input rather than ignore it.

  It also fails, with `path: reason`, on a file with no faces. Before this, an empty mesh only
  failed later, at the weld, with a misleading message.
- **The output arrays are replaced, not appended to.** Nothing is welded: `main.cpp` still welds
  unconditionally (D2).

### The writer

- **Header.** The GUI's two header lines, `# AutoRemesher 1.2.0` and
  `# https://autoremesher.dust3d.org/`, with their values taken from `autoremesher.pro` at
  configure time. The values are read rather than written into the code, so a version bump in the
  `.pro` file reaches both tools and they stay comparable.
- **Lines.** Then `v x y z` per vertex and `f a b c …` per face, with `face_sizes[i]` 1-based
  indices each. Lines end with `\n`, and the file is written in binary mode.
- **Numbers.** They are written as `QTextStream` writes a `double` by default, which is **not**
  exactly printf's `%g`. The real Qt 6.11 `QTextStream` differs from glibc's `%g` in two ways:
  - **negative zero** is written as `0`, where `%g` writes `-0`;
  - **an exact tie** at six significant digits **rounds away from zero**: `123456.5` is
    written `123457` and `-12345.25` is written `-12345.3`. `%g` rounds a tie to even, giving
    `123456` and `-12345.2`.

  `formatNumber` in `obj_io.cpp` does both. A value is an exact tie when its `%.6e` text ends in
  `5` and `strtod` of that text gives back the same double; such a value is moved one ulp away from
  zero before it is formatted with `%g`.
- **Errors.** A failure to open the file, a write error or a failed close returns false, with
  `strerror` in the message where there is one.

## Measured

All measured in the toolbox `instant-meshes-build` (Fedora, GCC 16.2.1, CMake 4.4.3, `nproc` = 8),
Release, on 2026-10-14. `source.obj` is 6,672,315 bytes, with md5 `e290fdb7ad6e507395e9c5c35387c453`.

- **The driver's CI command, after `rm -rf build`:** exit **0** in 64 s wall. 8/8 tests pass:
  `autoremesher_core_no_qt` 0.01 s, `test_core_api` 0.23 s, `test_weld` 0.00 s, `test_core`
  36.93 s, `autoremesher_cli_no_qt` 0.01 s, `test_cli_options` 0.00 s, `test_obj_io` 0.05 s,
  `test_cli` 0.11 s.
  - All 30 compiler warnings in the log are from `thirdparty/tbb`; none is in this repository's
    own code.
  - The changed and new files also compile cleanly under `-Wall -Wextra -Wpedantic` with
    `-fsyntax-only`.
- **Red first.** With the new `test_obj_io` built against M2.1's provisional `obj_io.cpp`, it
  reported `29 check(s) failed`:
  - accepted inputs that should be refused (2-corner face, zero index, `2x`, `2.0`, too-high and
    too-low indices, empty file, no faces);
  - no negative indices;
  - the wrong header;
  - `-0`, and half-to-even ties.

  With the new code it passes.
- **Reader on `source.obj`:** 97,400 vertices and 175,566 triangles. The first vertex is exactly
  (105.67, -0.60052, 44.038). The whole `test_obj_io` run, which includes reading the file,
  takes 0.04 s on each of three runs; without `source.obj` it takes 0.00 s.
- **Writer against real Qt.**
  - **Method.** 200,004 doubles went through `writeObj` and, separately, through the host's real
    `libQt6Core.so.6` 6.11.1 `QTextStream` (the probe, reading values on stdin). The lines were
    then compared. The values:
    - 60,000 uniform in [-200, 200], like the coordinates;
    - 30,000 spread over 10⁻¹²–10¹²;
    - 90,000 constructed exact ties: 7-digit integers ending in 5, `n.25`/`n.75`, and `n.5`
      scaled by powers of 2;
    - 20,000 values one ulp either side of a tie;
    - ±0 and the smallest denormal.
  - **Result: 0 mismatches.** Plain `%g` would have differed from Qt on **30,760** of them, all
    ties or negative zero.
  - Ties and `-0` are rare in real remesher output: the 11,054-quad output below contains no
    `-0`. So the case handling matters for byte parity, not for geometry.
- **The exit-criterion command:** `./autoremesher-cli -i source.obj -o /tmp/out.obj
  --target-quads 11054`, with `DISPLAY`, `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM` unset.
  - It printed `8060 vertices, 8084 faces (7913 quads, 171 non-quads), 15.2 s`. Wall time was
    15.19 s, at 106 % CPU and 290 MB maximum RSS. These are M2.1's numbers exactly.
  - The faces are 7,913 quads, 165 pentagons and 6 triangles.
  - The output spans x ∈ [-65.9775, 117.889], y ∈ [-38.4004, 37.2365], z ∈ [-39.6777, 47.7157].
    That is inside `source.obj`'s box on every side.
  - **Against M2.1's output:** I built commit `440c44c9` (M2.1) in a temporary worktree and ran
    the same command. The two files differ only in the header: M2.1 wrote `# autoremesher-cli`,
    and this unit writes the GUI's two lines. Every `v` and `f` line is identical (`diff` of the
    files without their `#` lines). So the new reader gives the core exactly the same mesh.
- **The defaults** (`-i source.obj -o …`): `25920 vertices, 25988 faces (25932 quads,
  56 non-quads), 36.9 s`, 675 MB maximum RSS. These are M1's and M2.1's numbers.
- **`ldd build/autoremesher-cli`:** 0 lines mention Qt. The binary is 61,568 bytes; it was 53,760.
- **Input rounded through `float`.** This is how the GUI's tinyobjloader reads coordinates:
  `real_t` is `float` unless `TINYOBJLOADER_USE_DOUBLE` is defined, and the GUI does not define
  it.
  - I wrote a copy of `source.obj` with every coordinate rounded through `float`. That changes
    289,997 of its 292,200 coordinates, each by at most about 4 × 10⁻⁶.
  - At `--target-quads 11054`, the copy gives **8,459 vertices and 8,503 faces (8,369 quads,
    126 pentagons, 8 triangles) in 14.2 s**. The double input gives 8,060 vertices and 8,084 faces.
  - **This is not a prediction of the GUI's output.** The GUI also does not weld its input,
    while the CLI always does (D2). I could not run the GUI here, because the toolbox has no Qt
    and the host has no compiler.

## What I got wrong on the way

- **I first wrote the writer's expected output as plain `%g`**, with `-0` and half-to-even ties,
  on the assumption that `QTextStream`'s default format is `%g`. Only the probe against the real
  Qt library showed the two differences. Without it, the writer would have claimed GUI parity it
  did not have.
- **I nearly reported the float-rounded run as "what the GUI would produce".** It is not, because
  the GUI does not weld. See the last item under Measured.
- **The first draft of `test_obj_io` hardcoded `1.2.0`** in the expected header, so a version
  bump in `autoremesher.pro` would have broken an unrelated test. The test now builds the
  expected header from the same CMake definitions. It also checks the `# AutoRemesher ` prefix
  literally, so a failed `.pro` parse is still caught.

## M2 exit criteria (phase-wide; held at M2.3): status after this unit

- [x] *Runs to completion with no Qt/X11/display dependency.* **Met**, and re-measured above:
  `ldd` shows no Qt, CTest `autoremesher_cli_no_qt` passes, and the run worked with no display
  variables set.
- [ ] *Output OBJ is all-quad, non-empty, positions within `source.obj`'s bounding box.*
  - **Non-empty: yes,** re-measured with this unit's writer.
  - **Bounding box: yes,** re-measured with this unit's writer.
  - **All-quad: still no.** M2.1's Open #1 is still waiting for Giacomo's answer: accept polygons,
    post-process them, or change `QuadExtractor`. This unit changes nothing there.

## Open

1. **For M3/M4, and Giacomo to know: the core's output is very sensitive to tiny input changes.**
   Moving coordinates by at most about 4 × 10⁻⁶, on a model about 186 units across, changes the
   11,054-quad result by 5 % in face count (8,084 → 8,503). The quad count also rises, from 7,913
   to 8,369. Expect results across M4's sweep, and between the CLI and the GUI, to be noisy at this
   level. Nearby parameter values may not give smoothly varying scores.
2. **The CLI's geometry is not the GUI's, by design, so only the file format is diff-comparable.**
   There are three causes:
   - the weld (D2);
   - the GUI reads coordinates as `float` (this unit; measured above);
   - the GUI casts its real parameters to `float` (M2.1 Open #3).

   I kept `double` input, because it keeps M1's and M2.1's numbers and matches what the C# demo
   will pass through the C API. **Option, if Giacomo wants CLI–GUI parity:** round each coordinate
   through `float` in `readObj`, a one-line change. It would move every number recorded so far,
   and the weld would still make the two tools differ.
3. **The parity check covers Qt 6 only.** The probe declares Qt 6's ABI and was run against
   Qt 6.11.1. Which Qt version built the GUI binaries in use is not known here. Qt 5's
   `QTextStream` may format negative zero or ties differently; this is not verified.
4. **A non-convex polygon in the input is triangulated differently from the GUI.** The GUI's
   tinyobjloader clips ears; the CLI fans. They agree for triangles, which are all of
   `source.obj`, and for convex polygons. Implementing ear clipping would match the GUI in this
   case, but I judged it outside a "minimal reader".
5. **`source.obj` is still untracked** (M1.4 Open #1). The `source.obj` part of `test_obj_io` is
   skipped where the file is missing, as `test_core` is.
6. **A failed write leaves a partial file.** If a write fails partway, the partial output file is
   not removed. M2.1 behaved the same way. Writing to a temporary file and renaming it would fix
   this, if M4's harness needs all-or-nothing output.

## For the next unit (M2.3, progress + report)

- **`src/cli/obj_io.h`'s API is stable.** `readObj` replaces its output vectors and fails with
  `path:line: reason`. `writeObj` writes the GUI's format.
- **`--report` is still refused** in `main.cpp`, by the `if (!options.reportPath.empty())` block,
  and the progress handler `recordStatus` still only records the last status. Both are as M2.1
  left them.
- **Header values.** If the report needs the app name or version, the `AUTOREMESHER_APP_*`
  definitions are PUBLIC on `autoremesher_cli_obj_io`, so `autoremesher-cli` already sees them.
- **Expected numbers on `source.obj`, unchanged:**
  - defaults: 25,920 vertices, 25,988 faces, 25,932 quads, about 37 s;
  - `--target-quads 11054`: 8,060 vertices, 8,084 faces, 7,913 quads, about 15 s.
- **The build and test command is unchanged.** CI takes about 64 s from clean, of which
  `test_core` is 37 s.
