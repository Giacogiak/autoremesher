/*
 *  Tests autoremesher_core_split_into_quads() on small hand-built meshes.
 *  Written in C, like test_weld.c, so that it also checks the header is
 *  valid C.
 */
#include <autoremesher_core_api.h>
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

static int isZeroed(const autoremesher_core_result* result)
{
    return NULL == result->vertices && 0 == result->vertex_count
        && NULL == result->face_indices && 0 == result->index_count
        && NULL == result->face_sizes && 0 == result->face_count && 0 == result->quad_count;
}

/* A mesh over caller-owned arrays, in the layout get_result() returns. */
static autoremesher_core_result makeMesh(double* vertices, size_t vertexCount,
    uint32_t* indices, uint32_t* sizes, size_t faceCount)
{
    autoremesher_core_result mesh;
    size_t i;
    memset(&mesh, 0, sizeof(mesh));
    mesh.vertices = vertices;
    mesh.vertex_count = vertexCount;
    mesh.face_indices = indices;
    mesh.face_sizes = sizes;
    mesh.face_count = faceCount;
    for (i = 0; i < faceCount; ++i) {
        mesh.index_count += sizes[i];
        if (4 == sizes[i])
            ++mesh.quad_count;
    }
    return mesh;
}

/* Every output face is a quad, and the counts agree with each other. */
static void checkAllQuads(const autoremesher_core_result* result, size_t expectedVertexCount,
    size_t expectedFaceCount)
{
    size_t i;
    CHECK(result->vertex_count == expectedVertexCount);
    CHECK(result->face_count == expectedFaceCount);
    CHECK(result->quad_count == expectedFaceCount);
    CHECK(result->index_count == 4 * expectedFaceCount);
    for (i = 0; i < result->face_count; ++i)
        CHECK(4 == result->face_sizes[i]);
    for (i = 0; i < result->index_count; ++i)
        CHECK(result->face_indices[i] < result->vertex_count);
}

/* Splits and checks the output against the expected arrays exactly. */
static void checkSplit(const char* name, const autoremesher_core_result* mesh,
    const double* expectedVertices, size_t expectedVertexCount,
    const uint32_t* expectedQuads, size_t expectedQuadCount)
{
    autoremesher_core_result result;
    size_t i;

    memset(&result, 0xff, sizeof(result));
    CHECK(1 == autoremesher_core_split_into_quads(mesh, &result));
    checkAllQuads(&result, expectedVertexCount, expectedQuadCount);
    if (result.vertex_count == expectedVertexCount) {
        for (i = 0; i < expectedVertexCount * 3; ++i)
            CHECK(result.vertices[i] == expectedVertices[i]);
    }
    if (result.face_count == expectedQuadCount) {
        for (i = 0; i < expectedQuadCount * 4; ++i)
            CHECK(result.face_indices[i] == expectedQuads[i]);
    }
    printf("%s: %zu faces, %zu vertices -> %zu quads, %zu vertices\n", name, mesh->face_count,
        mesh->vertex_count, result.face_count, result.vertex_count);
    autoremesher_core_free_result(&result);
    CHECK(isZeroed(&result));
}

/* Expects the split to be refused, with the result left zeroed. */
static void checkRefused(const char* name, const autoremesher_core_result* mesh)
{
    autoremesher_core_result result;
    memset(&result, 0xff, sizeof(result));
    CHECK(0 == autoremesher_core_split_into_quads(mesh, &result));
    CHECK(isZeroed(&result));
    printf("%s: refused\n", name);
}

