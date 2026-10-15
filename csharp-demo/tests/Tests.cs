/*
 *  Tests of the C# demo: the P/Invoke bindings against the real library
 *  (struct layout, every function, exact arrays across the boundary both
 *  ways), the OBJ reader and writer against autoremesher-cli's formats, and
 *  the demo's command line end to end on a small synthetic sphere.
 *
 *  Usage: AutoRemesherDemo.Tests <scratch directory> <repository root>
 *         AutoRemesherDemo.Tests --source <scratch directory> <source.obj>
 *
 *  The second form runs the demo with its defaults (M4.3's winner) on the
 *  real input and checks the output's counts.  It exits 77, which CTest
 *  reports as skipped, if the file is missing: source.obj is not in git.
 *
 *  The library is found as the demo finds it; CTest sets
 *  AUTOREMESHER_CORE_LIBRARY to the one it built.
 */
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;

namespace AutoRemesherDemo.Tests
{
    public static class Tests
    {
        static int s_failures;
        static string s_scratch = "";

        static void Check(bool condition, string what = "",
            [CallerFilePath] string file = "", [CallerLineNumber] int line = 0)
        {
            if (!condition) {
                Console.Error.WriteLine(Path.GetFileName(file) + ":" + line + ": CHECK failed" + (what.Length > 0 ? ": " + what : ""));
                ++s_failures;
            }
        }

        public static int Main(string[] args)
        {
            if (args.Length == 3 && args[0] == "--source")
                return SourceObj(args[1], args[2]);
            if (args.Length != 2) {
                Console.Error.WriteLine("usage: AutoRemesherDemo.Tests <scratch directory> <repository root>\n"
                    + "       AutoRemesherDemo.Tests --source <scratch directory> <source.obj>");
                return 2;
            }
            s_scratch = args[0];
            Directory.CreateDirectory(s_scratch);
            Native.InstallResolver(args[1]);

            StructLayout();
            Weld();
            Create();
            SplitIntoQuads();
            RemeshSphere();
            EverySetterTakesEffect();
            RemeshFailure();
            FormatNumbers();
            WriteObj(args[1]);
            ReadObj();
            Options();
            DemoEndToEnd();

            Console.WriteLine("library: " + Native.LoadedFrom);
            /* The library CTest names is the one in use, not another build. */
            string? explicitPath = Environment.GetEnvironmentVariable("AUTOREMESHER_CORE_LIBRARY");
            if (!string.IsNullOrEmpty(explicitPath))
                Check(Native.LoadedFrom == Path.GetFullPath(explicitPath), "loaded from $AUTOREMESHER_CORE_LIBRARY");
            if (s_failures > 0) {
                Console.Error.WriteLine(s_failures + " check(s) failed");
                return 1;
            }
            Console.WriteLine("all checks passed");
            return 0;
        }

        /* The structs must match the C header field for field: 7 and 4
         * pointer-sized fields, in order, no padding. */
        static void StructLayout()
        {
            int p = IntPtr.Size;
            Check(Marshal.SizeOf<CoreResult>() == 7 * p, "sizeof(autoremesher_core_result)");
            string[] resultFields = { "vertices", "vertex_count", "face_indices", "index_count", "face_sizes",
                "face_count", "quad_count" };
            for (int i = 0; i < resultFields.Length; ++i)
                Check(Marshal.OffsetOf<CoreResult>(resultFields[i]) == (IntPtr)(i * p), "offset of " + resultFields[i]);
            Check(Marshal.SizeOf<CoreWeldResult>() == 4 * p, "sizeof(autoremesher_core_weld_result)");
            string[] weldFields = { "vertices", "vertex_count", "triangle_indices", "triangle_count" };
            for (int i = 0; i < weldFields.Length; ++i)
                Check(Marshal.OffsetOf<CoreWeldResult>(weldFields[i]) == (IntPtr)(i * p), "offset of " + weldFields[i]);
            Check(RuntimeHelpers.IsReferenceOrContainsReferences<CoreResult>() == false, "CoreResult is blittable");
            Check(RuntimeHelpers.IsReferenceOrContainsReferences<CoreWeldResult>() == false, "CoreWeldResult is blittable");
        }

        static bool IsZeroed(CoreWeldResult r) =>
            r.vertices == IntPtr.Zero && r.vertex_count == 0 && r.triangle_indices == IntPtr.Zero && r.triangle_count == 0;

