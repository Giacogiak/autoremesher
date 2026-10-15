/*
 *  The demo's command line: reads an OBJ, remeshes it in-process through
 *  Remesher (the C API over P/Invoke) and writes the result as an OBJ, the
 *  way autoremesher-cli writes it.  The options are the CLI's, with M4.3's
 *  winning parameters as their defaults and <repo>/source.obj as the input.
 *
 *  Exit status, as the CLI's: 0 on success, 1 if reading, remeshing or
 *  writing fails (or the library cannot be loaded), 2 on a usage error.
 */
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Security.Cryptography;

namespace AutoRemesherDemo
{
    public sealed class DemoOptions
    {
        public string? InputPath; /* null: <repo>/source.obj */
        public string OutputPath = "remeshed_csharp.obj";
        public RemeshSettings Settings = new RemeshSettings();
        public bool Help;
    }

    public static class Demo
    {
        public const string ProgramName = "AutoRemesherDemo";

        public static int Run(string[] args, TextWriter stdout, TextWriter stderr)
        {
            string? repoRoot = FindRepoRoot();
            Native.InstallResolver(repoRoot);

            DemoOptions options;
            try {
                options = Parse(args);
            } catch (ArgumentException e) {
                stderr.WriteLine(ProgramName + ": " + e.Message + "\nTry '" + ProgramName + " --help'.");
                return 2;
            }
            if (options.Help) {
                stdout.Write(Usage());
                return 0;
            }
            string input = options.InputPath ?? (repoRoot == null ? "source.obj" : Path.Combine(repoRoot, "source.obj"));

            try {
                var clock = Stopwatch.StartNew();
                ObjIo.Read(input, out double[] vertices, out uint[] triangles);
                var lines = new ProgressLines();
                RemeshRun run = Remesher.Run(vertices, triangles, options.Settings, (progress, status) => {
                    string line = lines.Update(progress);
                    if (line.Length > 0) {
                        stdout.Write(line);
                        stdout.Flush();
                    }
                });
                double seconds = clock.ElapsedMilliseconds / 1000.0;
                ObjIo.Write(options.OutputPath, run.Mesh);
                stdout.Write(lines.Done());
                stdout.Write(Summary(input, options, run, seconds));
                return 0;
            } catch (Exception e) when (e is ObjException || e is RemeshException || e is IOException
                                        || e is UnauthorizedAccessException) {
                stderr.WriteLine(ProgramName + ": " + e.Message);
                return 1;
            } catch (DllNotFoundException e) {
                stderr.WriteLine(ProgramName + ": cannot load " + Native.FileName + ": " + e.Message
                    + "\nBuild it with CMake (see csharp-demo/README.md), or set AUTOREMESHER_CORE_LIBRARY to its path.");
                return 1;
            }
        }

        /* The CLI's syntax: "--name value" or "--name=value", "-i value",
         * "-o value", and -h/--help anywhere wins.  The numbers are checked
         * against the CLI's ranges (src/cli/cli_options.h). */
        public static DemoOptions Parse(string[] args)
        {
            var options = new DemoOptions();
            foreach (string argument in args) {
                if (argument == "-h" || argument == "--help") {
                    options.Help = true;
                    return options;
                }
            }
            RemeshSettings s = options.Settings;
            for (int i = 0; i < args.Length; ++i) {
                string name = args[i];
                string? value = null;
                if (name.StartsWith("--", StringComparison.Ordinal)) {
                    int equals = name.IndexOf('=');
                    if (equals >= 0) {
                        value = name.Substring(equals + 1);
                        name = name.Substring(0, equals);
                    }
                }
                if (name == "--split-to-quads") {
                    if (value != null)
                        throw new ArgumentException(name + " takes no value");
                    s.SplitToQuads = true;
                    continue;
                }
                if (Array.IndexOf(ValueOptions, name) < 0)
                    throw new ArgumentException((name.StartsWith("-", StringComparison.Ordinal) ? "unknown option " : "unexpected argument ") + name);
                if (value == null) {
                    if (i + 1 >= args.Length)
                        throw new ArgumentException(name + " needs a value");
                    value = args[++i];
                }
                switch (name) {
                case "-i":
                case "--input":
                    options.InputPath = value;
                    break;
                case "-o":
                case "--output":
                    options.OutputPath = value;
                    break;
                case "--target-quads":
                    s.TargetQuads = ParseCount(name, value, 1, 2147483647);
                    break;
                case "--edge-scaling":
                    s.EdgeScaling = ParseReal(name, value, 1.0, 4.0);
                    break;
                case "--sharp-edge":
                    s.SharpEdgeDegrees = ParseReal(name, value, 30.0, 180.0);
                    break;
                case "--smooth-normal":
                    s.SmoothNormalDegrees = ParseReal(name, value, 0.0, 180.0);
                    break;
                case "--adaptivity":
                    s.Adaptivity = ParseReal(name, value, 0.0, 1.0);
                    break;
                case "--anisotropy":
                    s.Anisotropy = ParseReal(name, value, 0.0, 1.0);
                    break;
                }
            }
            return options;
        }

