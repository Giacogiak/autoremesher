# Handoff — M5 Task 1: `csharp-demo/`

Unit `M5.1`, plan `plan/roadmap/05_M5_pinvoke_demo.md`, section "Task 1". Branch `wrapper`.

**Status: done.** `csharp-demo/` is a .NET 8 console app. It binds every function of the
`autoremesher_core` C API with `DllImport("autoremesher_core")`. It runs the CLI's pipeline
in-process on `source.obj` with M4.3's winning parameters, and writes its own OBJ.

- **CMake builds it** into `build/csharp-demo/` whenever `dotnet` is on the path.
- **CTest runs two new tests:** `csharp_demo` (0.6–0.8 s) and `csharp_demo_source` (17 s).
- **The driver's CI command passes: 15/15 tests.**

**One measurement belongs to Task 2, not to this unit, but is worth knowing now.** On
`source.obj`, with its defaults, the demo's output is **byte-identical to the CLI's M4.3 winner**:
sha256 `fdbaf07b73754938822b964e64d62f389afad3b0407855e6043b15260962e0f2`, the hash
`HANDOFF_M4_TASK3.md` records. I got this three times: twice through CTest and once through
`dotnet run`. I did not score the two outputs with M3's scorer, and I added no parity test.
Both are Task 2's.

## What changed and why

| File | Change |
|---|---|
| `csharp-demo/NativeMethods.cs` (new) | **The bindings.** All 15 functions of `src/autoremesher_core_api.h`, under their C names. `create` has a second overload that takes `IntPtr`s, so a weld result goes straight in as the CLI passes it. `CoreResult` and `CoreWeldResult` mirror the C structs: `size_t` is `nuint` and every pointer is `IntPtr`, so both are blittable. The progress callback delegate is Cdecl, and its `status` is an `IntPtr` read as UTF-8 during the call. Also the library resolver; see "Which library is loaded". |
| `csharp-demo/Remesher.cs` (new) | **The CLI's pipeline** (`src/cli/main.cpp`) over the bindings: weld → create → free the weld → all six setters → progress handler → remesh → get result → destroy → optional split → copy to managed arrays → free. Every native allocation is released in a `finally`. The delegate is kept alive with `GC.KeepAlive`. The core's failure reason comes through the progress handler, as in the CLI. An exception thrown in the managed callback is caught and rethrown after `remesh` returns, so it never unwinds through native frames. `RemeshSettings` defaults to M4.3's winner. |
| `csharp-demo/ObjIo.cs` (new) | **The OBJ reader and writer, matching `src/cli/obj_io.cpp`.** The reader triangulates each polygon as a fan, accepts `7/1/3` and negative indices, and gives the same error messages. The writer writes the GUI's two header lines and formats numbers as QTextStream does (`%g`, `-0` → `0`, exact ties rounded away from zero), so the output can match the CLI's byte for byte. |
| `csharp-demo/Demo.cs`, `Program.cs` (new) | **The command line.** It has the CLI's options, syntax, ranges and exit codes (0 / 1 / 2) and the CLI's `status NN%` lines. The defaults differ from the CLI's: input `<repo>/source.obj`, output `remeshed_csharp.obj`, and M4.3's parameters. The summary prints the library file actually loaded, the input and weld counts, the parameters, the output's counts by face size, its sha256, and the time. |
| `csharp-demo/AutoRemesherDemo.csproj` (new) | `net8.0`, nullable, warnings as errors, invariant globalization, **no package references**. It excludes `tests/**`. |
| `csharp-demo/tests/` (new) | `AutoRemesherDemo.Tests.csproj` and `Tests.cs`: a console test runner. It has no test framework, so it needs no NuGet and no network. It compiles the demo's sources except `Program.cs`. See "Tests". |
| `csharp-demo/README.md` (new) | Build, run, how the library is found, and what the tests cover. |
| `CMakeLists.txt` | **A `csharp_demo` target (ALL)** runs `dotnet build -c Release --artifacts-path build/csharp-demo` on both projects. It always runs, because `dotnet build` is incremental and knows its own inputs (about 3 s when nothing changed). **Tests `csharp_demo` and `csharp_demo_source`** get `AUTOREMESHER_CORE_LIBRARY=$<TARGET_FILE:autoremesher_core>`. `csharp_demo_source` has `SKIP_RETURN_CODE 77`. If `dotnet` is missing, CMake warns, like the Python block. |
| `.gitignore` | `csharp-demo/bin/` and `csharp-demo/tests/bin/`, which `dotnet run` creates in place. `obj/` was already ignored. |
| `plan/roadmap/05_M5_pinvoke_demo.md` | A "Done in M5.1" note under Task 1. No criterion was changed. |

