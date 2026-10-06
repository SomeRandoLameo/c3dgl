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
    buffers[1] = (Buffer){ true, (u8*)input, sizeof(input), GL_STATIC_DRAW };
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
    for (int used = 0; used <= C3DGL_MAX_VERTICES; used++)
        for (int count = 0; count <= 96; count++)
            for (int flat = 0; flat < 2; flat++)
                for (int vbo = 0; vbo < 2; vbo++)
                    comparePaths(used, count, flat, vbo);

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
