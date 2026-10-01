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
//   - Strips, fans, quads and polygons are split into triangles on the CPU, which also implements flat shading
//     (provoking vertex color), glPolygonMode (outlines/vertices of each polygon, culled on the CPU) and the
//     slope part of glPolygonOffset (per-vertex depth bias that the vertex shader adds).
//   - Lines and points have no PICA equivalent: they are transformed on the CPU and expanded to quads in NDC.
//   - Lighting is computed per vertex on the CPU when the vertex is submitted (exact GL 1.1 formula); the lit color
//     replaces the vertex color, so flat shading, lines and points need nothing special. Two-sided lighting also
//     computes the back color, emitPolygon() picks one per polygon from its facing.
//   - Textures are padded to power-of-two sizes and Morton-swizzled. The shader applies the texture matrix
//     combined with the scale back from the padded size; a projective texture matrix uses PICA's projection mode.
//   - One render target per screen, sharing the vertex buffer and GL state; c3dglSetScreen() flushes the
//     batch and switches the target (C3D_FrameDrawOn), citro3d presents every target drawn on in the frame.
//
//   - Depth and stencil share one D24S8 buffer that a memory fill can only clear as a whole. glClear uses the
//     fill when that is equivalent, otherwise it draws a full-screen quad (scissor, masks, depth or stencil only).
//
// Known limitations: no glReadPixels, REPEAT wrap on non-power-of-two textures samples the padding.
#include "GL/gl.h"
#include "c3dgl.h"

#include <3ds.h>
#include <citro3d.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "c3dgl_vsh_shbin.h"

//----------------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------------
#define C3DGL_MAX_VERTICES      (64*1024)   // Per frame, 24 bytes each
#define C3DGL_MAX_TEXTURES      512         // Texture ids 1..C3DGL_MAX_TEXTURES-1
#define C3DGL_MATRIX_STACK      32
#define C3DGL_TEXTURE_UNITS     3           // PICA texture units 0..2 (unit 3 is procedural only)
#define C3DGL_ATTRIB_STACK      16          // glPushAttrib / glPushClientAttrib depth (GL minimum)
#define C3DGL_MAX_EVAL_ORDER    30          // Evaluator order (GL minimum 8)
#define C3DGL_MAX_LIGHTS        8
#define C3DGL_MAX_TEXTURE_SIZE  1024
#define MAX_TEXTURE_LEVEL       10          // log2(C3DGL_MAX_TEXTURE_SIZE)

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
// Vertex as assembled on the CPU. The fields up to texExtra are vertex buffer 0 (always written); texExtra goes to
// vertex buffer 1, written only while texture units 1/2 are in use
typedef struct {
    float pos[3];
    float tex[3];               // Unit 0: s, t, q (r is not kept: only 2D textures)
    u8 color[4];
    float depthBias;            // Added to PICA NDC depth by the shader: polygon offset of filled polygons
    float texExtra[C3DGL_TEXTURE_UNITS - 1][3];     // Units 1, 2: s, t, q
    u8 backColor[4];            // Lit color of back faces (two-sided lighting only); not sent to the GPU
} Vertex;

#define GPU_VERTEX_SIZE     offsetof(Vertex, texExtra)
#define GPU_EXTRA_SIZE      sizeof(((Vertex *)0)->texExtra)

typedef struct {
    float ambient[4], diffuse[4], specular[4];
    float position[4];          // Eye coordinates (transformed by the modelview of the glLight call)
    float spotDirection[3];     // Eye coordinates
    float spotExponent, spotCutoff;
    float attenuation[3];       // Constant, linear, quadratic

    // Derived by updateLight(), so that lightVertex() does not redo them per vertex
    float unitPosition[3];      // Directional light: unit vector to the light
    float halfVector[3];        // Directional light, no local viewer: unit half vector
    float spotUnit[3];          // Unit spot direction
    float spotCos;              // cos(spotCutoff)
} Light;

typedef struct {
    float ambient[4], diffuse[4], specular[4], emission[4];
    float shininess;
    float colorIndexes[3];      // Color index mode, stored only
} Material;

// glLight, glLightModel, glMaterial and glColorMaterial state (GL_LIGHTING_BIT without the enables)
typedef struct {
    Light lights[C3DGL_MAX_LIGHTS];
    Material material[2];       // Front, back
    float modelAmbient[4];
    bool localViewer, twoSide;
    GLenum colorMaterialFace, colorMaterialMode;
} LightingState;

typedef struct {
    float m[16];                // Column-major, like OpenGL
} Mat4;

typedef struct {
    GPU_TEXCOLOR format;
    int bpp;                    // Bytes per pixel
    bool reverse;               // Byte order within a pixel is reversed on PICA (RGBA -> ABGR, ...)
    bool packed16;              // One 16-bit element per pixel (GL_UNSIGNED_SHORT_*): affected by *_SWAP_BYTES
} TexFormat;

// glPixelStore state for one direction (unpack: GL -> c3dgl, pack: c3dgl -> GL)
typedef struct {
    GLint alignment, rowLength, skipRows, skipPixels, imageHeight, skipImages;
    bool swapBytes, lsbFirst;
} PixelStore;

// Result of a glTexImage2D on GL_PROXY_TEXTURE_2D (all zero if the image would not fit)
typedef struct {
    GLint width, height, border, internalFormat;
    GPU_TEXCOLOR format;
} ProxyLevel;

// One mipmap level as GL sees it (also levels below 8x8, which PICA cannot store)
typedef struct {
    bool defined;
    int width, height;          // Without border
    int border;
    GLint internalFormat;
    GPU_TEXCOLOR format;
} TexLevel;

typedef struct {
    bool used;                  // Id handed out by glGenTextures
    bool loaded;                // tex is initialized (level 0 defined)
    C3D_Tex tex;                // Level 0 padded to power-of-two; mip chain down to 8x8 once a level > 0 arrives
    int levels;                 // Levels stored in tex (1 + tex.maxLevel when mipmapped)
    TexFormat format;
    int width, height;          // Level 0 image size without border
    TexLevel level[MAX_TEXTURE_LEVEL + 1];
    bool complete;              // All levels down to 1x1 defined and consistent (needed by mipmap filters)
    bool generateMipmap;        // GL_GENERATE_MIPMAP
    GLenum minFilter, magFilter, wrapS, wrapT;
} Texture;

// Texture environment of one unit (glTexEnv)
typedef struct {
    GLenum mode;                // GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND, GL_ADD, GL_COMBINE
    u32 color;                  // GL_TEXTURE_ENV_COLOR, 0xAABBGGRR like the PICA
    GLenum combineRgb, combineAlpha;
    GLenum srcRgb[3], srcAlpha[3], operandRgb[3], operandAlpha[3];
    u8 rgbScale, alphaScale;    // 1, 2, 4
} TexEnvState;

typedef struct {
    GLuint texture;             // 0: unit not used (set in prepareDraw)
    TexEnvState env;            // Zeroed for unused units, so they don't split batches
} TexUnitState;

// Everything that decides how a range of vertices is rendered; see prepareDraw()
typedef struct {
    TexUnitState units[C3DGL_TEXTURE_UNITS];
    bool clipSpace;             // Vertices are already in NDC (expanded lines and points)
    u32 matrixSerial;           // Matrix version (0 for clipSpace)
    u32 texMatrixSerial;        // Texture matrix version (0 when untextured)
    bool texQ;                  // Unit 0 texcoords with q != 1 were used (projection mode), only when textured
    bool blend;
    GLenum blendSrc, blendDst;
    bool depthTest, depthMask;
    GLenum depthFunc;
    float depthNear, depthFar;  // glDepthRange
    bool alphaTest;
    GLenum alphaFunc;
    u8 alphaRef;                // 0..255
    bool stencilTest;
    GLenum stencilFunc;
    u8 stencilRef, stencilFuncMask, stencilWriteMask;
    GLenum stencilFail, stencilDepthFail, stencilPass;
    u8 colorMask;
    bool cull;
    GLenum cullFace, frontFace;
    bool scissor;
    GLint scissorBox[4];
    GLint viewport[4];
} DrawState;

typedef struct {
    bool enabled;
    const void *pointer;        // Offset into `buffer` if that is not 0
    GLuint buffer;              // GL_ARRAY_BUFFER binding when the pointer was set
    GLint size;
    GLenum type;
    GLsizei stride;
} ClientArray;

// Buffer object (VBO). Kept in normal memory: vertices are converted into the per-frame vertex buffer anyway
typedef struct {
    bool used;                  // Id handed out by glGenBuffers or created by glBindBuffer
    u8 *data;
    GLsizeiptr size;
    GLenum usage;
} Buffer;

enum { ARRAY_VERTEX, ARRAY_TEXCOORD0, ARRAY_TEXCOORD1, ARRAY_TEXCOORD2, ARRAY_COLOR, ARRAY_NORMAL, ARRAY_EDGEFLAG, ARRAY_COUNT };

// Texcoord array of the client active unit (glClientActiveTexture)
#define ARRAY_TEXCOORD      (ARRAY_TEXCOORD0 + gl.clientActiveTexture)

//----------------------------------------------------------------------------------
// Global state
//----------------------------------------------------------------------------------
static struct {
    bool ready;
    C3D_RenderTarget *targets[C3DGL_SCREEN_COUNT];
    C3DGLscreen screen;                 // Screen drawn on, see c3dglSetScreen()
    DVLB_s *dvlb;
    shaderProgram_s program;
    int uLocMvp, uLocTexMat[C3DGL_TEXTURE_UNITS];
    Mat4 post;                          // OpenGL clip space -> PICA clip space (rotation, depth range)

    // Frame and vertex batching
    bool frameActive;
    bool drawnThisFrame;
    C3D_Tex dummyTexture;               // 8x8, bound to units 1/2 while they are unused (see applyState)
    u8 *vbo;                            // Vertex buffer 0, GPU_VERTEX_SIZE per vertex; linear memory, rewritten every frame
    float (*vboExtra)[C3DGL_TEXTURE_UNITS - 1][3];  // Vertex buffer 1: texcoords of units 1, 2
    int vertexCount;
    int batchStart;                     // First vertex not yet submitted
    DrawState batch;                    // State applied to the GPU for the current batch
    bool batchValid;

    // GL state as set by the gl* calls
    DrawState state;
    bool texture2D[C3DGL_TEXTURE_UNITS];
    GLuint boundTexture[C3DGL_TEXTURE_UNITS];
    int activeTexture, clientActiveTexture;     // glActiveTexture, glClientActiveTexture: 0..2
    PixelStore unpack, pack;
    ProxyLevel proxy2D[11];             // Per level, 1024 >> 10 = 1
    float lineWidth, pointSize;
    u32 clearColor;                     // 0xRRGGBBAA
    float clearDepth;
    u8 clearStencil;
    bool stencilUsed;                   // GL_STENCIL_TEST was enabled once: glClear must preserve stencil values
    GLenum error;                       // First error since the last glGetError()
    u32 ignoredCaps;                    // Capabilities accepted but not implemented, see ignoredCapBit()
    GLenum shadeModel;
    float currentNormal[3];
    float currentTexR[C3DGL_TEXTURE_UNITS];     // r of the current texcoords, only for glGet
    bool texQUsed;                      // A texcoord with q != 1 was submitted: sticky projection mode

    // Matrices
    int matrixMode;                     // 0: modelview, 1: projection, 2: texture (of the active unit)
    Mat4 stack[2 + C3DGL_TEXTURE_UNITS][C3DGL_MATRIX_STACK];    // Modelview, projection, texture per unit
    int stackDepth[2 + C3DGL_TEXTURE_UNITS];
    u32 matrixSerial;                   // Incremented on every modelview/projection change
    u32 texMatrixSerial;                // Incremented on every texture matrix change
    Mat4 pmv;                           // projection * modelview, cached for pmvSerial
    u32 pmvSerial;

    // Immediate mode
    bool inBegin;
    GLenum primitive;
    Vertex current;                     // Current texcoord and color
    Vertex prim[4];                     // Vertices kept for the primitive being assembled, see submitVertex()
    bool primEdge[4];                   // Their edge flags
    bool currentEdge;                   // glEdgeFlag
    GLenum polygonMode[2];              // Front, back
    bool offsetFill, offsetLine, offsetPoint;
    float offsetFactor, offsetUnits;

    // GL_POLYGON is collected until glEnd when it is not simply filled (outline, vertices)
    bool collectPolygon;
    Vertex *polyVerts;
    const Vertex **polyPtrs;
    bool *polyEdges;
    int polyCount, polyCapacity;
    int primCount;                      // Vertices in prim
    int primTotal;                      // Vertices submitted since glBegin/glDraw*

    ClientArray arrays[ARRAY_COUNT];
    Buffer *buffers;                    // Index = buffer id, grows as needed (id 0 unused)
    GLuint bufferCount;
    GLuint arrayBuffer, elementArrayBuffer;     // Bindings
    int attribDepth, clientAttribDepth;         // glPushAttrib / glPushClientAttrib stacks (see attribStack)

    // Evaluators (glMap1/2): 9 maps each, in the order of GL_MAP1_COLOR_4 .. GL_MAP1_VERTEX_4
    struct EvalMap {
        bool enabled;
        int uorder, vorder;             // vorder = 1 for 1D maps
        float u1, u2, v1, v2;
        float *points;                  // uorder*vorder*k floats, NULL if never defined
    } map1[9], map2[9];
    bool autoNormal;
    int grid1n, grid2un, grid2vn;       // glMapGrid
    float grid1u1, grid1u2, grid2u1, grid2u2, grid2v1, grid2v2;

    // Lighting, see lightVertex()
    LightingState lighting;
    bool lightingEnabled, colorMaterial, normalize, rescaleNormal;
    u8 lightEnabled;                    // Bit per light
    float normalMatrix[9];              // Inverse transpose of the modelview's upper 3x3 (row-major), for normalSerial
    float normalRescale;                // GL_RESCALE_NORMAL factor
    u32 normalSerial;

    Texture textures[C3DGL_MAX_TEXTURES];

    // Textures deleted during a frame are freed once the GPU is done with that frame
    C3D_Tex *deferredDeletes;
    int deferredCount, deferredCapacity;
} gl;

// Record an error for glGetError(); like OpenGL, only the first one is kept until it is read
static void setError(GLenum error)
{
    if (gl.error == GL_NO_ERROR) gl.error = error;
}

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

// Index into gl.stack: 0 modelview, 1 projection, 2 + unit for the texture matrix of the active unit
static int matrixStack(void)
{
    return (gl.matrixMode == 2)? 2 + gl.activeTexture : gl.matrixMode;
}

static Mat4 *currentMatrix(void)
{
    return &gl.stack[matrixStack()][gl.stackDepth[matrixStack()]];
}

static void matrixChanged(void)
{
    if (gl.matrixMode != 2) gl.matrixSerial++;
    else gl.texMatrixSerial++;
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

static GPU_STENCILOP stencilOp(GLenum op)
{
    switch (op)
    {
        case GL_ZERO: return GPU_STENCIL_ZERO;
        case GL_REPLACE: return GPU_STENCIL_REPLACE;
        case GL_INCR: return GPU_STENCIL_INCR;
        case GL_DECR: return GPU_STENCIL_DECR;
        case GL_INVERT: return GPU_STENCIL_INVERT;
        case GL_INCR_WRAP: return GPU_STENCIL_INCR_WRAP;
        case GL_DECR_WRAP: return GPU_STENCIL_DECR_WRAP;
        default: return GPU_STENCIL_KEEP;
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

static bool mipmapFilter(GLenum f)
{
    return (f == GL_NEAREST_MIPMAP_NEAREST) || (f == GL_LINEAR_MIPMAP_NEAREST) ||
           (f == GL_NEAREST_MIPMAP_LINEAR) || (f == GL_LINEAR_MIPMAP_LINEAR);
}

// GL_CLAMP (GL 1.1) would blend in the border color at the edges; without border texels clamping to the edge is
// the usual approximation
static GPU_TEXTURE_WRAP_PARAM texWrap(GLenum w)
{
    switch (w)
    {
        case GL_CLAMP: case GL_CLAMP_TO_EDGE: return GPU_CLAMP_TO_EDGE;
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
            case GL_RGBA: *out = (TexFormat){ GPU_RGBA8, 4, true, false }; return true;
            case GL_RGB: *out = (TexFormat){ GPU_RGB8, 3, true, false }; return true;
            case GL_LUMINANCE_ALPHA: *out = (TexFormat){ GPU_LA8, 2, true, false }; return true;
            case GL_LUMINANCE: *out = (TexFormat){ GPU_L8, 1, false, false }; return true;
            case GL_ALPHA: *out = (TexFormat){ GPU_A8, 1, false, false }; return true;
            default: return false;
        }
    }

    // Packed 16-bit formats have the same bit layout on PICA
    if ((format == GL_RGB) && (type == GL_UNSIGNED_SHORT_5_6_5)) { *out = (TexFormat){ GPU_RGB565, 2, false, true }; return true; }
    if ((format == GL_RGBA) && (type == GL_UNSIGNED_SHORT_5_5_5_1)) { *out = (TexFormat){ GPU_RGBA5551, 2, false, true }; return true; }
    if ((format == GL_RGBA) && (type == GL_UNSIGNED_SHORT_4_4_4_4)) { *out = (TexFormat){ GPU_RGBA4, 2, false, true }; return true; }

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

    GSPGPU_FlushDataCache(gl.vbo + (size_t)gl.batchStart*GPU_VERTEX_SIZE, count*GPU_VERTEX_SIZE);
    if (gl.batch.units[1].texture || gl.batch.units[2].texture)
        GSPGPU_FlushDataCache(&gl.vboExtra[gl.batchStart], count*GPU_EXTRA_SIZE);
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

// Texture environment of unit `unit` = TexEnv stage `unit`. GL's fragment color Cf of unit n is the result of
// unit n - 1 (GL_PREVIOUS), the primary color for unit 0.
//
// Classic modes: GL 1.1 table 3.22. The result depends on the texture's base format: PICA samples L as (L, L, L, 1),
// A as (0, 0, 0, A) and formats without alpha with A = 1, which matches GL's (Lt, Ct, At) except where GL takes
// the fragment color/alpha instead (no color in A textures, REPLACE without alpha)
static GPU_TEVSRC texSource(int unit) { return (GPU_TEVSRC)(GPU_TEXTURE0 + unit); }
static GPU_TEVSRC previousSource(int unit) { return unit? GPU_PREVIOUS : GPU_PRIMARY_COLOR; }

static GPU_TEVSRC combineSource(GLenum src, int unit)
{
    switch (src)
    {
        case GL_TEXTURE: return texSource(unit);
        case GL_TEXTURE0: case GL_TEXTURE1: case GL_TEXTURE2: return texSource(src - GL_TEXTURE0);   // Crossbar
        case GL_CONSTANT: return GPU_CONSTANT;
        case GL_PRIMARY_COLOR: return GPU_PRIMARY_COLOR;
        default: return previousSource(unit);     // GL_PREVIOUS
    }
}

static GPU_COMBINEFUNC combineFunc(GLenum f)
{
    switch (f)
    {
        case GL_REPLACE: return GPU_REPLACE;
        case GL_ADD: return GPU_ADD;
        case GL_ADD_SIGNED: return GPU_ADD_SIGNED;
        case GL_INTERPOLATE: return GPU_INTERPOLATE;
        case GL_SUBTRACT: return GPU_SUBTRACT;
        case GL_DOT3_RGB: return GPU_DOT3_RGB;
        case GL_DOT3_RGBA: return GPU_DOT3_RGBA;
        default: return GPU_MODULATE;
    }
}

static GPU_TEVSCALE tevScale(u8 scale) { return (scale == 4)? GPU_TEVSCALE_4 : (scale == 2)? GPU_TEVSCALE_2 : GPU_TEVSCALE_1; }

// GL_COMBINE: arguments and functions map 1:1 onto a PICA TexEnv stage
static void setupCombine(C3D_TexEnv *env, int unit, const TexEnvState *e)
{
    GPU_TEVOP_RGB opRgb[3];
    GPU_TEVOP_A opAlpha[3];
    for (int i = 0; i < 3; i++)
    {
        opRgb[i] = (e->operandRgb[i] == GL_ONE_MINUS_SRC_COLOR)? GPU_TEVOP_RGB_ONE_MINUS_SRC_COLOR :
                   (e->operandRgb[i] == GL_SRC_ALPHA)? GPU_TEVOP_RGB_SRC_ALPHA :
                   (e->operandRgb[i] == GL_ONE_MINUS_SRC_ALPHA)? GPU_TEVOP_RGB_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_RGB_SRC_COLOR;
        opAlpha[i] = (e->operandAlpha[i] == GL_ONE_MINUS_SRC_ALPHA)? GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA : GPU_TEVOP_A_SRC_ALPHA;
    }

    C3D_TexEnvSrc(env, C3D_RGB, combineSource(e->srcRgb[0], unit), combineSource(e->srcRgb[1], unit), combineSource(e->srcRgb[2], unit));
    C3D_TexEnvSrc(env, C3D_Alpha, combineSource(e->srcAlpha[0], unit), combineSource(e->srcAlpha[1], unit), combineSource(e->srcAlpha[2], unit));
    C3D_TexEnvOpRgb(env, opRgb[0], opRgb[1], opRgb[2]);
    C3D_TexEnvOpAlpha(env, opAlpha[0], opAlpha[1], opAlpha[2]);
    C3D_TexEnvFunc(env, C3D_RGB, combineFunc(e->combineRgb));
    C3D_TexEnvFunc(env, C3D_Alpha, combineFunc(e->combineAlpha));   // Ignored by PICA for DOT3_RGBA, like in GL
    C3D_TexEnvScale(env, C3D_RGB, tevScale(e->rgbScale));
    C3D_TexEnvScale(env, C3D_Alpha, tevScale(e->alphaScale));
}

static void setupTexEnv(C3D_TexEnv *env, int unit, const TexEnvState *e, GPU_TEXCOLOR format)
{
    C3D_TexEnvColor(env, e->color);
    if (e->mode == GL_COMBINE) { setupCombine(env, unit, e); return; }

    GLenum mode = e->mode;
    GPU_TEVSRC tex = texSource(unit), prev = previousSource(unit);
    bool hasColor = (format != GPU_A8) && (format != GPU_A4);
    bool hasAlpha = (format == GPU_RGBA8) || (format == GPU_RGBA5551) || (format == GPU_RGBA4) ||
                    (format == GPU_LA8) || (format == GPU_LA4) || (format == GPU_A8) || (format == GPU_A4);

    // Color: Cf = previous color, Ct = texture, Cc = env color
    if (!hasColor)
    {
        C3D_TexEnvSrc(env, C3D_RGB, prev, prev, prev);
        C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
    }
    else switch (mode)
    {
        case GL_REPLACE:
            C3D_TexEnvSrc(env, C3D_RGB, tex, prev, prev);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
            break;
        case GL_DECAL:      // Cf*(1 - At) + Ct*At
            C3D_TexEnvSrc(env, C3D_RGB, tex, prev, tex);
            C3D_TexEnvOpRgb(env, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
            break;
        case GL_BLEND:      // Cf*(1 - Ct) + Cc*Ct
            C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, prev, tex);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
            break;
        case GL_ADD:
            C3D_TexEnvSrc(env, C3D_RGB, prev, tex, prev);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_ADD);
            break;
        default:            // GL_MODULATE
            C3D_TexEnvSrc(env, C3D_RGB, tex, prev, prev);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_MODULATE);
            break;
    }

    // Alpha: REPLACE takes At, DECAL keeps Af, everything else is Af*At (At = 1 without alpha)
    if ((mode == GL_DECAL) || ((mode == GL_REPLACE) && !hasAlpha))
    {
        C3D_TexEnvSrc(env, C3D_Alpha, prev, prev, prev);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    }
    else if (mode == GL_REPLACE)
    {
        C3D_TexEnvSrc(env, C3D_Alpha, tex, prev, prev);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
    }
    else
    {
        C3D_TexEnvSrc(env, C3D_Alpha, tex, prev, prev);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
    }
}

// Texture matrix of `unit` as shader uniform rows s, t, q; s and t scaled from the image to the padded texture size
static void applyTextureMatrix(int unit, const Texture *t, bool *projective)
{
    const Mat4 *tm = &gl.stack[2 + unit][gl.stackDepth[2 + unit]];
    float scale[2] = { (float)t->width/t->tex.width, (float)t->height/t->tex.height };
    for (int row = 0; row < 2; row++)
        C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[unit] + row, tm->m[row]*scale[row], tm->m[4 + row]*scale[row],
                      tm->m[8 + row]*scale[row], tm->m[12 + row]*scale[row]);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[unit] + 2, tm->m[3], tm->m[7], tm->m[11], tm->m[15]);

    // q != 1 (r is always 0, so m[11] does not matter)
    *projective = (tm->m[3] != 0.0f) || (tm->m[7] != 0.0f) || (tm->m[15] != 1.0f);
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

    // Stored depth = 1 - window depth (see depthFunc()), window depth = n + (f - n)*(z_pica + 1) with z_pica in [-1, 0]
    C3D_DepthMap(true, -(s->depthFar - s->depthNear), 1.0f - s->depthFar);

    GPU_WRITEMASK writeMask = (GPU_WRITEMASK)(s->colorMask | ((s->depthTest && s->depthMask)? GPU_WRITE_DEPTH : 0));
    C3D_DepthTest(s->depthTest, s->depthTest? depthFunc(s->depthFunc) : GPU_ALWAYS, writeMask);
    C3D_AlphaTest(s->alphaTest, testFunc(s->alphaFunc), s->alphaRef);

    if (s->stencilTest)
    {
        C3D_StencilTest(true, testFunc(s->stencilFunc), s->stencilRef, s->stencilFuncMask, s->stencilWriteMask);
        C3D_StencilOp(stencilOp(s->stencilFail), stencilOp(s->stencilDepthFail), stencilOp(s->stencilPass));
    }
    else C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);

    if (s->blend)
    {
        GPU_BLENDFACTOR src = blendFactor(s->blendSrc), dst = blendFactor(s->blendDst);
        C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, src, dst, src, dst);
    }
    else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);

    C3D_CullFace(cullMode(s));

    // Fragment stage: TexEnv stage n combines texture unit n with the result of stage n - 1 (glTexEnv per unit)
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        C3D_TexEnv *env = C3D_GetTexEnv(unit);
        C3D_TexEnvInit(env);
        const TexUnitState *u = &s->units[unit];
        if (u->texture == 0)
        {
            // Pass the previous color through (stage 0: the vertex color)
            C3D_TexEnvSrc(env, C3D_Both, previousSource(unit), GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

            // Unit 0 is switched off with NULL. C3D_TexBind reads the texture type for units 1/2 (only 2D allowed
            // there), so NULL would be dereferenced: they get a dummy texture instead (never sampled, the stage
            // does not use it)
            C3D_TexBind(unit, unit? &gl.dummyTexture : NULL);
            continue;
        }

        Texture *t = &gl.textures[u->texture];
        bool projective;
        applyTextureMatrix(unit, t, &projective);

        // Unit 0 can let PICA divide s and t by q per pixel (projection mode); units 1/2 divide per vertex in the shader
        if (unit == 0)
        {
            projective = projective || s->texQ;
            t->tex.param = (t->tex.param & ~GPU_TEXTURE_MODE(7)) | GPU_TEXTURE_MODE(projective? GPU_TEX_PROJECTION : GPU_TEX_2D);
        }
        else t->tex.param = (t->tex.param & ~GPU_TEXTURE_MODE(7)) | GPU_TEXTURE_MODE(GPU_TEX_2D);

        C3D_TexBind(unit, &t->tex);
        setupTexEnv(env, unit, &u->env, t->format.format);
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

// Start a new batch if key differs from the applied state. memcmp: keys are built with zeroed padding
static void useState(const DrawState *key)
{
    ensureFrame();

    if (!gl.batchValid || (memcmp(key, &gl.batch, sizeof(DrawState)) != 0))
    {
        flush();
        applyState(key);
        memcpy(&gl.batch, key, sizeof(DrawState));
        gl.batchValid = true;
    }
}

// Call before emitting vertices: starts a new batch if the draw state changed
static void prepareDraw(bool clipSpace)
{
    DrawState key;
    memcpy(&key, &gl.state, sizeof(DrawState));
    key.clipSpace = clipSpace;
    key.matrixSerial = clipSpace? 0 : gl.matrixSerial;
    bool textured = false;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        TexUnitState *u = &key.units[unit];
        u->texture = (gl.texture2D[unit] && textureValid(gl.boundTexture[unit]))? gl.boundTexture[unit] : 0;
        if (u->texture && mipmapFilter(gl.textures[u->texture].minFilter) && !gl.textures[u->texture].complete)
        {
            // GL: a mipmap filter without all levels disables the unit
            WARN_ONCE("Texture %u has a mipmap min filter but not all mipmap levels: texturing disabled "
                      "(set GL_TEXTURE_MIN_FILTER to GL_LINEAR or GL_NEAREST?)\n", u->texture);
            u->texture = 0;
        }
        if (u->texture == 0) memset(&u->env, 0, sizeof(u->env));      // Unused, don't split batches over it
        textured = textured || (u->texture != 0);
    }
    key.texMatrixSerial = textured? gl.texMatrixSerial : 0;
    key.texQ = (key.units[0].texture != 0) && gl.texQUsed;
    if (!key.stencilTest)
    {
        key.stencilFunc = key.stencilFail = key.stencilDepthFail = key.stencilPass = 0;
        key.stencilRef = key.stencilFuncMask = key.stencilWriteMask = 0;
    }

    useState(&key);
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

    // Vertex buffer 0 always; texcoords of units 1/2 only when the batch uses them (stale data is never sampled)
    u8 *v = gl.vbo + (size_t)gl.vertexCount*GPU_VERTEX_SIZE;
    memcpy(v, a, GPU_VERTEX_SIZE);
    memcpy(v + GPU_VERTEX_SIZE, b, GPU_VERTEX_SIZE);
    memcpy(v + 2*GPU_VERTEX_SIZE, c, GPU_VERTEX_SIZE);
    if (gl.batch.units[1].texture || gl.batch.units[2].texture)
    {
        memcpy(gl.vboExtra[gl.vertexCount], a->texExtra, GPU_EXTRA_SIZE);
        memcpy(gl.vboExtra[gl.vertexCount + 1], b->texExtra, GPU_EXTRA_SIZE);
        memcpy(gl.vboExtra[gl.vertexCount + 2], c->texExtra, GPU_EXTRA_SIZE);
    }
    gl.vertexCount += 3;
}

