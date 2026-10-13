# Handoff — M1 Task 2: extern "C" API

Unit `M1.2`, plan `plan/roadmap/01_M1_native-library.md`, section "Task 2". Branch `wrapper`.

**Status: done.** `libautoremesher_core.so` now exports a plain-C API (12 functions). A new CTest,
`test_core_api`, drives every function from a C program on a synthetic sphere. One part of the
plan was wrong, and the API differs from it there: the output faces are not all quads (see
"Where the plan was wrong").

## What changed and why

| File | Change |
|---|---|
| `src/autoremesher_core_api.h` (new) | The C header. It defines an opaque `autoremesher_core` handle, the `autoremesher_core_progress_handler` typedef (the same signature as `AutoRemesherProgressHandler`), the `autoremesher_core_result` struct, and an `AUTOREMESHER_CORE_API` export macro. Threading, ownership and failure rules are documented on each function. |
| `src/autoremesher_core_api.cpp` (new) | The implementation. No exception gets past the C boundary: every entry point that allocates is wrapped in `catch (...)`. A NULL handle is accepted by every function. |
| `include/autoremesher_core_api.h` (new) | A one-line forwarding header, on the same pattern as `include/AutoRemesher/AutoRemesher`. Consumers write `#include <autoremesher_core_api.h>`, and `include/` is already a PUBLIC include dir. I did not make `src/` public: it would put the GUI's `util.h`, `theme.h`, `version.h` and similar headers on every consumer's include path. |
| `tests/test_core_api.c` (new) | The C test, described below. It is in C so that it also proves the header is valid C. |
| `CMakeLists.txt` | `project(... C CXX)` (C is for the test only). `src/autoremesher_core_api.cpp` is added to the library. `AUTOREMESHER_CORE_BUILDING` is defined PRIVATE; it only matters on Windows, for dllexport. Adds the `test_core_api` executable and test (TIMEOUT 300). |
| `plan/roadmap/01_M1_native-library.md` | An "M1.2 measured (correction)" note under Task 2 about polygon faces. |

### The API

```c
autoremesher_core* autoremesher_core_create(const double* vertices, size_t vertex_count,
                                            const uint32_t* triangle_indices, size_t triangle_count);
void autoremesher_core_destroy(autoremesher_core*);
void autoremesher_core_set_target_quad_count(autoremesher_core*, size_t);   // ×2 → triangles
void autoremesher_core_set_scaling / _sharp_edge_degrees / _smooth_normal_degrees
     / _adaptivity / _anisotropy (autoremesher_core*, double);
void autoremesher_core_set_progress_handler(autoremesher_core*, handler, void* tag);
int  autoremesher_core_remesh(autoremesher_core*);                          // 1 ok, 0 fail
int  autoremesher_core_get_result(const autoremesher_core*, autoremesher_core_result*);
void autoremesher_core_free_result(autoremesher_core_result*);
```

Design choices worth knowing:

- **Each `remesh()` builds a fresh `AutoRemesher` from the stored input.** The class appends to
  `m_remeshedVertices` and `m_remeshedQuads` and never clears them (`autoremesher.cpp` ~930–940).
  So calling `remesh()` twice on one object would concatenate two meshes. With a fresh object
  per call, one handle can be re-run with new settings, which M4's sweep will want. The cost is
  one copy of the input per run, which is negligible next to the remesh. The test checks that a
  second run does not grow the output.
- **Unset settings are not passed to the core.** This is `Setting<T>{isSet, value}`, and it
  copies `QuadMeshGenerator::generate()`, which only sets scaling and target when they are
  `> 0`. The core's own defaults stay the only copy of the default values. Model type is never
  set, so it stays Organic (D4). `setTag` is folded into
  `set_progress_handler(handle, handler, tag)`.
- **Target quads ×2:** I checked this, not assumed it. `mainwindow.cpp:1078` and `:1122` both
  pass `m_targetQuadCount * 2`, and `quadmeshgenerator.cpp:84` hands it to
  `setTargetTriangleCount` unchanged. The C API multiplies by exactly 2, saturating on overflow.
  Setting 0 quads gives target 0, so the core refuses to remesh.
- **Validation at `create`:** it returns NULL for a NULL array with a non-zero count, for any
  triangle index `>= vertex_count`, or on a `count*3` overflow. The core does not check indices
  itself, and a bad index would read out of bounds. Empty input (0, 0) is accepted, and
  `remesh()` then fails with the core's own "input mesh has no vertices".
