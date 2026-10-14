/*
 *  Unit test of autoremesher-cli's OBJ reader and writer (src/cli/obj_io).
 *
 *  Usage: test_obj_io <scratch directory> [source.obj]
 *
 *  With the optional second argument, also reads the real input mesh and
 *  checks its counts; that part is skipped if the file is missing.
 */
#include "cli/obj_io.h"
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

std::string scratch;

std::string writeFile(const std::string& name, const std::string& content)
{
    const std::string path = scratch + "/" + name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

std::string readFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

bool exists(const std::string& path)
{
    return std::ifstream(path).good();
}

struct Mesh {
    std::vector<double> vertices;
    std::vector<uint32_t> triangles;
    std::string error;
    bool ok = false;
};

Mesh read(const std::string& name, const std::string& content)
{
    Mesh mesh;
    // Leftovers from an earlier call must not survive a read.
    mesh.vertices = { 9.0 };
    mesh.triangles = { 9 };
    mesh.ok = AutoRemesherCli::readObj(writeFile(name, content), mesh.vertices, mesh.triangles,
        mesh.error);
    if (!mesh.ok)
        std::printf("  %s: %s\n", name.c_str(), mesh.error.c_str());
    return mesh;
}

// The reader must fail on `content`, with an error that names the line.
void checkRejected(const std::string& name, const std::string& content, const std::string& where)
{
    const Mesh mesh = read(name, content);
    CHECK(!mesh.ok);
    if (mesh.error.find(where) == std::string::npos) {
        std::fprintf(stderr, "%s: error \"%s\" does not contain \"%s\"\n", name.c_str(),
            mesh.error.c_str(), where.c_str());
        ++failures;
    }
}

const std::string unitSquare = "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n";

void testReader()
{
    // source.obj's own format: `v x y z r g b` (Rhino writes vertex colours),
    // with a comment and a blank line first.
    {
        const Mesh mesh = read("rhino.obj",
            "# Rhino\n\nv 105.67 -0.60052 44.038 0 0 0\nv 105.37 -0.53955 44.609 0 0 0\n"
            "v 105 -0.55474 44.388 0 0 0\nf 1 2 3\n");
        CHECK(mesh.ok);
        CHECK((mesh.vertices == std::vector<double> { 105.67, -0.60052, 44.038, 105.37, -0.53955,
                  44.609, 105, -0.55474, 44.388 }));
        CHECK((mesh.triangles == std::vector<uint32_t> { 0, 1, 2 }));
    }

    // Anything after the three coordinates is discarded, numeric or not, and
    // the exact decimal value of each coordinate is kept (read as a double).
    {
        const Mesh mesh = read("trailing.obj",
            "v 1 2 3 1.0\nv 4 5 6 colour\nv 0.1 1e-3 -2.5E+2 0 0 0 0 0 0\nf 1 2 3\n");
        CHECK(mesh.ok);
        CHECK((mesh.vertices == std::vector<double> { 1, 2, 3, 4, 5, 6, 0.1, 1e-3, -2.5e2 }));
    }

    // Every other statement is ignored, CRLF line ends and tabs are accepted,
    // and `f` corners may carry texture and normal indices.
    {
        const Mesh mesh = read("statements.obj",
            "mtllib a.mtl\r\no object\r\ng group\r\nusemtl material\r\ns 1\r\n"
            "v 0 0 0\r\nv 1 0 0\r\nv 1 1 0\r\nv 0 1 0\r\n"
            "vt 0 0\r\nvt 1 0\r\nvt 1 1\r\nvn 0 0 1\r\nvp 0.5\r\nl 1 2\r\np 1\r\n"
            "  f\t1/1/1 2/2/1\t 3/3/1  \r\nf 1//1 3//1 4//1\r\nf 2/2 3/3 4/3\r\n# end\r\n");
        CHECK(mesh.ok);
        CHECK((mesh.vertices == std::vector<double> { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 }));
        CHECK((mesh.triangles == std::vector<uint32_t> { 0, 1, 2, 0, 2, 3, 1, 2, 3 }));
    }

    // Polygons are split into a fan of triangles around their first corner,
    // which is what the GUI's tinyobjloader also does for a convex polygon.
    {
        const Mesh mesh = read("polygons.obj",
            unitSquare + "v 0.5 1.5 0\nf 1 2 3 4\nf 1 2 3 5 4\n");
        CHECK(mesh.ok);
        CHECK((mesh.triangles == std::vector<uint32_t> { 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 4, 0, 4,
                  3 }));
    }

    // Negative indices count back from the last vertex read so far.
    {
        const Mesh mesh = read("relative.obj",
            "v 0 0 0\nv 1 0 0\nv 1 1 0\nf -3 -2 -1\nv 0 1 0\nf -4/-1 -2/-1 -1/-1\n");
        CHECK(mesh.ok);
        CHECK((mesh.triangles == std::vector<uint32_t> { 0, 1, 2, 0, 2, 3 }));
    }

    // A face may name a vertex that is defined further down the file.
    {
        const Mesh mesh = read("forward.obj", "f 1 2 3\nv 0 0 0\nv 1 0 0\nv 1 1 0\n");
        CHECK(mesh.ok);
        CHECK((mesh.triangles == std::vector<uint32_t> { 0, 1, 2 }));
        CHECK(9 == mesh.vertices.size());
    }

    // A file without a final newline.
    {
        const Mesh mesh = read("no_newline.obj", "v 0 0 0\nv 1 0 0\nv 1 1 0\nf 1 2 3");
        CHECK(mesh.ok);
        CHECK((mesh.triangles == std::vector<uint32_t> { 0, 1, 2 }));
    }

    // Errors name the file and the line.
    const std::string triangle = "v 0 0 0\nv 1 0 0\nv 1 1 0\n";
    {
        Mesh mesh;
        CHECK(!AutoRemesherCli::readObj(scratch + "/missing.obj", mesh.vertices, mesh.triangles,
            mesh.error));
        CHECK(mesh.error.find("missing.obj") != std::string::npos);
    }
    checkRejected("short_vertex.obj", "v 0 0 0\nv 1 0\n", "short_vertex.obj:2:");
    checkRejected("text_vertex.obj", "v 0 0 0\nv 1 abc 0\n", "text_vertex.obj:2:");
    checkRejected("glued_vertex.obj", "v 0 0 0\nv 1 2.5x 0\n", "glued_vertex.obj:2:");
    checkRejected("nan_vertex.obj", "v nan 0 0\n", "nan_vertex.obj:1:");
    checkRejected("inf_vertex.obj", "v 0 0 inf\n", "inf_vertex.obj:1:");
    checkRejected("huge_vertex.obj", "v 0 0 1e999\n", "huge_vertex.obj:1:");
    checkRejected("zero_index.obj", triangle + "f 0 1 2\n", "zero_index.obj:4:");
    checkRejected("text_index.obj", triangle + "f 1 two 3\n", "text_index.obj:4:");
    checkRejected("glued_index.obj", triangle + "f 1 2x 3\n", "glued_index.obj:4:");
    checkRejected("float_index.obj", triangle + "f 1 2.0 3\n", "float_index.obj:4:");
    checkRejected("high_index.obj", triangle + "f 1 2 3\nf 1 2 4\n", "high_index.obj:5:");
    checkRejected("huge_index.obj", triangle + "f 1 2 99999999999999999999\n",
        "huge_index.obj:4:");
    checkRejected("low_relative.obj", triangle + "f -1 -2 -4\n", "low_relative.obj:4:");
    checkRejected("two_corners.obj", triangle + "f 1 2\n", "two_corners.obj:4:");
    checkRejected("no_faces.obj", triangle, "no_faces.obj");
    checkRejected("empty.obj", "", "empty.obj");
}

// A result as autoremesher_core_get_result() fills it in.
struct Result {
    std::vector<double> vertices;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> sizes;

    autoremesher_core_result get()
    {
        autoremesher_core_result result;
        result.vertices = vertices.data();
        result.vertex_count = vertices.size() / 3;
        result.face_indices = indices.data();
        result.index_count = indices.size();
        result.face_sizes = sizes.data();
        result.face_count = sizes.size();
        result.quad_count = 0;
        for (uint32_t size : sizes)
            result.quad_count += 4 == size ? 1 : 0;
        return result;
    }
};

void testWriter()
{
    // The GUI's header, from autoremesher.pro's product name, version and
    // homepage, which CMake reads; "# AutoRemesher 1.2.0" and
    // "# https://autoremesher.dust3d.org/" at the time of writing.
    const std::string header = std::string("# ") + AUTOREMESHER_APP_NAME + " "
        + AUTOREMESHER_APP_HUMAN_VER + "\n# " + AUTOREMESHER_APP_HOMEPAGE_URL + "\n";
    CHECK(0 == header.compare(0, 15, "# AutoRemesher "));

    // The exact bytes MainWindow::saveMeshToFile writes for the same mesh:
    // its two header lines, then QTextStream's default number format.  That
    // is printf's %g (6 significant digits) except that negative zero loses
    // its sign and an exact tie rounds away from zero, not to even (measured
    // against Qt 6.11's QTextStream; see tests/manual/qtextstream_probe.cpp).
    Result mesh;
    mesh.vertices = { 105.67, -0.60052, 44.038, //
        0.1 + 0.2, 1.0 / 3.0, -2.0 / 3.0, //
        1234567.0, 0.00001, -0.0, //
        0, 100000, 1e-300, //
        -67.62912345, 118.88, 2, //
        123456.5, -12345.25, 1234575 };
    mesh.indices = { 0, 1, 2, 3, 3, 2, 4, 0, 1, 2, 3, 4 };
    mesh.sizes = { 4, 3, 5 };
    const std::string expected = header + "v 105.67 -0.60052 44.038\n"
                                 "v 0.3 0.333333 -0.666667\n"
                                 "v 1.23457e+06 1e-05 0\n"
                                 "v 0 100000 1e-300\n"
                                 "v -67.6291 118.88 2\n"
                                 "v 123457 -12345.3 1.23458e+06\n"
                                 "f 1 2 3 4\n"
                                 "f 4 3 5\n"
                                 "f 1 2 3 4 5\n";
    const std::string path = scratch + "/written.obj";
    std::remove(path.c_str());
    std::string error;
    CHECK(AutoRemesherCli::writeObj(path, mesh.get(), error));
    const std::string written = readFile(path);
    if (written != expected) {
        std::fprintf(stderr, "writer output differs; expected:\n%sgot:\n%s", expected.c_str(),
            written.c_str());
        ++failures;
    }

    // What the writer writes, the reader reads back: the same positions to
    // six digits, and each polygon as a fan.
    Mesh back;
    CHECK(AutoRemesherCli::readObj(path, back.vertices, back.triangles, back.error));
    CHECK(18 == back.vertices.size());
    CHECK((back.triangles == std::vector<uint32_t> { 0, 1, 2, 0, 2, 3, 3, 2, 4, 0, 1, 2, 0, 2, 3,
              0, 3, 4 }));
    CHECK(-67.6291 == back.vertices[12]);

    // An existing file is replaced, not appended to.
    CHECK(AutoRemesherCli::writeObj(path, mesh.get(), error));
    CHECK(readFile(path) == expected);

    // An empty mesh is just the header.
    Result empty;
    CHECK(AutoRemesherCli::writeObj(path, empty.get(), error));
    CHECK(readFile(path) == header);

    // A path that cannot be opened.
    error.clear();
    CHECK(!AutoRemesherCli::writeObj(scratch + "/no/such/dir/out.obj", mesh.get(), error));
    CHECK(error.find("no/such/dir/out.obj") != std::string::npos);
    CHECK(!exists(scratch + "/no/such/dir/out.obj"));
}

// The real input mesh, if it is there.
void testSourceObj(const std::string& path)
{
    if (!exists(path)) {
        std::printf("%s not found; skipping its checks\n", path.c_str());
        return;
    }
    Mesh mesh;
    mesh.ok = AutoRemesherCli::readObj(path, mesh.vertices, mesh.triangles, mesh.error);
    CHECK(mesh.ok);
    std::printf("%s: %zu vertices, %zu triangles\n", path.c_str(), mesh.vertices.size() / 3,
        mesh.triangles.size() / 3);
    CHECK(97400 * 3 == mesh.vertices.size());
    CHECK(175566 * 3 == mesh.triangles.size());
    if (mesh.vertices.size() >= 3) {
        CHECK(105.67 == mesh.vertices[0]);
        CHECK(-0.60052 == mesh.vertices[1]);
        CHECK(44.038 == mesh.vertices[2]);
    }
}

}

int main(int argc, char** argv)
{
    if (argc != 2 && argc != 3) {
        std::fprintf(stderr, "usage: %s <scratch directory> [source.obj]\n", argv[0]);
        return 2;
    }
    scratch = argv[1];
    testReader();
    testWriter();
    if (3 == argc)
        testSourceObj(argv[2]);
    if (0 != failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
