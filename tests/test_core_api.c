/*
 *  Tests the C API of autoremesher_core on a small synthetic sphere.  Written
 *  in C, not C++, so that it also checks the header is valid C.
 */
#include <autoremesher_core_api.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                #condition);                                               \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

/* A closed UV sphere of radius 1: `rings` latitude bands of `segments` quads
 * each, split into triangles, with one vertex at each pole. */
enum { segments = 32,
    rings = 16,
    sphereVertexCount = 2 + (rings - 1) * segments,
    sphereTriangleCount = 2 * segments * (rings - 1) };

static double sphereVertices[sphereVertexCount * 3];
static uint32_t sphereTriangles[sphereTriangleCount * 3];

static void buildSphere(void)
{
    const double pi = 3.14159265358979323846;
    size_t v = 0;
    size_t t = 0;
    uint32_t northPole, southPole;
    int ring, segment;

    northPole = (uint32_t)v;
    sphereVertices[v * 3] = 0.0;
    sphereVertices[v * 3 + 1] = 1.0;
    sphereVertices[v * 3 + 2] = 0.0;
    ++v;
    for (ring = 1; ring < rings; ++ring) {
        const double theta = pi * ring / rings;
        for (segment = 0; segment < segments; ++segment) {
            const double phi = 2.0 * pi * segment / segments;
            sphereVertices[v * 3] = sin(theta) * cos(phi);
            sphereVertices[v * 3 + 1] = cos(theta);
            sphereVertices[v * 3 + 2] = sin(theta) * sin(phi);
            ++v;
        }
    }
    southPole = (uint32_t)v;
    sphereVertices[v * 3] = 0.0;
    sphereVertices[v * 3 + 1] = -1.0;
    sphereVertices[v * 3 + 2] = 0.0;
    ++v;

#define RING_VERTEX(r, s) ((uint32_t)(1 + ((r)-1) * segments + ((s) % segments)))
#define TRIANGLE(a, b, c)            \
    do {                             \
        sphereTriangles[t * 3] = a;     \
        sphereTriangles[t * 3 + 1] = b; \
        sphereTriangles[t * 3 + 2] = c; \
        ++t;                         \
    } while (0)
    /* Wound so the normals point outwards. */
    for (segment = 0; segment < segments; ++segment) {
        TRIANGLE(northPole, RING_VERTEX(1, segment + 1), RING_VERTEX(1, segment));
        TRIANGLE(southPole, RING_VERTEX(rings - 1, segment), RING_VERTEX(rings - 1, segment + 1));
    }
    for (ring = 1; ring < rings - 1; ++ring) {
        for (segment = 0; segment < segments; ++segment) {
            TRIANGLE(RING_VERTEX(ring, segment), RING_VERTEX(ring, segment + 1), RING_VERTEX(ring + 1, segment));
            TRIANGLE(RING_VERTEX(ring, segment + 1), RING_VERTEX(ring + 1, segment + 1), RING_VERTEX(ring + 1, segment));
        }
    }
#undef TRIANGLE
#undef RING_VERTEX

    if (v != sphereVertexCount || t != sphereTriangleCount) {
        fprintf(stderr, "sphere built wrong: %zu vertices, %zu triangles\n", v, t);
        exit(2);
    }
}

/* The sphere as a triangle soup: each triangle with its own three vertices,
 * as an OBJ exporter that does not share vertices would write it. */
static double soupVertices[sphereTriangleCount * 3 * 3];
static uint32_t soupTriangles[sphereTriangleCount * 3];

static void buildSoup(void)
{
    size_t i;
    for (i = 0; i < sphereTriangleCount * 3; ++i) {
        memcpy(&soupVertices[i * 3], &sphereVertices[sphereTriangles[i] * 3], 3 * sizeof(double));
        soupTriangles[i] = (uint32_t)i;
    }
}

typedef struct {
    int calls;
    int outOfRange;
    int nullStatus;
    float lastProgress;
    char lastStatus[256];
} ProgressLog;

static void recordProgress(void* tag, float progress, const char* status)
{
    ProgressLog* log = (ProgressLog*)tag;
    ++log->calls;
    if (progress < 0.0f || progress > 1.0f)
        ++log->outOfRange;
    log->lastProgress = progress;
    if (NULL == status) {
        ++log->nullStatus;
        log->lastStatus[0] = '\0';
    } else {
        strncpy(log->lastStatus, status, sizeof(log->lastStatus) - 1);
        log->lastStatus[sizeof(log->lastStatus) - 1] = '\0';
    }
}

