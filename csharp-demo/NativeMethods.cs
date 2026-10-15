/*
 *  P/Invoke bindings to every function of the autoremesher_core C API
 *  (src/autoremesher_core_api.h), one to one, with the C names.  The structs
 *  mirror the header's field for field; size_t is nuint and every pointer is
 *  IntPtr, so both structs are blittable and are passed by reference as the
 *  C functions take them.
 */
using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace AutoRemesherDemo
{
    /* autoremesher_core_result: 7 pointer-sized fields, 56 bytes on 64-bit. */
    [StructLayout(LayoutKind.Sequential)]
    public struct CoreResult
    {
        public IntPtr vertices; /* double[vertex_count * 3] */
        public nuint vertex_count;
        public IntPtr face_indices; /* uint32_t[index_count] */
        public nuint index_count;
        public IntPtr face_sizes; /* uint32_t[face_count] */
        public nuint face_count;
        public nuint quad_count;
    }

    /* autoremesher_core_weld_result: 4 pointer-sized fields, 32 bytes on 64-bit. */
    [StructLayout(LayoutKind.Sequential)]
    public struct CoreWeldResult
    {
        public IntPtr vertices; /* double[vertex_count * 3] */
        public nuint vertex_count;
        public IntPtr triangle_indices; /* uint32_t[triangle_count * 3] */
        public nuint triangle_count;
    }

    /* autoremesher_core_progress_handler.  `status` is a C string that is only
     * valid during the call, so it stays an IntPtr here; read it with
     * Marshal.PtrToStringUTF8 before returning. */
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    public delegate void CoreProgressHandler(IntPtr tag, float progress, IntPtr status);

    public static class Native
    {
        public const string Library = "autoremesher_core";

        /* Set by the resolver to the file it loaded, or to a note that the
         * runtime's own search found the library. */
        public static string LoadedFrom { get; private set; } = "(not loaded yet)";

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr autoremesher_core_create([In] double[]? vertices, nuint vertex_count,
            [In] uint[]? triangle_indices, nuint triangle_count);

        /* The same function, for arrays the library allocated (a weld result). */
        [DllImport(Library, EntryPoint = "autoremesher_core_create", CallingConvention = CallingConvention.Cdecl)]
        public static extern IntPtr autoremesher_core_create(IntPtr vertices, nuint vertex_count,
            IntPtr triangle_indices, nuint triangle_count);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_destroy(IntPtr handle);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_target_quad_count(IntPtr handle, nuint target_quad_count);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_scaling(IntPtr handle, double scaling);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_sharp_edge_degrees(IntPtr handle, double degrees);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_smooth_normal_degrees(IntPtr handle, double degrees);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_adaptivity(IntPtr handle, double adaptivity);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_anisotropy(IntPtr handle, double anisotropy);

        /* The caller must keep `handler` reachable for as long as the library
         * may call it (until the handler is replaced or the handle destroyed):
         * the marshaller does not. */
        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_set_progress_handler(IntPtr handle,
            CoreProgressHandler? handler, IntPtr tag);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern int autoremesher_core_remesh(IntPtr handle);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern int autoremesher_core_get_result(IntPtr handle, out CoreResult result);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_free_result(ref CoreResult result);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern int autoremesher_core_split_into_quads(in CoreResult mesh, out CoreResult result);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern int autoremesher_core_weld([In] double[]? vertices, nuint vertex_count,
            [In] uint[]? triangle_indices, nuint triangle_count, out CoreWeldResult result);

        [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
        public static extern void autoremesher_core_free_weld_result(ref CoreWeldResult result);

        /* Where DllImport finds the library, in order:
         *   1. $AUTOREMESHER_CORE_LIBRARY, a path to the file, if set;
         *   2. the runtime's own search: next to this assembly, then the
         *      system's (LD_LIBRARY_PATH and the loader's default paths);
         *   3. <repo>/build/, where the README's CMake build puts it.
         * Installed once per process; later calls do nothing. */
        public static void InstallResolver(string? repoRoot)
        {
            if (s_resolverInstalled)
                return;
            s_resolverInstalled = true;
            NativeLibrary.SetDllImportResolver(typeof(Native).Assembly, (name, assembly, searchPath) => {
                if (name != Library)
                    return IntPtr.Zero;
                /* The runtime asks again for each function it binds, and a
                 * second search would find the loaded library by its soname
                 * whatever file it came from: answer with the first load. */
                if (s_handle != IntPtr.Zero)
                    return s_handle;
                s_handle = Load(name, assembly, searchPath, repoRoot);
                return s_handle;
            });
        }

        static IntPtr Load(string name, Assembly assembly, DllImportSearchPath? searchPath, string? repoRoot)
        {
            IntPtr handle;
            string? explicitPath = Environment.GetEnvironmentVariable("AUTOREMESHER_CORE_LIBRARY");
            if (!string.IsNullOrEmpty(explicitPath)) {
                /* Set, so no fallback: a wrong path is an error, not a
                 * silent switch to some other build of the library. */
                handle = NativeLibrary.Load(explicitPath);
                LoadedFrom = Path.GetFullPath(explicitPath);
                return handle;
            }
            if (NativeLibrary.TryLoad(name, assembly, searchPath, out handle)) {
                LoadedFrom = MappedPath() ?? "(the runtime's default search for " + FileName + ")";
                return handle;
            }
            if (repoRoot != null) {
                string path = Path.Combine(repoRoot, "build", FileName);
                if (File.Exists(path) && NativeLibrary.TryLoad(path, out handle)) {
                    LoadedFrom = path;
                    return handle;
                }
            }
            return IntPtr.Zero; /* DllNotFoundException at the first call */
        }

        public static string FileName =>
            OperatingSystem.IsWindows() ? Library + ".dll"
            : OperatingSystem.IsMacOS() ? "lib" + Library + ".dylib"
            : "lib" + Library + ".so";

        /* On Linux, the file the loader mapped for the library, from
         * /proc/self/maps; null elsewhere or if it is not found. */
        static string? MappedPath()
        {
            if (!OperatingSystem.IsLinux())
                return null;
            try {
                foreach (string line in File.ReadLines("/proc/self/maps")) {
                    int slash = line.IndexOf('/');
                    if (slash >= 0 && line.EndsWith("/" + FileName, StringComparison.Ordinal))
                        return line.Substring(slash);
                }
            } catch (IOException) {
            }
            return null;
        }

        static bool s_resolverInstalled;
        static IntPtr s_handle;
    }
}