int main(void)
{
    autoremesher_core_result result;

    /* One quad becomes four.  The output vertices are the input's, then one
     * per edge (its midpoint) in order of first use, then one per face (its
     * centroid).  Corner j of a face gives the quad (corner j, midpoint of
     * the edge leaving it, centroid, midpoint of the edge entering it), so
     * the winding is kept. */
    {
        double vertices[] = { 0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0 };
        uint32_t indices[] = { 0, 1, 2, 3 };
        uint32_t sizes[] = { 4 };
        const double expected[] = {
            0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0, /* corners */
            1, 0, 0, 2, 1, 0, 1, 2, 0, 0, 1, 0, /* edges 01 12 23 30 */
            1, 1, 0 /* centroid */
        };
        const uint32_t quads[] = { 0, 4, 8, 7, 1, 5, 8, 4, 2, 6, 8, 5, 3, 7, 8, 6 };
        double copy[12];
        autoremesher_core_result mesh = makeMesh(vertices, 4, indices, sizes, 1);
        memcpy(copy, vertices, sizeof(vertices));
        checkSplit("quad", &mesh, expected, 9, quads, 4);
        /* The input is not touched. */
        CHECK(0 == memcmp(copy, vertices, sizeof(vertices)));
        CHECK(0 == indices[0] && 3 == indices[3] && 4 == sizes[0]);
    }

    /* A triangle becomes three quads. */
    {
        double vertices[] = { 0, 0, 0, 3, 0, 0, 0, 3, 0 };
        uint32_t indices[] = { 0, 1, 2 };
        uint32_t sizes[] = { 3 };
        const double expected[] = {
            0, 0, 0, 3, 0, 0, 0, 3, 0,
            1.5, 0, 0, 1.5, 1.5, 0, 0, 1.5, 0,
            1, 1, 0
        };
        const uint32_t quads[] = { 0, 3, 6, 5, 1, 4, 6, 3, 2, 5, 6, 4 };
        autoremesher_core_result mesh = makeMesh(vertices, 3, indices, sizes, 1);
        checkSplit("triangle", &mesh, expected, 7, quads, 3);
    }

    /* Two triangles that share an edge share its midpoint, whichever way
     * round each one uses the edge, so the result has no T-junction. */
    {
        double vertices[] = { 0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0 };
        uint32_t indices[] = { 0, 1, 2, 0, 2, 3 };
        uint32_t sizes[] = { 3, 3 };
        const double expected[] = {
            0, 0, 0, 2, 0, 0, 2, 2, 0, 0, 2, 0,
            1, 0, 0, 2, 1, 0, 1, 1, 0, 1, 2, 0, 0, 1, 0, /* edges 01 12 20 23 30 */
            4.0 / 3, 2.0 / 3, 0, 2.0 / 3, 4.0 / 3, 0 /* centroids */
        };
        const uint32_t quads[] = {
            0, 4, 9, 6, 1, 5, 9, 4, 2, 6, 9, 5,
            0, 6, 10, 8, 2, 7, 10, 6, 3, 8, 10, 7
        };
        autoremesher_core_result mesh = makeMesh(vertices, 4, indices, sizes, 2);
        checkSplit("shared edge", &mesh, expected, 11, quads, 6);
    }

    /* A pentagon, a hexagon and a heptagon, as the extractor emits: n quads
     * each, with the centroid the mean of the corners. */
    {
        double vertices[7 * 3];
        uint32_t indices[5 + 6 + 7];
        uint32_t sizes[] = { 5, 6, 7 };
        size_t i, next = 0, face;
        autoremesher_core_result mesh;
        for (i = 0; i < 7; ++i) {
            vertices[i * 3] = (double)i;
            vertices[i * 3 + 1] = (double)(i * i);
            vertices[i * 3 + 2] = -1.0;
        }
        for (face = 0; face < 3; ++face) {
            for (i = 0; i < sizes[face]; ++i)
                indices[next++] = (uint32_t)i;
        }
        mesh = makeMesh(vertices, 7, indices, sizes, 3);
        memset(&result, 0xff, sizeof(result));
        CHECK(1 == autoremesher_core_split_into_quads(&mesh, &result));
        /* Edges: 0-1 .. 3-4 shared by all three faces, 4-0, 4-5, 5-0, 5-6,
         * 6-0: 4 + 1 + 2 + 2 = 9.  Vertices: 7 + 9 + 3 centroids. */
        checkAllQuads(&result, 7 + 9 + 3, 5 + 6 + 7);
        if (19 == result.vertex_count) {
            /* The heptagon's centroid, the last vertex: mean of x = 3,
             * mean of y = (0+1+4+9+16+25+36)/7 = 13. */
            CHECK(3.0 == result.vertices[18 * 3]);
            CHECK(13.0 == result.vertices[18 * 3 + 1]);
            CHECK(-1.0 == result.vertices[18 * 3 + 2]);
        }
        autoremesher_core_free_result(&result);
    }

    /* A closed cube stays closed and consistently oriented: every directed
     * edge of the output is used once, and so is its reverse.  V - E + F
     * stays 2: 26 - 48 + 24. */
    {
        double vertices[] = {
            0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0,
            0, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1
        };
        uint32_t indices[] = {
            0, 3, 2, 1, 4, 5, 6, 7, 0, 1, 5, 4,
            1, 2, 6, 5, 2, 3, 7, 6, 3, 0, 4, 7
        };
        uint32_t sizes[] = { 4, 4, 4, 4, 4, 4 };
        autoremesher_core_result mesh = makeMesh(vertices, 8, indices, sizes, 6);
        memset(&result, 0xff, sizeof(result));
        CHECK(1 == autoremesher_core_split_into_quads(&mesh, &result));
        checkAllQuads(&result, 26, 24);
        if (24 == result.face_count && 26 == result.vertex_count) {
            int uses[26][26];
            size_t f, j, a, b, unmatched = 0;
            memset(uses, 0, sizeof(uses));
            for (f = 0; f < 24; ++f) {
                for (j = 0; j < 4; ++j)
                    ++uses[result.face_indices[f * 4 + j]][result.face_indices[f * 4 + (j + 1) % 4]];
            }
            for (a = 0; a < 26; ++a) {
                for (b = 0; b < 26; ++b) {
                    if (0 != uses[a][b] && (1 != uses[a][b] || 1 != uses[b][a]))
                        ++unmatched;
                }
            }
            CHECK(0 == unmatched);
            /* Every new vertex lies inside the cube. */
            for (j = 0; j < 26 * 3; ++j)
                CHECK(result.vertices[j] >= 0.0 && result.vertices[j] <= 1.0);
        }
        autoremesher_core_free_result(&result);
    }

    /* A vertex no face uses is kept, in place: the split does not clean the
     * mesh up, as the weld does not. */
    {
        double vertices[] = { 9, 9, 9, 0, 0, 0, 3, 0, 0, 0, 3, 0 };
        uint32_t indices[] = { 1, 2, 3 };
        uint32_t sizes[] = { 3 };
        const double expected[] = {
            9, 9, 9, 0, 0, 0, 3, 0, 0, 0, 3, 0,
            1.5, 0, 0, 1.5, 1.5, 0, 0, 1.5, 0,
            1, 1, 0
        };
        const uint32_t quads[] = { 1, 4, 7, 6, 2, 5, 7, 4, 3, 6, 7, 5 };
        autoremesher_core_result mesh = makeMesh(vertices, 4, indices, sizes, 1);
        checkSplit("unreferenced", &mesh, expected, 8, quads, 3);
    }

    /* A face with a repeated corner is still split, not refused: its
     * zero-length edge gets a midpoint at that corner. */
    {
        double vertices[] = { 0, 0, 0, 2, 0, 0 };
        uint32_t indices[] = { 0, 0, 1 };
        uint32_t sizes[] = { 3 };
        autoremesher_core_result mesh = makeMesh(vertices, 2, indices, sizes, 1);
        memset(&result, 0xff, sizeof(result));
        CHECK(1 == autoremesher_core_split_into_quads(&mesh, &result));
        /* Edges 0-0, 0-1, 1-0 (the same as 0-1): 2.  2 + 2 + 1. */
        checkAllQuads(&result, 5, 3);
        if (5 == result.vertex_count)
            CHECK(0.0 == result.vertices[2 * 3] && 0.0 == result.vertices[2 * 3 + 1]);
        autoremesher_core_free_result(&result);
    }

    /* An empty mesh gives an empty mesh. */
    {
        autoremesher_core_result mesh;
        memset(&mesh, 0, sizeof(mesh));
        memset(&result, 0xff, sizeof(result));
        CHECK(1 == autoremesher_core_split_into_quads(&mesh, &result));
        CHECK(0 == result.vertex_count && 0 == result.face_count && 0 == result.index_count
            && 0 == result.quad_count);
        autoremesher_core_free_result(&result);
        CHECK(isZeroed(&result));
        /* Freeing twice is harmless. */
        autoremesher_core_free_result(&result);
        CHECK(isZeroed(&result));
    }

    /* Refusals. */
    CHECK(0 == autoremesher_core_split_into_quads(NULL, NULL));
    {
        double vertices[] = { 0, 0, 0, 1, 0, 0, 0, 1, 0 };
        uint32_t indices[] = { 0, 1, 2 };
        uint32_t sizes[] = { 3 };
        autoremesher_core_result mesh = makeMesh(vertices, 3, indices, sizes, 1);
        autoremesher_core_result bad;

        CHECK(0 == autoremesher_core_split_into_quads(&mesh, NULL));
        checkRefused("no mesh", NULL);

        bad = mesh;
        bad.vertex_count = 2; /* index 2 is out of range */
        checkRefused("index out of range", &bad);

        {
            uint32_t two[] = { 2 };
            bad = makeMesh(vertices, 3, indices, two, 1);
            checkRefused("two-corner face", &bad);
        }

        bad = mesh;
        bad.index_count = 4; /* the face sizes sum to 3 */
        checkRefused("sizes do not sum to index_count", &bad);
        bad.index_count = 2;
        checkRefused("sizes overrun index_count", &bad);

        bad = mesh;
        bad.vertices = NULL;
        checkRefused("null vertices", &bad);
        bad = mesh;
        bad.face_indices = NULL;
        checkRefused("null indices", &bad);
        bad = mesh;
        bad.face_sizes = NULL;
        checkRefused("null sizes", &bad);
    }

    if (0 != failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