### Decisions taken

- **`autoremesher_core_split_into_quads()` is bound and reachable, but not called by default.**
  - The plan's list ("weld, create/configure/remesh/get-results/destroy") predates M2.3's split. "Every function in M1's C API" covers it, so I bound it.
  - D6 is reversed, so the demo calls it only with `--split-to-quads`, as the CLI does.
- **The demo's OBJ writer copies the CLI's exactly**, including the QTextStream number quirks.
  - This makes the outputs comparable byte for byte, which is the strongest parity evidence there is.
  - The plan does not require it. Task 2 can still score the two files if that is wanted.
- **One test runner with two modes, not xunit.** The C tests (`test_core_api.c`, etc.) work the
  same way, and a test framework would need a NuGet restore, which needs the network.
- **The library comes from `$AUTOREMESHER_CORE_LIBRARY` first.** CTest sets it, so the tests can
  only load the `.so` that this build made. A test checks that this is the file the resolver
  reports. A wrong path is an error; the resolver does not fall back.

## Which library is loaded

In order:

1. `$AUTOREMESHER_CORE_LIBRARY`, if set.
2. The runtime's own search: the assembly's directory, then `LD_LIBRARY_PATH` and the loader's
   defaults.
3. `<repo>/build/libautoremesher_core.so`. `<repo>` is the nearest ancestor of the assembly, or
   of the working directory, that holds `include/autoremesher_core_api.h`.

The resolver **caches its first handle**; see "What I got wrong" for why. When step 2 succeeds, it
reports the mapped file from `/proc/self/maps`.

Checked by hand in the toolbox, all from `/`:

| how it was run | the summary's `library:` line |
|---|---|
| CMake's build, no env var | `/home/giak/Documents/autoremesher/build/libautoremesher_core.so` (step 3) |
| the same with `LD_LIBRARY_PATH=<repo>/build` | the same file (step 2) |
| `AUTOREMESHER_CORE_LIBRARY=/nope.so` | exit 1: `cannot load libautoremesher_core.so: Unable to load shared library '/nope.so' …` |
| `dotnet run --project csharp-demo` | `<repo>/build/libautoremesher_core.so` |

This answers the exit criterion's question: **on Linux, a conventional `DllImport` with a
`NativeLibrary.SetDllImportResolver` fallback works with this .NET (8.0.131 SDK, 8.0.31
runtime).** Nothing has to be copied next to the assembly.

## Measured

Measured 2026-10-15 in the `instant-meshes-build` toolbox (Fedora 44): .NET SDK 8.0.131,
runtime 8.0.31, CMake 4.4.3, Release build. The library's and the CLI's sources are unchanged
since M2.4. `source.obj` is unchanged: 97,400 vertices and 175,566 triangles, welded to 87,691.

**The demo on `source.obj` with its defaults** (`--target-quads 30000 --edge-scaling 1.25
--sharp-edge 90 --smooth-normal 0 --adaptivity 0.5 --anisotropy 1`, no split):

- **The output:** 11,050 vertices and 11,119 faces: 11,065 quads, 53 pentagons and 1 triangle.
  These are exactly M4.3's numbers.
- **sha256** `fdbaf07b73754938822b964e64d62f389afad3b0407855e6043b15260962e0f2`. That is the
  same as `tuning_runs/unsplit/trials/tq30000_es1.25_se90_sn0_ad0.5_an1/out.obj`, checked with
  `sha256sum` on both files.
- **Time:** 16.9 s and 17.0 s through CTest, and 17.7 s through `dotnet run`, read to written.
  M4.3's CLI took 16.8 s.

**The number formatter**, checked once by hand and not committed:

- **The values:** 299,948 doubles. There were random values in ±300 and in ±1e7 with 0–3
  decimals, halves, quarters, and random bit patterns, of which **2,982 were exact ties**.