static void lerpVertex(Vertex *out, const Vertex *a, const Vertex *b, float t)
{
    for (int i = 0; i < 3; i++) out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i])*t;
    for (int u = 0; u < C3DGL_TEXTURE_UNITS - 1; u++)
        for (int i = 0; i < 3; i++) out->texExtra[u][i] = a->texExtra[u][i] + (b->texExtra[u][i] - a->texExtra[u][i])*t;
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

// Expand a line to a screen-aligned quad in NDC (batch must be in clipSpace mode).
// zBias is added to the NDC depth (polygon offset of polygon outlines)
static void emitLine(const Vertex *a, const Vertex *b, float zBias)
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

    float pa[3] = { ca[0]/ca[3], ca[1]/ca[3], ca[2]/ca[3] + zBias };
    float pb[3] = { cb[0]/cb[3], cb[1]/cb[3], cb[2]/cb[3] + zBias };

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
static void emitPoint(const Vertex *v, float zBias)
{
    float c[4];
    mat4Transform(projectionModelview(), v->pos, c);
    if (c[3] < CLIP_W_MIN) return;

    float p[3] = { c[0]/c[3], c[1]/c[3], c[2]/c[3] + zBias };
    float r = 0.5f*gl.pointSize;
    float rx = 2.0f*r/(float)gl.state.viewport[2], ry = 2.0f*r/(float)gl.state.viewport[3];

    // Zero-length "line" from p to p: the cap offset gives the width, the perpendicular offset the height
    emitExpandedQuad(v, v, p, p, 0.0f, ry, rx, 0.0f);
}

// Copy of v with the color of the provoking vertex pv (flat shading, NULL: smooth) and a depth bias.
// back: take the back color (back-facing polygon with two-sided lighting)
static Vertex shadeVertex(const Vertex *v, const Vertex *pv, float depthBias, bool back)
{
    Vertex out = *v;
    const Vertex *src = (pv != NULL)? pv : v;
    memcpy(out.color, back? src->backColor : src->color, sizeof(out.color));
    out.depthBias = depthBias;
    return out;
}

// Provoking vertex for flat shading, NULL when shading is smooth
#define FLAT(pv) ((gl.shadeModel == GL_FLAT)? (pv) : NULL)

static void emitShadedLine(const Vertex *a, const Vertex *b, const Vertex *pv)
{
    Vertex va = shadeVertex(a, pv, 0.0f, false), vb = shadeVertex(b, pv, 0.0f, false);
    emitLine(&va, &vb, 0.0f);
}

// Twice the signed area of the polygon in clip space (x, y, w), positive when counter-clockwise on screen.
// Homogeneous, so it is also right for vertices behind the viewer
static float polygonArea(const Vertex *const *vs, int n)
{
    const Mat4 *pmv = projectionModelview();
    float c0[4], c1[4], c2[4];
    mat4Transform(pmv, vs[0]->pos, c0);
    mat4Transform(pmv, vs[1]->pos, c1);

    float area = 0.0f;
    for (int i = 2; i < n; i++)
    {
        mat4Transform(pmv, vs[i]->pos, c2);
        area += c0[0]*(c1[1]*c2[3] - c2[1]*c1[3]) - c1[0]*(c0[1]*c2[3] - c2[1]*c0[3]) + c2[0]*(c0[1]*c1[3] - c1[1]*c0[3]);
        memcpy(c1, c2, sizeof(c1));
    }
    return area;
}

#define DEPTH_RESOLUTION    (1.0f/16777216.0f)  // r of glPolygonOffset: one step of the 24-bit depth buffer

// glPolygonOffset in window depth units: factor*m + r*units, m = max depth slope in window space
static float polygonOffset(const Vertex *const *vs, int n)
{
    if (gl.offsetFactor == 0.0f) return gl.offsetUnits*DEPTH_RESOLUTION;

    const Mat4 *pmv = projectionModelview();
    float halfW = 0.5f*(float)gl.state.viewport[2], halfH = 0.5f*(float)gl.state.viewport[3];
    float halfD = 0.5f*(gl.state.depthFar - gl.state.depthNear);

    // Window coordinates of three vertices (the polygon is planar); behind the viewer: no slope
    float w[3][3];
    for (int i = 0; i < 3; i++)
    {
        float c[4];
        mat4Transform(pmv, vs[(i == 0)? 0 : (n - 3 + i)]->pos, c);
        if (c[3] < CLIP_W_MIN) return gl.offsetUnits*DEPTH_RESOLUTION;
        w[i][0] = c[0]/c[3]*halfW;
        w[i][1] = c[1]/c[3]*halfH;
        w[i][2] = c[2]/c[3]*halfD;
    }

    // Depth gradient from the plane normal
    float e1[3] = { w[1][0] - w[0][0], w[1][1] - w[0][1], w[1][2] - w[0][2] };
    float e2[3] = { w[2][0] - w[0][0], w[2][1] - w[0][1], w[2][2] - w[0][2] };
    float nx = e1[1]*e2[2] - e1[2]*e2[1], ny = e1[2]*e2[0] - e1[0]*e2[2], nz = e1[0]*e2[1] - e1[1]*e2[0];
    float m = (fabsf(nz) > 1e-12f)? fmaxf(fabsf(nx/nz), fabsf(ny/nz)) : 0.0f;

    return gl.offsetFactor*m + gl.offsetUnits*DEPTH_RESOLUTION;
}

// A polygon (triangle, quad, polygon) with edge flags: filled, outlined or as vertices depending on
// glPolygonMode of the side that faces the viewer. pv: provoking vertex for flat shading
static void emitPolygon(const Vertex *const *vs, const bool *edges, int n, const Vertex *pv)
{
    GLenum mode = GL_FILL;
    bool polygonModes = (gl.polygonMode[0] != GL_FILL) || (gl.polygonMode[1] != GL_FILL);
    bool twoSided = gl.lightingEnabled && gl.lighting.twoSide;
    bool front = true;
    if (polygonModes || twoSided) front = (polygonArea(vs, n) > 0.0f) == (gl.state.frontFace == GL_CCW);
    bool back = twoSided && !front;
    if (polygonModes)
    {
        // Culling on the CPU: outlines and vertices are drawn in NDC, the GPU cannot cull them
        if (gl.state.cull && ((gl.state.cullFace == GL_FRONT_AND_BACK) || ((gl.state.cullFace == GL_FRONT) == front))) return;

        mode = gl.polygonMode[front? 0 : 1];
        prepareDraw(mode != GL_FILL);
    }

    bool offset = (mode == GL_FILL)? gl.offsetFill : (mode == GL_LINE)? gl.offsetLine : gl.offsetPoint;
    float range = gl.state.depthFar - gl.state.depthNear;
    float windowOffset = (offset && (range != 0.0f))? polygonOffset(vs, n)/range : 0.0f;    // In PICA NDC (= 1/2 GL NDC)

    if (mode == GL_FILL)
    {
        Vertex a = shadeVertex(vs[0], pv, windowOffset, back);
        for (int i = 1; i + 1 < n; i++)
        {
            Vertex b = shadeVertex(vs[i], pv, windowOffset, back), c = shadeVertex(vs[i + 1], pv, windowOffset, back);
            emitTriangle(&a, &b, &c);
        }
    }
    else
    {
        // Edge i runs from vertex i to i + 1; its flag also decides whether vertex i is drawn as a point
        for (int i = 0; i < n; i++)
        {
            if (!edges[i]) continue;
            Vertex a = shadeVertex(vs[i], pv, 0.0f, back);
            if (mode == GL_LINE)
            {
                Vertex b = shadeVertex(vs[(i + 1) % n], pv, 0.0f, back);
                emitLine(&a, &b, 2.0f*windowOffset);
            }
            else emitPoint(&a, 2.0f*windowOffset);
        }
    }
}

static const bool allEdges[4] = { true, true, true, true };

static bool beginPrimitive(GLenum mode)
{
    if (mode > GL_POLYGON)
    {
        WARN_ONCE("Primitive 0x%x not supported\n", mode);
        setError(GL_INVALID_ENUM);
        return false;
    }

    bool lineOrPoint = (mode == GL_POINTS) || (mode == GL_LINES) || (mode == GL_LINE_STRIP) || (mode == GL_LINE_LOOP);
    prepareDraw(lineOrPoint);
    gl.primitive = mode;
    gl.primCount = 0;
    gl.primTotal = 0;
    gl.collectPolygon = (mode == GL_POLYGON) && ((gl.polygonMode[0] != GL_FILL) || (gl.polygonMode[1] != GL_FILL));
    gl.polyCount = 0;
    return true;
}

static void collectPolygonVertex(const Vertex *v, bool edge)
{
    if (gl.polyCount == gl.polyCapacity)
    {
        int capacity = gl.polyCapacity? gl.polyCapacity*2 : 32;
        Vertex *verts = realloc(gl.polyVerts, capacity*sizeof(Vertex));
        if (verts != NULL) gl.polyVerts = verts;
        bool *edges = realloc(gl.polyEdges, capacity*sizeof(bool));
        if (edges != NULL) gl.polyEdges = edges;
        const Vertex **ptrs = realloc(gl.polyPtrs, capacity*sizeof(Vertex *));
        if (ptrs != NULL) gl.polyPtrs = ptrs;
        if ((verts == NULL) || (edges == NULL) || (ptrs == NULL)) { setError(GL_OUT_OF_MEMORY); return; }
        gl.polyCapacity = capacity;
    }
    gl.polyVerts[gl.polyCount] = *v;
    gl.polyEdges[gl.polyCount] = edge;
    gl.polyCount++;
}

// Assemble the primitive from the submitted vertices. prim[] holds what the mode still needs:
//   strips:              the last two vertices (quad strip: up to four)
//   fans, polygons, line loops/strips: the first and the last vertex
// Provoking vertices (flat shading) follow GL: the last vertex of each primitive, the first one for GL_POLYGON.
// Edge flags only apply to separate triangles, quads and polygons
static void submitVertex(const Vertex *v, bool edge)
{
    Vertex *p = gl.prim;
    int n = gl.primTotal++;

    switch (gl.primitive)
    {
        case GL_POINTS:
            emitPoint(v, 0.0f);
            break;
        case GL_LINES:
            p[gl.primCount++] = *v;
            if (gl.primCount == 2) { emitShadedLine(&p[0], &p[1], FLAT(&p[1])); gl.primCount = 0; }
            break;
        case GL_LINE_STRIP:
        case GL_LINE_LOOP:
            if (n == 0) p[0] = *v;
            else emitShadedLine(&p[1], v, FLAT(v));
            p[1] = *v;
            break;
        case GL_TRIANGLES:
            gl.primEdge[gl.primCount] = edge;
            p[gl.primCount++] = *v;
            if (gl.primCount == 3)
            {
                const Vertex *tri[3] = { &p[0], &p[1], &p[2] };
                emitPolygon(tri, gl.primEdge, 3, FLAT(&p[2]));
                gl.primCount = 0;
            }
            break;
        case GL_TRIANGLE_STRIP:
            // Every other triangle is flipped to keep the winding of the first one
            if (n >= 2)
            {
                const Vertex *odd[3] = { &p[1], &p[0], v }, *even[3] = { &p[0], &p[1], v };
                emitPolygon((n & 1)? odd : even, allEdges, 3, FLAT(v));
            }
            p[0] = p[1];
            p[1] = *v;
            break;
        case GL_TRIANGLE_FAN:
            if (n == 0) p[0] = *v;
            else if (n >= 2)
            {
                const Vertex *tri[3] = { &p[0], &p[1], v };
                emitPolygon(tri, allEdges, 3, FLAT(v));
            }
            p[1] = *v;
            break;
        case GL_POLYGON:
            if (gl.collectPolygon) { collectPolygonVertex(v, edge); break; }
            // Filled: a fan, all triangles shaded with the first vertex
            if (n == 0) p[0] = *v;
            else if (n >= 2)
            {
                const Vertex *tri[3] = { &p[0], &p[1], v };
                emitPolygon(tri, allEdges, 3, FLAT(&p[0]));
            }
            p[1] = *v;
            break;
        case GL_QUADS:
            gl.primEdge[gl.primCount] = edge;
            p[gl.primCount++] = *v;
            if (gl.primCount == 4)
            {
                const Vertex *quad[4] = { &p[0], &p[1], &p[2], &p[3] };
                emitPolygon(quad, gl.primEdge, 4, FLAT(&p[3]));
                gl.primCount = 0;
            }
            break;
        case GL_QUAD_STRIP:
            // Quad i is v[2i], v[2i+1], v[2i+3], v[2i+2]
            p[gl.primCount++] = *v;
            if (gl.primCount == 4)
            {
                const Vertex *quad[4] = { &p[0], &p[1], &p[3], &p[2] };
                emitPolygon(quad, allEdges, 4, FLAT(&p[3]));
                p[0] = p[2];
                p[1] = p[3];
                gl.primCount = 2;
            }
            break;
        default: break;
    }
}

// glEnd or the end of a glDraw* call: close line loops, draw collected polygons, drop incomplete primitives
static void endPrimitive(void)
{
    // The closing segment of a loop is shaded with the first vertex
    if ((gl.primitive == GL_LINE_LOOP) && (gl.primTotal >= 2)) emitShadedLine(&gl.prim[1], &gl.prim[0], FLAT(&gl.prim[0]));

    if (gl.collectPolygon && (gl.polyCount >= 3))
    {
        for (int i = 0; i < gl.polyCount; i++) gl.polyPtrs[i] = &gl.polyVerts[i];
        emitPolygon(gl.polyPtrs, gl.polyEdges, gl.polyCount, FLAT(&gl.polyVerts[0]));
    }

    gl.primCount = 0;
    gl.primTotal = 0;
    gl.polyCount = 0;
    gl.collectPolygon = false;
}

//----------------------------------------------------------------------------------
// Lighting (GL 1.1 section 2.13): per vertex on the CPU, when the vertex is submitted
//----------------------------------------------------------------------------------
static u8 colorByte(float c)
{
    if (c <= 0.0f) return 0;
    if (c >= 1.0f) return 255;
    return (u8)(c*255.0f + 0.5f);
}

static void setColor4(float out[4], float r, float g, float b, float a)
{
    out[0] = r; out[1] = g; out[2] = b; out[3] = a;
}

static float dot3(const float a[3], const float b[3]) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static void normalize3(float v[3])
{
    float len = sqrtf(dot3(v, v));
    if (len > 0.0f) for (int i = 0; i < 3; i++) v[i] /= len;
}

// Recompute the derived values after a glLight change
static void updateLight(Light *li)
{
    memcpy(li->unitPosition, li->position, sizeof(li->unitPosition));
    normalize3(li->unitPosition);
    for (int k = 0; k < 3; k++) li->halfVector[k] = li->unitPosition[k] + ((k == 2)? 1.0f : 0.0f);
    normalize3(li->halfVector);
    memcpy(li->spotUnit, li->spotDirection, sizeof(li->spotUnit));
    normalize3(li->spotUnit);
    li->spotCos = cosf(li->spotCutoff*(float)M_PI/180.0f);
}

// GL defaults (GL 1.1 table 6.9)
static void initLighting(void)
{
    LightingState *l = &gl.lighting;
    memset(l, 0, sizeof(*l));
    for (int i = 0; i < C3DGL_MAX_LIGHTS; i++)
    {
        Light *li = &l->lights[i];
        float c = (i == 0)? 1.0f : 0.0f;    // Only light 0 is white
        setColor4(li->ambient, 0.0f, 0.0f, 0.0f, 1.0f);
        setColor4(li->diffuse, c, c, c, 1.0f);
        setColor4(li->specular, c, c, c, 1.0f);
        li->position[2] = 1.0f;             // Directional, along +z
        li->spotDirection[2] = -1.0f;
        li->spotCutoff = 180.0f;
        li->attenuation[0] = 1.0f;
        updateLight(li);
    }
    for (int f = 0; f < 2; f++)
    {
        Material *m = &l->material[f];
        setColor4(m->ambient, 0.2f, 0.2f, 0.2f, 1.0f);
        setColor4(m->diffuse, 0.8f, 0.8f, 0.8f, 1.0f);
        setColor4(m->specular, 0.0f, 0.0f, 0.0f, 1.0f);
        setColor4(m->emission, 0.0f, 0.0f, 0.0f, 1.0f);
        m->colorIndexes[1] = m->colorIndexes[2] = 1.0f;
    }
    setColor4(l->modelAmbient, 0.2f, 0.2f, 0.2f, 1.0f);
    l->colorMaterialFace = GL_FRONT_AND_BACK;
    l->colorMaterialMode = GL_AMBIENT_AND_DIFFUSE;
}

// GL_COLOR_MATERIAL: the tracked material properties take the color (and keep it, like in GL)
static void applyColorMaterial(const u8 color[4])
{
    float c[4];
    for (int i = 0; i < 4; i++) c[i] = color[i]/255.0f;

    GLenum face = gl.lighting.colorMaterialFace, mode = gl.lighting.colorMaterialMode;
    for (int f = 0; f < 2; f++)
    {
        if ((face != GL_FRONT_AND_BACK) && (face != (f? GL_BACK : GL_FRONT))) continue;
        Material *m = &gl.lighting.material[f];
        if ((mode == GL_AMBIENT) || (mode == GL_AMBIENT_AND_DIFFUSE)) memcpy(m->ambient, c, sizeof(c));
        if ((mode == GL_DIFFUSE) || (mode == GL_AMBIENT_AND_DIFFUSE)) memcpy(m->diffuse, c, sizeof(c));
        if (mode == GL_SPECULAR) memcpy(m->specular, c, sizeof(c));
        if (mode == GL_EMISSION) memcpy(m->emission, c, sizeof(c));
    }
}

// Normals go to eye space with the inverse transpose of the modelview's upper 3x3 (n' = n M^-1), which is the
// cofactor matrix divided by the determinant. Also the GL_RESCALE_NORMAL factor: 1/length of M^-1's third row
static void updateNormalMatrix(void)
{
    if (gl.normalSerial == gl.matrixSerial) return;
    gl.normalSerial = gl.matrixSerial;

    const float *m = gl.stack[0][gl.stackDepth[0]].m;
    #define M(r, c) m[(c)*4 + (r)]
    float cof[9] = {
        M(1,1)*M(2,2) - M(1,2)*M(2,1), M(1,2)*M(2,0) - M(1,0)*M(2,2), M(1,0)*M(2,1) - M(1,1)*M(2,0),
        M(0,2)*M(2,1) - M(0,1)*M(2,2), M(0,0)*M(2,2) - M(0,2)*M(2,0), M(0,1)*M(2,0) - M(0,0)*M(2,1),
        M(0,1)*M(1,2) - M(0,2)*M(1,1), M(0,2)*M(1,0) - M(0,0)*M(1,2), M(0,0)*M(1,1) - M(0,1)*M(1,0),
    };
    float det = M(0,0)*cof[0] + M(0,1)*cof[1] + M(0,2)*cof[2];
    #undef M

    float inv = (det != 0.0f)? 1.0f/det : 0.0f;     // Singular: normals collapse to 0 (ambient and emission only)
    for (int i = 0; i < 9; i++) gl.normalMatrix[i] = cof[i]*inv;
    const float *n = gl.normalMatrix;
    float len = sqrtf(n[2]*n[2] + n[5]*n[5] + n[8]*n[8]);
    gl.normalRescale = (len > 0.0f)? 1.0f/len : 1.0f;
}

// Lit color of one side: eye = vertex in eye space, n = eye space normal of that side
static void shadeFace(const Material *m, const float eye[3], const float n[3], u8 out[4])
{
    const LightingState *l = &gl.lighting;
    float c[3];
    for (int k = 0; k < 3; k++) c[k] = m->emission[k] + m->ambient[k]*l->modelAmbient[k];

    for (int i = 0; i < C3DGL_MAX_LIGHTS; i++)
    {
        if (!(gl.lightEnabled & (1u << i))) continue;
        const Light *li = &l->lights[i];

        // vp: unit vector from the vertex to the light; distance attenuation for positional lights only
        float vp[3], att = 1.0f;
        if (li->position[3] != 0.0f)
        {
            for (int k = 0; k < 3; k++) vp[k] = li->position[k]/li->position[3] - eye[k];
            float d2 = dot3(vp, vp), d = sqrtf(d2);
            if (d > 0.0f) for (int k = 0; k < 3; k++) vp[k] /= d;
            float denom = li->attenuation[0] + li->attenuation[1]*d + li->attenuation[2]*d2;
            if (denom > 0.0f) att = 1.0f/denom;
        }
        else memcpy(vp, li->unitPosition, sizeof(vp));

        if (li->spotCutoff != 180.0f)
        {
            // Outside the cone the light contributes nothing, not even ambient
            float cosAngle = -dot3(vp, li->spotUnit);
            if (cosAngle < li->spotCos) continue;
            att *= powf(cosAngle, li->spotExponent);
        }

        float r[3];
        for (int k = 0; k < 3; k++) r[k] = m->ambient[k]*li->ambient[k];

        float ndotl = dot3(n, vp);
        if (ndotl > 0.0f)
        {
            for (int k = 0; k < 3; k++) r[k] += ndotl*m->diffuse[k]*li->diffuse[k];

            float spec[3];
            for (int k = 0; k < 3; k++) spec[k] = m->specular[k]*li->specular[k];
            if ((spec[0] != 0.0f) || (spec[1] != 0.0f) || (spec[2] != 0.0f))
            {
                // Half vector between vp and the direction to the eye ((0, 0, 1) without local viewer)
                float h[3], toEye[3] = { 0.0f, 0.0f, 1.0f };
                if (!l->localViewer && (li->position[3] == 0.0f)) memcpy(h, li->halfVector, sizeof(h));
                else
                {
                    if (l->localViewer)
                    {
                        for (int k = 0; k < 3; k++) toEye[k] = -eye[k];
                        normalize3(toEye);
                    }
                    for (int k = 0; k < 3; k++) h[k] = vp[k] + toEye[k];
                    normalize3(h);
                }
                float ndoth = dot3(n, h);
                if (ndoth > 0.0f)
                {
                    float f = powf(ndoth, m->shininess);
                    for (int k = 0; k < 3; k++) r[k] += f*spec[k];
                }
            }
        }

        for (int k = 0; k < 3; k++) c[k] += att*r[k];
    }

    for (int k = 0; k < 3; k++) out[k] = colorByte(c[k]);
    out[3] = colorByte(m->diffuse[3]);
}