- **Failure reason:** the core reports it only through the progress handler, as one call with
  progress 1.0 and the reason as status. It also writes it to stderr. The C API does not store
  it. A caller that wants the reason has to install a handler.
- **Progress threading:** the handler is called from the `remesh()` caller's thread and from
  TBB worker threads. Calls are serialized: in the parallel phase every call goes through
  `updateProgress` under `m_progressMutex`, and the main-thread calls come before and after
  that phase. The header says so. **For the C# demo (M5):** the delegate must be kept alive
  (GC-rooted) for the whole `remesh()` call, and it runs on non-.NET threads.
- **Result ownership:** the result is `malloc`'d by the library and freed only by
  `autoremesher_core_free_result()`, which also zeroes the struct, so a double free is a no-op.
  It stays valid after `destroy`. A failed `remesh()` discards the previous result.
  `remesh()` fails if the output has more than 2³²−1 vertices, because indices are `uint32_t`.
- **Symbol visibility is still default** (everything exported). `AUTOREMESHER_CORE_API` marks
  the C functions explicitly, so `-fvisibility=hidden` can be switched on later without editing
  the header. I left it off because Task 4 or M2 may still use the C++ class directly.

### Where the plan was wrong

**Faces are polygons, not quads.** The plan asked for `uint32_t* quad_indices, size_t
quad_count`, which implies 4 indices per face. But `QuadExtractor` pushes 3-, 4-, 5-, 6- and
7-vertex faces (`quadextractor.cpp:603–742`). The GUI keeps them: the OBJ export writes each
face at its real size, and the headless report counts "non-quads" (`mainwindow.cpp:1172–1178`),
which M2 Task 3's report must reproduce. On `source.obj`, 56 of 25,988 faces are not quads.

A 4-per-face array would have to drop or corrupt those faces. So the result uses the polygon
layout that is standard for this: flat `face_indices`, a per-face `face_sizes`, and
`quad_count` for convenience. It is still flat, uses no `std::vector`, and has three pointers
and four `size_t` fields, which is easy to marshal from C#. The plan file now has a correction
note.

I made this call without a decision from Giacomo, since it keeps the plan's intent (flat, marshalable) and loses nothing. If 4-per-face is required anyway, the options are
(a) drop non-quads, which loses 0.2 % of faces on `source.obj` and changes the
`quad_count`/non-quad report, or (b) fan-split them, which changes topology. Both are worse.

**Previous handoff wording:** M1.1 reported "25,988 quads". That number is **faces**: 25,932
quads + 52 pentagons + 4 triangles.

## Measured (toolbox `instant-meshes-build`: Fedora, GCC 16.2.1, CMake 4.4.3, 8 threads, Release)

- **Driver CI command, from a deleted `build/`:** exit 0 in about 25 s wall. CTest: 2/2 passed
  (`autoremesher_core_no_qt` and `test_core_api`). My two new files produce no compiler
  warnings. The only warnings are still TBB's `atomic.h:548` ones.
- **`test_core_api`: 0.20 s.**
  - The sphere is 482 vertices and 960 triangles, a UV sphere with 32 segments × 16 rings.
  - Target 300 quads gives 252 vertices and 250 faces, all of them quads.
  - Target 1,200 gives 894 quads.
  - 20 back-to-back runs gave identical numbers, so the run is deterministic and the
    `quad_count` ratio check (> 2× going from 300 to 1,200) is not flaky.
  - The test checks:
    - `create` rejects NULL arrays and out-of-range indices.
    - Every function accepts a NULL handle.
    - Empty input cannot be remeshed.
    - With no target, `remesh()` fails, and the handler gets one call at 1.0 whose status
      contains "target triangle count".
    - On success, the handler is called more than once, progress stays in [0,1], status is
      never NULL, and the last call is 1.0 with "Done".
    - The result is well formed: sizes ≥ 3, sizes sum to `index_count`, `quad_count` matches,
      indices are in range, and every vertex radius is in (0.9, 1.1).
    - `get_result` copies are independent.
    - Re-running does not accumulate output, and a run with no handler gets no callbacks.
    - A failed remesh clears the result.
    - The result outlives `destroy`, and a double `free_result` is harmless.
