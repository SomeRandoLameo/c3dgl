// c3dgl: OpenGL 1.1 subset for the Nintendo 3DS, implemented on citro3d.
//
// Design:
//   - Immediate mode and client arrays are assembled into triangles in one linear vertex buffer.
//     Vertices are batched across glBegin/glEnd; a batch is drawn with one C3D_DrawArrays() when
//     the draw state (texture, matrices, blend, depth, ...) changes, see prepareDraw().
//     Engines like raylib set/unset the texture around every glyph/shape, so state is compared
//     lazily at draw time instead of flushing inside the gl* setters.
//   - Vertices stay in object space, the vertex shader applies post * projection * modelview.
//     `post` maps OpenGL clip space to PICA clip space: 90 degree screen rotation + depth range [-1, 0].
//   - Strips, fans, quads and polygons are split into triangles on the CPU.
//   - Lines and points have no PICA equivalent: they are transformed on the CPU and expanded to quads in NDC.
//   - Textures are padded to power-of-two sizes and Morton-swizzled; the shader scales UVs back.
//   - One render target per screen, sharing the vertex buffer and GL state; c3dglSetScreen() flushes the
//     batch and switches the target (C3D_FrameDrawOn), citro3d presents every target drawn on in the frame.
//
// Known limitations: no mipmaps, no glReadPixels, glClear ignores scissor/color mask,
// REPEAT wrap on non-power-of-two textures samples the padding.
#include "GL/gl.h"
#include "c3dgl.h"

#include <3ds.h>
#include <citro3d.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c3dgl_vsh_shbin.h"

//----------------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------------
#define C3DGL_MAX_VERTICES      (64*1024)   // Per frame, 24 bytes each
#define C3DGL_MAX_TEXTURES      512         // Texture ids 1..C3DGL_MAX_TEXTURES-1
#define C3DGL_MATRIX_STACK      32
#define C3DGL_MAX_TEXTURE_SIZE  1024

// Row order of texture memory: the first row in memory is the top of the texture (t = 1),
// glTexImage2D data starts at t = 0, so rows are flipped while swizzling (verified in Azahar)
#define C3DGL_TEXTURE_FLIP_Y    1

// Output format is added per screen, see screenTransferFlags()
#define DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define C3DGL_SCREEN_COUNT      2

#define LOG(...) printf("C3DGL: " __VA_ARGS__)
#define WARN_ONCE(...) do { static bool warned = false; if (!warned) { warned = true; LOG(__VA_ARGS__); } } while (0)

//----------------------------------------------------------------------------------
// Types
//----------------------------------------------------------------------------------
typedef struct {
    float pos[3];
    float uv[2];
    u8 color[4];
} Vertex;

typedef struct {
    float m[16];                // Column-major, like OpenGL
} Mat4;

typedef struct {
    GPU_TEXCOLOR format;
    int bpp;                    // Bytes per pixel
    bool reverse;               // Byte order within a pixel is reversed on PICA (RGBA -> ABGR, ...)
} TexFormat;

typedef struct {
    bool used;                  // Id handed out by glGenTextures
    bool loaded;                // tex is initialized
    C3D_Tex tex;
    TexFormat format;
    int width, height;          // Image size; tex is padded to power-of-two
    GLenum minFilter, magFilter, wrapS, wrapT;
} Texture;

// Everything that decides how a range of vertices is rendered; see prepareDraw()
typedef struct {
    GLuint texture;             // 0: untextured
    bool clipSpace;             // Vertices are already in NDC (expanded lines and points)
    u32 matrixSerial;           // Matrix version (0 for clipSpace)
    bool blend;
    GLenum blendSrc, blendDst;
    bool depthTest, depthMask;
    GLenum depthFunc;
    bool alphaTest;
    GLenum alphaFunc;
    u8 alphaRef;                // 0..255
    u8 colorMask;
    bool cull;
    GLenum cullFace, frontFace;
    bool scissor;
    GLint scissorBox[4];
    GLint viewport[4];
} DrawState;

typedef struct {
    bool enabled;
    const void *pointer;
    GLint size;
    GLenum type;
    GLsizei stride;
} ClientArray;

enum { ARRAY_VERTEX, ARRAY_TEXCOORD, ARRAY_COLOR, ARRAY_COUNT };

//----------------------------------------------------------------------------------
// Global state
//----------------------------------------------------------------------------------
static struct {
    bool ready;
    C3D_RenderTarget *targets[C3DGL_SCREEN_COUNT];
    C3DGLscreen screen;                 // Screen drawn on, see c3dglSetScreen()
    DVLB_s *dvlb;
    shaderProgram_s program;
    int uLocMvp, uLocTexScale;
    Mat4 post;                          // OpenGL clip space -> PICA clip space (rotation, depth range)

    // Frame and vertex batching
    bool frameActive;
    bool drawnThisFrame;
    Vertex *vbo;                        // Linear memory, rewritten every frame
    int vertexCount;
    int batchStart;                     // First vertex not yet submitted
    DrawState batch;                    // State applied to the GPU for the current batch
    bool batchValid;

    // GL state as set by the gl* calls
    DrawState state;
    bool texture2D;
    GLuint boundTexture;
    GLint unpackAlignment, packAlignment;
    float lineWidth, pointSize;
    u32 clearColor;                     // 0xRRGGBBAA
    float clearDepth;

    // Matrices
    int matrixMode;                     // 0: modelview, 1: projection, 2: texture
    Mat4 stack[3][C3DGL_MATRIX_STACK];
    int stackDepth[3];
    u32 matrixSerial;                   // Incremented on every modelview/projection change
    Mat4 pmv;                           // projection * modelview, cached for pmvSerial
    u32 pmvSerial;

    // Immediate mode
    bool inBegin;
    GLenum primitive;
    Vertex current;                     // Current texcoord and color
    Vertex prim[4];                     // Vertices kept for the primitive being assembled, see submitVertex()
    int primCount;                      // Vertices in prim
    int primTotal;                      // Vertices submitted since glBegin/glDraw*

    ClientArray arrays[ARRAY_COUNT];
    Texture textures[C3DGL_MAX_TEXTURES];

    // Textures deleted during a frame are freed once the GPU is done with that frame
    C3D_Tex *deferredDeletes;
    int deferredCount, deferredCapacity;
} gl;

//----------------------------------------------------------------------------------
// Matrix helpers
//----------------------------------------------------------------------------------
static void mat4Identity(Mat4 *out)
{
    memset(out, 0, sizeof(*out));
    out->m[0] = out->m[5] = out->m[10] = out->m[15] = 1.0f;
}