        public static string Usage()
        {
            RemeshSettings d = new RemeshSettings();
            return "Usage: " + ProgramName + " [-i <file.obj>] [-o <output.obj>] [options]\n"
                + "\n"
                + "Remeshes an OBJ in-process with the autoremesher_core library, through its C API\n"
                + "over P/Invoke, as autoremesher-cli does natively: the input is welded, remeshed\n"
                + "and written as the core's own polygon mesh (mostly quads).  The defaults are\n"
                + "M4.3's winning parameters.  Progress is printed as 'status NN%' lines, then a\n"
                + "summary.\n"
                + "\n"
                + "Options:\n"
                + "  -i, --input <file.obj>     Input .obj file (default: <repo>/source.obj)\n"
                + "  -o, --output <output.obj>  Output .obj file (default: remeshed_csharp.obj)\n"
                + "  --target-quads <count>     Target quad count (default: " + d.TargetQuads + ")\n"
                + "  --edge-scaling <factor>    Edge scaling factor (default: " + F(d.EdgeScaling) + ", range: 1.0-4.0)\n"
                + "  --sharp-edge <degrees>     Sharp edge dihedral angle threshold in degrees\n"
                + "                             (default: " + F(d.SharpEdgeDegrees) + ", range: 30.0-180.0)\n"
                + "  --smooth-normal <degrees>  Smooth normal angle threshold in degrees\n"
                + "                             (default: " + F(d.SmoothNormalDegrees) + ", range: 0.0-180.0)\n"
                + "  --adaptivity <value>       Curvature-adaptive quad density\n"
                + "                             (default: " + F(d.Adaptivity) + ", range: 0.0-1.0)\n"
                + "  --anisotropy <value>       Curvature-adaptive quad elongation\n"
                + "                             (default: " + F(d.Anisotropy) + ", range: 0.0-1.0)\n"
                + "  --split-to-quads           Split every remeshed face into quads; off by default\n"
                + "  -h, --help                 Show this help\n"
                + "\n"
                + "Values may also be given as --name=value.  The library is loaded from\n"
                + "$AUTOREMESHER_CORE_LIBRARY if set, else from the runtime's search path, else\n"
                + "from <repo>/build/.\n"
                + "Exit status: 0 on success, 1 if remeshing, file I/O or loading the library\n"
                + "fails, 2 on a usage error.\n";
        }

