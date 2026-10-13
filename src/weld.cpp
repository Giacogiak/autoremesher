/*
 *  See weld.h.
 */
#include "weld.h"
#include <AutoRemesher/PositionKey>
#include <cmath>
#include <limits>
#include <map>

namespace AutoRemesher {

namespace {

    // PositionKey casts coordinate * 1e5 to long, which is undefined for NaN
    // and for anything that does not fit.  One unit of margin covers rounding.
    const double maxCoordinate = static_cast<double>(std::numeric_limits<long>::max() / 100000 - 1);

    bool isKeyable(double coordinate)
    {
        // False for NaN too.
        return std::fabs(coordinate) <= maxCoordinate;
    }

}

bool weldVertices(const double* vertices, size_t vertexCount,
    const uint32_t* triangleIndices, size_t triangleCount,
    std::vector<double>* weldedVertices,
    std::vector<uint32_t>* weldedTriangleIndices)
{
    weldedVertices->clear();
    weldedTriangleIndices->clear();

    if (vertexCount > std::numeric_limits<size_t>::max() / 3
        || triangleCount > std::numeric_limits<size_t>::max() / 3)
        return false;
    for (size_t i = 0; i < vertexCount * 3; ++i) {
        if (!isKeyable(vertices[i]))
            return false;
    }
    for (size_t i = 0; i < triangleCount * 3; ++i) {
        if (triangleIndices[i] >= vertexCount)
            return false;
    }

    std::map<PositionKey, uint32_t> positionToIndex;
    std::vector<uint32_t> remap(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i) {
        const double* position = &vertices[i * 3];
        const PositionKey key(position[0], position[1], position[2]);
        auto found = positionToIndex.find(key);
        if (found == positionToIndex.end()) {
            // Unreferenced vertices are welded too, so there can be more
            // positions than a 32-bit index can name.
            const size_t next = weldedVertices->size() / 3;
            if (next > std::numeric_limits<uint32_t>::max()) {
                weldedVertices->clear();
                return false;
            }
            found = positionToIndex.emplace(key, static_cast<uint32_t>(next)).first;
            weldedVertices->insert(weldedVertices->end(), position, position + 3);
        }
        remap[i] = found->second;
    }

    weldedTriangleIndices->resize(triangleCount * 3);
    for (size_t i = 0; i < triangleCount * 3; ++i)
        (*weldedTriangleIndices)[i] = remap[triangleIndices[i]];
    return true;
}

}