// Replace v's color by the lit color, also the back color with two-sided lighting. normal: in object space
static void lightVertex(Vertex *v, const float normal[3])
{
    if (gl.colorMaterial) applyColorMaterial(v->color);
    updateNormalMatrix();

    // The eye space position is only needed for positional lights and the local viewer
    bool needEye = gl.lighting.localViewer;
    for (int i = 0; i < C3DGL_MAX_LIGHTS; i++)
        if ((gl.lightEnabled & (1u << i)) && (gl.lighting.lights[i].position[3] != 0.0f)) needEye = true;
    float eye[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (needEye)
    {
        mat4Transform(&gl.stack[0][gl.stackDepth[0]], v->pos, eye);
        if ((eye[3] != 1.0f) && (eye[3] != 0.0f)) for (int k = 0; k < 3; k++) eye[k] /= eye[3];
    }

    // Not normalized unless asked for, like GL (a scaling modelview changes the brightness)
    const float *nm = gl.normalMatrix;
    float n[3];
    for (int k = 0; k < 3; k++) n[k] = nm[k*3]*normal[0] + nm[k*3 + 1]*normal[1] + nm[k*3 + 2]*normal[2];
    if (gl.normalize) normalize3(n);
    else if (gl.rescaleNormal) for (int k = 0; k < 3; k++) n[k] *= gl.normalRescale;

    shadeFace(&gl.lighting.material[0], eye, n, v->color);
    if (gl.lighting.twoSide)
    {
        float back[3] = { -n[0], -n[1], -n[2] };
        shadeFace(&gl.lighting.material[1], eye, back, v->backColor);
    }
}

// Every vertex goes through here: lighting, then primitive assembly
static void submitLitVertex(Vertex *v, const float normal[3], bool edge)
{
    if (gl.lightingEnabled) lightVertex(v, normal);
    submitVertex(v, edge);
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

    if (!C3D_TexInit(&gl.dummyTexture, 8, 8, GPU_L8)) { LOG("Failed to allocate texture\n"); c3dglClose(); return false; }
    memset(gl.dummyTexture.data, 0, gl.dummyTexture.size);
    C3D_TexFlush(&gl.dummyTexture);

    gl.vbo = linearAlloc(C3DGL_MAX_VERTICES*GPU_VERTEX_SIZE);
    gl.vboExtra = linearAlloc(C3DGL_MAX_VERTICES*GPU_EXTRA_SIZE);
    if ((gl.vbo == NULL) || (gl.vboExtra == NULL)) { LOG("Failed to allocate vertex buffer\n"); c3dglClose(); return false; }

    gl.dvlb = DVLB_ParseFile((u32 *)c3dgl_vsh_shbin, c3dgl_vsh_shbin_size);
    shaderProgramInit(&gl.program);
    shaderProgramSetVsh(&gl.program, &gl.dvlb->DVLE[0]);
    C3D_BindProgram(&gl.program);
    gl.uLocMvp = shaderInstanceGetUniformLocation(gl.program.vertexShader, "mvp");
    gl.uLocTexMat[0] = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texmat0");
    gl.uLocTexMat[1] = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texmat1");
    gl.uLocTexMat[2] = shaderInstanceGetUniformLocation(gl.program.vertexShader, "texmat2");

    // Vertex layout: v0 = position (3 floats), v1 = texcoord s, t, q (3 floats), v2 = color (4 ubytes), v3 = depth bias (float)
    C3D_AttrInfo *attrInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attrInfo);
    AttrInfo_AddLoader(attrInfo, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrInfo, 1, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attrInfo, 2, GPU_UNSIGNED_BYTE, 4);
    AttrInfo_AddLoader(attrInfo, 3, GPU_FLOAT, 1);
    AttrInfo_AddLoader(attrInfo, 4, GPU_FLOAT, 3);     // Buffer 1: texcoords of units 1 and 2
    AttrInfo_AddLoader(attrInfo, 5, GPU_FLOAT, 3);

    C3D_BufInfo *bufInfo = C3D_GetBufInfo();
    BufInfo_Init(bufInfo);
    BufInfo_Add(bufInfo, gl.vbo, GPU_VERTEX_SIZE, 4, 0x3210);
    BufInfo_Add(bufInfo, gl.vboExtra, GPU_EXTRA_SIZE, 2, 0x54);

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
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        // OpenGL defaults (GL 1.3 / ES 1.1 table 6.18)
        TexEnvState *e = &gl.state.units[unit].env;
        e->mode = GL_MODULATE;
        e->combineRgb = e->combineAlpha = GL_MODULATE;
        e->srcRgb[0] = e->srcAlpha[0] = GL_TEXTURE;
        e->srcRgb[1] = e->srcAlpha[1] = GL_PREVIOUS;
        e->srcRgb[2] = e->srcAlpha[2] = GL_CONSTANT;
        e->operandRgb[0] = e->operandRgb[1] = GL_SRC_COLOR;
        e->operandRgb[2] = GL_SRC_ALPHA;
        e->operandAlpha[0] = e->operandAlpha[1] = e->operandAlpha[2] = GL_SRC_ALPHA;
        e->rgbScale = e->alphaScale = 1;
    }
    for (int unit = 1; unit < C3DGL_TEXTURE_UNITS; unit++) gl.current.texExtra[unit - 1][2] = 1.0f;    // q
    gl.state.stencilFunc = GL_ALWAYS;
    gl.state.stencilFuncMask = gl.state.stencilWriteMask = 0xFF;
    gl.state.stencilFail = gl.state.stencilDepthFail = gl.state.stencilPass = GL_KEEP;
    gl.state.colorMask = GPU_WRITE_COLOR;
    gl.state.cullFace = GL_BACK;
    gl.state.frontFace = GL_CCW;
    gl.unpack.alignment = gl.pack.alignment = 4;
    gl.lineWidth = gl.pointSize = 1.0f;
    gl.clearColor = 0x000000FF;
    gl.clearDepth = 1.0f;
    gl.state.depthFar = 1.0f;
    gl.polygonMode[0] = gl.polygonMode[1] = GL_FILL;
    gl.currentEdge = true;
    gl.shadeModel = GL_SMOOTH;
    gl.currentNormal[2] = 1.0f;
    initLighting();
    gl.ignoredCaps = 1u << 0;     // GL_DITHER is enabled by default
    memset(gl.current.color, 255, 4);
    gl.current.tex[2] = 1.0f;

    // Client array defaults: size 4 (3 for normals), GL_FLOAT
    for (int i = 0; i < ARRAY_COUNT; i++)
    {
        gl.arrays[i].size = (i == ARRAY_NORMAL)? 3 : (i == ARRAY_EDGEFLAG)? 1 : 4;
        gl.arrays[i].type = (i == ARRAY_EDGEFLAG)? GL_UNSIGNED_BYTE : GL_FLOAT;
    }

    for (int i = 0; i < 2 + C3DGL_TEXTURE_UNITS; i++) mat4Identity(&gl.stack[i][0]);

    // Evaluator defaults: grids of 1 segment over [0, 1]
    gl.grid1n = gl.grid2un = gl.grid2vn = 1;
    gl.grid1u2 = gl.grid2u2 = gl.grid2v2 = 1.0f;
    gl.matrixSerial = gl.texMatrixSerial = 1;

    gl.ready = true;
    return true;
}

void c3dglClose(void)
{
    if (gl.frameActive) { C3D_FrameEnd(0); gl.frameActive = false; }

    for (int i = 1; i < C3DGL_MAX_TEXTURES; i++) if (gl.textures[i].loaded) C3D_TexDelete(&gl.textures[i].tex);
    processDeferredDeletes();
    free(gl.deferredDeletes);
    free(gl.polyVerts);
    free(gl.polyEdges);
    free(gl.polyPtrs);
    for (GLuint i = 0; i < gl.bufferCount; i++) free(gl.buffers[i].data);
    free(gl.buffers);
    for (int i = 0; i < 9; i++) { free(gl.map1[i].points); free(gl.map2[i].points); }

    if (gl.dvlb != NULL) { shaderProgramFree(&gl.program); DVLB_Free(gl.dvlb); }
    if (gl.dummyTexture.data != NULL) C3D_TexDelete(&gl.dummyTexture);
    if (gl.vbo != NULL) linearFree(gl.vbo);
    if (gl.vboExtra != NULL) linearFree(gl.vboExtra);
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
// Capabilities that programs commonly toggle but c3dgl does not implement: stored for glIsEnabled,
// enabling the ones that change the picture warns once
static const GLenum ignoredCaps[] = {
    GL_DITHER, GL_LINE_SMOOTH, GL_POINT_SMOOTH, GL_POLYGON_SMOOTH, GL_FOG,
};

static int ignoredCapBit(GLenum cap)
{
    for (int i = 0; i < (int)(sizeof(ignoredCaps)/sizeof(ignoredCaps[0])); i++) if (ignoredCaps[i] == cap) return i;
    return -1;
}

static void setCapability(GLenum cap, bool enable)
{
    int bit = ignoredCapBit(cap);
    if (bit >= 0)
    {
        if (enable) gl.ignoredCaps |= 1u << bit;
        else gl.ignoredCaps &= ~(1u << bit);

        bool cosmetic = (cap == GL_DITHER) || (cap == GL_LINE_SMOOTH) || (cap == GL_POINT_SMOOTH) || (cap == GL_POLYGON_SMOOTH);
        if (enable && !cosmetic) WARN_ONCE("glEnable: capability 0x%x not supported, ignored\n", cap);
        return;
    }

    switch (cap)
    {
        case GL_TEXTURE_2D: gl.texture2D[gl.activeTexture] = enable; break;
        case GL_BLEND: gl.state.blend = enable; break;
        case GL_DEPTH_TEST: gl.state.depthTest = enable; break;
        case GL_ALPHA_TEST: gl.state.alphaTest = enable; break;
        case GL_STENCIL_TEST:
            gl.state.stencilTest = enable;
            if (enable) gl.stencilUsed = true;
            break;
        case GL_CULL_FACE: gl.state.cull = enable; break;
        case GL_SCISSOR_TEST: gl.state.scissor = enable; break;
        case GL_LIGHTING: gl.lightingEnabled = enable; break;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            if (enable) gl.lightEnabled |= 1u << (cap - GL_LIGHT0);
            else gl.lightEnabled &= ~(1u << (cap - GL_LIGHT0));
            break;
        case GL_COLOR_MATERIAL:
            gl.colorMaterial = enable;
            if (enable) applyColorMaterial(gl.current.color);     // The material follows the current color from now on
            break;
        case GL_NORMALIZE: gl.normalize = enable; break;
        case GL_RESCALE_NORMAL: gl.rescaleNormal = enable; break;
        case GL_AUTO_NORMAL: gl.autoNormal = enable; break;
        case GL_MAP1_COLOR_4: case GL_MAP1_INDEX: case GL_MAP1_NORMAL: case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP1_TEXTURE_COORD_2: case GL_MAP1_TEXTURE_COORD_3: case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP1_VERTEX_3: case GL_MAP1_VERTEX_4:
            gl.map1[cap - GL_MAP1_COLOR_4].enabled = enable;
            break;
        case GL_MAP2_COLOR_4: case GL_MAP2_INDEX: case GL_MAP2_NORMAL: case GL_MAP2_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_2: case GL_MAP2_TEXTURE_COORD_3: case GL_MAP2_TEXTURE_COORD_4:
        case GL_MAP2_VERTEX_3: case GL_MAP2_VERTEX_4:
            gl.map2[cap - GL_MAP2_COLOR_4].enabled = enable;
            break;
        case GL_POLYGON_OFFSET_FILL: gl.offsetFill = enable; break;
        case GL_POLYGON_OFFSET_LINE: gl.offsetLine = enable; break;
        case GL_POLYGON_OFFSET_POINT: gl.offsetPoint = enable; break;
        default:
            WARN_ONCE("glEnable/glDisable: capability 0x%x not supported\n", cap);
            setError(GL_INVALID_ENUM);
            break;
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
        case GL_EDGE_FLAG_ARRAY: gl.arrays[ARRAY_EDGEFLAG].enabled = enable; break;
        case GL_NORMAL_ARRAY: gl.arrays[ARRAY_NORMAL].enabled = enable; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glEnableClientState(GLenum array) { setClientState(array, true); }
void glDisableClientState(GLenum array) { setClientState(array, false); }

GLboolean glIsEnabled(GLenum cap)
{
    int bit = ignoredCapBit(cap);
    if (bit >= 0) return (gl.ignoredCaps >> bit) & 1;

    switch (cap)
    {
        case GL_TEXTURE_2D: return gl.texture2D[gl.activeTexture];
        case GL_BLEND: return gl.state.blend;
        case GL_DEPTH_TEST: return gl.state.depthTest;
        case GL_ALPHA_TEST: return gl.state.alphaTest;
        case GL_STENCIL_TEST: return gl.state.stencilTest;
        case GL_CULL_FACE: return gl.state.cull;
        case GL_SCISSOR_TEST: return gl.state.scissor;
        case GL_LIGHTING: return gl.lightingEnabled;
        case GL_LIGHT0: case GL_LIGHT1: case GL_LIGHT2: case GL_LIGHT3:
        case GL_LIGHT4: case GL_LIGHT5: case GL_LIGHT6: case GL_LIGHT7:
            return (gl.lightEnabled >> (cap - GL_LIGHT0)) & 1;
        case GL_COLOR_MATERIAL: return gl.colorMaterial;
        case GL_NORMALIZE: return gl.normalize;
        case GL_RESCALE_NORMAL: return gl.rescaleNormal;
        case GL_AUTO_NORMAL: return gl.autoNormal;
        case GL_MAP1_COLOR_4: case GL_MAP1_INDEX: case GL_MAP1_NORMAL: case GL_MAP1_TEXTURE_COORD_1:
        case GL_MAP1_TEXTURE_COORD_2: case GL_MAP1_TEXTURE_COORD_3: case GL_MAP1_TEXTURE_COORD_4:
        case GL_MAP1_VERTEX_3: case GL_MAP1_VERTEX_4:
            return gl.map1[cap - GL_MAP1_COLOR_4].enabled;
        case GL_MAP2_COLOR_4: case GL_MAP2_INDEX: case GL_MAP2_NORMAL: case GL_MAP2_TEXTURE_COORD_1:
        case GL_MAP2_TEXTURE_COORD_2: case GL_MAP2_TEXTURE_COORD_3: case GL_MAP2_TEXTURE_COORD_4:
        case GL_MAP2_VERTEX_3: case GL_MAP2_VERTEX_4:
            return gl.map2[cap - GL_MAP2_COLOR_4].enabled;
        case GL_VERTEX_ARRAY: return gl.arrays[ARRAY_VERTEX].enabled;
        case GL_TEXTURE_COORD_ARRAY: return gl.arrays[ARRAY_TEXCOORD].enabled;
        case GL_COLOR_ARRAY: return gl.arrays[ARRAY_COLOR].enabled;
        case GL_EDGE_FLAG_ARRAY: return gl.arrays[ARRAY_EDGEFLAG].enabled;
        case GL_NORMAL_ARRAY: return gl.arrays[ARRAY_NORMAL].enabled;
        case GL_POLYGON_OFFSET_FILL: return gl.offsetFill;
        case GL_POLYGON_OFFSET_LINE: return gl.offsetLine;
        case GL_POLYGON_OFFSET_POINT: return gl.offsetPoint;
        default: setError(GL_INVALID_ENUM); return GL_FALSE;
    }
}

GLenum glGetError(void)
{
    GLenum error = gl.error;
    gl.error = GL_NO_ERROR;
    return error;
}

// Draws are submitted at c3dglSwapBuffers(); flushing the batch is all that can be done earlier
void glFlush(void) { if (gl.frameActive) flush(); }
void glFinish(void) { glFlush(); }

void glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }

void glShadeModel(GLenum mode)
{
    if ((mode != GL_SMOOTH) && (mode != GL_FLAT)) { setError(GL_INVALID_ENUM); return; }
    gl.shadeModel = mode;
}

void glPixelStorei(GLenum pname, GLint param)
{
    bool unpack = (pname >= GL_UNPACK_SWAP_BYTES) && (pname <= GL_UNPACK_ALIGNMENT);
    if ((pname == GL_UNPACK_IMAGE_HEIGHT) || (pname == GL_UNPACK_SKIP_IMAGES)) unpack = true;
    PixelStore *ps = unpack? &gl.unpack : &gl.pack;

    switch (pname)
    {
        case GL_UNPACK_ALIGNMENT: case GL_PACK_ALIGNMENT:
            if ((param != 1) && (param != 2) && (param != 4) && (param != 8)) { setError(GL_INVALID_VALUE); return; }
            ps->alignment = param;
            return;
        case GL_UNPACK_SWAP_BYTES: case GL_PACK_SWAP_BYTES: ps->swapBytes = (param != 0); return;
        case GL_UNPACK_LSB_FIRST: case GL_PACK_LSB_FIRST: ps->lsbFirst = (param != 0); return;     // Only for GL_BITMAP
        default: break;
    }

    if (param < 0) { setError(GL_INVALID_VALUE); return; }
    switch (pname)
    {
        case GL_UNPACK_ROW_LENGTH: case GL_PACK_ROW_LENGTH: ps->rowLength = param; break;
        case GL_UNPACK_SKIP_ROWS: case GL_PACK_SKIP_ROWS: ps->skipRows = param; break;
        case GL_UNPACK_SKIP_PIXELS: case GL_PACK_SKIP_PIXELS: ps->skipPixels = param; break;
        case GL_UNPACK_IMAGE_HEIGHT: case GL_PACK_IMAGE_HEIGHT: ps->imageHeight = param; break;     // GL 1.2, no 3D textures
        case GL_UNPACK_SKIP_IMAGES: case GL_PACK_SKIP_IMAGES: ps->skipImages = param; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPixelStoref(GLenum pname, GLfloat param) { glPixelStorei(pname, (GLint)lroundf(param)); }

// State for glGet*: fills v and returns the number of values (0: unknown pname).
// *normalized: the values are colors/depths in [0, 1], which integer queries scale to [0, INT_MAX]
static int getState(GLenum pname, double v[16], bool *normalized)
{
    *normalized = false;

    switch (pname)
    {
        case GL_MODELVIEW_MATRIX:
        case GL_PROJECTION_MATRIX:
        case GL_TEXTURE_MATRIX:
        {
            int mode = pname - GL_MODELVIEW_MATRIX;
            if (mode == 2) mode += gl.activeTexture;
            for (int i = 0; i < 16; i++) v[i] = gl.stack[mode][gl.stackDepth[mode]].m[i];
            return 16;
        }
        case GL_MODELVIEW_STACK_DEPTH: v[0] = gl.stackDepth[0] + 1; return 1;
        case GL_PROJECTION_STACK_DEPTH: v[0] = gl.stackDepth[1] + 1; return 1;
        case GL_TEXTURE_STACK_DEPTH: v[0] = gl.stackDepth[2 + gl.activeTexture] + 1; return 1;
        case GL_MAX_MODELVIEW_STACK_DEPTH:
        case GL_MAX_PROJECTION_STACK_DEPTH:
        case GL_MAX_TEXTURE_STACK_DEPTH: v[0] = C3DGL_MATRIX_STACK; return 1;
        case GL_MATRIX_MODE: v[0] = GL_MODELVIEW + gl.matrixMode; return 1;

        case GL_VIEWPORT: for (int i = 0; i < 4; i++) v[i] = gl.state.viewport[i]; return 4;
        case GL_SCISSOR_BOX: for (int i = 0; i < 4; i++) v[i] = gl.state.scissorBox[i]; return 4;
        case GL_MAX_VIEWPORT_DIMS: v[0] = C3DGL_TOP_SCREEN_WIDTH; v[1] = C3DGL_SCREEN_HEIGHT; return 2;
        case GL_DEPTH_RANGE: v[0] = gl.state.depthNear; v[1] = gl.state.depthFar; *normalized = true; return 2;
        case GL_POLYGON_MODE: v[0] = gl.polygonMode[0]; v[1] = gl.polygonMode[1]; return 2;
        case GL_POLYGON_OFFSET_FACTOR: v[0] = gl.offsetFactor; return 1;
        case GL_POLYGON_OFFSET_UNITS: v[0] = gl.offsetUnits; return 1;
        case GL_EDGE_FLAG: v[0] = gl.currentEdge; return 1;

        case GL_CURRENT_COLOR: for (int i = 0; i < 4; i++) v[i] = gl.current.color[i]/255.0; *normalized = true; return 4;
        case GL_CURRENT_TEXTURE_COORDS:
        {
            const float *tc = (gl.activeTexture == 0)? gl.current.tex : gl.current.texExtra[gl.activeTexture - 1];
            v[0] = tc[0]; v[1] = tc[1]; v[2] = gl.currentTexR[gl.activeTexture]; v[3] = tc[2];
            return 4;
        }
        case GL_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + gl.activeTexture; return 1;
        case GL_CLIENT_ACTIVE_TEXTURE: v[0] = GL_TEXTURE0 + gl.clientActiveTexture; return 1;
        case GL_MAX_TEXTURE_UNITS: v[0] = C3DGL_TEXTURE_UNITS; return 1;
        case GL_ATTRIB_STACK_DEPTH: v[0] = gl.attribDepth; return 1;
        case GL_MAX_EVAL_ORDER: v[0] = C3DGL_MAX_EVAL_ORDER; return 1;
        case GL_MAP1_GRID_DOMAIN: v[0] = gl.grid1u1; v[1] = gl.grid1u2; return 2;
        case GL_MAP1_GRID_SEGMENTS: v[0] = gl.grid1n; return 1;
        case GL_MAP2_GRID_DOMAIN: v[0] = gl.grid2u1; v[1] = gl.grid2u2; v[2] = gl.grid2v1; v[3] = gl.grid2v2; return 4;
        case GL_MAP2_GRID_SEGMENTS: v[0] = gl.grid2un; v[1] = gl.grid2vn; return 2;
        case GL_CLIENT_ATTRIB_STACK_DEPTH: v[0] = gl.clientAttribDepth; return 1;
        case GL_MAX_ATTRIB_STACK_DEPTH: case GL_MAX_CLIENT_ATTRIB_STACK_DEPTH: v[0] = C3DGL_ATTRIB_STACK; return 1;

        // Client arrays
        case GL_VERTEX_ARRAY_SIZE: v[0] = gl.arrays[ARRAY_VERTEX].size; return 1;
        case GL_VERTEX_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_VERTEX].type; return 1;
        case GL_VERTEX_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_VERTEX].stride; return 1;
        case GL_NORMAL_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_NORMAL].type; return 1;
        case GL_NORMAL_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_NORMAL].stride; return 1;
        case GL_COLOR_ARRAY_SIZE: v[0] = gl.arrays[ARRAY_COLOR].size; return 1;
        case GL_COLOR_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_COLOR].type; return 1;
        case GL_COLOR_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_COLOR].stride; return 1;
        case GL_TEXTURE_COORD_ARRAY_SIZE: v[0] = gl.arrays[ARRAY_TEXCOORD].size; return 1;
        case GL_TEXTURE_COORD_ARRAY_TYPE: v[0] = gl.arrays[ARRAY_TEXCOORD].type; return 1;
        case GL_TEXTURE_COORD_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_TEXCOORD].stride; return 1;
        case GL_EDGE_FLAG_ARRAY_STRIDE: v[0] = gl.arrays[ARRAY_EDGEFLAG].stride; return 1;
        case GL_ARRAY_BUFFER_BINDING: v[0] = gl.arrayBuffer; return 1;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0] = gl.elementArrayBuffer; return 1;
        case GL_VERTEX_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_VERTEX].buffer; return 1;
        case GL_NORMAL_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_NORMAL].buffer; return 1;
        case GL_COLOR_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_COLOR].buffer; return 1;
        case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_TEXCOORD].buffer; return 1;
        case GL_EDGE_FLAG_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_EDGEFLAG].buffer; return 1;
        case GL_CURRENT_NORMAL: for (int i = 0; i < 3; i++) v[i] = gl.currentNormal[i]; return 3;

        case GL_COLOR_CLEAR_VALUE:
            for (int i = 0; i < 4; i++) v[i] = ((gl.clearColor >> (24 - 8*i)) & 0xFF)/255.0;
            *normalized = true;
            return 4;
        case GL_DEPTH_CLEAR_VALUE: v[0] = gl.clearDepth; *normalized = true; return 1;
        case GL_STENCIL_CLEAR_VALUE: v[0] = gl.clearStencil; return 1;

        case GL_COLOR_WRITEMASK:
            v[0] = (gl.state.colorMask & GPU_WRITE_RED) != 0;
            v[1] = (gl.state.colorMask & GPU_WRITE_GREEN) != 0;
            v[2] = (gl.state.colorMask & GPU_WRITE_BLUE) != 0;
            v[3] = (gl.state.colorMask & GPU_WRITE_ALPHA) != 0;
            return 4;
        case GL_DEPTH_WRITEMASK: v[0] = gl.state.depthMask; return 1;
        case GL_DEPTH_FUNC: v[0] = gl.state.depthFunc; return 1;
        case GL_BLEND_SRC: v[0] = gl.state.blendSrc; return 1;
        case GL_BLEND_DST: v[0] = gl.state.blendDst; return 1;
        case GL_ALPHA_TEST_FUNC: v[0] = gl.state.alphaFunc; return 1;
        case GL_ALPHA_TEST_REF: v[0] = gl.state.alphaRef/255.0; *normalized = true; return 1;
        case GL_STENCIL_FUNC: v[0] = gl.state.stencilFunc; return 1;
        case GL_STENCIL_REF: v[0] = gl.state.stencilRef; return 1;
        case GL_STENCIL_VALUE_MASK: v[0] = gl.state.stencilFuncMask; return 1;
        case GL_STENCIL_WRITEMASK: v[0] = gl.state.stencilWriteMask; return 1;
        case GL_STENCIL_FAIL: v[0] = gl.state.stencilFail; return 1;
        case GL_STENCIL_PASS_DEPTH_FAIL: v[0] = gl.state.stencilDepthFail; return 1;
        case GL_STENCIL_PASS_DEPTH_PASS: v[0] = gl.state.stencilPass; return 1;
        case GL_CULL_FACE_MODE: v[0] = gl.state.cullFace; return 1;
        case GL_FRONT_FACE: v[0] = gl.state.frontFace; return 1;
        case GL_SHADE_MODEL: v[0] = gl.shadeModel; return 1;
        case GL_MAX_LIGHTS: v[0] = C3DGL_MAX_LIGHTS; return 1;
        case GL_LIGHT_MODEL_AMBIENT: for (int i = 0; i < 4; i++) v[i] = gl.lighting.modelAmbient[i]; *normalized = true; return 4;
        case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = gl.lighting.localViewer; return 1;
        case GL_LIGHT_MODEL_TWO_SIDE: v[0] = gl.lighting.twoSide; return 1;
        case GL_COLOR_MATERIAL_FACE: v[0] = gl.lighting.colorMaterialFace; return 1;
        case GL_COLOR_MATERIAL_PARAMETER: v[0] = gl.lighting.colorMaterialMode; return 1;

        case GL_LINE_WIDTH: v[0] = gl.lineWidth; return 1;
        case GL_POINT_SIZE: v[0] = gl.pointSize; return 1;
        case GL_UNPACK_ALIGNMENT: v[0] = gl.unpack.alignment; return 1;
        case GL_UNPACK_ROW_LENGTH: v[0] = gl.unpack.rowLength; return 1;
        case GL_UNPACK_SKIP_ROWS: v[0] = gl.unpack.skipRows; return 1;
        case GL_UNPACK_SKIP_PIXELS: v[0] = gl.unpack.skipPixels; return 1;
        case GL_UNPACK_SWAP_BYTES: v[0] = gl.unpack.swapBytes; return 1;
        case GL_UNPACK_LSB_FIRST: v[0] = gl.unpack.lsbFirst; return 1;
        case GL_UNPACK_IMAGE_HEIGHT: v[0] = gl.unpack.imageHeight; return 1;
        case GL_UNPACK_SKIP_IMAGES: v[0] = gl.unpack.skipImages; return 1;
        case GL_PACK_ALIGNMENT: v[0] = gl.pack.alignment; return 1;
        case GL_PACK_ROW_LENGTH: v[0] = gl.pack.rowLength; return 1;
        case GL_PACK_SKIP_ROWS: v[0] = gl.pack.skipRows; return 1;
        case GL_PACK_SKIP_PIXELS: v[0] = gl.pack.skipPixels; return 1;
        case GL_PACK_SWAP_BYTES: v[0] = gl.pack.swapBytes; return 1;
        case GL_PACK_LSB_FIRST: v[0] = gl.pack.lsbFirst; return 1;
        case GL_PACK_IMAGE_HEIGHT: v[0] = gl.pack.imageHeight; return 1;
        case GL_PACK_SKIP_IMAGES: v[0] = gl.pack.skipImages; return 1;
        case GL_TEXTURE_BINDING_2D: v[0] = gl.boundTexture[gl.activeTexture]; return 1;
        case GL_MAX_TEXTURE_SIZE: v[0] = C3DGL_MAX_TEXTURE_SIZE; return 1;

        // Render target: RGBA8 color, D24S8 depth/stencil
        case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
        case GL_DEPTH_BITS: v[0] = 24; return 1;
        case GL_STENCIL_BITS: v[0] = 8; return 1;

        default:
            // Capabilities can be queried with glGet too
            if ((ignoredCapBit(pname) >= 0) || (pname == GL_TEXTURE_2D) || (pname == GL_BLEND) || (pname == GL_DEPTH_TEST) ||
                (pname == GL_ALPHA_TEST) || (pname == GL_STENCIL_TEST) || (pname == GL_CULL_FACE) || (pname == GL_SCISSOR_TEST) ||
                (pname == GL_VERTEX_ARRAY) || (pname == GL_TEXTURE_COORD_ARRAY) || (pname == GL_COLOR_ARRAY) || (pname == GL_NORMAL_ARRAY) ||
                (pname == GL_EDGE_FLAG_ARRAY) || (pname == GL_POLYGON_OFFSET_FILL) || (pname == GL_POLYGON_OFFSET_LINE) ||
                (pname == GL_POLYGON_OFFSET_POINT) || (pname == GL_AUTO_NORMAL) || (pname == GL_LIGHTING) ||
                ((pname >= GL_LIGHT0) && (pname <= GL_LIGHT7)) || (pname == GL_COLOR_MATERIAL) ||
                (pname == GL_NORMALIZE) || (pname == GL_RESCALE_NORMAL) ||
                ((pname >= GL_MAP1_COLOR_4) && (pname <= GL_MAP1_VERTEX_4)) || ((pname >= GL_MAP2_COLOR_4) && (pname <= GL_MAP2_VERTEX_4)))
            {
                v[0] = glIsEnabled(pname);
                return 1;
            }
            WARN_ONCE("glGet: 0x%x not supported\n", pname);
            setError(GL_INVALID_ENUM);
            return 0;
    }
}