        static string Summary(string input, DemoOptions options, RemeshRun run, double seconds)
        {
            RemeshSettings s = options.Settings;
            PolygonMesh mesh = run.Mesh;
            var bySize = new SortedDictionary<uint, long>();
            foreach (uint size in mesh.FaceSizes)
                bySize[size] = bySize.TryGetValue(size, out long n) ? n + 1 : 1;
            var sizes = new List<string>();
            foreach (var pair in bySize)
                sizes.Add(pair.Key + "-gons " + pair.Value);
            string sha256;
            using (var stream = File.OpenRead(options.OutputPath))
                sha256 = Convert.ToHexString(SHA256.HashData(stream)).ToLowerInvariant();
            return "library: " + Native.LoadedFrom + "\n"
                + "input: " + input + " (" + run.InputVertexCount + " vertices, " + run.InputTriangleCount
                + " triangles; welded to " + run.WeldedVertexCount + " vertices)\n"
                + "parameters: --target-quads " + s.TargetQuads + " --edge-scaling " + F(s.EdgeScaling)
                + " --sharp-edge " + F(s.SharpEdgeDegrees) + " --smooth-normal " + F(s.SmoothNormalDegrees)
                + " --adaptivity " + F(s.Adaptivity) + " --anisotropy " + F(s.Anisotropy)
                + (s.SplitToQuads ? " --split-to-quads" : "") + "\n"
                + "output: " + options.OutputPath + " (" + mesh.VertexCount + " vertices, " + mesh.FaceCount
                + " faces: " + mesh.QuadCount + " quads, " + (mesh.FaceCount - mesh.QuadCount) + " non-quads; "
                + string.Join(", ", sizes) + ")\n"
                + "output sha256: " + sha256 + "\n"
                + "time: " + seconds.ToString("0.000", CultureInfo.InvariantCulture) + " seconds\n";
        }

        static readonly string[] ValueOptions = {
            "-i", "-o", "--input", "--output", "--target-quads", "--edge-scaling", "--sharp-edge",
            "--smooth-normal", "--adaptivity", "--anisotropy"
        };

        static string F(double value) => value.ToString("0.0##", CultureInfo.InvariantCulture);

        static ulong ParseCount(string name, string text, ulong minimum, ulong maximum)
        {
            bool digits = text.Length > 0 && text.Length <= 19;
            foreach (char c in text)
                digits &= c >= '0' && c <= '9';
            if (!digits || !ulong.TryParse(text, NumberStyles.None, CultureInfo.InvariantCulture, out ulong value)
                || value < minimum || value > maximum)
                throw new ArgumentException(name + " must be a whole number from " + minimum + " to " + maximum
                    + ", not \"" + text + "\"");
            return value;
        }

        static double ParseReal(string name, string text, double minimum, double maximum)
        {
            if (text.Length == 0 || char.IsWhiteSpace(text[0])
                || !double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out double value)
                || !double.IsFinite(value) || !(value >= minimum && value <= maximum))
                throw new ArgumentException(name + " must be a number from " + minimum.ToString(CultureInfo.InvariantCulture)
                    + " to " + maximum.ToString(CultureInfo.InvariantCulture) + ", not \"" + text + "\"");
            return value;
        }

        /* The repository: the nearest directory, from the assembly's and then
         * the working directory upwards, that holds the C API header. */
        public static string? FindRepoRoot()
        {
            foreach (string start in new[] { AppContext.BaseDirectory, Directory.GetCurrentDirectory() }) {
                for (DirectoryInfo? dir = new DirectoryInfo(start); dir != null; dir = dir.Parent) {
                    if (File.Exists(Path.Combine(dir.FullName, "include", "autoremesher_core_api.h")))
                        return dir.FullName;
                }
            }
            return null;
        }
    }

    /* The CLI's 'status NN%' lines (AutoRemesherCli::ProgressLines): a line
     * whenever the whole percent goes up, capped at 99 until Done(). */
    public sealed class ProgressLines
    {
        int m_lastPercent = -1;

        public string Update(float progress)
        {
            int percent = 0;
            if (progress >= 1.0f)
                percent = 99;
            else if (progress > 0.0f)
                percent = Math.Min(99, (int)(progress * 100));
            if (percent <= m_lastPercent)
                return "";
            m_lastPercent = percent;
            return "status " + percent + "%\n";
        }

        public string Done()
        {
            m_lastPercent = 100;
            return "status 100%\n";
        }
    }
}
