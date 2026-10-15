# Handoff — M5 Task 2: parity check

Unit `M5.2`, plan `plan/roadmap/05_M5_pinvoke_demo.md`, section "Task 2". Branch `wrapper`.

**Status: done. Parity holds and there was no divergence to run down.**

- On `source.obj`, at M4.3's winning parameters, the C# demo and the native `autoremesher-cli`
  write **byte-identical files**.
- **M3's scorer** gives an all-zero score between the two files, in both directions.
- Scored against `quad-remeshed.obj`, the two files get **the same record**.
- **A new CTest test, `test_csharp_parity`, pins this.** It runs 11 synthetic cases (about 2 s)
  and the `source.obj` case (about 37 s).
- **The driver's CI command passes: 16/16 tests.**

This is M5's last unit. Both exit criteria hold; the evidence is at the end.

## What changed and why

| File | Change |
|---|---|
| `tests/test_csharp_parity.py` (new) | **The parity test.** It runs the CLI and the demo on the same input with the same arguments. It checks that the demo loaded the library under test. It scores the two outputs against each other with M3's scorer (`score_remesh.score`), both ways, and then compares their sha256. See "The test". |
| `CMakeLists.txt` | **CTest `test_csharp_parity`**, added only when both `dotnet` and Python 3 are found. It gets the CLI, the demo's DLL, the library, `dotnet`, `source.obj` and `quad-remeshed.obj` through the environment. Timeout 600 s. |
| `csharp-demo/README.md` | A paragraph on the parity test. |
| `plan/roadmap/05_M5_pinvoke_demo.md` | A "Done in M5.2" note under Task 2. **No criterion was changed**: none was wrong. |

No source file of the library, the CLI or the demo was changed.

## Measured

Measured 2026-10-15 in the `instant-meshes-build` toolbox (Fedora 44): .NET SDK 8.0.131,
runtime 8.0.31, Python 3.14, Release build.

- **The sources:** the library, CLI and demo are unchanged since M5.1 (`9465b7a1`).
- **The input:** `source.obj` is unchanged: 97,400 vertices and 175,566 triangles, welded to 87,691.
- **The settings:** every run passed all six explicitly: `--target-quads 30000 --edge-scaling 1.25
  --sharp-edge 90 --smooth-normal 0 --adaptivity 0.5 --anisotropy 1`.
- **The library:** both callers used `build/libautoremesher_core.so`.
  - The demo was given it through `$AUTOREMESHER_CORE_LIBRARY`, and its `library:` line confirms it.
  - The CLI resolves it through its soname and rpath; `ldd` confirms it, and so does the test.

### Fresh runs by hand, outputs in `/tmp/parity/` (not committed)

**M4.3's winner, no split:**

| | CLI | C# demo |
|---|---|---|
| time, read to written | 16.9 s | 17.1 s |
| vertices / faces | 11,050 / 11,119 | 11,050 / 11,119 |
| face sizes | 11,065 quads, 53 pentagons, 1 triangle | the same |
| sha256 | `fdbaf07b73754938822b964e64d62f389afad3b0407855e6043b15260962e0f2` | the same |

The hash is also the hash of M4.3's committed trial,
`tuning_runs/unsplit/trials/tq30000_es1.25_se90_sn0_ad0.5_an1/out.obj`.

**M3's scorer** (`scripts/score_remesh.py`, `scorer_version` 1, every vertex, no sampling):

- **The demo against the CLI, and the CLI against the demo:**
  - quads delta 0; non-quad faces 54 = 54; vertices 11,050 = 11,050; degenerate quads 0 = 0.
  - edge-length CV 0.36312893… on both sides, delta 0.0.
  - `fit` and `fit_reverse`: 11,050 samples each; mean, rms, p95 and max all exactly 0.0.
