# Handoff — M1 Task 1: CMake target for the algorithm, no Qt

Unit `M1.1`, plan `plan/roadmap/01_M1_native-library.md`, section "Task 1". Branch `wrapper`.

**Status: done.** `libautoremesher_core.so` builds from a new root `CMakeLists.txt`, with TBB
compiled from the vendored `thirdparty/tbb` and linked statically. It has no Qt dependency,
and a CTest checks that. `autoremesher.pro` is untouched.

## What changed and why

| File | Change |
|---|---|
| `CMakeLists.txt` (new, repo root) | `SHARED` target `autoremesher_core` with the 17 sources the plan lists, C++14. Include dirs: `include/` and `thirdparty/eigen` are PUBLIC, because the public header `<AutoRemesher/AutoRemesher>` needs both. `thirdparty/isotropicremesher`, `thirdparty/meshoptimizer/src` and `thirdparty/tbb/include` are PRIVATE. TBB comes from `add_subdirectory(thirdparty/tbb ext_build/tbb)` with the plan's five `TBB_BUILD_*` cache settings, and `tbb_static` is made PIC. The GCC `-Wno-changes-meaning` block is copied from the sibling project. On Linux it links with `-Wl,--no-undefined -Wl,--exclude-libs,ALL`. `enable_testing()` plus one test. |
| `cmake/check_no_qt.cmake` (new) | CTest script that runs `ldd` on the library and fails if the output contains `qt` (case-insensitive) or `not found`. This is exit criterion 1 (`ldd … \| grep -i qt`) as a test. |
| `plan/roadmap/01_M1_native-library.md` | Two "M1.1 measured" notes: the `-lz` correction (under Task 1) and the "defaults don't remesh" finding (under Task 4). See below. |

### Where the plan was wrong or incomplete

1. **`-lz` is not linked.** The plan said to keep `-lz -ldl`. Two problems:
   - Nothing in the core sources or anywhere else in `src/` includes zlib. `grep -rn zlib src`
     finds nothing; `autoremesher.pro` lists it without needing it.
   - The toolbox has only `/usr/lib64/libz.so.1` (zlib-ng) and no `libz.so` dev symlink, so
     `-lz` cannot link. Installing `zlib-ng-compat-devel` would need a package install, which
     this run must not do.

   `-ldl` is kept as `${CMAKE_DL_LIBS}`.
2. **`tbb_static` does not carry its own link deps.** In `thirdparty/tbb/CMakeLists.txt`, only
   the shared `tbb` target links `pthread dl` (line 219). The static one links nothing. So
   `autoremesher_core` links `Threads::Threads` and `${CMAKE_DL_LIBS}` itself. On glibc ≥ 2.34
   both are in libc, which is why `readelf -d` shows no `libdl`/`libpthread` NEEDED entry.
3. **CMake ≥ 4 needs `CMAKE_POLICY_VERSION_MINIMUM`.** The vendored TBB asks for
   `cmake_minimum_required(VERSION 2.8)`, which CMake 4.x rejects. The plan's quoted snippet
   left this out. The sibling project's fix is copied: set it to 3.5 on CMake ≥ 4 unless the
   user already set it. Configure still prints one harmless `CMake Warning (deprecated)` from
   TBB's line 1.
4. **The vendored TBB is the same release as the sibling's, not a byte-identical drop.** Both
   are TBB 2017.0 (interface 9100). The CMakeLists differ in one line, the default of
   `TBB_BUILD_TESTS`, which we force OFF anyway. The plan's pattern applies unchanged.

## Measured (toolbox `instant-meshes-build`: Fedora, GCC 16.2.1, CMake 4.4.3 at `~/.local/bin/cmake`, 8 hardware threads; Release)

