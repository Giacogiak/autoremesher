/*
 *  Tests autoremesher-cli's command line parsing (src/cli/cli_options.cpp):
 *  the defaults, both value syntaxes, the ranges and the refusals.
 */
#include "cli/cli_options.h"
#include <cstdio>
#include <initializer_list>
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

// Parses the arguments after the program name.
bool parse(std::initializer_list<const char*> args, AutoRemesherCli::Options& options,
    std::string& error)
{
    std::vector<const char*> argv { "autoremesher-cli" };
    argv.insert(argv.end(), args);
    options = AutoRemesherCli::Options();
    error.clear();
    return AutoRemesherCli::parse(static_cast<int>(argv.size()), argv.data(), options, error);
}

// True if parsing fails with a non-empty error.
bool refused(std::initializer_list<const char*> args)
{
    AutoRemesherCli::Options options;
    std::string error;
    if (parse(args, options, error))
        return false;
    return !error.empty();
}

}

int main()
{
    AutoRemesherCli::Options options;
    std::string error;

    // Defaults match the GUI's headless mode (main.cpp HeadlessParams).
    CHECK(parse({ "-i", "in.obj", "-o", "out.obj" }, options, error));
    CHECK(error.empty());
    CHECK("in.obj" == options.inputPath);
    CHECK("out.obj" == options.outputPath);
    CHECK(options.reportPath.empty());
    CHECK(50000 == options.targetQuads);
    CHECK(1.0 == options.edgeScaling);
    CHECK(90.0 == options.sharpEdgeDegrees);
    CHECK(0.0 == options.smoothNormalDegrees);
    CHECK(1.0 == options.adaptivity);
    CHECK(1.0 == options.anisotropy);
    CHECK(!options.splitToQuads);
    CHECK(!options.help);

    // Every flag, "--name value" form.
    CHECK(parse({ "--input", "a b.obj", "--output", "/tmp/out.obj", "--report", "r.txt",
                    "--target-quads", "11054", "--edge-scaling", "2.5", "--sharp-edge", "30",
                    "--smooth-normal", "180", "--adaptivity", "0", "--anisotropy", "0.25" },
        options, error));
    CHECK("a b.obj" == options.inputPath);
    CHECK("/tmp/out.obj" == options.outputPath);
    CHECK("r.txt" == options.reportPath);
    CHECK(11054 == options.targetQuads);
    CHECK(2.5 == options.edgeScaling);
    CHECK(30.0 == options.sharpEdgeDegrees);
    CHECK(180.0 == options.smoothNormalDegrees);
    CHECK(0.0 == options.adaptivity);
    CHECK(0.25 == options.anisotropy);

    // "--name=value" form, and range ends.
    CHECK(parse({ "--input=x.obj", "--output=y.obj", "--report=", "--target-quads=1",
                    "--edge-scaling=4", "--sharp-edge=180.0", "--smooth-normal=0",
                    "--adaptivity=1", "--anisotropy=1e-1" },
        options, error));
    CHECK("x.obj" == options.inputPath);
    CHECK("y.obj" == options.outputPath);
    CHECK(options.reportPath.empty());
    CHECK(1 == options.targetQuads);
    CHECK(4.0 == options.edgeScaling);
    CHECK(180.0 == options.sharpEdgeDegrees);
    CHECK(0.1 == options.anisotropy);
    CHECK(parse({ "-i", "x", "-o", "y", "--target-quads", "2147483647" }, options, error));
    CHECK(2147483647 == options.targetQuads);

    // The last of a repeated flag wins; a value may start with '-'.
    CHECK(parse({ "-i", "a", "-o", "b", "--input", "c", "--target-quads", "10",
                    "--target-quads", "20", "-o", "-out.obj" },
        options, error));
    CHECK("c" == options.inputPath);
    CHECK("-out.obj" == options.outputPath);
    CHECK(20 == options.targetQuads);

    // --split-to-quads is a switch: it takes no value, and repeating it is harmless.
    CHECK(parse({ "-i", "in.obj", "--split-to-quads", "-o", "out.obj" }, options, error));
    CHECK(options.splitToQuads);
    CHECK("out.obj" == options.outputPath);
    CHECK(parse({ "--split-to-quads", "-i", "in.obj", "-o", "out.obj", "--split-to-quads" },
        options, error));
    CHECK(options.splitToQuads);
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--split-to-quads=yes" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--split-to-quads=" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--split-to-quads", "yes" }));

    // Help needs nothing else, and wins over errors after it.
    CHECK(parse({ "--help" }, options, error));
    CHECK(options.help);
    CHECK(parse({ "-h", "--bogus" }, options, error));
    CHECK(options.help);
    CHECK(!AutoRemesherCli::usage("autoremesher-cli").empty());

    // Missing required flags.
    CHECK(refused({}));
    CHECK(refused({ "-i", "in.obj" }));
    CHECK(refused({ "-o", "out.obj" }));
    CHECK(refused({ "-i", "", "-o", "out.obj" }));
    CHECK(refused({ "-i", "in.obj", "-o=" }));

    // Unknown flags and positional arguments.
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--weld" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "-x" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "extra" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--help=yes" }));

    // A flag with no value.
    CHECK(refused({ "-o", "out.obj", "-i" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--target-quads" }));

    // Malformed numbers.
    for (const char* value : { "", "abc", "12x", " 12", "12 ", "1.5", "-5", "+5", "0x10", "1e4" })
        CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--target-quads", value }));
    for (const char* value : { "", "abc", "1.0x", " 1", "1,5", "nan", "inf", "-inf" })
        CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--adaptivity", value }));

    // Out of range.
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--target-quads", "0" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--target-quads", "2147483648" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--target-quads", "99999999999999999999999" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--edge-scaling", "0.999" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--edge-scaling", "4.001" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--sharp-edge", "29.9" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--sharp-edge", "180.1" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--smooth-normal", "-0.1" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--smooth-normal", "181" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--adaptivity", "1.01" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--anisotropy", "-1" }));
    CHECK(refused({ "-i", "in.obj", "-o", "out.obj", "--anisotropy", "1e400" }));

    if (0 != failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