- **Each against `quad-remeshed.obj`:** the two records are equal, field for field, apart from the
  `candidate` path. The values:

  | field | value |
  |---|---|
  | quads | 11,065 vs 11,054 (+11, +0.0995 %) |
  | non-quad faces | 54 |
  | edge-length CV | 0.36313 vs 0.55516 |
  | fit mean / rms / p95 / max | 0.065278 / 0.096940 / 0.20207 / 0.84937 |
  | fit_reverse mean / rms / p95 / max | 0.073621 / 0.14323 / 0.23776 / 1.8303 |

- **Time:** each score took 1.2–1.4 s.

**The same winner with `--split-to-quads`, measured once and not in CTest:**

- **The output:** 44,429 vertices and 44,528 quads on both sides.
- **sha256** `3e4d4375937f4127fc1fadb79f9f737e3a0fdb8979417475c5270bb91a14e1c4`, the same for both.
- **The scorer between them:** all zeros, 44,429 samples each way.
- **Against the target:** the two records are equal. Fit mean is 0.068415, fit_reverse mean 0.072873.
- **Why this case matters:** it exercises `autoremesher_core_split_into_quads()`. The demo calls
  it with pinned managed arrays in both directions, where the CLI passes raw native buffers.

### The CI command on the final tree

- **Exit 0**, 2 min 9 s wall; the build was incremental.
- **16/16 tests pass.** The new `test_csharp_parity` took 39.7 s.
- **The slow ones:** `test_core` 36.9 s, `test_tune_params` 25.3 s, `csharp_demo_source` 17.0 s.

## The test

`tests/test_csharp_parity.py` is a `unittest` file, like the other Python tests.

**Every case runs both programs** with identical `-i`, `-o` and settings arguments. It needs both
exits to be 0, and the demo's `library:` line to name `$AUTOREMESHER_CORE_LIBRARY`. Then
`assert_parity` makes two checks:

1. **M3's scorer, both ways.** These must be equal: the quads, the non-quad faces, the vertices,
   the edge-length CV and the degenerate quads. `fit` and `fit_reverse` must have max 0.0 over
   every vertex. When this fails, the message lists every field that differs, with its numbers,
   so a divergence is reported by its size.
2. **The sha256 of the two files.**

The cases:

| test | input | settings |
|---|---|---|
| `test_both_load_the_same_library` | — | `ldd autoremesher-cli` resolves `libautoremesher_core` to `$AUTOREMESHER_CORE_LIBRARY` |
| `test_sphere_soup` | `test_cli.cpp`'s UV sphere, written as a triangle soup, so both callers weld it | the winner at 300 target quads; `test_cli.cpp`'s all-flags-moved set; the winner with `--split-to-quads` |
| `test_quad_box` | `Tests.cs`'s 2×1×1 box, written as quads, with negative indices and `v x y z 0 0 0` lines, so both readers fan-triangulate polygons | the winner at 300; the same with the split; each of the six settings moved alone, as in `Tests.cs`'s `EverySetterTakesEffect`. It also checks that all eight outputs differ, so each case tests something distinct. |
| `test_source_obj_at_m4_winner` | `source.obj` (skipped if missing) | M4.3's winner. It also checks the demo's printed sha256, and that both outputs score the same against `quad-remeshed.obj`; that part is skipped if the target is missing. The fit is non-zero there, so it is a real comparison. |

**Skips:**

- **Without `$AUTOREMESHER_CLI`, `$AUTOREMESHER_CSHARP_DEMO` and `$AUTOREMESHER_CORE_LIBRARY`,** the
  whole class is skipped, as when the file is run by hand.
- **When they are set but a path does not exist,** the test fails instead. CTest always sets them,
  so a broken build path cannot pass silently.

**Mutations of `csharp-demo/Remesher.cs`.** Each was applied alone, rebuilt and reverted; the
`source.obj` case was skipped for speed. All were caught:

| mutation | caught by |
|---|---|
| `set_anisotropy` dropped | 2 cases: box `anisotropy`, sphere `all_moved`. The scorer reports edge CV 0.24625 vs 0.23339 and fit max 0.0621. |
| the last result vertex not copied out (`Marshal.Copy` length − 3) | all 11 synthetic cases |
| the last welded triangle not passed to `create` (`triangle_count − 1`) | all 11 synthetic cases |

