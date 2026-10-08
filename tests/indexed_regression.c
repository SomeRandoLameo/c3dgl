// Exercise the real CPU assembly paths without submitting GPU commands.
// A small capacity exhaustively tests empty/full/partial triangle boundaries.
#ifdef C3DGL_MAX_VERTICES
#undef C3DGL_MAX_VERTICES
#endif
#define C3DGL_MAX_VERTICES 48
#include "../src/c3dgl.c"

typedef struct { s16 pos[3], pad, uv[2]; u8 color[4]; } Packed;
static Packed input[64];
static u16 indices[96];
static u8 output[C3DGL_MAX_VERTICES * GPU_VERTEX_SIZE];
static u8 reference[sizeof(output)];
static Buffer buffers[2];
static int checks, failures;
static FILE *logFile;

static void check(bool ok, const char *what)
{
    checks++;
    if (!ok) { failures++; fprintf(logFile, "FAIL %s\n", what); }
}

static void setup(int used, bool flat, bool vbo)
{
    memset(&gl, 0, sizeof(gl));
    memset(output, 0xa5, sizeof(output));
    gl.vbo = output;
    gl.vertexCount = used;
    gl.renderMode = GL_RENDER;
    gl.primitive = GL_TRIANGLES;
    gl.polygonMode[0] = gl.polygonMode[1] = GL_FILL;
    gl.shadeModel = flat ? GL_FLAT : GL_SMOOTH;
    gl.current.tex[2] = 1.0f;
    gl.currentEdge = true;
    gl.buffers = buffers;
    gl.bufferCount = 2;
    buffers[1] = (Buffer){ true, (u8*)input, sizeof(input), GL_STATIC_DRAW, 0, NULL };
    gl.arrays[ARRAY_VERTEX] = (ClientArray){true, vbo ? 0 : input[0].pos, vbo ? 1 : 0, 3, GL_SHORT, sizeof(Packed)};
    gl.arrays[ARRAY_TEXCOORD0] = (ClientArray){true, vbo ? (void*)8 : input[0].uv, vbo ? 1 : 0, 2, GL_SHORT, sizeof(Packed)};
    gl.arrays[ARRAY_COLOR] = (ClientArray){true, vbo ? (void*)12 : input[0].color, vbo ? 1 : 0, 4, GL_UNSIGNED_BYTE, sizeof(Packed)};
}

static void comparePaths(int used, int count, bool flat, bool vbo)
{
    setup(used, flat, vbo);
    Vertex current = gl.current;
    for (int i = 0; i < count; i++) submitArrayVertex(indices[i]);
    endPrimitive();
    int referenceCount = gl.vertexCount;
    memcpy(reference, output, sizeof(output));
    GLenum referenceError = gl.error;

    setup(used, flat, vbo);
    check(indexedTriangleFastPath(GL_TRIANGLES, GL_UNSIGNED_SHORT, (u8*)indices, count), "eligible path handled");
    endPrimitive();
    check(gl.vertexCount == referenceCount, "same complete triangle count");
    check(memcmp(reference, output, sizeof(output)) == 0, "identical GPU vertex bytes including untouched tail");
    check(memcmp(&current, &gl.current, sizeof(current)) == 0, "current attributes unchanged");
    check(gl.error == referenceError, "same GL error state");
}

// glDrawArrays(GL_TRIANGLES) with the Tesselator layout: float position and texcoord, byte color
typedef struct { float pos[3], uv[2]; u8 color[4]; } PackedF;
static PackedF inputF[64];

