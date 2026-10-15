/*
 *  OBJ reading and writing for the demo, as autoremesher-cli does it
 *  (src/cli/obj_io.cpp), so that the two callers of the library feed it the
 *  same arrays and write their results the same way, byte for byte.
 */
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace AutoRemesherDemo
{
    /* A polygon mesh as autoremesher_core_result holds it, in managed arrays. */
    public sealed class PolygonMesh
    {
        public double[] Vertices = Array.Empty<double>(); /* x, y, z of each vertex */
        public uint[] FaceIndices = Array.Empty<uint>(); /* 0-based, face after face */
        public uint[] FaceSizes = Array.Empty<uint>(); /* corners of each face */
        public long QuadCount; /* as the library counted them */

        public long VertexCount => Vertices.Length / 3;
        public long FaceCount => FaceSizes.Length;
    }

    public class ObjException : Exception
    {
        public ObjException(string message) : base(message) { }
    }

    public static class ObjIo
    {
        /* autoremesher-cli's header lines: autoremesher.pro's QMAKE_TARGET_PRODUCT,
         * HUMAN_VERSION and HOMEPAGE_URL, which CMake passes to the CLI.  The
         * tests check these against autoremesher.pro. */
        public const string AppName = "AutoRemesher";
        public const string AppHumanVersion = "1.2.0";
        public const string AppHomepageUrl = "https://autoremesher.dust3d.org/";

        /* Reads the `v` and `f` lines of an OBJ file; every other line is
         * ignored.  A vertex takes the first three numbers of its line; a face
         * the vertex index at the start of each corner (`7`, `7/1`, `7//3`,
         * negative indices counting back from the last vertex so far).  Each
         * polygon becomes a fan of triangles from its first corner.  Throws
         * ObjException, naming the file and line, where the CLI's reader fails. */
        public static void Read(string path, out double[] vertices, out uint[] triangles)
        {
            var vertexList = new List<double>();
            var triangleList = new List<uint>();
            long largestIndex = 0;
            long largestIndexLine = 0;
            long lineNumber = 0;
            var face = new List<uint>();
            Exception Fail(string message) => new ObjException(path + ":" + lineNumber + ": " + message);

            using (var reader = OpenReader(path)) {
                string? line;
                while ((line = reader.ReadLine()) != null) {
                    ++lineNumber;
                    var tokens = Tokenize(line);
                    if (tokens.Count == 0)
                        continue;
                    if (tokens[0] == "v") {
                        if (tokens.Count < 4)
                            throw Fail("a vertex needs three coordinates");
                        for (int i = 1; i <= 3; ++i) {
                            if (!double.TryParse(tokens[i], NumberStyles.Float, CultureInfo.InvariantCulture,
                                    out double coordinate)
                                || !double.IsFinite(coordinate))
                                throw Fail("\"" + tokens[i] + "\" is not a finite number");
                            vertexList.Add(coordinate);
                        }
                    } else if (tokens[0] == "f") {
                        long vertexCount = vertexList.Count / 3;
                        face.Clear();
                        for (int i = 1; i < tokens.Count; ++i) {
                            string token = tokens[i];
                            if (!ParseIndex(token, out long index))
                                throw Fail("\"" + token + "\" is not a vertex index");
                            if (index < 0) {
                                index += vertexCount;
                                if (index < 0)
                                    throw Fail("\"" + token + "\" is before the first vertex");
                            } else if (index == 0 || index > uint.MaxValue) {
                                throw Fail("\"" + token + "\" is not a vertex index");
                            } else {
                                if (index > largestIndex) {
                                    largestIndex = index;
                                    largestIndexLine = lineNumber;
                                }
                                --index;
                            }
                            face.Add((uint)index);
                        }
                        if (face.Count < 3)
                            throw Fail("a face needs at least three vertices");
                        for (int i = 1; i + 1 < face.Count; ++i) {
                            triangleList.Add(face[0]);
                            triangleList.Add(face[i]);
                            triangleList.Add(face[i + 1]);
                        }
                    }
                }
            }
            long totalVertices = vertexList.Count / 3;
            if (totalVertices > uint.MaxValue)
                throw new ObjException(path + ": more vertices than 32-bit indices can address");
            if (largestIndex > totalVertices) {
                lineNumber = largestIndexLine;
                throw Fail("vertex " + largestIndex + " does not exist; the file has " + totalVertices);
            }
            if (triangleList.Count == 0)
                throw new ObjException(path + ": no faces");
            vertices = vertexList.ToArray();
            triangles = triangleList.ToArray();
        }

        /* Writes `mesh` as the CLI writes a result: its two header lines, a
         * `v` line per vertex with each coordinate in FormatQtDouble's format,
         * and an `f` line per face with 1-based indices, all ending in "\n". */
        public static void Write(string path, PolygonMesh mesh)
        {
            var text = new StringBuilder();
            text.Append("# ").Append(AppName).Append(' ').Append(AppHumanVersion).Append('\n');
            text.Append("# ").Append(AppHomepageUrl).Append('\n');
            double[] v = mesh.Vertices;
            for (long i = 0; i + 2 < v.Length; i += 3) {
                text.Append("v ").Append(FormatQtDouble(v[i])).Append(' ').Append(FormatQtDouble(v[i + 1]))
                    .Append(' ').Append(FormatQtDouble(v[i + 2])).Append('\n');
            }
            long index = 0;
            foreach (uint size in mesh.FaceSizes) {
                text.Append('f');
                for (uint j = 0; j < size; ++j)
                    text.Append(' ').Append(1UL + mesh.FaceIndices[index++]);
                text.Append('\n');
            }
            File.WriteAllText(path, text.ToString(), new UTF8Encoding(false));
        }

        /* A double as QTextStream writes it by default (SmartNotation,
         * precision 6), which is what the GUI and the CLI write: printf's %g,
         * except that negative zero loses its sign and a value exactly halfway
         * between two six-digit results rounds away from zero (see
         * formatNumber in src/cli/obj_io.cpp). */
        public static string FormatQtDouble(double value)
        {
            if (value == 0.0)
                return "0";
            /* An exact tie is a value whose seventh significant digit is a 5
             * with nothing after it; nudged one step away from zero, it rounds
             * that way. */
            string seven = value.ToString("E6", CultureInfo.InvariantCulture);
            int exponentAt = seven.IndexOf('E');
            if (seven[exponentAt - 1] == '5' && double.Parse(seven, CultureInfo.InvariantCulture) == value)
                value = value > 0 ? Math.BitIncrement(value) : Math.BitDecrement(value);
            return FormatG6(value);
        }

        /* printf's "%g" for a finite, non-zero value: six significant digits,
         * in %e style if the decimal exponent is below -4 or at least 6, in %f
         * style otherwise, without trailing zeros or a trailing point. */
        static string FormatG6(double value)
        {
            /* "-d.dddddE+xxx", correctly rounded to six significant digits. */
            string e = value.ToString("E5", CultureInfo.InvariantCulture);
            bool negative = e[0] == '-';
            int exponentAt = e.IndexOf('E');
            int exponent = int.Parse(e.Substring(exponentAt + 1), CultureInfo.InvariantCulture);
            string digits = e.Substring(negative ? 1 : 0, exponentAt - (negative ? 1 : 0)).Replace(".", "");
            var text = new StringBuilder();
            if (negative)
                text.Append('-');
            if (exponent < -4 || exponent >= 6) {
                string fraction = digits.Substring(1).TrimEnd('0');
                text.Append(digits[0]);
                if (fraction.Length > 0)
                    text.Append('.').Append(fraction);
                text.Append(exponent < 0 ? "e-" : "e+").Append(Math.Abs(exponent).ToString("00", CultureInfo.InvariantCulture));
            } else if (exponent >= 0) {
                string fraction = digits.Substring(exponent + 1).TrimEnd('0');
                text.Append(digits, 0, exponent + 1);
                if (fraction.Length > 0)
                    text.Append('.').Append(fraction);
            } else {
                string fraction = (new string('0', -exponent - 1) + digits).TrimEnd('0');
                text.Append("0.").Append(fraction);
            }
            return text.ToString();
        }

        static StreamReader OpenReader(string path)
        {
            try {
                return new StreamReader(path, Encoding.Latin1, false);
            } catch (Exception e) when (e is IOException || e is UnauthorizedAccessException) {
                throw new ObjException("cannot open " + path);
            }
        }

        /* The CLI's whitespace: blank, tab, CR, VT and FF. */
        static List<string> Tokenize(string line)
        {
            var tokens = new List<string>();
            int i = 0;
            while (true) {
                while (i < line.Length && IsSpace(line[i]))
                    ++i;
                if (i >= line.Length)
                    return tokens;
                int begin = i;
                while (i < line.Length && !IsSpace(line[i]))
                    ++i;
                tokens.Add(line.Substring(begin, i - begin));
            }
        }

        static bool IsSpace(char c) => c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f';

        /* A whole number (optional sign, then digits) ended by the token's end
         * or by a '/'. */
        static bool ParseIndex(string token, out long value)
        {
            int end = token.IndexOf('/');
            string number = end < 0 ? token : token.Substring(0, end);
            return long.TryParse(number, NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out value);
        }
    }
}