**Not red first.** Parity already held when I wrote the test, so it passed on its first run. The
mutations above are what shows it can fail.

## What I got wrong on the way

- **The first version compared the hashes before scoring.**
  - On the mutations, every failure was a bare sha256 mismatch, and the scorer never ran.
  - That is useless for running down a divergence, which is what the plan asks the scorer for.
  - The scorer now runs first and lists what differs. The hash comparison runs last, to catch
    any byte difference the scorer cannot see, such as the header or number formatting.
  - `maxDiff = None` was needed for the list to print in full.
- **The first skip condition also checked that the paths existed.** Under CTest, a wrong DLL path
  would have skipped the class, and CTest counts a skip as a pass. Paths that are given but
  missing now fail in `setUp`.

## Open

1. **Windows P/Invoke, per D3: not attempted, as the exit criterion says.**
   - Nothing has been built or run on Windows: no `.dll` build of `autoremesher_core`, and no run
     from a Windows .NET host.
   - **On paper, the bindings are ready:** the calling convention is Cdecl, the structs use
     `nuint`/`IntPtr` (blittable, with the layout pinned in `csharp_demo`), and the resolver knows
     the `.dll` name.
   - **Unknowns:**
     - whether the library and vendored TBB build with MSVC at all;
     - the `.dll`'s exports, since there is no `__declspec(dllexport)` today;
     - whether the output matches Linux. Floating point and TBB scheduling may differ, so Windows
       parity would have to be measured against a Windows CLI, not against Linux's hashes.
2. **`--split-to-quads` parity on `source.obj` is measured once, not pinned.**
   - The synthetic split cases are in CTest.
   - Adding `source.obj`'s split would cost about 35 s more per CI run.
3. **Carried over from M5.1:**
   - The demo's OBJ reader differs from the CLI's on lone-`\r` line endings and on hex floats;
     neither occurs in these inputs.
   - `source.obj` and `quad-remeshed.obj` are untracked, so the `source.obj` cases skip on a fresh
     clone.
   - The core's output has non-manifold edges (8 in the winner).

## M5 exit criteria (phase-wide)

- [x] **Parity confirmed on Linux.**
  - **The setup:** .NET 8 (runtime 8.0.31) and the native `build/libautoremesher_core.so`, loaded by
    a conventional `DllImport("autoremesher_core")`. A `NativeLibrary.SetDllImportResolver`
    resolves it through `$AUTOREMESHER_CORE_LIBRARY`, then the runtime's search, then `<repo>/build/`.
  - **The evidence:**
    - **`source.obj` at M4.3's winner:** the two files are byte-identical (`fdbaf07b…62e0f2`). M3's
      scorer is all zeros between them, both ways, and they get the same record against the
      target. See "Measured".
    - **The same with `--split-to-quads`:** byte-identical (`3e4d4375…a14e1c4`).
    - **11 synthetic cases** (welding, polygon reading, every setter, the split), byte-identical
      and scorer-identical.
    - **The check is pinned** in CTest `test_csharp_parity`, which passed in the CI command
      (16/16, exit 0).
- [x] **Windows P/Invoke recorded as an explicit open item, not attempted (per D3).**
  - It is Open #1 above, with what is known and what is not.
  - M5.1's handoff recorded it too (its Open #2).
  - Nothing Windows-specific was built or run.

## For the next unit

**M5 is closed.** Nothing in this phase is waiting on another unit. If anyone changes the library
or the CLI, here is what to know:

- **Divergence fails the build.** `test_csharp_parity` fails if the demo and the CLI diverge on any
  case, and the scorer's numbers in its message show by how much.
- **The test does not pin hashes, only equality between the two callers.** An intended algorithm
  change passes it as long as both sides change together. `csharp_demo_source` and M4's committed
  results are what pin the winner's counts.
- **The demo's settings are passed explicitly in every case.** The demo's defaults (M4.3's winner)
  are not the CLI's, so a case that leaned on defaults would compare different settings.