        static bool IsZeroed(CoreResult r) =>
            r.vertices == IntPtr.Zero && r.vertex_count == 0 && r.face_indices == IntPtr.Zero && r.index_count == 0
            && r.face_sizes == IntPtr.Zero && r.face_count == 0 && r.quad_count == 0;

        /* Two triangles sharing an edge, as a soup of six vertices: welded to
         * four, in order of first use, with the triangles remapped. */
        static void Weld()
        {
            double[] soup = { 0, 0, 0, 1, 0, 0, 0, 1, 0, /**/ 1, 0, 0, 1, 1, 0, 0, 1, 0 };
            uint[] triangles = { 0, 1, 2, 3, 4, 5 };
            Check(Native.autoremesher_core_weld(soup, 6, triangles, 2, out CoreWeldResult welded) == 1, "weld");
            Check(welded.vertex_count == 4 && welded.triangle_count == 2, "welded counts");
            Remesher.ToManaged(welded, out double[] v, out uint[] t);
            Check(v.SequenceEqual(new double[] { 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 }), "welded vertices");
            Check(t.SequenceEqual(new uint[] { 0, 1, 2, 1, 3, 2 }), "welded triangles");
            Native.autoremesher_core_free_weld_result(ref welded);
            Check(IsZeroed(welded), "freed weld result is zeroed");
            Native.autoremesher_core_free_weld_result(ref welded); /* harmless twice */

            /* Refused, with the result zeroed: an index out of range, a NaN,
             * a NULL array with a non-zero count. */
            Check(Native.autoremesher_core_weld(soup, 6, new uint[] { 0, 1, 6 }, 1, out welded) == 0 && IsZeroed(welded),
                "out-of-range index refused");
            double[] nan = (double[])soup.Clone();
            nan[4] = double.NaN;
            Check(Native.autoremesher_core_weld(nan, 6, triangles, 2, out welded) == 0 && IsZeroed(welded), "NaN refused");
            Check(Native.autoremesher_core_weld(null, 6, triangles, 2, out welded) == 0 && IsZeroed(welded), "NULL refused");
            Check(Native.autoremesher_core_weld(null, 0, null, 0, out welded) == 1 && welded.vertex_count == 0,
                "empty input welds to an empty mesh");
            Native.autoremesher_core_free_weld_result(ref welded);
        }

        static void Create()
        {
            double[] v = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
            Check(Native.autoremesher_core_create(v, 3, new uint[] { 0, 1, 3 }, 1) == IntPtr.Zero, "bad index refused");
            Check(Native.autoremesher_core_create(null, 3, new uint[] { 0, 1, 2 }, 1) == IntPtr.Zero, "NULL vertices refused");
            Native.autoremesher_core_destroy(IntPtr.Zero); /* accepted and ignored */
            Native.autoremesher_core_free_result(ref Unsafe.NullRef<CoreResult>());

            /* Empty input is accepted but does not remesh; no result after. */
            IntPtr handle = Native.autoremesher_core_create((double[]?)null, 0, null, 0);
            Check(handle != IntPtr.Zero, "empty input accepted");
            Native.autoremesher_core_set_target_quad_count(handle, 100);
            Check(Native.autoremesher_core_remesh(handle) == 0, "empty input does not remesh");
            Check(Native.autoremesher_core_get_result(handle, out CoreResult result) == 0 && IsZeroed(result), "no result");
            Native.autoremesher_core_destroy(handle);
        }