// out = a*b (out may alias a or b)
static void mat4Mul(Mat4 *out, const Mat4 *a, const Mat4 *b)
{
    Mat4 r;
    for (int col = 0; col < 4; col++)
    {
        for (int row = 0; row < 4; row++)
        {
            r.m[col*4 + row] = a->m[0*4 + row]*b->m[col*4 + 0] + a->m[1*4 + row]*b->m[col*4 + 1] +
                               a->m[2*4 + row]*b->m[col*4 + 2] + a->m[3*4 + row]*b->m[col*4 + 3];
        }
    }
    *out = r;
}

static void mat4Transform(const Mat4 *m, const float v[3], float out[4])
{
    for (int row = 0; row < 4; row++) out[row] = m->m[row]*v[0] + m->m[4 + row]*v[1] + m->m[8 + row]*v[2] + m->m[12 + row];
}

static void mat4ToC3D(const Mat4 *m, C3D_Mtx *out)
{
    for (int row = 0; row < 4; row++)
    {
        out->r[row].x = m->m[0*4 + row];
        out->r[row].y = m->m[1*4 + row];
        out->r[row].z = m->m[2*4 + row];
        out->r[row].w = m->m[3*4 + row];
    }
}

static void mat4FromC3D(const C3D_Mtx *m, Mat4 *out)
{
    for (int row = 0; row < 4; row++)
    {
        out->m[0*4 + row] = m->r[row].x;
        out->m[1*4 + row] = m->r[row].y;
        out->m[2*4 + row] = m->r[row].z;
        out->m[3*4 + row] = m->r[row].w;
    }
}

static Mat4 *currentMatrix(void)
{
    return &gl.stack[gl.matrixMode][gl.stackDepth[gl.matrixMode]];
}

static void matrixChanged(void)
{
    if (gl.matrixMode != 2) gl.matrixSerial++;
}

static void multCurrent(const Mat4 *m)
{
    Mat4 *cur = currentMatrix();
    mat4Mul(cur, cur, m);
    matrixChanged();
}

static const Mat4 *projectionModelview(void)
{
    if (gl.pmvSerial != gl.matrixSerial)
    {
        mat4Mul(&gl.pmv, &gl.stack[1][gl.stackDepth[1]], &gl.stack[0][gl.stackDepth[0]]);
        gl.pmvSerial = gl.matrixSerial;
    }
    return &gl.pmv;
}

//----------------------------------------------------------------------------------
// GL -> PICA enum mapping
//----------------------------------------------------------------------------------
static GPU_BLENDFACTOR blendFactor(GLenum f)
{
    switch (f)
    {
        case GL_ZERO: return GPU_ZERO;
        case GL_ONE: return GPU_ONE;
        case GL_SRC_COLOR: return GPU_SRC_COLOR;
        case GL_ONE_MINUS_SRC_COLOR: return GPU_ONE_MINUS_SRC_COLOR;
        case GL_SRC_ALPHA: return GPU_SRC_ALPHA;
        case GL_ONE_MINUS_SRC_ALPHA: return GPU_ONE_MINUS_SRC_ALPHA;
        case GL_DST_ALPHA: return GPU_DST_ALPHA;
        case GL_ONE_MINUS_DST_ALPHA: return GPU_ONE_MINUS_DST_ALPHA;
        case GL_DST_COLOR: return GPU_DST_COLOR;
        case GL_ONE_MINUS_DST_COLOR: return GPU_ONE_MINUS_DST_COLOR;
        case GL_SRC_ALPHA_SATURATE: return GPU_SRC_ALPHA_SATURATE;
        default: return GPU_ONE;
    }
}

static GPU_TESTFUNC testFunc(GLenum f)
{
    switch (f)
    {
        case GL_NEVER: return GPU_NEVER;
        case GL_LESS: return GPU_LESS;
        case GL_EQUAL: return GPU_EQUAL;
        case GL_LEQUAL: return GPU_LEQUAL;
        case GL_GREATER: return GPU_GREATER;
        case GL_NOTEQUAL: return GPU_NOTEQUAL;
        case GL_GEQUAL: return GPU_GEQUAL;
        default: return GPU_ALWAYS;
    }
}

// PICA stores depth reversed (near = 1, far = 0, see C3D_DepthMap in c3dglInit), so comparisons flip
static GPU_TESTFUNC depthFunc(GLenum f)
{
    switch (f)
    {
        case GL_LESS: return GPU_GREATER;
        case GL_LEQUAL: return GPU_GEQUAL;
        case GL_GREATER: return GPU_LESS;
        case GL_GEQUAL: return GPU_LEQUAL;
        default: return testFunc(f);
    }
}

static GPU_CULLMODE cullMode(const DrawState *s)
{
    if (!s->cull || s->clipSpace || (s->cullFace == GL_FRONT_AND_BACK)) return GPU_CULL_NONE;

    bool cullCCW = (s->cullFace == GL_FRONT) == (s->frontFace == GL_CCW);
    return cullCCW? GPU_CULL_FRONT_CCW : GPU_CULL_BACK_CCW;
}

static GPU_TEXTURE_FILTER_PARAM texFilter(GLenum f)
{
    return ((f == GL_NEAREST) || (f == GL_NEAREST_MIPMAP_NEAREST) || (f == GL_NEAREST_MIPMAP_LINEAR))? GPU_NEAREST : GPU_LINEAR;
}

static GPU_TEXTURE_WRAP_PARAM texWrap(GLenum w)
{
    switch (w)
    {
        case GL_CLAMP_TO_EDGE: return GPU_CLAMP_TO_EDGE;
        case GL_MIRRORED_REPEAT: return GPU_MIRRORED_REPEAT;
        default: return GPU_REPEAT;
    }
}

static bool texFormat(GLenum format, GLenum type, TexFormat *out)
{
    if (type == GL_UNSIGNED_BYTE)
    {
        switch (format)
        {
            case GL_RGBA: *out = (TexFormat){ GPU_RGBA8, 4, true }; return true;
            case GL_RGB: *out = (TexFormat){ GPU_RGB8, 3, true }; return true;
            case GL_LUMINANCE_ALPHA: *out = (TexFormat){ GPU_LA8, 2, true }; return true;
            case GL_LUMINANCE: *out = (TexFormat){ GPU_L8, 1, false }; return true;
            case GL_ALPHA: *out = (TexFormat){ GPU_A8, 1, false }; return true;
            default: return false;
        }
    }

    // Packed 16-bit formats have the same bit layout on PICA
    if ((format == GL_RGB) && (type == GL_UNSIGNED_SHORT_5_6_5)) { *out = (TexFormat){ GPU_RGB565, 2, false }; return true; }
    if ((format == GL_RGBA) && (type == GL_UNSIGNED_SHORT_5_5_5_1)) { *out = (TexFormat){ GPU_RGBA5551, 2, false }; return true; }
    if ((format == GL_RGBA) && (type == GL_UNSIGNED_SHORT_4_4_4_4)) { *out = (TexFormat){ GPU_RGBA4, 2, false }; return true; }

    return false;
}

