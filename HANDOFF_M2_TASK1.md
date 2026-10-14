# Handoff — M2 Task 1: `autoremesher-cli`

Unit `M2.1`, plan `plan/roadmap/02_M2_cli-tool.md`, section "Task 1". Branch `wrapper`.

**Status: done.** A new CMake executable, `build/autoremesher-cli`, runs the GUI headless
mode's pipeline with no Qt: it reads an OBJ, welds it, remeshes it through the C API, and
writes the result. It has the headless mode's flag surface and defaults. It links only
`libautoremesher_core.so` and the C/C++ runtime. Two plan assumptions did not hold:

- **The phase exit criterion "Output OBJ is all-quad" is contradicted by measurement.** At
  `--target-quads 11054` the output has 171 non-quads among its 8,084 faces. I added a
  correction note to the plan file. The question of what to do about it is Open #1, for
  Giacomo.
- **The CLI needs OBJ I/O to run at all, and that is Task 2's job.** I added a provisional
  reader and writer in their own file, `src/cli/obj_io.cpp`, copied from `test_core`'s loader,
  for Task 2 to replace.

## What changed and why

| File | Change |
|---|---|
| `src/cli/cli_options.{h,cpp}` (new) | Command-line parsing without Qt and without `getopt`. Holds the `Options` struct with the GUI's defaults and the range constants, and provides `parse()` and `usage()`. It is built as the static library `autoremesher_cli_options`, so the parser can be unit-tested without running a remesh. |
| `src/cli/main.cpp` (new) | The pipeline: `parse`, `readObj`, `autoremesher_core_weld`, `create`, all six setters, `remesh`, `get_result`, `writeObj`, then a one-line summary on stdout. |
| `src/cli/obj_io.{h,cpp}` (new, **provisional**) | The `v` / `f` reader from `tests/test_core.cpp`, plus two errors: a `v` line with fewer than three numbers (the original read uninitialized doubles) and a bad face index (instead of an uncaught `std::stoul` exception). Tokens after the third coordinate, as in `source.obj`'s `v x y z 0 0 0`, are ignored. The writer emits `v x y z` lines and one `f` line per face, with `face_sizes[i]` indices each. The header says Task 2 replaces both. |
| `tests/test_cli_options.cpp` (new) | Parser unit test (CTest `test_cli_options`). Covers the defaults, both syntaxes, a repeated flag (the last one wins), help, missing `--input`/`--output`, unknown flags, positional arguments, a missing value, malformed numbers (including `nan`, `inf`, ` 12`, `1e4` and `-5` for the count) and both ends of every range. |
| `tests/test_cli.cpp` (new) | End-to-end test (CTest `test_cli`). It writes a 960-triangle unit sphere as a **triangle soup** (2,880 vertices), so the CLI must weld it, and runs the real executable with every flag set to a non-default value. It checks exit 0, that every face index is valid, at least 90 % quads, and every vertex at radius 0.98–1.001. It then checks exit 0 for `-h`/`--help`, exit 2 for usage errors with no output file written, and exit 1 for a missing input and for an unwritable output. |
| `CMakeLists.txt` | Adds the `autoremesher_cli_options` library, the `autoremesher-cli` executable and the tests `autoremesher_cli_no_qt`, `test_cli_options` and `test_cli`. |
| `cmake/check_no_qt.cmake` | Only its comment changed: it now says it also accepts an executable. The `ldd` check itself already worked on one. |
| `plan/roadmap/02_M2_cli-tool.md` | An "M2.1 measured" note under Task 1, and a correction note under the exit criteria about the "all-quad" bullet. |

Design choices:

- **The ranges are enforced, which the GUI does not do.** The GUI's headless mode passes any
  value through, and `toInt()`/`toDouble()` turn garbage into 0. The CLI refuses, with exit 2:
  - any value outside the ranges the plan lists, which are the GUI widgets' ranges;
  - a value that is not entirely a finite number;
  - a `--target-quads` value that is not a whole number from 1 to 2,147,483,647 (the GUI
    stores it in an `int`).

  I did **not** apply the GUI widget's 1,000–1,000,000 limit on target quads, because the plan
  does not list one for that flag. M4's sweep cannot silently run a garbage parameter.
- **`--report` is parsed but refused** (exit 2, `--report is not implemented yet`) instead of
  being silently ignored. Task 3 removes the refusal in `main.cpp`.
- **Exit codes:** 0 on success, 1 if reading, welding, remeshing or writing fails, 2 for a
  usage error. A usage error writes nothing.
