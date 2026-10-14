/*
 *  Smoke test of autoremesher_core on the real input, source.obj: load it,
 *  weld it, and remesh it at the GUI's defaults through the C API.
 *
 *  Usage: test_core <path to source.obj>
 *
 *  Exits 77 (which CTest reports as skipped) if the file cannot be opened:
 *  source.obj is not in git (.gitignore has *.obj).
 */
#include <autoremesher_core_api.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
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

// The regression guard of M1's exit criteria: source.obj's vertex count
// before and after welding (measured in M1.1-M1.3), and its triangle count.
const size_t sourceVertexCount = 97400;
const size_t sourceTriangleCount = 175566;
const size_t weldedVertexCount = 87691;

// The GUI's default "Target Quads" (mainwindow.cpp) and scaling.
const size_t targetQuadCount = 50000;
const double scaling = 1.0;

// Reads the positions of `v` lines and the vertex indices of `f` lines,
// fan-triangulating polygons. Nothing else of OBJ: the real reader is M2's.
bool loadObj(const char* path, std::vector<double>& vertices, std::vector<uint32_t>& triangles)
{
    std::ifstream in(path);
    if (!in)
        return false;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream stream(line);
        std::string tag;
        stream >> tag;
        if ("v" == tag) {
            double x, y, z;
            stream >> x >> y >> z;
            vertices.insert(vertices.end(), { x, y, z });
        } else if ("f" == tag) {
            std::vector<uint32_t> face;
            std::string corner;
            // "7", "7/1" and "7/1/3" all start with the 1-based vertex index.
            while (stream >> corner)
                face.push_back(static_cast<uint32_t>(std::stoul(corner) - 1));
            for (size_t i = 1; i + 1 < face.size(); ++i)
                triangles.insert(triangles.end(), { face[0], face[i], face[i + 1] });
        }
    }
    return true;
}

double secondsSince(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <source.obj>\n", argv[0]);
        return 2;
    }

    std::vector<double> rawVertices;
    std::vector<uint32_t> rawTriangles;
    if (!loadObj(argv[1], rawVertices, rawTriangles)) {
        std::fprintf(stderr, "cannot open %s; skipping\n", argv[1]);
        return 77;
    }
    CHECK(sourceVertexCount * 3 == rawVertices.size());
    CHECK(sourceTriangleCount * 3 == rawTriangles.size());

    autoremesher_core_weld_result welded;
    auto start = std::chrono::steady_clock::now();
    CHECK(1 == autoremesher_core_weld(rawVertices.data(), rawVertices.size() / 3,
                   rawTriangles.data(), rawTriangles.size() / 3, &welded));
    std::printf("weld: %zu -> %zu vertices, %zu triangles, %.3f s\n",
        rawVertices.size() / 3, welded.vertex_count, welded.triangle_count,
        secondsSince(start));
    CHECK(weldedVertexCount == welded.vertex_count);
    CHECK(sourceTriangleCount == welded.triangle_count);

    autoremesher_core* handle = autoremesher_core_create(welded.vertices, welded.vertex_count,
        welded.triangle_indices, welded.triangle_count);
    autoremesher_core_free_weld_result(&welded);
    CHECK(nullptr != handle);
    if (nullptr == handle)
        return 1;
    autoremesher_core_set_target_quad_count(handle, targetQuadCount);
    autoremesher_core_set_scaling(handle, scaling);

    start = std::chrono::steady_clock::now();
    CHECK(1 == autoremesher_core_remesh(handle));
    const double remeshSeconds = secondsSince(start);

    autoremesher_core_result result;
    CHECK(1 == autoremesher_core_get_result(handle, &result));
    std::printf("remesh, target %zu quads: %zu vertices, %zu faces, %zu quads, %.1f s\n",
        targetQuadCount, result.vertex_count, result.face_count, result.quad_count,
        remeshSeconds);
    CHECK(result.vertex_count > 0);
    CHECK(result.quad_count > 0);
    CHECK(result.quad_count <= result.face_count);
    size_t indexSum = 0;
    for (size_t i = 0; i < result.face_count; ++i)
        indexSum += result.face_sizes[i];
    CHECK(indexSum == result.index_count);
    for (size_t i = 0; i < result.index_count; ++i)
        CHECK(result.face_indices[i] < result.vertex_count);
    autoremesher_core_free_result(&result);
    autoremesher_core_destroy(handle);

    if (0 != failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