//----------------------------------------------------------------------------------
// Frame and batch management
//----------------------------------------------------------------------------------
static void processDeferredDeletes(void)
{
    for (int i = 0; i < gl.deferredCount; i++) C3D_TexDelete(&gl.deferredDeletes[i]);
    gl.deferredCount = 0;
}

static void deferTextureDelete(const C3D_Tex *tex)
{
    if (gl.deferredCount == gl.deferredCapacity)
    {
        int capacity = gl.deferredCapacity? gl.deferredCapacity*2 : 16;
        C3D_Tex *list = realloc(gl.deferredDeletes, capacity*sizeof(C3D_Tex));
        if (list == NULL) { LOG("Out of memory, leaking texture\n"); return; }
        gl.deferredDeletes = list;
        gl.deferredCapacity = capacity;
    }
    gl.deferredDeletes[gl.deferredCount++] = *tex;
}

static int screenWidth(C3DGLscreen screen)
{
    return (screen == C3DGL_SCREEN_BOTTOM)? C3DGL_BOTTOM_SCREEN_WIDTH : C3DGL_TOP_SCREEN_WIDTH;
}

// The display transfer has to write the framebuffer format the screen is currently set to
// (gfxInitDefault: BGR8, consoleInit changes it to RGB565)
static u32 screenTransferFlags(gfxScreen_t screen)
{
    GX_TRANSFER_FORMAT out;
    switch (gfxGetScreenFormat(screen))
    {
        case GSP_RGBA8_OES: out = GX_TRANSFER_FMT_RGBA8; break;
        case GSP_RGB565_OES: out = GX_TRANSFER_FMT_RGB565; break;
        case GSP_RGB5_A1_OES: out = GX_TRANSFER_FMT_RGB5A1; break;
        case GSP_RGBA4_OES: out = GX_TRANSFER_FMT_RGBA4; break;
        default: out = GX_TRANSFER_FMT_RGB8; break;
    }
    return DISPLAY_TRANSFER_FLAGS | GX_TRANSFER_OUT_FORMAT(out);
}

// Link the current screen's target to its display. Done on every switch, as the app may have
// changed the screen format in between (e.g. from the console to graphics)
static void linkTarget(void)
{
    gfxScreen_t screen = (gl.screen == C3DGL_SCREEN_BOTTOM)? GFX_BOTTOM : GFX_TOP;
    C3D_RenderTargetSetOutput(gl.targets[gl.screen], screen, GFX_LEFT, screenTransferFlags(screen));
}

static void ensureFrame(void)
{
    if (gl.frameActive) return;

    // SYNCDRAW: waits until the GPU finished the previous frame, so the vertex buffer can be reused
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameDrawOn(gl.targets[gl.screen]);     // Also resets the viewport, hence batchValid = false

    gl.frameActive = true;
    gl.drawnThisFrame = false;
    gl.vertexCount = 0;
    gl.batchStart = 0;
    gl.batchValid = false;

    processDeferredDeletes();
}

// Submit the vertices collected since the last flush with the currently applied state
static void flush(void)
{
    int count = gl.vertexCount - gl.batchStart;
    if (count <= 0) return;

    GSPGPU_FlushDataCache(&gl.vbo[gl.batchStart], count*sizeof(Vertex));
    C3D_DrawArrays(GPU_TRIANGLES, gl.batchStart, count);

    gl.batchStart = gl.vertexCount;
    gl.drawnThisFrame = true;
}

// Logical (landscape, bottom-left origin) rectangle -> physical render target rectangle.
// The target is 240xN (portrait); `post` maps logical x to physical -y and logical y to physical x.
static void physicalRect(const GLint r[4], int *x, int *y, int *w, int *h)
{
    *x = r[1];
    *y = screenWidth(gl.screen) - r[0] - r[2];
    *w = r[3];
    *h = r[2];
}

static void applyState(const DrawState *s)
{
    int x, y, w, h;
    physicalRect(s->viewport, &x, &y, &w, &h);
    C3D_SetViewport(x, y, w, h);

    if (s->scissor)
    {
        physicalRect(s->scissorBox, &x, &y, &w, &h);
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (w < 0) w = 0;
        if (h < 0) h = 0;
        C3D_SetScissor(GPU_SCISSOR_NORMAL, x, y, x + w, y + h);
    }
    else C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);

    GPU_WRITEMASK writeMask = (GPU_WRITEMASK)(s->colorMask | ((s->depthTest && s->depthMask)? GPU_WRITE_DEPTH : 0));
    C3D_DepthTest(s->depthTest, s->depthTest? depthFunc(s->depthFunc) : GPU_ALWAYS, writeMask);
    C3D_AlphaTest(s->alphaTest, testFunc(s->alphaFunc), s->alphaRef);

    if (s->blend)
    {
        GPU_BLENDFACTOR src = blendFactor(s->blendSrc), dst = blendFactor(s->blendDst);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, src, dst, src, dst);
    }
    else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

    C3D_CullFace(cullMode(s));

    // Fragment stage: vertex color, optionally modulated by the texture
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvInit(env);
    if (s->texture != 0)
    {
        Texture *t = &gl.textures[s->texture];
        C3D_TexBind(0, &t->tex);
        C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexScale, (float)t->width/t->tex.width, (float)t->height/t->tex.height, 1.0f, 1.0f);
    }
    else
    {
        C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
        C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
    }

    Mat4 mvp = gl.post;
    if (!s->clipSpace) mat4Mul(&mvp, &gl.post, projectionModelview());
    C3D_Mtx mtx;
    mat4ToC3D(&mvp, &mtx);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, gl.uLocMvp, &mtx);
}

static bool textureValid(GLuint id)
{
    return (id > 0) && (id < C3DGL_MAX_TEXTURES) && gl.textures[id].loaded;
}

// Call before emitting vertices: starts a new batch if the draw state changed
static void prepareDraw(bool clipSpace)
{
    ensureFrame();

    // memcpy/memcmp: padding bytes must match too
    DrawState key;
    memcpy(&key, &gl.state, sizeof(DrawState));
    key.clipSpace = clipSpace;
    key.matrixSerial = clipSpace? 0 : gl.matrixSerial;
    key.texture = (gl.texture2D && textureValid(gl.boundTexture))? gl.boundTexture : 0;

    if (!gl.batchValid || (memcmp(&key, &gl.batch, sizeof(DrawState)) != 0))
    {
        flush();
        applyState(&key);
        memcpy(&gl.batch, &key, sizeof(DrawState));
        gl.batchValid = true;
    }
}

static bool reserveVertices(int count)
{
    if (gl.vertexCount + count <= C3DGL_MAX_VERTICES) return true;

    WARN_ONCE("Vertex buffer full (%i vertices per frame), dropping geometry\n", C3DGL_MAX_VERTICES);
    return false;
}

