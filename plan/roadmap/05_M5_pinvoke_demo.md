# M5 — C# P/Invoke console demo

## Task 1 — `csharp-demo/`

A .NET console app with `DllImport("autoremesher_core")` bindings to every
function in M1's C API (weld, create/configure/remesh/get-results/destroy),
running the pipeline in-process on `source.obj` using M4's winning parameter
set, and writing its own output OBJ.

> **Done in M5.1** (`HANDOFF_M5_TASK1.md`): `csharp-demo/` (.NET 8, no NuGet
> packages), with bindings to every function of the C API, including
> `autoremesher_core_split_into_quads()` (added in M2.3, opt-in via
> `--split-to-quads` as in the CLI, off by default). Its defaults are M4.3's
> winner, and its OBJ writer copies the CLI's byte format. CMake builds it into
> `build/csharp-demo/` when it finds `dotnet`. CTest runs `csharp_demo` (the
> bindings, the writer, the command line, on small meshes) and
> `csharp_demo_source` (the defaults on `source.obj`: 11,050 vertices, 11,119
> faces, 11,065 quads). The library is loaded from `$AUTOREMESHER_CORE_LIBRARY`,
> then the runtime's own search, then `<repo>/build/`. **Measured, not yet
> Task 2's check:** on `source.obj` the demo's output has the same sha256 as
> the CLI's winner, `fdbaf07b…62e0f2`. Scoring both with M3's scorer is Task 2's
> job.

## Task 2 — parity check

Score the C# demo's output against the native `autoremesher-cli`'s output
(same input, same parameters) with M3's scorer — they should be effectively
identical (same algorithm, same library, two callers). Any divergence point to
a marshaling bug (e.g. flat-array layout mismatch, struct packing) rather than
an algorithm difference, and must be run down before declaring this phase
done — this is exactly the kind of gap the sibling Instant Meshes project's
own M4.2 ("weld parity evidence") caught.

## Exit criteria

- Parity confirmed on Linux (.NET + the native `.so`, e.g. via `dotnet run`
  against a `libdl`/`NativeLibrary.Load`-resolved path, or a conventional
  `DllImport` resolve — whichever this environment's .NET supports cleanly).
- Windows P/Invoke (a `.dll` build of `autoremesher_core`, tested from an
  actual Windows/.NET host) is recorded as an explicit open item, not
  attempted (per D3).