- **Every setter is called explicitly,** so the CLI's own defaults are the ones that apply. They
  equal the core's defaults, so the default output is unchanged (measured below).
- **The failure reason is captured.** The core reports why a remesh failed only through the
  progress handler (M1 Open). `main.cpp` installs a handler that keeps the last status and
  appends it to the error message. It prints nothing; `status NN%` output is Task 3's.
- **Welding always runs; there is no `--weld` flag** (D2). `--weld` is refused as an unknown
  option, and `test_cli` checks this.
- **Parsing:** `--name value`, `--name=value`, `-i value` and `-o value` are accepted. A value
  may start with `-`. If `-h` or `--help` appears anywhere, help wins. Numbers are read with
  `strtod`/`strtoull` in the C locale; the program never calls `setlocale`.

## Measured

All measured in the toolbox `instant-meshes-build` (Fedora, GCC 16.2.1, CMake 4.4.3, `nproc` = 8),
Release, on 2026-10-14. `source.obj` md5 is `e290fdb7ad6e507395e9c5c35387c453`.

- **The driver's CI command, after `rm -rf build`:** exit **0** in 64 s wall. 7/7 tests pass:
  `autoremesher_core_no_qt` 0.01 s, `test_core_api` 0.23 s, `test_weld` 0.00 s, `test_core`
  37.39 s, `autoremesher_cli_no_qt` 0.01 s, `test_cli_options` 0.00 s, `test_cli` 0.11 s.
  All 30 compiler warnings in the log are from `thirdparty/tbb`. The new files also compile
  cleanly under `-Wall -Wextra -Wpedantic` with `-fsyntax-only`.
- **Red first:** against a stub `parse()` that returned false, `test_cli_options` reported
  `73 check(s) failed`. With the real parser it passes.
- **`ldd build/autoremesher-cli`** lists `linux-vdso`, `libautoremesher_core.so` (from the build
  tree, through CMake's build RPATH), `libstdc++`, `libm`, `libgcc_s`, `libc` and `ld-linux`.
  `ldd … | grep -ic qt` gives 0, and `nm -D … | grep -ci qt` gives 0. The binary is 53,760 bytes.
- **The exit-criterion command:** `./autoremesher-cli -i source.obj -o /tmp/out.obj
  --target-quads 11054`, run with `DISPLAY`, `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM` unset,
  under `/usr/bin/time -v`:
  - It exited 0 and printed
    `/tmp/out.obj: 8060 vertices, 8084 faces (7913 quads, 171 non-quads), 15.2 s`.
    Wall time was 15.18 s, at 106 % CPU and 290 MB maximum RSS.
  - The faces are 7,913 quads, 165 pentagons and 6 triangles.
  - Output bounding box: x ∈ [-65.98, 117.89], y ∈ [-38.40, 37.24], z ∈ [-39.68, 47.72]. That
    is inside `source.obj`'s box, x ∈ [-67.629, 118.88], y ∈ [-38.892, 37.797],
    z ∈ [-39.881, 47.865], on every side.
  - A second run produced a byte-identical file (`cmp`), so the output is deterministic.
- **The defaults** (`-i source.obj -o …`, run from another directory) give
  `25920 vertices, 25988 faces (25932 quads, 56 non-quads), 37.4 s`. These are exactly
  M1.1–M1.4's numbers, so the CLI's settings reproduce the core's defaults.
- **The synthetic sphere soup:** at target 300 with the defaults it gives 252 vertices and 250
  faces, all quads, the same as M1.3's welded sphere. With `test_cli`'s non-default settings it
  gives 106 vertices and 104 faces, all quads.
- **Streams:** the core's own diagnostic chatter (about 68 lines per run: step names, timings,
  "Islands: 1", and so on) goes to **stderr**. The CLI's stdout carries only its summary line.

## What I got wrong on the way

- **The first build failed to link `test_cli`** with `cannot open output file test_cli: Is a
  directory`: I had named the scratch directory `build/test_cli`, the same path as the
  executable. It is now `build/test_cli_scratch`. That failed build also made
  `autoremesher_cli_no_qt` fail once, as a side effect.
- **I assumed the plan's "all-quad" bullet would hold at 11,054 quads.** M1.2 had already shown
  non-quads at 50,000, and they also appear at 11,054, at a higher rate (2.1 % against 0.2 %).
- I wrote one code comment that claimed, unverified, that unwelded input "would be seen as
  holes". I replaced it with what is actually known: `source.obj` repeats vertices, and D2
  makes the weld unconditional.

## M2 exit criteria (phase-wide; held at M2.3): status after this unit

