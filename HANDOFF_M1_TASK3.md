# Handoff — M1 Task 3: vertex welding utility

Unit `M1.3`, plan `plan/roadmap/01_M1_native-library.md`, section "Task 3". Branch `wrapper`.

**Status: done.** `libautoremesher_core.so` now exports `autoremesher_core_weld()` and
`autoremesher_core_free_weld_result()`. Run on `source.obj`, the weld takes 97,400 vertices to
exactly **87,691**. A new CTest, `test_weld`, covers the function's contract on small
hand-built meshes. `test_core_api` gained a section that welds a triangle-soup sphere and then
remeshes it. The plan was right this time, so it needed no correction. It only got a
"measured" note.

## What changed and why

| File | Change |
|---|---|
| `src/weld.h`, `src/weld.cpp` (new) | `AutoRemesher::weldVertices()`, a C++ function with no Qt. It takes flat arrays and builds a `std::map<PositionKey, uint32_t>`, as the plan asks. It fills a `std::vector<double>` of welded vertices and a `std::vector<uint32_t>` of remapped triangle indices. All input validation is done here. |
| `src/autoremesher_core_api.h` | Adds the `autoremesher_core_weld_result` struct (`vertices`, `vertex_count`, `triangle_indices`, `triangle_count`), `autoremesher_core_weld()` and `autoremesher_core_free_weld_result()`. The usage comment at the top now says to weld soups before `create`. |
| `src/autoremesher_core_api.cpp` | The two C entry points. NULL checks, a `catch (...)` around everything, and `malloc`'d output, as in M1.2. The `allocate` lambda from `get_result` became a file-local function so both callers share it. |
| `tests/test_weld.c` (new) | The contract test, written in C. Described below. |
| `tests/test_core_api.c` | **Added** a final section. It welds the sphere written as a triangle soup (2,880 vertices), then remeshes the result. The existing checks are unchanged. |
| `CMakeLists.txt` | Adds `src/weld.cpp` to `autoremesher_core`, and adds the `test_weld` executable and test. |
| `plan/roadmap/01_M1_native-library.md` | An "M1.3 measured" note under Task 3. |

### The API

```c
typedef struct autoremesher_core_weld_result {
    double* vertices;            /* vertex_count * 3 */
    size_t vertex_count;
    uint32_t* triangle_indices;  /* triangle_count * 3 */
    size_t triangle_count;
} autoremesher_core_weld_result;

int  autoremesher_core_weld(const double* vertices, size_t vertex_count,
                            const uint32_t* triangle_indices, size_t triangle_count,
                            autoremesher_core_weld_result* result);   /* 1 ok, 0 fail */
void autoremesher_core_free_weld_result(autoremesher_core_weld_result* result);
```

The weld does not need a handle and is independent of `remesh()`, as the plan asks. The input
arrays have the same layout as `autoremesher_core_create()`'s, so the welded arrays can be
passed to it directly.

Design choices worth knowing:

- **Key: `PositionKey`, as the plan names it.** `PositionKey` multiplies each coordinate by
  1e5 and truncates it to `long`. So it is a fixed grid, not a tolerance parameter, which fits
  D2's "no epsilon parameter". Equal positions always merge. Positions that differ by less
  than 1e-5 may also merge, if they fall in the same grid cell. The header says this. On
  `source.obj` the grid merges nothing beyond exact equality (measured below). See Open #1.
- **First occurrence wins.** Welded vertices keep the exact coordinates of the first input
  vertex at each position, and they come out in the order those vertices appear in the
  input. So a mesh that has no duplicates comes back bit-for-bit unchanged, and the order is
  deterministic.
- **Positions only; no cleanup.** Every input vertex is welded, including vertices no
  triangle uses. Every triangle is kept, so `triangle_count` is unchanged. A triangle that
  already had two corners at one position keeps a repeated index; it is not dropped. Both
  behaviors are documented and tested. Dropping degenerate triangles would be a topology
  change that D2 does not ask for. `source.obj` has none.
