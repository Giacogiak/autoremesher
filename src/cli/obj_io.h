/*
 *  OBJ reading and writing for autoremesher-cli.  Provisional: the minimum
 *  needed to run the pipeline on source.obj, lifted from tests/test_core.cpp.
 *  M2 Task 2 replaces it with the real, tested reader and writer.
 */
#ifndef AUTOREMESHER_CLI_OBJ_IO_H
#define AUTOREMESHER_CLI_OBJ_IO_H

#include <autoremesher_core_api.h>
#include <cstdint>
#include <string>
#include <vector>

namespace AutoRemesherCli {

// Reads the positions of `v` lines and the vertex indices of `f` lines,
// fan-triangulating polygons; everything else, including tokens after a
// vertex's three coordinates, is ignored.  Returns false and sets `error` if
// the file cannot be read or a vertex or face index is malformed.
bool readObj(const std::string& path, std::vector<double>& vertices,
    std::vector<uint32_t>& triangles, std::string& error);

// Writes `v x y z` lines, then one `f` line of 1-based indices per face, with
// as many indices as the face has.
bool writeObj(const std::string& path, const autoremesher_core_result& mesh, std::string& error);

}

#endif
