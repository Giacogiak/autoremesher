/*
 *  Vertex welding for meshes read from files that do not share vertices
 *  between faces (an OBJ triangle soup, for example).  No Qt.
 */
#ifndef AUTOREMESHER_CORE_WELD_H
#define AUTOREMESHER_CORE_WELD_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace AutoRemesher {

// Collapses vertices at the same position into one and remaps the triangles
// to match.  Positions are compared as AutoRemesher::PositionKey does, the
// key the core itself uses: each coordinate times 1e5, truncated to an
// integer.  There is no tolerance parameter (decision D2).
//
// `vertices` is vertexCount * 3 doubles (x, y, z), `triangleIndices` is
// triangleCount * 3 0-based indices.  The welded vertices keep the
// coordinates and the order of the first input vertex at each position.
// Every input vertex is welded, whether a triangle uses it or not, and every
// triangle is kept, even one that ends up with a repeated index.
//
// Returns false, leaving the outputs empty, if an index is out of range, if a
// coordinate is not finite or too large for PositionKey (|c| above about
// 9.2e13 with a 64-bit long), or if there are more than 2^32 distinct
// positions.  May throw std::bad_alloc.
bool weldVertices(const double* vertices, size_t vertexCount,
    const uint32_t* triangleIndices, size_t triangleCount,
    std::vector<double>* weldedVertices,
    std::vector<uint32_t>* weldedTriangleIndices);

}

#endif
