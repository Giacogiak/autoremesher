/*
 *  Runs the autoremesher-cli executable end to end on a small synthetic
 *  sphere, checks what it prints and the report it writes, and checks its
 *  exit status on bad command lines.
 *
 *  Usage: test_cli <path to autoremesher-cli> <scratch directory>
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <vector>

namespace {

int failures = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                #condition);                                               \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

std::string cli;
std::string scratch;

// Runs the CLI with `arguments` (already shell-quoted) and returns its exit
// status, or -1 if it did not exit normally.
int run(const std::string& arguments)
{
    const std::string command = "'" + cli + "' " + arguments;
    std::printf("$ %s\n", command.c_str());
    std::fflush(stdout);
    const int status = std::system(command.c_str());
    if (-1 == status || !WIFEXITED(status))
        return -1;
    return WEXITSTATUS(status);
}

// A closed UV sphere of radius 1 written as a triangle soup, every triangle
// with its own three vertices, so the CLI has to weld it to remesh it.
void writeSphereSoup(const std::string& path)
{
    const int segments = 32, rings = 16;
    const double pi = 3.14159265358979323846;
    std::vector<double> positions { 0.0, 1.0, 0.0 };
    for (int ring = 1; ring < rings; ++ring) {
        const double theta = pi * ring / rings;
        for (int segment = 0; segment < segments; ++segment) {
            const double phi = 2.0 * pi * segment / segments;
            positions.insert(positions.end(),
                { std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) });
        }
    }
    positions.insert(positions.end(), { 0.0, -1.0, 0.0 });
    const int southPole = 1 + (rings - 1) * segments;
    auto ringVertex = [&](int ring, int segment) { return 1 + (ring - 1) * segments + segment % segments; };

    std::vector<int> triangles;
    for (int segment = 0; segment < segments; ++segment) {
        triangles.insert(triangles.end(), { 0, ringVertex(1, segment + 1), ringVertex(1, segment) });
        triangles.insert(triangles.end(),
            { southPole, ringVertex(rings - 1, segment), ringVertex(rings - 1, segment + 1) });
    }
    for (int ring = 1; ring < rings - 1; ++ring) {
        for (int segment = 0; segment < segments; ++segment) {
            triangles.insert(triangles.end(),
                { ringVertex(ring, segment), ringVertex(ring, segment + 1), ringVertex(ring + 1, segment) });
            triangles.insert(triangles.end(),
                { ringVertex(ring, segment + 1), ringVertex(ring + 1, segment + 1),
                    ringVertex(ring + 1, segment) });
        }
    }

    FILE* file = std::fopen(path.c_str(), "w");
    if (nullptr == file) {
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
        std::exit(2);
    }
    for (int corner : triangles) {
        std::fprintf(file, "v %.17g %.17g %.17g\n", positions[corner * 3], positions[corner * 3 + 1],
            positions[corner * 3 + 2]);
    }
    for (size_t i = 0; i < triangles.size(); i += 3)
        std::fprintf(file, "f %zu %zu %zu\n", i + 1, i + 2, i + 3);
    std::fclose(file);
}

bool exists(const std::string& path)
{
    return std::ifstream(path).good();
}

std::vector<std::string> readLines(const std::string& path)
{
    std::vector<std::string> lines;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

// An OBJ as autoremesher-cli writes it: `v x y z` and `f a b c ...` lines.
struct Mesh {
    std::string firstLine;
    std::vector<std::vector<double>> positions;
    std::vector<double> radii;
    std::vector<std::vector<size_t>> faces;
    size_t badIndices = 0; // out-of-range indices and faces of under 3 corners

    size_t vertexCount() const { return positions.size(); }
    size_t quadCount() const
    {
        return std::count_if(faces.begin(), faces.end(),
            [](const std::vector<size_t>& face) { return 4 == face.size(); });
    }
};

Mesh readMesh(const std::string& path)
{
    Mesh mesh;
    std::ifstream in(path);
    CHECK(in.good());
    std::getline(in, mesh.firstLine);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;
        if ("v" == tag) {
            double x = 0, y = 0, z = 0;
            stream >> x >> y >> z;
            mesh.positions.push_back({ x, y, z });
            mesh.radii.push_back(std::sqrt(x * x + y * y + z * z));
        } else if ("f" == tag) {
            std::vector<size_t> face;
            size_t index = 0;
            while (stream >> index)
                face.push_back(index);
            if (face.size() < 3)
                ++mesh.badIndices;
            mesh.faces.push_back(face);
        }
    }
    for (const std::vector<size_t>& face : mesh.faces) {
        for (size_t index : face) {
            if (index < 1 || index > mesh.positions.size())
                ++mesh.badIndices;
        }
    }
    return mesh;
}

}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <autoremesher-cli> <scratch directory>\n", argv[0]);
        return 2;
    }
    cli = argv[1];
    scratch = argv[2];
    const std::string input = scratch + "/sphere_soup.obj";
    const std::string output = scratch + "/sphere_remeshed.obj";
    writeSphereSoup(input);
    std::remove(output.c_str());

    // Every flag, set to a value that differs from its default.
    const std::string report = scratch + "/sphere_report.txt";
    const std::string printed = scratch + "/sphere_stdout.txt";
    std::remove(report.c_str());
    CHECK(0 == run("-i '" + input + "' --output='" + output + "' --target-quads 300"
                       " --edge-scaling 1.5 --sharp-edge 60 --smooth-normal 10"
                       " --adaptivity 0.5 --anisotropy 0.5 --report '" + report + "' > '"
            + printed + "'"));

    // The output: a non-empty polygon mesh, mostly quads, on the unit sphere.
    // It is the core's own mesh, not split into quads (D6, reverted).
    const Mesh mesh = readMesh(output);
    // It starts with the GUI's own header.
    CHECK(0 == mesh.firstLine.compare(0, 15, "# AutoRemesher "));
    const size_t vertexCount = mesh.vertexCount(), faceCount = mesh.faces.size();
    const size_t quadCount = mesh.quadCount();
    size_t offSphere = 0, corners = 0;
    for (double radius : mesh.radii) {
        // Remeshed vertices lie on the input's surface, whose flat facets sit
        // at most about 1 % inside the unit sphere.
        if (!(radius > 0.98 && radius < 1.001))
            ++offSphere;
    }
    for (const std::vector<size_t>& face : mesh.faces)
        corners += face.size();
    std::printf("output: %zu vertices, %zu faces, %zu quads\n", vertexCount, faceCount, quadCount);
    CHECK(vertexCount > 0);
    CHECK(faceCount > 0);
    CHECK(quadCount * 10 >= faceCount * 9);
    CHECK(0 == mesh.badIndices);
    CHECK(0 == offSphere);

    // Stdout: `status NN%` lines counting up to 100, then the GUI's report
    // block with the output's own counts, and nothing else.
    const std::vector<std::string> stdoutLines = readLines(printed);
    int lastPercent = -1;
    size_t statusLines = 0;
    for (; statusLines < stdoutLines.size(); ++statusLines) {
        int percent = -1;
        char rest = 0;
        if (2 != std::sscanf(stdoutLines[statusLines].c_str(), "status %d%c", &percent, &rest))
            break;
        CHECK('%' == rest);
        CHECK("status " + std::to_string(percent) + "%" == stdoutLines[statusLines]);
        CHECK(percent > lastPercent && percent <= 100);
        lastPercent = percent;
    }
    std::printf("stdout: %zu status lines\n", statusLines);
    CHECK(statusLines >= 3);
    CHECK(100 == lastPercent);
    double seconds = -1.0;
    CHECK(statusLines + 8 == stdoutLines.size());
    if (statusLines + 8 == stdoutLines.size()) {
        const std::string* block = &stdoutLines[statusLines];
        CHECK("=== AutoRemesher Report ===" == block[0]);
        CHECK("Input: " + input == block[1]);
        CHECK("Output: " + output == block[2]);
        CHECK("Quads: " + std::to_string(quadCount) == block[3]);
        CHECK("Non-quads: " + std::to_string(faceCount - quadCount) == block[4]);
        CHECK("Vertices: " + std::to_string(vertexCount) == block[5]);
        CHECK(1 == std::sscanf(block[6].c_str(), "Time: %lf seconds", &seconds));
        CHECK(seconds > 0.0 && seconds < 300.0);
        CHECK("===========================" == block[7]);
    }

    // The report file: the GUI's --report format, with every parameter.
    const std::vector<std::string> reportLines = readLines(report);
    CHECK(17 == reportLines.size());
    if (17 == reportLines.size()) {
        const std::vector<std::string> expected { "AutoRemesher Report", "===================", "",
            "Input file: " + input, "Output file: " + output, "Target quads: 300",
            "Edge scaling: 1.5", "Sharp edge degrees: 60", "Smooth normal degrees: 10",
            "Adaptivity: 0.5", "Anisotropy: 0.5", "", "Results:",
            "  Quads: " + std::to_string(quadCount),
            "  Non-quads: " + std::to_string(faceCount - quadCount),
            "  Vertices: " + std::to_string(vertexCount) };
        for (size_t i = 0; i < expected.size(); ++i) {
            if (expected[i] != reportLines[i]) {
                std::fprintf(stderr, "report line %zu: expected '%s', got '%s'\n", i + 1,
                    expected[i].c_str(), reportLines[i].c_str());
                ++failures;
            }
        }
        double reportSeconds = -1.0;
        CHECK(1 == std::sscanf(reportLines[16].c_str(), "  Total time: %lf seconds", &reportSeconds));
        CHECK(reportSeconds == seconds);
    }

    // --split-to-quads: the same remesh, every face then split into quads, an
    // n-gon into n.  Corner 0 of each output quad is a vertex of the remesh,
    // kept first and in order, and corners 1 to 3 are the edge midpoints and
    // face centroids the split adds.
    const std::string splitOutput = scratch + "/sphere_split.obj";
    const std::string splitPrinted = scratch + "/sphere_split_stdout.txt";
    std::remove(splitOutput.c_str());
    CHECK(0 == run("-i '" + input + "' -o '" + splitOutput + "' --target-quads 300"
                       " --edge-scaling 1.5 --sharp-edge 60 --smooth-normal 10"
                       " --adaptivity 0.5 --anisotropy 0.5 --split-to-quads > '" + splitPrinted
            + "'"));
    const Mesh split = readMesh(splitOutput);
    CHECK(0 == split.firstLine.compare(0, 15, "# AutoRemesher "));
    CHECK(0 == split.badIndices);
    CHECK(split.faces.size() == corners);
    CHECK(split.quadCount() == split.faces.size());
    CHECK(split.vertexCount() > vertexCount);
    size_t movedVertices = 0, offChord = 0;
    double lowestAdded = 2.0;
    for (size_t i = 0; i < vertexCount && i < split.vertexCount(); ++i) {
        if (mesh.positions[i] != split.positions[i])
            ++movedVertices;
    }
    if (0 == split.badIndices && split.quadCount() == split.faces.size()) {
        std::vector<int> role(split.vertexCount(), -1);
        for (const std::vector<size_t>& face : split.faces) {
            for (size_t corner = 0; corner < 4; ++corner) {
                const int expected = 0 == corner ? 0 : 1;
                int& known = role[face[corner] - 1];
                if (-1 != known && expected != known)
                    ++offChord;
                if (expected != (face[corner] <= vertexCount ? 0 : 1))
                    ++offChord;
                known = expected;
            }
        }
        for (size_t i = vertexCount; i < split.vertexCount(); ++i) {
            // Added vertices are means of remeshed ones, so they sit on chords
            // of the sphere, further inside: 0.953 at worst here, with faces
            // about 0.35 across.
            lowestAdded = std::min(lowestAdded, split.radii[i]);
            if (!(split.radii[i] > 0.9 && split.radii[i] < 1.001))
                ++offChord;
        }
    }
    std::printf("split: %zu vertices, %zu quads; lowest added radius %.4f\n", split.vertexCount(),
        split.faces.size(), lowestAdded);
    CHECK(0 == movedVertices);
    CHECK(0 == offChord);
    // The stdout block counts the split mesh that was written.
    const std::vector<std::string> splitLines = readLines(splitPrinted);
    CHECK(splitLines.size() >= 8);
    if (splitLines.size() >= 8) {
        const std::string* block = &splitLines[splitLines.size() - 8];
        CHECK("=== AutoRemesher Report ===" == block[0]);
        CHECK("Quads: " + std::to_string(split.faces.size()) == block[3]);
        CHECK("Non-quads: 0" == block[4]);
        CHECK("Vertices: " + std::to_string(split.vertexCount()) == block[5]);
    }

    // Without --report, no report, and the same stdout format.
    const std::string printedAgain = scratch + "/sphere_stdout_again.txt";
    std::remove(report.c_str());
    CHECK(0 == run("-i '" + input + "' -o '" + output + "' --target-quads 300 > '" + printedAgain
        + "'"));
    CHECK(!exists(report));
    const std::vector<std::string> againLines = readLines(printedAgain);
    CHECK(!againLines.empty() && 0 == againLines.front().compare(0, 7, "status ")
        && "===========================" == againLines.back());

    // Help.
    CHECK(0 == run("--help > /dev/null"));
    CHECK(0 == run("-h > /dev/null"));

    // Usage errors exit 2 and write nothing.
    const std::string unwritten = scratch + "/unwritten.obj";
    std::remove(unwritten.c_str());
    CHECK(2 == run(""));
    CHECK(2 == run("-i '" + input + "'"));
    CHECK(2 == run("-o '" + unwritten + "'"));
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --weld"));
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --target-quads 0"));
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --sharp-edge 200"));
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --adaptivity abc"));
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --report"));
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --split-to-quads=yes"));
    CHECK(!exists(unwritten));

    // Runtime failures exit 1.
    CHECK(1 == run("-i '" + scratch + "/missing.obj' -o '" + unwritten + "'"));
    CHECK(!exists(unwritten));
    CHECK(1 == run("-i '" + input + "' -o '" + scratch + "/no/such/dir/out.obj' --target-quads 300"));
    // An input the reader refuses: face 2 names a vertex that does not exist.
    const std::string bad = scratch + "/bad_index.obj";
    std::ofstream(bad) << "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3\nf 1 3 5\n";
    CHECK(1 == run("-i '" + bad + "' -o '" + unwritten + "'"));
    CHECK(!exists(unwritten));
    // A run that fails writes no report and prints nothing on stdout.
    const std::string unreported = scratch + "/unreported.txt";
    const std::string failedStdout = scratch + "/failed_stdout.txt";
    std::remove(unreported.c_str());
    CHECK(1 == run("-i '" + bad + "' -o '" + unwritten + "' --report '" + unreported + "' > '"
        + failedStdout + "'"));
    CHECK(!exists(unreported));
    CHECK(readLines(failedStdout).empty());
    // An unwritable report fails the run, after the mesh is written.
    std::remove(output.c_str());
    CHECK(1 == run("-i '" + input + "' -o '" + output + "' --target-quads 300 --report '" + scratch
        + "/no/such/dir/report.txt' > /dev/null"));
    CHECK(exists(output));

    if (0 != failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