        /* One quad, from managed arrays pinned into a CoreResult, split into
         * four: the exact arrays of test_split.c's "quad" case come back. */
        static void SplitIntoQuads()
        {
            double[] vertices = { 0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0 };
            uint[] indices = { 0, 1, 2, 3 };
            uint[] sizes = { 4 };
            GCHandle pv = GCHandle.Alloc(vertices, GCHandleType.Pinned);
            GCHandle pi = GCHandle.Alloc(indices, GCHandleType.Pinned);
            GCHandle ps = GCHandle.Alloc(sizes, GCHandleType.Pinned);
            try {
                var mesh = new CoreResult {
                    vertices = pv.AddrOfPinnedObject(), vertex_count = 4,
                    face_indices = pi.AddrOfPinnedObject(), index_count = 4,
                    face_sizes = ps.AddrOfPinnedObject(), face_count = 1, quad_count = 1
                };
                Check(Native.autoremesher_core_split_into_quads(in mesh, out CoreResult quads) == 1, "split");
                PolygonMesh m = Remesher.ToManaged(quads);
                Check(m.Vertices.SequenceEqual(new double[] {
                    0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0, /* the input's */
                    1, 0, 0, 2, 1, 0, 1, 2, 0, 0, 1, 0, /* edge midpoints */
                    1, 1, 0 }), "split vertices"); /* the centroid */
                Check(m.FaceIndices.SequenceEqual(new uint[] { 0, 4, 8, 7, 1, 5, 8, 4, 2, 6, 8, 5, 3, 7, 8, 6 }),
                    "split faces");
                Check(m.FaceSizes.SequenceEqual(new uint[] { 4, 4, 4, 4 }) && m.QuadCount == 4, "split sizes");
                Native.autoremesher_core_free_result(ref quads);
                Check(IsZeroed(quads), "freed result is zeroed");
                Native.autoremesher_core_free_result(ref quads); /* harmless twice */

                /* Sizes that do not sum to index_count are refused. */
                mesh.index_count = 3;
                Check(Native.autoremesher_core_split_into_quads(in mesh, out quads) == 0 && IsZeroed(quads),
                    "inconsistent sizes refused");
            } finally {
                pv.Free();
                pi.Free();
                ps.Free();
            }
        }

        /* A closed UV sphere of radius 1, as test_core_api.c builds it, as a
         * triangle soup (each triangle with its own vertices). */
        static void Sphere(out double[] soup, out uint[] triangles)
        {
            const int segments = 32, rings = 16;
            var v = new List<double> { 0, 1, 0 };
            for (int ring = 1; ring < rings; ++ring) {
                double theta = Math.PI * ring / rings;
                for (int segment = 0; segment < segments; ++segment) {
                    double phi = 2.0 * Math.PI * segment / segments;
                    v.AddRange(new[] { Math.Sin(theta) * Math.Cos(phi), Math.Cos(theta), Math.Sin(theta) * Math.Sin(phi) });
                }
            }
            v.AddRange(new double[] { 0, -1, 0 });
            uint north = 0, south = (uint)(v.Count / 3 - 1);
            uint R(int r, int s) => (uint)(1 + (r - 1) * segments + (s % segments));
            var t = new List<uint>();
            for (int s = 0; s < segments; ++s) {
                t.AddRange(new[] { north, R(1, s + 1), R(1, s) });
                t.AddRange(new[] { south, R(rings - 1, s), R(rings - 1, s + 1) });
            }
            for (int r = 1; r < rings - 1; ++r) {
                for (int s = 0; s < segments; ++s) {
                    t.AddRange(new[] { R(r, s), R(r, s + 1), R(r + 1, s) });
                    t.AddRange(new[] { R(r, s + 1), R(r + 1, s + 1), R(r + 1, s) });
                }
            }
            soup = new double[t.Count * 3];
            triangles = new uint[t.Count];
            for (int i = 0; i < t.Count; ++i) {
                Array.Copy(v.ToArray(), (int)t[i] * 3, soup, i * 3, 3);
                triangles[i] = (uint)i;
            }
        }

        static void CheckMesh(PolygonMesh m, string name)
        {
            Check(m.VertexCount > 0 && m.FaceCount > 0 && m.QuadCount > 0, name + ": non-empty");
            Check(m.FaceSizes.All(s => s >= 3), name + ": faces of 3 or more corners");
            Check(m.FaceSizes.Sum(s => (long)s) == m.FaceIndices.Length, name + ": sizes sum to the index count");
            Check(m.FaceSizes.Count(s => s == 4) == m.QuadCount, name + ": quad_count");
            Check(m.FaceIndices.All(i => i < m.VertexCount), name + ": indices in range");
            for (long i = 0; i < m.VertexCount; ++i) {
                double r = Math.Sqrt(m.Vertices[i * 3] * m.Vertices[i * 3] + m.Vertices[i * 3 + 1] * m.Vertices[i * 3 + 1]
                    + m.Vertices[i * 3 + 2] * m.Vertices[i * 3 + 2]);
                if (!(r > 0.9 && r < 1.1)) {
                    Check(false, name + ": vertex " + i + " at radius " + r);
                    break;
                }
            }
        }

