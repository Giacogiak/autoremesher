/*
 *  Splitting a polygon mesh into an all-quad mesh, as one step of
 *  Catmull-Clark subdivision does, without its smoothing.  No Qt.
 */
#ifndef AUTOREMESHER_CORE_QUAD_SPLIT_H
#define AUTOREMESHER_CORE_QUAD_SPLIT_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace AutoRemesher {

// Splits each face into as many quads as it has corners (decision D6).
//
// `vertices` is vertexCount * 3 doubles (x, y, z); face i has faceSizes[i]
// corners, the next faceSizes[i] entries of `faceIndices` (0-based), which
// hold indexCount entries in all.  The outputs are the split mesh's vertices
// (x, y, z) and its quads (four 0-based indices each), laid out as
// autoremesher_core_split_into_quads() documents: the input vertices, then
// the edge midpoints in order of first use, then the face centroids.
//
// Returns false, leaving the outputs empty, if the face sizes do not sum to
// indexCount, a face has fewer than 3 corners, an index is out of range, or
// the result would have more than 2^32 vertices.  May throw std::bad_alloc.
bool splitIntoQuads(const double* vertices, size_t vertexCount,
    const uint32_t* faceIndices, size_t indexCount,
    const uint32_t* faceSizes, size_t faceCount,
    std::vector<double>* splitVertices,
    std::vector<uint32_t>* quadIndices);

}

#endif
