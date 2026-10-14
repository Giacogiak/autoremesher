/*
 *  See quad_split.h.
 */
#include "quad_split.h"
#include <unordered_map>
#include <utility>

namespace AutoRemesher {

bool splitIntoQuads(const double* vertices, size_t vertexCount,
    const uint32_t* faceIndices, size_t indexCount,
    const uint32_t* faceSizes, size_t faceCount,
    std::vector<double>* splitVertices,
    std::vector<uint32_t>* quadIndices)
{
    splitVertices->clear();
    quadIndices->clear();

    // Check everything before reading any face's corners.
    size_t corners = 0;
    for (size_t i = 0; i < faceCount; ++i) {
        if (faceSizes[i] < 3 || faceSizes[i] > indexCount - corners)
            return false;
        corners += faceSizes[i];
    }
    if (corners != indexCount)
        return false;
    for (size_t i = 0; i < indexCount; ++i) {
        if (faceIndices[i] >= vertexCount)
            return false;
    }

    // One midpoint per undirected edge, numbered in order of first use.
    // There are at most indexCount edges.
    const uint64_t firstEdgeVertex = vertexCount;
    std::unordered_map<uint64_t, uint32_t> edgeVertex;
    edgeVertex.reserve(indexCount);
    std::vector<uint32_t> leaving(indexCount);
    std::vector<uint32_t> edgeEnds;
    size_t faceStart = 0;
    for (size_t i = 0; i < faceCount; ++i) {
        const uint32_t* face = faceIndices + faceStart;
        for (uint32_t j = 0; j < faceSizes[i]; ++j) {
            uint64_t a = face[j], b = face[(j + 1) % faceSizes[i]];
            if (a > b)
                std::swap(a, b);
            const auto inserted = edgeVertex.emplace((a << 32) | b, 0);
            if (inserted.second) {
                const uint64_t index = firstEdgeVertex + edgeEnds.size() / 2;
                if (index > UINT32_MAX)
                    return false;
                inserted.first->second = static_cast<uint32_t>(index);
                edgeEnds.push_back(static_cast<uint32_t>(a));
                edgeEnds.push_back(static_cast<uint32_t>(b));
            }
            leaving[faceStart + j] = inserted.first->second;
        }
        faceStart += faceSizes[i];
    }
    const uint64_t firstFaceVertex = firstEdgeVertex + edgeEnds.size() / 2;
    // Every index must fit in a uint32_t.
    if (firstFaceVertex + faceCount > uint64_t(UINT32_MAX) + 1)
        return false;

    std::vector<double> outVertices;
    outVertices.reserve((firstFaceVertex + faceCount) * 3);
    outVertices.assign(vertices, vertices + vertexCount * 3);
    for (size_t e = 0; e < edgeEnds.size(); e += 2) {
        const double* a = vertices + edgeEnds[e] * 3;
        const double* b = vertices + edgeEnds[e + 1] * 3;
        for (int k = 0; k < 3; ++k)
            outVertices.push_back((a[k] + b[k]) / 2);
    }
    std::vector<uint32_t> outQuads;
    outQuads.reserve(indexCount * 4);
    faceStart = 0;
    for (size_t i = 0; i < faceCount; ++i) {
        const uint32_t size = faceSizes[i];
        const uint32_t* face = faceIndices + faceStart;
        double centroid[3] = { 0, 0, 0 };
        for (uint32_t j = 0; j < size; ++j) {
            for (int k = 0; k < 3; ++k)
                centroid[k] += vertices[face[j] * 3 + k];
        }
        for (int k = 0; k < 3; ++k)
            outVertices.push_back(centroid[k] / size);
        const uint32_t center = static_cast<uint32_t>(firstFaceVertex + i);
        for (uint32_t j = 0; j < size; ++j) {
            outQuads.push_back(face[j]);
            outQuads.push_back(leaving[faceStart + j]);
            outQuads.push_back(center);
            outQuads.push_back(leaving[faceStart + (j + size - 1) % size]);
        }
        faceStart += size;
    }

    splitVertices->swap(outVertices);
    quadIndices->swap(outQuads);
    return true;
}

}