- **Driver CI command, from a deleted `build/`:** `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
  - Exit 0 in about 25 s wall.
  - No `error:` lines and no `CMake Error`. The only CMake warning is TBB's deprecation notice.
  - 30 compiler warnings, all `-Wcast-user-defined` from `thirdparty/tbb/include/tbb/atomic.h:548`.
  - CTest: 1/1 passed (`autoremesher_core_no_qt`).
- **`build/libautoremesher_core.so`:** 1,348,864 bytes.
  - `ldd`: only `libstdc++`, `libm`, `libgcc_s`, `libc` and the loader. `ldd … | grep -i qt` prints nothing.
  - `nm -D -u | grep tbb`: 0, so TBB is resolved inside the library.
  - The 41 dynamic symbols with "tbb" in the name are header template instantiations (V/W,
    weak). No `libtbb_static.a` object symbols are exported.
  - `AutoRemesher::AutoRemesher::remesh()` is exported (default visibility; see "Next units").
- **The no-Qt test checks both ways:**
  - With a fake `ldd` on `PATH` that prints `libQt6Core.so.6`, the script fails with "depends on Qt".
  - With a missing path, it fails with "LIBRARY not set or missing".
  - The toolbox has no Qt libs, so I could not test it against a real Qt library.
- **Runtime sanity check (throwaway, not committed):** `/tmp/arcore_smoke/smoke.cpp`, linked
  against the `.so`.
  - It reads `source.obj`: 97,400 `v` lines and 175,566 `f` lines, all triangles.
  - It welds by exact `(x,y,z)` with a `std::map`: **87,691 unique positions**. This matches
    the plan's exit-criterion number.
  - It then calls `remesh()`:
    - **With no target set:** `remesh()` returns `false` immediately with "target triangle
      count must be greater than zero". `m_targetTriangleCount` defaults to 0.
    - **With `setTargetTriangleCount(100000)`** (the GUI default of 50,000 quads × 2,
      `mainwindow.cpp:1078`; scaling not set, so 0 instead of the GUI's 1.0): `ok = 1`,
      **25,988 quads, 25,920 vertices, 37.0 s wall**, ~105 % CPU, 680 MB max RSS.
  - So the `.so` loads, its static TBB initialises, and the full pipeline runs end to end.

## What I got wrong on the way

- My first version linked `z` as the plan said. I checked the toolbox before building, found
  that `libz.so` does not exist, and found that nothing uses zlib. It never reached a build.
- I first wrote `cmake_minimum_required(VERSION 3.5...3.31)`. `target_link_options` needs
  3.13, so I raised it to `3.13...3.31` before committing. CMake 4.4 would not have
  complained, which is why it nearly slipped through.
- I assumed "remesh() with defaults" (plan Task 4) would work. It does not (above).

## Exit criteria of M1 (phase-wide; held at M1.4) — status after this unit

- [x] *`cmake --build` produces `libautoremesher_core.so` with zero Qt symbols.* **Met, and
  guarded by a CTest.** Task 2 adds sources to the same target, and the test keeps covering them.
- [ ] *The weld pass collapses 97,400 → exactly 87,691, asserted in the smoke test.* **Open.**
  Task 3 (weld) and Task 4 (assertion) remain. My throwaway exact-position `std::map` dedup
  confirms the number is reachable.
- [ ] *`test_core` passes, producing a non-empty quad mesh from `source.obj`.* **Open** (Task 4).
  The throwaway run above shows it can: 25,988 quads in 37 s at target 100,000 triangles.

## Open

1. **Optimisation flags differ from the qmake build.** CMake Release gives `-O3 -DNDEBUG`.
   `autoremesher.pro`'s Linux block also has `-flto -funroll-loops -march=x86-64-v2`, which I
   did not copy. Add them if M4's timings must match the GUI's.
2. **Symbol visibility is default (everything exported).** That is deliberate for now: Task 4's
   test may use the C++ class directly. Task 2 may want hidden visibility with an export macro
   on the C API only.
3. **`-lz` correction.** If you want `-lz` kept anyway, it needs `zlib-ng-compat-devel` in the
   toolbox, and it would add a pointless NEEDED entry.
4. **No real-Qt negative test** of `check_no_qt.cmake`, because the toolbox has no Qt. Only the
   fake-`ldd` test was done.

## For the next units

- **Build:** `toolbox run -c instant-meshes-build -- bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"`.
  - The library is `build/libautoremesher_core.so`; TBB is `build/ext_build/tbb/libtbb_static.a`.
  - `build/` is already in `.gitignore`.
- **Adding sources (Task 2 `autoremesher_core_api.cpp`, Task 3 `weld.cpp`):** append them to
  the `add_library(autoremesher_core SHARED …)` list.
  - `--no-undefined` turns any missing definition into a link error.
  - `src/` itself is **not** on the include path, only `include/`. Either add
    `${CMAKE_CURRENT_SOURCE_DIR}/src` or include by relative path.
- **Tests (Task 4):** `enable_testing()` is already called. Add the executable and an
  `add_test`, and link `autoremesher_core`. That brings in the PUBLIC `include/` and
  `thirdparty/eigen` dirs.
  - The test must set a target triangle count. The class default (0) makes `remesh()` fail.
  - Expect about 37 s for one `source.obj` remesh at target 100,000 in Release. Set a CTest
    `TIMEOUT` with headroom; the default is 1500 s, which is fine.
- **Task 2's quad→triangle factor:** `mainwindow.cpp:1078` and `:1122` both use
  `parameters.targetTriangleCount = m_targetQuadCount * 2`. `quadmeshgenerator.cpp:84` passes
  it through unchanged, and only when `> 0`. Scaling is likewise only set when `> 0`, and the
  GUI default is 1.0.
- **TBB includes:** `autoremesher.cpp` and `meshseparator.cpp` prefer `<oneapi/tbb/…>` via
  `__has_include`. The toolbox has no system TBB headers, so the vendored legacy `tbb/…`
  headers are used, which is correct. If a system oneTBB ever shows up in `/usr/include`, the
  headers would silently mismatch the vendored `tbb_static`. Watch for this if the toolbox
  changes.