- **Memory checking:** the toolbox has no libasan/libubsan (the link fails) and no valgrind. The
  test does pass under glibc's `MALLOC_CHECK_=3 MALLOC_PERTURB_=165`. That is a weak check, not
  a sanitizer run.
- **Exports:** `nm -D --defined-only` lists all 12 `autoremesher_core_*` functions as `T`.
- **Real mesh through the C API (throwaway, `/tmp/m12_smoke/smoke.cpp`, not committed):**
  - It loads `source.obj` (97,400 `v`), exact-welds with `std::map` to **87,691**, and gets
    175,566 triangles.
  - Settings: `set_target_quad_count(50000)`, `set_scaling(1.0)`.
  - Result: `remesh` = 1, **37.0 s wall**, 105 % CPU, 684 MB max RSS.
  - Output: **25,920 vertices, 25,988 faces = 25,932 quads + 52 pentagons + 4 triangles**,
    104,000 indices.
  - This is the same as M1.1's direct C++ run with scaling unset, so `scaling = 1.0` and unset
    give the same mesh here, and the C wrapper adds nothing measurable.

## What I got wrong on the way

- I started writing the result as the plan's 4-per-face `quad_indices`. I checked
  `quadextractor.cpp` first and found 3–7-gon pushes and the GUI's non-quad count, so I switched
  to the polygon layout before any build. The `source.obj` run then confirmed 56 non-quads.
- I tried an ASan/UBSan build of the test. It cannot link in this toolbox (the sanitizer
  runtimes are not installed), so I fell back to glibc malloc checking.

## Exit criteria of M1 (phase-wide; held at M1.4) — status after this unit

- [x] *`cmake --build` produces `libautoremesher_core.so` with zero Qt symbols.* **Still met**
  with the API source added. The `autoremesher_core_no_qt` CTest passes.
- [ ] *Weld collapses 97,400 → exactly 87,691, asserted in the smoke test.* **Open** (Tasks 3
  and 4). The throwaway run above again reaches 87,691 with an exact-position map.
- [ ] *`test_core` passes, producing a non-empty quad mesh from `source.obj`.* **Open** (Task 4).
  Through the C API it gives 25,932 quads in 37 s at 50,000 target quads.

## Open

1. **The polygon-layout deviation** (above). It is documented in the plan. Revisit only if
   4-per-face is a hard requirement somewhere downstream.
2. **No stored failure reason.** If M2 or M5 want an `autoremesher_core_last_error()`, the cheap
   way is a trampoline handler in the C layer. But installing a handler makes the core take
   `m_progressMutex` on every fine-grained update even when the caller wants no progress, so
   measure the cost before making it unconditional.
3. **No sanitizer run.** Do one if a toolbox with `libasan` becomes available.
4. **Hidden visibility** is still not enabled (see Design choices).
5. Carried over from M1.1: the `-flto -funroll-loops -march=x86-64-v2` flags are not copied,
   and there is no real-Qt negative test of `check_no_qt.cmake`.

## For the next units

- **M1.3 (weld):** expose `autoremesher_core_weld(...)` in the same header with the same
  conventions: `AUTOREMESHER_CORE_API`, flat `double*` xyz and `uint32_t*` indices,
  library-owned output freed by a matching free function, NULL-tolerant, no exceptions across
  the boundary. Add `src/weld.cpp` to the `add_library` list. `src/` is not on the include
  path, so include `"weld.h"` from `autoremesher_core_api.cpp`, which sits in the same
  directory.
- **M1.4 (smoke test):** `test_core_api` already covers the API's contract on a tiny mesh, so
  the smoke test can stick to `source.obj`: weld count 87,691, then
  `set_target_quad_count(50000)` and assert `quad_count > 0`. Expect about 37 s. To link a
  C++ test, use `target_link_libraries(... autoremesher_core)`. The C test also links `m` for
  `sqrt`/`sin`.
- **M2 Task 3 report:** non-quad count is `face_count - quad_count`. The vertex count is
  `vertex_count`.
- **M5 C#:** the result struct is
  `{IntPtr vertices; nuint vertex_count; IntPtr face_indices; nuint index_count; IntPtr face_sizes; nuint face_count; nuint quad_count;}`,
  sequential layout. Return values are C `int` (1/0). The progress delegate is
  `void(IntPtr tag, float progress, IntPtr status)` with a UTF-8 `status`. Keep the delegate
  alive across `remesh()`.
- Build and test command as before:
  `toolbox run -c instant-meshes-build -- bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"`.