//----------------------------------------------------------------------------------
// Primitive assembly
//----------------------------------------------------------------------------------
static void emitTriangle(const Vertex *a, const Vertex *b, const Vertex *c)
{
    if (!reserveVertices(3)) return;

    Vertex *v = &gl.vbo[gl.vertexCount];
    v[0] = *a;
    v[1] = *b;
    v[2] = *c;
    gl.vertexCount += 3;
}

static void lerpVertex(Vertex *out, const Vertex *a, const Vertex *b, float t)
{
    for (int i = 0; i < 2; i++) out->uv[i] = a->uv[i] + (b->uv[i] - a->uv[i])*t;
    for (int i = 0; i < 4; i++) out->color[i] = (u8)(a->color[i] + ((float)b->color[i] - a->color[i])*t);
}

#define CLIP_W_MIN  1e-5f       // Lines and points are clipped against w > CLIP_W_MIN before the divide

// Quad around the NDC positions a and b, widened by (nx, ny) perpendicular and (ex, ey) along a -> b
static void emitExpandedQuad(const Vertex *a, const Vertex *b, const float pa[3], const float pb[3],
                             float nx, float ny, float ex, float ey)
{
    Vertex q[4] = { *a, *a, *b, *b };
    q[0].pos[0] = pa[0] - ex - nx; q[0].pos[1] = pa[1] - ey - ny; q[0].pos[2] = pa[2];
    q[1].pos[0] = pa[0] - ex + nx; q[1].pos[1] = pa[1] - ey + ny; q[1].pos[2] = pa[2];
    q[2].pos[0] = pb[0] + ex + nx; q[2].pos[1] = pb[1] + ey + ny; q[2].pos[2] = pb[2];
    q[3].pos[0] = pb[0] + ex - nx; q[3].pos[1] = pb[1] + ey - ny; q[3].pos[2] = pb[2];

    emitTriangle(&q[0], &q[1], &q[2]);
    emitTriangle(&q[0], &q[2], &q[3]);
}

// Expand a line to a screen-aligned quad in NDC (batch must be in clipSpace mode)
static void emitLine(const Vertex *a, const Vertex *b)
{
    const Mat4 *pmv = projectionModelview();

    float ca[4], cb[4];
    mat4Transform(pmv, a->pos, ca);
    mat4Transform(pmv, b->pos, cb);

    // Clip against w > 0 before the perspective divide
    if ((ca[3] < CLIP_W_MIN) && (cb[3] < CLIP_W_MIN)) return;

    Vertex va = *a, vb = *b;
    if (ca[3] < CLIP_W_MIN)
    {
        float t = (CLIP_W_MIN - ca[3])/(cb[3] - ca[3]);
        for (int i = 0; i < 4; i++) ca[i] += (cb[i] - ca[i])*t;
        lerpVertex(&va, a, b, t);
    }
    else if (cb[3] < CLIP_W_MIN)
    {
        float t = (CLIP_W_MIN - cb[3])/(ca[3] - cb[3]);
        for (int i = 0; i < 4; i++) cb[i] += (ca[i] - cb[i])*t;
        lerpVertex(&vb, b, a, t);
    }

    float pa[3] = { ca[0]/ca[3], ca[1]/ca[3], ca[2]/ca[3] };
    float pb[3] = { cb[0]/cb[3], cb[1]/cb[3], cb[2]/cb[3] };

    // Direction in pixels, then half line width back to NDC (perpendicular and along the line for square caps)
    float halfW = 0.5f*(float)gl.state.viewport[2], halfH = 0.5f*(float)gl.state.viewport[3];
    float dx = (pb[0] - pa[0])*halfW, dy = (pb[1] - pa[1])*halfH;
    float len = sqrtf(dx*dx + dy*dy);
    if (len < 1e-6f) { dx = 1.0f; dy = 0.0f; }
    else { dx /= len; dy /= len; }

    float r = 0.5f*gl.lineWidth;
    emitExpandedQuad(&va, &vb, pa, pb, -dy*r/halfW, dx*r/halfH, dx*r/halfW, dy*r/halfH);
}

// Expand a point to a screen-aligned square of glPointSize pixels in NDC (batch must be in clipSpace mode)
static void emitPoint(const Vertex *v)
{
    float c[4];
    mat4Transform(projectionModelview(), v->pos, c);
    if (c[3] < CLIP_W_MIN) return;

    float p[3] = { c[0]/c[3], c[1]/c[3], c[2]/c[3] };
    float r = 0.5f*gl.pointSize;
    float rx = 2.0f*r/(float)gl.state.viewport[2], ry = 2.0f*r/(float)gl.state.viewport[3];

    // Zero-length "line" from p to p: the cap offset gives the width, the perpendicular offset the height
    emitExpandedQuad(v, v, p, p, 0.0f, ry, rx, 0.0f);
}

static bool primitiveInClipSpace(GLenum mode)
{
    return (mode == GL_POINTS) || (mode == GL_LINES) || (mode == GL_LINE_STRIP) || (mode == GL_LINE_LOOP);
}

static bool beginPrimitive(GLenum mode)
{
    if (mode > GL_POLYGON)
    {
        WARN_ONCE("Primitive 0x%x not supported\n", mode);
        return false;
    }

    prepareDraw(primitiveInClipSpace(mode));
    gl.primitive = mode;
    gl.primCount = 0;
    gl.primTotal = 0;
    return true;
}

// Assemble the primitive from the submitted vertices. prim[] holds what the mode still needs:
//   strips:              the last two vertices (quad strip: up to four)
//   fans, polygons, line loops/strips: the first and the last vertex
static void submitVertex(const Vertex *v)
{
    Vertex *p = gl.prim;
    int n = gl.primTotal++;

    switch (gl.primitive)
    {
        case GL_POINTS:
            emitPoint(v);
            break;
        case GL_LINES:
            p[gl.primCount++] = *v;
            if (gl.primCount == 2) { emitLine(&p[0], &p[1]); gl.primCount = 0; }
            break;
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            if (n == 0) p[0] = *v;
            else emitLine(&p[1], v);
            p[1] = *v;
            break;
        case GL_TRIANGLES:
            p[gl.primCount++] = *v;
            if (gl.primCount == 3) { emitTriangle(&p[0], &p[1], &p[2]); gl.primCount = 0; }
            break;
        case GL_TRIANGLE_STRIP:
            // Every other triangle is flipped to keep the winding of the first one
            if (n >= 2)
            {
                if (n & 1) emitTriangle(&p[1], &p[0], v);
                else emitTriangle(&p[0], &p[1], v);
            }
            p[0] = p[1];
            p[1] = *v;
            break;
        case GL_TRIANGLE_FAN:
        case GL_POLYGON:    // Convex, so a fan
            if (n == 0) p[0] = *v;
            else if (n >= 2) emitTriangle(&p[0], &p[1], v);
            p[1] = *v;
            break;
        case GL_QUADS:
            p[gl.primCount++] = *v;
            if (gl.primCount == 4)
            {
                emitTriangle(&p[0], &p[1], &p[2]);
                emitTriangle(&p[0], &p[2], &p[3]);
                gl.primCount = 0;
            }
            break;
        case GL_QUAD_STRIP:
            // Quad i is v[2i], v[2i+1], v[2i+3], v[2i+2]
            p[gl.primCount++] = *v;
            if (gl.primCount == 4)
            {
                emitTriangle(&p[0], &p[1], &p[3]);
                emitTriangle(&p[0], &p[3], &p[2]);
                p[0] = p[2];
                p[1] = p[3];
                gl.primCount = 2;
            }
            break;
        default: break;
    }
}