- **The reference:** Python's `%g`, with the same tie nudge that `obj_io.cpp` applies.
- **The result: 0 mismatches.** The scripts were in `/tmp/fmtcheck/`.

**The CI command**, run on the final tree:

- **Exit 0**, 90 s wall; the build was incremental.
- **15/15 tests pass.** The two new ones took 0.66 s and 17.19 s.
- **The slow ones:** `test_core` 37.1 s, `test_tune_params` 25.5 s, `test_score_remesh` 5.9 s.

**A from-scratch configure and build** into `/tmp/fresh-build` also passed both C# tests. It was
started before the last change to `Remesher.cs` (the callback exception), so its point is the
fresh configure and restore, not the final code. That
includes TBB and a first `dotnet build` with an empty `obj/`.

- `project.assets.json` lists no libraries.
- The SDK's own `packs/` holds the targeting and apphost packs (`Microsoft.NETCore.App.Ref`, `…Host.fedora.44-x64`).
- So the build needs no network.

## Tests

`csharp_demo` (`csharp-demo/tests/Tests.cs`, against the real library):

- **Struct layout.** `sizeof` is 7 and 4 pointers. Every field's offset is checked. Both structs
  are blittable.
- **Weld.** A two-triangle soup welds to the exact expected arrays. It refuses an out-of-range
  index, a NaN and a NULL array, each time leaving the result zeroed. Empty input welds to an
  empty mesh. Freeing twice is harmless.
- **Create.** Bad input gives NULL. NULL is accepted by `destroy` and `free_result`. Empty input
  creates a handle that will not remesh and has no result.
- **Split, through pinned managed arrays.** One quad comes back as exactly the 9 vertices and 4
  quads of `test_split.c`'s "quad" case. Inconsistent sizes are refused.
