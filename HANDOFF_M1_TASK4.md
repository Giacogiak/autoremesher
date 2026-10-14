# Handoff — M1 Task 4: smoke test

Unit `M1.4`, plan `plan/roadmap/01_M1_native-library.md`, section "Task 4". Branch `wrapper`.
This is the last unit of M1.

**Status: done. All three M1 exit criteria hold** (evidence below). A new CTest, `test_core`,
loads the real `source.obj`, welds it through `autoremesher_core_weld()`, and asserts exactly
**87,691** vertices. It then remeshes the mesh at the GUI defaults through the C API and asserts
a non-empty polygon mesh: 25,932 quads out of 25,988 faces, in about 37 s. One thing the plan
did not anticipate: **`source.obj` is not in git**. The test is therefore skipped, not failed,
on a checkout that lacks the file. See Open #1.

## What changed and why

| File | Change |
|---|---|
| `tests/test_core.cpp` (new) | The smoke test. A minimal inline OBJ loader reads `v` positions and `f` vertex indices, fan-triangulating faces and ignoring `/vt/vn` and everything else. The real OBJ I/O is M2's job. The test asserts the raw counts (97,400 vertices, 175,566 triangles), welds the mesh, and asserts the welded counts (87,691 vertices, 175,566 triangles). Then it calls `create`, `set_target_quad_count(50000)`, `set_scaling(1.0)`, `remesh` and `get_result`, and asserts `quad_count > 0`, `vertex_count > 0`, `quad_count <= face_count`, that `face_sizes` sums to `index_count`, and that every face index is in range. It prints the counts and timings. |
| `CMakeLists.txt` | Adds the `test_core` executable and test, a cache variable `AUTOREMESHER_SOURCE_OBJ` (default `${CMAKE_CURRENT_SOURCE_DIR}/source.obj`) passed as the test's argument, a configure-time `WARNING` if that file does not exist, and `TIMEOUT 600` with `SKIP_RETURN_CODE 77`. |
| `plan/roadmap/01_M1_native-library.md` | An "M1.4 measured" note under Task 4. The plan's criteria were right, so nothing was corrected. |

Design choices:

- **The C API, not the C++ classes.** The test calls the same entry points the CLI (M2) and the
  C# demo (M5) will use, so it guards the path they depend on. It is C++ only because the loader
  is shorter in C++. `test_core_api.c` already checks that the header is valid C.
- **"Defaults" means the GUI's defaults**, as the M1.1 note says: 50,000 target quads (the API
  doubles that to 100,000 triangles) and scaling 1.0. The other four settings are left at the
  core's own defaults, which match the GUI's (sharp edge 90°, smooth normal 0°, adaptivity 1,
  anisotropy 1). This is the configuration M1.1–M1.3 measured, and it gives the same output.
- **The test asserts only the weld counts exactly.** The quad count (25,932) is printed, not
  asserted. The exit criteria ask for an exact assertion only on the weld. The remesh output is
  not specified anywhere as a contract, and pinning it would make M4's parameter work and any
  algorithm change fail the build.
- **A missing file skips the test (exit 77) rather than failing it.** CTest prints
  `***Skipped`, lists the test under "did not run", and still exits 0. A file that exists but has
  different content fails the test, because the raw-count checks catch it.

## Exit criteria of M1: evidence

All measured in the toolbox `instant-meshes-build` (Fedora, GCC 16.2.1, CMake 4.4.3, `nproc` = 8),
Release, on 2026-10-14.

- [x] **`cmake --build` produces `libautoremesher_core.so` with zero Qt symbols.**
  - The driver's CI command, run after `rm -rf build`, exited **0** in 62 s wall (about 25 s
    build plus 37.4 s of tests).
  - It produced `build/libautoremesher_core.so` (1,367,072 bytes).
  - `ldd build/libautoremesher_core.so` lists only `linux-vdso`, `libstdc++`, `libm`,
    `libgcc_s`, `libc` and `ld-linux`.
  - `ldd build/libautoremesher_core.so | grep -i qt` printed nothing and exited 1.
  - `nm -D build/libautoremesher_core.so | grep -ci qt` returned 0.
  - The CTest `autoremesher_core_no_qt` passes; it runs the same `ldd` check.
- [x] **The weld takes `source.obj`'s 97,400 vertices to exactly 87,691, asserted in the smoke
  test.** `test_core` asserts `97400 * 3 == rawVertices.size()` and
  `87691 == welded.vertex_count`, and prints:
  `weld: 97400 -> 87691 vertices, 175566 triangles, 0.036 s`.
  - **The guard catches a change.** I appended one extra vertex (`v 999 999 999 0 0 0`) to a copy
    of `source.obj` in `/tmp/m14/perturbed.obj`. With that file, `test_core` reported
    `2 check(s) failed` (the raw count and the welded count) and exited 1.
  - The `source.obj` measured here has md5 `e290fdb7ad6e507395e9c5c35387c453` and is 6,672,315
    bytes.