static int isZeroed(const autoremesher_core_result* result)
{
    return NULL == result->vertices && 0 == result->vertex_count
        && NULL == result->face_indices && 0 == result->index_count
        && NULL == result->face_sizes && 0 == result->face_count
        && 0 == result->quad_count;
}

/* Checks the result is a well-formed polygon mesh. */
static void checkResult(const autoremesher_core_result* result)
{
    size_t i, indexSum = 0, quads = 0;
    CHECK(result->vertex_count > 0);
    CHECK(result->face_count > 0);
    CHECK(result->quad_count > 0);
    CHECK(result->quad_count <= result->face_count);
    for (i = 0; i < result->face_count; ++i) {
        CHECK(result->face_sizes[i] >= 3);
        indexSum += result->face_sizes[i];
        if (4 == result->face_sizes[i])
            ++quads;
    }
    CHECK(indexSum == result->index_count);
    CHECK(quads == result->quad_count);
    for (i = 0; i < result->index_count; ++i)
        CHECK(result->face_indices[i] < result->vertex_count);
    /* The sphere has radius 1, so the remeshed one should be close to it. */
    for (i = 0; i < result->vertex_count; ++i) {
        const double* p = &result->vertices[i * 3];
        const double radius = sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        CHECK(radius > 0.9 && radius < 1.1);
    }
}