// omit: 1 = no texcoord array, 2 = no color array, 3 = neither (those come from the current values)
static void setupF(int used, bool flat, bool vbo, int omit)
{
    setup(used, flat, vbo);
    gl.current.tex[0] = 0.3f; gl.current.tex[1] = 0.6f; gl.current.tex[2] = 2.0f;
    gl.current.color[0] = 11; gl.current.color[1] = 22; gl.current.color[2] = 33; gl.current.color[3] = 44;
    gl.current.depthBias = 0.5f;
    buffers[1] = (Buffer){ true, (u8*)inputF, sizeof(inputF), GL_STATIC_DRAW, 0, NULL };
    gl.arrays[ARRAY_VERTEX] = (ClientArray){true, vbo ? 0 : inputF[0].pos, vbo ? 1 : 0, 3, GL_FLOAT, sizeof(PackedF)};
    gl.arrays[ARRAY_TEXCOORD0] = (ClientArray){true, vbo ? (void*)12 : inputF[0].uv, vbo ? 1 : 0, 2, GL_FLOAT, sizeof(PackedF)};
    gl.arrays[ARRAY_COLOR] = (ClientArray){true, vbo ? (void*)20 : inputF[0].color, vbo ? 1 : 0, 4, GL_UNSIGNED_BYTE, sizeof(PackedF)};
    if (omit & 1) gl.arrays[ARRAY_TEXCOORD0].enabled = false;
    if (omit & 2) gl.arrays[ARRAY_COLOR].enabled = false;
}