void glGetDoublev(GLenum pname, GLdouble *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetFloatv(GLenum pname, GLfloat *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = (GLfloat)v[i];
}

// Integer query of a color/depth: [-1, 1] -> [-INT_MAX, INT_MAX], clamped (light colors may exceed 1)
static GLint normalizedToInt(double v)
{
    if (v >= 1.0) return 2147483647;
    if (v <= -1.0) return -2147483647;
    return (GLint)(v*2147483647.0);
}

void glGetIntegerv(GLenum pname, GLint *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = normalized? normalizedToInt(v[i]) : (GLint)lround(v[i]);
}

void glGetBooleanv(GLenum pname, GLboolean *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = (v[i] != 0.0)? GL_TRUE : GL_FALSE;
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
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    gl.state.viewport[0] = x;
    gl.state.viewport[1] = y;
    gl.state.viewport[2] = width;
    gl.state.viewport[3] = height;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    gl.state.scissorBox[0] = x;
    gl.state.scissorBox[1] = y;
    gl.state.scissorBox[2] = width;
    gl.state.scissorBox[3] = height;
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    gl.clearColor = ((u32)colorByte(red) << 24) | ((u32)colorByte(green) << 16) | ((u32)colorByte(blue) << 8) | colorByte(alpha);
}

void glClearDepth(GLclampd depth)
{
    gl.clearDepth = (depth < 0.0)? 0.0f : (depth > 1.0)? 1.0f : (float)depth;
}

void glClearStencil(GLint s) { gl.clearStencil = (u8)s; }

// Clear by drawing a full-screen quad at the clear depth: honors scissor and all write masks
static void clearWithQuad(bool color, bool depth, bool stencil)
{
    DrawState key;
    memset(&key, 0, sizeof(key));
    key.clipSpace = true;
    key.viewport[2] = screenWidth(gl.screen);       // glClear ignores the viewport
    key.viewport[3] = C3DGL_SCREEN_HEIGHT;
    key.scissor = gl.state.scissor;
    memcpy(key.scissorBox, gl.state.scissorBox, sizeof(key.scissorBox));
    key.colorMask = color? gl.state.colorMask : 0;
    key.depthTest = true;                           // Depth writes need the test enabled
    key.depthFunc = GL_ALWAYS;
    key.depthMask = depth;
    key.depthFar = 1.0f;                            // The clear depth is not affected by glDepthRange
    if (stencil)
    {
        key.stencilTest = true;
        key.stencilFunc = GL_ALWAYS;
        key.stencilRef = gl.clearStencil;
        key.stencilFuncMask = 0xFF;
        key.stencilWriteMask = gl.state.stencilWriteMask;
        key.stencilFail = key.stencilDepthFail = key.stencilPass = GL_REPLACE;
    }
    useState(&key);

    Vertex v[4];
    memset(v, 0, sizeof(v));
    for (int i = 0; i < 4; i++)
    {
        v[i].pos[0] = (i == 1 || i == 2)? 1.0f : -1.0f;
        v[i].pos[1] = (i >= 2)? 1.0f : -1.0f;
        v[i].pos[2] = 2.0f*gl.clearDepth - 1.0f;    // Window depth -> NDC
        for (int c = 0; c < 4; c++) v[i].color[c] = (u8)(gl.clearColor >> (24 - 8*c));
    }
    emitTriangle(&v[0], &v[1], &v[2]);
    emitTriangle(&v[0], &v[2], &v[3]);
}

void glClear(GLbitfield mask)
{
    // Write masks apply to clears
    bool color = (mask & GL_COLOR_BUFFER_BIT) && (gl.state.colorMask != 0);
    bool depth = (mask & GL_DEPTH_BUFFER_BIT) && gl.state.depthMask;
    bool stencil = (mask & GL_STENCIL_BUFFER_BIT) && (gl.state.stencilWriteMask != 0);
    if (!color && !depth && !stencil) return;

    // The memory fill clears whole buffers, and depth and stencil only together. Stencil may be
    // overwritten as long as it was never used
    bool fill = !gl.state.scissor &&
                (!color || (gl.state.colorMask == GPU_WRITE_COLOR)) &&
                (!stencil || (gl.state.stencilWriteMask == 0xFF)) &&
                ((depth == stencil) || (depth && !gl.stencilUsed));
    if (!fill)
    {
        clearWithQuad(color, depth, stencil);
        return;
    }

    ensureFrame();
    flush();

    // Clears run as memory fills outside the command list; split it so earlier draws stay before the clear
    if (gl.drawnThisFrame) C3D_FrameSplit(0);

    // D24S8: stencil in the top byte, depth reversed (see depthFunc())
    u32 depthStencil = ((u32)gl.clearStencil << 24) | (u32)((1.0f - gl.clearDepth)*0xFFFFFF);
    int bits = (color? C3D_CLEAR_COLOR : 0) | ((depth || stencil)? C3D_CLEAR_DEPTH : 0);
    C3D_RenderTargetClear(gl.targets[gl.screen], (C3D_ClearBits)bits, gl.clearColor, depthStencil);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    gl.state.colorMask = (red? GPU_WRITE_RED : 0) | (green? GPU_WRITE_GREEN : 0) | (blue? GPU_WRITE_BLUE : 0) | (alpha? GPU_WRITE_ALPHA : 0);
}

void glDepthMask(GLboolean flag) { gl.state.depthMask = flag; }
void glDepthFunc(GLenum func) { gl.state.depthFunc = func; }

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    gl.state.stencilFunc = func;
    gl.state.stencilRef = (u8)((ref < 0)? 0 : (ref > 255)? 255 : ref);
    gl.state.stencilFuncMask = (u8)mask;
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    gl.state.stencilFail = fail;
    gl.state.stencilDepthFail = zfail;
    gl.state.stencilPass = zpass;
}

void glStencilMask(GLuint mask) { gl.state.stencilWriteMask = (u8)mask; }

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
    if ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL)) { setError(GL_INVALID_ENUM); return; }

    switch (face)
    {
        case GL_FRONT: gl.polygonMode[0] = mode; break;
        case GL_BACK: gl.polygonMode[1] = mode; break;
        case GL_FRONT_AND_BACK: gl.polygonMode[0] = gl.polygonMode[1] = mode; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPolygonOffset(GLfloat factor, GLfloat units)
{
    gl.offsetFactor = factor;
    gl.offsetUnits = units;
}

void glDepthRange(GLclampd zNear, GLclampd zFar)
{
    gl.state.depthNear = (zNear < 0.0)? 0.0f : (zNear > 1.0)? 1.0f : (float)zNear;
    gl.state.depthFar = (zFar < 0.0)? 0.0f : (zFar > 1.0)? 1.0f : (float)zFar;
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
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPushMatrix(void)
{
    int *depth = &gl.stackDepth[matrixStack()];
    if (*depth + 1 >= C3DGL_MATRIX_STACK) { WARN_ONCE("Matrix stack overflow\n"); setError(GL_STACK_OVERFLOW); return; }

    gl.stack[matrixStack()][*depth + 1] = gl.stack[matrixStack()][*depth];
    (*depth)++;
}

void glPopMatrix(void)
{
    int *depth = &gl.stackDepth[matrixStack()];
    if (*depth == 0) { WARN_ONCE("Matrix stack underflow\n"); setError(GL_STACK_UNDERFLOW); return; }

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

void glMultMatrixd(const GLdouble *m)
{
    Mat4 mat;
    for (int i = 0; i < 16; i++) mat.m[i] = (float)m[i];
    multCurrent(&mat);
}

void glLoadMatrixf(const GLfloat *m)
{
    memcpy(currentMatrix()->m, m, 16*sizeof(float));
    matrixChanged();
}

void glLoadMatrixd(const GLdouble *m)
{
    Mat4 *cur = currentMatrix();
    for (int i = 0; i < 16; i++) cur->m[i] = (float)m[i];
    matrixChanged();
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

void glTranslated(GLdouble x, GLdouble y, GLdouble z) { glTranslatef((float)x, (float)y, (float)z); }
void glRotated(GLdouble angle, GLdouble x, GLdouble y, GLdouble z) { glRotatef((float)angle, (float)x, (float)y, (float)z); }
void glScaled(GLdouble x, GLdouble y, GLdouble z) { glScalef((float)x, (float)y, (float)z); }

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
    submitLitVertex(&v, gl.currentNormal, gl.currentEdge);
}

// Vertices are stored with w = 1: homogeneous positions are divided (exact unless w <= 0)
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (w == 0.0f) { WARN_ONCE("glVertex4: w = 0 (point at infinity) not supported\n"); return; }
    glVertex3f(x/w, y/w, z/w);
}

// The first texcoord with q != 1 switches texturing to projection mode (for good, it is exact with q = 1 too)
static void markTexQ(void)
{
    if (gl.texQUsed) return;
    gl.texQUsed = true;
    if (gl.inBegin) prepareDraw(gl.batch.clipSpace);    // The batch of the current primitive needs it already
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    gl.current.tex[0] = s;
    gl.current.tex[1] = t;
    gl.current.tex[2] = q;
    gl.currentTexR[0] = r;
    if (q != 1.0f) markTexQ();
}

void glEdgeFlag(GLboolean flag) { gl.currentEdge = flag; }
void glEdgeFlagv(const GLboolean *flag) { gl.currentEdge = *flag; }

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    gl.currentNormal[0] = nx;
    gl.currentNormal[1] = ny;
    gl.currentNormal[2] = nz;
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    gl.current.color[0] = red;
    gl.current.color[1] = green;
    gl.current.color[2] = blue;
    gl.current.color[3] = alpha;
    if (gl.colorMaterial) applyColorMaterial(gl.current.color);
}

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    glColor4ub(colorByte(red), colorByte(green), colorByte(blue), colorByte(alpha));
}