        /* Remesher.Run against the same calls made by hand: the same arrays,
         * so the wrapper adds nothing and loses nothing.  Then the progress
         * calls, and the split. */
        static void RemeshSphere()
        {
            Sphere(out double[] soup, out uint[] soupTriangles);
            var settings = new RemeshSettings { TargetQuads = 300, EdgeScaling = 1, Adaptivity = 1 };

            var calls = new List<(float progress, string status)>();
            RemeshRun run = Remesher.Run(soup, soupTriangles, settings, (p, s) => calls.Add((p, s)));
            Console.WriteLine("sphere: " + run.InputVertexCount + " soup vertices, welded to " + run.WeldedVertexCount
                + " -> " + run.Mesh.VertexCount + " vertices, " + run.Mesh.FaceCount + " faces, " + run.Mesh.QuadCount + " quads");
            Check(run.InputVertexCount == 2880 && run.InputTriangleCount == 960, "sphere input counts");
            Check(run.WeldedVertexCount == 482 && run.WeldedTriangleCount == 960, "sphere welded counts");
            CheckMesh(run.Mesh, "sphere");
            Check(run.RemeshedFaceCount == run.Mesh.FaceCount && run.RemeshedQuadCount == run.Mesh.QuadCount, "no split");
            Check(calls.Count > 1, "progress calls");
            Check(calls.All(c => c.progress >= 0 && c.progress <= 1), "progress in 0..1");
            Check(calls.Count > 0 && calls[^1].progress == 1.0f && calls[^1].status == "Done", "last progress call is 1, \"Done\"");

            /* By hand, without the wrapper. */
            Check(Native.autoremesher_core_weld(soup, (nuint)(soup.Length / 3), soupTriangles,
                (nuint)(soupTriangles.Length / 3), out CoreWeldResult welded) == 1, "weld by hand");
            IntPtr handle = Native.autoremesher_core_create(welded.vertices, welded.vertex_count,
                welded.triangle_indices, welded.triangle_count);
            Native.autoremesher_core_free_weld_result(ref welded);
            Check(handle != IntPtr.Zero, "create by hand");
            Native.autoremesher_core_set_target_quad_count(handle, 300);
            Native.autoremesher_core_set_scaling(handle, 1);
            Native.autoremesher_core_set_sharp_edge_degrees(handle, 90);
            Native.autoremesher_core_set_smooth_normal_degrees(handle, 0);
            Native.autoremesher_core_set_adaptivity(handle, 1);
            Native.autoremesher_core_set_anisotropy(handle, 1);
            Check(Native.autoremesher_core_remesh(handle) == 1, "remesh by hand");
            Check(Native.autoremesher_core_get_result(handle, out CoreResult result) == 1, "result by hand");
            Native.autoremesher_core_destroy(handle);
            PolygonMesh byHand = Remesher.ToManaged(result); /* the result outlives the handle */
            Native.autoremesher_core_free_result(ref result);
            Check(byHand.Vertices.SequenceEqual(run.Mesh.Vertices), "same vertices by hand");
            Check(byHand.FaceIndices.SequenceEqual(run.Mesh.FaceIndices), "same faces by hand");
            Check(byHand.FaceSizes.SequenceEqual(run.Mesh.FaceSizes), "same sizes by hand");

            /* With the split: n quads for each n-gon, so as many quads as the
             * remeshed mesh had corners. */
            settings.SplitToQuads = true;
            RemeshRun split = Remesher.Run(soup, soupTriangles, settings);
            Check(split.RemeshedFaceCount == run.Mesh.FaceCount, "the split starts from the same remesh");
            Check(split.Mesh.FaceCount == run.Mesh.FaceIndices.Length && split.Mesh.QuadCount == split.Mesh.FaceCount,
                "split: all quads, one per corner");
            Check(split.Mesh.Vertices.Take(run.Mesh.Vertices.Length).SequenceEqual(run.Mesh.Vertices),
                "split keeps the remeshed vertices first");
        }