// glEnd or the end of a glDraw* call: close line loops, drop incomplete primitives
static void endPrimitive(void)
{
    if ((gl.primitive == GL_LINE_LOOP) && (gl.primTotal >= 2)) emitLine(&gl.prim[1], &gl.prim[0]);
    gl.primCount = 0;
    gl.primTotal = 0;
}

//----------------------------------------------------------------------------------
// Platform API (c3dgl.h)
//----------------------------------------------------------------------------------
bool c3dglInit(void)
{
    if (gl.ready) return true;
    memset(&gl, 0, sizeof(gl));

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) { LOG("C3D_Init failed\n"); return false; }

    // Render targets are portrait (240x400, 240x320) because the screens are rotated.
    // The bottom one is linked to its display on the first c3dglSetScreen(), so a console there stays untouched
    for (int i = 0; i < C3DGL_SCREEN_COUNT; i++)
    {
        gl.targets[i] = C3D_RenderTargetCreate(C3DGL_SCREEN_HEIGHT, screenWidth((C3DGLscreen)i), GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
        if (gl.targets[i] == NULL) { LOG("Failed to create render target\n"); c3dglClose(); return false; }
    }
    gl.screen = C3DGL_SCREEN_TOP;
    linkTarget();

    gl.vbo = linearAlloc(C3DGL_MAX_VERTICES*sizeof(Vertex));
    if (gl.vbo == NULL) { LOG("Failed to allocate vertex buffer\n"); c3dglClose(); return false; }

    gl.dvlb = DVLB_ParseFile((u32 *)c3dgl_vsh_shbin, c3dgl_vsh_shbin_size);
    shaderProgramInit(&gl.program);
    shaderProgramSetVsh(&gl.program, &gl.dvlb->DVLE[0]);
    C3D_BindProgram(&gl.program);
    gl.uLocMvp = shaderInstanceGetUniformLocation(gl.program.vertexShader, "mvp");
    gl.uLocTexScale = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texscale");

    // Vertex layout: v0 = position (3 floats), v1 = texcoord (2 floats), v2 = color (4 ubytes)
    C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attrInfo);
    AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 2);
    AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, gl.vbo, sizeof(Vertex), 3, 0x210);

    // Stored depth = -z_clip: near = 1, far = 0 (see depthFunc())
    C3D_DepthMap(true, -1.0f, 0.0f);

    // OpenGL clip space -> PICA clip space, taken from citro3d itself:
    // Mtx_OrthoTilt(-1,1,-1,1,-1,1) = post * glOrtho(-1,1,-1,1,-1,1) and glOrtho(...) = diag(1,1,-1,1)
    C3D_Mtx tilt;
    Mtx_OrthoTilt(&tilt, -1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f, false);
    mat4FromC3D(&tilt, &gl.post);
    for (int row = 0; row < 4; row++) gl.post.m[2*4 + row] = -gl.post.m[2*4 + row];

    // OpenGL default state
    gl.state.viewport[2] = gl.state.scissorBox[2] = C3DGL_TOP_SCREEN_WIDTH;
    gl.state.viewport[3] = gl.state.scissorBox[3] = C3DGL_SCREEN_HEIGHT;
    gl.state.blendSrc = GL_ONE;
    gl.state.blendDst = GL_ZERO;
    gl.state.depthFunc = GL_LESS;
    gl.state.depthMask = true;
    gl.state.alphaFunc = GL_ALWAYS;
    gl.state.colorMask = GPU_WRITE_COLOR;
    gl.state.cullFace = GL_BACK;
    gl.state.frontFace = GL_CCW;
    gl.unpackAlignment = gl.packAlignment = 4;
    gl.lineWidth = gl.pointSize = 1.0f;
    gl.clearColor = 0x000000FF;
    gl.clearDepth = 1.0f;
    memset(gl.current.color, 255, 4);

    for (int i = 0; i < 3; i++) mat4Identity(&gl.stack[i][0]);
    gl.matrixSerial = 1;

    gl.ready = true;
    return true;
}

void c3dglClose(void)
{
    if (gl.frameActive) { C3D_FrameEnd(0); gl.frameActive = false; }

    for (int i = 1; i < C3DGL_MAX_TEXTURES; i++) if (gl.textures[i].loaded) C3D_TexDelete(&gl.textures[i].tex);
    processDeferredDeletes();
    free(gl.deferredDeletes);

    if (gl.dvlb != NULL) { shaderProgramFree(&gl.program); DVLB_Free(gl.dvlb); }
    if (gl.vbo != NULL) linearFree(gl.vbo);
    for (int i = 0; i < C3DGL_SCREEN_COUNT; i++) if (gl.targets[i] != NULL) C3D_RenderTargetDelete(gl.targets[i]);
    C3D_Fini();

    memset(&gl, 0, sizeof(gl));
}

void c3dglSetScreen(C3DGLscreen screen)
{
    if ((screen != C3DGL_SCREEN_TOP) && (screen != C3DGL_SCREEN_BOTTOM)) return;

    if (gl.frameActive) flush();    // Pending vertices belong to the previous screen

    gl.screen = screen;
    linkTarget();

    // Viewport and scissor box of the new screen size, like a freshly bound framebuffer
    gl.state.viewport[0] = gl.state.viewport[1] = 0;
    gl.state.viewport[2] = screenWidth(screen);
    gl.state.viewport[3] = C3DGL_SCREEN_HEIGHT;
    memcpy(gl.state.scissorBox, gl.state.viewport, sizeof(gl.state.viewport));

    if (gl.frameActive)
    {
        C3D_FrameDrawOn(gl.targets[screen]);
        gl.batchValid = false;
    }
}

C3DGLscreen c3dglGetScreen(void)
{
    return gl.screen;
}

int c3dglGetScreenWidth(C3DGLscreen screen)
{
    return screenWidth(screen);
}

void c3dglSwapBuffers(void)
{
    ensureFrame();      // Present even if nothing was drawn
    flush();
    C3D_FrameEnd(0);
    gl.frameActive = false;
}

