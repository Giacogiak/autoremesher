/*
 *  autoremesher-cli: the GUI's headless mode (src/main.cpp) without Qt.
 *  Reads an OBJ, welds it, remeshes it through the autoremesher_core C API
 *  and writes the result as an OBJ, and with --report a report file.  With
 *  --split-to-quads it first splits every face of the result into quads.
 *
 *  Stdout carries `status NN%` lines while it works, then the GUI's report
 *  block; the core's own diagnostics go to stderr.
 *
 *  Exit status: 0 on success, 1 if reading, remeshing or writing fails, 2 on
 *  a usage error.
 */
#include "cli_options.h"
#include "obj_io.h"
#include "report.h"
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

struct Progress {
    AutoRemesherCli::ProgressLines lines;
    // The core reports why a remesh failed only through the progress
    // handler, as its last status.
    std::string lastStatus;
};

void reportProgress(void* tag, float progress, const char* status)
{
    Progress* state = static_cast<Progress*>(tag);
    state->lastStatus = status;
    const std::string line = state->lines.update(progress);
    if (!line.empty()) {
        // Flushed, so that a script reading through a pipe sees it now.
        std::fputs(line.c_str(), stdout);
        std::fflush(stdout);
    }
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
    Progress progress;
    autoremesher_core_set_progress_handler(handle, reportProgress, &progress);

    autoremesher_core_result result;
    if (!autoremesher_core_remesh(handle) || !autoremesher_core_get_result(handle, &result)) {
        autoremesher_core_destroy(handle);
        return fail("remeshing " + options.inputPath + " failed"
            + (progress.lastStatus.empty() ? std::string() : ": " + progress.lastStatus));
    }
    autoremesher_core_destroy(handle);

    // Split into quads only on request (decision D6, reverted): the split
    // turns every face into quads, quads too, so the output has about four
    // times as many faces as the core made, and loses the core's edge flow.
    if (options.splitToQuads) {
        autoremesher_core_result quads;
        const bool split = autoremesher_core_split_into_quads(&result, &quads);
        std::fprintf(stderr,
            "%s: remeshed to %zu vertices, %zu faces (%zu quads, %zu non-quads)%s\n",
            programName, result.vertex_count, result.face_count, result.quad_count,
            result.face_count - result.quad_count,
            split ? (", split into " + std::to_string(quads.face_count) + " quads").c_str() : "");
        autoremesher_core_free_result(&result);
        if (!split)
            return fail("cannot split the remeshed faces of " + options.inputPath + " into quads");
        result = quads; // takes over the split's arrays
    }

    // Timed as the GUI times it: whole milliseconds (QElapsedTimer) from
    // before the input is read until the mesh is made, not the writing.
    AutoRemesherCli::RunResult run;
    run.seconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start)
                      .count()
        / 1000.0;
    run.vertexCount = result.vertex_count;
    run.quadCount = result.quad_count;
    run.nonQuadCount = result.face_count - result.quad_count;

    const bool written = AutoRemesherCli::writeObj(options.outputPath, result, error);
    autoremesher_core_free_result(&result);
    if (!written)
        return fail(error);
    if (!options.reportPath.empty()
        && !AutoRemesherCli::writeReport(options.reportPath,
            AutoRemesherCli::reportFile(options, run), error))
        return fail(error);

    std::fputs(progress.lines.done().c_str(), stdout);
    std::fputs(AutoRemesherCli::reportSummary(options, run).c_str(), stdout);
    return 0;
}
