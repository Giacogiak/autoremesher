/*
 *  Runs the autoremesher-cli executable end to end on a small synthetic
 *  sphere, and checks its exit status on bad command lines.
 *
 *  Usage: test_cli <path to autoremesher-cli> <scratch directory>
 */
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

    // Every flag, set to a value that differs from its default, except
    // --report, which is not implemented yet.
    CHECK(0 == run("-i '" + input + "' --output='" + output + "' --target-quads 300"
                       " --edge-scaling 1.5 --sharp-edge 60 --smooth-normal 10"
                       " --adaptivity 0.5 --anisotropy 0.5"));

    // The output: a non-empty polygon mesh, mostly quads, on the unit sphere.
    std::ifstream in(output);
    CHECK(in.good());
    size_t vertexCount = 0, faceCount = 0, quadCount = 0, badIndices = 0, offSphere = 0;
    std::vector<size_t> faceIndices;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;
        if ("v" == tag) {
            double x = 0, y = 0, z = 0;
            stream >> x >> y >> z;
            ++vertexCount;
            // Remeshed vertices lie on the input's surface, whose flat facets
            // sit at most about 1 % inside the unit sphere.
            const double radius = std::sqrt(x * x + y * y + z * z);
            if (!(radius > 0.98 && radius < 1.001))
                ++offSphere;
        } else if ("f" == tag) {
            size_t index = 0, size = 0;
            while (stream >> index) {
                faceIndices.push_back(index);
                ++size;
            }
            ++faceCount;
            if (4 == size)
                ++quadCount;
            if (size < 3)
                ++badIndices;
        }
    }
    for (size_t index : faceIndices) {
        if (index < 1 || index > vertexCount)
            ++badIndices;
    }
    std::printf("output: %zu vertices, %zu faces, %zu quads\n", vertexCount, faceCount, quadCount);
    CHECK(vertexCount > 0);
    CHECK(faceCount > 0);
    CHECK(quadCount * 10 >= faceCount * 9);
    CHECK(0 == badIndices);
    CHECK(0 == offSphere);

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
    CHECK(2 == run("-i '" + input + "' -o '" + unwritten + "' --report '" + scratch + "/r.txt'"));
    CHECK(!exists(unwritten));

    // Runtime failures exit 1.
    CHECK(1 == run("-i '" + scratch + "/missing.obj' -o '" + unwritten + "'"));
    CHECK(!exists(unwritten));
    CHECK(1 == run("-i '" + input + "' -o '" + scratch + "/no/such/dir/out.obj' --target-quads 300"));

    if (0 != failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