        /* A 2 x 1 x 1 box, each unit of each side a grid of n x n squares,
         * split into triangles and wound outwards, with its vertices shared. */
        static void Box(int n, out double[] vertices, out uint[] triangles)
        {
            var v = new List<double>();
            var t = new List<uint>();
            void Side(double[] origin, double[] u, double[] w, int nu, int nw)
            {
                uint first = (uint)(v.Count / 3);
                for (int j = 0; j <= nw; ++j)
                    for (int i = 0; i <= nu; ++i)
                        for (int k = 0; k < 3; ++k)
                            v.Add(origin[k] + u[k] * i / nu + w[k] * j / nw);
                for (int j = 0; j < nw; ++j) {
                    for (int i = 0; i < nu; ++i) {
                        uint a = first + (uint)(j * (nu + 1) + i), b = a + 1, c = a + (uint)(nu + 1), d = c + 1;
                        t.AddRange(new[] { a, b, d, a, d, c });
                    }
                }
            }
            double[] o = { 0, 0, 0 }, x = { 2, 0, 0 }, y = { 0, 1, 0 }, z = { 0, 0, 1 };
            Side(o, y, x, n, 2 * n);
            Side(z, x, y, 2 * n, n);
            Side(o, x, z, 2 * n, n);
            Side(y, z, x, n, 2 * n);
            Side(o, z, y, n, n);
            Side(x, y, z, n, n);
            vertices = v.ToArray();
            triangles = t.ToArray();
        }

        /* Each setting reaches the core: moving any one of them away from
         * M4.3's winner changes the remeshed box.  The core's own defaults
         * equal three of the winner's values (sharp edge 90, smooth normal 0,
         * anisotropy 1), so a lost setter would not show on source.obj. */
        static void EverySetterTakesEffect()
        {
            Box(8, out double[] vertices, out uint[] triangles);
            RemeshSettings Winner() => new RemeshSettings { TargetQuads = 300 };
            PolygonMesh baseline = Remesher.Run(vertices, triangles, Winner()).Mesh;
            Console.WriteLine("box: " + vertices.Length / 3 + " vertices -> " + baseline.VertexCount + " vertices, "
                + baseline.FaceCount + " faces at M4.3's winner and 300 target quads");
            var moves = new (string name, Action<RemeshSettings> move)[] {
                ("target quads 500", s => s.TargetQuads = 500),
                ("edge scaling 1.5", s => s.EdgeScaling = 1.5),
                ("sharp edge 60", s => s.SharpEdgeDegrees = 60),
                ("smooth normal 30", s => s.SmoothNormalDegrees = 30),
                ("adaptivity 1", s => s.Adaptivity = 1),
                ("anisotropy 0.5", s => s.Anisotropy = 0.5),
            };
            foreach (var (name, move) in moves) {
                RemeshSettings settings = Winner();
                move(settings);
                PolygonMesh moved = Remesher.Run(vertices, triangles, settings).Mesh;
                Check(!moved.Vertices.SequenceEqual(baseline.Vertices), name + " changes the output");
            }
            /* And the same settings give the same output. */
            Check(Remesher.Run(vertices, triangles, Winner()).Mesh.Vertices.SequenceEqual(baseline.Vertices), "deterministic");
        }

        /* The core's reason for a failure arrives through the progress
         * handler, and the wrapper reports it. */
        static void RemeshFailure()
        {
            Sphere(out double[] soup, out uint[] soupTriangles);
            try {
                Remesher.Run(soup, soupTriangles, new RemeshSettings { TargetQuads = 0 });
                Check(false, "target 0 should fail");
            } catch (RemeshException e) {
                Check(e.Message.Contains("target triangle count"), "failure reason: " + e.Message);
            }
            /* An exception in the progress callback comes back out of Run,
             * rather than unwinding through the library. */
            try {
                Remesher.Run(soup, soupTriangles, new RemeshSettings { TargetQuads = 300 },
                    (p, s) => throw new InvalidOperationException("from the callback"));
                Check(false, "the callback's exception should come out");
            } catch (InvalidOperationException e) {
                Check(e.Message == "from the callback", "callback exception: " + e.Message);
            }
            try {
                Remesher.Run(soup, new uint[] { 0, 1, 99999 }, new RemeshSettings());
                Check(false, "a bad index should fail");
            } catch (RemeshException e) {
                Check(e.Message.StartsWith("cannot weld"), "weld failure: " + e.Message);
            }
        }