- **The sphere (`test_core_api.c`'s).**
  - Through `Remesher.Run`, the counts are 2,880 → 482 welded.
  - The result's invariants hold, including every vertex at radius 0.9–1.1.
  - Progress values stay in 0..1, and the last call is `1, "Done"`.
  - **The same calls made by hand give identical arrays.**
  - With the split, there is one quad per corner, and the remeshed vertices come first.
- **Every setter takes effect.** On a 2×1×1 box, moving any one of the six settings away from
  M4.3's winner changes the output. The same settings run twice give the same output.
- **Failures.**
  - Target 0 gives the core's reason ("target triangle count…") in the exception.
  - A bad index gives "cannot weld".
  - An exception thrown in the progress callback comes back out of `Run`.
- **The number format.** `test_obj_io.cpp`'s values plus 12 more: ties, the `%e`/`%f` switch at
  1e-4 and 1e6, `double.MaxValue` and `double.Epsilon`.
- **The writer.**
  - `test_obj_io.cpp`'s mesh comes out byte for byte.
  - The header constants are checked against `autoremesher.pro`.
  - An empty mesh is the header alone.
- **The reader.**
  - Corner forms, negative indices, forward references, tabs/CRLF and ignored lines all read correctly.
  - Nine bad files give the CLI's exact messages, with their line numbers.
- **The command line.** The defaults are M4.3's. `--name=value` and `--help` work. 12 usage
  errors exit 2. The sphere runs end to end, and the summary's counts match the file. A missing
  input or an unwritable output exits 1.
- **The library.** The loaded file is `$AUTOREMESHER_CORE_LIBRARY`.

`csharp_demo_source` runs the demo with no parameters on `source.obj`. It checks the input and
weld counts, M4.3's parameters in the summary, and the read-back output: 11,050 / 11,119 /
11,065.

**Mutations.** Each was applied alone and reverted; all were caught:

| mutation | caught by |
|---|---|
| `face_sizes` and `index_count` swapped in `CoreResult` | 2 offset checks, then a segfault |
| the tie nudge removed | `FormatQtDouble(123456.5)`, `(-12345.25)`, writer bytes |
| fans from the previous corner instead of the first | 2 reader checks |
| the split's success test inverted | an unhandled `RemeshException` |
| default adaptivity 1 | "defaults are M4.3's winner" |
| each of the six setters dropped from `Remesher.Run`, one at a time | 6/6: target-quads → remesh fails; the other five → "… changes the output" |

**Not red first.** I wrote the code before the tests, and the tests passed on their first run. The
mutations above are what shows they can fail.

## What I got wrong on the way

- **A dropped setter went unnoticed at first.**
  - My first sphere test compared the wrapper with hand-made calls at the core's defaults. So did
    `csharp_demo_source`, in effect: the core's own defaults are sharp-edge 90, smooth-normal 0
    and anisotropy 1, which are exactly the winner's.
  - **Removing `set_anisotropy` passed both tests.**
  - The box test was added for this. I first checked which parameters change the box's output; all
    six do. The anisotropy change shows only in the positions, not in the counts.
- **The resolver mislabelled its library.**
  - The runtime calls the resolver for **every** P/Invoke function, not once per library.
  - The first call loaded `<repo>/build/libautoremesher_core.so` through step 3.
  - Later calls succeeded at step 2, because `dlopen` finds an already-loaded library by its
    soname. So the summary said "the runtime's default search".
  - `LD_DEBUG=files` showed this. The resolver now caches its first handle, and that also stops
    the repeated failing probes.
- **A mutation run used a stale binary.** `value = value;` broke the build, because
  warnings are errors. The runner still ran the binary from the mutation before it. I redid that
  mutation another way; the table above has only clean runs.

## Open

1. **Task 2: the parity check with M3's scorer.** Byte equality is already measured, but no CTest
   pins it yet, and the scorer has not been run on the demo's output.
2. **Windows P/Invoke, per D3: not attempted, as the exit criterion says.** The bindings are
   Windows-ready on paper. The calling convention is Cdecl, the structs use `nuint`/`IntPtr`, and
   the resolver knows the `.dll` name. Nothing has been built or run on Windows.
3. **The reader differs from the CLI's in two edge cases** that do not occur in `source.obj`:
   - .NET's `ReadLine` also ends a line at a lone `\r`, where the CLI's `getline` treats `\r` as a
     blank. A file with classic Mac line endings would be read differently.
   - `double.TryParse` rejects hex floats (`0x1p3`), and `strtod` accepts them.
4. **Carried over:** `source.obj` is untracked, so `csharp_demo_source` skips on a fresh clone; the
   core's output has non-manifold edges (8 in the winner).

## M5 exit criteria (phase-wide), where they stand

- [ ] **Parity confirmed on Linux.** The mechanism is settled: `DllImport` plus a resolver, against
  the `.so` CMake builds. The outputs are byte-identical on `source.obj`. **The bullet stays
  open for Task 2**, which runs the scorer comparison the plan asks for.
- [ ] **Windows P/Invoke recorded as an explicit open item.** It is recorded above (Open #2). The
  phase's last unit should carry it into its handoff.

## For the next unit (M5.2)

- **Build and run:**
  - The CI command builds the demo into `build/csharp-demo/bin/AutoRemesherDemo/release/AutoRemesherDemo.dll`.
  - Run it with `dotnet <that dll> -i source.obj -o <out>`. Set `AUTOREMESHER_CORE_LIBRARY=build/libautoremesher_core.so`
    to be sure which library is used; the summary's `library:` line confirms it.
- **The CLI's side:** `build/autoremesher-cli -i source.obj -o <out> --target-quads 30000
  --edge-scaling 1.25 --sharp-edge 90 --smooth-normal 0 --adaptivity 0.5 --anisotropy 1`, or the
  committed trial `tuning_runs/unsplit/trials/tq30000_es1.25_se90_sn0_ad0.5_an1/out.obj`, which
  is gitignored.
- **Expect identical files.** If they ever differ, check these first:
  - **The writer:** `ObjIo.FormatQtDouble` against `obj_io.cpp`'s `formatNumber`.
  - **The library:** whether both runs loaded the same `.so`.
  - **The settings:** the demo's are in `RemeshSettings` and printed in the summary.
- **Both runs take about 17 s.** `csharp_demo_source` already runs the demo once in CTest, and
  writes `build/csharp_demo_scratch/remeshed_csharp.obj`, which a parity test could reuse.
- **Struct layout** is already pinned in `csharp_demo`. A layout bug would show there before it
  could show up in parity.
