#include "obj_io.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

namespace AutoRemesherCli {

namespace {

bool isSpace(char c)
{
    return ' ' == c || '\t' == c || '\r' == c || '\v' == c || '\f' == c;
}

// The whitespace-separated tokens of one line, in order.
class Tokens {
public:
    explicit Tokens(const std::string& line)
        : m_next(line.c_str())
    {
    }

    // Sets [begin, end) to the next token; false at the end of the line.
    bool next(const char*& begin, const char*& end)
    {
        while (isSpace(*m_next))
            ++m_next;
        if ('\0' == *m_next)
            return false;
        begin = m_next;
        while ('\0' != *m_next && !isSpace(*m_next))
            ++m_next;
        end = m_next;
        return true;
    }

private:
    const char* m_next;
};

// A token that is entirely a finite number.
bool parseCoordinate(const char* begin, const char* end, double& value)
{
    char* parsed = nullptr;
    value = std::strtod(begin, &parsed);
    return parsed == end && std::isfinite(value);
}

// The vertex index at the start of a face corner (`7`, `7/1`, `7//3`,
// `-2/1/3`): a whole number, ended by the token's end or by a '/'.
bool parseIndex(const char* begin, const char* end, long long& value)
{
    char* parsed = nullptr;
    errno = 0;
    value = std::strtoll(begin, &parsed, 10);
    return parsed != begin && 0 == errno && (parsed == end || '/' == *parsed);
}

// The number as QTextStream writes a double by default (SmartNotation,
// precision 6), into `buffer`.  That is printf's %g, with two differences
// measured against Qt 6.11 (tests/manual/qtextstream_probe.cpp): negative
// zero loses its sign, and a value exactly halfway between two six-digit
// results rounds away from zero where glibc rounds it to even.
void formatNumber(double value, char (&buffer)[32])
{
    if (0.0 == value) {
        std::strcpy(buffer, "0");
        return;
    }
    // An exact tie is a value whose seventh significant digit is a 5 with
    // nothing after it; nudged one step away from zero, it rounds that way.
    char seven[32];
    std::snprintf(seven, sizeof(seven), "%.6e", value);
    const char* exponent = std::strchr(seven, 'e');
    if (nullptr != exponent && '5' == exponent[-1] && std::strtod(seven, nullptr) == value)
        value = std::nextafter(value, value > 0 ? HUGE_VAL : -HUGE_VAL);
    std::snprintf(buffer, sizeof(buffer), "%g", value);
}

}

std::string formatQtDouble(double value)
{
    char buffer[32];
    formatNumber(value, buffer);
    return buffer;
}

bool readObj(const std::string& path, std::vector<double>& vertices,
    std::vector<uint32_t>& triangles, std::string& error)
{
    vertices.clear();
    triangles.clear();
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }

    std::string line;
    size_t lineNumber = 0;
    auto fail = [&](const std::string& message) {
        error = path + ":" + std::to_string(lineNumber) + ": " + message;
        return false;
    };
    // A face may name a vertex defined further down, so positive indices are
    // checked against the vertex count only at the end of the file.
    long long largestIndex = 0;
    size_t largestIndexLine = 0;
    std::vector<uint32_t> face;
    while (std::getline(in, line)) {
        ++lineNumber;
        Tokens tokens(line);
        const char* begin = nullptr;
        const char* end = nullptr;
        if (!tokens.next(begin, end))
            continue;
        const std::string tag(begin, end);
        if ("v" == tag) {
            double position[3];
            for (double& coordinate : position) {
                if (!tokens.next(begin, end))
                    return fail("a vertex needs three coordinates");
                if (!parseCoordinate(begin, end, coordinate))
                    return fail("\"" + std::string(begin, end) + "\" is not a finite number");
            }
            vertices.insert(vertices.end(), position, position + 3);
        } else if ("f" == tag) {
            const long long vertexCount = static_cast<long long>(vertices.size() / 3);
            face.clear();
            while (tokens.next(begin, end)) {
                long long index = 0;
                if (!parseIndex(begin, end, index))
                    return fail("\"" + std::string(begin, end) + "\" is not a vertex index");
                if (index < 0) {
                    index += vertexCount;
                    if (index < 0) {
                        return fail("\"" + std::string(begin, end) + "\" is before the first vertex");
                    }
                } else if (0 == index || index > std::numeric_limits<uint32_t>::max()) {
                    return fail("\"" + std::string(begin, end) + "\" is not a vertex index");
                } else {
                    if (index > largestIndex) {
                        largestIndex = index;
                        largestIndexLine = lineNumber;
                    }
                    --index;
                }
                face.push_back(static_cast<uint32_t>(index));
            }
            if (face.size() < 3)
                return fail("a face needs at least three vertices");
            for (size_t i = 1; i + 1 < face.size(); ++i)
                triangles.insert(triangles.end(), { face[0], face[i], face[i + 1] });
        }
    }
    if (in.bad()) {
        error = "cannot read " + path;
        return false;
    }
    const size_t vertexCount = vertices.size() / 3;
    if (vertexCount > std::numeric_limits<uint32_t>::max()) {
        error = path + ": more vertices than 32-bit indices can address";
        return false;
    }
    if (static_cast<unsigned long long>(largestIndex) > vertexCount) {
        lineNumber = largestIndexLine;
        return fail("vertex " + std::to_string(largestIndex) + " does not exist; the file has "
            + std::to_string(vertexCount));
    }
    if (triangles.empty()) {
        error = path + ": no faces";
        return false;
    }
    return true;
}

bool writeObj(const std::string& path, const autoremesher_core_result& mesh, std::string& error)
{
    FILE* file = std::fopen(path.c_str(), "wb");
    if (nullptr == file) {
        error = "cannot write " + path + ": " + std::strerror(errno);
        return false;
    }
    // MainWindow::saveMeshToFile's header: "# " APP_NAME " " APP_HUMAN_VER,
    // then "# " APP_HOMEPAGE_URL.
    std::fprintf(file, "# %s %s\n# %s\n", AUTOREMESHER_APP_NAME, AUTOREMESHER_APP_HUMAN_VER,
        AUTOREMESHER_APP_HOMEPAGE_URL);
    char x[32], y[32], z[32];
    for (size_t i = 0; i < mesh.vertex_count; ++i) {
        formatNumber(mesh.vertices[i * 3], x);
        formatNumber(mesh.vertices[i * 3 + 1], y);
        formatNumber(mesh.vertices[i * 3 + 2], z);
        std::fprintf(file, "v %s %s %s\n", x, y, z);
    }
    size_t index = 0;
    for (size_t i = 0; i < mesh.face_count; ++i) {
        std::fputc('f', file);
        for (uint32_t j = 0; j < mesh.face_sizes[i]; ++j)
            std::fprintf(file, " %lu", 1ul + mesh.face_indices[index++]);
        std::fputc('\n', file);
    }
    const bool failed = 0 != std::ferror(file);
    if (0 != std::fclose(file) || failed) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

}
