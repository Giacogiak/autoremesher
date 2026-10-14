/*
 *  Unit test of autoremesher-cli's progress lines and reports
 *  (src/cli/report).  The expected texts are the GUI headless mode's, from
 *  src/main.cpp.
 *
 *  Usage: test_report <scratch directory>
 */
#include "cli/obj_io.h"
#include "cli/report.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

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

#define CHECK_TEXT(expected, actual)                                       \
    do {                                                                   \
        const std::string expectedText = (expected), actualText = (actual); \
        if (expectedText != actualText) {                                  \
            std::fprintf(stderr, "%s:%d: CHECK failed: expected\n%s\n--- got\n%s\n---\n", \
                __FILE__, __LINE__, expectedText.c_str(), actualText.c_str()); \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

AutoRemesherCli::Options sampleOptions()
{
    AutoRemesherCli::Options options;
    options.inputPath = "dir with space/source.obj";
    options.outputPath = "/tmp/out.obj";
    options.reportPath = "/tmp/report.txt";
    options.targetQuads = 11054;
    options.edgeScaling = 1.5;
    options.sharpEdgeDegrees = 90.0;
    options.smoothNormalDegrees = 0.0;
    options.adaptivity = 0.3;
    options.anisotropy = 1.0;
    return options;
}

AutoRemesherCli::RunResult sampleResult()
{
    AutoRemesherCli::RunResult result;
    result.quadCount = 7913;
    result.nonQuadCount = 171;
    result.vertexCount = 8060;
    result.seconds = 15.183;
    return result;
}

void testReportFile()
{
    CHECK_TEXT("AutoRemesher Report\n"
               "===================\n"
               "\n"
               "Input file: dir with space/source.obj\n"
               "Output file: /tmp/out.obj\n"
               "Target quads: 11054\n"
               "Edge scaling: 1.5\n"
               "Sharp edge degrees: 90\n"
               "Smooth normal degrees: 0\n"
               "Adaptivity: 0.3\n"
               "Anisotropy: 1\n"
               "\n"
               "Results:\n"
               "  Quads: 7913\n"
               "  Non-quads: 171\n"
               "  Vertices: 8060\n"
               "  Total time: 15.183 seconds\n",
        AutoRemesherCli::reportFile(sampleOptions(), sampleResult()));

    // The defaults, and a time that QTextStream and std::cout write
    // differently: an exact tie at six digits.
    AutoRemesherCli::Options defaults;
    defaults.inputPath = "a.obj";
    defaults.outputPath = "b.obj";
    AutoRemesherCli::RunResult slow;
    slow.seconds = 123456.5;
    CHECK_TEXT("AutoRemesher Report\n"
               "===================\n"
               "\n"
               "Input file: a.obj\n"
               "Output file: b.obj\n"
               "Target quads: 50000\n"
               "Edge scaling: 1\n"
               "Sharp edge degrees: 90\n"
               "Smooth normal degrees: 0\n"
               "Adaptivity: 1\n"
               "Anisotropy: 1\n"
               "\n"
               "Results:\n"
               "  Quads: 0\n"
               "  Non-quads: 0\n"
               "  Vertices: 0\n"
               "  Total time: 123457 seconds\n",
        AutoRemesherCli::reportFile(defaults, slow));
}

void testReportSummary()
{
    CHECK_TEXT("=== AutoRemesher Report ===\n"
               "Input: dir with space/source.obj\n"
               "Output: /tmp/out.obj\n"
               "Quads: 7913\n"
               "Non-quads: 171\n"
               "Vertices: 8060\n"
               "Time: 15.183 seconds\n"
               "===========================\n",
        AutoRemesherCli::reportSummary(sampleOptions(), sampleResult()));

    // std::cout rounds the tie to even, unlike QTextStream above.
    AutoRemesherCli::Options options;
    options.inputPath = "a.obj";
    options.outputPath = "b.obj";
    AutoRemesherCli::RunResult slow;
    slow.seconds = 123456.5;
    const std::string summary = AutoRemesherCli::reportSummary(options, slow);
    CHECK(std::string::npos != summary.find("\nTime: 123456 seconds\n"));
}

void testFormatQtDouble()
{
    CHECK("0" == AutoRemesherCli::formatQtDouble(-0.0));
    CHECK("1.5" == AutoRemesherCli::formatQtDouble(1.5));
    CHECK("123457" == AutoRemesherCli::formatQtDouble(123456.5));
    CHECK("1e+06" == AutoRemesherCli::formatQtDouble(1e6));
}

void testWriteReport(const std::string& scratch)
{
    const std::string path = scratch + "/report.txt";
    std::string error;
    std::ofstream(path) << "an older, longer report that must not survive\n";
    CHECK(AutoRemesherCli::writeReport(path, "a\nb\n", error));
    CHECK("a\nb\n" == readFile(path));

    error.clear();
    CHECK(!AutoRemesherCli::writeReport(scratch + "/no/such/dir/report.txt", "a\n", error));
    CHECK(std::string::npos != error.find("no/such/dir/report.txt"));
}

void testProgressLines()
{
    AutoRemesherCli::ProgressLines lines;
    CHECK("status 0%\n" == lines.update(0.0f));
    CHECK("" == lines.update(0.0f));
    CHECK("" == lines.update(0.009f));
    CHECK("status 1%\n" == lines.update(0.01f));
    CHECK("status 2%\n" == lines.update(0.025f));
    CHECK("" == lines.update(0.029f));
    // Never backwards.
    CHECK("" == lines.update(0.015f));
    CHECK("status 57%\n" == lines.update(0.5712f));
    // 100 % is kept for done(); progress 1 also comes with a failure.
    CHECK("status 99%\n" == lines.update(1.0f));
    CHECK("" == lines.update(1.0f));
    CHECK("status 100%\n" == lines.done());

    // Nonsense in, nothing out of range out.
    AutoRemesherCli::ProgressLines odd;
    CHECK("status 0%\n" == odd.update(-0.5f));
    CHECK("" == odd.update(std::nanf("")));
    CHECK("status 99%\n" == odd.update(7.0f));

    // A run that starts late still starts with its own percent.
    AutoRemesherCli::ProgressLines late;
    CHECK("status 30%\n" == late.update(0.3f));
}

}

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <scratch directory>\n", argv[0]);
        return 2;
    }
    testReportFile();
    testReportSummary();
    testFormatQtDouble();
    testWriteReport(argv[1]);
    testProgressLines();

    if (0 != failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
