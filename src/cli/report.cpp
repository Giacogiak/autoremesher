#include "report.h"
#include "obj_io.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace AutoRemesherCli {

namespace {

// A double as std::cout writes it by default: %g, six significant digits.
std::string formatCoutDouble(double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return buffer;
}

}

std::string reportSummary(const Options& options, const RunResult& result)
{
    // src/main.cpp, the headlessFinished handler's std::cout lines.
    return "=== AutoRemesher Report ===\n"
           "Input: " + options.inputPath + "\n"
           "Output: " + options.outputPath + "\n"
           "Quads: " + std::to_string(result.quadCount) + "\n"
           "Non-quads: " + std::to_string(result.nonQuadCount) + "\n"
           "Vertices: " + std::to_string(result.vertexCount) + "\n"
           "Time: " + formatCoutDouble(result.seconds) + " seconds\n"
           "===========================\n";
}

std::string reportFile(const Options& options, const RunResult& result)
{
    // src/main.cpp, the same handler's QTextStream lines.
    return "AutoRemesher Report\n"
           "===================\n\n"
           "Input file: " + options.inputPath + "\n"
           "Output file: " + options.outputPath + "\n"
           "Target quads: " + std::to_string(options.targetQuads) + "\n"
           "Edge scaling: " + formatQtDouble(options.edgeScaling) + "\n"
           "Sharp edge degrees: " + formatQtDouble(options.sharpEdgeDegrees) + "\n"
           "Smooth normal degrees: " + formatQtDouble(options.smoothNormalDegrees) + "\n"
           "Adaptivity: " + formatQtDouble(options.adaptivity) + "\n"
           "Anisotropy: " + formatQtDouble(options.anisotropy) + "\n\n"
           "Results:\n"
           "  Quads: " + std::to_string(result.quadCount) + "\n"
           "  Non-quads: " + std::to_string(result.nonQuadCount) + "\n"
           "  Vertices: " + std::to_string(result.vertexCount) + "\n"
           "  Total time: " + formatQtDouble(result.seconds) + " seconds\n";
}

bool writeReport(const std::string& path, const std::string& content, std::string& error)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (nullptr == file) {
        error = "cannot write " + path + ": " + std::strerror(errno);
        return false;
    }
    const bool failed = content.size() != std::fwrite(content.data(), 1, content.size(), file);
    if (0 != std::fclose(file) || failed) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

std::string ProgressLines::update(float progress)
{
    // As MainWindow::updateProgressDetailed turns it into a percent, in float.
    int percent = 0;
    if (progress >= 1.0f)
        percent = 99;
    else if (progress > 0.0f)
        percent = std::min(99, (int)(progress * 100));
    if (percent <= m_lastPercent)
        return std::string();
    m_lastPercent = percent;
    return "status " + std::to_string(percent) + "%\n";
}

std::string ProgressLines::done()
{
    m_lastPercent = 100;
    return "status 100%\n";
}

}
