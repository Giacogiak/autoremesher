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
> On `source.obj` at 11,054 target quads: 58 status lines over 15.3 s.
>
> **M2.3, after D6** (*reverted by Task 4: the split is now opt-in,
> `--split-to-quads`*; Giacomo chose to split every face into quads; see
> `plan/01_decisions.md`): the CLI now always calls the new
> `autoremesher_core_split_into_quads()` after the remesh, so the OBJ and
> both reports describe the **split** mesh. At 11,054 target quads the core
> makes 8,084 faces (7,913 quads, 171 non-quads, 8,060 vertices), and the
> output is **32,495 quads, 0 non-quads, 32,377 vertices**, in 15.0 s; the
> defaults give 104,000 quads; `--target-quads 2764` gives 11,526. The
> core's own counts go to stderr as one `remeshed to …` line. See
> `HANDOFF_M2_TASK3.md`.

## Task 4 — revert the mandatory quad-split (D6 reversed)

**D6 is reverted** — see `plan/01_decisions.md`. Visual review of M4's
winning trial showed the mandatory post-remesh split
(`autoremesher_core_split_into_quads()`, called unconditionally since M2.3)
produces a messy, overly dense topology: it is one Catmull–Clark-style step
applied to *every* face, including faces that were already valid quads,
quadrupling the face count and breaking each quad's clean edge flow into a
4-quad pinwheel around a centroid, with the new centroid/midpoint vertices
sitting slightly inside the true surface rather than on it.

Change `autoremesher-cli` so it does **not** call the split by default.
Decide and document how the split stays reachable, if at all (an opt-in
flag such as `--split-to-quads`, or simply leaving it unexposed — nothing
in this plan calls for forcing pure-quad output anymore). The library
function itself is unchanged; only the CLI's default path changes. Output
reverts to the natural polygon mesh (quads plus the core's own few
triangles/pentagons), reported via the existing non-quad-count line — no new
reporting code should be needed. Update the CLI's help text and this file's
Exit criteria (below) to match.

> **M2.4 measured:** the split is now **opt-in**, behind a new switch
> `--split-to-quads` (no value; `--split-to-quads=…` is a usage error, exit
> 2). Without it, `autoremesher-cli` writes the core's own mesh, as in M2.1–
> M2.3 part 1. With it, the output is byte-identical to M2.3's after-D6
> output, and the stderr `remeshed to …` line is printed only then. Kept
> as a flag rather than unexposed: it costs one `if`, and it keeps the
> library's split callable and tested end to end (`test_cli`) for M5's
> parity check and for anyone who needs all-quad output. The help text
> describes the default output and the flag. On `source.obj` at 11,054
> target quads: 8,084 faces (7,913 quads, 165 pentagons, 6 triangles; 97.9 %
> quads), 8,060 vertices, 15.1 s, the same bytes on a second run; with
> `--split-to-quads`, 32,495 quads as before. `test_tune_params`' real-CLI
> check, which reproduces M3.2's split-output baseline, now runs the CLI
> through a wrapper that adds `--split-to-quads`, until M3.3 re-records
> that baseline. See `HANDOFF_M2_TASK4.md`.

## Exit criteria

- `./autoremesher-cli -i source.obj -o /tmp/out.obj --target-quads 11054`
  runs to completion with no Qt/X11/display dependency at all (verify with
  `ldd` and by running under `QT_QPA_PLATFORM=offscreen`-free environment, or
  simply confirm no `libQt*` in `ldd` output).
- Output OBJ is a non-empty polygon mesh, mostly quads (the core's own
  ratio — ≈98% on `source.obj`), every face index valid, and vertex
  positions sit within `source.obj`'s bounding box (x ∈ [-67.63, 118.88],
  y ∈ [-38.89, 37.80], z ∈ [-39.88, 47.87], ±a small margin).

  > **Superseded by D6's reversal (2026-10-15):** this replaces the
  > "M2.3 (phase end)" all-quad reading below, which only held while D6's
  > split was mandatory. Task 4 is now the phase's last task and carries
  > this criterion.

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
> `HANDOFF_M2_TASK1.md`. (Settled: D6, then D6's reversal — the
> default output keeps them; `--split-to-quads` converts them on request.)

> **M2.3 (phase end)** — *superseded by M2.4 below; it describes the split
> output, now opt-in:* both bullets hold **as originally written**; M2.1's
> "mostly quads" reading above is superseded by D6. The first: `ldd` lists
> no Qt, CTest `autoremesher_cli_no_qt` passes, and the run had `DISPLAY`,
> `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM` unset. The second: at
> `--target-quads 11054` the output is 32,495 faces, **all quads**, every
> index valid, spanning x ∈ [-65.9775, 117.889], y ∈ [-38.4004, 37.2365],
> z ∈ [-39.6777, 47.7157] — inside `source.obj`'s box (the split only adds
> means of existing vertices, so it cannot leave the box).

> **M2.4 (phase end):** both bullets hold as worded above. The first:
> `ldd build/autoremesher-cli` lists no Qt (`grep -ic qt` gives 0), CTest
> `autoremesher_cli_no_qt` passes, and the 11,054-quad run completed (exit
> 0) with `DISPLAY`, `WAYLAND_DISPLAY` and `QT_QPA_PLATFORM` unset. The
> second: that run wrote 8,084 faces (7,913 quads = 97.9 %, 165 pentagons,
> 6 triangles) and 8,060 vertices, 0 out-of-range indices, 0 faces with a
> repeated index, spanning x ∈ [-65.9775, 117.889], y ∈ [-38.4004,
> 37.2365], z ∈ [-39.6777, 47.7157] — inside `source.obj`'s box. `test_cli`
> checks the default output (mostly quads, every index valid, every vertex
> on its sphere) and the `--split-to-quads` output on every CI run.
