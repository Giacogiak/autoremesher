/*
 *  C ABI over AutoRemesher::AutoRemesher.  See autoremesher_core_api.h.
 */
#include "autoremesher_core_api.h"
#include "weld.h"
#include <AutoRemesher/AutoRemesher>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <vector>

namespace {

// malloc(0) may return null, so allocate at least one element and only treat
// null as failure when something was asked for.
void* allocate(size_t count, size_t size)
{
    return std::malloc((0 == count ? 1 : count) * size);
}

// A setting the caller may or may not have given.  Unset ones are not passed
// to the core, so its own defaults stay the single source of truth for them.
template <typename T>
struct Setting {
    bool isSet = false;
    T value = T();

    void set(T newValue)
    {
        value = newValue;
        isSet = true;
    }
};

}

struct autoremesher_core {
    std::vector<AutoRemesher::Vector3> vertices;
    std::vector<std::vector<size_t>> triangles;

    Setting<size_t> targetTriangleCount;
    Setting<double> scaling;
    Setting<double> sharpEdgeDegrees;
    Setting<double> smoothNormalDegrees;
    Setting<double> adaptivity;
    Setting<double> anisotropy;
    autoremesher_core_progress_handler progressHandler = nullptr;
    void* tag = nullptr;

    // The last successful run, or null.  A fresh AutoRemesher is made for
    // every run: the class appends to its outputs rather than clearing them,
    // so it cannot be remeshed twice.
    std::unique_ptr<AutoRemesher::AutoRemesher> remesher;
};

