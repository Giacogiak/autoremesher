# M1 — GUI-free native core library

## Task 1 — CMake target for the algorithm, no Qt

Create `CMakeLists.txt` (repo root or a `cmake/` subdir — pick one and be
consistent) defining a shared library target `autoremesher_core` that compiles:

- `src/AutoRemesher/{autoremesher,isotropicremesher,parameterizer,surfacemesh,
  singularitysimplifier,constrainedleastsquares,mixedintegerleastsquares,
  framefield,quadparameterizer,quadextractor,positionkey,meshseparator}.cpp`
- `thirdparty/isotropicremesher/{isotropicremesher,isotropichalfedgemesh,
  axisalignedboundingboxtree}.cpp`
- `thirdparty/meshoptimizer/src/{simplifier,indexgenerator}.cpp`

Include paths: `include/`, `thirdparty/eigen` (header-only),
`thirdparty/isotropicremesher`, `thirdparty/meshoptimizer/src`.
`CMAKE_CXX_STANDARD 14` to match `CONFIG += c++14`. Do not touch
`autoremesher.pro` — this is a wholly separate, additive build.

**TBB: build from the vendored source, do not look for a system package.**
This dev environment has no `tbb-devel` installed and no passwordless sudo to
install one. The sibling Instant Meshes P/Invoke project already solved this
identical problem for the identical vendored TBB drop: `instant-meshes/CMakeLists.txt`
lines 87-97 do
```cmake
set(TBB_BUILD_STATIC ON CACHE BOOL " " FORCE)
set(TBB_BUILD_SHARED OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TBBMALLOC OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TBBMALLOC_PROXY OFF CACHE BOOL " " FORCE)
set(TBB_BUILD_TESTS OFF CACHE BOOL " " FORCE)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ext/tbb ext_build/tbb)
set_property(TARGET tbb_static PROPERTY POSITION_INDEPENDENT_CODE ON)
```
and, right before it (lines 65-76), a GCC-version compatibility flag:
```cmake
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-Wno-changes-meaning" HAS_WNO_CHANGES_MEANING)
  if (HAS_WNO_CHANGES_MEANING)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-changes-meaning")
  endif()
endif()
```
(GCC ≥13 rejects the bundled TBB headers' use of `tbb/task.h` without it.)
Apply the same two blocks here against this repo's own `thirdparty/tbb`
(`add_subdirectory(thirdparty/tbb ext_build/tbb)`), link the resulting
`tbb_static` target into `autoremesher_core`, and keep `-lz -ldl` for the
other Linux link deps `autoremesher.pro`'s `unix:!macx` block already lists.
No system `tbb-devel` package, no sudo, anywhere in this plan.

**Toolchain location:** this machine's host has no compiler either — build
and run everything inside the existing `instant-meshes-build` toolbox
container (`toolbox run -c instant-meshes-build -- <command>`), which already
has `gcc-c++` 16.2.1, `cmake` 4.4.3 and `dotnet-sdk-8.0` installed from the
sibling project's own setup. Every build/test command in this plan's later
phases must be run the same way.

> **M1.1 measured (correction):** `-lz` is **not** linked. Nothing in this
> library (or the GUI) includes zlib, and the toolbox has only
> `libz.so.1` — no `libz.so` development symlink — so `-lz` fails to link.
> `tbb_static`, unlike the shared `tbb` target, does not declare its own
> `pthread`/`dl` link deps, so `autoremesher_core` links
> `Threads::Threads` and `${CMAKE_DL_LIBS}` itself. `CMakeLists.txt` lives at
> the repo root. See `HANDOFF_M1_TASK1.md`.

## Task 2 — extern "C" API

Add `src/autoremesher_core_api.{h,cpp}` wrapping `AutoRemesher::AutoRemesher`
behind a C ABI: opaque handle create/destroy, one setter per existing public
setter (`target_quad_count` — note the GUI/CLI convention is *quads*, the core
API takes *triangle* count so double it, matching `MainWindow`'s own
`targetQuads * 2` convention — check `quadmeshgenerator.cpp` for the exact
factor before hardcoding it), `scaling`, `sharp_edge_degrees`,
`smooth_normal_degrees`, `adaptivity`, `anisotropy`, a progress-callback
setter taking the existing `AutoRemesherProgressHandler` signature verbatim,
`remesh()`, and getters returning flat arrays (`double* vertices, size_t
vertex_count`, `uint32_t* quad_indices, size_t quad_count` — flat, not
`std::vector<std::vector<size_t>>`, since that type cannot cross a C ABI or be
marshaled cleanly from C#) that the caller must free via a matching
`autoremesher_core_free_result()`.

## Task 3 — vertex welding utility

Add a weld pass (e.g. `src/weld.{h,cpp}`, no Qt) that takes raw OBJ vertices
and triangle indices, builds a `std::map<AutoRemesher::PositionKey, size_t>`
keyed by exact (quantized) position, collapses duplicate-position vertices to
one index each, and remaps every triangle's indices accordingly (per decision
D2 — exact-position dedup only, no epsilon parameter). Expose it from the same
C API as `autoremesher_core_weld(...)`, independent of `remesh()`, so the CLI
(M2) and the C# demo (M5) can both call it before remeshing.

## Task 4 — smoke test

A throwaway `test_core.cpp` (or a CTest target) that links `autoremesher_core`,
loads `source.obj`'s raw vertex/triangle arrays (no OBJ parser needed yet —
hardcode a tiny synthetic mesh, or a minimal inline loader is fine here; the
real OBJ I/O is M2's job), welds, and calls `remesh()` with defaults, asserting
`quad_count > 0`.

> **M1.1 measured:** `AutoRemesher` with its own constructor defaults does
> **not** remesh — `m_targetTriangleCount` defaults to 0 and `remesh()` returns
> `false` with "target triangle count must be greater than zero". "Defaults"
> here has to mean the GUI's defaults: 50,000 target quads → `100000`
> triangles (`mainwindow.cpp:1078`), scaling 1.0. With target 100000 (scaling
> left unset) on welded `source.obj` it gave 25,988 quads in 37 s (Release,
> 8-thread toolbox).

## Exit criteria

- `cmake --build` produces `libautoremesher_core.so` with zero Qt symbols
  (`ldd libautoremesher_core.so | grep -i qt` returns nothing).
- The weld pass run on `source.obj`'s 97,400 vertices collapses them to exactly
  87,691 unique positions (the number already confirmed in this repo's current
  `source.obj`) — assert this exact count in the smoke test as a regression
  guard.
- `test_core` passes, producing a non-empty quad mesh from `source.obj`.
