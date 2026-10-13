/*
 *  Tests autoremesher_core_weld() on small hand-built meshes.  Written in C,
 *  like test_core_api.c, so that it also checks the header is valid C.
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

static int isZeroed(const autoremesher_core_weld_result* result)
{
    return NULL == result->vertices && 0 == result->vertex_count
        && NULL == result->triangle_indices && 0 == result->triangle_count;
}

/* Welds and checks the output against the expected arrays exactly. */
static void checkWeld(const char* name,
    const double* vertices, size_t vertexCount,
    const uint32_t* triangles, size_t triangleCount,
    const double* expectedVertices, size_t expectedVertexCount,
    const uint32_t* expectedTriangles)
{
    autoremesher_core_weld_result result;
    size_t i;

    memset(&result, 0xff, sizeof(result));
    CHECK(1 == autoremesher_core_weld(vertices, vertexCount, triangles, triangleCount, &result));
    CHECK(result.vertex_count == expectedVertexCount);
    CHECK(result.triangle_count == triangleCount);
    if (result.vertex_count == expectedVertexCount) {
        for (i = 0; i < expectedVertexCount * 3; ++i)
            CHECK(result.vertices[i] == expectedVertices[i]);
    }
    if (result.triangle_count == triangleCount) {
        for (i = 0; i < triangleCount * 3; ++i)
            CHECK(result.triangle_indices[i] == expectedTriangles[i]);
    }
    printf("%s: %zu vertices -> %zu\n", name, vertexCount, result.vertex_count);
    autoremesher_core_free_weld_result(&result);
    CHECK(isZeroed(&result));
}

/* Expects the weld to be refused, with the result left zeroed. */
static void checkRefused(const double* vertices, size_t vertexCount,
    const uint32_t* triangles, size_t triangleCount)
{
    autoremesher_core_weld_result result;
    memset(&result, 0xff, sizeof(result));
    CHECK(0 == autoremesher_core_weld(vertices, vertexCount, triangles, triangleCount, &result));
    CHECK(isZeroed(&result));
}

int main(void)
{
    autoremesher_core_weld_result result;

    /* A unit square as a triangle soup: each triangle has its own copies of
     * its corners.  Welding keeps the first copy of each position, in order
     * of first appearance. */
    {
        const double soup[] = {
            0, 0, 0, 1, 0, 0, 1, 1, 0, /* A B C */
            0, 0, 0, 1, 1, 0, 0, 1, 0 /* A C D */
        };
        const uint32_t soupTriangles[] = { 0, 1, 2, 3, 4, 5 };
        const double welded[] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        const uint32_t weldedTriangles[] = { 0, 1, 2, 0, 2, 3 };
        double copy[sizeof(soup) / sizeof(soup[0])];
        memcpy(copy, soup, sizeof(soup));
        checkWeld("square soup", soup, 6, soupTriangles, 2, welded, 4, weldedTriangles);
        /* The input is not touched. */
        CHECK(0 == memcmp(copy, soup, sizeof(soup)));
    }

    /* A mesh with no duplicate positions comes back unchanged. */
    {
        const double vertices[] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        const uint32_t triangles[] = { 0, 1, 2, 0, 2, 3 };
        checkWeld("already welded", vertices, 4, triangles, 2, vertices, 4, triangles);
    }

    /* Triangles may index vertices in any order; the output vertex order is
     * the input order of first occurrence, not the order triangles use them. */
    {
        const double vertices[] = { 5, 5, 5, 0, 0, 0, 1, 0, 0, 5, 5, 5, 0, 1, 0 };
        const uint32_t triangles[] = { 4, 2, 3, 1, 2, 0 };
        const double welded[] = { 5, 5, 5, 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        const uint32_t weldedTriangles[] = { 3, 2, 0, 1, 2, 0 };
        checkWeld("out of order", vertices, 5, triangles, 2, welded, 4, weldedTriangles);
    }

    /* Vertices no triangle uses are still welded and kept: the weld works on
     * positions only and does not clean the mesh up. */
    {
        const double vertices[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 7, 7, 7 };
        const uint32_t triangles[] = { 0, 1, 2 };
        const double welded[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0, 7, 7, 7 };
        checkWeld("unreferenced", vertices, 5, triangles, 1, welded, 4, triangles);
    }

    /* A triangle that already had two corners at one position keeps all three
     * entries; it is not dropped. */
    {
        const double vertices[] = { 0, 0, 0, 0, 0, 0, 1, 0, 0 };
        const uint32_t triangles[] = { 0, 1, 2 };
        const double welded[] = { 0, 0, 0, 1, 0, 0 };
        const uint32_t weldedTriangles[] = { 0, 0, 1 };
        checkWeld("degenerate", vertices, 3, triangles, 1, welded, 2, weldedTriangles);
    }

    /* -0.0 and 0.0 are the same position. */
    {
        const double vertices[] = { 0.0, 1, 2, -0.0, 1, 2, 3, 4, 5 };
        const uint32_t triangles[] = { 0, 1, 2 };
        const double welded[] = { 0.0, 1, 2, 3, 4, 5 };
        const uint32_t weldedTriangles[] = { 0, 0, 1 };
        checkWeld("signed zero", vertices, 3, triangles, 1, welded, 2, weldedTriangles);
    }

    /* Close but different positions are not merged (no tolerance, D2). */
    {
        const double vertices[] = { 1, 1, 1, 1.0001, 1, 1, 1, 1, 1.001, 1, 0.9999, 1 };
        const uint32_t triangles[] = { 0, 1, 2, 0, 2, 3 };
        checkWeld("near misses", vertices, 4, triangles, 2, vertices, 4, triangles);
    }

    /* Empty input is fine and gives an empty mesh. */
    memset(&result, 0xff, sizeof(result));
    CHECK(1 == autoremesher_core_weld(NULL, 0, NULL, 0, &result));
    CHECK(0 == result.vertex_count);
    CHECK(0 == result.triangle_count);
    autoremesher_core_free_weld_result(&result);
    CHECK(isZeroed(&result));

    /* Bad input is refused. */
    {
        const double vertices[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        const uint32_t triangles[] = { 0, 1, 2 };
        const uint32_t badTriangle[] = { 0, 1, 3 };
        double notANumber[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        double infinite[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        double huge[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        notANumber[4] = NAN;
        infinite[8] = -INFINITY;
        huge[3] = 1e15;

        CHECK(0 == autoremesher_core_weld(vertices, 3, triangles, 1, NULL));
        checkRefused(NULL, 3, triangles, 1);
        checkRefused(vertices, 3, NULL, 1);
        checkRefused(vertices, 3, badTriangle, 1);
        checkRefused(notANumber, 3, triangles, 1);
        checkRefused(infinite, 3, triangles, 1);
        checkRefused(huge, 3, triangles, 1);
        /* An unreferenced vertex is still checked. */
        checkRefused(huge, 3, NULL, 0);
    }

    /* NULL is harmless, and so is freeing twice. */
    autoremesher_core_free_weld_result(NULL);
    autoremesher_core_free_weld_result(&result);

    if (0 != failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
