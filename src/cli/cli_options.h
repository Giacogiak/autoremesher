/*
 *  Command line of autoremesher-cli.  The flags are those of the GUI's
 *  headless mode (src/main.cpp), with the same defaults, so scripts written
 *  against one work with the other.  No Qt.
 */
#ifndef AUTOREMESHER_CLI_OPTIONS_H
#define AUTOREMESHER_CLI_OPTIONS_H

#include <cstddef>
#include <string>

namespace AutoRemesherCli {

struct Options {
    std::string inputPath;
    std::string outputPath;
    std::string reportPath; // empty: no report
    std::size_t targetQuads = 50000;
    double edgeScaling = 1.0;
    double sharpEdgeDegrees = 90.0;
    double smoothNormalDegrees = 0.0;
    double adaptivity = 1.0;
    double anisotropy = 1.0;
    bool splitToQuads = false; // --split-to-quads: split every face into quads
    bool help = false;
};

// Inclusive ranges, from the GUI's widgets (MainWindow::MainWindow).
const double minEdgeScaling = 1.0, maxEdgeScaling = 4.0;
const double minSharpEdgeDegrees = 30.0, maxSharpEdgeDegrees = 180.0;
const double minSmoothNormalDegrees = 0.0, maxSmoothNormalDegrees = 180.0;
const double minAdaptivity = 0.0, maxAdaptivity = 1.0;
const double minAnisotropy = 0.0, maxAnisotropy = 1.0;
// The GUI stores the target in an int.
const std::size_t minTargetQuads = 1, maxTargetQuads = 2147483647;

// Parses argv[1..argc-1] into `options`, which starts from the defaults
// above.  Accepts "--name value" and "--name=value", and "-i value" /
// "-o value" for the two short forms, and the switch --split-to-quads, which
// takes no value; when a flag is repeated the last value
// wins, as with QCommandLineParser.  Returns false and sets `error` on an
// unknown flag, a missing or malformed value, a value out of range, a
// positional argument, or a missing --input or --output.  With -h/--help it
// returns true with `options.help` set and checks nothing else.
bool parse(int argc, const char* const* argv, Options& options, std::string& error);

std::string usage(const char* programName);

}

#endif