- **`-0.0` and `0.0` merge.** They are the same position, and both truncate to key 0.
- **Validation.** The weld returns 0 with `result` zeroed for:
  - a NULL array with a non-zero count;
  - any triangle index `>= vertex_count`;
  - a `count * 3` overflow;
  - a coordinate that is NaN, infinite, or larger in magnitude than
    `LONG_MAX / 1e5 - 1` (about 9.2e13). Casting such a value to `long` inside `PositionKey`
    would be undefined behavior. This check also applies to vertices no triangle uses;
  - more than 2³² distinct positions, since the output indices are `uint32_t`;
  - running out of memory.

  It returns 0 without writing anything if `result` is NULL. Empty input succeeds and
  gives an empty mesh.
- **Separate result type and free function.** I did not reuse `autoremesher_core_result`,
  because that struct is a polygon layout with `face_sizes` and `quad_count`. A triangle mesh
  in it would mean a `face_sizes` array of all 3s that nobody needs. The free function works
  like `free_result`: it zeroes the struct, so a double free is a no-op.
- **Exports:** `nm -D --defined-only` lists 14 `autoremesher_core_*` functions as `T` (12
  from M1.2 plus 2 new). The C++ `AutoRemesher::weldVertices` is also exported, because
  visibility is still default (M1.2 Open #4).

## Measured (toolbox `instant-meshes-build`: Fedora, GCC 16.2.1, CMake 4.4.3, 8 threads, Release)

- **The driver's CI command, from a deleted `build/`:** exit 0 in 25 s wall. CTest passes 3/3:
  `autoremesher_core_no_qt` (0.01 s), `test_core_api` (0.23 s), and `test_weld` (0.00 s). The
  new files cause no compiler warnings. The only warnings are still TBB's, plus the CMake
  deprecation notice for TBB's `cmake_minimum_required`.
- **`test_weld` checks the following:**
  - **Square soup:** 6 vertices weld to 4, and the triangles come out exactly
    `{0,1,2},{0,2,3}`. The input array is not modified.
  - **Already welded:** the mesh comes back unchanged.
  - **Out of order:** vertex order follows the input order, not the order the triangles
    use the vertices.
  - **Unreferenced:** a duplicate vertex that no triangle uses is merged, and the unused
    unique vertex is kept.
  - **Degenerate:** the triangle `{0,0,1}` is kept.
  - **Signed zero:** `-0.0` and `0.0` merge.
  - **Near misses:** coordinates 1e-4 apart stay separate.
  - **Empty input:** succeeds with counts of 0.
  - **Refusals:** NULL `result`; a NULL array with a non-zero count; an out-of-range index;
    NaN; `-inf`; `1e15`, including on an unreferenced vertex. In each case the result is
    zeroed.
  - **Freeing:** a NULL argument and a double free are both harmless.
- **`test_core_api` soup section:** the 960-triangle sphere written as a 2,880-vertex soup
  welds to **482** vertices and 960 triangles, which is the original sphere's count. Every
  welded triangle has the same corner coordinates as its soup triangle. Remeshing it at
  target 300 gives 250 faces, all of them quads. That is the same count as the original
  indexed sphere, even though the welded vertex order is different.
- **`source.obj` through the C API** (throwaway `/tmp/m13/smoke.cpp`, not committed): the
  loader is the same minimal one M1.2 used, with `v` lines and fan-triangulated `f` lines.
  - `autoremesher_core_weld`: **97,400 → 87,691 vertices**, 175,566 triangles, in
    **0.035 s**. It produces 0 degenerate triangles and 0 unreferenced vertices.
  - **Bit-identical** to M1.2's exact-key weld (`std::map<std::tuple<double,double,double>>`,
    first occurrence wins): the vertices and the triangle indices are equal element for
    element.
  - Then `create`, `set_target_quad_count(50000)`, `set_scaling(1.0)`, `remesh`. The result
    is **25,920 vertices, 25,988 faces, 25,932 quads**, in 36.9 s for the remesh and 37.2 s
    wall in total, at 105 % CPU and 680 MB max RSS. This is the same output as M1.1 and M1.2.
- **A standalone count** (`/tmp/m13/count.cpp`, which compiles `positionkey.cpp` directly):
  `std::set` of exact tuples gives 87,691, and `std::map<PositionKey>` gives 87,691.
  `source.obj`'s largest coordinate magnitude is 118.88, far below the 9.2e13 limit.
