/*
 *  OBJ reading and writing for autoremesher-cli, without Qt.
 *
 *  The reader takes what the GUI's loader (tinyobjloader, in
 *  MainWindow::loadObj) takes from a mesh: vertex positions and faces.  The
 *  writer writes what MainWindow::saveMeshToFile writes, byte for byte, so the
 *  two tools' outputs can be compared with diff.
 *
 *  Numbers are read with strtod and written with snprintf, so both assume the
 *  C locale; nothing in autoremesher-cli calls setlocale.
 */
#ifndef AUTOREMESHER_CLI_OBJ_IO_H
#define AUTOREMESHER_CLI_OBJ_IO_H

#include <autoremesher_core_api.h>
#include <cstdint>
#include <string>
#include <vector>

namespace AutoRemesherCli {

// Reads an OBJ file into `vertices` (x, y, z per `v` line, as doubles) and
// `triangles` (three 0-based indices into `vertices` per triangle), replacing
// their contents.  Nothing is welded or deduplicated.
//
// - A `v` line's first three numbers are its position.  Anything after them,
//   such as the vertex colour in source.obj's `v x y z 0 0 0`, is discarded.
// - An `f` line's corners may be `v`, `v/vt`, `v//vn` or `v/vt/vn`; only `v`
//   is used.  Negative indices count back from the last vertex read so far.
//   A polygon becomes a fan of triangles around its first corner.
// - Every other line (`vt`, `vn`, `g`, `usemtl`, comments, ...) is ignored.
//
// Returns false and sets `error`, naming the file and line, if the file
// cannot be read, a coordinate is not a finite number, a face has fewer than
// three corners or an index that is not a vertex, or there are no faces.
bool readObj(const std::string& path, std::vector<double>& vertices,
    std::vector<uint32_t>& triangles, std::string& error);

// Writes the GUI's two header lines, a `v x y z` line per vertex and an
// `f a b c ...` line of 1-based indices per face, as many as the face has.
// Numbers are formatted as Qt's QTextStream formats a double by default: six
// significant digits, as printf's %g, except that negative zero is written
// as 0 and an exact tie rounds away from zero.  Returns false and sets
// `error` if the file cannot be written.
bool writeObj(const std::string& path, const autoremesher_core_result& mesh, std::string& error);

// One number as writeObj writes it, which is how QTextStream writes a double
// by default.  The --report file uses it too, as the GUI's is a QTextStream.
std::string formatQtDouble(double value);

}

#endif