//----------------------------------------------------------------------------------
// OpenGL: state
//----------------------------------------------------------------------------------
static void setCapability(GLenum cap, bool enable)
{
    switch (cap)
    {
        case GL_TEXTURE_2D: gl.texture2D = enable; break;
        case GL_BLEND: gl.state.blend = enable; break;
        case GL_DEPTH_TEST: gl.state.depthTest = enable; break;
        case GL_ALPHA_TEST: gl.state.alphaTest = enable; break;
        case GL_CULL_FACE: gl.state.cull = enable; break;
        case GL_SCISSOR_TEST: gl.state.scissor = enable; break;
        case GL_LINE_SMOOTH: break;
        default: WARN_ONCE("glEnable/glDisable: capability 0x%x not supported\n", cap); break;
    }
}

void glEnable(GLenum cap) { setCapability(cap, true); }
void glDisable(GLenum cap) { setCapability(cap, false); }

static void setClientState(GLenum array, bool enable)
{
    switch (array)
    {
        case GL_VERTEX_ARRAY: gl.arrays[ARRAY_VERTEX].enabled = enable; break;
        case GL_TEXTURE_COORD_ARRAY: gl.arrays[ARRAY_TEXCOORD].enabled = enable; break;
        case GL_COLOR_ARRAY: gl.arrays[ARRAY_COLOR].enabled = enable; break;
        default: break;     // Normals are not used (no lighting)
    }
}

void glEnableClientState(GLenum array) { setClientState(array, true); }
void glDisableClientState(GLenum array) { setClientState(array, false); }

void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
void glShadeModel(GLenum mode) { (void)mode; }

void glPixelStorei(GLenum pname, GLint param)
{
    if (pname == GL_UNPACK_ALIGNMENT) gl.unpackAlignment = param;
    else if (pname == GL_PACK_ALIGNMENT) gl.packAlignment = param;
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    switch (pname)
    {
        case GL_MODELVIEW_MATRIX: memcpy(params, gl.stack[0][gl.stackDepth[0]].m, 16*sizeof(float)); break;
        case GL_PROJECTION_MATRIX: memcpy(params, gl.stack[1][gl.stackDepth[1]].m, 16*sizeof(float)); break;
        case GL_LINE_WIDTH: params[0] = gl.lineWidth; break;
        case GL_POINT_SIZE: params[0] = gl.pointSize; break;
        default: WARN_ONCE("glGetFloatv: 0x%x not supported\n", pname); break;
    }
}

const GLubyte *glGetString(GLenum name)
{
    switch (name)
    {
        case GL_VENDOR: return (const GLubyte *)"c3dgl";
        case GL_RENDERER: return (const GLubyte *)"citro3d (PICA200)";
        case GL_VERSION: return (const GLubyte *)"1.1 c3dgl";
        default: return (const GLubyte *)"";
    }
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    gl.state.viewport[0] = x;
    gl.state.viewport[1] = y;
    gl.state.viewport[2] = width;
    gl.state.viewport[3] = height;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    gl.state.scissorBox[0] = x;
    gl.state.scissorBox[1] = y;
    gl.state.scissorBox[2] = width;
    gl.state.scissorBox[3] = height;
}

static u8 colorByte(float c)
{
    if (c <= 0.0f) return 0;
    if (c >= 1.0f) return 255;
    return (u8)(c*255.0f + 0.5f);
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    gl.clearColor = ((u32)colorByte(red) << 24) | ((u32)colorByte(green) << 16) | ((u32)colorByte(blue) << 8) | colorByte(alpha);
}

void glClearDepth(GLclampd depth) { gl.clearDepth = (float)depth; }

void glClear(GLbitfield mask)
{
    int bits = 0;
    if (mask & GL_COLOR_BUFFER_BIT) bits |= C3D_CLEAR_COLOR;
    if (mask & GL_DEPTH_BUFFER_BIT) bits |= C3D_CLEAR_DEPTH;
    if (bits == 0) return;

    ensureFrame();
    flush();

    // Clears run as memory fills outside the command list; split it so earlier draws stay before the clear
    if (gl.drawnThisFrame) C3D_FrameSplit(0);

    float depth = 1.0f - gl.clearDepth;     // Reversed depth, see depthFunc()
    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;
    C3D_RenderTargetClear(gl.targets[gl.screen], (C3D_ClearBits)bits, gl.clearColor, (u32)(depth*0xFFFFFF));
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    gl.state.colorMask = (red? GPU_WRITE_RED : 0) | (green? GPU_WRITE_GREEN : 0) | (blue? GPU_WRITE_BLUE : 0) | (alpha? GPU_WRITE_ALPHA : 0);
}

void glDepthMask(GLboolean flag) { gl.state.depthMask = flag; }
void glDepthFunc(GLenum func) { gl.state.depthFunc = func; }

void glAlphaFunc(GLenum func, GLclampf ref)
{
    gl.state.alphaFunc = func;
    gl.state.alphaRef = colorByte(ref);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    gl.state.blendSrc = sfactor;
    gl.state.blendDst = dfactor;
}

void glCullFace(GLenum mode) { gl.state.cullFace = mode; }
void glFrontFace(GLenum mode) { gl.state.frontFace = mode; }

void glPolygonMode(GLenum face, GLenum mode)
{
    (void)face;
    if (mode != GL_FILL) WARN_ONCE("glPolygonMode: only GL_FILL is supported\n");
}

void glLineWidth(GLfloat width) { gl.lineWidth = width; }
void glPointSize(GLfloat size) { gl.pointSize = size; }

//----------------------------------------------------------------------------------
// OpenGL: matrices
//----------------------------------------------------------------------------------
void glMatrixMode(GLenum mode)
{
    switch (mode)
    {
        case GL_MODELVIEW: gl.matrixMode = 0; break;
        case GL_PROJECTION: gl.matrixMode = 1; break;
        case GL_TEXTURE: gl.matrixMode = 2; break;
        default: break;
    }
}

void glPushMatrix(void)
{
    int *depth = &gl.stackDepth[gl.matrixMode];
    if (*depth + 1 >= C3DGL_MATRIX_STACK) { WARN_ONCE("Matrix stack overflow\n"); return; }

    gl.stack[gl.matrixMode][*depth + 1] = gl.stack[gl.matrixMode][*depth];
    (*depth)++;
}

void glPopMatrix(void)
{
    int *depth = &gl.stackDepth[gl.matrixMode];
    if (*depth == 0) { WARN_ONCE("Matrix stack underflow\n"); return; }

    (*depth)--;
    matrixChanged();
}

void glLoadIdentity(void)
{
    mat4Identity(currentMatrix());
    matrixChanged();
}

