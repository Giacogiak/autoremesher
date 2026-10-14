#include "cli_options.h"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace AutoRemesherCli {

namespace {

    // Digits only: no sign, no blanks, no exponent.
    bool parseCount(const std::string& text, std::size_t minimum, std::size_t maximum,
        std::size_t& value)
    {
        if (text.empty() || text.size() > 19)
            return false;
        for (char c : text) {
            if (!std::isdigit(static_cast<unsigned char>(c)))
                return false;
        }
        const unsigned long long parsed = std::strtoull(text.c_str(), nullptr, 10);
        if (parsed < minimum || parsed > maximum)
            return false;
        value = static_cast<std::size_t>(parsed);
        return true;
    }

    // A finite decimal number (strtod syntax, "C" locale) in [minimum, maximum],
    // with nothing before or after it.
    bool parseReal(const std::string& text, double minimum, double maximum, double& value)
    {
        if (text.empty() || std::isspace(static_cast<unsigned char>(text[0])))
            return false;
        errno = 0;
        char* end = nullptr;
        const double parsed = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size() || ERANGE == errno || !std::isfinite(parsed))
            return false;
        if (!(parsed >= minimum && parsed <= maximum))
            return false;
        value = parsed;
        return true;
    }

    std::string formatRange(double minimum, double maximum)
    {
        std::ostringstream stream;
        stream << minimum << " to " << maximum;
        return stream.str();
    }

}

bool parse(int argc, const char* const* argv, Options& options, std::string& error)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if ("-h" == argument || "--help" == argument) {
            options.help = true;
            return true;
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string name = argv[i];
        std::string value;
        bool hasValue = false;
        if (0 == name.compare(0, 2, "--")) {
            const std::size_t equals = name.find('=');
            if (std::string::npos != equals) {
                value = name.substr(equals + 1);
                name.erase(equals);
                hasValue = true;
            }
        }
        if ("--split-to-quads" == name) {
            if (hasValue) {
                error = name + " takes no value";
                return false;
            }
            options.splitToQuads = true;
            continue;
        }
        const bool isFlag = "-i" == name || "-o" == name || "--input" == name
            || "--output" == name || "--report" == name || "--target-quads" == name
            || "--edge-scaling" == name || "--sharp-edge" == name || "--smooth-normal" == name
            || "--adaptivity" == name || "--anisotropy" == name;
        if (!isFlag) {
            if (!name.empty() && '-' == name[0])
                error = "unknown option " + name;
            else
                error = "unexpected argument " + name;
            return false;
        }
        if (!hasValue) {
            if (i + 1 >= argc) {
                error = name + " needs a value";
                return false;
            }
            value = argv[++i];
        }

        if ("-i" == name || "--input" == name) {
            options.inputPath = value;
        } else if ("-o" == name || "--output" == name) {
            options.outputPath = value;
        } else if ("--report" == name) {
            options.reportPath = value;
        } else if ("--target-quads" == name) {
            if (!parseCount(value, minTargetQuads, maxTargetQuads, options.targetQuads)) {
                error = name + " must be a whole number from " + std::to_string(minTargetQuads)
                    + " to " + std::to_string(maxTargetQuads) + ", not \"" + value + "\"";
                return false;
            }
        } else {
            struct Real {
                const char* name;
                double minimum, maximum;
                double* target;
            };
            const Real reals[] = {
                { "--edge-scaling", minEdgeScaling, maxEdgeScaling, &options.edgeScaling },
                { "--sharp-edge", minSharpEdgeDegrees, maxSharpEdgeDegrees,
                    &options.sharpEdgeDegrees },
                { "--smooth-normal", minSmoothNormalDegrees, maxSmoothNormalDegrees,
                    &options.smoothNormalDegrees },
                { "--adaptivity", minAdaptivity, maxAdaptivity, &options.adaptivity },
                { "--anisotropy", minAnisotropy, maxAnisotropy, &options.anisotropy },
            };
            for (const Real& real : reals) {
                if (name != real.name)
                    continue;
                if (!parseReal(value, real.minimum, real.maximum, *real.target)) {
                    error = name + " must be a number from "
                        + formatRange(real.minimum, real.maximum) + ", not \"" + value + "\"";
                    return false;
                }
            }
        }
    }

    if (options.inputPath.empty()) {
        error = "--input is required";
        return false;
    }
    if (options.outputPath.empty()) {
        error = "--output is required";
        return false;
    }
    return true;
}

std::string usage(const char* programName)
{
    std::ostringstream stream;
    stream << "Usage: " << programName << " -i <file.obj> -o <output.obj> [options]\n"
           << "\n"
           << "AutoRemesher - Automatic quad remeshing tool, without the GUI.\n"
           << "The input is welded (vertices at the same position merged) before remeshing.\n"
           << "The output is the remesher's own polygon mesh: mostly quads, with a few\n"
           << "triangles and 5- to 7-gons, counted as non-quads in the summary.\n"
           << "Progress is printed on stdout as 'status NN%' lines, then a summary.\n"
           << "\n"
           << "Options:\n"
           << "  -i, --input <file.obj>     Input .obj file to remesh (required)\n"
           << "  -o, --output <output.obj>  Output .obj file for the remeshed result (required)\n"
           << "  --report <report.txt>      Report file with the paths, the parameters and stats\n"
           << "                             (quads, non-quads, vertices, time); none if omitted\n"
           << "  --target-quads <count>     Target quad count (default: 50000)\n"
           << "  --edge-scaling <factor>    Edge scaling factor (default: 1.0, range: 1.0-4.0)\n"
           << "  --sharp-edge <degrees>     Sharp edge dihedral angle threshold in degrees\n"
           << "                             (default: 90.0, range: 30.0-180.0)\n"
           << "  --smooth-normal <degrees>  Smooth normal angle threshold in degrees\n"
           << "                             (default: 0.0, range: 0.0-180.0)\n"
           << "  --adaptivity <value>       Curvature-adaptive quad density\n"
           << "                             (default: 1.0, range: 0.0-1.0)\n"
           << "  --anisotropy <value>       Curvature-adaptive quad elongation\n"
           << "                             (default: 1.0, range: 0.0-1.0)\n"
           << "  --split-to-quads           Split every remeshed face into quads (an n-gon into\n"
           << "                             n quads, quads included), so the output is all quads,\n"
           << "                             with about 4 times as many faces; off by default\n"
           << "  -h, --help                 Show this help\n"
           << "\n"
           << "Values may also be given as --name=value.\n"
           << "Exit status: 0 on success, 1 if remeshing or file I/O fails, 2 on a usage error.\n";
    return stream.str();
}

}