- [x] **`test_core` passes, producing a non-empty quad mesh from `source.obj`.**
  - In the CI run: `4/4 Test #4: test_core .... Passed 37.16 sec`, and `100% tests passed out of 4`.
    The other tests also passed: `autoremesher_core_no_qt` (0.01 s), `test_core_api` (0.23 s)
    and `test_weld` (0.00 s).
  - Run directly under `/usr/bin/time -v`, it printed
    `remesh, target 50000 quads: 25920 vertices, 25988 faces, 25932 quads, 36.9 s`. Wall time was
    37.1 s, at 105 % CPU and 678 MB maximum RSS. This is the same output M1.1–M1.3 measured.

Also checked:

- **The skip path.** I configured a separate build with
  `-DAUTOREMESHER_SOURCE_OBJ=/nonexistent.obj`. CMake warned
  `/nonexistent.obj not found; test_core will be skipped`, CTest showed
  `test_core ...***Skipped`, and `ctest` exited 0. Run with no argument, `test_core` exits 2 and
  prints its usage.
- **Warnings.** `tests/test_core.cpp` compiles with no warnings. The only warnings in the build
  are still TBB's, plus its CMake deprecation notice.

## What I got wrong on the way

- **Nothing broke, but the test was not written red-first.** Everything it exercises already
  existed and was measured in M1.1–M1.3, so there was nothing to watch go from red to green. To
  show that the regression guard can fail, I used the perturbed-file run above instead.
- **I first read the CI log for the weld and remesh lines and found none.**
  `ctest --output-on-failure` hides the output of tests that pass. The numbers above come from
  `ctest -V` and from running `build/test_core` directly. If you need the counts from a passing
  CI run, use `ctest -V -R test_core`.

## Open

1. **`source.obj` is untracked and ignored by git** (`.gitignore:8` has `*.obj`, which also
   covers `quad-remeshed.obj`). This makes the exit criteria machine-dependent: on this machine
   the file exists and `test_core` passes, but on a fresh clone it is skipped. The options, for
   Giacomo:
   - **(a) Commit it** with `git add -f source.obj`. That adds 6.7 MB to the history. Do this
     only if the asset may be shared; it is a Rhino export, and I did not commit it because that
     is not my call.
   - **(b) Keep it external**, as now, and treat a skipped `test_core` as acceptable.
   - **(c) Make a missing file a failure.** Remove `SKIP_RETURN_CODE` and return 1 instead of
     77. CI then breaks loudly anywhere the file is missing.

   M2's exit criteria (`autoremesher-cli -i source.obj ...`), M3 and M5 depend on the same file,
   so this question applies to them too.
2. **The core only uses one core.** The run log says "Cores kept busy across the parallel phase:
   1.00 (islands are the unit of parallelism)": `source.obj` is a single island. Each
   remesh therefore costs about 37 s however many threads the machine has. M4's sweep will have
   to plan for that: about 2,160 trials at 37 s each is about 22 h, run serially per process,
   although separate processes would parallelize.
3. Carried over from M1.2/M1.3, still open:
   - No ASan or valgrind run; the toolbox has neither.
   - Hidden symbol visibility is still off.
   - `remesh()` does not store a failure reason; the reason only reaches the progress handler.
   - The output is a polygon layout, not quads only.
   - `PositionKey` welds by grid cell, not by bitwise equality. On `source.obj` the result is
     identical.
   - The weld does not return a vertex remap.

## For the next unit (M2.1, `autoremesher-cli`)

- **The pipeline to copy is the one in `tests/test_core.cpp`:** load, then
  `autoremesher_core_weld`, then `create`, `set_target_quad_count`, `set_scaling`, `remesh`,
  `get_result`, and finally the frees. Its loader is a throwaway. M2.2's OBJ reader replaces it
  and must still accept `source.obj`'s `v x y z 0 0 0` lines and its `# Rhino` header.
- **Faces are polygons.** Write each face with `face_sizes[i]` indices, not 4. On `source.obj`
  the output has 52 pentagons and 4 triangles.
- **Expected numbers on `source.obj` at the defaults:**
  - raw: 97,400 vertices, 175,566 triangles;
  - welded: 87,691 vertices;
  - remeshed: 25,920 vertices and 25,988 faces, 25,932 of them quads, in about 37 s.
- **CI time.** The driver's CI command now takes about 62 s from a clean build. Of that,
  `test_core` accounts for about 37 s and has a `TIMEOUT` of 600 s.
- The build and test command is unchanged:
  `toolbox run -c instant-meshes-build -- bash -c "cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j\$(nproc) && ctest --test-dir build --output-on-failure"`.
