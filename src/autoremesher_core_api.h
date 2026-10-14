/*
 *  C ABI over AutoRemesher::AutoRemesher, for callers that cannot use the C++
 *  class directly (the C# P/Invoke demo, dlopen from other languages).  Plain
 *  C: no C++ types cross this boundary and no exception escapes it.
 *
 *  Usage:
 *
 *      autoremesher_core* h = autoremesher_core_create(vertices, vertex_count,
 *          triangle_indices, triangle_count);
 *      autoremesher_core_set_target_quad_count(h, 50000);
 *      if (autoremesher_core_remesh(h)) {
 *          autoremesher_core_result result;
 *          if (autoremesher_core_get_result(h, &result)) {
 *              ... read result ...
 *              autoremesher_core_free_result(&result);
 *          }
 *      }
 *      autoremesher_core_destroy(h);
 *
 *  Input read from a file that does not share vertices between triangles
 *  (an OBJ triangle soup) should go through autoremesher_core_weld() first,
 *  and the welded arrays to autoremesher_core_create().
 *
 *  A handle may be remeshed again after changing its settings; each
 *  autoremesher_core_remesh() call starts from the original input.  A handle
 *  must not be used from two threads at once.
 */
#ifndef AUTOREMESHER_CORE_API_H
#define AUTOREMESHER_CORE_API_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(AUTOREMESHER_CORE_BUILDING)
#define AUTOREMESHER_CORE_API __declspec(dllexport)
#else
#define AUTOREMESHER_CORE_API __declspec(dllimport)
#endif
#else
#define AUTOREMESHER_CORE_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct autoremesher_core autoremesher_core;

/* Same signature as AutoRemesher::AutoRemesherProgressHandler.  `progress` is
 * 0..1 and `status` names the current step (never NULL, may be "").  When
 * remeshing fails on invalid input, it is called once with progress 1 and the
 * reason as `status`.  Calls come from the thread that called
 * autoremesher_core_remesh() and from its worker threads, but never two at
 * once.  `status` is only valid for the duration of the call. */
typedef void (*autoremesher_core_progress_handler)(void* tag, float progress, const char* status);

/* The remeshed mesh, in arrays the library allocates and
 * autoremesher_core_free_result() releases.
 *
 * Faces are polygons, not always quads: the extractor also emits faces of 3,
 * 5, 6 and 7 vertices.  Face i has face_sizes[i] vertices, which are the next
 * face_sizes[i] entries of face_indices after those of faces 0..i-1. */
typedef struct autoremesher_core_result {
    double* vertices; /* vertex_count * 3: x, y, z of each vertex */
    size_t vertex_count;
    uint32_t* face_indices; /* index_count entries, 0-based into vertices */
    size_t index_count;
    uint32_t* face_sizes; /* face_count entries; they sum to index_count */
    size_t face_count;
    size_t quad_count; /* how many of the faces have exactly 4 vertices */
} autoremesher_core_result;

/* Copies the input mesh: `vertices` is vertex_count * 3 doubles (x, y, z),
 * `triangle_indices` is triangle_count * 3 0-based indices.  Returns NULL if
 * either array is NULL with a non-zero count, if an index is out of range, or
 * if memory runs out.  Empty input is accepted; autoremesher_core_remesh()
 * then fails. */
AUTOREMESHER_CORE_API autoremesher_core* autoremesher_core_create(const double* vertices,
    size_t vertex_count,
    const uint32_t* triangle_indices,
    size_t triangle_count);

/* NULL is accepted and ignored. */
AUTOREMESHER_CORE_API void autoremesher_core_destroy(autoremesher_core* handle);

/* Target in quads, as the GUI's "Target Quads" field.  The core takes a
 * triangle count, which is set to twice this (MainWindow's own convention).
 * Must be set before remeshing: the core's default target is 0, which makes
 * autoremesher_core_remesh() fail. */
AUTOREMESHER_CORE_API void autoremesher_core_set_target_quad_count(autoremesher_core* handle,
    size_t target_quad_count);

/* The setters below map one to one onto AutoRemesher's.  Each takes effect
 * only once called; until then the core's own default is used. */
AUTOREMESHER_CORE_API void autoremesher_core_set_scaling(autoremesher_core* handle, double scaling);
AUTOREMESHER_CORE_API void autoremesher_core_set_sharp_edge_degrees(autoremesher_core* handle,
    double degrees);
AUTOREMESHER_CORE_API void autoremesher_core_set_smooth_normal_degrees(autoremesher_core* handle,
    double degrees);
AUTOREMESHER_CORE_API void autoremesher_core_set_adaptivity(autoremesher_core* handle,
    double adaptivity);
AUTOREMESHER_CORE_API void autoremesher_core_set_anisotropy(autoremesher_core* handle,
    double anisotropy);