void glMultMatrixf(const GLfloat *m)
{
    Mat4 mat;
    memcpy(mat.m, m, sizeof(mat.m));
    multCurrent(&mat);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    Mat4 m;
    mat4Identity(&m);
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    multCurrent(&m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float len = sqrtf(x*x + y*y + z*z);
    if (len == 0.0f) return;
    x /= len; y /= len; z /= len;

    float rad = angle*(float)M_PI/180.0f;
    float c = cosf(rad), s = sinf(rad), t = 1.0f - c;

    Mat4 m;
    mat4Identity(&m);
    m.m[0] = x*x*t + c;     m.m[4] = x*y*t - z*s;   m.m[8] = x*z*t + y*s;
    m.m[1] = y*x*t + z*s;   m.m[5] = y*y*t + c;     m.m[9] = y*z*t - x*s;
    m.m[2] = z*x*t - y*s;   m.m[6] = z*y*t + x*s;   m.m[10] = z*z*t + c;
    multCurrent(&m);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    Mat4 m;
    mat4Identity(&m);
    m.m[0] = x;
    m.m[5] = y;
    m.m[10] = z;
    multCurrent(&m);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    Mat4 m;
    mat4Identity(&m);
    m.m[0] = (float)(2.0/(right - left));
    m.m[5] = (float)(2.0/(top - bottom));
    m.m[10] = (float)(-2.0/(zFar - zNear));
    m.m[12] = (float)(-(right + left)/(right - left));
    m.m[13] = (float)(-(top + bottom)/(top - bottom));
    m.m[14] = (float)(-(zFar + zNear)/(zFar - zNear));
    multCurrent(&m);
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    Mat4 m;
    memset(&m, 0, sizeof(m));
    m.m[0] = (float)(2.0*zNear/(right - left));
    m.m[5] = (float)(2.0*zNear/(top - bottom));
    m.m[8] = (float)((right + left)/(right - left));
    m.m[9] = (float)((top + bottom)/(top - bottom));
    m.m[10] = (float)(-(zFar + zNear)/(zFar - zNear));
    m.m[11] = -1.0f;
    m.m[14] = (float)(-2.0*zFar*zNear/(zFar - zNear));
    multCurrent(&m);
}

//----------------------------------------------------------------------------------
// OpenGL: immediate mode
//----------------------------------------------------------------------------------
void glBegin(GLenum mode)
{
    gl.inBegin = beginPrimitive(mode);
}

void glEnd(void)
{
    if (gl.inBegin) endPrimitive();
    gl.inBegin = false;
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    if (!gl.inBegin) return;

    Vertex v = gl.current;
    v.pos[0] = x;
    v.pos[1] = y;
    v.pos[2] = z;
    submitVertex(&v);
}

void glVertex2f(GLfloat x, GLfloat y) { glVertex3f(x, y, 0.0f); }
void glVertex2i(GLint x, GLint y) { glVertex3f((float)x, (float)y, 0.0f); }

void glTexCoord2f(GLfloat s, GLfloat t)
{
    gl.current.uv[0] = s;
    gl.current.uv[1] = t;
}

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) { (void)nx; (void)ny; (void)nz; }

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    gl.current.color[0] = red;
    gl.current.color[1] = green;
    gl.current.color[2] = blue;
    gl.current.color[3] = alpha;
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    glColor4ub(colorByte(red), colorByte(green), colorByte(blue), colorByte(alpha));
}

void glColor3f(GLfloat red, GLfloat green, GLfloat blue) { glColor4f(red, green, blue, 1.0f); }

//----------------------------------------------------------------------------------
// OpenGL: client-side vertex arrays
//----------------------------------------------------------------------------------
static void setArray(int index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    gl.arrays[index].pointer = pointer;
    gl.arrays[index].size = size;
    gl.arrays[index].type = type;
    gl.arrays[index].stride = stride;
}

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { setArray(ARRAY_VERTEX, size, type, stride, pointer); }
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { setArray(ARRAY_TEXCOORD, size, type, stride, pointer); }
void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer) { setArray(ARRAY_COLOR, size, type, stride, pointer); }
void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer) { (void)type; (void)stride; (void)pointer; }

static const u8 *arrayElement(const ClientArray *a, int index, int componentSize)
{
    int stride = a->stride? a->stride : a->size*componentSize;
    return (const u8 *)a->pointer + (size_t)index*stride;
}

static void submitArrayVertex(int index)
{
    Vertex v = gl.current;

    const ClientArray *pos = &gl.arrays[ARRAY_VERTEX];
    const float *p = (const float *)arrayElement(pos, index, sizeof(float));
    v.pos[0] = p[0];
    v.pos[1] = (pos->size > 1)? p[1] : 0.0f;
    v.pos[2] = (pos->size > 2)? p[2] : 0.0f;

    const ClientArray *tc = &gl.arrays[ARRAY_TEXCOORD];
    if (tc->enabled && (tc->pointer != NULL))
    {
        const float *t = (const float *)arrayElement(tc, index, sizeof(float));
        v.uv[0] = t[0];
        v.uv[1] = t[1];
    }

    const ClientArray *col = &gl.arrays[ARRAY_COLOR];
    if (col->enabled && (col->pointer != NULL))
    {
        if (col->type == GL_UNSIGNED_BYTE)
        {
            const u8 *c = arrayElement(col, index, 1);
            for (int i = 0; i < 4; i++) v.color[i] = (i < col->size)? c[i] : 255;
        }
        else
        {
            const float *c = (const float *)arrayElement(col, index, sizeof(float));
            for (int i = 0; i < 4; i++) v.color[i] = (i < col->size)? colorByte(c[i]) : 255;
        }
    }

    submitVertex(&v);
}

static bool arraysReady(void)
{
    const ClientArray *pos = &gl.arrays[ARRAY_VERTEX];
    if (!pos->enabled || (pos->pointer == NULL)) return false;
    if (pos->type != GL_FLOAT) { WARN_ONCE("Only GL_FLOAT vertex arrays are supported\n"); return false; }
    return true;
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (!arraysReady() || !beginPrimitive(mode)) return;

    for (int i = 0; i < count; i++) submitArrayVertex(first + i);
    endPrimitive();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if (!arraysReady() || !beginPrimitive(mode)) return;

    for (int i = 0; i < count; i++)
    {
        int index = 0;
        if (type == GL_UNSIGNED_SHORT) index = ((const GLushort *)indices)[i];
        else if (type == GL_UNSIGNED_INT) index = (int)((const GLuint *)indices)[i];
        else index = ((const GLubyte *)indices)[i];
        submitArrayVertex(index);
    }
    endPrimitive();
}

//----------------------------------------------------------------------------------
// OpenGL: textures
//----------------------------------------------------------------------------------
static int nextPow2(int v)
{
    int p = 8;      // PICA minimum texture size
    while (p < v) p <<= 1;
    return p;
}

