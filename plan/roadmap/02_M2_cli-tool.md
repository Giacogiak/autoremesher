# M2 — Standalone CLI executable

## Task 1 — `autoremesher-cli`

New CMake executable target linking `autoremesher_core`, with the same flag
surface as the existing GUI headless mode (`src/main.cpp` lines 94–137) for
continuity: `-i/--input`, `-o/--output`, `--report`, `--target-quads` (default
50000, matching the GUI default), `--edge-scaling` (1.0–4.0), `--sharp-edge`
(30.0–180.0, default 90.0 — `AutoRemesher::m_defaultSharpEdgeDegrees`),
`--smooth-normal` (0.0–180.0, default 0.0), `--adaptivity` (0.0–1.0, default
1.0), `--anisotropy` (0.0–1.0, default 1.0). No `--weld` flag: welding always
runs (per D2, this is default behavior, not opt-in — mirrors the precedent in
the sibling Instant Meshes project, where welding the source became the
demo's unconditional default rather than a flag).

> **M2.1 measured:** `build/autoremesher-cli` (sources in `src/cli/`) links
> only `libautoremesher_core.so` and the C/C++ runtime — no Qt in `ldd`
> (CTest `autoremesher_cli_no_qt`). Flags take `--name value` or
> `--name=value`; the ranges above are enforced (exit 2 outside them — the
> GUI's headless mode does not check them at all), and `--target-quads` must
> be a whole number from 1 to 2³¹−1. `--report` is parsed but refused with
> exit 2 until Task 3 implements it. The OBJ reader/writer in
> `src/cli/obj_io.cpp` is a provisional copy of `test_core`'s loader, for
> Task 2 to replace. On `source.obj`: the defaults give 25,932 quads of
> 25,988 faces in 37.4 s (same as M1), and `--target-quads 11054` gives
> 7,913 quads of 8,084 faces in 15.2 s. See `HANDOFF_M2_TASK1.md`.

## Task 2 — OBJ I/O without Qt

A minimal reader tolerant of the `v x y z 0 0 0` format seen in `source.obj`
(extra trailing tokens after the 3 coordinates — read and discard them, don't
fail), and a writer emitting plain `v x y z` / `f a b c d` lines, matching
`MainWindow::saveMeshToFile`'s own format (`mainwindow.cpp:1026`) so outputs
are diff-comparable with anything the GUI produces.

> **M2.2 measured:** `src/cli/obj_io.{h,cpp}` (static library
> `autoremesher_cli_obj_io`, CTest `test_obj_io`). The reader also takes
> `v/vt/vn` corners, negative indices, CRLF and polygons (fanned, as
> tinyobjloader does for convex ones), and refuses with file:line a
> non-finite or missing coordinate, a bad or missing vertex index, a face of
> under 3 corners, or a file with no faces. It reads `source.obj` (97,400
> vertices, 175,566 triangles) in about 0.04 s. The writer's output is
> byte-identical to what `saveMeshToFile` writes: its two header lines
> (`# AutoRemesher 1.2.0`, `# https://autoremesher.dust3d.org/`, read from
> `autoremesher.pro` by CMake) and QTextStream's number format. That format
> is **not** plain `%g`: Qt writes `-0` as `0` and rounds exact ties away
> from zero. This was checked against the real Qt 6.11 `QTextStream` on
> 200,004 values, with 0 mismatches (`tests/manual/qtextstream_probe.cpp`).
> "Diff-comparable" holds for the **format** only. The GUI's geometry differs:
> it does not weld (D2), and tinyobjloader reads coordinates as `float`. On
> `source.obj` at 11,054 target quads, float-rounded input alone moves the
> result from 8,084 to 8,503 faces. See `HANDOFF_M2_TASK2.md`.

## Task 3 — progress + report output

Wire the C API's progress callback to print `status NN%` lines to stdout, and
`--report` to write the same fields as the GUI's headless report
(`main.cpp:194-215`: input/output paths, every parameter value, quad count,
non-quad count, vertex count, elapsed time) so M3/M4's scripts can parse one
consistent report format regardless of which tool produced it.

> **M2.3 measured:** `src/cli/report.{h,cpp}` (static library
> `autoremesher_cli_report`, CTest `test_report`; `test_cli` checks both
> outputs end to end). Stdout carries `status NN%` lines (bare, no step name;
> whole percents computed as the GUI does, printed only when the percent
> rises, at most 99 from the core, then `status 100%` once every file is
> written, flushed per line), then the GUI's **stdout** block
> (`=== AutoRemesher Report ===` … `Time: … seconds`), which replaces M2.1's
> one-line summary. `--report` writes the GUI's report **file** line for
> line: the same 17 lines, numbers formatted as QTextStream formats them
> (the file) or as `std::cout` does (the block), as in the GUI. Time is
> measured as the GUI measures it: whole milliseconds from before the input
> is read to the end of the remesh, excluding the write. A run that fails
> writes no report; an unwritable report exits 1 after the OBJ is written.
> On `source.obj` at 11,054 target quads: 58 status lines over 15.3 s,
> 7,913 quads, 171 non-quads, 8,060 vertices — M2.2's numbers, and a
> byte-identical OBJ across runs. See `HANDOFF_M2_TASK3.md`.

## Exit criteria

- `./autoremesher-cli -i source.obj -o /tmp/out.obj --target-quads 11054`
  runs to completion with no Qt/X11/display dependency at all (verify with
  `ldd` and by running under `QT_QPA_PLATFORM=offscreen`-free environment, or
  simply confirm no `libQt*` in `ldd` output).
- Output OBJ is all-quad, non-empty, and vertex positions sit within
  `source.obj`'s bounding box (x ∈ [-67.63, 118.88], y ∈ [-38.89, 37.80],
  z ∈ [-39.88, 47.87], ±a small margin).

> **M2.1 measured (correction to the second bullet):** the output is **not
> all-quad**, and cannot be with the core as it is. `QuadExtractor` emits 3-
> to 7-gons (see M1.2's correction in `01_M1_native-library.md`), and the
> GUI's own OBJ export and headless report keep them. At
> `--target-quads 11054` on welded `source.obj` the CLI writes 8,084 faces:
> 7,913 quads (97.9 %), 165 pentagons, 6 triangles. Read the bullet as: a
> non-empty polygon mesh, mostly quads (≈98 % here), every face index valid.
> The bounding-box part holds: the output spans x ∈ [-65.98, 117.89],
> y ∈ [-38.40, 37.24], z ∈ [-39.68, 47.72]. Whether the CLI should instead
> convert the non-quads to quads is an open question for Giacomo; see
> `HANDOFF_M2_TASK1.md`.

> **M2.3 (phase end):** the first bullet holds (`ldd` lists no Qt; CTest
> `autoremesher_cli_no_qt`; the run above had `DISPLAY`, `WAYLAND_DISPLAY`
> and `QT_QPA_PLATFORM` unset). The second holds **only under M2.1's reading
> above**: non-empty, every index valid, inside the bounding box, but 2.1 %
> non-quads. Whether that reading is accepted is still Giacomo's call
> (M2.1 Open #1), so M2 is not closed until that is answered.