        /* test_obj_io.cpp's numbers, and what printf's %g gives for others. */
        static void FormatNumbers()
        {
            var cases = new (double value, string text)[] {
                (105.67, "105.67"), (-0.60052, "-0.60052"), (44.038, "44.038"),
                (0.1 + 0.2, "0.3"), (1.0 / 3.0, "0.333333"), (-2.0 / 3.0, "-0.666667"),
                (1234567.0, "1.23457e+06"), (0.00001, "1e-05"), (-0.0, "0"),
                (0, "0"), (100000, "100000"), (1e-300, "1e-300"),
                (-67.62912345, "-67.6291"), (118.88, "118.88"), (2, "2"),
                /* exact ties round away from zero (QTextStream), not to even */
                (123456.5, "123457"), (-12345.25, "-12345.3"), (1234575, "1.23458e+06"),
                (0.0001, "0.0001"), (0.00012345678, "0.000123457"), (999999.5, "1e+06"),
                (999999.4, "999999"), (-1e100, "-1e+100"), (1.5, "1.5"), (-26.0804, "-26.0804"),
                (double.MaxValue, "1.79769e+308"), (double.Epsilon, "4.94066e-324"),
            };
            foreach (var (value, text) in cases) {
                string got = ObjIo.FormatQtDouble(value);
                Check(got == text, "FormatQtDouble(" + value.ToString("R") + ") = " + got + ", expected " + text);
            }
        }

        /* test_obj_io.cpp's writer case, byte for byte. */
        static void WriteObj(string repoRoot)
        {
            string pro = File.ReadAllText(Path.Combine(repoRoot, "autoremesher.pro"));
            Check(Regex.Match(pro, "QMAKE_TARGET_PRODUCT = ([^\n]+)").Groups[1].Value.Trim() == ObjIo.AppName, "AppName");
            Check(Regex.Match(pro, "HUMAN_VERSION = \"([^\"]+)\"").Groups[1].Value == ObjIo.AppHumanVersion, "AppHumanVersion");
            Check(Regex.Match(pro, "HOMEPAGE_URL = \"([^\"]+)\"").Groups[1].Value == ObjIo.AppHomepageUrl, "AppHomepageUrl");

            var mesh = new PolygonMesh {
                Vertices = new double[] { 105.67, -0.60052, 44.038, 0.1 + 0.2, 1.0 / 3.0, -2.0 / 3.0,
                    1234567.0, 0.00001, -0.0, 0, 100000, 1e-300, -67.62912345, 118.88, 2,
                    123456.5, -12345.25, 1234575 },
                FaceIndices = new uint[] { 0, 1, 2, 3, 3, 2, 4, 0, 1, 2, 3, 4 },
                FaceSizes = new uint[] { 4, 3, 5 },
                QuadCount = 1
            };
            string header = "# AutoRemesher 1.2.0\n# https://autoremesher.dust3d.org/\n";
            string expected = header
                + "v 105.67 -0.60052 44.038\n"
                + "v 0.3 0.333333 -0.666667\n"
                + "v 1.23457e+06 1e-05 0\n"
                + "v 0 100000 1e-300\n"
                + "v -67.6291 118.88 2\n"
                + "v 123457 -12345.3 1.23458e+06\n"
                + "f 1 2 3 4\n"
                + "f 4 3 5\n"
                + "f 1 2 3 4 5\n";
            string path = Path.Combine(s_scratch, "written.obj");
            ObjIo.Write(path, mesh);
            Check(File.ReadAllBytes(path).SequenceEqual(System.Text.Encoding.ASCII.GetBytes(expected)), "written bytes");

            /* Read back: the positions to six digits, each polygon as a fan. */
            ObjIo.Read(path, out double[] v, out uint[] t);
            Check(v.Length == 18 && v[12] == -67.6291, "read back vertices");
            Check(t.SequenceEqual(new uint[] { 0, 1, 2, 0, 2, 3, 3, 2, 4, 0, 1, 2, 0, 2, 3, 0, 3, 4 }), "read back fans");

            ObjIo.Write(path, new PolygonMesh());
            Check(File.ReadAllText(path) == header, "an empty mesh is the header only");
        }

        static string Write(string name, string text)
        {
            string path = Path.Combine(s_scratch, name);
            File.WriteAllText(path, text);
            return path;
        }