- **Memory checking:** both tests pass under `MALLOC_CHECK_=3 MALLOC_PERTURB_=165`. The
  toolbox still has no ASan or valgrind, so this remains a weak check.

## What I got wrong on the way

- My first draft of `weld.cpp` had two problems:
  - It derived the coordinate limit with a confusing ternary.
  - It did the 2³² position-count check after `static_cast<uint32_t>` had already
    produced the new index. If the check had ever triggered, that cast would already have
    stored a wrapped index.

  I rewrote it before the first build. It now does a `find` first, checks the next index,
  and only then calls `emplace`, and the limit is `LONG_MAX / 100000 - 1`.
- That was the only mistake. The red-first build failed as expected, with the
  `autoremesher_core_weld_result` type unknown, and once the implementation was in, every
  check passed on the first run.

## Exit criteria of M1 (phase-wide; held at M1.4): status after this unit

- [x] *`cmake --build` produces `libautoremesher_core.so` with zero Qt symbols.* **Still met.**
  `weld.cpp` uses only the standard library and `PositionKey`, and `autoremesher_core_no_qt`
  passes.
- [ ] *Weld collapses 97,400 → exactly 87,691, asserted in the smoke test.* **Open, for
  Task 4.** The weld exists and was measured at exactly 87,691 (above). No committed test
  asserts this yet: the plan puts that assertion in Task 4's smoke test, and I did not read
  ahead into Task 4.
- [ ] *`test_core` passes, producing a non-empty quad mesh from `source.obj`.* **Open, for
  Task 4.** Weld → remesh through the C API gives 25,932 quads in about 37 s.

## Open

1. **`PositionKey` merges by grid cell, not by bitwise equality.** It truncates each
   coordinate times 1e5 to an integer. As a result:
   - Two points less than 1e-5 apart can merge when they share a cell.
   - Two points that are even closer can stay apart when a cell boundary falls between them.
     For example, 0.000009 and 0.000011 do not merge.

   This is what the plan specified ("exact (quantized)"), and it changes nothing on
   `source.obj`. If Giacomo reads D2 as strictly bitwise-exact, the change is one line: swap
   the key for an exact `std::tuple<double,double,double>` (with `-0.0` normalized to
   `0.0`), which on `source.obj` gives the same 87,691 vertices, bit-identical. I did not
   block on this, because the plan names `PositionKey` explicitly.
2. **No vertex remap is returned.** A caller that has per-vertex attributes (normals, UVs)
   cannot map them through the weld. Neither M2 nor M5 needs this as planned. Adding a
   `uint32_t* vertex_remap` (one entry per input vertex) to the struct later would be cheap,
   but it would change the struct's C# layout, so decide before M5 marshals it.
3. Carried over from M1.2: no sanitizer run, hidden visibility still off, no stored failure
   reason for `remesh()`, and the polygon-layout deviation.

## For the next unit

- **M1.4 (smoke test):** load `source.obj`, then call `autoremesher_core_weld(...)` and
  assert `vertex_count == 87691`. You can also assert `triangle_count == 175566` if the
  loader fan-triangulates the way `/tmp/m13/smoke.cpp` does. Then pass the welded arrays
  straight to `autoremesher_core_create()`, call `set_target_quad_count(50000)` (the
  core's default target is 0 and fails, see M1.1), call `remesh`, and assert
  `quad_count > 0`. Expect about 37 s, so give the test a generous CTest `TIMEOUT`
  (≥ 300 s). Point the test at the file through an absolute path, for example
  `${CMAKE_CURRENT_SOURCE_DIR}/source.obj` as an argument, because ctest runs from `build/`.
- `test_core_api.c`'s new soup section already shows the weld → create → remesh sequence in
  C. Copy it.
- **M2 CLI / M5 C#:** the weld struct in C# is
  `{IntPtr vertices; nuint vertex_count; IntPtr triangle_indices; nuint triangle_count;}`,
  with sequential layout. It returns a C `int` (1/0), and the struct must be freed with
  `autoremesher_core_free_weld_result`.
- Build and test command as before:
  `toolbox run -c instant-meshes-build -- bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"`.