- [x] *Runs to completion with no Qt/X11/display dependency.* **Met now**, with evidence above:
  `ldd` shows no Qt, a CTest guards it, and the run worked with no display variables set.
- [ ] *Output OBJ is all-quad, non-empty, positions within `source.obj`'s bounding box.*
  - **Non-empty: yes.**
  - **Bounding box: yes.**
  - **All-quad: no**, and the core cannot produce all-quad output as it is (Open #1).
  - The output is still written by the **provisional** writer, so this bullet should be
    re-measured once Task 2's writer lands.

## Open

1. **For Giacomo: "all-quad" against what the core produces.** At 11,054 target quads, 2.1 % of
   the faces are pentagons and triangles. The options:
   - **(a) Accept polygon output.** Amend the criterion to "mostly quads; non-quads are counted
     and reported". This matches the GUI's export and its headless report, which already has a
     `Non-quads:` line, so no code is needed. The correction note I added to the plan reads it
     this way provisionally.
   - **(b) Post-process to all-quad.** Split every face into quads Catmull–Clark style (an
     n-gon becomes n quads). It is simple and always gives quads, but it about **quadruples**
     the face count, so `--target-quads` stops meaning what it says. It is also a topology
     change that the GUI does not make.
   - **(c) Fix it in `QuadExtractor`.** This is an algorithm change in the core with an unknown
     cost. It is not in this plan.

   Option (a) costs nothing. Options (b) and (c) change the output that M3 and M4 score.
2. **The provisional OBJ I/O is Task 2's to replace** (`src/cli/obj_io.{h,cpp}`). It has these
   known gaps:
   - no tests of its own; only `test_cli` and the hand runs on `source.obj` exercise it.
     The two error paths (a bad `v` line, a bad face index) were checked by hand, once each;
   - negative (relative) OBJ indices are not supported; they wrap to out-of-range values, and
     the weld then refuses the mesh;
   - the writer's header is `# autoremesher-cli`, not the GUI's two lines;
   - vertices are written with `std::ostream`'s default precision of 6 significant digits.
     This is the same as `QTextStream`'s default in `saveMeshToFile`, but it is not
     verified to match the GUI byte for byte.
3. **Parameter precision differs from the GUI's.** `MainWindow::setHeadlessParams` casts every
   real parameter to `float`; the CLI passes `double`s.
   - The two agree for values that are exact in `float`, such as 1.0, 90, 0, 0.5 and 0.25.
   - For a value such as 0.3 the core sees a slightly different number, so its output may not
     be bit-identical to the GUI's.

   I kept `double` because the C API takes doubles and so will the C# demo. If byte parity
   with the GUI matters, round through `float` in `main.cpp`.
4. **The output path is only checked after the remesh.** An unwritable `-o` is reported after
   15–37 s of work, with exit 1. Opening the file early would fail fast, but it would truncate
   an existing file even when the remesh then fails, so I left it.
5. **`source.obj` is still untracked** (M1.4 Open #1, unchanged). No new test depends on it:
   `test_cli` uses a synthetic sphere. The exit-criterion run above was done by hand.

## For the next unit (M2.2, OBJ I/O without Qt)

- **Replace `src/cli/obj_io.{h,cpp}`.** Keep its two signatures or change them, but update the
  two calls in `src/cli/main.cpp`.
- **`readObj` returns raw arrays** (`std::vector<double>` of x, y, z and `std::vector<uint32_t>`
  of triangle corners, fan-triangulated). `main.cpp` welds them unconditionally afterwards, so
  the reader must **not** weld or deduplicate anything itself.
- **`writeObj` takes the `autoremesher_core_result` directly.** Faces are polygons: write
  `face_sizes[i]` indices per face.
- **`tests/test_cli.cpp` must keep passing.** It writes its sphere with `%.17g` and plain
  `v x y z` lines, and it parses the output by reading `v` and `f` tokens, so it does not
  depend on the header or on number formatting.
- **Expected numbers on `source.obj`:**
  - defaults: 25,920 vertices, 25,988 faces, 25,932 quads, about 37 s;
  - `--target-quads 11054`: 8,060 vertices, 8,084 faces, 7,913 quads, about 15 s.

  A reader change that alters these numbers has changed the input mesh.
- **For M2.3:** the `--report` refusal is the `if (!options.reportPath.empty())` block in
  `main.cpp`. The progress handler `recordStatus` is where `status NN%` printing would go; it
  must still keep the last status for the error message. Because the core's chatter goes to
  stderr, stdout is free for the `status` lines and the report.
- The build and test command is unchanged; CI now takes about 64 s from clean.