        static void ReadObj()
        {
            /* Corners with texture and normal indices, negative indices, a
             * face before a vertex it names, tabs, CRLF, other lines ignored. */
            string path = Write("forms.obj", "# comment\r\nvn 0 0 1\nv 0 0 0\nv\t1 0 0 1.0\nf 1/1/1 2//1 4\nvt 0 0\n"
                + "v 0 1 0\r\nv 1 1 0\nf -4 -3 -2 -1\ng group\n");
            ObjIo.Read(path, out double[] v, out uint[] t);
            Check(v.SequenceEqual(new double[] { 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0 }), "vertices of the forms file");
            Check(t.SequenceEqual(new uint[] { 0, 1, 3, 0, 1, 2, 0, 2, 3 }), "triangles of the forms file");

            var bad = new (string text, string message)[] {
                ("v 0 0\n", ":1: a vertex needs three coordinates"),
                ("v 0 0 x\n", ":1: \"x\" is not a finite number"),
                ("v 0 0 inf\n", ":1: \"inf\" is not a finite number"),
                ("v 0 0 0\nf 1 2\n", ":2: a face needs at least three vertices"),
                ("v 0 0 0\nf 1 1 a\n", ":2: \"a\" is not a vertex index"),
                ("v 0 0 0\nf 1 0 1\n", ":2: \"0\" is not a vertex index"),
                ("v 0 0 0\nf 1 -2 1\n", ":2: \"-2\" is before the first vertex"),
                ("v 0 0 0\nf 1 2 3\nv 1 0 0\n", ":2: vertex 3 does not exist; the file has 2"),
                ("v 0 0 0\n", ": no faces"),
            };
            for (int i = 0; i < bad.Length; ++i) {
                string p = Write("bad" + i + ".obj", bad[i].text);
                try {
                    ObjIo.Read(p, out _, out _);
                    Check(false, "bad" + i + " read without an error");
                } catch (ObjException e) {
                    Check(e.Message == p + bad[i].message, "bad" + i + ": " + e.Message);
                }
            }
            try {
                ObjIo.Read(Path.Combine(s_scratch, "missing.obj"), out _, out _);
                Check(false, "a missing file read");
            } catch (ObjException e) {
                Check(e.Message.StartsWith("cannot open "), "missing: " + e.Message);
            }
        }

        static void Options()
        {
            /* The defaults are M4.3's winner, tq30000_es1.25_se90_sn0_ad0.5_an1. */
            DemoOptions o = Demo.Parse(Array.Empty<string>());
            RemeshSettings s = o.Settings;
            Check(s.TargetQuads == 30000 && s.EdgeScaling == 1.25 && s.SharpEdgeDegrees == 90
                && s.SmoothNormalDegrees == 0 && s.Adaptivity == 0.5 && s.Anisotropy == 1 && !s.SplitToQuads,
                "defaults are M4.3's winner");
            Check(o.InputPath == null && o.OutputPath == "remeshed_csharp.obj", "default paths");

            o = Demo.Parse(new[] { "-i", "a.obj", "--output=b.obj", "--target-quads", "11054", "--edge-scaling=2",
                "--sharp-edge", "45", "--smooth-normal", "30", "--adaptivity", "0", "--anisotropy=0.25", "--split-to-quads" });
            s = o.Settings;
            Check(o.InputPath == "a.obj" && o.OutputPath == "b.obj", "paths");
            Check(s.TargetQuads == 11054 && s.EdgeScaling == 2 && s.SharpEdgeDegrees == 45 && s.SmoothNormalDegrees == 30
                && s.Adaptivity == 0 && s.Anisotropy == 0.25 && s.SplitToQuads, "values");
            Check(Demo.Parse(new[] { "--target-quads", "x", "--help" }).Help, "--help wins");

            string[][] usageErrors = {
                new[] { "--target-quads", "0" }, new[] { "--target-quads", "-5" }, new[] { "--target-quads", "1e3" },
                new[] { "--edge-scaling", "0.5" }, new[] { "--sharp-edge", "20" }, new[] { "--adaptivity", "1.5" },
                new[] { "--anisotropy", "nan" }, new[] { "--smooth-normal", " 1" }, new[] { "--output" },
                new[] { "--split-to-quads=1" }, new[] { "--bogus" }, new[] { "stray" },
            };
            foreach (string[] args in usageErrors) {
                var stdout = new StringWriter();
                var stderr = new StringWriter();
                Check(Demo.Run(args, stdout, stderr) == 2, "exit 2 for " + string.Join(" ", args));
                Check(stderr.ToString().Contains("Try 'AutoRemesherDemo --help'"), "usage hint for " + string.Join(" ", args));
            }
            var help = new StringWriter();
            Check(Demo.Run(new[] { "--help" }, help, new StringWriter()) == 0 && help.ToString().StartsWith("Usage: "), "--help");
        }

