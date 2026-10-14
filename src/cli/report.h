/*
 *  What autoremesher-cli prints while and after it works, without Qt: the
 *  `status NN%` progress lines, and the GUI headless mode's two reports
 *  (src/main.cpp), the block it prints on stdout and the file --report
 *  writes, with the same fields and lines so one parser reads both tools.
 */
#ifndef AUTOREMESHER_CLI_REPORT_H
#define AUTOREMESHER_CLI_REPORT_H

#include "cli_options.h"
#include <cstddef>
#include <string>

namespace AutoRemesherCli {

struct RunResult {
    std::size_t quadCount = 0;
    std::size_t nonQuadCount = 0;
    std::size_t vertexCount = 0;
    // From the start of reading the input to the end of the remesh, before
    // the output is written, as the GUI times it.
    double seconds = 0.0;
};

// The block the GUI prints on stdout after a headless run, from
// "=== AutoRemesher Report ===" to its closing line.  Numbers are formatted
// as std::cout formats them, as in the GUI.
std::string reportSummary(const Options& options, const RunResult& result);

// The text the GUI writes to its --report file: the paths, every parameter
// and the results.  Numbers are formatted as QTextStream formats them, as in
// the GUI (see formatQtDouble in obj_io.h).
std::string reportFile(const Options& options, const RunResult& result);

// Writes `content` to `path`, replacing it.  Returns false and sets `error`
// if the file cannot be written.
bool writeReport(const std::string& path, const std::string& content, std::string& error);

// Turns the core's progress calls into `status NN%` lines, NN a whole
// percent.  A line is only made when the percent goes past the last line's,
// so the lines count up and there are at most 101 of them.
class ProgressLines {
public:
    // The line for `progress` (0..1), with its '\n', or "" if there is
    // nothing new to say.  Stops at 99: only done() says 100.
    std::string update(float progress);

    // "status 100%\n", once the output has been written.
    std::string done();

private:
    int m_lastPercent = -1;
};

}

#endif
