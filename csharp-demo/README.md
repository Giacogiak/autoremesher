# csharp-demo — AutoRemesher from .NET over P/Invoke

A .NET 8 console app that runs AutoRemesher in-process through the
`autoremesher_core` shared library's C API (`src/autoremesher_core_api.h`),
with `DllImport("autoremesher_core")`. It does what `autoremesher-cli` does,
from managed code: read an OBJ, weld it, remesh it, write the result as an OBJ
in the CLI's exact format.

Without options it remeshes `<repo>/source.obj` with M4.3's winning parameters
(`--target-quads 30000 --edge-scaling 1.25 --sharp-edge 90 --smooth-normal 0
--adaptivity 0.5 --anisotropy 1`, no split; see `HANDOFF_M4_TASK3.md`) into
`remeshed_csharp.obj` in the working directory. `--help` lists the options,
which are the CLI's.

Linux only for now (decision D3). Windows needs a `.dll` build of the library,
which nobody has tried.

## Files

| file | what it is |
|---|---|
| `NativeMethods.cs` | The bindings: every function of the C API, the two result structs, the progress callback, and how the library is found |
| `Remesher.cs` | The CLI's pipeline over the bindings (weld, create, six setters, progress handler, remesh, get result, destroy, optional split). Frees everything the library allocates. |
| `ObjIo.cs` | OBJ reader and writer, matching `src/cli/obj_io.cpp` (fan triangulation, QTextStream's number format) |
| `Demo.cs`, `Program.cs` | The command line and its summary |
| `tests/` | A test runner (a console app, no NuGet packages) that CTest runs |

## Build and run

The library first, with CMake, from the repository root. CMake also builds the
demo and its tests into `build/csharp-demo/` when it finds `dotnet`:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
ctest --test-dir build -R csharp_demo --output-on-failure
```

Then, from anywhere:

```
dotnet run --project csharp-demo -c Release -- -o remeshed_csharp.obj
# or the build CMake made:
dotnet build/csharp-demo/bin/AutoRemesherDemo/release/AutoRemesherDemo.dll
```

It takes about 17 s on `source.obj`. The summary at the end names the library
file it loaded and the output's sha256.

## Which library it loads

In this order:

1. `$AUTOREMESHER_CORE_LIBRARY`, the path to a `libautoremesher_core.so`, if
   set. A wrong path is an error, not a reason to look elsewhere.
2. The runtime's own search: next to the assembly, then `LD_LIBRARY_PATH` and
   the loader's default paths.
3. `<repo>/build/libautoremesher_core.so`, where `<repo>` is found by walking up
   from the assembly, then from the working directory, to the directory that
   holds `include/autoremesher_core_api.h`.

CTest uses the first, with the library it just built.

## Tests

`ctest -R csharp_demo` runs two tests:

- `csharp_demo` (under a second): the structs' layout, every binding against
  the real library (exact arrays both ways for weld and split, refusals, NULL
  handling), a remesh of a sphere through the wrapper compared with the same
  calls made by hand, a check that each of the six settings changes the
  output of a box, the OBJ writer byte for byte against `test_obj_io.cpp`'s
  case, the reader's forms and errors, and the command line end to end.
- `csharp_demo_source` (about 17 s, skipped without `source.obj`): the demo
  with its defaults on `source.obj`, checking the output's vertex, face and
  quad counts against M4.3's winner.