        /* The sphere written as an OBJ soup, through the demo's command line. */
        static void DemoEndToEnd()
        {
            Sphere(out double[] soup, out uint[] triangles);
            var text = new System.Text.StringBuilder();
            for (int i = 0; i < soup.Length; i += 3)
                text.Append("v ").Append(soup[i].ToString("R")).Append(' ').Append(soup[i + 1].ToString("R"))
                    .Append(' ').Append(soup[i + 2].ToString("R")).Append('\n');
            for (int i = 0; i < triangles.Length; i += 3)
                text.Append("f ").Append(triangles[i] + 1).Append(' ').Append(triangles[i + 1] + 1).Append(' ')
                    .Append(triangles[i + 2] + 1).Append('\n');
            string input = Write("sphere.obj", text.ToString());
            string output = Path.Combine(s_scratch, "sphere_out.obj");
            File.Delete(output);

            var stdout = new StringWriter();
            var stderr = new StringWriter();
            int status = Demo.Run(new[] { "-i", input, "-o", output, "--target-quads", "300", "--edge-scaling", "1",
                "--adaptivity", "1" }, stdout, stderr);
            Console.Write(stdout.ToString());
            Check(status == 0, "demo exit status " + status + ": " + stderr);
            Check(stdout.ToString().Contains("status 100%\n"), "status 100%");
            Check(File.Exists(output), "output written");
            if (!File.Exists(output))
                return;
            ObjIo.Read(output, out double[] v, out _);
            Match m = Regex.Match(stdout.ToString(), @"output: .* \((\d+) vertices, (\d+) faces: (\d+) quads, (\d+) non-quads");
            Check(m.Success && long.Parse(m.Groups[1].Value) == v.Length / 3, "summary vertex count matches the file");
            Check(File.ReadAllLines(output).Count(l => l.StartsWith("f ")) == (m.Success ? int.Parse(m.Groups[2].Value) : -1),
                "summary face count matches the file");

            /* Failures exit 1, as the CLI's. */
            Check(Demo.Run(new[] { "-i", Path.Combine(s_scratch, "missing.obj"), "-o", output }, new StringWriter(),
                stderr = new StringWriter()) == 1 && stderr.ToString().Contains("cannot open"), "missing input exits 1");
            Check(Demo.Run(new[] { "-i", input, "-o", Path.Combine(s_scratch, "no", "such", "dir", "out.obj"),
                "--target-quads", "300" }, new StringWriter(), new StringWriter()) == 1, "unwritable output exits 1");
        }

        /* M4.3's winner from source.obj, by the demo with its defaults:
         * 11,050 vertices and 11,119 faces, 11,065 of them quads
         * (HANDOFF_M4_TASK3.md, "The winning parameter set"). */
        static int SourceObj(string scratch, string sourceObj)
        {
            if (!File.Exists(sourceObj)) {
                Console.WriteLine(sourceObj + " not found; skipping");
                return 77;
            }
            Directory.CreateDirectory(scratch);
            string output = Path.Combine(scratch, "remeshed_csharp.obj");
            File.Delete(output);
            var stdout = new StringWriter();
            int status = Demo.Run(new[] { "-i", sourceObj, "-o", output }, stdout, Console.Error);
            Console.Write(stdout.ToString());
            Check(status == 0, "exit status " + status);
            Check(stdout.ToString().Contains("(97400 vertices, 175566 triangles; welded to 87691 vertices)"), "input counts");
            Check(stdout.ToString().Contains("--target-quads 30000 --edge-scaling 1.25 --sharp-edge 90.0 --smooth-normal 0.0 "
                + "--adaptivity 0.5 --anisotropy 1.0\n"), "M4.3's parameters");
            if (File.Exists(output)) {
                ObjIo.Read(output, out double[] v, out _);
                string[] faces = File.ReadAllLines(output).Where(l => l.StartsWith("f ")).ToArray();
                int quads = faces.Count(l => l.Split(' ').Length == 5);
                Console.WriteLine("read back: " + v.Length / 3 + " vertices, " + faces.Length + " faces, " + quads + " quads");
                Check(v.Length / 3 == 11050, "11050 vertices");
                Check(faces.Length == 11119, "11119 faces");
                Check(quads == 11065, "11065 quads");
            } else {
                Check(false, "output written");
            }
            if (s_failures > 0) {
                Console.Error.WriteLine(s_failures + " check(s) failed");
                return 1;
            }
            return 0;
        }
    }
}
