# M5 — C# P/Invoke console demo

## Task 1 — `csharp-demo/`

A .NET console app with `DllImport("autoremesher_core")` bindings to every
function in M1's C API (weld, create/configure/remesh/get-results/destroy),
running the pipeline in-process on `source.obj` using M4's winning parameter
set, and writing its own output OBJ.

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
