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

## Task 2 — OBJ I/O without Qt

A minimal reader tolerant of the `v x y z 0 0 0` format seen in `source.obj`
(extra trailing tokens after the 3 coordinates — read and discard them, don't
fail), and a writer emitting plain `v x y z` / `f a b c d` lines, matching
`MainWindow::saveMeshToFile`'s own format (`mainwindow.cpp:1026`) so outputs
are diff-comparable with anything the GUI produces.

## Task 3 — progress + report output

Wire the C API's progress callback to print `status NN%` lines to stdout, and
`--report` to write the same fields as the GUI's headless report
(`main.cpp:194-215`: input/output paths, every parameter value, quad count,
non-quad count, vertex count, elapsed time) so M3/M4's scripts can parse one
consistent report format regardless of which tool produced it.

## Exit criteria

- `./autoremesher-cli -i source.obj -o /tmp/out.obj --target-quads 11054`
  runs to completion with no Qt/X11/display dependency at all (verify with
  `ldd` and by running under `QT_QPA_PLATFORM=offscreen`-free environment, or
  simply confirm no `libQt*` in `ldd` output).
- Output OBJ is all-quad, non-empty, and vertex positions sit within
  `source.obj`'s bounding box (x ∈ [-67.63, 118.88], y ∈ [-38.89, 37.80],
  z ∈ [-39.88, 47.87], ±a small margin).
