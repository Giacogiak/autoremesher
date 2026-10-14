/*
 *  autoremesher-cli: the GUI's headless mode (src/main.cpp) without Qt.
 *  Reads an OBJ, welds it, remeshes it through the autoremesher_core C API
 *  and writes the result as an OBJ.
 *
 *  Exit status: 0 on success, 1 if reading, remeshing or writing fails, 2 on
 *  a usage error.
 */
#include "cli_options.h"
#include "obj_io.h"
#include <autoremesher_core_api.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* programName = "autoremesher-cli";

int fail(const std::string& message)
{
    std::fprintf(stderr, "%s: %s\n", programName, message.c_str());
    return 1;
}

// The core reports why a remesh failed only through the progress handler,
// as its last status.
void recordStatus(void* tag, float, const char* status)
{
    *static_cast<std::string*>(tag) = status;
}

}

int main(int argc, char** argv)
{
    AutoRemesherCli::Options options;
    std::string error;
    if (!AutoRemesherCli::parse(argc, argv, options, error)) {
        std::fprintf(stderr, "%s: %s\nTry '%s --help'.\n", programName, error.c_str(),
            programName);
        return 2;
    }
    if (options.help) {
        std::fputs(AutoRemesherCli::usage(programName).c_str(), stdout);
        return 0;
    }
    if (!options.reportPath.empty()) {
        // Lands with M2 Task 3; refused rather than silently ignored until then.
        std::fprintf(stderr, "%s: --report is not implemented yet\n", programName);
        return 2;
    }

    const auto start = std::chrono::steady_clock::now();

    std::vector<double> rawVertices;
    std::vector<uint32_t> rawTriangles;
    if (!AutoRemesherCli::readObj(options.inputPath, rawVertices, rawTriangles, error))
        return fail(error);

    // Always welded, with no flag to turn it off (decision D2): OBJ files
    // such as source.obj repeat a vertex wherever its normals or UVs differ.
    autoremesher_core_weld_result welded;
    if (!autoremesher_core_weld(rawVertices.data(), rawVertices.size() / 3,
            rawTriangles.data(), rawTriangles.size() / 3, &welded)) {
        // readObj has already refused bad indices and non-finite coordinates.
        return fail("cannot weld " + options.inputPath
            + " (a coordinate beyond about 9.2e13, or out of memory?)");
    }
    autoremesher_core* handle = autoremesher_core_create(welded.vertices, welded.vertex_count,
        welded.triangle_indices, welded.triangle_count);
    autoremesher_core_free_weld_result(&welded);
    if (nullptr == handle)
        return fail("cannot create the remesher for " + options.inputPath);

    // Every setting is set, so the CLI's defaults are the ones that apply.
    autoremesher_core_set_target_quad_count(handle, options.targetQuads);
    autoremesher_core_set_scaling(handle, options.edgeScaling);
    autoremesher_core_set_sharp_edge_degrees(handle, options.sharpEdgeDegrees);
    autoremesher_core_set_smooth_normal_degrees(handle, options.smoothNormalDegrees);
    autoremesher_core_set_adaptivity(handle, options.adaptivity);
    autoremesher_core_set_anisotropy(handle, options.anisotropy);
    std::string lastStatus;
    autoremesher_core_set_progress_handler(handle, recordStatus, &lastStatus);

    autoremesher_core_result result;
    if (!autoremesher_core_remesh(handle) || !autoremesher_core_get_result(handle, &result)) {
        autoremesher_core_destroy(handle);
        return fail("remeshing " + options.inputPath + " failed"
            + (lastStatus.empty() ? std::string() : ": " + lastStatus));
    }
    autoremesher_core_destroy(handle);

    const bool written = AutoRemesherCli::writeObj(options.outputPath, result, error);
    const size_t vertexCount = result.vertex_count;
    const size_t faceCount = result.face_count;
    const size_t quadCount = result.quad_count;
    autoremesher_core_free_result(&result);
    if (!written)
        return fail(error);

    const double seconds
        = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("%s: %zu vertices, %zu faces (%zu quads, %zu non-quads), %.1f s\n",
        options.outputPath.c_str(), vertexCount, faceCount, quadCount, faceCount - quadCount,
        seconds);
    return 0;
}