// Byte offset of pixel (x, y) in a Morton-swizzled PICA texture (8x8 tiles, Z-order inside a tile)
static u32 tiledOffset(const C3D_Tex *tex, int x, int y, int bpp)
{
#if C3DGL_TEXTURE_FLIP_Y
    y = tex->height - 1 - y;
#endif
    u32 tile = (u32)((y >> 3)*(tex->width >> 3) + (x >> 3));
    u32 morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return (tile*64 + morton)*bpp;
}

// Copy a rectangle between linear GL pixel data and the swizzled texture (in either direction)
static void transferPixels(Texture *t, int x0, int y0, int w, int h, u8 *pixels, int alignment, bool upload)
{
    const TexFormat *f = &t->format;
    int rowBytes = w*f->bpp;
    if (alignment > 1) rowBytes = (rowBytes + alignment - 1)/alignment*alignment;

    u8 *texData = (u8 *)t->tex.data;
    for (int y = 0; y < h; y++)
    {
        u8 *row = pixels + (size_t)y*rowBytes;
        for (int x = 0; x < w; x++)
        {
            u8 *src = row + x*f->bpp;
            u8 *dst = texData + tiledOffset(&t->tex, x0 + x, y0 + y, f->bpp);
            for (int i = 0; i < f->bpp; i++)
            {
                int j = f->reverse? (f->bpp - 1 - i) : i;
                if (upload) dst[j] = src[i];
                else src[i] = dst[j];
            }
        }
    }
}

static Texture *boundTexture(GLenum target)
{
    if ((target != GL_TEXTURE_2D) || (gl.boundTexture == 0) || (gl.boundTexture >= C3DGL_MAX_TEXTURES)) return NULL;
    return &gl.textures[gl.boundTexture];
}

// Texture about to change: submit pending vertices that use it and rebind it for the next draw
// (C3D_TexBind() only keeps a pointer, changes are not picked up otherwise)
static void textureModified(GLuint id)
{
    if (gl.batchValid && (gl.batch.texture == id))
    {
        flush();
        gl.batchValid = false;
    }
}

static void applyTextureParams(Texture *t)
{
    C3D_TexSetFilter(&t->tex, texFilter(t->magFilter), texFilter(t->minFilter));
    C3D_TexSetWrap(&t->tex, texWrap(t->wrapS), texWrap(t->wrapT));
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    GLuint id = 1;
    for (int i = 0; i < n; i++)
    {
        while ((id < C3DGL_MAX_TEXTURES) && gl.textures[id].used) id++;
        if (id >= C3DGL_MAX_TEXTURES) { LOG("Out of texture ids\n"); textures[i] = 0; continue; }

        Texture *t = &gl.textures[id];
        memset(t, 0, sizeof(*t));
        t->used = true;
        t->minFilter = GL_NEAREST_MIPMAP_LINEAR;    // OpenGL defaults
        t->magFilter = GL_LINEAR;
        t->wrapS = t->wrapT = GL_REPEAT;
        textures[i] = id;
    }
}

void glDeleteTextures(GLsizei n, const GLuint *textures)
{
    for (int i = 0; i < n; i++)
    {
        GLuint id = textures[i];
        if ((id == 0) || (id >= C3DGL_MAX_TEXTURES) || !gl.textures[id].used) continue;

        textureModified(id);

        Texture *t = &gl.textures[id];
        if (t->loaded)
        {
            // The GPU may still read it in the current frame
            if (gl.frameActive) deferTextureDelete(&t->tex);
            else C3D_TexDelete(&t->tex);
        }
        memset(t, 0, sizeof(*t));
        if (gl.boundTexture == id) gl.boundTexture = 0;
    }
}

void glBindTexture(GLenum target, GLuint texture)
{
    if (target == GL_TEXTURE_2D) gl.boundTexture = texture;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    Texture *t = boundTexture(target);
    if (t == NULL) return;

    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER: t->minFilter = param; break;
        case GL_TEXTURE_MAG_FILTER: t->magFilter = param; break;
        case GL_TEXTURE_WRAP_S: t->wrapS = param; break;
        case GL_TEXTURE_WRAP_T: t->wrapT = param; break;
        default: return;
    }

    if (t->loaded)
    {
        textureModified(gl.boundTexture);
        applyTextureParams(t);
    }
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)internalformat; (void)border;

    Texture *t = boundTexture(target);
    if ((t == NULL) || (level != 0)) return;     // Mipmaps are not supported, only level 0 is used

    TexFormat f;
    if (!texFormat(format, type, &f)) { LOG("glTexImage2D: format 0x%x/0x%x not supported\n", format, type); return; }

    int texWidth = nextPow2(width), texHeight = nextPow2(height);
    if ((texWidth > C3DGL_MAX_TEXTURE_SIZE) || (texHeight > C3DGL_MAX_TEXTURE_SIZE))
    {
        LOG("glTexImage2D: %ix%i exceeds the maximum of %i\n", width, height, C3DGL_MAX_TEXTURE_SIZE);
        return;
    }

    textureModified(gl.boundTexture);
    if (t->loaded)
    {
        if (gl.frameActive) deferTextureDelete(&t->tex);
        else C3D_TexDelete(&t->tex);
        t->loaded = false;
    }

    if (!C3D_TexInit(&t->tex, texWidth, texHeight, f.format))
    {
        LOG("glTexImage2D: out of memory for %ix%i texture\n", texWidth, texHeight);
        return;
    }

    t->loaded = true;
    t->format = f;
    t->width = width;
    t->height = height;

    memset(t->tex.data, 0, t->tex.size);
    if (pixels != NULL) transferPixels(t, 0, 0, width, height, (u8 *)pixels, gl.unpackAlignment, true);
    C3D_TexFlush(&t->tex);
    applyTextureParams(t);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels)
{
    Texture *t = boundTexture(target);
    if ((t == NULL) || !t->loaded || (level != 0) || (pixels == NULL)) return;

    TexFormat f;
    if (!texFormat(format, type, &f) || (f.format != t->format.format)) { LOG("glTexSubImage2D: format mismatch\n"); return; }
    if ((xoffset < 0) || (yoffset < 0) || (xoffset + width > t->width) || (yoffset + height > t->height)) return;

    textureModified(gl.boundTexture);
    transferPixels(t, xoffset, yoffset, width, height, (u8 *)pixels, gl.unpackAlignment, true);
    C3D_TexFlush(&t->tex);
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    Texture *t = boundTexture(target);
    if ((t == NULL) || !t->loaded || (level != 0)) return;

    TexFormat f;
    if (!texFormat(format, type, &f) || (f.format != t->format.format)) { LOG("glGetTexImage: format mismatch\n"); return; }

    transferPixels(t, 0, 0, t->width, t->height, (u8 *)pixels, gl.packAlignment, false);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    (void)x; (void)y; (void)format; (void)type;
    WARN_ONCE("glReadPixels not supported, returning black\n");
    memset(pixels, 0, (size_t)width*height*4);
}