extern "C" {

autoremesher_core* autoremesher_core_create(const double* vertices,
    size_t vertex_count,
    const uint32_t* triangle_indices,
    size_t triangle_count)
{
    if ((nullptr == vertices && 0 != vertex_count)
        || (nullptr == triangle_indices && 0 != triangle_count))
        return nullptr;
    if (vertex_count > std::numeric_limits<size_t>::max() / 3
        || triangle_count > std::numeric_limits<size_t>::max() / 3)
        return nullptr;
    for (size_t i = 0; i < triangle_count * 3; ++i) {
        if (triangle_indices[i] >= vertex_count)
            return nullptr;
    }

    try {
        std::unique_ptr<autoremesher_core> handle(new autoremesher_core);
        handle->vertices.reserve(vertex_count);
        for (size_t i = 0; i < vertex_count; ++i)
            handle->vertices.emplace_back(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
        handle->triangles.reserve(triangle_count);
        for (size_t i = 0; i < triangle_count; ++i) {
            handle->triangles.push_back({ triangle_indices[i * 3],
                triangle_indices[i * 3 + 1],
                triangle_indices[i * 3 + 2] });
        }
        return handle.release();
    } catch (...) {
        return nullptr;
    }
}

void autoremesher_core_destroy(autoremesher_core* handle)
{
    delete handle;
}

void autoremesher_core_set_target_quad_count(autoremesher_core* handle, size_t target_quad_count)
{
    if (nullptr == handle)
        return;
    // MainWindow passes targetQuadCount * 2, and QuadMeshGenerator hands that
    // to setTargetTriangleCount() unchanged.  Saturate rather than wrap.
    const size_t maxQuads = std::numeric_limits<size_t>::max() / 2;
    handle->targetTriangleCount.set(target_quad_count > maxQuads
            ? std::numeric_limits<size_t>::max()
            : target_quad_count * 2);
}

void autoremesher_core_set_scaling(autoremesher_core* handle, double scaling)
{
    if (nullptr != handle)
        handle->scaling.set(scaling);
}

void autoremesher_core_set_sharp_edge_degrees(autoremesher_core* handle, double degrees)
{
    if (nullptr != handle)
        handle->sharpEdgeDegrees.set(degrees);
}

void autoremesher_core_set_smooth_normal_degrees(autoremesher_core* handle, double degrees)
{
    if (nullptr != handle)
        handle->smoothNormalDegrees.set(degrees);
}

void autoremesher_core_set_adaptivity(autoremesher_core* handle, double adaptivity)
{
    if (nullptr != handle)
        handle->adaptivity.set(adaptivity);
}

void autoremesher_core_set_anisotropy(autoremesher_core* handle, double anisotropy)
{
    if (nullptr != handle)
        handle->anisotropy.set(anisotropy);
}

void autoremesher_core_set_progress_handler(autoremesher_core* handle,
    autoremesher_core_progress_handler handler,
    void* tag)
{
    if (nullptr == handle)
        return;
    handle->progressHandler = handler;
    handle->tag = tag;
}

int autoremesher_core_remesh(autoremesher_core* handle)
{
    if (nullptr == handle)
        return 0;
    handle->remesher.reset();

    try {
        std::unique_ptr<AutoRemesher::AutoRemesher> remesher(
            new AutoRemesher::AutoRemesher(handle->vertices, handle->triangles));
        // Model type stays the core's default, Organic (decision D4).
        if (handle->targetTriangleCount.isSet)
            remesher->setTargetTriangleCount(handle->targetTriangleCount.value);
        if (handle->scaling.isSet)
            remesher->setScaling(handle->scaling.value);
        if (handle->sharpEdgeDegrees.isSet)
            remesher->setSharpEdgeDegrees(handle->sharpEdgeDegrees.value);
        if (handle->smoothNormalDegrees.isSet)
            remesher->setSmoothNormalDegrees(handle->smoothNormalDegrees.value);
        if (handle->adaptivity.isSet)
            remesher->setGradientAdaptivity(handle->adaptivity.value);
        if (handle->anisotropy.isSet)
            remesher->setAnisotropy(handle->anisotropy.value);
        remesher->setProgressHandler(handle->progressHandler);
        remesher->setTag(handle->tag);

        if (!remesher->remesh())
            return 0;
        // The result is handed out with 32-bit indices.
        if (remesher->remeshedVertices().size() > std::numeric_limits<uint32_t>::max())
            return 0;
        handle->remesher = std::move(remesher);
        return 1;
    } catch (...) {
        return 0;
    }
}

int autoremesher_core_get_result(const autoremesher_core* handle, autoremesher_core_result* result)
{
    if (nullptr == result)
        return 0;
    std::memset(result, 0, sizeof(*result));
    if (nullptr == handle || nullptr == handle->remesher)
        return 0;

    const auto& vertices = handle->remesher->remeshedVertices();
    const auto& faces = handle->remesher->remeshedQuads();
    size_t indexCount = 0;
    size_t quadCount = 0;
    for (const auto& face : faces) {
        indexCount += face.size();
        if (4 == face.size())
            ++quadCount;
    }

    result->vertices = static_cast<double*>(allocate(vertices.size() * 3, sizeof(double)));
    result->face_indices = static_cast<uint32_t*>(allocate(indexCount, sizeof(uint32_t)));
    result->face_sizes = static_cast<uint32_t*>(allocate(faces.size(), sizeof(uint32_t)));
    if (nullptr == result->vertices || nullptr == result->face_indices || nullptr == result->face_sizes) {
        autoremesher_core_free_result(result);
        return 0;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        result->vertices[i * 3] = vertices[i].x();
        result->vertices[i * 3 + 1] = vertices[i].y();
        result->vertices[i * 3 + 2] = vertices[i].z();
    }
    size_t next = 0;
    for (size_t i = 0; i < faces.size(); ++i) {
        result->face_sizes[i] = static_cast<uint32_t>(faces[i].size());
        for (size_t index : faces[i])
            result->face_indices[next++] = static_cast<uint32_t>(index);
    }
    result->vertex_count = vertices.size();
    result->index_count = indexCount;
    result->face_count = faces.size();
    result->quad_count = quadCount;
    return 1;
}

void autoremesher_core_free_result(autoremesher_core_result* result)
{
    if (nullptr == result)
        return;
    std::free(result->vertices);
    std::free(result->face_indices);
    std::free(result->face_sizes);
    std::memset(result, 0, sizeof(*result));
}

int autoremesher_core_weld(const double* vertices,
    size_t vertex_count,
    const uint32_t* triangle_indices,
    size_t triangle_count,
    autoremesher_core_weld_result* result)
{
    if (nullptr == result)
        return 0;
    std::memset(result, 0, sizeof(*result));
    if ((nullptr == vertices && 0 != vertex_count)
        || (nullptr == triangle_indices && 0 != triangle_count))
        return 0;

    try {
        std::vector<double> weldedVertices;
        std::vector<uint32_t> weldedTriangleIndices;
        if (!AutoRemesher::weldVertices(vertices, vertex_count, triangle_indices, triangle_count,
                &weldedVertices, &weldedTriangleIndices))
            return 0;

        result->vertices = static_cast<double*>(allocate(weldedVertices.size(), sizeof(double)));
        result->triangle_indices = static_cast<uint32_t*>(
            allocate(weldedTriangleIndices.size(), sizeof(uint32_t)));
        if (nullptr == result->vertices || nullptr == result->triangle_indices) {
            autoremesher_core_free_weld_result(result);
            return 0;
        }
        std::copy(weldedVertices.begin(), weldedVertices.end(), result->vertices);
        std::copy(weldedTriangleIndices.begin(), weldedTriangleIndices.end(), result->triangle_indices);
        result->vertex_count = weldedVertices.size() / 3;
        result->triangle_count = triangle_count;
        return 1;
    } catch (...) {
        autoremesher_core_free_weld_result(result);
        return 0;
    }
}

void autoremesher_core_free_weld_result(autoremesher_core_weld_result* result)
{
    if (nullptr == result)
        return;
    std::free(result->vertices);
    std::free(result->triangle_indices);
    std::memset(result, 0, sizeof(*result));
}

}
