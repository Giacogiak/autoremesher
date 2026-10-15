/*
 *  The pipeline autoremesher-cli runs (src/cli/main.cpp), in-process through
 *  the P/Invoke bindings: weld, create, set every parameter, remesh, get the
 *  result, destroy, and optionally split the result into quads.  Every native
 *  allocation is released here; the caller only sees managed arrays.
 */
using System;
using System.Runtime.InteropServices;

namespace AutoRemesherDemo
{
    public sealed class RemeshSettings
    {
        /* M4.3's winner, tq30000_es1.25_se90_sn0_ad0.5_an1 (HANDOFF_M4_TASK3.md),
         * visually confirmed by Giacomo Forcina on 2026-10-15. */
        public ulong TargetQuads = 30000;
        public double EdgeScaling = 1.25;
        public double SharpEdgeDegrees = 90;
        public double SmoothNormalDegrees = 0;
        public double Adaptivity = 0.5;
        public double Anisotropy = 1;
        public bool SplitToQuads; /* off, as in the CLI since decision D6 was reverted */
    }

    public class RemeshException : Exception
    {
        public RemeshException(string message) : base(message) { }
    }

    public sealed class RemeshRun
    {
        public long InputVertexCount, InputTriangleCount;
        public long WeldedVertexCount, WeldedTriangleCount;
        public long RemeshedFaceCount, RemeshedQuadCount; /* before any split */
        public PolygonMesh Mesh = new PolygonMesh();
    }

    public static class Remesher
    {
        /* `progress`, if given, receives the library's progress calls (0..1 and
         * a status), from the remeshing thread and from its workers, one at a
         * time.  Throws RemeshException where the CLI would exit 1. */
        public static RemeshRun Run(double[] vertices, uint[] triangles, RemeshSettings settings,
            Action<float, string>? progress = null)
        {
            var run = new RemeshRun {
                InputVertexCount = vertices.Length / 3,
                InputTriangleCount = triangles.Length / 3
            };

            /* Always welded, as in the CLI (decision D2). */
            if (Native.autoremesher_core_weld(vertices, (nuint)(vertices.Length / 3), triangles,
                    (nuint)(triangles.Length / 3), out CoreWeldResult welded) == 0)
                throw new RemeshException("cannot weld the input (a coordinate beyond about 9.2e13, or out of memory?)");
            IntPtr handle;
            try {
                run.WeldedVertexCount = (long)welded.vertex_count;
                run.WeldedTriangleCount = (long)welded.triangle_count;
                handle = Native.autoremesher_core_create(welded.vertices, welded.vertex_count,
                    welded.triangle_indices, welded.triangle_count);
            } finally {
                Native.autoremesher_core_free_weld_result(ref welded);
            }
            if (handle == IntPtr.Zero)
                throw new RemeshException("cannot create the remesher");

            CoreResult result;
            string lastStatus = "";
            Exception? callbackError = null;
            /* Kept in a local that outlives the remesh call (GC.KeepAlive
             * below): the library holds only the function pointer.  An
             * exception must not unwind through the library's frames, so the
             * first one is kept and rethrown once the remesh returns. */
            CoreProgressHandler handler = (tag, value, status) => {
                string text = Marshal.PtrToStringUTF8(status) ?? "";
                lastStatus = text;
                if (callbackError != null)
                    return;
                try {
                    progress?.Invoke(value, text);
                } catch (Exception e) {
                    callbackError = e;
                }
            };
            try {
                Native.autoremesher_core_set_target_quad_count(handle, (nuint)settings.TargetQuads);
                Native.autoremesher_core_set_scaling(handle, settings.EdgeScaling);
                Native.autoremesher_core_set_sharp_edge_degrees(handle, settings.SharpEdgeDegrees);
                Native.autoremesher_core_set_smooth_normal_degrees(handle, settings.SmoothNormalDegrees);
                Native.autoremesher_core_set_adaptivity(handle, settings.Adaptivity);
                Native.autoremesher_core_set_anisotropy(handle, settings.Anisotropy);
                Native.autoremesher_core_set_progress_handler(handle, handler, IntPtr.Zero);
                int remeshed = Native.autoremesher_core_remesh(handle);
                if (callbackError != null)
                    System.Runtime.ExceptionServices.ExceptionDispatchInfo.Throw(callbackError);
                if (remeshed == 0 || Native.autoremesher_core_get_result(handle, out result) == 0)
                    throw new RemeshException("remeshing failed" + (lastStatus.Length == 0 ? "" : ": " + lastStatus));
            } finally {
                Native.autoremesher_core_destroy(handle);
                GC.KeepAlive(handler);
            }

            try {
                run.RemeshedFaceCount = (long)result.face_count;
                run.RemeshedQuadCount = (long)result.quad_count;
                if (settings.SplitToQuads) {
                    if (Native.autoremesher_core_split_into_quads(in result, out CoreResult quads) == 0)
                        throw new RemeshException("cannot split the remeshed faces into quads");
                    Native.autoremesher_core_free_result(ref result);
                    result = quads;
                }
                run.Mesh = ToManaged(result);
            } finally {
                Native.autoremesher_core_free_result(ref result);
            }
            return run;
        }

        /* Copies a library-owned result into managed arrays; does not free it. */
        public static PolygonMesh ToManaged(in CoreResult result)
        {
            var mesh = new PolygonMesh {
                Vertices = new double[checked((int)result.vertex_count * 3)],
                FaceIndices = new uint[checked((int)result.index_count)],
                FaceSizes = new uint[checked((int)result.face_count)],
                QuadCount = (long)result.quad_count
            };
            if (mesh.Vertices.Length > 0)
                Marshal.Copy(result.vertices, mesh.Vertices, 0, mesh.Vertices.Length);
            CopyUInt32(result.face_indices, mesh.FaceIndices);
            CopyUInt32(result.face_sizes, mesh.FaceSizes);
            return mesh;
        }

        /* Copies a library-owned weld result into managed arrays; does not free it. */
        public static void ToManaged(in CoreWeldResult welded, out double[] vertices, out uint[] triangles)
        {
            vertices = new double[checked((int)welded.vertex_count * 3)];
            triangles = new uint[checked((int)welded.triangle_count * 3)];
            if (vertices.Length > 0)
                Marshal.Copy(welded.vertices, vertices, 0, vertices.Length);
            CopyUInt32(welded.triangle_indices, triangles);
        }

        /* Marshal.Copy has no uint[] overload; int and uint have the same
         * layout, so copy through the int[] view of the same memory. */
        static void CopyUInt32(IntPtr source, uint[] destination)
        {
            if (destination.Length == 0)
                return;
            var asInt = new int[destination.Length];
            Marshal.Copy(source, asInt, 0, asInt.Length);
            Buffer.BlockCopy(asInt, 0, destination, 0, destination.Length * sizeof(uint));
        }
    }
}