// glRect: counter-clockwise quad from (x1, y1) to (x2, y2) at z = 0
void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    glBegin(GL_QUADS);
    glVertex2f(x1, y1);
    glVertex2f(x2, y1);
    glVertex2f(x2, y2);
    glVertex2f(x1, y2);
    glEnd();
}
// All other GL 1.1 variants of glVertex, glTexCoord, glNormal, glColor, glRect, generated: integer colors and
// normals are normalized ((2c + 1)/(2^b - 1) for signed types), vertices and texcoords are not
void glVertex2d(GLdouble x, GLdouble y) { glVertex3f((float)x, (float)y, 0.0f); }
void glVertex2dv(const GLdouble *v) { glVertex3f((float)v[0], (float)v[1], 0.0f); }
void glVertex2f(GLfloat x, GLfloat y) { glVertex3f(x, y, 0.0f); }
void glVertex2fv(const GLfloat *v) { glVertex3f(v[0], v[1], 0.0f); }
void glVertex2i(GLint x, GLint y) { glVertex3f((float)x, (float)y, 0.0f); }
void glVertex2iv(const GLint *v) { glVertex3f((float)v[0], (float)v[1], 0.0f); }
void glVertex2s(GLshort x, GLshort y) { glVertex3f(x, y, 0.0f); }
void glVertex2sv(const GLshort *v) { glVertex3f(v[0], v[1], 0.0f); }
void glVertex3d(GLdouble x, GLdouble y, GLdouble z) { glVertex3f((float)x, (float)y, (float)z); }
void glVertex3dv(const GLdouble *v) { glVertex3f((float)v[0], (float)v[1], (float)v[2]); }
void glVertex3fv(const GLfloat *v) { glVertex3f(v[0], v[1], v[2]); }
void glVertex3i(GLint x, GLint y, GLint z) { glVertex3f((float)x, (float)y, (float)z); }
void glVertex3iv(const GLint *v) { glVertex3f((float)v[0], (float)v[1], (float)v[2]); }
void glVertex3s(GLshort x, GLshort y, GLshort z) { glVertex3f(x, y, z); }
void glVertex3sv(const GLshort *v) { glVertex3f(v[0], v[1], v[2]); }
void glVertex4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { glVertex4f((float)x, (float)y, (float)z, (float)w); }
void glVertex4dv(const GLdouble *v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glVertex4fv(const GLfloat *v) { glVertex4f(v[0], v[1], v[2], v[3]); }
void glVertex4i(GLint x, GLint y, GLint z, GLint w) { glVertex4f((float)x, (float)y, (float)z, (float)w); }
void glVertex4iv(const GLint *v) { glVertex4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glVertex4s(GLshort x, GLshort y, GLshort z, GLshort w) { glVertex4f(x, y, z, w); }
void glVertex4sv(const GLshort *v) { glVertex4f(v[0], v[1], v[2], v[3]); }
void glTexCoord1d(GLdouble s) { glTexCoord4f((float)s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1dv(const GLdouble *v) { glTexCoord4f((float)v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord1f(GLfloat s) { glTexCoord4f(s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1fv(const GLfloat *v) { glTexCoord4f(v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord1i(GLint s) { glTexCoord4f((float)s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1iv(const GLint *v) { glTexCoord4f((float)v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord1s(GLshort s) { glTexCoord4f(s, 0.0f, 0.0f, 1.0f); }
void glTexCoord1sv(const GLshort *v) { glTexCoord4f(v[0], 0.0f, 0.0f, 1.0f); }
void glTexCoord2d(GLdouble s, GLdouble t) { glTexCoord4f((float)s, (float)t, 0.0f, 1.0f); }
void glTexCoord2dv(const GLdouble *v) { glTexCoord4f((float)v[0], (float)v[1], 0.0f, 1.0f); }
void glTexCoord2f(GLfloat s, GLfloat t) { glTexCoord4f(s, t, 0.0f, 1.0f); }
void glTexCoord2fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], 0.0f, 1.0f); }
void glTexCoord2i(GLint s, GLint t) { glTexCoord4f((float)s, (float)t, 0.0f, 1.0f); }
void glTexCoord2iv(const GLint *v) { glTexCoord4f((float)v[0], (float)v[1], 0.0f, 1.0f); }
void glTexCoord2s(GLshort s, GLshort t) { glTexCoord4f(s, t, 0.0f, 1.0f); }
void glTexCoord2sv(const GLshort *v) { glTexCoord4f(v[0], v[1], 0.0f, 1.0f); }
void glTexCoord3d(GLdouble s, GLdouble t, GLdouble r) { glTexCoord4f((float)s, (float)t, (float)r, 1.0f); }
void glTexCoord3dv(const GLdouble *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { glTexCoord4f(s, t, r, 1.0f); }
void glTexCoord3fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], v[2], 1.0f); }
void glTexCoord3i(GLint s, GLint t, GLint r) { glTexCoord4f((float)s, (float)t, (float)r, 1.0f); }
void glTexCoord3iv(const GLint *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glTexCoord3s(GLshort s, GLshort t, GLshort r) { glTexCoord4f(s, t, r, 1.0f); }
void glTexCoord3sv(const GLshort *v) { glTexCoord4f(v[0], v[1], v[2], 1.0f); }
void glTexCoord4d(GLdouble s, GLdouble t, GLdouble r, GLdouble q) { glTexCoord4f((float)s, (float)t, (float)r, (float)q); }
void glTexCoord4dv(const GLdouble *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glTexCoord4fv(const GLfloat *v) { glTexCoord4f(v[0], v[1], v[2], v[3]); }
void glTexCoord4i(GLint s, GLint t, GLint r, GLint q) { glTexCoord4f((float)s, (float)t, (float)r, (float)q); }
void glTexCoord4iv(const GLint *v) { glTexCoord4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glTexCoord4s(GLshort s, GLshort t, GLshort r, GLshort q) { glTexCoord4f(s, t, r, q); }
void glTexCoord4sv(const GLshort *v) { glTexCoord4f(v[0], v[1], v[2], v[3]); }
void glNormal3b(GLbyte nx, GLbyte ny, GLbyte nz) { glNormal3f((2.0f*nx + 1.0f)/255.0f, (2.0f*ny + 1.0f)/255.0f, (2.0f*nz + 1.0f)/255.0f); }
void glNormal3bv(const GLbyte *v) { glNormal3f((2.0f*v[0] + 1.0f)/255.0f, (2.0f*v[1] + 1.0f)/255.0f, (2.0f*v[2] + 1.0f)/255.0f); }
void glNormal3d(GLdouble nx, GLdouble ny, GLdouble nz) { glNormal3f((float)nx, (float)ny, (float)nz); }
void glNormal3dv(const GLdouble *v) { glNormal3f((float)v[0], (float)v[1], (float)v[2]); }
void glNormal3fv(const GLfloat *v) { glNormal3f(v[0], v[1], v[2]); }
void glNormal3i(GLint nx, GLint ny, GLint nz) { glNormal3f((float)((2.0*nx + 1.0)/4294967295.0), (float)((2.0*ny + 1.0)/4294967295.0), (float)((2.0*nz + 1.0)/4294967295.0)); }
void glNormal3iv(const GLint *v) { glNormal3f((float)((2.0*v[0] + 1.0)/4294967295.0), (float)((2.0*v[1] + 1.0)/4294967295.0), (float)((2.0*v[2] + 1.0)/4294967295.0)); }
void glNormal3s(GLshort nx, GLshort ny, GLshort nz) { glNormal3f((2.0f*nx + 1.0f)/65535.0f, (2.0f*ny + 1.0f)/65535.0f, (2.0f*nz + 1.0f)/65535.0f); }
void glNormal3sv(const GLshort *v) { glNormal3f((2.0f*v[0] + 1.0f)/65535.0f, (2.0f*v[1] + 1.0f)/65535.0f, (2.0f*v[2] + 1.0f)/65535.0f); }
void glColor3b(GLbyte red, GLbyte green, GLbyte blue) { glColor4f((2.0f*red + 1.0f)/255.0f, (2.0f*green + 1.0f)/255.0f, (2.0f*blue + 1.0f)/255.0f, 1.0f); }
void glColor3bv(const GLbyte *v) { glColor4f((2.0f*v[0] + 1.0f)/255.0f, (2.0f*v[1] + 1.0f)/255.0f, (2.0f*v[2] + 1.0f)/255.0f, 1.0f); }
void glColor3d(GLdouble red, GLdouble green, GLdouble blue) { glColor4f((float)red, (float)green, (float)blue, 1.0f); }
void glColor3dv(const GLdouble *v) { glColor4f((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glColor3f(GLfloat red, GLfloat green, GLfloat blue) { glColor4f(red, green, blue, 1.0f); }
void glColor3fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], 1.0f); }
void glColor3i(GLint red, GLint green, GLint blue) { glColor4f((float)((2.0*red + 1.0)/4294967295.0), (float)((2.0*green + 1.0)/4294967295.0), (float)((2.0*blue + 1.0)/4294967295.0), 1.0f); }
void glColor3iv(const GLint *v) { glColor4f((float)((2.0*v[0] + 1.0)/4294967295.0), (float)((2.0*v[1] + 1.0)/4294967295.0), (float)((2.0*v[2] + 1.0)/4294967295.0), 1.0f); }
void glColor3s(GLshort red, GLshort green, GLshort blue) { glColor4f((2.0f*red + 1.0f)/65535.0f, (2.0f*green + 1.0f)/65535.0f, (2.0f*blue + 1.0f)/65535.0f, 1.0f); }
void glColor3sv(const GLshort *v) { glColor4f((2.0f*v[0] + 1.0f)/65535.0f, (2.0f*v[1] + 1.0f)/65535.0f, (2.0f*v[2] + 1.0f)/65535.0f, 1.0f); }
void glColor3ub(GLubyte red, GLubyte green, GLubyte blue) { glColor4ub(red, green, blue, 255); }
void glColor3ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], 255); }
void glColor3ui(GLuint red, GLuint green, GLuint blue) { glColor4f((float)(red/4294967295.0), (float)(green/4294967295.0), (float)(blue/4294967295.0), 1.0f); }
void glColor3uiv(const GLuint *v) { glColor4f((float)(v[0]/4294967295.0), (float)(v[1]/4294967295.0), (float)(v[2]/4294967295.0), 1.0f); }
void glColor3us(GLushort red, GLushort green, GLushort blue) { glColor4f(red/65535.0f, green/65535.0f, blue/65535.0f, 1.0f); }
void glColor3usv(const GLushort *v) { glColor4f(v[0]/65535.0f, v[1]/65535.0f, v[2]/65535.0f, 1.0f); }
void glColor4b(GLbyte red, GLbyte green, GLbyte blue, GLbyte alpha) { glColor4f((2.0f*red + 1.0f)/255.0f, (2.0f*green + 1.0f)/255.0f, (2.0f*blue + 1.0f)/255.0f, (2.0f*alpha + 1.0f)/255.0f); }
void glColor4bv(const GLbyte *v) { glColor4f((2.0f*v[0] + 1.0f)/255.0f, (2.0f*v[1] + 1.0f)/255.0f, (2.0f*v[2] + 1.0f)/255.0f, (2.0f*v[3] + 1.0f)/255.0f); }
void glColor4d(GLdouble red, GLdouble green, GLdouble blue, GLdouble alpha) { glColor4f((float)red, (float)green, (float)blue, (float)alpha); }
void glColor4dv(const GLdouble *v) { glColor4f((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glColor4fv(const GLfloat *v) { glColor4f(v[0], v[1], v[2], v[3]); }
void glColor4i(GLint red, GLint green, GLint blue, GLint alpha) { glColor4f((float)((2.0*red + 1.0)/4294967295.0), (float)((2.0*green + 1.0)/4294967295.0), (float)((2.0*blue + 1.0)/4294967295.0), (float)((2.0*alpha + 1.0)/4294967295.0)); }
void glColor4iv(const GLint *v) { glColor4f((float)((2.0*v[0] + 1.0)/4294967295.0), (float)((2.0*v[1] + 1.0)/4294967295.0), (float)((2.0*v[2] + 1.0)/4294967295.0), (float)((2.0*v[3] + 1.0)/4294967295.0)); }
void glColor4s(GLshort red, GLshort green, GLshort blue, GLshort alpha) { glColor4f((2.0f*red + 1.0f)/65535.0f, (2.0f*green + 1.0f)/65535.0f, (2.0f*blue + 1.0f)/65535.0f, (2.0f*alpha + 1.0f)/65535.0f); }
void glColor4sv(const GLshort *v) { glColor4f((2.0f*v[0] + 1.0f)/65535.0f, (2.0f*v[1] + 1.0f)/65535.0f, (2.0f*v[2] + 1.0f)/65535.0f, (2.0f*v[3] + 1.0f)/65535.0f); }
void glColor4ubv(const GLubyte *v) { glColor4ub(v[0], v[1], v[2], v[3]); }
void glColor4ui(GLuint red, GLuint green, GLuint blue, GLuint alpha) { glColor4f((float)(red/4294967295.0), (float)(green/4294967295.0), (float)(blue/4294967295.0), (float)(alpha/4294967295.0)); }
void glColor4uiv(const GLuint *v) { glColor4f((float)(v[0]/4294967295.0), (float)(v[1]/4294967295.0), (float)(v[2]/4294967295.0), (float)(v[3]/4294967295.0)); }
void glColor4us(GLushort red, GLushort green, GLushort blue, GLushort alpha) { glColor4f(red/65535.0f, green/65535.0f, blue/65535.0f, alpha/65535.0f); }
void glColor4usv(const GLushort *v) { glColor4f(v[0]/65535.0f, v[1]/65535.0f, v[2]/65535.0f, v[3]/65535.0f); }
void glRectd(GLdouble x1, GLdouble y1, GLdouble x2, GLdouble y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }
void glRectdv(const GLdouble *v1, const GLdouble *v2) { glRectf((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]); }
void glRectfv(const GLfloat *v1, const GLfloat *v2) { glRectf(v1[0], v1[1], v2[0], v2[1]); }
void glRecti(GLint x1, GLint y1, GLint x2, GLint y2) { glRectf((float)x1, (float)y1, (float)x2, (float)y2); }
void glRectiv(const GLint *v1, const GLint *v2) { glRectf((float)v1[0], (float)v1[1], (float)v2[0], (float)v2[1]); }
void glRects(GLshort x1, GLshort y1, GLshort x2, GLshort y2) { glRectf(x1, y1, x2, y2); }
void glRectsv(const GLshort *v1, const GLshort *v2) { glRectf(v1[0], v1[1], v2[0], v2[1]); }

//----------------------------------------------------------------------------------
// OpenGL: lighting parameters (glLight, glLightModel, glMaterial, glColorMaterial)
//----------------------------------------------------------------------------------
static bool isColorParam(GLenum pname)
{
    return (pname == GL_AMBIENT) || (pname == GL_DIFFUSE) || (pname == GL_SPECULAR) || (pname == GL_EMISSION) ||
           (pname == GL_AMBIENT_AND_DIFFUSE) || (pname == GL_LIGHT_MODEL_AMBIENT);
}

// Integer parameters: colors are mapped like glColor*i (most positive integer = 1.0), everything else converted
static void intParams(GLenum pname, const GLint *in, int count, float *out)
{
    bool color = isColorParam(pname);
    for (int i = 0; i < count; i++) out[i] = color? (float)((2.0*in[i] + 1.0)/4294967295.0) : (float)in[i];
}

static Light *lightFor(GLenum light)
{
    if ((light < GL_LIGHT0) || (light >= GL_LIGHT0 + C3DGL_MAX_LIGHTS)) { setError(GL_INVALID_ENUM); return NULL; }
    return &gl.lighting.lights[light - GL_LIGHT0];
}

// Values of a glLight parameter, 0: not a glLight parameter
static int lightParamCount(GLenum pname)
{
    switch (pname)
    {
        case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_POSITION: return 4;
        case GL_SPOT_DIRECTION: return 3;
        case GL_SPOT_EXPONENT: case GL_SPOT_CUTOFF:
        case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION: case GL_QUADRATIC_ATTENUATION: return 1;
        default: return 0;
    }
}

// Position and spot direction are stored in eye coordinates, transformed by the current modelview
static void setLight(GLenum light, GLenum pname, const float *p)
{
    Light *li = lightFor(light);
    if (li == NULL) return;

    const float *m = gl.stack[0][gl.stackDepth[0]].m;
    switch (pname)
    {
        case GL_AMBIENT: memcpy(li->ambient, p, sizeof(li->ambient)); break;
        case GL_DIFFUSE: memcpy(li->diffuse, p, sizeof(li->diffuse)); break;
        case GL_SPECULAR: memcpy(li->specular, p, sizeof(li->specular)); break;
        case GL_POSITION:
            for (int r = 0; r < 4; r++) li->position[r] = m[r]*p[0] + m[4 + r]*p[1] + m[8 + r]*p[2] + m[12 + r]*p[3];
            break;
        case GL_SPOT_DIRECTION:
            for (int r = 0; r < 3; r++) li->spotDirection[r] = m[r]*p[0] + m[4 + r]*p[1] + m[8 + r]*p[2];
            break;
        case GL_SPOT_EXPONENT:
            if ((p[0] < 0.0f) || (p[0] > 128.0f)) { setError(GL_INVALID_VALUE); return; }
            li->spotExponent = p[0];
            break;
        case GL_SPOT_CUTOFF:
            if (((p[0] < 0.0f) || (p[0] > 90.0f)) && (p[0] != 180.0f)) { setError(GL_INVALID_VALUE); return; }
            li->spotCutoff = p[0];
            break;
        case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION: case GL_QUADRATIC_ATTENUATION:
            if (p[0] < 0.0f) { setError(GL_INVALID_VALUE); return; }
            li->attenuation[pname - GL_CONSTANT_ATTENUATION] = p[0];
            break;
        default: setError(GL_INVALID_ENUM); return;
    }
    updateLight(li);
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params) { setLight(light, pname, params); }

void glLightf(GLenum light, GLenum pname, GLfloat param)
{
    if (lightParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    setLight(light, pname, &param);
}

void glLightiv(GLenum light, GLenum pname, const GLint *params)
{
    int n = lightParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    intParams(pname, params, n, f);
    setLight(light, pname, f);
}

void glLighti(GLenum light, GLenum pname, GLint param)
{
    if (lightParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    glLightiv(light, pname, &param);
}

static int lightModelParamCount(GLenum pname)
{
    if (pname == GL_LIGHT_MODEL_AMBIENT) return 4;
    return ((pname == GL_LIGHT_MODEL_LOCAL_VIEWER) || (pname == GL_LIGHT_MODEL_TWO_SIDE))? 1 : 0;
}

static void setLightModel(GLenum pname, const float *p)
{
    switch (pname)
    {
        case GL_LIGHT_MODEL_AMBIENT: memcpy(gl.lighting.modelAmbient, p, sizeof(gl.lighting.modelAmbient)); break;
        case GL_LIGHT_MODEL_LOCAL_VIEWER: gl.lighting.localViewer = (p[0] != 0.0f); break;
        case GL_LIGHT_MODEL_TWO_SIDE: gl.lighting.twoSide = (p[0] != 0.0f); break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glLightModelfv(GLenum pname, const GLfloat *params) { setLightModel(pname, params); }

void glLightModelf(GLenum pname, GLfloat param)
{
    if (lightModelParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    setLightModel(pname, &param);
}

void glLightModeliv(GLenum pname, const GLint *params)
{
    int n = lightModelParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    intParams(pname, params, n, f);
    setLightModel(pname, f);
}

void glLightModeli(GLenum pname, GLint param)
{
    if (lightModelParamCount(pname) != 1) { setError(GL_INVALID_ENUM); return; }
    glLightModeliv(pname, &param);
}

static int materialParamCount(GLenum pname)
{
    switch (pname)
    {
        case GL_AMBIENT: case GL_DIFFUSE: case GL_SPECULAR: case GL_EMISSION: case GL_AMBIENT_AND_DIFFUSE: return 4;
        case GL_COLOR_INDEXES: return 3;
        case GL_SHININESS: return 1;
        default: return 0;
    }
}

static bool faceValid(GLenum face) { return (face == GL_FRONT) || (face == GL_BACK) || (face == GL_FRONT_AND_BACK); }

// Also allowed between glBegin and glEnd: the next vertices are lit with the new material
static void setMaterial(GLenum face, GLenum pname, const float *p)
{
    if (!faceValid(face) || (materialParamCount(pname) == 0)) { setError(GL_INVALID_ENUM); return; }
    if ((pname == GL_SHININESS) && ((p[0] < 0.0f) || (p[0] > 128.0f))) { setError(GL_INVALID_VALUE); return; }

    for (int f = 0; f < 2; f++)
    {
        if ((face != GL_FRONT_AND_BACK) && (face != (f? GL_BACK : GL_FRONT))) continue;
        Material *m = &gl.lighting.material[f];
        switch (pname)
        {
            case GL_AMBIENT: memcpy(m->ambient, p, sizeof(m->ambient)); break;
            case GL_DIFFUSE: memcpy(m->diffuse, p, sizeof(m->diffuse)); break;
            case GL_AMBIENT_AND_DIFFUSE:
                memcpy(m->ambient, p, sizeof(m->ambient));
                memcpy(m->diffuse, p, sizeof(m->diffuse));
                break;
            case GL_SPECULAR: memcpy(m->specular, p, sizeof(m->specular)); break;
            case GL_EMISSION: memcpy(m->emission, p, sizeof(m->emission)); break;
            case GL_SHININESS: m->shininess = p[0]; break;
            default: memcpy(m->colorIndexes, p, sizeof(m->colorIndexes)); break;    // GL_COLOR_INDEXES
        }
    }
}

void glMaterialfv(GLenum face, GLenum pname, const GLfloat *params) { setMaterial(face, pname, params); }

void glMaterialf(GLenum face, GLenum pname, GLfloat param)
{
    if (pname != GL_SHININESS) { setError(GL_INVALID_ENUM); return; }
    setMaterial(face, pname, &param);
}

void glMaterialiv(GLenum face, GLenum pname, const GLint *params)
{
    int n = materialParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    intParams(pname, params, n, f);
    setMaterial(face, pname, f);
}

void glMateriali(GLenum face, GLenum pname, GLint param)
{
    if (pname != GL_SHININESS) { setError(GL_INVALID_ENUM); return; }
    glMaterialiv(face, pname, &param);
}

void glColorMaterial(GLenum face, GLenum mode)
{
    bool modeValid = (mode == GL_EMISSION) || (mode == GL_AMBIENT) || (mode == GL_DIFFUSE) || (mode == GL_SPECULAR) ||
                     (mode == GL_AMBIENT_AND_DIFFUSE);
    if (!faceValid(face) || !modeValid) { setError(GL_INVALID_ENUM); return; }
    gl.lighting.colorMaterialFace = face;
    gl.lighting.colorMaterialMode = mode;
    if (gl.colorMaterial) applyColorMaterial(gl.current.color);
}

// glGetLight: fills v and returns the number of values (0: error). *color: the values are colors
static int getLight(GLenum light, GLenum pname, float v[4], bool *color)
{
    const Light *li = lightFor(light);
    if (li == NULL) return 0;

    *color = isColorParam(pname);
    switch (pname)
    {
        case GL_AMBIENT: memcpy(v, li->ambient, sizeof(li->ambient)); return 4;
        case GL_DIFFUSE: memcpy(v, li->diffuse, sizeof(li->diffuse)); return 4;
        case GL_SPECULAR: memcpy(v, li->specular, sizeof(li->specular)); return 4;
        case GL_POSITION: memcpy(v, li->position, sizeof(li->position)); return 4;
        case GL_SPOT_DIRECTION: memcpy(v, li->spotDirection, sizeof(li->spotDirection)); return 3;
        case GL_SPOT_EXPONENT: v[0] = li->spotExponent; return 1;
        case GL_SPOT_CUTOFF: v[0] = li->spotCutoff; return 1;
        case GL_CONSTANT_ATTENUATION: case GL_LINEAR_ATTENUATION: case GL_QUADRATIC_ATTENUATION:
            v[0] = li->attenuation[pname - GL_CONSTANT_ATTENUATION];
            return 1;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

// glGetMaterial: face is GL_FRONT or GL_BACK
static int getMaterial(GLenum face, GLenum pname, float v[4], bool *color)
{
    if ((face != GL_FRONT) && (face != GL_BACK)) { setError(GL_INVALID_ENUM); return 0; }

    const Material *m = &gl.lighting.material[(face == GL_BACK)? 1 : 0];
    *color = isColorParam(pname);
    switch (pname)
    {
        case GL_AMBIENT: memcpy(v, m->ambient, sizeof(m->ambient)); return 4;
        case GL_DIFFUSE: memcpy(v, m->diffuse, sizeof(m->diffuse)); return 4;
        case GL_SPECULAR: memcpy(v, m->specular, sizeof(m->specular)); return 4;
        case GL_EMISSION: memcpy(v, m->emission, sizeof(m->emission)); return 4;
        case GL_SHININESS: v[0] = m->shininess; return 1;
        case GL_COLOR_INDEXES: memcpy(v, m->colorIndexes, sizeof(m->colorIndexes)); return 3;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetLightfv(GLenum light, GLenum pname, GLfloat *params)
{
    float v[4];
    bool color;
    int n = getLight(light, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetLightiv(GLenum light, GLenum pname, GLint *params)
{
    float v[4];
    bool color;
    int n = getLight(light, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = color? normalizedToInt(v[i]) : (GLint)lroundf(v[i]);
}

void glGetMaterialfv(GLenum face, GLenum pname, GLfloat *params)
{
    float v[4];
    bool color;
    int n = getMaterial(face, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetMaterialiv(GLenum face, GLenum pname, GLint *params)
{
    float v[4];
    bool color;
    int n = getMaterial(face, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = color? normalizedToInt(v[i]) : (GLint)lroundf(v[i]);
}

//----------------------------------------------------------------------------------
// OpenGL: client-side vertex arrays
//----------------------------------------------------------------------------------
static int typeSize(GLenum type)
{
    switch (type)
    {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
        case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: case GL_FIXED: return 4;
        case GL_DOUBLE: return 8;
        default: return 0;
    }
}

// Validate and set a client array. sizes: bit n set = size n allowed; types: zero-terminated list
static void setArray(int index, GLint size, GLenum type, GLsizei stride, const GLvoid *pointer, unsigned sizes, const GLenum *types)
{
    if (((sizes >> size) & 1) == 0) { setError(GL_INVALID_VALUE); return; }
    if (stride < 0) { setError(GL_INVALID_VALUE); return; }

    bool valid = false;
    for (const GLenum *t = types; *t != 0; t++) valid = valid || (*t == type);
    if (!valid) { setError(GL_INVALID_ENUM); return; }

    gl.arrays[index].pointer = pointer;
    gl.arrays[index].buffer = gl.arrayBuffer;
    gl.arrays[index].size = size;
    gl.arrays[index].type = type;
    gl.arrays[index].stride = stride;
}

// Types of GL 1.1 and ES 1.1 together (GL_BYTE and GL_FIXED from ES, GL_INT and GL_DOUBLE from GL)
static const GLenum positionTypes[] = { GL_BYTE, GL_SHORT, GL_INT, GL_FLOAT, GL_DOUBLE, GL_FIXED, 0 };
static const GLenum colorTypes[] = { GL_BYTE, GL_UNSIGNED_BYTE, GL_SHORT, GL_UNSIGNED_SHORT, GL_INT, GL_UNSIGNED_INT,
                                     GL_FLOAT, GL_DOUBLE, GL_FIXED, 0 };
static const GLenum edgeFlagTypes[] = { GL_UNSIGNED_BYTE, 0 };

void glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_VERTEX, size, type, stride, pointer, (1 << 2) | (1 << 3) | (1 << 4), positionTypes);
}

void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_TEXCOORD, size, type, stride, pointer, (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4), positionTypes);
}

void glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_COLOR, size, type, stride, pointer, (1 << 3) | (1 << 4), colorTypes);
}

void glNormalPointer(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    if (type == GL_UNSIGNED_BYTE) { setError(GL_INVALID_ENUM); return; }
    setArray(ARRAY_NORMAL, 3, type, stride, pointer, 1 << 3, positionTypes);
}

void glEdgeFlagPointer(GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_EDGEFLAG, 1, GL_UNSIGNED_BYTE, stride, pointer, 1 << 1, edgeFlagTypes);
}

void glGetPointerv(GLenum pname, GLvoid **params)
{
    switch (pname)
    {
        case GL_VERTEX_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_VERTEX].pointer; break;
        case GL_NORMAL_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_NORMAL].pointer; break;
        case GL_COLOR_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_COLOR].pointer; break;
        case GL_TEXTURE_COORD_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_TEXCOORD].pointer; break;
        case GL_EDGE_FLAG_ARRAY_POINTER: *params = (GLvoid *)gl.arrays[ARRAY_EDGEFLAG].pointer; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

// Data of `bytes` bytes at `offset` of a buffer object, or of client memory if buffer is 0.
// NULL if it is outside the buffer (GL leaves that undefined; we skip the data instead of crashing)
static const u8 *bufferRange(GLuint buffer, const void *pointer, size_t offset, size_t bytes)
{
    if (buffer == 0) return (const u8 *)pointer + offset;

    const Buffer *b = &gl.buffers[buffer];
    size_t start = (size_t)(uintptr_t)pointer + offset;
    if ((b->data == NULL) || (start + bytes > (size_t)b->size))
    {
        WARN_ONCE("Buffer object %u read out of range, skipped\n", buffer);
        return NULL;
    }
    return b->data + start;
}

// Address of element `index` of a client array (NULL if outside its buffer object)
static const u8 *arrayElement(const ClientArray *a, int index)
{
    int size = typeSize(a->type);
    size_t stride = a->stride? (size_t)a->stride : (size_t)(a->size*size);
    return bufferRange(a->buffer, a->pointer, (size_t)index*stride, (size_t)(a->size*size));
}

// Components of element `index` as floats. normalized: integer types map to [0, 1] / [-1, 1] like glColor.
// False if the element is outside its buffer object
static bool readArray(const ClientArray *a, int index, float out[4], bool normalized)
{
    int size = typeSize(a->type);
    const u8 *p = arrayElement(a, index);
    if (p == NULL) return false;

    for (int i = 0; i < a->size; i++, p += size)
    {
        switch (a->type)
        {
            case GL_BYTE: { s8 v = *(const s8 *)p; out[i] = normalized? (2.0f*v + 1.0f)/255.0f : v; break; }
            case GL_UNSIGNED_BYTE: out[i] = normalized? *p/255.0f : *p; break;
            case GL_SHORT: { s16 v; memcpy(&v, p, 2); out[i] = normalized? (2.0f*v + 1.0f)/65535.0f : v; break; }
            case GL_UNSIGNED_SHORT: { u16 v; memcpy(&v, p, 2); out[i] = normalized? v/65535.0f : v; break; }
            case GL_INT: { s32 v; memcpy(&v, p, 4); out[i] = normalized? (float)((2.0*v + 1.0)/4294967295.0) : (float)v; break; }
            case GL_UNSIGNED_INT: { u32 v; memcpy(&v, p, 4); out[i] = normalized? (float)(v/4294967295.0) : (float)v; break; }
            case GL_FIXED: { s32 v; memcpy(&v, p, 4); out[i] = v/65536.0f; break; }
            case GL_DOUBLE: { double v; memcpy(&v, p, 8); out[i] = (float)v; break; }
            default: memcpy(&out[i], p, 4); break;     // GL_FLOAT
        }
    }
    return true;
}

// Enabled and set; with a buffer object the pointer is an offset and may be 0
static bool arrayActive(int index)
{
    return gl.arrays[index].enabled && ((gl.arrays[index].pointer != NULL) || (gl.arrays[index].buffer != 0));
}

// Vertex `index` from the enabled arrays; attributes without an array come from the current values.
// Without a vertex array only the current values are updated (glArrayElement)
static void submitArrayVertex(int index)
{
    Vertex v = gl.current;
    bool edge = gl.currentEdge;

    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        if (!arrayActive(ARRAY_TEXCOORD0 + unit)) continue;
        float t[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (!readArray(&gl.arrays[ARRAY_TEXCOORD0 + unit], index, t, false)) return;
        float *dst = (unit == 0)? v.tex : v.texExtra[unit - 1];
        dst[0] = t[0];
        dst[1] = t[1];
        dst[2] = t[3];
    }
    if (arrayActive(ARRAY_COLOR))
    {
        float c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (!readArray(&gl.arrays[ARRAY_COLOR], index, c, true)) return;
        for (int i = 0; i < 4; i++) v.color[i] = colorByte(c[i]);
    }
    if (arrayActive(ARRAY_NORMAL))
    {
        float n[4];
        if (!readArray(&gl.arrays[ARRAY_NORMAL], index, n, true)) return;
        memcpy(gl.currentNormal, n, sizeof(gl.currentNormal));
    }
    if (arrayActive(ARRAY_EDGEFLAG))
    {
        const u8 *e = arrayElement(&gl.arrays[ARRAY_EDGEFLAG], index);
        if (e == NULL) return;
        edge = (*e != 0);
    }

    if (!arrayActive(ARRAY_VERTEX))
    {
        memcpy(gl.current.tex, v.tex, sizeof(v.tex));
        memcpy(gl.current.texExtra, v.texExtra, sizeof(v.texExtra));
        memcpy(gl.current.color, v.color, 4);
        gl.currentEdge = edge;
        return;
    }

    float p[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    if (!readArray(&gl.arrays[ARRAY_VERTEX], index, p, false)) return;
    if (p[3] != 1.0f)
    {
        if (p[3] == 0.0f) { WARN_ONCE("Vertex array: w = 0 (point at infinity) not supported\n"); return; }
        for (int i = 0; i < 3; i++) p[i] /= p[3];
    }
    memcpy(v.pos, p, sizeof(v.pos));
    submitLitVertex(&v, gl.currentNormal, edge);
}

// Before drawing from arrays: a vertex array is needed, size 4 texcoords may need projection mode
static bool arraysReady(void)
{
    if (!arrayActive(ARRAY_VERTEX)) return false;
    if (arrayActive(ARRAY_TEXCOORD0) && (gl.arrays[ARRAY_TEXCOORD0].size == 4)) markTexQ();
    return true;
}

void glArrayElement(GLint i)
{
    if (arrayActive(ARRAY_TEXCOORD0) && (gl.arrays[ARRAY_TEXCOORD0].size == 4)) markTexQ();
    if (!gl.inBegin && arrayActive(ARRAY_VERTEX)) return;    // A vertex outside glBegin/glEnd is ignored
    submitArrayVertex(i);
}

// glInterleavedArrays formats: which arrays, their sizes/types and byte offsets, the default stride
typedef struct {
    GLenum format;
    int texSize, colorSize, normal, vertexSize;
    GLenum colorType;
    int colorOffset, normalOffset, vertexOffset, stride;
} InterleavedFormat;

#define F_ sizeof(GLfloat)
#define C_ 4            // 4 GLubytes rounded up to a multiple of sizeof(GLfloat)
static const InterleavedFormat interleavedFormats[] = {
    { GL_V2F,             0, 0, 0, 2, 0,                0,     0,     0,      2*F_ },
    { GL_V3F,             0, 0, 0, 3, 0,                0,     0,     0,      3*F_ },
    { GL_C4UB_V2F,        0, 4, 0, 2, GL_UNSIGNED_BYTE, 0,     0,     C_,     C_ + 2*F_ },
    { GL_C4UB_V3F,        0, 4, 0, 3, GL_UNSIGNED_BYTE, 0,     0,     C_,     C_ + 3*F_ },
    { GL_C3F_V3F,         0, 3, 0, 3, GL_FLOAT,         0,     0,     3*F_,   6*F_ },
    { GL_N3F_V3F,         0, 0, 1, 3, 0,                0,     0,     3*F_,   6*F_ },
    { GL_C4F_N3F_V3F,     0, 4, 1, 3, GL_FLOAT,         0,     4*F_,  7*F_,   10*F_ },
    { GL_T2F_V3F,         2, 0, 0, 3, 0,                0,     0,     2*F_,   5*F_ },
    { GL_T4F_V4F,         4, 0, 0, 4, 0,                0,     0,     4*F_,   8*F_ },
    { GL_T2F_C4UB_V3F,    2, 4, 0, 3, GL_UNSIGNED_BYTE, 2*F_,  0,     C_ + 2*F_, C_ + 5*F_ },
    { GL_T2F_C3F_V3F,     2, 3, 0, 3, GL_FLOAT,         2*F_,  0,     5*F_,   8*F_ },
    { GL_T2F_N3F_V3F,     2, 0, 1, 3, 0,                0,     2*F_,  5*F_,   8*F_ },
    { GL_T2F_C4F_N3F_V3F, 2, 4, 1, 3, GL_FLOAT,         2*F_,  6*F_,  9*F_,   12*F_ },
    { GL_T4F_C4F_N3F_V4F, 4, 4, 1, 4, GL_FLOAT,         4*F_,  8*F_,  11*F_,  15*F_ },
};
#undef F_
#undef C_

void glInterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer)
{
    const InterleavedFormat *f = NULL;
    for (size_t i = 0; i < sizeof(interleavedFormats)/sizeof(interleavedFormats[0]); i++)
        if (interleavedFormats[i].format == format) f = &interleavedFormats[i];
    if (f == NULL) { setError(GL_INVALID_ENUM); return; }
    if (stride < 0) { setError(GL_INVALID_VALUE); return; }

    if (stride == 0) stride = f->stride;
    const u8 *base = pointer;

    gl.arrays[ARRAY_EDGEFLAG].enabled = false;
    gl.arrays[ARRAY_TEXCOORD].enabled = (f->texSize > 0);
    if (f->texSize > 0) glTexCoordPointer(f->texSize, GL_FLOAT, stride, base);
    gl.arrays[ARRAY_COLOR].enabled = (f->colorSize > 0);
    if (f->colorSize > 0) glColorPointer(f->colorSize, f->colorType, stride, base + f->colorOffset);
    gl.arrays[ARRAY_NORMAL].enabled = f->normal;
    if (f->normal) glNormalPointer(GL_FLOAT, stride, base + f->normalOffset);
    gl.arrays[ARRAY_VERTEX].enabled = true;
    glVertexPointer(f->vertexSize, GL_FLOAT, stride, base + f->vertexOffset);
}

void glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    if (count < 0) { setError(GL_INVALID_VALUE); return; }
    if (!arraysReady() || !beginPrimitive(mode)) return;

    for (int i = 0; i < count; i++) submitArrayVertex(first + i);
    endPrimitive();
}

void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    if ((type != GL_UNSIGNED_BYTE) && (type != GL_UNSIGNED_SHORT) && (type != GL_UNSIGNED_INT)) { setError(GL_INVALID_ENUM); return; }
    if (count < 0) { setError(GL_INVALID_VALUE); return; }

    // With an element array buffer bound, `indices` is an offset into it
    int indexSize = typeSize(type);
    const u8 *data = bufferRange(gl.elementArrayBuffer, indices, 0, (size_t)count*indexSize);
    if ((data == NULL) || !arraysReady() || !beginPrimitive(mode)) return;

    for (int i = 0; i < count; i++)
    {
        const u8 *p = data + (size_t)i*indexSize;
        int index = 0;
        if (type == GL_UNSIGNED_SHORT) { GLushort v; memcpy(&v, p, 2); index = v; }
        else if (type == GL_UNSIGNED_INT) { GLuint v; memcpy(&v, p, 4); index = (int)v; }
        else index = *p;     // GL_UNSIGNED_BYTE, checked above
        submitArrayVertex(index);
    }
    endPrimitive();
}

//----------------------------------------------------------------------------------
// OpenGL: buffer objects (ES 1.1, GL 1.5)
//----------------------------------------------------------------------------------
static bool bufferValid(GLuint id)
{
    return (id > 0) && (id < gl.bufferCount) && gl.buffers[id].used;
}

// Make room for ids up to `id` (table grows, there is no fixed limit)
static bool reserveBufferId(GLuint id)
{
    if (id < gl.bufferCount) return true;

    GLuint count = gl.bufferCount? gl.bufferCount : 16;
    while (count <= id) count *= 2;
    Buffer *table = realloc(gl.buffers, count*sizeof(Buffer));
    if (table == NULL) { setError(GL_OUT_OF_MEMORY); return false; }
    memset(table + gl.bufferCount, 0, (count - gl.bufferCount)*sizeof(Buffer));
    gl.buffers = table;
    gl.bufferCount = count;
    return true;
}

void glGenBuffers(GLsizei n, GLuint *buffers)
{
    if (n < 0) { setError(GL_INVALID_VALUE); return; }

    GLuint id = 1;
    for (int i = 0; i < n; i++)
    {
        while ((id < gl.bufferCount) && gl.buffers[id].used) id++;
        if (!reserveBufferId(id)) return;
        memset(&gl.buffers[id], 0, sizeof(Buffer));
        gl.buffers[id].used = true;
        gl.buffers[id].usage = GL_STATIC_DRAW;
        buffers[i] = id++;
    }
}

void glDeleteBuffers(GLsizei n, const GLuint *buffers)
{
    if (n < 0) { setError(GL_INVALID_VALUE); return; }

    for (int i = 0; i < n; i++)
    {
        GLuint id = buffers[i];
        if (!bufferValid(id)) continue;

        // Bindings to the deleted buffer revert to 0; arrays sourcing from it are cleared (their pointer is an offset)
        if (gl.arrayBuffer == id) gl.arrayBuffer = 0;
        if (gl.elementArrayBuffer == id) gl.elementArrayBuffer = 0;
        for (int a = 0; a < ARRAY_COUNT; a++)
        {
            if (gl.arrays[a].buffer != id) continue;
            gl.arrays[a].buffer = 0;
            gl.arrays[a].pointer = NULL;
        }

        free(gl.buffers[id].data);
        memset(&gl.buffers[id], 0, sizeof(Buffer));
    }
}

GLboolean glIsBuffer(GLuint buffer) { return bufferValid(buffer); }

static GLuint *bufferBinding(GLenum target)
{
    switch (target)
    {
        case GL_ARRAY_BUFFER: return &gl.arrayBuffer;
        case GL_ELEMENT_ARRAY_BUFFER: return &gl.elementArrayBuffer;
        default: setError(GL_INVALID_ENUM); return NULL;
    }
}

void glBindBuffer(GLenum target, GLuint buffer)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;

    // Binding an unused name creates the buffer (like glBindTexture)
    if ((buffer != 0) && !bufferValid(buffer))
    {
        if (!reserveBufferId(buffer)) return;
        memset(&gl.buffers[buffer], 0, sizeof(Buffer));
        gl.buffers[buffer].used = true;
        gl.buffers[buffer].usage = GL_STATIC_DRAW;
    }
    *binding = buffer;
}

static bool bufferUsageValid(GLenum usage)
{
    // ES 1.1: STATIC_DRAW, DYNAMIC_DRAW; GL 1.5 adds the STREAM and READ/COPY variants
    return (usage >= GL_STREAM_DRAW) && (usage <= GL_DYNAMIC_COPY) && (usage != 0x88E3) && (usage != 0x88E7);
}

void glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;
    if (!bufferUsageValid(usage)) { setError(GL_INVALID_ENUM); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    if (*binding == 0) { setError(GL_INVALID_OPERATION); return; }

    Buffer *b = &gl.buffers[*binding];
    u8 *storage = NULL;
    if (size > 0)
    {
        storage = malloc((size_t)size);
        if (storage == NULL) { setError(GL_OUT_OF_MEMORY); return; }
        if (data != NULL) memcpy(storage, data, (size_t)size);
    }
    free(b->data);
    b->data = storage;
    b->size = size;
    b->usage = usage;
}

void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;
    if (*binding == 0) { setError(GL_INVALID_OPERATION); return; }

    Buffer *b = &gl.buffers[*binding];
    if ((offset < 0) || (size < 0) || (offset + size > b->size)) { setError(GL_INVALID_VALUE); return; }
    if (size > 0) memcpy(b->data + offset, data, (size_t)size);
}

void glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
    GLuint *binding = bufferBinding(target);
    if (binding == NULL) return;
    if (*binding == 0) { setError(GL_INVALID_OPERATION); return; }

    const Buffer *b = &gl.buffers[*binding];
    switch (pname)
    {
        case GL_BUFFER_SIZE: *params = (GLint)b->size; break;
        case GL_BUFFER_USAGE: *params = (GLint)b->usage; break;
        default: setError(GL_INVALID_ENUM); break;
    }
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

// Byte offset of pixel (x, y) in a Morton-swizzled PICA texture image of texWidth x texHeight
// (8x8 tiles, Z-order inside a tile)
static u32 tiledOffset(int texWidth, int texHeight, int x, int y, int bpp)
{
#if C3DGL_TEXTURE_FLIP_Y
    y = texHeight - 1 - y;
#else
    (void)texHeight;
#endif
    u32 tile = (u32)((y >> 3)*(texWidth >> 3) + (x >> 3));
    u32 morton = (x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3);
    return (tile*64 + morton)*bpp;
}

// Stored mip level `level`: data and padded size
static u8 *levelData(Texture *t, int level, int *texWidth, int *texHeight)
{
    *texWidth = t->tex.width >> level;
    *texHeight = t->tex.height >> level;
    return (u8 *)C3D_Tex2DGetImagePtr(&t->tex, level, NULL);
}

// Copy a rectangle between GL pixel data (laid out as described by ps) and stored mip level `level`,
// in either direction
static void transferPixels(Texture *t, int level, int x0, int y0, int w, int h, u8 *pixels, const PixelStore *ps, bool upload)
{
    const TexFormat *f = &t->format;
    size_t rowBytes = (size_t)((ps->rowLength > 0)? ps->rowLength : w)*f->bpp;
    if (ps->alignment > 1) rowBytes = (rowBytes + ps->alignment - 1)/ps->alignment*ps->alignment;
    pixels += (size_t)ps->skipRows*rowBytes + (size_t)ps->skipPixels*f->bpp;

    // Byte order within a pixel: PICA reversal, and GL byte swapping of 16-bit elements
    bool reverse = f->reverse != (f->packed16 && ps->swapBytes);

    int texWidth, texHeight;
    u8 *texData = levelData(t, level, &texWidth, &texHeight);
    for (int y = 0; y < h; y++)
    {
        u8 *row = pixels + (size_t)y*rowBytes;
        for (int x = 0; x < w; x++)
        {
            u8 *src = row + x*f->bpp;
            u8 *dst = texData + tiledOffset(texWidth, texHeight, x0 + x, y0 + y, f->bpp);
            for (int i = 0; i < f->bpp; i++)
            {
                int j = reverse? (f->bpp - 1 - i) : i;
                if (upload) dst[j] = src[i];
                else src[i] = dst[j];
            }
        }
    }
}

static Texture *boundTexture(GLenum target)
{
    GLuint id = gl.boundTexture[gl.activeTexture];
    if ((target != GL_TEXTURE_2D) || (id == 0) || (id >= C3DGL_MAX_TEXTURES)) return NULL;
    return &gl.textures[id];
}

// Texture about to change: submit pending vertices that use it and rebind it for the next draw
// (C3D_TexBind() only keeps a pointer, changes are not picked up otherwise)
static void textureModified(GLuint id)
{
    bool used = false;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) used = used || (gl.batch.units[unit].texture == id);
    if (gl.batchValid && used)
    {
        flush();
        gl.batchValid = false;
    }
}

static void applyTextureParams(Texture *t)
{
    C3D_TexSetFilter(&t->tex, texFilter(t->magFilter), texFilter(t->minFilter));
    C3D_TexSetWrap(&t->tex, texWrap(t->wrapS), texWrap(t->wrapT));

    // Mipmap filters use all stored levels, the others only level 0
    bool mipLinear = (t->minFilter == GL_NEAREST_MIPMAP_LINEAR) || (t->minFilter == GL_LINEAR_MIPMAP_LINEAR);
    C3D_TexSetFilterMipmap(&t->tex, mipLinear? GPU_LINEAR : GPU_NEAREST);
    t->tex.minLevel = 0;
    t->tex.maxLevel = mipmapFilter(t->minFilter)? (u8)(t->levels - 1) : 0;
}

// GL mipmap completeness: levels 1..log2(max(w, h)) defined with halved sizes and level 0's format
static void updateCompleteness(Texture *t)
{
    const TexLevel *base = &t->level[0];
    t->complete = base->defined && (base->width > 0) && (base->height > 0);
    for (int l = 1; t->complete && ((base->width >> l) > 0 || (base->height >> l) > 0); l++)
    {
        const TexLevel *lv = &t->level[l];
        int w = (base->width >> l)? (base->width >> l) : 1, h = (base->height >> l)? (base->height >> l) : 1;
        t->complete = lv->defined && (lv->width == w) && (lv->height == h) && (lv->format == base->format) &&
                      (lv->border == base->border);
    }
}

// Switch tex to a mip chain (down to 8x8), keeping level 0; false if the size has no levels below 8x8
static bool ensureMipmapStorage(Texture *t)
{
    if (t->levels > 1) return true;
    if (C3D_TexCalcMaxLevel(t->tex.width, t->tex.height) < 1) return false;

    C3D_Tex mip;
    if (!C3D_TexInitMipmap(&mip, t->tex.width, t->tex.height, t->format.format))
    {
        LOG("Out of memory for mipmaps\n");
        setError(GL_OUT_OF_MEMORY);
        return false;
    }
    memset(mip.data, 0, C3D_TexCalcTotalSize(mip.size, mip.maxLevel));
    memcpy(mip.data, t->tex.data, t->tex.size);     // Level 0 comes first in both

    if (gl.frameActive) deferTextureDelete(&t->tex);
    else C3D_TexDelete(&t->tex);
    t->tex = mip;
    t->levels = mip.maxLevel + 1;
    return true;
}

static void flushTexture(Texture *t)
{
    GSPGPU_FlushDataCache(t->tex.data, C3D_TexCalcTotalSize(t->tex.size, t->levels - 1));
}

// Unpack/pack a texel of a 16-bit packed format into 4 channels of 0..255
static void unpack16(GPU_TEXCOLOR format, u16 v, int c[4])
{
    switch (format)
    {
        case GPU_RGB565: c[0] = (v >> 11)*255/31; c[1] = ((v >> 5) & 63)*255/63; c[2] = (v & 31)*255/31; c[3] = 255; break;
        case GPU_RGBA5551: c[0] = (v >> 11)*255/31; c[1] = ((v >> 6) & 31)*255/31; c[2] = ((v >> 1) & 31)*255/31; c[3] = (v & 1)*255; break;
        default: c[0] = (v >> 12)*17; c[1] = ((v >> 8) & 15)*17; c[2] = ((v >> 4) & 15)*17; c[3] = (v & 15)*17; break;   // RGBA4
    }
}

static u16 pack16(GPU_TEXCOLOR format, const int c[4])
{
    switch (format)
    {
        case GPU_RGB565: return (u16)(((c[0]*31 + 127)/255 << 11) | ((c[1]*63 + 127)/255 << 5) | ((c[2]*31 + 127)/255));
        case GPU_RGBA5551: return (u16)(((c[0]*31 + 127)/255 << 11) | ((c[1]*31 + 127)/255 << 6) | ((c[2]*31 + 127)/255 << 1) | (c[3] >= 128));
        default: return (u16)(((c[0]*15 + 127)/255 << 12) | ((c[1]*15 + 127)/255 << 8) | ((c[2]*15 + 127)/255 << 4) | ((c[3]*15 + 127)/255));
    }
}

// GL_GENERATE_MIPMAP: all levels from level 0 with a 2x2 box filter (on the CPU). Levels below 8x8 are only
// marked as defined, PICA cannot sample them
static void generateMipmaps(Texture *t)
{
    const TexLevel base = t->level[0];
    for (int l = 1; (base.width >> l) > 0 || (base.height >> l) > 0; l++)
    {
        TexLevel *lv = &t->level[l];
        *lv = base;
        lv->width = (base.width >> l)? (base.width >> l) : 1;
        lv->height = (base.height >> l)? (base.height >> l) : 1;
        lv->border = 0;
    }
    t->level[0].border = 0;     // Generated levels have no border, level 0's border texels were dropped anyway
    updateCompleteness(t);

    if (!ensureMipmapStorage(t)) return;
    int bpp = t->format.bpp;
    for (int l = 1; l < t->levels; l++)
    {
        int srcW, srcH, dstW, dstH;
        const u8 *src = levelData(t, l - 1, &srcW, &srcH);
        u8 *dst = levelData(t, l, &dstW, &dstH);
        int w = t->level[l].width, h = t->level[l].height;
        int sw = t->level[l - 1].width, sh = t->level[l - 1].height;

        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                // Source texels 2x..2x+1, 2y..2y+1, clamped for odd and 1-wide sizes
                int xs[2] = { 2*x, (2*x + 1 < sw)? 2*x + 1 : 2*x }, ys[2] = { 2*y, (2*y + 1 < sh)? 2*y + 1 : 2*y };
                int sum[4] = { 0 };
                for (int i = 0; i < 4; i++)
                {
                    const u8 *p = src + tiledOffset(srcW, srcH, xs[i & 1], ys[i >> 1], bpp);
                    if (t->format.packed16)
                    {
                        int c[4];
                        u16 v;
                        memcpy(&v, p, 2);
                        unpack16(t->format.format, v, c);
                        for (int k = 0; k < 4; k++) sum[k] += c[k];
                    }
                    else for (int k = 0; k < bpp; k++) sum[k] += p[k];     // 8-bit channels: average byte-wise
                }

                u8 *q = dst + tiledOffset(dstW, dstH, x, y, bpp);
                if (t->format.packed16)
                {
                    int c[4] = { (sum[0] + 2)/4, (sum[1] + 2)/4, (sum[2] + 2)/4, (sum[3] + 2)/4 };
                    u16 v = pack16(t->format.format, c);
                    memcpy(q, &v, 2);
                }
                else for (int k = 0; k < bpp; k++) q[k] = (u8)((sum[k] + 2)/4);
            }
        }
    }
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    GLuint id = 1;
    for (int i = 0; i < n; i++)
    {
        while ((id < C3DGL_MAX_TEXTURES) && gl.textures[id].used) id++;
        if (id >= C3DGL_MAX_TEXTURES) { LOG("Out of texture ids\n"); setError(GL_OUT_OF_MEMORY); textures[i] = 0; continue; }

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
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) if (gl.boundTexture[unit] == id) gl.boundTexture[unit] = 0;
    }
}

GLboolean glIsTexture(GLuint texture)
{
    return (texture > 0) && (texture < C3DGL_MAX_TEXTURES) && gl.textures[texture].used;
}

void glBindTexture(GLenum target, GLuint texture)
{
    if (target == GL_TEXTURE_2D) gl.boundTexture[gl.activeTexture] = texture;
}

static bool combineFuncValid(GLenum f, bool alpha)
{
    switch (f)
    {
        case GL_REPLACE: case GL_MODULATE: case GL_ADD: case GL_ADD_SIGNED: case GL_INTERPOLATE: case GL_SUBTRACT: return true;
        case GL_DOT3_RGB: case GL_DOT3_RGBA: return !alpha;
        default: return false;
    }
}

static bool combineSourceValid(GLenum src)
{
    return (src == GL_TEXTURE) || (src == GL_CONSTANT) || (src == GL_PRIMARY_COLOR) || (src == GL_PREVIOUS) ||
           ((src >= GL_TEXTURE0) && (src < GL_TEXTURE0 + C3DGL_TEXTURE_UNITS));    // Crossbar (GL 1.4)
}

// glTexEnv of the active texture unit; float-valued parameters (scales) arrive as floats
static void setTexEnv(GLenum target, GLenum pname, GLint value, GLfloat fvalue)
{
    if (target != GL_TEXTURE_ENV) { setError(GL_INVALID_ENUM); return; }
    TexEnvState *e = &gl.state.units[gl.activeTexture].env;

    switch (pname)
    {
        case GL_TEXTURE_ENV_MODE:
            switch (value)
            {
                case GL_MODULATE: case GL_REPLACE: case GL_DECAL: case GL_BLEND: case GL_ADD: case GL_COMBINE:
                    e->mode = (GLenum)value;
                    break;
                default: WARN_ONCE("glTexEnv: mode 0x%x not supported\n", value); setError(GL_INVALID_ENUM); break;
            }
            return;
        case GL_COMBINE_RGB: case GL_COMBINE_ALPHA:
            if (!combineFuncValid(value, pname == GL_COMBINE_ALPHA)) { setError(GL_INVALID_ENUM); return; }
            if (pname == GL_COMBINE_RGB) e->combineRgb = value;
            else e->combineAlpha = value;
            return;
        case GL_SRC0_RGB: case GL_SRC1_RGB: case GL_SRC2_RGB:
        case GL_SRC0_ALPHA: case GL_SRC1_ALPHA: case GL_SRC2_ALPHA:
            if (!combineSourceValid(value)) { setError(GL_INVALID_ENUM); return; }
            if (pname <= GL_SRC2_RGB) e->srcRgb[pname - GL_SRC0_RGB] = value;
            else e->srcAlpha[pname - GL_SRC0_ALPHA] = value;
            return;
        case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB:
            if ((value != GL_SRC_COLOR) && (value != GL_ONE_MINUS_SRC_COLOR) && (value != GL_SRC_ALPHA) &&
                (value != GL_ONE_MINUS_SRC_ALPHA)) { setError(GL_INVALID_ENUM); return; }
            e->operandRgb[pname - GL_OPERAND0_RGB] = value;
            return;
        case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA:
            if ((value != GL_SRC_ALPHA) && (value != GL_ONE_MINUS_SRC_ALPHA)) { setError(GL_INVALID_ENUM); return; }
            e->operandAlpha[pname - GL_OPERAND0_ALPHA] = value;
            return;
        case GL_RGB_SCALE: case GL_ALPHA_SCALE:
            if ((fvalue != 1.0f) && (fvalue != 2.0f) && (fvalue != 4.0f)) { setError(GL_INVALID_VALUE); return; }
            if (pname == GL_RGB_SCALE) e->rgbScale = (u8)fvalue;
            else e->alphaScale = (u8)fvalue;
            return;
        default: setError(GL_INVALID_ENUM); return;
    }
}

void glTexEnvi(GLenum target, GLenum pname, GLint param) { setTexEnv(target, pname, param, (GLfloat)param); }
void glTexEnvf(GLenum target, GLenum pname, GLfloat param) { setTexEnv(target, pname, (GLint)param, param); }

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    if ((target == GL_TEXTURE_ENV) && (pname == GL_TEXTURE_ENV_COLOR))
    {
        gl.state.units[gl.activeTexture].env.color = ((u32)colorByte(params[3]) << 24) | ((u32)colorByte(params[2]) << 16) |
                                                     ((u32)colorByte(params[1]) << 8) | colorByte(params[0]);
    }
    else glTexEnvf(target, pname, params[0]);
}

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    if (pname == GL_TEXTURE_ENV_COLOR)
    {
        // Integer colors map [0, INT_MAX] to [0, 1]
        GLfloat color[4];
        for (int i = 0; i < 4; i++) color[i] = (GLfloat)params[i]/2147483647.0f;
        glTexEnvfv(target, pname, color);
    }
    else glTexEnvi(target, pname, params[0]);
}

// glGetTexEnv: values of the active unit; returns the count, 0 on error
static int getTexEnv(GLenum target, GLenum pname, float v[4])
{
    if (target != GL_TEXTURE_ENV) { setError(GL_INVALID_ENUM); return 0; }
    const TexEnvState *e = &gl.state.units[gl.activeTexture].env;
    switch (pname)
    {
        case GL_TEXTURE_ENV_MODE: v[0] = e->mode; return 1;
        case GL_TEXTURE_ENV_COLOR: for (int i = 0; i < 4; i++) v[i] = ((e->color >> (8*i)) & 0xFF)/255.0f; return 4;
        case GL_COMBINE_RGB: v[0] = e->combineRgb; return 1;
        case GL_COMBINE_ALPHA: v[0] = e->combineAlpha; return 1;
        case GL_SRC0_RGB: case GL_SRC1_RGB: case GL_SRC2_RGB: v[0] = e->srcRgb[pname - GL_SRC0_RGB]; return 1;
        case GL_SRC0_ALPHA: case GL_SRC1_ALPHA: case GL_SRC2_ALPHA: v[0] = e->srcAlpha[pname - GL_SRC0_ALPHA]; return 1;
        case GL_OPERAND0_RGB: case GL_OPERAND1_RGB: case GL_OPERAND2_RGB: v[0] = e->operandRgb[pname - GL_OPERAND0_RGB]; return 1;
        case GL_OPERAND0_ALPHA: case GL_OPERAND1_ALPHA: case GL_OPERAND2_ALPHA: v[0] = e->operandAlpha[pname - GL_OPERAND0_ALPHA]; return 1;
        case GL_RGB_SCALE: v[0] = e->rgbScale; return 1;
        case GL_ALPHA_SCALE: v[0] = e->alphaScale; return 1;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetTexEnvfv(GLenum target, GLenum pname, GLfloat *params)
{
    float v[4];
    int n = getTexEnv(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetTexEnviv(GLenum target, GLenum pname, GLint *params)
{
    float v[4];
    int n = getTexEnv(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = (pname == GL_TEXTURE_ENV_COLOR)? (GLint)(v[i]*2147483647.0f) : (GLint)v[i];
}

// Multitexturing (ES 1.1, GL 1.3)
void glActiveTexture(GLenum texture)
{
    if ((texture < GL_TEXTURE0) || (texture >= GL_TEXTURE0 + C3DGL_TEXTURE_UNITS)) { setError(GL_INVALID_ENUM); return; }
    gl.activeTexture = texture - GL_TEXTURE0;
}

void glClientActiveTexture(GLenum texture)
{
    if ((texture < GL_TEXTURE0) || (texture >= GL_TEXTURE0 + C3DGL_TEXTURE_UNITS)) { setError(GL_INVALID_ENUM); return; }
    gl.clientActiveTexture = texture - GL_TEXTURE0;
}

void glMultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    if ((target < GL_TEXTURE0) || (target >= GL_TEXTURE0 + C3DGL_TEXTURE_UNITS)) { setError(GL_INVALID_ENUM); return; }
    int unit = target - GL_TEXTURE0;
    if (unit == 0) { glTexCoord4f(s, t, r, q); return; }

    // Units 1/2: q is divided out per vertex by the shader
    float *tc = gl.current.texExtra[unit - 1];
    tc[0] = s;
    tc[1] = t;
    tc[2] = q;
    gl.currentTexR[unit] = r;
}

// All other variants of glMultiTexCoord (generated like the glTexCoord variants)
void glMultiTexCoord1d(GLenum target, GLdouble s) { glMultiTexCoord4f(target, (float)s, 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1dv(GLenum target, const GLdouble *v) { glMultiTexCoord4f(target, (float)v[0], 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1f(GLenum target, GLfloat s) { glMultiTexCoord4f(target, s, 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1fv(GLenum target, const GLfloat *v) { glMultiTexCoord4f(target, v[0], 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1i(GLenum target, GLint s) { glMultiTexCoord4f(target, (float)s, 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1iv(GLenum target, const GLint *v) { glMultiTexCoord4f(target, (float)v[0], 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1s(GLenum target, GLshort s) { glMultiTexCoord4f(target, s, 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord1sv(GLenum target, const GLshort *v) { glMultiTexCoord4f(target, v[0], 0.0f, 0.0f, 1.0f); }
void glMultiTexCoord2d(GLenum target, GLdouble s, GLdouble t) { glMultiTexCoord4f(target, (float)s, (float)t, 0.0f, 1.0f); }
void glMultiTexCoord2dv(GLenum target, const GLdouble *v) { glMultiTexCoord4f(target, (float)v[0], (float)v[1], 0.0f, 1.0f); }
void glMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t) { glMultiTexCoord4f(target, s, t, 0.0f, 1.0f); }
void glMultiTexCoord2fv(GLenum target, const GLfloat *v) { glMultiTexCoord4f(target, v[0], v[1], 0.0f, 1.0f); }
void glMultiTexCoord2i(GLenum target, GLint s, GLint t) { glMultiTexCoord4f(target, (float)s, (float)t, 0.0f, 1.0f); }
void glMultiTexCoord2iv(GLenum target, const GLint *v) { glMultiTexCoord4f(target, (float)v[0], (float)v[1], 0.0f, 1.0f); }
void glMultiTexCoord2s(GLenum target, GLshort s, GLshort t) { glMultiTexCoord4f(target, s, t, 0.0f, 1.0f); }
void glMultiTexCoord2sv(GLenum target, const GLshort *v) { glMultiTexCoord4f(target, v[0], v[1], 0.0f, 1.0f); }
void glMultiTexCoord3d(GLenum target, GLdouble s, GLdouble t, GLdouble r) { glMultiTexCoord4f(target, (float)s, (float)t, (float)r, 1.0f); }
void glMultiTexCoord3dv(GLenum target, const GLdouble *v) { glMultiTexCoord4f(target, (float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glMultiTexCoord3f(GLenum target, GLfloat s, GLfloat t, GLfloat r) { glMultiTexCoord4f(target, s, t, r, 1.0f); }
void glMultiTexCoord3fv(GLenum target, const GLfloat *v) { glMultiTexCoord4f(target, v[0], v[1], v[2], 1.0f); }
void glMultiTexCoord3i(GLenum target, GLint s, GLint t, GLint r) { glMultiTexCoord4f(target, (float)s, (float)t, (float)r, 1.0f); }
void glMultiTexCoord3iv(GLenum target, const GLint *v) { glMultiTexCoord4f(target, (float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glMultiTexCoord3s(GLenum target, GLshort s, GLshort t, GLshort r) { glMultiTexCoord4f(target, s, t, r, 1.0f); }
void glMultiTexCoord3sv(GLenum target, const GLshort *v) { glMultiTexCoord4f(target, v[0], v[1], v[2], 1.0f); }
void glMultiTexCoord4d(GLenum target, GLdouble s, GLdouble t, GLdouble r, GLdouble q) { glMultiTexCoord4f(target, (float)s, (float)t, (float)r, (float)q); }
void glMultiTexCoord4dv(GLenum target, const GLdouble *v) { glMultiTexCoord4f(target, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glMultiTexCoord4fv(GLenum target, const GLfloat *v) { glMultiTexCoord4f(target, v[0], v[1], v[2], v[3]); }
void glMultiTexCoord4i(GLenum target, GLint s, GLint t, GLint r, GLint q) { glMultiTexCoord4f(target, (float)s, (float)t, (float)r, (float)q); }
void glMultiTexCoord4iv(GLenum target, const GLint *v) { glMultiTexCoord4f(target, (float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glMultiTexCoord4s(GLenum target, GLshort s, GLshort t, GLshort r, GLshort q) { glMultiTexCoord4f(target, s, t, r, q); }
void glMultiTexCoord4sv(GLenum target, const GLshort *v) { glMultiTexCoord4f(target, v[0], v[1], v[2], v[3]); }

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError((target == GL_TEXTURE_2D)? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }

    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER:
            if ((param != GL_NEAREST) && (param != GL_LINEAR) && !mipmapFilter(param)) { setError(GL_INVALID_ENUM); return; }
            t->minFilter = param;
            break;
        case GL_TEXTURE_MAG_FILTER:
            if ((param != GL_NEAREST) && (param != GL_LINEAR)) { setError(GL_INVALID_ENUM); return; }
            t->magFilter = param;
            break;
        case GL_TEXTURE_WRAP_S: case GL_TEXTURE_WRAP_T:
            if ((param != GL_REPEAT) && (param != GL_CLAMP_TO_EDGE) && (param != GL_MIRRORED_REPEAT) && (param != GL_CLAMP))
            {
                setError(GL_INVALID_ENUM);
                return;
            }
            if (pname == GL_TEXTURE_WRAP_S) t->wrapS = param;
            else t->wrapT = param;
            break;
        case GL_GENERATE_MIPMAP:
            t->generateMipmap = (param != 0);
            if (t->generateMipmap && t->loaded)
            {
                textureModified(gl.boundTexture[gl.activeTexture]);
                generateMipmaps(t);
                flushTexture(t);
            }
            break;
        default: setError(GL_INVALID_ENUM); return;
    }

    if (t->loaded)
    {
        textureModified(gl.boundTexture[gl.activeTexture]);
        applyTextureParams(t);
    }
}

// glGetTexParameter: values of the texture bound to the active unit; returns the count, 0 on error
static int getTexParameter(GLenum target, GLenum pname, float v[4])
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError((target == GL_TEXTURE_2D)? GL_INVALID_OPERATION : GL_INVALID_ENUM); return 0; }
    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER: v[0] = t->minFilter; return 1;
        case GL_TEXTURE_MAG_FILTER: v[0] = t->magFilter; return 1;
        case GL_TEXTURE_WRAP_S: v[0] = t->wrapS; return 1;
        case GL_TEXTURE_WRAP_T: v[0] = t->wrapT; return 1;
        case GL_GENERATE_MIPMAP: v[0] = t->generateMipmap; return 1;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    float v[4];
    int n = getTexParameter(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = (GLint)v[i];
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    float v[4];
    int n = getTexParameter(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param) { glTexParameteri(target, pname, (GLint)param); }
void glTexParameteriv(GLenum target, GLenum pname, const GLint *params) { glTexParameteri(target, pname, params[0]); }
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { glTexParameteri(target, pname, (GLint)params[0]); }

// Size check shared by real and proxy textures. Non-power-of-two sizes are accepted (padded internally)
static bool textureSizeValid(GLint level, GLsizei width, GLsizei height, GLint border)
{
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL) || ((border != 0) && (border != 1))) return false;
    return (width - 2*border >= 0) && (height - 2*border >= 0);
}

static bool textureSizeFits(GLint level, GLsizei width, GLsizei height, GLint border)
{
    int max = C3DGL_MAX_TEXTURE_SIZE >> level, w = 1, h = 1;
    while (w < width - 2*border) w <<= 1;
    while (h < height - 2*border) h <<= 1;
    return (w <= max) && (h <= max);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (!textureSizeValid(level, width, height, border)) { setError(GL_INVALID_VALUE); return; }

    TexFormat f;
    if (!texFormat(format, type, &f)) { LOG("glTexImage2D: format 0x%x/0x%x not supported\n", format, type); setError(GL_INVALID_ENUM); return; }

    // Proxy: only record whether the image would be accepted
    if (target == GL_PROXY_TEXTURE_2D)
    {
        ProxyLevel *p = &gl.proxy2D[level];
        memset(p, 0, sizeof(*p));
        if (textureSizeFits(level, width, height, border))
        {
            p->width = width;
            p->height = height;
            p->border = border;
            p->internalFormat = internalformat;
            p->format = f.format;
        }
        return;
    }

    Texture *t = boundTexture(target);
    if (t == NULL) { setError((target == GL_TEXTURE_2D)? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }

    if (!textureSizeFits(level, width, height, border))
    {
        LOG("glTexImage2D: %ix%i exceeds the maximum of %i\n", width, height, C3DGL_MAX_TEXTURE_SIZE >> level);
        setError(GL_INVALID_VALUE);
        return;
    }

    int imageWidth = width - 2*border, imageHeight = height - 2*border;
    TexLevel lv = { true, imageWidth, imageHeight, border, internalformat, f.format };
    textureModified(gl.boundTexture[gl.activeTexture]);

    // The border texels are not stored (PICA has no texture borders): skip them
    PixelStore ps = gl.unpack;
    if (border)
    {
        if (ps.rowLength == 0) ps.rowLength = width;
        ps.skipRows += border;
        ps.skipPixels += border;
    }

    if (level > 0)
    {
        if (!t->loaded) { WARN_ONCE("glTexImage2D: mipmap level before level 0, ignored\n"); return; }
        t->level[level] = lv;
        updateCompleteness(t);

        // Stored if it fits the chain (sizes and format of level 0), levels below 8x8 only count for completeness
        bool matches = (lv.width == (t->width >> level)) && (lv.height == (t->height >> level)) && (f.format == t->format.format);
        if (matches && (level <= C3D_TexCalcMaxLevel(t->tex.width, t->tex.height)) && ensureMipmapStorage(t))
        {
            if (pixels != NULL) transferPixels(t, level, 0, 0, imageWidth, imageHeight, (u8 *)pixels, &ps, true);
            flushTexture(t);
        }
        applyTextureParams(t);
        return;
    }

    // Level 0: keep the storage (and the other levels) if only the content changes
    int texWidth = nextPow2(imageWidth), texHeight = nextPow2(imageHeight);
    bool same = t->loaded && (imageWidth == t->width) && (imageHeight == t->height) && (f.format == t->format.format);
    if (!same)
    {
        if (t->loaded)
        {
            if (gl.frameActive) deferTextureDelete(&t->tex);
            else C3D_TexDelete(&t->tex);
            t->loaded = false;
        }
        if (!C3D_TexInit(&t->tex, texWidth, texHeight, f.format))
        {
            LOG("glTexImage2D: out of memory for %ix%i texture\n", texWidth, texHeight);
            setError(GL_OUT_OF_MEMORY);
            return;
        }
        memset(t->tex.data, 0, t->tex.size);
        memset(t->level, 0, sizeof(t->level));
        t->loaded = true;
        t->levels = 1;
        t->format = f;
        t->width = imageWidth;
        t->height = imageHeight;
    }
    t->level[0] = lv;

    if (pixels != NULL) transferPixels(t, 0, 0, 0, imageWidth, imageHeight, (u8 *)pixels, &ps, true);
    if (t->generateMipmap) generateMipmaps(t);
    updateCompleteness(t);
    flushTexture(t);
    applyTextureParams(t);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels)
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError((target == GL_TEXTURE_2D)? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }
    const TexLevel *lv = &t->level[level];
    if (!t->loaded || !lv->defined) { setError(GL_INVALID_OPERATION); return; }
    if (pixels == NULL) return;

    TexFormat f;
    if (!texFormat(format, type, &f) || (f.format != t->format.format)) { LOG("glTexSubImage2D: format mismatch\n"); setError(GL_INVALID_OPERATION); return; }
    if ((xoffset < 0) || (yoffset < 0) || (xoffset + width > lv->width) || (yoffset + height > lv->height)) { setError(GL_INVALID_VALUE); return; }

    textureModified(gl.boundTexture[gl.activeTexture]);
    if (level < t->levels) transferPixels(t, level, xoffset, yoffset, width, height, (u8 *)pixels, &gl.unpack, true);
    if ((level == 0) && t->generateMipmap) generateMipmaps(t);
    flushTexture(t);
}

void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError((target == GL_TEXTURE_2D)? GL_INVALID_OPERATION : GL_INVALID_ENUM); return; }
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }
    if (!t->loaded || !t->level[level].defined) return;

    TexFormat f;
    if (!texFormat(format, type, &f) || (f.format != t->format.format)) { LOG("glGetTexImage: format mismatch\n"); setError(GL_INVALID_OPERATION); return; }

    if (level < t->levels) transferPixels(t, level, 0, 0, t->level[level].width, t->level[level].height, (u8 *)pixels, &gl.pack, false);
    else WARN_ONCE("glGetTexImage: levels below 8x8 are not stored\n");
}

// Bits per component of a PICA format: R, G, B, A, L
static void formatBits(GPU_TEXCOLOR format, int bits[5])
{
    memset(bits, 0, 5*sizeof(int));
    switch (format)
    {
        case GPU_RGBA8: bits[0] = bits[1] = bits[2] = bits[3] = 8; break;
        case GPU_RGB8: bits[0] = bits[1] = bits[2] = 8; break;
        case GPU_RGBA5551: bits[0] = bits[1] = bits[2] = 5; bits[3] = 1; break;
        case GPU_RGB565: bits[0] = 5; bits[1] = 6; bits[2] = 5; break;
        case GPU_RGBA4: bits[0] = bits[1] = bits[2] = bits[3] = 4; break;
        case GPU_LA8: bits[3] = bits[4] = 8; break;
        case GPU_L8: bits[4] = 8; break;
        case GPU_A8: bits[3] = 8; break;
        default: break;
    }
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }

    // Width, height, border, internal format, format of the level; zero if it has no image
    GLint width = 0, height = 0, border = 0, internalFormat = 0;
    GPU_TEXCOLOR format = 0;
    bool hasImage = false;
    switch (target)
    {
        case GL_TEXTURE_2D:
        {
            Texture *t = boundTexture(target);
            if ((t != NULL) && t->loaded && t->level[level].defined)
            {
                const TexLevel *lv = &t->level[level];
                width = lv->width + 2*lv->border;
                height = lv->height + 2*lv->border;
                border = lv->border;
                internalFormat = lv->internalFormat;
                format = lv->format;
                hasImage = true;
            }
            break;
        }
        case GL_PROXY_TEXTURE_2D:
        {
            const ProxyLevel *p = &gl.proxy2D[level];
            width = p->width;
            height = p->height;
            border = p->border;
            internalFormat = p->internalFormat;
            format = p->format;
            hasImage = (p->width > 0);
            break;
        }
        case GL_TEXTURE_1D: case GL_PROXY_TEXTURE_1D: break;    // No 1D textures yet
        default: setError(GL_INVALID_ENUM); return;
    }

    int bits[5] = { 0 };
    if (hasImage) formatBits(format, bits);
    switch (pname)
    {
        case GL_TEXTURE_WIDTH: *params = width; break;
        case GL_TEXTURE_HEIGHT: *params = height; break;
        case GL_TEXTURE_BORDER: *params = border; break;
        case GL_TEXTURE_INTERNAL_FORMAT: *params = hasImage? internalFormat : 1; break;     // GL default: 1 component
        case GL_TEXTURE_RED_SIZE: *params = bits[0]; break;
        case GL_TEXTURE_GREEN_SIZE: *params = bits[1]; break;
        case GL_TEXTURE_BLUE_SIZE: *params = bits[2]; break;
        case GL_TEXTURE_ALPHA_SIZE: *params = bits[3]; break;
        case GL_TEXTURE_LUMINANCE_SIZE: *params = bits[4]; break;
        case GL_TEXTURE_INTENSITY_SIZE: *params = 0; break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glGetTexLevelParameterfv(GLenum target, GLint level, GLenum pname, GLfloat *params)
{
    GLint value = 0;
    glGetTexLevelParameteriv(target, level, pname, &value);
    *params = (GLfloat)value;
}

//----------------------------------------------------------------------------------
// OpenGL: evaluators (GL). Maps are Bezier polynomials in u (and v), evaluated on the CPU with de Casteljau.
// Evaluated values feed the generated vertex only; like in GL they do not change the current color, normal or
// texcoords. Evaluated texcoords go to texture unit 0.
//----------------------------------------------------------------------------------
static const int mapComponents[9] = { 4, 1, 3, 1, 2, 3, 4, 3, 4 };   // COLOR_4, INDEX, NORMAL, TEXCOORD_1..4, VERTEX_3, VERTEX_4
enum { MAP_COLOR, MAP_INDEX, MAP_NORMAL, MAP_TEX1, MAP_TEX2, MAP_TEX3, MAP_TEX4, MAP_VERTEX3, MAP_VERTEX4 };

static struct EvalMap *mapForTarget(GLenum target, bool *twoD)
{
    if ((target >= GL_MAP1_COLOR_4) && (target <= GL_MAP1_VERTEX_4)) { *twoD = false; return &gl.map1[target - GL_MAP1_COLOR_4]; }
    if ((target >= GL_MAP2_COLOR_4) && (target <= GL_MAP2_VERTEX_4)) { *twoD = true; return &gl.map2[target - GL_MAP2_COLOR_4]; }
    return NULL;
}

// Store control points: uorder x vorder points of k components, read with the given strides
static void defineMap(GLenum target, double u1, double u2, int ustride, int uorder, double v1, double v2, int vstride,
                      int vorder, const void *points, bool isDouble, bool twoD)
{
    bool targetTwoD;
    struct EvalMap *m = mapForTarget(target, &targetTwoD);
    if ((m == NULL) || (targetTwoD != twoD)) { setError(GL_INVALID_ENUM); return; }
    int k = mapComponents[m - (twoD? gl.map2 : gl.map1)];
    if ((uorder < 1) || (uorder > C3DGL_MAX_EVAL_ORDER) || (vorder < 1) || (vorder > C3DGL_MAX_EVAL_ORDER) ||
        (u1 == u2) || (twoD && (v1 == v2)) || (ustride < k) || (twoD && (vstride < k)))
    {
        setError(GL_INVALID_VALUE);
        return;
    }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    float *data = malloc((size_t)uorder*vorder*k*sizeof(float));
    if (data == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    for (int i = 0; i < uorder; i++)
        for (int j = 0; j < vorder; j++)
            for (int c = 0; c < k; c++)
            {
                size_t src = (size_t)i*ustride + (size_t)j*vstride + c;
                data[(i*vorder + j)*k + c] = isDouble? (float)((const double *)points)[src] : ((const float *)points)[src];
            }

    free(m->points);
    m->points = data;
    m->uorder = uorder;
    m->vorder = vorder;
    m->u1 = (float)u1; m->u2 = (float)u2;
    m->v1 = (float)v1; m->v2 = (float)v2;
}

void glMap1f(GLenum target, GLfloat u1, GLfloat u2, GLint stride, GLint order, const GLfloat *points)
{
    defineMap(target, u1, u2, stride, order, 0.0, 1.0, 0, 1, points, false, false);
}
void glMap1d(GLenum target, GLdouble u1, GLdouble u2, GLint stride, GLint order, const GLdouble *points)
{
    defineMap(target, u1, u2, stride, order, 0.0, 1.0, 0, 1, points, true, false);
}
void glMap2f(GLenum target, GLfloat u1, GLfloat u2, GLint ustride, GLint uorder,
             GLfloat v1, GLfloat v2, GLint vstride, GLint vorder, const GLfloat *points)
{
    defineMap(target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points, false, true);
}
void glMap2d(GLenum target, GLdouble u1, GLdouble u2, GLint ustride, GLint uorder,
             GLdouble v1, GLdouble v2, GLint vstride, GLint vorder, const GLdouble *points)
{
    defineMap(target, u1, u2, ustride, uorder, v1, v2, vstride, vorder, points, true, true);
}

// Bernstein basis of degree n - 1 at t into b[0..n-1], built up degree by degree (stable, like de Casteljau);
// db: its derivative d/dt, from the basis of degree n - 2
static void bernstein(int n, float t, float *b, float *db)
{
    b[0] = 1.0f;
    for (int d = 1; d < n; d++)
    {
        if ((d == n - 1) && (db != NULL))
            for (int i = 0; i < n; i++) db[i] = (float)(n - 1)*(((i > 0)? b[i - 1] : 0.0f) - ((i < n - 1)? b[i] : 0.0f));
        b[d] = t*b[d - 1];
        for (int i = d - 1; i > 0; i--) b[i] = (1.0f - t)*b[i] + t*b[i - 1];
        b[0] *= 1.0f - t;
    }
    if ((n == 1) && (db != NULL)) db[0] = 0.0f;
}

// Evaluate a 1D map at u
static void evalMap1(const struct EvalMap *m, int k, float u, float *out)
{
    float b[C3DGL_MAX_EVAL_ORDER];
    bernstein(m->uorder, (u - m->u1)/(m->u2 - m->u1), b, NULL);
    for (int c = 0; c < k; c++) out[c] = 0.0f;
    for (int i = 0; i < m->uorder; i++)
        for (int c = 0; c < k; c++) out[c] += b[i]*m->points[i*k + c];
}

// Evaluate a 2D map at (u, v): the value and, if wanted, the partial derivatives d/du and d/dv
static void evalMap2(const struct EvalMap *m, int k, float u, float v, float *out, float *du, float *dv)
{
    float bu[C3DGL_MAX_EVAL_ORDER], bv[C3DGL_MAX_EVAL_ORDER], dbu[C3DGL_MAX_EVAL_ORDER], dbv[C3DGL_MAX_EVAL_ORDER];
    bool derivs = (du != NULL);
    bernstein(m->uorder, (u - m->u1)/(m->u2 - m->u1), bu, derivs? dbu : NULL);
    bernstein(m->vorder, (v - m->v1)/(m->v2 - m->v1), bv, derivs? dbv : NULL);

    for (int c = 0; c < k; c++) out[c] = 0.0f;
    if (derivs) for (int c = 0; c < k; c++) du[c] = dv[c] = 0.0f;
    const float *p = m->points;
    for (int i = 0; i < m->uorder; i++)
    {
        for (int j = 0; j < m->vorder; j++, p += k)
        {
            float w = bu[i]*bv[j];
            for (int c = 0; c < k; c++) out[c] += w*p[c];
            if (!derivs) continue;
            float wu = dbu[i]*bv[j]/(m->u2 - m->u1), wv = bu[i]*dbv[j]/(m->v2 - m->v1);
            for (int c = 0; c < k; c++) { du[c] += wu*p[c]; dv[c] += wv*p[c]; }
        }
    }
}

// Highest enabled texcoord map (GL: the one with the most components wins)
static int texcoordMap(const struct EvalMap *maps)
{
    for (int i = MAP_TEX4; i >= MAP_TEX1; i--) if (maps[i].enabled && maps[i].points) return i;
    return -1;
}

// Build and submit the vertex for evaluated values. normal: evaluated normal, NULL: the current normal
static void submitEvaluated(const float *pos, int posSize, const float *color, const float *tex, int texSize, const float *normal)
{
    Vertex v = gl.current;      // Values without a map come from the current state
    if (color != NULL) for (int c = 0; c < 4; c++) v.color[c] = colorByte(color[c]);
    if (tex != NULL)
    {
        v.tex[0] = tex[0];
        v.tex[1] = (texSize > 1)? tex[1] : 0.0f;
        v.tex[2] = (texSize > 3)? tex[3] : 1.0f;
        if (v.tex[2] != 1.0f) markTexQ();
    }

    float w = (posSize == 4)? pos[3] : 1.0f;
    if (w == 0.0f) { WARN_ONCE("Evaluator: w = 0 (point at infinity) not supported\n"); return; }
    for (int c = 0; c < 3; c++) v.pos[c] = pos[c]/w;
    submitLitVertex(&v, (normal != NULL)? normal : gl.currentNormal, gl.currentEdge);
}

void glEvalCoord1f(GLfloat u)
{
    const struct EvalMap *maps = gl.map1;
    int vertexMap = (maps[MAP_VERTEX4].enabled && maps[MAP_VERTEX4].points)? MAP_VERTEX4 :
                    (maps[MAP_VERTEX3].enabled && maps[MAP_VERTEX3].points)? MAP_VERTEX3 : -1;
    if (!gl.inBegin || (vertexMap < 0)) return;     // No vertex map: no vertex (GL)

    float pos[4], color[4], tex[4], normal[3];
    #define EVAL1(index, out) evalMap1(&maps[index], mapComponents[index], u, out)
    EVAL1(vertexMap, pos);
    bool hasColor = maps[MAP_COLOR].enabled && maps[MAP_COLOR].points;
    if (hasColor) EVAL1(MAP_COLOR, color);
    int texMap = texcoordMap(maps);
    if (texMap >= 0) EVAL1(texMap, tex);
    bool hasNormal = maps[MAP_NORMAL].enabled && maps[MAP_NORMAL].points;
    if (hasNormal) EVAL1(MAP_NORMAL, normal);
    #undef EVAL1

    submitEvaluated(pos, mapComponents[vertexMap], hasColor? color : NULL, (texMap >= 0)? tex : NULL,
                    mapComponents[texMap >= 0? texMap : 0], hasNormal? normal : NULL);
}

void glEvalCoord2f(GLfloat u, GLfloat v)
{
    const struct EvalMap *maps = gl.map2;
    int vertexMap = (maps[MAP_VERTEX4].enabled && maps[MAP_VERTEX4].points)? MAP_VERTEX4 :
                    (maps[MAP_VERTEX3].enabled && maps[MAP_VERTEX3].points)? MAP_VERTEX3 : -1;
    if (!gl.inBegin || (vertexMap < 0)) return;

    int k = mapComponents[vertexMap];
    float pos[4], du[4], dv[4], color[4], tex[4], normal[3];
    bool autoNormal = gl.autoNormal;
    evalMap2(&maps[vertexMap], k, u, v, pos, autoNormal? du : NULL, autoNormal? dv : NULL);

    bool hasNormal = false;
    if (autoNormal)
    {
        // Normal = dP/du x dP/dv of the 3D position (for VERTEX_4: of (x, y, z)/w, quotient rule)
        if (k == 4)
        {
            float w = pos[3], w2 = w*w;
            for (int c = 0; c < 3; c++)
            {
                du[c] = (du[c]*w - pos[c]*du[3])/w2;
                dv[c] = (dv[c]*w - pos[c]*dv[3])/w2;
            }
        }
        normal[0] = du[1]*dv[2] - du[2]*dv[1];
        normal[1] = du[2]*dv[0] - du[0]*dv[2];
        normal[2] = du[0]*dv[1] - du[1]*dv[0];
        hasNormal = true;
    }
    else if (maps[MAP_NORMAL].enabled && maps[MAP_NORMAL].points)
    {
        evalMap2(&maps[MAP_NORMAL], 3, u, v, normal, NULL, NULL);
        hasNormal = true;
    }

    bool hasColor = maps[MAP_COLOR].enabled && maps[MAP_COLOR].points;
    if (hasColor) evalMap2(&maps[MAP_COLOR], 4, u, v, color, NULL, NULL);
    int texMap = texcoordMap(maps);
    if (texMap >= 0) evalMap2(&maps[texMap], mapComponents[texMap], u, v, tex, NULL, NULL);

    submitEvaluated(pos, k, hasColor? color : NULL, (texMap >= 0)? tex : NULL, mapComponents[texMap >= 0? texMap : 0],
                    hasNormal? normal : NULL);
}

void glEvalCoord1d(GLdouble u) { glEvalCoord1f((float)u); }
void glEvalCoord1fv(const GLfloat *u) { glEvalCoord1f(u[0]); }
void glEvalCoord1dv(const GLdouble *u) { glEvalCoord1f((float)u[0]); }
void glEvalCoord2d(GLdouble u, GLdouble v) { glEvalCoord2f((float)u, (float)v); }
void glEvalCoord2fv(const GLfloat *u) { glEvalCoord2f(u[0], u[1]); }
void glEvalCoord2dv(const GLdouble *u) { glEvalCoord2f((float)u[0], (float)u[1]); }

void glMapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
    if (un <= 0) { setError(GL_INVALID_VALUE); return; }
    gl.grid1n = un;
    gl.grid1u1 = u1;
    gl.grid1u2 = u2;
}

void glMapGrid1d(GLint un, GLdouble u1, GLdouble u2) { glMapGrid1f(un, (float)u1, (float)u2); }

void glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
    if ((un <= 0) || (vn <= 0)) { setError(GL_INVALID_VALUE); return; }
    gl.grid2un = un; gl.grid2u1 = u1; gl.grid2u2 = u2;
    gl.grid2vn = vn; gl.grid2v1 = v1; gl.grid2v2 = v2;
}

void glMapGrid2d(GLint un, GLdouble u1, GLdouble u2, GLint vn, GLdouble v1, GLdouble v2)
{
    glMapGrid2f(un, (float)u1, (float)u2, vn, (float)v1, (float)v2);
}

// Grid coordinate i of n segments over [a, b]; exactly b at i = n (as required by the spec)
static float gridCoord(int i, int n, float a, float b) { return (i == n)? b : a + (b - a)*i/n; }

void glEvalPoint1(GLint i) { glEvalCoord1f(gridCoord(i, gl.grid1n, gl.grid1u1, gl.grid1u2)); }

void glEvalPoint2(GLint i, GLint j)
{
    glEvalCoord2f(gridCoord(i, gl.grid2un, gl.grid2u1, gl.grid2u2), gridCoord(j, gl.grid2vn, gl.grid2v1, gl.grid2v2));
}

void glEvalMesh1(GLenum mode, GLint i1, GLint i2)
{
    if ((mode != GL_POINT) && (mode != GL_LINE)) { setError(GL_INVALID_ENUM); return; }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    glBegin((mode == GL_POINT)? GL_POINTS : GL_LINE_STRIP);
    for (int i = i1; i <= i2; i++) glEvalPoint1(i);
    glEnd();
}

void glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
    if ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL)) { setError(GL_INVALID_ENUM); return; }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    switch (mode)
    {
        case GL_FILL:
            for (int j = j1; j < j2; j++)
            {
                glBegin(GL_QUAD_STRIP);
                for (int i = i1; i <= i2; i++) { glEvalPoint2(i, j); glEvalPoint2(i, j + 1); }
                glEnd();
            }
            break;
        case GL_LINE:
            for (int j = j1; j <= j2; j++)
            {
                glBegin(GL_LINE_STRIP);
                for (int i = i1; i <= i2; i++) glEvalPoint2(i, j);
                glEnd();
            }
            for (int i = i1; i <= i2; i++)
            {
                glBegin(GL_LINE_STRIP);
                for (int j = j1; j <= j2; j++) glEvalPoint2(i, j);
                glEnd();
            }
            break;
        default:
            glBegin(GL_POINTS);
            for (int j = j1; j <= j2; j++)
                for (int i = i1; i <= i2; i++) glEvalPoint2(i, j);
            glEnd();
            break;
    }
}

// glGetMap: GL_COEFF (control points), GL_ORDER, GL_DOMAIN; returns the count
static int getMap(GLenum target, GLenum query, double *out)
{
    bool twoD;
    const struct EvalMap *m = mapForTarget(target, &twoD);
    if (m == NULL) { setError(GL_INVALID_ENUM); return 0; }
    int k = mapComponents[m - (twoD? gl.map2 : gl.map1)];
    switch (query)
    {
        case GL_COEFF:
        {
            if (m->points == NULL) return 0;
            int n = m->uorder*m->vorder*k;
            for (int i = 0; i < n; i++) out[i] = m->points[i];
            return n;
        }
        case GL_ORDER:
            out[0] = m->points? m->uorder : 1;
            out[1] = m->points? m->vorder : 1;
            return twoD? 2 : 1;
        case GL_DOMAIN:
            out[0] = m->points? m->u1 : 0.0; out[1] = m->points? m->u2 : 1.0;
            out[2] = m->points? m->v1 : 0.0; out[3] = m->points? m->v2 : 1.0;
            return twoD? 4 : 2;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

static double mapValues[C3DGL_MAX_EVAL_ORDER*C3DGL_MAX_EVAL_ORDER*4];

void glGetMapdv(GLenum target, GLenum query, GLdouble *v)
{
    int n = getMap(target, query, mapValues);
    for (int i = 0; i < n; i++) v[i] = mapValues[i];
}

void glGetMapfv(GLenum target, GLenum query, GLfloat *v)
{
    int n = getMap(target, query, mapValues);
    for (int i = 0; i < n; i++) v[i] = (GLfloat)mapValues[i];
}

void glGetMapiv(GLenum target, GLenum query, GLint *v)
{
    int n = getMap(target, query, mapValues);
    for (int i = 0; i < n; i++) v[i] = (GLint)lround(mapValues[i]);
}

//----------------------------------------------------------------------------------
// OpenGL: attribute stacks (glPushAttrib, glPushClientAttrib)
//
// A push saves a snapshot of everything; a pop restores only the groups of the pushed mask (GL 1.1 tables
// 6.x). Groups of features that do not exist yet (stipple, pixel transfer, lists, accumulation)
// save nothing so far. When a feature moves out of ignoredCaps, its state has to be added here.
//----------------------------------------------------------------------------------
typedef struct {
    GLuint id;                  // Texture bound to the unit at push time
    GLenum minFilter, magFilter, wrapS, wrapT;
    bool generateMipmap;
} SavedTexParams;

typedef struct {
    GLbitfield mask;
    DrawState state;
    Vertex current;
    bool currentEdge;
    float currentNormal[3], currentTexR[C3DGL_TEXTURE_UNITS];
    float lineWidth, pointSize;
    GLenum shadeModel, polygonMode[2];
    bool offsetFill, offsetLine, offsetPoint;
    float offsetFactor, offsetUnits;
    u32 ignoredCaps, clearColor;
    float clearDepth;
    u8 clearStencil;
    bool texture2D[C3DGL_TEXTURE_UNITS];
    GLuint boundTexture[C3DGL_TEXTURE_UNITS];
    SavedTexParams texParams[C3DGL_TEXTURE_UNITS];
    int activeTexture, matrixMode;
    bool map1Enabled[9], map2Enabled[9], autoNormal;
    int grid1n, grid2un, grid2vn;
    float grid[6];              // grid1u1, grid1u2, grid2u1, grid2u2, grid2v1, grid2v2
    LightingState lighting;
    bool lightingEnabled, colorMaterial, normalize, rescaleNormal;
    u8 lightEnabled;
} AttribState;

typedef struct {
    GLbitfield mask;
    PixelStore unpack, pack;
    ClientArray arrays[ARRAY_COUNT];
    GLuint arrayBuffer, elementArrayBuffer;
    int clientActiveTexture;
} ClientAttribState;

static AttribState attribStack[C3DGL_ATTRIB_STACK];
static ClientAttribState clientAttribStack[C3DGL_ATTRIB_STACK];

// Bits of ignoredCaps (stored-only capabilities) that belong to an attribute group
static u32 capBits(const GLenum *caps, int count)
{
    u32 bits = 0;
    for (int i = 0; i < count; i++) bits |= 1u << ignoredCapBit(caps[i]);
    return bits;
}

void glPushAttrib(GLbitfield mask)
{
    if (gl.attribDepth == C3DGL_ATTRIB_STACK) { setError(GL_STACK_OVERFLOW); return; }

    AttribState *a = &attribStack[gl.attribDepth++];
    a->mask = mask;
    a->state = gl.state;
    a->current = gl.current;
    a->currentEdge = gl.currentEdge;
    memcpy(a->currentNormal, gl.currentNormal, sizeof(a->currentNormal));
    memcpy(a->currentTexR, gl.currentTexR, sizeof(a->currentTexR));
    a->lineWidth = gl.lineWidth;
    a->pointSize = gl.pointSize;
    a->shadeModel = gl.shadeModel;
    memcpy(a->polygonMode, gl.polygonMode, sizeof(a->polygonMode));
    a->offsetFill = gl.offsetFill;
    a->offsetLine = gl.offsetLine;
    a->offsetPoint = gl.offsetPoint;
    a->offsetFactor = gl.offsetFactor;
    a->offsetUnits = gl.offsetUnits;
    a->ignoredCaps = gl.ignoredCaps;
    a->clearColor = gl.clearColor;
    a->clearDepth = gl.clearDepth;
    a->clearStencil = gl.clearStencil;
    memcpy(a->texture2D, gl.texture2D, sizeof(a->texture2D));
    memcpy(a->boundTexture, gl.boundTexture, sizeof(a->boundTexture));
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        GLuint id = gl.boundTexture[unit];
        SavedTexParams *p = &a->texParams[unit];
        p->id = id;
        if ((id > 0) && (id < C3DGL_MAX_TEXTURES))
        {
            const Texture *t = &gl.textures[id];
            p->minFilter = t->minFilter;
            p->magFilter = t->magFilter;
            p->wrapS = t->wrapS;
            p->wrapT = t->wrapT;
            p->generateMipmap = t->generateMipmap;
        }
    }
    a->activeTexture = gl.activeTexture;
    a->matrixMode = gl.matrixMode;
    for (int i = 0; i < 9; i++) { a->map1Enabled[i] = gl.map1[i].enabled; a->map2Enabled[i] = gl.map2[i].enabled; }
    a->autoNormal = gl.autoNormal;
    a->grid1n = gl.grid1n;
    a->grid2un = gl.grid2un;
    a->grid2vn = gl.grid2vn;
    float grid[6] = { gl.grid1u1, gl.grid1u2, gl.grid2u1, gl.grid2u2, gl.grid2v1, gl.grid2v2 };
    memcpy(a->grid, grid, sizeof(grid));
    a->lighting = gl.lighting;
    a->lightingEnabled = gl.lightingEnabled;
    a->lightEnabled = gl.lightEnabled;
    a->colorMaterial = gl.colorMaterial;
    a->normalize = gl.normalize;
    a->rescaleNormal = gl.rescaleNormal;
}

void glPopAttrib(void)
{
    if (gl.attribDepth == 0) { setError(GL_STACK_UNDERFLOW); return; }

    const AttribState *a = &attribStack[--gl.attribDepth];
    GLbitfield mask = a->mask;
    DrawState *st = &gl.state;
    const DrawState *sv = &a->state;
    u32 caps = 0;   // ignoredCaps bits to restore

    if (mask & GL_CURRENT_BIT)
    {
        gl.current = a->current;
        gl.currentEdge = a->currentEdge;
        memcpy(gl.currentNormal, a->currentNormal, sizeof(gl.currentNormal));
        memcpy(gl.currentTexR, a->currentTexR, sizeof(gl.currentTexR));
    }
    if (mask & GL_POINT_BIT)
    {
        gl.pointSize = a->pointSize;
        caps |= capBits((const GLenum[]){ GL_POINT_SMOOTH }, 1);
    }
    if (mask & GL_LINE_BIT)
    {
        gl.lineWidth = a->lineWidth;
        caps |= capBits((const GLenum[]){ GL_LINE_SMOOTH }, 1);
    }
    if (mask & GL_POLYGON_BIT)
    {
        st->cull = sv->cull;
        st->cullFace = sv->cullFace;
        st->frontFace = sv->frontFace;
        memcpy(gl.polygonMode, a->polygonMode, sizeof(gl.polygonMode));
        gl.offsetFill = a->offsetFill;
        gl.offsetLine = a->offsetLine;
        gl.offsetPoint = a->offsetPoint;
        gl.offsetFactor = a->offsetFactor;
        gl.offsetUnits = a->offsetUnits;
        caps |= capBits((const GLenum[]){ GL_POLYGON_SMOOTH }, 1);
    }
    if (mask & GL_LIGHTING_BIT)
    {
        gl.shadeModel = a->shadeModel;
        gl.lighting = a->lighting;
        gl.lightingEnabled = a->lightingEnabled;
        gl.lightEnabled = a->lightEnabled;
        gl.colorMaterial = a->colorMaterial;
    }
    if (mask & GL_FOG_BIT) caps |= capBits((const GLenum[]){ GL_FOG }, 1);
    if (mask & GL_DEPTH_BUFFER_BIT)
    {
        st->depthTest = sv->depthTest;
        st->depthFunc = sv->depthFunc;
        st->depthMask = sv->depthMask;
        gl.clearDepth = a->clearDepth;
    }
    if (mask & GL_STENCIL_BUFFER_BIT)
    {
        st->stencilTest = sv->stencilTest;
        st->stencilFunc = sv->stencilFunc;
        st->stencilRef = sv->stencilRef;
        st->stencilFuncMask = sv->stencilFuncMask;
        st->stencilWriteMask = sv->stencilWriteMask;
        st->stencilFail = sv->stencilFail;
        st->stencilDepthFail = sv->stencilDepthFail;
        st->stencilPass = sv->stencilPass;
        gl.clearStencil = a->clearStencil;
    }
    if (mask & GL_VIEWPORT_BIT)
    {
        memcpy(st->viewport, sv->viewport, sizeof(st->viewport));
        st->depthNear = sv->depthNear;
        st->depthFar = sv->depthFar;
    }
    if (mask & GL_TRANSFORM_BIT)
    {
        gl.matrixMode = a->matrixMode;
        gl.normalize = a->normalize;
        gl.rescaleNormal = a->rescaleNormal;
    }
    if (mask & GL_ENABLE_BIT)
    {
        st->alphaTest = sv->alphaTest;
        st->blend = sv->blend;
        st->cull = sv->cull;
        st->depthTest = sv->depthTest;
        st->scissor = sv->scissor;
        st->stencilTest = sv->stencilTest;
        gl.offsetFill = a->offsetFill;
        gl.offsetLine = a->offsetLine;
        gl.offsetPoint = a->offsetPoint;
        memcpy(gl.texture2D, a->texture2D, sizeof(gl.texture2D));
        gl.lightingEnabled = a->lightingEnabled;
        gl.lightEnabled = a->lightEnabled;
        gl.colorMaterial = a->colorMaterial;
        gl.normalize = a->normalize;
        gl.rescaleNormal = a->rescaleNormal;
        caps = 0xFFFFFFFFu;     // All stored-only capabilities
    }
    if (mask & GL_COLOR_BUFFER_BIT)
    {
        st->alphaTest = sv->alphaTest;
        st->alphaFunc = sv->alphaFunc;
        st->alphaRef = sv->alphaRef;
        st->blend = sv->blend;
        st->blendSrc = sv->blendSrc;
        st->blendDst = sv->blendDst;
        st->colorMask = sv->colorMask;
        gl.clearColor = a->clearColor;
        caps |= capBits((const GLenum[]){ GL_DITHER }, 1);
    }
    if (mask & GL_TEXTURE_BIT)
    {
        // Enables, environments, bindings and the active unit, then the parameters of the textures bound at push time
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) st->units[unit].env = sv->units[unit].env;
        memcpy(gl.texture2D, a->texture2D, sizeof(gl.texture2D));
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
        {
            const SavedTexParams *p = &a->texParams[unit];
            gl.boundTexture[unit] = (p->id < C3DGL_MAX_TEXTURES) && gl.textures[p->id].used? p->id : 0;
            if ((p->id == 0) || (gl.boundTexture[unit] != p->id)) continue;

            Texture *t = &gl.textures[p->id];
            t->minFilter = p->minFilter;
            t->magFilter = p->magFilter;
            t->wrapS = p->wrapS;
            t->wrapT = p->wrapT;
            t->generateMipmap = p->generateMipmap;
            if (t->loaded)
            {
                textureModified(p->id);
                applyTextureParams(t);
            }
        }
        gl.activeTexture = a->activeTexture;
    }
    if (mask & (GL_EVAL_BIT | GL_ENABLE_BIT))
    {
        for (int i = 0; i < 9; i++) { gl.map1[i].enabled = a->map1Enabled[i]; gl.map2[i].enabled = a->map2Enabled[i]; }
        gl.autoNormal = a->autoNormal;
    }
    if (mask & GL_EVAL_BIT)
    {
        gl.grid1n = a->grid1n;
        gl.grid2un = a->grid2un;
        gl.grid2vn = a->grid2vn;
        gl.grid1u1 = a->grid[0]; gl.grid1u2 = a->grid[1];
        gl.grid2u1 = a->grid[2]; gl.grid2u2 = a->grid[3];
        gl.grid2v1 = a->grid[4]; gl.grid2v2 = a->grid[5];
    }
    if (mask & GL_SCISSOR_BIT)
    {
        st->scissor = sv->scissor;
        memcpy(st->scissorBox, sv->scissorBox, sizeof(st->scissorBox));
    }

    gl.ignoredCaps = (gl.ignoredCaps & ~caps) | (a->ignoredCaps & caps);
}

void glPushClientAttrib(GLbitfield mask)
{
    if (gl.clientAttribDepth == C3DGL_ATTRIB_STACK) { setError(GL_STACK_OVERFLOW); return; }

    ClientAttribState *a = &clientAttribStack[gl.clientAttribDepth++];
    a->mask = mask;
    a->unpack = gl.unpack;
    a->pack = gl.pack;
    memcpy(a->arrays, gl.arrays, sizeof(a->arrays));
    a->arrayBuffer = gl.arrayBuffer;
    a->elementArrayBuffer = gl.elementArrayBuffer;
    a->clientActiveTexture = gl.clientActiveTexture;
}

void glPopClientAttrib(void)
{
    if (gl.clientAttribDepth == 0) { setError(GL_STACK_UNDERFLOW); return; }

    const ClientAttribState *a = &clientAttribStack[--gl.clientAttribDepth];
    if (a->mask & GL_CLIENT_PIXEL_STORE_BIT)
    {
        gl.unpack = a->unpack;
        gl.pack = a->pack;
    }
    if (a->mask & GL_CLIENT_VERTEX_ARRAY_BIT)
    {
        // Buffers deleted since the push are not restored (their arrays are cleared, like glDeleteBuffers does)
        memcpy(gl.arrays, a->arrays, sizeof(gl.arrays));
        for (int i = 0; i < ARRAY_COUNT; i++)
        {
            if ((gl.arrays[i].buffer == 0) || bufferValid(gl.arrays[i].buffer)) continue;
            gl.arrays[i].buffer = 0;
            gl.arrays[i].pointer = NULL;
        }
        gl.arrayBuffer = bufferValid(a->arrayBuffer)? a->arrayBuffer : 0;
        gl.elementArrayBuffer = bufferValid(a->elementArrayBuffer)? a->elementArrayBuffer : 0;
        gl.clientActiveTexture = a->clientActiveTexture;
    }
}

//----------------------------------------------------------------------------------
// Not implemented yet (declared so that code like GLU links; see gl.h)
//----------------------------------------------------------------------------------
#define NOT_IMPLEMENTED(name) do { WARN_ONCE(name " not implemented yet\n"); setError(GL_INVALID_OPERATION); } while (0)

void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)target; (void)level; (void)internalformat; (void)width; (void)border; (void)format; (void)type; (void)pixels;
    NOT_IMPLEMENTED("glTexImage1D");
}



// GL 1.2: no 3D textures
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)target; (void)level; (void)internalformat; (void)width; (void)height; (void)depth; (void)border;
    (void)format; (void)type; (void)pixels;
    setError(GL_INVALID_ENUM);
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    (void)x; (void)y; (void)format; (void)type;
    WARN_ONCE("glReadPixels not supported, returning black\n");
    memset(pixels, 0, (size_t)width*height*4);
}

//----------------------------------------------------------------------------------
// OpenGL ES 1.1: float variants of the double functions and the fixed-point (16.16) API.
// Enum-valued parameters are passed unscaled in the x functions, like in ES
//----------------------------------------------------------------------------------
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) { glOrtho(left, right, bottom, top, zNear, zFar); }
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) { glFrustum(left, right, bottom, top, zNear, zFar); }
void glDepthRangef(GLclampf zNear, GLclampf zFar) { glDepthRange(zNear, zFar); }
void glClearDepthf(GLclampf depth) { glClearDepth(depth); }

static float fixedToFloat(GLfixed x) { return x/65536.0f; }

static GLfixed floatToFixed(double f)
{
    double v = f*65536.0;
    if (v >= 2147483647.0) return 0x7FFFFFFF;
    if (v <= -2147483648.0) return (GLfixed)0x80000000;
    return (GLfixed)lround(v);
}

static bool fixedParamIsEnum(GLenum pname)
{
    return (pname == GL_TEXTURE_ENV_MODE) || (pname == GL_TEXTURE_MIN_FILTER) || (pname == GL_TEXTURE_MAG_FILTER) ||
           (pname == GL_TEXTURE_WRAP_S) || (pname == GL_TEXTURE_WRAP_T) || (pname == GL_GENERATE_MIPMAP) ||
           ((pname >= GL_COMBINE_RGB) && (pname <= GL_COMBINE_ALPHA)) || ((pname >= GL_SRC0_RGB) && (pname <= GL_OPERAND2_ALPHA));
}

void glAlphaFuncx(GLenum func, GLclampx ref) { glAlphaFunc(func, fixedToFloat(ref)); }
void glClearColorx(GLclampx red, GLclampx green, GLclampx blue, GLclampx alpha) { glClearColor(fixedToFloat(red), fixedToFloat(green), fixedToFloat(blue), fixedToFloat(alpha)); }
void glClearDepthx(GLclampx depth) { glClearDepth(fixedToFloat(depth)); }
void glColor4x(GLfixed red, GLfixed green, GLfixed blue, GLfixed alpha) { glColor4f(fixedToFloat(red), fixedToFloat(green), fixedToFloat(blue), fixedToFloat(alpha)); }
void glDepthRangex(GLclampx zNear, GLclampx zFar) { glDepthRange(fixedToFloat(zNear), fixedToFloat(zFar)); }
void glLineWidthx(GLfixed width) { glLineWidth(fixedToFloat(width)); }
void glNormal3x(GLfixed nx, GLfixed ny, GLfixed nz) { glNormal3f(fixedToFloat(nx), fixedToFloat(ny), fixedToFloat(nz)); }
void glPointSizex(GLfixed size) { glPointSize(fixedToFloat(size)); }
void glPolygonOffsetx(GLfixed factor, GLfixed units) { glPolygonOffset(fixedToFloat(factor), fixedToFloat(units)); }
void glRotatex(GLfixed angle, GLfixed x, GLfixed y, GLfixed z) { glRotatef(fixedToFloat(angle), fixedToFloat(x), fixedToFloat(y), fixedToFloat(z)); }
void glScalex(GLfixed x, GLfixed y, GLfixed z) { glScalef(fixedToFloat(x), fixedToFloat(y), fixedToFloat(z)); }
void glTranslatex(GLfixed x, GLfixed y, GLfixed z) { glTranslatef(fixedToFloat(x), fixedToFloat(y), fixedToFloat(z)); }

void glOrthox(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar)
{
    glOrtho(fixedToFloat(left), fixedToFloat(right), fixedToFloat(bottom), fixedToFloat(top), fixedToFloat(zNear), fixedToFloat(zFar));
}

void glFrustumx(GLfixed left, GLfixed right, GLfixed bottom, GLfixed top, GLfixed zNear, GLfixed zFar)
{
    glFrustum(fixedToFloat(left), fixedToFloat(right), fixedToFloat(bottom), fixedToFloat(top), fixedToFloat(zNear), fixedToFloat(zFar));
}

void glLoadMatrixx(const GLfixed *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = fixedToFloat(m[i]);
    glLoadMatrixf(f);
}

void glMultMatrixx(const GLfixed *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = fixedToFloat(m[i]);
    glMultMatrixf(f);
}

void glTexEnvx(GLenum target, GLenum pname, GLfixed param) { glTexEnvi(target, pname, fixedParamIsEnum(pname)? param : (GLint)fixedToFloat(param)); }

void glTexEnvxv(GLenum target, GLenum pname, const GLfixed *params)
{
    if (pname == GL_TEXTURE_ENV_COLOR)
    {
        GLfloat color[4];
        for (int i = 0; i < 4; i++) color[i] = fixedToFloat(params[i]);
        glTexEnvfv(target, pname, color);
    }
    else glTexEnvx(target, pname, params[0]);
}

void glMultiTexCoord4x(GLenum target, GLfixed s, GLfixed t, GLfixed r, GLfixed q)
{
    glMultiTexCoord4f(target, fixedToFloat(s), fixedToFloat(t), fixedToFloat(r), fixedToFloat(q));
}

void glGetTexEnvxv(GLenum target, GLenum pname, GLfixed *params)
{
    float v[4];
    int n = getTexEnv(target, pname, v);
    bool isEnum = (pname != GL_TEXTURE_ENV_COLOR) && (pname != GL_RGB_SCALE) && (pname != GL_ALPHA_SCALE);
    for (int i = 0; i < n; i++) params[i] = isEnum? (GLfixed)v[i] : floatToFixed(v[i]);
}

void glTexParameterx(GLenum target, GLenum pname, GLfixed param) { glTexParameteri(target, pname, fixedParamIsEnum(pname)? param : (GLint)fixedToFloat(param)); }
void glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params) { glTexParameterx(target, pname, params[0]); }

void glGetTexParameterxv(GLenum target, GLenum pname, GLfixed *params)
{
    float v[4];
    int n = getTexParameter(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = (GLfixed)v[i];     // All values are enums or booleans
}

void glLightx(GLenum light, GLenum pname, GLfixed param) { glLightf(light, pname, fixedToFloat(param)); }
void glLightModelx(GLenum pname, GLfixed param) { glLightModelf(pname, fixedToFloat(param)); }
void glMaterialx(GLenum face, GLenum pname, GLfixed param) { glMaterialf(face, pname, fixedToFloat(param)); }

void glLightxv(GLenum light, GLenum pname, const GLfixed *params)
{
    int n = lightParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    for (int i = 0; i < n; i++) f[i] = fixedToFloat(params[i]);
    setLight(light, pname, f);
}

void glLightModelxv(GLenum pname, const GLfixed *params)
{
    int n = lightModelParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    for (int i = 0; i < n; i++) f[i] = fixedToFloat(params[i]);
    setLightModel(pname, f);
}

void glMaterialxv(GLenum face, GLenum pname, const GLfixed *params)
{
    int n = materialParamCount(pname);
    if (n == 0) { setError(GL_INVALID_ENUM); return; }
    float f[4];
    for (int i = 0; i < n; i++) f[i] = fixedToFloat(params[i]);
    setMaterial(face, pname, f);
}

void glGetLightxv(GLenum light, GLenum pname, GLfixed *params)
{
    float v[4];
    bool color;
    int n = getLight(light, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}

void glGetMaterialxv(GLenum face, GLenum pname, GLfixed *params)
{
    float v[4];
    bool color;
    int n = getMaterial(face, pname, v, &color);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}

void glGetFixedv(GLenum pname, GLfixed *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}