int main(void)
{
    autoremesher_core* handle;
    autoremesher_core_result result, again;
    ProgressLog log;
    const uint32_t badTriangle[3] = { 0, 1, 3 };

    buildSphere();

    /* Bad input is refused. */
    CHECK(NULL == autoremesher_core_create(NULL, 3, badTriangle, 1));
    CHECK(NULL == autoremesher_core_create(sphereVertices, 3, NULL, 1));
    CHECK(NULL == autoremesher_core_create(sphereVertices, 3, badTriangle, 1));

    /* NULL is harmless everywhere. */
    autoremesher_core_destroy(NULL);
    autoremesher_core_free_result(NULL);
    autoremesher_core_set_target_quad_count(NULL, 1);
    autoremesher_core_set_progress_handler(NULL, recordProgress, &log);
    CHECK(0 == autoremesher_core_remesh(NULL));
    CHECK(0 == autoremesher_core_get_result(NULL, NULL));
    memset(&result, 0xff, sizeof(result));
    CHECK(0 == autoremesher_core_get_result(NULL, &result));
    CHECK(isZeroed(&result));

    /* Empty input is accepted but cannot be remeshed. */
    handle = autoremesher_core_create(NULL, 0, NULL, 0);
    CHECK(NULL != handle);
    autoremesher_core_set_target_quad_count(handle, 100);
    CHECK(0 == autoremesher_core_remesh(handle));
    autoremesher_core_destroy(handle);

    handle = autoremesher_core_create(sphereVertices, sphereVertexCount,
        sphereTriangles, sphereTriangleCount);
    CHECK(NULL != handle);
    if (NULL == handle)
        return 1;

    memset(&log, 0, sizeof(log));
    autoremesher_core_set_progress_handler(handle, recordProgress, &log);

    /* No result before a remesh. */
    memset(&result, 0xff, sizeof(result));
    CHECK(0 == autoremesher_core_get_result(handle, &result));
    CHECK(isZeroed(&result));

    /* Without a target the core refuses, and says why through the handler. */
    CHECK(0 == autoremesher_core_remesh(handle));
    CHECK(1 == log.calls);
    CHECK(1.0f == log.lastProgress);
    CHECK(NULL != strstr(log.lastStatus, "target triangle count"));
    CHECK(0 == autoremesher_core_get_result(handle, &result));

    /* Every setter, at the GUI's default values. */
    autoremesher_core_set_target_quad_count(handle, 300);
    autoremesher_core_set_scaling(handle, 1.0);
    autoremesher_core_set_sharp_edge_degrees(handle, 90.0);
    autoremesher_core_set_smooth_normal_degrees(handle, 0.0);
    autoremesher_core_set_adaptivity(handle, 1.0);
    autoremesher_core_set_anisotropy(handle, 1.0);

    memset(&log, 0, sizeof(log));
    CHECK(1 == autoremesher_core_remesh(handle));
    CHECK(log.calls > 1);
    CHECK(0 == log.outOfRange);
    CHECK(0 == log.nullStatus);
    CHECK(1.0f == log.lastProgress);
    CHECK(0 == strcmp(log.lastStatus, "Done"));

    CHECK(1 == autoremesher_core_get_result(handle, &result));
    checkResult(&result);
    printf("sphere: %d vertices, %d triangles -> %zu vertices, %zu faces, %zu quads\n",
        (int)sphereVertexCount, (int)sphereTriangleCount,
        result.vertex_count, result.face_count, result.quad_count);

    /* A second result is an independent copy of the same mesh. */
    CHECK(1 == autoremesher_core_get_result(handle, &again));
    CHECK(again.vertex_count == result.vertex_count);
    CHECK(again.face_count == result.face_count);
    CHECK(again.vertices != result.vertices);
    autoremesher_core_free_result(&again);

    /* Remeshing again starts from the input, not from the last output. */
    autoremesher_core_set_progress_handler(handle, NULL, NULL);
    memset(&log, 0, sizeof(log));
    CHECK(1 == autoremesher_core_remesh(handle));
    CHECK(0 == log.calls);
    CHECK(1 == autoremesher_core_get_result(handle, &again));
    checkResult(&again);
    CHECK(again.vertex_count < result.vertex_count * 3 / 2);
    CHECK(again.face_count < result.face_count * 3 / 2);
    autoremesher_core_free_result(&again);

    /* A bigger target gives more quads. */
    autoremesher_core_set_target_quad_count(handle, 1200);
    CHECK(1 == autoremesher_core_remesh(handle));
    CHECK(1 == autoremesher_core_get_result(handle, &again));
    checkResult(&again);
    printf("target 1200 quads: %zu faces, %zu quads\n", again.face_count, again.quad_count);
    CHECK(again.quad_count > result.quad_count * 2);
    autoremesher_core_free_result(&again);

    /* A failed remesh drops the earlier result. */
    autoremesher_core_set_target_quad_count(handle, 0);
    CHECK(0 == autoremesher_core_remesh(handle));
    CHECK(0 == autoremesher_core_get_result(handle, &again));
    CHECK(isZeroed(&again));

    /* The result outlives the handle, and freeing twice is harmless. */
    autoremesher_core_destroy(handle);
    checkResult(&result);
    autoremesher_core_free_result(&result);
    CHECK(isZeroed(&result));
    autoremesher_core_free_result(&result);

    /* Welding the soup gives back the sphere's vertices, every triangle at
     * the same corner positions, and a mesh that remeshes. */
    {
        autoremesher_core_weld_result welded;
        size_t i;
        buildSoup();
        CHECK(1 == autoremesher_core_weld(soupVertices, sphereTriangleCount * 3,
                       soupTriangles, sphereTriangleCount, &welded));
        CHECK(sphereVertexCount == welded.vertex_count);
        CHECK(sphereTriangleCount == welded.triangle_count);
        for (i = 0; i < welded.triangle_count * 3; ++i) {
            CHECK(welded.triangle_indices[i] < welded.vertex_count);
            CHECK(0 == memcmp(&welded.vertices[welded.triangle_indices[i] * 3],
                           &soupVertices[i * 3], 3 * sizeof(double)));
        }
        printf("soup: %d vertices -> %zu welded\n", (int)(sphereTriangleCount * 3),
            welded.vertex_count);

        handle = autoremesher_core_create(welded.vertices, welded.vertex_count,
            welded.triangle_indices, welded.triangle_count);
        autoremesher_core_free_weld_result(&welded);
        CHECK(NULL != handle);
        autoremesher_core_set_target_quad_count(handle, 300);
        CHECK(1 == autoremesher_core_remesh(handle));
        CHECK(1 == autoremesher_core_get_result(handle, &result));
        checkResult(&result);
        printf("welded soup, target 300 quads: %zu faces, %zu quads\n",
            result.face_count, result.quad_count);
        autoremesher_core_free_result(&result);
        autoremesher_core_destroy(handle);
    }

    if (0 != failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