/* `handler` may be NULL to remove it.  `tag` is passed back to it verbatim. */
AUTOREMESHER_CORE_API void autoremesher_core_set_progress_handler(autoremesher_core* handle,
    autoremesher_core_progress_handler handler,
    void* tag);

/* Runs the remesher on the input given to autoremesher_core_create().
 * Returns 1 on success and 0 on failure, in which case any earlier result is
 * discarded too.  Blocks until done. */
AUTOREMESHER_CORE_API int autoremesher_core_remesh(autoremesher_core* handle);

/* Fills `result` with a copy of the last successful remesh.  Returns 1 on
 * success.  Returns 0, with `result` zeroed, if there is no result or memory
 * runs out.  Each successful call must be matched by one
 * autoremesher_core_free_result(); the result stays valid after the handle is
 * destroyed. */
AUTOREMESHER_CORE_API int autoremesher_core_get_result(const autoremesher_core* handle,
    autoremesher_core_result* result);

/* Frees the arrays of `result` and zeroes it, so a second call is harmless.
 * NULL is accepted and ignored. */
AUTOREMESHER_CORE_API void autoremesher_core_free_result(autoremesher_core_result* result);

/* Splits every face of `mesh` into quads, as one step of Catmull-Clark
 * subdivision splits them, but without its smoothing: an n-gon becomes n
 * quads, so a quad becomes four and the result is all quads (decision D6).
 * Needs no handle; `mesh` is typically a result of
 * autoremesher_core_get_result().
 *
 * The result's vertices are, in order: the input's, unchanged; then the
 * midpoint of every edge, in order of first use (faces in order, each face's
 * edges from corner j to corner j+1), an edge shared by two faces getting one
 * midpoint whichever way round each uses it; then the centroid (the mean of
 * the corners) of every face, in face order.  Corner j of an input face gives
 * the quad (corner j, midpoint of the edge leaving it, centroid, midpoint of
 * the edge entering it), so the winding is kept, and the quads of face i come
 * before those of face i+1.  Nothing is moved, merged or dropped: unused
 * vertices are kept, and a face with a repeated corner is split like any
 * other.  mesh->quad_count is not read.
 *
 * Returns 1 and fills `result` on success; each success must be matched by
 * one autoremesher_core_free_result().  Returns 0 if `result` is NULL.
 * Otherwise returns 0, with `result` zeroed, if `mesh` is NULL, if an array
 * is NULL with a non-zero count, if the face sizes do not sum to index_count,
 * if a face has fewer than 3 corners, if an index is out of range, if the
 * result would have more than 2^32 vertices, or if memory runs out.  An empty
 * mesh gives an empty mesh.  `mesh` is not modified. */
AUTOREMESHER_CORE_API int autoremesher_core_split_into_quads(const autoremesher_core_result* mesh,
    autoremesher_core_result* result);

/* A welded triangle mesh, in arrays the library allocates and
 * autoremesher_core_free_weld_result() releases. */
typedef struct autoremesher_core_weld_result {
    double* vertices; /* vertex_count * 3: x, y, z of each vertex */
    size_t vertex_count;
    uint32_t* triangle_indices; /* triangle_count * 3, 0-based into vertices */
    size_t triangle_count;
} autoremesher_core_weld_result;

/* Merges vertices at the same position into one and remaps the triangles to
 * match, with no tolerance parameter.  Positions are compared the way the
 * core compares them internally (AutoRemesher::PositionKey): each coordinate
 * times 1e5, truncated to an integer.  Same positions always merge; positions
 * that differ by less than 1e-5 may also merge.
 *
 * Takes the same arrays as autoremesher_core_create() and needs no handle.
 * The welded vertices keep the coordinates and order of the first input
 * vertex at each position.  Every input vertex is welded, used or not, and
 * every triangle is kept, so triangle_count is unchanged and a triangle that
 * had two corners at one position ends up with a repeated index.
 *
 * Returns 1 and fills `result` on success; each success must be matched by
 * one autoremesher_core_free_weld_result().  Returns 0 if `result` is NULL.
 * Otherwise returns 0, with `result` zeroed, if an array is NULL with a
 * non-zero count, if an index is out of range, if a coordinate is NaN, infinite or
 * beyond about 9.2e13, or if memory runs out.  Empty input gives an empty
 * mesh.  The input arrays are not modified. */
AUTOREMESHER_CORE_API int autoremesher_core_weld(const double* vertices,
    size_t vertex_count,
    const uint32_t* triangle_indices,
    size_t triangle_count,
    autoremesher_core_weld_result* result);

/* Frees the arrays of `result` and zeroes it, so a second call is harmless.
 * NULL is accepted and ignored. */
AUTOREMESHER_CORE_API void autoremesher_core_free_weld_result(autoremesher_core_weld_result* result);

#ifdef __cplusplus
}
#endif

#endif