static void compareArrayPaths(int used, int first, int count, bool flat, bool vbo, int omit)
{
    setupF(used, flat, vbo, omit);
    Vertex current = gl.current;
    for (int i = 0; i < count; i++) submitArrayVertex(first + i);
    endPrimitive();
    int referenceCount = gl.vertexCount;
    memcpy(reference, output, sizeof(output));
    GLenum referenceError = gl.error;

    setupF(used, flat, vbo, omit);
    check(arrayTriangleFastPath(GL_TRIANGLES, first, count), "array path handled");
    endPrimitive();
    check(gl.vertexCount == referenceCount, "array path: same complete triangle count");
    {
        static int shown = 0;
        int diff = -1;
        for (int i = 0; i < (int)sizeof(output); i++) if (reference[i] != output[i]) { diff = i; break; }
        if (diff >= 0 && shown++ < 8)
            fprintf(logFile, "DIFF omit=%d flat=%d vbo=%d used=%d first=%d count=%d at byte %d (vertex %d offset %d): ref=%02x got=%02x\n",
                    omit, flat, vbo, used, first, count, diff, diff/GPU_VERTEX_SIZE, diff%GPU_VERTEX_SIZE, reference[diff], output[diff]);
    }
    check(memcmp(reference, output, sizeof(output)) == 0, "array path: identical GPU vertex bytes including untouched tail");
    check(memcmp(&current, &gl.current, sizeof(current)) == 0, "array path: current attributes unchanged");
    check(gl.error == referenceError, "array path: same GL error state");
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    logFile = fopen("sdmc:/c3dgl-indexed-regression.log", "w");
    if (!logFile) { gfxExit(); return 1; }
    for (int i = 0; i < 64; i++) {
        input[i].pos[0] = (s16)(i * 1040 - 32768);
        input[i].pos[1] = (s16)(32767 - i * 1003);
        input[i].pos[2] = (s16)(i * 731 - 16384);
        input[i].uv[0] = (s16)(i * 977 - 30000);
        input[i].uv[1] = (s16)(31000 - i * 997);
        for (int c = 0; c < 4; c++) input[i].color[c] = (u8)(i * 4 + c);
    }
    for (int i = 0; i < 96; i++) indices[i] = (u16)((i * 17) % 64);
    printf("Indexed paths ...\n");
    for (int used = 0; used <= C3DGL_MAX_VERTICES; used++)
        for (int count = 0; count <= 96; count++)
            for (int flat = 0; flat < 2; flat++)
                for (int vbo = 0; vbo < 2; vbo++)
                    comparePaths(used, count, flat, vbo);

    for (int i = 0; i < 64; i++) {
        inputF[i].pos[0] = i * 1.5f - 40.0f;
        inputF[i].pos[1] = 100.0f - i * 0.25f;
        inputF[i].pos[2] = -(float)i;
        inputF[i].uv[0] = i / 64.0f;
        inputF[i].uv[1] = 1.0f - i / 64.0f;
        for (int c = 0; c < 4; c++) inputF[i].color[c] = (u8)(i * 4 + c);
    }
    static const int firsts[] = { 0, 3, 7 };
    for (int used = 0; used <= C3DGL_MAX_VERTICES; used++)
    {
        for (int f = 0; f < 3; f++)
            for (int count = 0; count <= 57; count++)
                for (int flat = 0; flat < 2; flat++)
                    for (int vbo = 0; vbo < 2; vbo++)
                        for (int omit = 0; omit < 4; omit++)
                            compareArrayPaths(used, firsts[f], count, flat, vbo, omit);
        printf("Array paths: %d / %d, %d failures\n", used + 1, C3DGL_MAX_VERTICES + 1, failures);
    }
    // Reading past the end of a buffer object, a negative start and other modes are left to the generic path
    setupF(0, false, true, 0);
    check(!arrayTriangleFastPath(GL_TRIANGLES, 60, 9), "array path: buffer overrun rejected");
    setupF(0, false, false, 0);
    check(!arrayTriangleFastPath(GL_TRIANGLES, -3, 9), "array path: negative first rejected");
    setupF(0, false, false, 0);
    check(!arrayTriangleFastPath(GL_TRIANGLE_STRIP, 0, 9), "array path: strip rejected");
    setupF(0, false, false, 0);
    gl.arrays[ARRAY_VERTEX].type = GL_SHORT;
    check(!arrayTriangleFastPath(GL_TRIANGLES, 0, 9), "array path: short positions rejected");
    setupF(0, false, false, 0);
    gl.arrays[ARRAY_VERTEX].enabled = false;
    check(!arrayTriangleFastPath(GL_TRIANGLES, 0, 9), "array path: no vertex array rejected");
    setupF(0, false, false, 0);
    gl.arrays[ARRAY_COLOR].type = GL_FLOAT;
    check(!arrayTriangleFastPath(GL_TRIANGLES, 0, 9), "array path: float colors rejected");
    setupF(0, false, false, 0);
    gl.lightingEnabled = true;
    check(!arrayTriangleFastPath(GL_TRIANGLES, 0, 9), "array path: lighting rejected");

    // All special features must still reject the fast path and retain their
    // original generic implementation, even when the frame buffer is full.
    for (int used = 0; used <= C3DGL_MAX_VERTICES; used += C3DGL_MAX_VERTICES) {
#define REJECT(change) do { setup(used, false, true); change; check(!indexedTriangleFastPath(GL_TRIANGLES, GL_UNSIGNED_SHORT, (u8*)indices, 12), #change); } while (0)
        REJECT(gl.lightingEnabled = true);
        REJECT(gl.clipEnabled = 1);
        REJECT(gl.texGen[0].enabled = 1);
        REJECT(gl.texGen[1].enabled = 1);
        REJECT(gl.texGen[2].enabled = 1);
        REJECT(gl.renderMode = GL_FEEDBACK);
        REJECT(gl.renderMode = GL_SELECT);
        REJECT(gl.polygonMode[0] = GL_LINE);
        REJECT(gl.polygonMode[1] = GL_POINT);
        REJECT(gl.offsetFill = true);
        REJECT(gl.batch.units[1].texture = 1);
        REJECT(gl.batch.units[2].texture = 1);
        REJECT(gl.arrays[ARRAY_NORMAL] = gl.arrays[ARRAY_VERTEX]);
        REJECT(gl.arrays[ARRAY_EDGEFLAG] = gl.arrays[ARRAY_VERTEX]);
        REJECT(gl.arrays[ARRAY_POINTSIZE] = gl.arrays[ARRAY_VERTEX]);
        REJECT(gl.arrays[ARRAY_TEXCOORD1] = gl.arrays[ARRAY_VERTEX]);
        REJECT(gl.arrays[ARRAY_TEXCOORD2] = gl.arrays[ARRAY_VERTEX]);
        REJECT(gl.arrays[ARRAY_VERTEX].type = GL_FLOAT);
        REJECT(gl.arrays[ARRAY_TEXCOORD0].size = 4);
#undef REJECT
    }
    fprintf(logFile, "checks=%d failures=%d\n", checks, failures);
    fclose(logFile);
    printf("Indexed regression: %d checks, %d failures\n", checks, failures);
    gfxExit();
    return failures ? 1 : 0;
}
