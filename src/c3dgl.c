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
//     Paletted textures are expanded on load; ETC1 blocks are stored as they are, upside down (see uploadEtc1()).
//     Images of any format/type are converted to the PICA format closest to their internal format (see loadTexels());
//     1D textures are 2D textures whose rows all hold the image.
//   - One render target per screen, sharing the vertex buffer and GL state; c3dglSetScreen() flushes the
//     batch and switches the target (C3D_FrameDrawOn), citro3d presents every target drawn on in the frame.
//
//   - Fog uses PICA's fog unit, which looks the fog factor up in a 128 entry table indexed by window depth. GL's factor
//     depends on the eye distance, so the table inverts the projection per entry (see updateFogLut()).
//   - Depth and stencil share one D24S8 buffer that a memory fill can only clear as a whole. glClear uses the
//     fill when that is equivalent, otherwise it draws a full-screen quad (scissor, masks, depth or stencil only).
//   - glDrawPixels/glBitmap draw textured quads in window coordinates; their textures live in per-frame linear memory,
//     bitmaps share an atlas (see drawBitmap()). glCopyPixels copies the color buffer into such a texture with a GX texture
//     copy queued between the draws (copyColorRect()). Depth/stencil images are written on the CPU (drawDepthStencil()).
//   - Feedback and selection (glRenderMode) take the primitives after user clipping, culling and polygon mode, clip them
//     against the view volume on the CPU and write tokens or hit records instead of drawing (see the feedback section).
//   - Display lists record the commands with their arguments (client data like pixels, control points and vertex
//     arrays copied at compile time) and replay them through the same gl* entry points, see listSave().
//
// Known limitations: REPEAT wrap on non-power-of-two textures samples the padding.
#include "GL/gl.h"
#include "c3dgl.h"

#include <3ds.h>
#include <citro3d.h>

#include <math.h>
#include <stdarg.h>
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
#define DEFAULT_TEXTURE_2D      C3DGL_MAX_TEXTURES          // Slots of the default textures (bound as texture 0)
#define DEFAULT_TEXTURE_1D      (C3DGL_MAX_TEXTURES + 1)
#define TEXTURE_SLOTS           (C3DGL_MAX_TEXTURES + 2)
#define C3DGL_MATRIX_STACK      32
#define C3DGL_TEXTURE_UNITS     3           // PICA texture units 0..2 (unit 3 is procedural only)
#define C3DGL_ATTRIB_STACK      16          // glPushAttrib / glPushClientAttrib depth (GL minimum)
#define C3DGL_MAX_EVAL_ORDER    30          // Evaluator order (GL minimum 8)
#define C3DGL_MAX_LIST_NESTING  64          // glCallList depth (GL minimum 64)
#define C3DGL_MAX_LIGHTS        8
#define C3DGL_MAX_CLIP_PLANES   6           // User clip planes, clipped on the CPU (GL minimum 6, ES 1)
#define C3DGL_MAX_TEXTURE_SIZE  1024
#define C3DGL_MAX_PIXEL_MAP_TABLE 256       // Entries of a glPixelMap table (GL minimum 32)
#define C3DGL_MAX_NAME_STACK    64          // Selection name stack depth (GL minimum 64)
#define C3DGL_MAX_POINT_SIZE    256.0f      // Points are quads, so any size works; a bit more than the screen height
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
    float pointSize;            // From the point size array (GL_OES_point_size_array), < 0: glPointSize; not sent
    float texR;                 // Unit 0 r, only for feedback (0 with texgen on unit 0); not sent
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
    bool compressed;            // ETC1 (bpp 0): 4x4 blocks stored with t = 0 at the top, see uploadEtc1()
} TexFormat;

// glPixelStore state for one direction (unpack: GL -> c3dgl, pack: c3dgl -> GL)
typedef struct {
    GLint alignment, rowLength, skipRows, skipPixels, imageHeight, skipImages;
    bool swapBytes, lsbFirst;
} PixelStore;

// Result of a glTexImage1D/2D on GL_PROXY_TEXTURE_1D/2D (all zero if the image would not fit)
typedef struct {
    GLint width, height, border, internalFormat;
    GLenum base;
    GPU_TEXCOLOR format;
} ProxyLevel;

// One mipmap level as GL sees it (also levels below 8x8, which PICA cannot store)
typedef struct {
    bool defined;
    int width, height;          // Without border
    int border;
    GLint internalFormat;
    GLenum base;                // Base internal format: GL_ALPHA, GL_LUMINANCE(_ALPHA), GL_INTENSITY, GL_RGB, GL_RGBA
    GPU_TEXCOLOR format;
} TexLevel;

// A 1D texture is stored as a 2D texture with every row holding the image (see replicateRows()), sampled with t = s
// (see applyTextureMatrix()). It is 8 rows high, as high as it is wide once mipmapped, so that its levels go down to 8
// texels in s
typedef struct {
    bool used;                  // Id handed out by glGenTextures or bound
    GLenum target;              // GL_TEXTURE_1D or GL_TEXTURE_2D once bound, 0 before
    bool loaded;                // tex is initialized (level 0 defined)
    C3D_Tex tex;                // Level 0 padded to power-of-two; mip chain down to 8x8 once a level > 0 arrives
    int levels;                 // Levels stored in tex (1 + tex.maxLevel when mipmapped)
    TexFormat format;
    GLenum base;                // Base internal format of level 0, decides how the texels are sampled (intensity)
    int width, height;          // Level 0 image size without border (height 1 for 1D)
    TexLevel level[MAX_TEXTURE_LEVEL + 1];
    bool complete;              // All levels down to 1x1 defined and consistent (needed by mipmap filters)
    bool generateMipmap;        // GL_GENERATE_MIPMAP
    GLenum minFilter, magFilter, wrapS, wrapT;
    float priority, borderColor[4];     // Stored only
} Texture;

// Texture environment of one unit (glTexEnv)
typedef struct {
    GLenum mode;                // GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND, GL_ADD, GL_COMBINE
    u32 color;                  // GL_TEXTURE_ENV_COLOR, 0xAABBGGRR like the PICA
    GLenum combineRgb, combineAlpha;
    GLenum srcRgb[3], srcAlpha[3], operandRgb[3], operandAlpha[3];
    u8 rgbScale, alphaScale;    // 1, 2, 4
} TexEnvState;

// Texture coordinate generation of one unit (glTexGen), coordinates s, t, r, q; see generateTexCoords()
typedef struct {
    u8 enabled;                 // GL_TEXTURE_GEN_S..Q, bit per coordinate
    GLenum mode[4];
    float objectPlane[4][4];
    float eyePlane[4][4];       // Eye coordinates (transformed by the inverse modelview of the glTexGen call)
} TexGenState;

// What generateTexCoords() does per vertex for one unit, derived from the texgen state, modelview and texture matrix:
// (s, t, q) = gen * (x, y, z, 1) + pass * (s, t, q of the vertex) + sphere * (sphere map s, t)
typedef struct {
    float gen[3][4];
    float pass[3][3];
    float sphere[3][2];
    bool usesPass, usesSphere;
} TexGenTransform;

typedef struct {
    GLuint texture;             // Slot in gl.textures, 0: unit not used (set in prepareDraw)
    TexEnvState env;            // Zeroed for unused units, so they don't split batches
} TexUnitState;

// Everything that decides how a range of vertices is rendered; see prepareDraw()
typedef struct {
    TexUnitState units[C3DGL_TEXTURE_UNITS];
    bool clipSpace;             // Vertices are already in NDC (expanded lines and points)
    u32 matrixSerial;           // Matrix version (0 for clipSpace)
    u32 texMatrixSerial;        // Texture matrix version (0 when untextured or only sprite units are textured)
    u8 spriteUnits;             // Point sprites: units with GL_COORD_REPLACE_OES, their texture matrix is not applied
    u8 texGenUnits;             // Units with texgen: their texcoords come with the texture matrix applied (on the CPU)
    bool texQ;                  // Unit 0 texcoords with q != 1 were used (projection mode), only when textured
    bool blend;
    GLenum blendSrc, blendDst;
    bool logicOp;               // GL_COLOR_LOGIC_OP, replaces blending
    GLenum logicOpMode;
    bool depthTest, depthMask;
    GLenum depthFunc;
    float depthNear, depthFar;  // glDepthRange
    bool alphaTest;
    GLenum alphaFunc;
    u8 alphaRef;                // 0..255
    bool fog;                   // Fog parameters are only set when fog is on (filled in by prepareDraw)
    GLenum fogMode;
    float fogDensity, fogStart, fogEnd;
    u32 fogColor;               // 0x00BBGGRR like the PICA
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
    u8 pixelMode;               // Pixel rectangle (glDrawPixels, glBitmap) with pixelTex on unit 0, see PixelMode
    const C3D_Tex *pixelTex;
} DrawState;

// How a pixel rectangle's texture becomes the fragment color, see drawPixelRect()
typedef enum {
    PIXEL_NONE,
    PIXEL_IMAGE,                // RGBA image: the texel
    PIXEL_BITMAP,               // A8 bitmap: raster color, alpha = raster alpha * texel alpha (alpha test > 0)
    PIXEL_BITMAP_ZERO,          // The same for raster alpha 0: alpha = 1 - texel alpha (alpha test == 0)
} PixelMode;

// Raster position (GL 1.1 section 2.12) in window coordinates, with its associated data
typedef struct {
    float pos[4];               // x, y, z (window), w (clip)
    bool valid;
    u8 color[4];
    float tex[4];               // Unit 0, texture matrix applied
    float distance;             // Eye distance
} RasterState;

// glPixelTransfer (GL 1.1 section 3.6.3), GL_PIXEL_MODE_BIT
typedef struct {
    bool mapColor, mapStencil;
    GLint indexShift, indexOffset;
    float scale[5], bias[5];    // R, G, B, A, depth
} PixelTransfer;

static const PixelTransfer noTransfer = { .scale = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f } };

// glPixelMap tables in GL_PIXEL_MAP_I_TO_I .. GL_PIXEL_MAP_A_TO_A order (not in an attribute group)
enum { PIXEL_MAP_I_TO_I, PIXEL_MAP_S_TO_S, PIXEL_MAP_I_TO_R, PIXEL_MAP_R_TO_R = PIXEL_MAP_I_TO_R + 4, PIXEL_MAP_COUNT = PIXEL_MAP_R_TO_R + 4 };

typedef struct {
    int size;
    float values[C3DGL_MAX_PIXEL_MAP_TABLE];    // Color maps: clamped to [0, 1]; index maps: as given
} PixelMap;

typedef struct {
    bool enabled;
    const void *pointer;        // Offset into `buffer` if that is not 0
    GLuint buffer;              // GL_ARRAY_BUFFER binding when the pointer was set
    GLint size;
    GLenum type;
    GLsizei stride;
} ClientArray;

// A polygon's vertices with edge flags (edges[i]: edge from vertex i to i + 1), scratch list of clipPolygon()
typedef struct {
    Vertex *verts;
    bool *edges;
    int capacity;
} PolygonList;

// Buffer object (VBO). Kept in normal memory: vertices are converted into the per-frame vertex buffer anyway
typedef struct {
    bool used;                  // Id handed out by glGenBuffers or created by glBindBuffer
    u8 *data;
    GLsizeiptr size;
    GLenum usage;
} Buffer;

enum { ARRAY_VERTEX, ARRAY_TEXCOORD0, ARRAY_TEXCOORD1, ARRAY_TEXCOORD2, ARRAY_COLOR, ARRAY_NORMAL, ARRAY_EDGEFLAG, ARRAY_POINTSIZE, ARRAY_COUNT };

// Texcoord array of the client active unit (glClientActiveTexture)
#define ARRAY_TEXCOORD      (ARRAY_TEXCOORD0 + gl.clientActiveTexture)

// Display lists: a list is a sequence of commands, each a header word (command | word count << 8, the header included)
// followed by its arguments
typedef union {
    GLfloat f;
    GLint i;
    GLuint u;
} ListWord;

typedef struct {
    GLuint name;
    ListWord *words;            // NULL for an empty list (glGenLists)
    int count;
} DisplayList;

// The commands that are compiled into display lists and how one is executed: w points to its arguments, as written by
// listSave() (doubles take two words, see listDouble()). Commands that GL executes immediately (glGet*, client state,
// glPixelStore, glGen*/glDelete*, glReadPixels, glFlush, ...) are not in here
#define LIST_COMMANDS(X) \
    X(ENABLE,           setCapability(w[0].u, w[1].i != 0)) \
    X(SHADE_MODEL,      glShadeModel(w[0].u)) \
    X(VIEWPORT,         glViewport(w[0].i, w[1].i, w[2].i, w[3].i)) \
    X(SCISSOR,          glScissor(w[0].i, w[1].i, w[2].i, w[3].i)) \
    X(CLEAR_COLOR,      glClearColor(w[0].f, w[1].f, w[2].f, w[3].f)) \
    X(CLEAR_DEPTH,      glClearDepth(listDouble(&w[0]))) \
    X(CLEAR_STENCIL,    glClearStencil(w[0].i)) \
    X(CLEAR,            glClear(w[0].u)) \
    X(COLOR_MASK,       glColorMask(w[0].i, w[1].i, w[2].i, w[3].i)) \
    X(DEPTH_MASK,       glDepthMask(w[0].i)) \
    X(DEPTH_FUNC,       glDepthFunc(w[0].u)) \
    X(STENCIL_FUNC,     glStencilFunc(w[0].u, w[1].i, w[2].u)) \
    X(STENCIL_OP,       glStencilOp(w[0].u, w[1].u, w[2].u)) \
    X(STENCIL_MASK,     glStencilMask(w[0].u)) \
    X(ALPHA_FUNC,       glAlphaFunc(w[0].u, w[1].f)) \
    X(BLEND_FUNC,       glBlendFunc(w[0].u, w[1].u)) \
    X(LOGIC_OP,         glLogicOp(w[0].u)) \
    X(SAMPLE_COVERAGE,  glSampleCoverage(w[0].f, w[1].i)) \
    X(CULL_FACE,        glCullFace(w[0].u)) \
    X(FRONT_FACE,       glFrontFace(w[0].u)) \
    X(POLYGON_MODE,     glPolygonMode(w[0].u, w[1].u)) \
    X(POLYGON_OFFSET,   glPolygonOffset(w[0].f, w[1].f)) \
    X(DEPTH_RANGE,      glDepthRange(listDouble(&w[0]), listDouble(&w[2]))) \
    X(LINE_WIDTH,       glLineWidth(w[0].f)) \
    X(POINT_SIZE,       glPointSize(w[0].f)) \
    X(POINT_PARAMETER,  glPointParameterfv(w[0].u, &w[1].f)) \
    X(MATRIX_MODE,      glMatrixMode(w[0].u)) \
    X(PUSH_MATRIX,      glPushMatrix()) \
    X(POP_MATRIX,       glPopMatrix()) \
    X(LOAD_IDENTITY,    glLoadIdentity()) \
    X(LOAD_MATRIX,      glLoadMatrixf(&w[0].f)) \
    X(MULT_MATRIX,      glMultMatrixf(&w[0].f)) \
    X(TRANSLATE,        glTranslatef(w[0].f, w[1].f, w[2].f)) \
    X(ROTATE,           glRotatef(w[0].f, w[1].f, w[2].f, w[3].f)) \
    X(SCALE,            glScalef(w[0].f, w[1].f, w[2].f)) \
    X(ORTHO,            glOrtho(listDouble(&w[0]), listDouble(&w[2]), listDouble(&w[4]), listDouble(&w[6]), \
                                listDouble(&w[8]), listDouble(&w[10]))) \
    X(FRUSTUM,          glFrustum(listDouble(&w[0]), listDouble(&w[2]), listDouble(&w[4]), listDouble(&w[6]), \
                                  listDouble(&w[8]), listDouble(&w[10]))) \
    X(BEGIN,            glBegin(w[0].u)) \
    X(END,              glEnd()) \
    X(VERTEX,           glVertex3f(w[0].f, w[1].f, w[2].f)) \
    X(TEX_COORD,        glTexCoord4f(w[0].f, w[1].f, w[2].f, w[3].f)) \
    X(MULTI_TEX_COORD,  glMultiTexCoord4f(w[0].u, w[1].f, w[2].f, w[3].f, w[4].f)) \
    X(EDGE_FLAG,        glEdgeFlag(w[0].i)) \
    X(NORMAL,           glNormal3f(w[0].f, w[1].f, w[2].f)) \
    X(COLOR,            glColor4ub(w[0].i, w[1].i, w[2].i, w[3].i)) \
    X(LIGHT,            setLight(w[0].u, w[1].u, &w[2].f)) \
    X(LIGHT_MODEL,      setLightModel(w[0].u, &w[1].f)) \
    X(MATERIAL,         setMaterial(w[0].u, w[1].u, &w[2].f)) \
    X(COLOR_MATERIAL,   glColorMaterial(w[0].u, w[1].u)) \
    X(FOG,              setFog(w[0].u, &w[1].f)) \
    X(TEX_GEN,          setTexGen(w[0].u, w[1].u, &w[3].f, w[2].i != 0)) \
    X(CLIP_PLANE,       setClipPlane(w[0].u, (const double[4]){ listDouble(&w[1]), listDouble(&w[3]), \
                                                                listDouble(&w[5]), listDouble(&w[7]) })) \
    X(BIND_TEXTURE,     glBindTexture(w[0].u, w[1].u)) \
    X(TEX_ENV,          setTexEnv(w[0].u, w[1].u, w[2].i, w[3].f)) \
    X(TEX_ENV_COLOR,    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, &w[0].f)) \
    X(ACTIVE_TEXTURE,   glActiveTexture(w[0].u)) \
    X(TEX_PARAMETER,    setTexParameter(w[0].u, w[1].u, &w[2].f)) \
    X(PRIORITIZE_TEXTURES, glPrioritizeTextures(w[0].i, &w[1].u, &w[1 + ((w[0].i > 0)? w[0].i : 0)].f)) \
    X(TEX_IMAGE,        listTexImage(w, false)) \
    X(TEX_SUB_IMAGE,    listTexImage(w, true)) \
    X(COMPRESSED_TEX_IMAGE, glCompressedTexImage2D(w[0].u, w[1].i, w[2].u, w[3].i, w[4].i, w[5].i, w[6].i, \
                                                   w[7].i? &w[8] : NULL)) \
    X(COMPRESSED_TEX_SUB_IMAGE, glCompressedTexSubImage2D(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i, w[5].i, w[6].u, \
                                                          w[7].i, NULL)) \
    X(COPY_TEX_IMAGE,   copyTexImage(w[0].u, w[1].i, w[2].u, w[3].i, w[4].i, w[5].i, w[6].i, w[7].i, w[8].i != 0)) \
    X(COPY_TEX_SUB_IMAGE, copyTexSubImage(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i, w[5].i, w[6].i, w[7].i, w[8].i != 0)) \
    X(DRAW_BUFFER,      glDrawBuffer(w[0].u)) \
    X(READ_BUFFER,      glReadBuffer(w[0].u)) \
    X(RASTER_POS,       setRasterPos(w[0].f, w[1].f, w[2].f, w[3].f)) \
    X(BITMAP,           listPixels(w, true)) \
    X(DRAW_PIXELS,      listPixels(w, false)) \
    X(COPY_PIXELS,      glCopyPixels(w[0].i, w[1].i, w[2].i, w[3].i, w[4].u)) \
    X(PIXEL_ZOOM,       glPixelZoom(w[0].f, w[1].f)) \
    X(PIXEL_TRANSFER,   glPixelTransferf(w[0].u, w[1].f)) \
    X(PIXEL_MAP,        setPixelMap(w[0].u, w[1].i, &w[2].f)) \
    X(MAP,              defineMap(w[0].u, listDouble(&w[2]), listDouble(&w[4]), w[6].i, w[7].i, listDouble(&w[8]), \
                                  listDouble(&w[10]), w[12].i, w[13].i, &w[14].f, false, w[1].i != 0)) \
    X(MAP_GRID1,        glMapGrid1f(w[0].i, w[1].f, w[2].f)) \
    X(MAP_GRID2,        glMapGrid2f(w[0].i, w[1].f, w[2].f, w[3].i, w[4].f, w[5].f)) \
    X(EVAL_COORD1,      glEvalCoord1f(w[0].f)) \
    X(EVAL_COORD2,      glEvalCoord2f(w[0].f, w[1].f)) \
    X(EVAL_POINT1,      glEvalPoint1(w[0].i)) \
    X(EVAL_POINT2,      glEvalPoint2(w[0].i, w[1].i)) \
    X(EVAL_MESH1,       glEvalMesh1(w[0].u, w[1].i, w[2].i)) \
    X(EVAL_MESH2,       glEvalMesh2(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i)) \
    X(PUSH_ATTRIB,      glPushAttrib(w[0].u)) \
    X(POP_ATTRIB,       glPopAttrib()) \
    X(CALL_LIST,        glCallList(w[0].u)) \
    X(CALL_LISTS,       glCallLists(w[0].i, w[1].u, &w[2])) \
    X(LIST_BASE,        glListBase(w[0].u)) \
    X(PASS_THROUGH,     glPassThrough(w[0].f)) \
    X(INIT_NAMES,       glInitNames()) \
    X(LOAD_NAME,        glLoadName(w[0].u)) \
    X(PUSH_NAME,        glPushName(w[0].u)) \
    X(POP_NAME,         glPopName())

typedef enum {
    #define X(name, call) LIST_##name,
    LIST_COMMANDS(X)
    #undef X
} ListCommand;

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
    int cacheFlushed;                   // Vertices before this are flushed from the CPU cache, see submitFrame()
    bool extraUsed;                     // Vertex buffer 1 was written since the last cache flush
    DrawState batch;                    // State applied to the GPU for the current batch
    bool batchValid;

    // GL state as set by the gl* calls
    DrawState state;
    bool texture1D[C3DGL_TEXTURE_UNITS], texture2D[C3DGL_TEXTURE_UNITS];
    GLuint boundTexture1D[C3DGL_TEXTURE_UNITS], boundTexture[C3DGL_TEXTURE_UNITS];
    TexGenState texGen[C3DGL_TEXTURE_UNITS];
    u32 texGenSerial;                   // Incremented on every texgen state change
    TexGenTransform texGenTransform[C3DGL_TEXTURE_UNITS];   // For texGenTransformSerials, see updateTexGenTransforms()
    u32 texGenTransformSerials[3];      // matrixSerial, texMatrixSerial, texGenSerial; all 0: stale
    int activeTexture, clientActiveTexture;     // glActiveTexture, glClientActiveTexture: 0..2
    PixelStore unpack, pack;
    ProxyLevel proxy1D[11], proxy2D[11];    // Per level, 1024 >> 10 = 1
    GLenum drawBuffer, readBuffer;      // glDrawBuffer, glReadBuffer: GL_NONE, GL_BACK, ...; all but GL_NONE are the frame
    RasterState raster;                 // glRasterPos
    float zoomX, zoomY;                 // glPixelZoom
    PixelTransfer transfer;             // glPixelTransfer
    PixelMap pixelMaps[PIXEL_MAP_COUNT];    // glPixelMap
    struct PixelChunk {                 // Linear memory for the textures of pixel rectangles drawn this frame
        u8 *data;
        size_t size, used, flushed;     // Bytes [flushed, used) are not flushed from the CPU cache yet
    } *pixelChunks;
    int pixelChunkCount;
    C3D_Tex *atlas;                     // Bitmap atlas being filled, NULL: none (see atlasSlot())
    int atlasX, atlasY, atlasRowHeight;
    float lineWidth, pointSize;
    float pointSizeMin, pointSizeMax, pointFadeThreshold, pointAttenuation[3];  // glPointParameter
    bool pointSprite;                   // GL_POINT_SPRITE_OES
    u8 coordReplace;                    // GL_COORD_REPLACE_OES per texture unit (bit n: unit n)
    u32 clearColor;                     // 0xRRGGBBAA
    float clearDepth;
    u8 clearStencil;
    bool stencilUsed;                   // GL_STENCIL_TEST was enabled once: glClear must preserve stencil values
    GLenum error;                       // First error since the last glGetError()
    u32 ignoredCaps;                    // Capabilities accepted but not implemented, see ignoredCapBit()
    float sampleCoverage;               // glSampleCoverage (no effect without sample buffers)
    bool sampleCoverageInvert;
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
    void *evalGrid;                     // EvalVertex scratch grid of glEvalMesh2
    int evalGridCapacity;

    // Fog (glFog); the PICA table is rebuilt only when its inputs change, see updateFogLut()
    bool fog;
    GLenum fogMode;
    float fogDensity, fogStart, fogEnd, fogColor[4], fogIndex;
    C3D_FogLut fogLut;
    float fogLutInputs[10];
    bool fogLutValid;

    // User clip planes (glClipPlane), see clipPolygon()
    float clipPlanes[C3DGL_MAX_CLIP_PLANES][4];     // Eye coordinates
    u8 clipEnabled;                     // Bit per plane
    float clipObject[C3DGL_MAX_CLIP_PLANES][4];     // Enabled planes in object coordinates, for clipObjectSerial
    int clipObjectCount;
    u32 clipObjectSerial;               // matrixSerial of clipObject, 0: stale
    PolygonList clipLists[2];
    const Vertex **clipPtrs;
    int clipPtrCapacity;

    // Lighting, see lightVertex()
    LightingState lighting;
    bool lightingEnabled, colorMaterial, normalize, rescaleNormal;
    u8 lightEnabled;                    // Bit per light
    float normalMatrix[9];              // Inverse transpose of the modelview's upper 3x3 (row-major), for normalSerial
    float normalRescale;                // GL_RESCALE_NORMAL factor
    u32 normalSerial;
    u32 litCacheGen;                    // Valid entries of litCache, see lightVertexCached()
    u32 litCacheSerial;                 // matrixSerial the entries were lit with

    Texture textures[TEXTURE_SLOTS];    // Index: texture id, or DEFAULT_TEXTURE_1D/2D for texture 0 of a target

    // Textures deleted during a frame are freed once the GPU is done with that frame
    C3D_Tex *deferredDeletes;
    int deferredCount, deferredCapacity;

    // Display lists (GL), see listSave()
    DisplayList *lists;                 // Sorted by name
    int listCount, listCapacity;
    GLuint listBase;                    // glListBase
    GLuint listName;                    // List being compiled (glNewList), 0: none
    GLenum listMode;                    // Its mode: GL_COMPILE or GL_COMPILE_AND_EXECUTE
    bool listCompiling;                 // gl* calls are recorded instead of executed (off while one is executed)
    ListWord *listWords;                // Commands recorded so far
    int listWordCount, listWordCapacity;
    int listLast;                       // Index of the last recorded command, -1 if it was dropped (out of memory)
    int listDepth;                      // Nesting of lists being executed

    // Feedback and selection (glRenderMode), see the feedback section
    GLenum renderMode;                  // GL_RENDER, GL_FEEDBACK or GL_SELECT
    GLfloat *feedbackBuffer;
    GLsizei feedbackSize;
    GLenum feedbackType;
    bool feedbackBufferSet;             // glFeedbackBuffer was called
    int feedbackCount;                  // Values written; size + 1: overflow
    bool lineReset;                     // The next line starts a strip, loop or outline: GL_LINE_RESET_TOKEN
    GLuint *selectBuffer;
    GLsizei selectSize;
    bool selectBufferSet;               // glSelectBuffer was called
    int selectCount;                    // Values written; size + 1: overflow
    int hits;                           // Hit records written
    bool hit;                           // A primitive was hit since the last hit record
    float hitMinZ, hitMaxZ;             // Window depth range of those hits
    GLuint names[C3DGL_MAX_NAME_STACK];
    int nameDepth;
} gl;

// Display list recording, see the display list section
static void listSave(ListCommand command, const char *format, ...);
static ListWord *listBegin(ListCommand command, int words);
static void listEnd(void);
static void listSaveImage(ListCommand command, const GLint args[8], GLsizei width, GLsizei height, bool sizeValid,
                          bool oneD, const void *pixels);
static void listArrayElement(int index);
static void listPixels(const ListWord *w, bool bitmap);
static void setRasterPos(float x, float y, float z, float w);
static void setPixelMap(GLenum map, GLsizei mapsize, const GLfloat *values);
static void initTexture(Texture *t);

// While a display list is compiled: record the command instead of executing it, and return from the gl* function
#define LIST_SAVE(command, ...) do { if (gl.listCompiling) { listSave(LIST_##command, __VA_ARGS__); return; } } while (0)

// Lighting state changed: lit colors cached by lightVertexCached() are stale
static void litStateChanged(void) { gl.litCacheGen++; }

// Record an error for glGetError(); like OpenGL, only the first one is kept until it is read
static void setError(GLenum error)
{
    if (gl.error == GL_NO_ERROR) gl.error = error;
}

static u8 colorByte(float c)
{
    if (c <= 0.0f) return 0;
    if (c >= 1.0f) return 255;
    return (u8)(c*255.0f + 0.5f);
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

// General inverse (cofactors); false and identity if m is singular
static bool mat4Invert(const Mat4 *m, Mat4 *out)
{
    const float *a = m->m;
    float inv[16];
    inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
    inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
    inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
    inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
    inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
    inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
    inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
    inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
    inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
    inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
    inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
    inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
    inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
    inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
    inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
    inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];

    float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
    if (det == 0.0f) { mat4Identity(out); return false; }
    for (int i = 0; i < 16; i++) out->m[i] = inv[i]/det;
    return true;
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
// GL_CLEAR + n -> PICA logic op
static GPU_LOGICOP logicOp(GLenum op)
{
    static const GPU_LOGICOP ops[16] = {
        GPU_LOGICOP_CLEAR, GPU_LOGICOP_AND, GPU_LOGICOP_AND_REVERSE, GPU_LOGICOP_COPY,
        GPU_LOGICOP_AND_INVERTED, GPU_LOGICOP_NOOP, GPU_LOGICOP_XOR, GPU_LOGICOP_OR,
        GPU_LOGICOP_NOR, GPU_LOGICOP_EQUIV, GPU_LOGICOP_INVERT, GPU_LOGICOP_OR_REVERSE,
        GPU_LOGICOP_COPY_INVERTED, GPU_LOGICOP_OR_INVERTED, GPU_LOGICOP_NAND, GPU_LOGICOP_SET,
    };
    return ops[(op - GL_CLEAR) & 15];
}

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
            case GL_RGBA: *out = (TexFormat){ GPU_RGBA8, 4, true, false, false }; return true;
            case GL_RGB: *out = (TexFormat){ GPU_RGB8, 3, true, false, false }; return true;
            case GL_LUMINANCE_ALPHA: *out = (TexFormat){ GPU_LA8, 2, true, false, false }; return true;
            case GL_LUMINANCE: *out = (TexFormat){ GPU_L8, 1, false, false, false }; return true;
            case GL_ALPHA: *out = (TexFormat){ GPU_A8, 1, false, false, false }; return true;
            default: return false;
        }
    }

    // Packed 16-bit formats have the same bit layout on PICA
    if ((format == GL_RGB) && (type == GL_UNSIGNED_SHORT_5_6_5)) { *out = (TexFormat){ GPU_RGB565, 2, false, true, false }; return true; }
    if ((format == GL_RGBA) && (type == GL_UNSIGNED_SHORT_5_5_5_1)) { *out = (TexFormat){ GPU_RGBA5551, 2, false, true, false }; return true; }
    if ((format == GL_RGBA) && (type == GL_UNSIGNED_SHORT_4_4_4_4)) { *out = (TexFormat){ GPU_RGBA4, 2, false, true, false }; return true; }

    return false;
}

// glCompressedTexImage2D formats (GL_COMPRESSED_TEXTURE_FORMATS): the 10 paletted formats in enum order, then ETC1
#define COMPRESSED_FORMAT_COUNT 11
static const GLenum compressedFormats[COMPRESSED_FORMAT_COUNT] = {
    GL_PALETTE4_RGB8_OES, GL_PALETTE4_RGBA8_OES, GL_PALETTE4_R5_G6_B5_OES, GL_PALETTE4_RGBA4_OES, GL_PALETTE4_RGB5_A1_OES,
    GL_PALETTE8_RGB8_OES, GL_PALETTE8_RGBA8_OES, GL_PALETTE8_R5_G6_B5_OES, GL_PALETTE8_RGBA4_OES, GL_PALETTE8_RGB5_A1_OES,
    GL_ETC1_RGB8_OES,
};

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
    gl.cacheFlushed = 0;
    gl.extraUsed = false;
    gl.batchValid = false;

    processDeferredDeletes();

    // The GPU is done with the pixel rectangles of the previous frame; one chunk is kept for the next ones
    for (int i = 1; i < gl.pixelChunkCount; i++) linearFree(gl.pixelChunks[i].data);
    if (gl.pixelChunkCount > 1) gl.pixelChunkCount = 1;
    if (gl.pixelChunkCount) gl.pixelChunks[0].used = gl.pixelChunks[0].flushed = 0;
    gl.atlas = NULL;
}

// Linear memory for a texture of a pixel rectangle, valid until the GPU finished the frame (128-byte aligned)
#define PIXEL_CHUNK_SIZE    (512*1024)

static u8 *allocPixelMemory(size_t size)
{
    ensureFrame();
    size = (size + 127) & ~(size_t)127;
    struct PixelChunk *c = gl.pixelChunkCount? &gl.pixelChunks[gl.pixelChunkCount - 1] : NULL;
    if ((c == NULL) || (c->used + size > c->size))
    {
        struct PixelChunk *chunks = realloc(gl.pixelChunks, (gl.pixelChunkCount + 1)*sizeof(*chunks));
        if (chunks == NULL) { setError(GL_OUT_OF_MEMORY); return NULL; }
        gl.pixelChunks = chunks;
        c = &chunks[gl.pixelChunkCount];
        c->size = (size > PIXEL_CHUNK_SIZE)? size : PIXEL_CHUNK_SIZE;
        c->data = linearAlloc(c->size);
        if (c->data == NULL) { LOG("Out of memory for drawing pixels\n"); setError(GL_OUT_OF_MEMORY); return NULL; }
        c->used = c->flushed = 0;
        gl.pixelChunkCount++;
    }
    u8 *p = c->data + c->used;
    c->used += size;
    return p;
}

// Submit the vertices collected since the last flush with the currently applied state
static void flush(void)
{
    int count = gl.vertexCount - gl.batchStart;
    if (count <= 0) return;

    C3D_DrawArrays(GPU_TRIANGLES, gl.batchStart, count);
    if (gl.batch.units[1].texture || gl.batch.units[2].texture) gl.extraUsed = true;

    gl.batchStart = gl.vertexCount;
    gl.drawnThisFrame = true;
}

// Before the command list goes to the GPU (C3D_FrameSplit, C3D_FrameEnd): the GPU only reads the vertex buffer
// from then on, so the vertices written since the last submission are flushed from the CPU cache in one go
// (instead of a GSP call per batch)
static void flushVertexCache(void)
{
    flush();
    for (int i = 0; i < gl.pixelChunkCount; i++)
    {
        struct PixelChunk *c = &gl.pixelChunks[i];
        if (c->used > c->flushed) GSPGPU_FlushDataCache(c->data + c->flushed, c->used - c->flushed);
        c->flushed = c->used;
    }
    gl.atlas = NULL;

    int count = gl.vertexCount - gl.cacheFlushed;
    if (count <= 0) return;

    GSPGPU_FlushDataCache(gl.vbo + (size_t)gl.cacheFlushed*GPU_VERTEX_SIZE, count*GPU_VERTEX_SIZE);
    if (gl.extraUsed) GSPGPU_FlushDataCache(&gl.vboExtra[gl.cacheFlushed], count*GPU_EXTRA_SIZE);
    gl.cacheFlushed = gl.vertexCount;
    gl.extraUsed = false;
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

// intensity: an intensity texture (stored as LA8 with L = A = I), which differs from luminance alpha in GL_BLEND and
// GL_ADD alpha
static void setupTexEnv(C3D_TexEnv *env, int unit, const TexEnvState *e, GPU_TEXCOLOR format, bool intensity)
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

    // Alpha: REPLACE takes At, DECAL keeps Af, everything else is Af*At (At = 1 without alpha); intensity textures
    // blend (Af*(1 - It) + Ac*It) and add (Af + It) the alpha like the color
    if (intensity && (mode == GL_BLEND))
    {
        C3D_TexEnvSrc(env, C3D_Alpha, GPU_CONSTANT, prev, tex);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_INTERPOLATE);
    }
    else if (intensity && (mode == GL_ADD))
    {
        C3D_TexEnvSrc(env, C3D_Alpha, tex, prev, prev);
        C3D_TexEnvFunc(env, C3D_Alpha, GPU_ADD);
    }
    else if ((mode == GL_DECAL) || ((mode == GL_REPLACE) && !hasAlpha))
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

// Texture matrix of `unit` as shader uniform rows s, t, q; s and t scaled from the image to the padded texture size.
// identity: the texcoords need no texture matrix (point sprite coordinates, which GL does not transform, or generated
// texcoords, transformed on the CPU already)
static void applyTextureMatrix(int unit, const Texture *t, bool identity, bool *projective)
{
    static const Mat4 identityMatrix = {{ 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1 }};
    const Mat4 *tm = identity? &identityMatrix : &gl.stack[2 + unit][gl.stackDepth[2 + unit]];
    float scale[2] = { (float)t->width/t->tex.width, (float)t->height/t->tex.height };
    for (int row = 0; row < 2; row++)
    {
        float r[4];
        for (int i = 0; i < 4; i++) r[i] = tm->m[4*i + row]*scale[row];

        // 1D: t' = s' (all rows hold the image, so t has no effect). Not a fixed t: the mipmap level follows the t
        // derivative (a fixed t always samples level 0 in Azahar), and with t' = s' it equals the s derivative, as the
        // mip chain is square
        if ((row == 1) && (t->target == GL_TEXTURE_1D)) for (int i = 0; i < 4; i++) r[i] = tm->m[4*i]*scale[0];

        // Compressed textures are stored upside down (t = 0 at the top): t' = q - t, which also holds for projective t
        if ((row == 1) && t->format.compressed) for (int i = 0; i < 4; i++) r[i] = tm->m[4*i + 3] - r[i];
        C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[unit] + row, r[0], r[1], r[2], r[3]);
    }
    C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[unit] + 2, tm->m[3], tm->m[7], tm->m[11], tm->m[15]);

    // q != 1 (r is always 0, so m[11] does not matter)
    *projective = (tm->m[3] != 0.0f) || (tm->m[7] != 0.0f) || (tm->m[15] != 1.0f);
}

// GL fog factor for eye distance c (clamped, 1 = no fog)
static float fogFactor(const DrawState *s, float c)
{
    float f;
    if (s->fogMode == GL_LINEAR) f = (s->fogEnd != s->fogStart)? (s->fogEnd - c)/(s->fogEnd - s->fogStart) : 1.0f;
    else if (s->fogMode == GL_EXP2) f = expf(-(s->fogDensity*c)*(s->fogDensity*c));
    else f = expf(-s->fogDensity*c);
    return (f < 0.0f)? 0.0f : (f > 1.0f)? 1.0f : f;
}

// PICA fog: factor = table[window depth*128], linearly interpolated between entries. Each entry goes back from window
// depth through NDC z to the eye z with the projection's z and w rows (z_ndc = (a*z + b)/(c*z + d), exact for
// glFrustum/glOrtho style projections); the fog distance is |z_eye| like most GL implementations
static void updateFogLut(const DrawState *s)
{
    const float *m = gl.stack[1][gl.stackDepth[1]].m;
    float a = m[10], b = m[14], c = m[11], d = m[15];
    float in[10] = { a, b, c, d, s->depthNear, s->depthFar, (float)s->fogMode, s->fogDensity, s->fogStart, s->fogEnd };
    if (gl.fogLutValid && (memcmp(in, gl.fogLutInputs, sizeof(in)) == 0)) return;
    memcpy(gl.fogLutInputs, in, sizeof(in));
    gl.fogLutValid = true;

    // data[0..127]: factor at the entry, data[128..255]: difference to the next entry
    float data[256], range = s->depthFar - s->depthNear;
    for (int i = 0; i <= 128; i++)
    {
        float zNdc = (range != 0.0f)? 2.0f*(i/128.0f - s->depthNear)/range - 1.0f : 0.0f;
        float den = c*zNdc - a;
        float zEye = (fabsf(den) > 1e-12f)? (b - d*zNdc)/den : 1e30f;
        float f = fogFactor(s, fabsf(zEye));
        if (i < 128) data[i] = f;
        if (i > 0) data[127 + i] = f - data[i - 1];
    }
    FogLut_FromArray(&gl.fogLut, data);
    C3D_FogLutBind(&gl.fogLut);     // Marks the table dirty: citro3d copies it into the command list at the next draw
}

// Set the GPU state of a batch. prev: the state applied for the previous batch (NULL: unknown), only what differs
// from it is set: citro3d re-sends every group that is set, changed or not
#define CHANGED(field) ((prev == NULL) || (memcmp(&s->field, &prev->field, sizeof(s->field)) != 0))

static void applyState(const DrawState *s, const DrawState *prev)
{
    int x, y, w, h;
    if (CHANGED(viewport))
    {
        physicalRect(s->viewport, &x, &y, &w, &h);
        C3D_SetViewport(x, y, w, h);
    }

    if (CHANGED(scissor) || (s->scissor && CHANGED(scissorBox)))
    {
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
    }

    // Stored depth = 1 - window depth (see depthFunc()), window depth = n + (f - n)*(z_pica + 1) with z_pica in [-1, 0]
    if (CHANGED(depthNear) || CHANGED(depthFar)) C3D_DepthMap(true, -(s->depthFar - s->depthNear), 1.0f - s->depthFar);

    if (CHANGED(colorMask) || CHANGED(depthTest) || CHANGED(depthMask) || CHANGED(depthFunc))
    {
        GPU_WRITEMASK writeMask = (GPU_WRITEMASK)(s->colorMask | ((s->depthTest && s->depthMask)? GPU_WRITE_DEPTH : 0));
        C3D_DepthTest(s->depthTest, s->depthTest? depthFunc(s->depthFunc) : GPU_ALWAYS, writeMask);
    }
    if (CHANGED(alphaTest) || CHANGED(alphaFunc) || CHANGED(alphaRef)) C3D_AlphaTest(s->alphaTest, testFunc(s->alphaFunc), s->alphaRef);

    // prepareDraw() zeroes the stencil fields while the test is off
    if (CHANGED(stencilTest) || CHANGED(stencilFunc) || CHANGED(stencilRef) || CHANGED(stencilFuncMask) ||
        CHANGED(stencilWriteMask) || CHANGED(stencilFail) || CHANGED(stencilDepthFail) || CHANGED(stencilPass))
    {
        if (s->stencilTest)
        {
            C3D_StencilTest(true, testFunc(s->stencilFunc), s->stencilRef, s->stencilFuncMask, s->stencilWriteMask);
            C3D_StencilOp(stencilOp(s->stencilFail), stencilOp(s->stencilDepthFail), stencilOp(s->stencilPass));
        }
        else C3D_StencilTest(false, GPU_ALWAYS, 0, 0xFF, 0x00);
    }

    // PICA does either blending or a logic op; prepareDraw() zeroes the fields of the one not in use
    if (CHANGED(blend) || CHANGED(blendSrc) || CHANGED(blendDst) || CHANGED(logicOp) || CHANGED(logicOpMode))
    {
        if (s->logicOp) C3D_ColorLogicOp(logicOp(s->logicOpMode));
        else if (s->blend)
        {
            GPU_BLENDFACTOR src = blendFactor(s->blendSrc), dst = blendFactor(s->blendDst);
            C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, src, dst, src, dst);
        }
        else C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
    }

    if ((prev == NULL) || (cullMode(s) != cullMode(prev))) C3D_CullFace(cullMode(s));

    // The fog table also depends on the projection, updateFogLut() rebuilds it only when its inputs change
    if (s->fog) updateFogLut(s);
    if (CHANGED(fog) || CHANGED(fogColor))
    {
        if (s->fog)
        {
            C3D_FogGasMode(GPU_FOG, GPU_PLAIN_DENSITY, true);   // Flipped: the table is indexed by window depth
            C3D_FogColor(s->fogColor);
        }
        else C3D_FogGasMode(GPU_NO_FOG, GPU_PLAIN_DENSITY, false);
    }

    // Fragment stage: TexEnv stage n combines texture unit n with the result of stage n - 1 (glTexEnv per unit).
    // Pixel rectangles have all units unused but set stage 0 themselves
    bool pixelChanged = CHANGED(pixelMode) || CHANGED(pixelTex);
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        const TexUnitState *u = &s->units[unit];
        bool sprite = (s->spriteUnits >> unit) & 1, texGen = (s->texGenUnits >> unit) & 1;
        if ((unit == 0) && s->pixelMode)
        {
            if (!pixelChanged) continue;
            C3D_TexEnv *env = C3D_GetTexEnv(0);
            C3D_TexEnvInit(env);
            if (s->pixelMode == PIXEL_IMAGE)
            {
                C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
            }
            else
            {
                C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
                C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
                if (s->pixelMode == PIXEL_BITMAP) C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
                else
                {
                    C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
                    C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
                }
            }
            C3D_TexBind(0, (C3D_Tex *)s->pixelTex);
            C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[0] + 0, 1.0f, 0.0f, 0.0f, 0.0f);
            C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[0] + 1, 0.0f, 1.0f, 0.0f, 0.0f);
            C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[0] + 2, 0.0f, 0.0f, 0.0f, 1.0f);
            continue;
        }
        if ((prev != NULL) && !((unit == 0) && pixelChanged) && (memcmp(u, &prev->units[unit], sizeof(*u)) == 0) &&
            ((u->texture == 0) || ((s->texMatrixSerial == prev->texMatrixSerial) && (s->texQ == prev->texQ) &&
                                   (sprite == ((prev->spriteUnits >> unit) & 1)) &&
                                   (texGen == ((prev->texGenUnits >> unit) & 1)))))
            continue;

        C3D_TexEnv *env = C3D_GetTexEnv(unit);
        C3D_TexEnvInit(env);
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
        applyTextureMatrix(unit, t, sprite || texGen, &projective);

        // Unit 0 can let PICA divide s and t by q per pixel (projection mode); units 1/2 divide per vertex in the shader
        if (unit == 0)
        {
            projective = projective || s->texQ;
            t->tex.param = (t->tex.param & ~GPU_TEXTURE_MODE(7)) | GPU_TEXTURE_MODE(projective? GPU_TEX_PROJECTION : GPU_TEX_2D);
        }
        else t->tex.param = (t->tex.param & ~GPU_TEXTURE_MODE(7)) | GPU_TEXTURE_MODE(GPU_TEX_2D);

        C3D_TexBind(unit, &t->tex);
        setupTexEnv(env, unit, &u->env, t->format.format, t->base == GL_INTENSITY);
    }

    if (CHANGED(clipSpace) || CHANGED(matrixSerial))
    {
        Mat4 mvp = gl.post;
        if (!s->clipSpace) mat4Mul(&mvp, &gl.post, projectionModelview());
        C3D_Mtx mtx;
        mat4ToC3D(&mvp, &mtx);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, gl.uLocMvp, &mtx);
    }
}

#undef CHANGED

static bool textureValid(GLuint slot)
{
    return (slot > 0) && (slot < TEXTURE_SLOTS) && gl.textures[slot].loaded;
}

// Slot in gl.textures of a texture bound to target: its id, texture 0 is the target's default texture
static GLuint textureSlot(GLenum target, GLuint id)
{
    return id? id : (target == GL_TEXTURE_1D)? DEFAULT_TEXTURE_1D : DEFAULT_TEXTURE_2D;
}

// Start a new batch if key differs from the applied state. memcmp: keys are built with zeroed padding
static void useState(const DrawState *key)
{
    ensureFrame();

    if (!gl.batchValid || (memcmp(key, &gl.batch, sizeof(DrawState)) != 0))
    {
        flush();
        applyState(key, gl.batchValid? &gl.batch : NULL);
        memcpy(&gl.batch, key, sizeof(DrawState));
        gl.batchValid = true;
    }
}

// The draw state of the vertices that follow. points: points follow (GL_POINTS, polygon mode GL_POINT), point sprites
// may replace their texcoords
static void drawKey(DrawState *out, bool clipSpace, bool points)
{
    DrawState key;
    memcpy(&key, &gl.state, sizeof(DrawState));
    key.clipSpace = clipSpace;
    key.matrixSerial = clipSpace? 0 : gl.matrixSerial;
    u8 textured = 0;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        TexUnitState *u = &key.units[unit];
        // GL_TEXTURE_2D takes precedence over GL_TEXTURE_1D
        GLuint slot = gl.texture2D[unit]? textureSlot(GL_TEXTURE_2D, gl.boundTexture[unit]) :
                      gl.texture1D[unit]? textureSlot(GL_TEXTURE_1D, gl.boundTexture1D[unit]) : 0;
        u->texture = textureValid(slot)? slot : 0;
        if (u->texture && mipmapFilter(gl.textures[u->texture].minFilter) && !gl.textures[u->texture].complete)
        {
            // GL: a mipmap filter without all levels disables the unit
            WARN_ONCE("Texture %u has a mipmap min filter but not all mipmap levels: texturing disabled "
                      "(set GL_TEXTURE_MIN_FILTER to GL_LINEAR or GL_NEAREST?)\n", u->texture);
            u->texture = 0;
        }
        if (u->texture == 0) memset(&u->env, 0, sizeof(u->env));      // Unused, don't split batches over it
        if (u->texture) textured |= 1u << unit;
    }
    key.spriteUnits = (points && gl.pointSprite)? (gl.coordReplace & textured) : 0;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) if (gl.texGen[unit].enabled) key.texGenUnits |= 1u << unit;
    key.texGenUnits &= textured & ~key.spriteUnits;
    key.texMatrixSerial = (textured & ~key.spriteUnits & ~key.texGenUnits)? gl.texMatrixSerial : 0;

    // Generated texcoords may have any q (eye linear q, projective texture matrices): unit 0 in projection mode
    key.texQ = (key.units[0].texture != 0) && (gl.texQUsed || (key.texGenUnits & 1)) && !(key.spriteUnits & 1);
    key.fog = gl.fog;
    if (gl.fog)
    {
        key.fogMode = gl.fogMode;
        key.fogDensity = gl.fogDensity;
        key.fogStart = gl.fogStart;
        key.fogEnd = gl.fogEnd;
        key.fogColor = colorByte(gl.fogColor[0]) | (colorByte(gl.fogColor[1]) << 8) | (colorByte(gl.fogColor[2]) << 16);
    }
    if (key.logicOp) { key.blend = false; key.blendSrc = key.blendDst = 0; }
    else key.logicOpMode = 0;
    if (gl.drawBuffer == GL_NONE) key.colorMask = 0;
    if (!key.stencilTest)
    {
        key.stencilFunc = key.stencilFail = key.stencilDepthFail = key.stencilPass = 0;
        key.stencilRef = key.stencilFuncMask = key.stencilWriteMask = 0;
    }
    *out = key;
}

// Call before emitting vertices: starts a new batch if the draw state changed
static void prepareDraw(bool clipSpace, bool points)
{
    DrawState key;
    drawKey(&key, clipSpace, points);
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
    for (int i = 0; i < 3; i++) out->pos[i] = a->pos[i] + (b->pos[i] - a->pos[i])*t;
    out->depthBias = a->depthBias + (b->depthBias - a->depthBias)*t;
    for (int i = 0; i < 3; i++) out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i])*t;
    for (int u = 0; u < C3DGL_TEXTURE_UNITS - 1; u++)
        for (int i = 0; i < 3; i++) out->texExtra[u][i] = a->texExtra[u][i] + (b->texExtra[u][i] - a->texExtra[u][i])*t;
    for (int i = 0; i < 4; i++) out->color[i] = (u8)(a->color[i] + ((float)b->color[i] - a->color[i])*t);
    for (int i = 0; i < 4; i++) out->backColor[i] = (u8)(a->backColor[i] + ((float)b->backColor[i] - a->backColor[i])*t);
    out->pointSize = a->pointSize + (b->pointSize - a->pointSize)*t;
    out->texR = a->texR + (b->texR - a->texR)*t;
}

//----------------------------------------------------------------------------------
// User clip planes (glClipPlane): clipped on the CPU in object space, before lines and points are expanded and
// before polygons are filled, outlined or drawn as vertices. Vertices reach this point untransformed, so the
// eye space planes go to object space with the modelview (p_obj = p_eye * M); attributes are interpolated linearly
// like GL does in clip space (object -> clip space is linear)
//----------------------------------------------------------------------------------
// The enabled planes in object coordinates, cached per modelview
static int objectClipPlanes(void)
{
    if (gl.clipObjectSerial == gl.matrixSerial) return gl.clipObjectCount;
    gl.clipObjectSerial = gl.matrixSerial;

    const float *m = gl.stack[0][gl.stackDepth[0]].m;
    int count = 0;
    for (int i = 0; i < C3DGL_MAX_CLIP_PLANES; i++)
    {
        if (!(gl.clipEnabled & (1u << i))) continue;
        const float *p = gl.clipPlanes[i];
        for (int c = 0; c < 4; c++) gl.clipObject[count][c] = p[0]*m[c*4] + p[1]*m[c*4 + 1] + p[2]*m[c*4 + 2] + p[3]*m[c*4 + 3];
        count++;
    }
    gl.clipObjectCount = count;
    return count;
}

static float clipDistance(const float plane[4], const Vertex *v)
{
    return plane[0]*v->pos[0] + plane[1]*v->pos[1] + plane[2]*v->pos[2] + plane[3];
}

// Points are kept or dropped whole
static bool pointClipped(const Vertex *v)
{
    int count = gl.clipEnabled? objectClipPlanes() : 0;
    for (int i = 0; i < count; i++) if (clipDistance(gl.clipObject[i], v) < 0.0f) return true;
    return false;
}

// Clip the segment *a -> *b; clipped ends are written to va/vb and *a/*b point there. False: nothing is left
static bool clipSegment(const Vertex **a, const Vertex **b, Vertex *va, Vertex *vb)
{
    int count = gl.clipEnabled? objectClipPlanes() : 0;
    float t0 = 0.0f, t1 = 1.0f;
    for (int i = 0; i < count; i++)
    {
        float da = clipDistance(gl.clipObject[i], *a), db = clipDistance(gl.clipObject[i], *b);
        if ((da < 0.0f) && (db < 0.0f)) return false;
        if (da < 0.0f) t0 = fmaxf(t0, da/(da - db));
        else if (db < 0.0f) t1 = fminf(t1, da/(da - db));
    }
    if (t0 > t1) return false;
    if ((t0 == 0.0f) && (t1 == 1.0f)) return true;

    const Vertex *oa = *a, *ob = *b;
    *va = *oa;
    *vb = *ob;
    if (t0 > 0.0f) lerpVertex(va, oa, ob, t0);
    if (t1 < 1.0f) lerpVertex(vb, oa, ob, t1);
    *a = va;
    *b = vb;
    return true;
}

static bool reservePolygonList(PolygonList *l, int count)
{
    if (count <= l->capacity) return true;

    Vertex *verts = realloc(l->verts, count*sizeof(Vertex));
    if (verts != NULL) l->verts = verts;
    bool *edges = realloc(l->edges, count*sizeof(bool));
    if (edges != NULL) l->edges = edges;
    if ((verts == NULL) || (edges == NULL)) { setError(GL_OUT_OF_MEMORY); return false; }
    l->capacity = count;
    return true;
}

// Sutherland-Hodgman against the enabled planes. On return *vs/*edges point to the clipped polygon (unchanged if
// it is completely inside), the result is its vertex count (< 3: nothing left). Parts of the original edges keep
// their edge flags, edges along a clip plane are not drawn by glPolygonMode outlines
static int clipPolygon(const Vertex *const **vs, const bool **edges, int n)
{
    int count = gl.clipEnabled? objectClipPlanes() : 0;
    bool inside = true;
    for (int p = 0; (p < count) && inside; p++)
        for (int i = 0; i < n; i++) if (clipDistance(gl.clipObject[p], (*vs)[i]) < 0.0f) { inside = false; break; }
    if (inside) return n;

    PolygonList *in = &gl.clipLists[0], *out = &gl.clipLists[1];
    if (!reservePolygonList(in, n)) return 0;
    for (int i = 0; i < n; i++) { in->verts[i] = *(*vs)[i]; in->edges[i] = (*edges)[i]; }

    for (int p = 0; (p < count) && (n >= 3); p++)
    {
        // Every edge adds at most two vertices
        if (!reservePolygonList(out, 2*n)) return 0;
        const float *plane = gl.clipObject[p];
        int m = 0;
        float da = clipDistance(plane, &in->verts[0]);
        for (int i = 0; i < n; i++)
        {
            const Vertex *a = &in->verts[i], *b = &in->verts[(i + 1) % n];
            float db = clipDistance(plane, b);
            if (da >= 0.0f) { out->verts[m] = *a; out->edges[m++] = in->edges[i]; }
            if ((da >= 0.0f) != (db >= 0.0f))
            {
                out->verts[m] = *a;
                lerpVertex(&out->verts[m], a, b, da/(da - db));
                out->edges[m++] = (da < 0.0f) && in->edges[i];     // Leaving: the next edge runs along the plane
            }
            da = db;
        }
        n = m;
        PolygonList *t = in; in = out; out = t;
    }
    if (n < 3) return 0;

    if (n > gl.clipPtrCapacity)
    {
        const Vertex **ptrs = realloc(gl.clipPtrs, n*sizeof(Vertex *));
        if (ptrs == NULL) { setError(GL_OUT_OF_MEMORY); return 0; }
        gl.clipPtrs = ptrs;
        gl.clipPtrCapacity = n;
    }
    for (int i = 0; i < n; i++) gl.clipPtrs[i] = &in->verts[i];
    *vs = gl.clipPtrs;
    *edges = in->edges;
    return n;
}

#define CLIP_W_MIN  1e-5f       // Lines and points are clipped against w > CLIP_W_MIN before the divide

//----------------------------------------------------------------------------------
// Feedback and selection (GL 1.1 sections 5.2, 5.3): in GL_FEEDBACK and GL_SELECT render mode nothing is drawn. Points,
// lines and polygons (after user clip planes, culling and polygon mode) are clipped against the view volume in clip
// space here and either written to the feedback buffer as tokens with their vertices in window coordinates, or
// recorded as a hit with their window depth range. glRasterPos hits, glBitmap/glDrawPixels/glCopyPixels feed back the
// raster position
//----------------------------------------------------------------------------------
// A vertex as feedback sees it: clip coordinates, color, texcoords of unit 0 with the texture matrix applied
typedef struct {
    float clip[4], color[4], tex[4];
} FeedbackVertex;

static void feedbackVertexOf(const Vertex *v, FeedbackVertex *out)
{
    mat4Transform(projectionModelview(), v->pos, out->clip);
    for (int i = 0; i < 4; i++) out->color[i] = v->color[i]*(1.0f/255.0f);
    float tc[4] = { v->tex[0], v->tex[1], v->texR, v->tex[2] };
    if (gl.texGen[0].enabled) { memcpy(out->tex, tc, sizeof(tc)); return; }    // Generated: matrix applied already

    const float *tm = gl.stack[2][gl.stackDepth[2]].m;
    for (int r = 0; r < 4; r++) out->tex[r] = tm[r]*tc[0] + tm[4 + r]*tc[1] + tm[8 + r]*tc[2] + tm[12 + r]*tc[3];
}

static void lerpFeedbackVertex(FeedbackVertex *out, const FeedbackVertex *a, const FeedbackVertex *b, float t)
{
    for (int i = 0; i < 4; i++)
    {
        out->clip[i] = a->clip[i] + (b->clip[i] - a->clip[i])*t;
        out->color[i] = a->color[i] + (b->color[i] - a->color[i])*t;
        out->tex[i] = a->tex[i] + (b->tex[i] - a->tex[i])*t;
    }
}

// Distance to view volume plane 0..5 (-w <= x, x <= w, y, z), >= 0 inside
static float viewPlaneDistance(const float c[4], int plane)
{
    return (plane & 1)? (c[3] - c[plane >> 1]) : (c[3] + c[plane >> 1]);
}

static void windowCoords(const float clip[4], float out[3])
{
    const GLint *vp = gl.state.viewport;
    float n = gl.state.depthNear, f = gl.state.depthFar;
    out[0] = vp[0] + 0.5f*(clip[0]/clip[3] + 1.0f)*vp[2];
    out[1] = vp[1] + 0.5f*(clip[1]/clip[3] + 1.0f)*vp[3];
    out[2] = n + 0.5f*(clip[2]/clip[3] + 1.0f)*(f - n);
}

static void feedbackValue(GLfloat value)
{
    if (gl.feedbackCount < gl.feedbackSize) gl.feedbackBuffer[gl.feedbackCount] = value;
    if (gl.feedbackCount <= gl.feedbackSize) gl.feedbackCount++;
}

// One vertex in the layout of the feedback type: window x, y, z, clip w, color, texcoords
static void feedbackWrite(const float win[3], float w, const float color[4], const float tex[4])
{
    GLenum type = gl.feedbackType;
    feedbackValue(win[0]);
    feedbackValue(win[1]);
    if (type != GL_2D) feedbackValue(win[2]);
    if (type == GL_4D_COLOR_TEXTURE) feedbackValue(w);
    if ((type == GL_2D) || (type == GL_3D)) return;
    for (int i = 0; i < 4; i++) feedbackValue(color[i]);
    if (type != GL_3D_COLOR) for (int i = 0; i < 4; i++) feedbackValue(tex[i]);
}

static void selectHit(float z)
{
    gl.hit = true;
    gl.hitMinZ = fminf(gl.hitMinZ, z);
    gl.hitMaxZ = fmaxf(gl.hitMaxZ, z);
}

// A clipped primitive: token (and vertex count for polygons) with its vertices, or a hit
static void feedbackPrimitive(GLenum token, const FeedbackVertex *vs, int n)
{
    if (gl.renderMode == GL_SELECT)
    {
        for (int i = 0; i < n; i++)
        {
            float win[3];
            windowCoords(vs[i].clip, win);
            selectHit(win[2]);
        }
        return;
    }

    feedbackValue((GLfloat)token);
    if (token == GL_POLYGON_TOKEN) feedbackValue((GLfloat)n);
    for (int i = 0; i < n; i++)
    {
        float win[3];
        windowCoords(vs[i].clip, win);
        feedbackWrite(win, vs[i].clip[3], vs[i].color, vs[i].tex);
    }
}

// Points are kept or dropped whole
static void feedbackPoint(const Vertex *v)
{
    FeedbackVertex f;
    feedbackVertexOf(v, &f);
    for (int p = 0; p < 6; p++) if (viewPlaneDistance(f.clip, p) < 0.0f) return;
    if (f.clip[3] <= 0.0f) return;      // Only the origin is left, which has no window position
    feedbackPrimitive(GL_POINT_TOKEN, &f, 1);
}

static void feedbackLine(const Vertex *a, const Vertex *b)
{
    FeedbackVertex f[2], clipped[2];
    feedbackVertexOf(a, &f[0]);
    feedbackVertexOf(b, &f[1]);
    float t0 = 0.0f, t1 = 1.0f;
    for (int p = 0; p < 6; p++)
    {
        float da = viewPlaneDistance(f[0].clip, p), db = viewPlaneDistance(f[1].clip, p);
        if ((da < 0.0f) && (db < 0.0f)) return;
        if (da < 0.0f) t0 = fmaxf(t0, da/(da - db));
        else if (db < 0.0f) t1 = fminf(t1, da/(da - db));
    }
    if (t0 > t1) return;
    lerpFeedbackVertex(&clipped[0], &f[0], &f[1], t0);
    lerpFeedbackVertex(&clipped[1], &f[0], &f[1], t1);
    if ((clipped[0].clip[3] <= 0.0f) || (clipped[1].clip[3] <= 0.0f)) return;

    feedbackPrimitive(gl.lineReset? GL_LINE_RESET_TOKEN : GL_LINE_TOKEN, clipped, 2);
    gl.lineReset = false;
}

// Filled polygon: Sutherland-Hodgman against the view volume
static void feedbackFilledPolygon(const Vertex *const *vs, int n, const Vertex *pv, bool back)
{
    FeedbackVertex *in = malloc((size_t)n*sizeof(FeedbackVertex)), *out = NULL;
    if (in == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    for (int i = 0; i < n; i++)
    {
        Vertex shaded = *vs[i];
        const Vertex *src = (pv != NULL)? pv : vs[i];
        memcpy(shaded.color, back? src->backColor : src->color, sizeof(shaded.color));
        feedbackVertexOf(&shaded, &in[i]);
    }

    for (int p = 0; (p < 6) && (n >= 3); p++)
    {
        // Every edge adds at most two vertices
        FeedbackVertex *grown = realloc(out, 2*(size_t)n*sizeof(FeedbackVertex));
        if (grown == NULL) { setError(GL_OUT_OF_MEMORY); n = 0; break; }
        out = grown;
        int m = 0;
        float da = viewPlaneDistance(in[0].clip, p);
        for (int i = 0; i < n; i++)
        {
            const FeedbackVertex *a = &in[i], *b = &in[(i + 1) % n];
            float db = viewPlaneDistance(b->clip, p);
            if (da >= 0.0f) out[m++] = *a;
            if ((da >= 0.0f) != (db >= 0.0f)) lerpFeedbackVertex(&out[m++], a, b, da/(da - db));
            da = db;
        }
        n = m;
        FeedbackVertex *t = in; in = out; out = t;
    }
    for (int i = 0; i < n; i++) if (in[i].clip[3] <= 0.0f) n = 0;     // Degenerate: collapsed onto the eye
    if (n >= 3) feedbackPrimitive(GL_POLYGON_TOKEN, in, n);
    free(in);
    free(out);
}

// glBitmap, glDrawPixels, glCopyPixels in feedback mode: the token and the raster position (in selection mode the
// raster position counted as a hit already)
static void feedbackRaster(GLenum token)
{
    if ((gl.renderMode != GL_FEEDBACK) || !gl.raster.valid) return;
    float color[4];
    for (int i = 0; i < 4; i++) color[i] = gl.raster.color[i]*(1.0f/255.0f);
    feedbackValue((GLfloat)token);
    feedbackWrite(gl.raster.pos, gl.raster.pos[3], color, gl.raster.tex);
}

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
    Vertex clippedA, clippedB;
    if (!clipSegment(&a, &b, &clippedA, &clippedB)) return;
    if (gl.renderMode != GL_RENDER) { feedbackLine(a, b); return; }

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

// Point size of v (glPointParameter, GL 1.4 / ES 1.1 3.3): glPointSize or the point size array value, scaled by the
// distance attenuation 1/sqrt(a + b*d + c*d^2) of the eye distance d, clamped to GL_POINT_SIZE_MIN/MAX. The fade
// threshold only applies with multisampling, which PICA does not have
static float pointSize(const Vertex *v)
{
    float size = (v->pointSize >= 0.0f)? v->pointSize : gl.pointSize;
    const float *att = gl.pointAttenuation;
    if ((att[0] != 1.0f) || (att[1] != 0.0f) || (att[2] != 0.0f))
    {
        const float *m = gl.stack[0][gl.stackDepth[0]].m;
        float e[3];
        for (int i = 0; i < 3; i++) e[i] = m[i]*v->pos[0] + m[4 + i]*v->pos[1] + m[8 + i]*v->pos[2] + m[12 + i];
        float d = sqrtf(e[0]*e[0] + e[1]*e[1] + e[2]*e[2]);
        float k = att[0] + att[1]*d + att[2]*d*d;
        size = (k > 0.0f)? size/sqrtf(k) : C3DGL_MAX_POINT_SIZE;
    }
    size = fminf(fmaxf(size, gl.pointSizeMin), gl.pointSizeMax);
    return fminf(fmaxf(size, 1.0f), C3DGL_MAX_POINT_SIZE);
}

// Expand a point to a screen-aligned square in NDC (batch must be in clipSpace mode). Point sprites: the texcoords
// of the batch's sprite units run from (0, 0) at the top left to (1, 1) at the bottom right (OES_point_sprite)
static void emitPoint(const Vertex *v, float zBias)
{
    if (pointClipped(v)) return;
    if (gl.renderMode != GL_RENDER) { feedbackPoint(v); return; }

    float c[4];
    mat4Transform(projectionModelview(), v->pos, c);
    if (c[3] < CLIP_W_MIN) return;

    float p[3] = { c[0]/c[3], c[1]/c[3], c[2]/c[3] + zBias };
    float r = 0.5f*pointSize(v);
    float rx = 2.0f*r/(float)gl.state.viewport[2], ry = 2.0f*r/(float)gl.state.viewport[3];

    // Corners bottom left, top left, top right, bottom right (NDC y points up)
    static const float corner[4][2] = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } };
    Vertex q[4];
    for (int i = 0; i < 4; i++)
    {
        q[i] = *v;
        q[i].pos[0] = p[0] + corner[i][0]*rx;
        q[i].pos[1] = p[1] + corner[i][1]*ry;
        q[i].pos[2] = p[2];
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
        {
            if (!((gl.batch.spriteUnits >> unit) & 1)) continue;
            float *tc = (unit == 0)? q[i].tex : q[i].texExtra[unit - 1];
            tc[0] = 0.5f + 0.5f*corner[i][0];
            tc[1] = 0.5f - 0.5f*corner[i][1];
            tc[2] = 1.0f;
        }
    }
    emitTriangle(&q[0], &q[1], &q[2]);
    emitTriangle(&q[0], &q[2], &q[3]);
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

// Polygon in feedback/selection mode (see the feedback section): culled, then filled, outlined or as vertices like
// emitPolygon() does
static void feedbackPolygon(const Vertex *const *vs, const bool *edges, int n, const Vertex *pv)
{
    bool front = (polygonArea(vs, n) > 0.0f) == (gl.state.frontFace == GL_CCW);
    if (gl.state.cull && ((gl.state.cullFace == GL_FRONT_AND_BACK) || ((gl.state.cullFace == GL_FRONT) == front))) return;
    bool back = gl.lightingEnabled && gl.lighting.twoSide && !front;

    GLenum mode = gl.polygonMode[front? 0 : 1];
    if (mode == GL_FILL) { feedbackFilledPolygon(vs, n, pv, back); return; }

    gl.lineReset = true;
    for (int i = 0; i < n; i++)
    {
        if (!edges[i]) continue;
        Vertex a = shadeVertex(vs[i], pv, 0.0f, back);
        if (mode == GL_LINE)
        {
            Vertex b = shadeVertex(vs[(i + 1) % n], pv, 0.0f, back);
            feedbackLine(&a, &b);
        }
        else feedbackPoint(&a);
    }
}

// A polygon (triangle, quad, polygon) with edge flags: filled, outlined or as vertices depending on
// glPolygonMode of the side that faces the viewer. pv: provoking vertex for flat shading
static void emitPolygon(const Vertex *const *vs, const bool *edges, int n, const Vertex *pv)
{
    if (gl.clipEnabled && ((n = clipPolygon(&vs, &edges, n)) < 3)) return;
    if (gl.renderMode != GL_RENDER) { feedbackPolygon(vs, edges, n, pv); return; }

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
        prepareDraw(mode != GL_FILL, mode == GL_POINT);
    }

    bool offset = (mode == GL_FILL)? gl.offsetFill : (mode == GL_LINE)? gl.offsetLine : gl.offsetPoint;
    float range = gl.state.depthFar - gl.state.depthNear;
    float windowOffset = (offset && (range != 0.0f))? polygonOffset(vs, n)/range : 0.0f;    // In PICA NDC (= 1/2 GL NDC)

    if ((mode == GL_FILL) && (pv == NULL) && !back && (windowOffset == 0.0f))
    {
        // Common case: the vertices go to the buffer as they are (their depth bias is 0)
        for (int i = 1; i + 1 < n; i++) emitTriangle(vs[0], vs[i], vs[i + 1]);
    }
    else if (mode == GL_FILL)
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
    bool render = (gl.renderMode == GL_RENDER);
    if (render) prepareDraw(lineOrPoint, mode == GL_POINTS);
    gl.primitive = mode;
    gl.primCount = 0;
    gl.primTotal = 0;
    gl.lineReset = true;
    // Feedback reports a polygon as one, not as the triangles it is filled with
    gl.collectPolygon = (mode == GL_POLYGON) && (!render || (gl.polygonMode[0] != GL_FILL) || (gl.polygonMode[1] != GL_FILL));
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
            if (gl.primCount == 2) { gl.lineReset = true; emitShadedLine(&p[0], &p[1], FLAT(&p[1])); gl.primCount = 0; }
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
    for (int i = 0; i < 4; i++) c[i] = color[i]*(1.0f/255.0f);

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

// Object space normal -> eye space. Not normalized unless asked for, like GL (a scaling modelview changes the brightness)
static void eyeNormal(const float normal[3], float n[3])
{
    updateNormalMatrix();
    const float *nm = gl.normalMatrix;
    for (int k = 0; k < 3; k++) n[k] = nm[k*3]*normal[0] + nm[k*3 + 1]*normal[1] + nm[k*3 + 2]*normal[2];
    if (gl.normalize) normalize3(n);
    else if (gl.rescaleNormal) for (int k = 0; k < 3; k++) n[k] *= gl.normalRescale;
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

    float n[3];
    eyeNormal(normal, n);

    shadeFace(&gl.lighting.material[0], eye, n, v->color);
    if (gl.lighting.twoSide)
    {
        float back[3] = { -n[0], -n[1], -n[2] };
        shadeFace(&gl.lighting.material[1], eye, back, v->backColor);
    }
}

// Lit colors of recent vertices. Meshes submit shared vertices several times (glDrawElements, triangle lists, strips
// next to each other), the lit result only depends on position, normal and color while the lighting state is
// unchanged. Every call that changes lighting state starts a new generation (litStateChanged()), a modelview change
// too; GL_COLOR_MATERIAL needs nothing: the tracked material values follow the color, which is part of the key
#define LIT_CACHE_SIZE  512         // Power of two; direct mapped
typedef struct {
    u32 key[7];                 // Position, normal, color
    u32 gen;
    u8 color[4], backColor[4];
} LitCacheEntry;
static LitCacheEntry litCache[LIT_CACHE_SIZE];

static void lightVertexCached(Vertex *v, const float normal[3])
{
    u32 key[7];
    memcpy(key, v->pos, 3*sizeof(float));
    memcpy(key + 3, normal, 3*sizeof(float));
    memcpy(key + 6, v->color, 4);
    if (gl.litCacheSerial != gl.matrixSerial) { gl.litCacheGen++; gl.litCacheSerial = gl.matrixSerial; }

    u32 h = 2166136261u;
    for (int i = 0; i < 7; i++) h = (h ^ key[i])*16777619u;
    LitCacheEntry *e = &litCache[(h ^ (h >> 16)) & (LIT_CACHE_SIZE - 1)];
    if ((e->gen == gl.litCacheGen) && (memcmp(e->key, key, sizeof(key)) == 0))
    {
        if (gl.colorMaterial) applyColorMaterial(v->color);     // The material still follows the color
        memcpy(v->color, e->color, 4);
        memcpy(v->backColor, e->backColor, 4);
        return;
    }

    lightVertex(v, normal);
    memcpy(e->key, key, sizeof(key));
    e->gen = gl.litCacheGen;
    memcpy(e->color, v->color, 4);
    memcpy(e->backColor, v->backColor, 4);
}

//----------------------------------------------------------------------------------
// Texture coordinate generation (GL 1.1 section 2.10.4): per vertex on the CPU, when the vertex is submitted
//----------------------------------------------------------------------------------
// Generated coordinates of the units with texgen replace the vertex's ones; the other coordinates of such a unit stay
// (r is 0 then: vertices keep no r). The unit's texture matrix is applied here too, with all of s, t, r, q, so that a
// generated r takes part (projective texturing: eye linear s, t, r, q times the light's matrices); prepareDraw() gives
// these units an identity matrix in the shader.
// Everything linear is folded into one TexGenTransform per unit: eye planes times the modelview are object planes,
// and the texture matrix rows s, t, q times the per-coordinate inputs give the final rows
static void updateTexGenTransforms(void)
{
    u32 serials[3] = { gl.matrixSerial, gl.texMatrixSerial, gl.texGenSerial };
    if (memcmp(serials, gl.texGenTransformSerials, sizeof(serials)) == 0) return;
    memcpy(gl.texGenTransformSerials, serials, sizeof(serials));

    const float *mv = gl.stack[0][gl.stackDepth[0]].m;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        const TexGenState *g = &gl.texGen[unit];
        TexGenTransform *x = &gl.texGenTransform[unit];
        memset(x, 0, sizeof(*x));
        if (!g->enabled) continue;

        // Input coordinate c (s, t, r, q) = gen[c] . (x, y, z, 1) + pass[c] . (s, t, q) + sphere[c] . (s, t)
        float gen[4][4] = {{ 0 }}, pass[4][3] = {{ 0 }}, sphere[4][2] = {{ 0 }};
        for (int c = 0; c < 4; c++)
        {
            if (!((g->enabled >> c) & 1))
            {
                if (c != 2) { pass[c][(c == 3)? 2 : c] = 1.0f; x->usesPass = true; }     // r stays 0
                continue;
            }
            switch (g->mode[c])
            {
                case GL_OBJECT_LINEAR: memcpy(gen[c], g->objectPlane[c], sizeof(gen[c])); break;
                case GL_EYE_LINEAR:
                    // plane . (MV p) = (plane^T MV) . p
                    for (int j = 0; j < 4; j++)
                    {
                        const float *p = g->eyePlane[c];
                        gen[c][j] = p[0]*mv[j*4] + p[1]*mv[j*4 + 1] + p[2]*mv[j*4 + 2] + p[3]*mv[j*4 + 3];
                    }
                    break;
                default: sphere[c][c] = 1.0f; x->usesSphere = true; break;     // GL_SPHERE_MAP: s, t only
            }
        }

        const float *tm = gl.stack[2 + unit][gl.stackDepth[2 + unit]].m;
        static const int rows[3] = { 0, 1, 3 };     // s, t, q
        for (int i = 0; i < 3; i++)
        {
            const float r[4] = { tm[rows[i]], tm[4 + rows[i]], tm[8 + rows[i]], tm[12 + rows[i]] };
            for (int c = 0; c < 4; c++)
            {
                for (int j = 0; j < 4; j++) x->gen[i][j] += r[c]*gen[c][j];
                for (int j = 0; j < 3; j++) x->pass[i][j] += r[c]*pass[c][j];
                for (int j = 0; j < 2; j++) x->sphere[i][j] += r[c]*sphere[c][j];
            }
        }
    }
}

// GL_SPHERE_MAP s, t: r = u - 2 n (n.u) with u the unit vector from the eye to the vertex and n the eye space normal,
// then s, t = r_x/m + 1/2, r_y/m + 1/2 with m = 2 sqrt(r_x^2 + r_y^2 + (r_z + 1)^2)
static void sphereMap(const Vertex *v, const float normal[3], float out[2])
{
    float u[4], n[3], r[3];
    mat4Transform(&gl.stack[0][gl.stackDepth[0]], v->pos, u);
    if ((u[3] != 1.0f) && (u[3] != 0.0f)) for (int k = 0; k < 3; k++) u[k] /= u[3];
    normalize3(u);
    eyeNormal(normal, n);
    float nu = 2.0f*dot3(n, u);
    for (int k = 0; k < 3; k++) r[k] = u[k] - nu*n[k];
    float m = 2.0f*sqrtf(r[0]*r[0] + r[1]*r[1] + (r[2] + 1.0f)*(r[2] + 1.0f));
    float inv = (m > 0.0f)? 1.0f/m : 0.0f;
    out[0] = r[0]*inv + 0.5f;
    out[1] = r[1]*inv + 0.5f;
}

static void generateTexCoords(Vertex *v, const float normal[3])
{
    updateTexGenTransforms();
    float sphere[2];
    bool haveSphere = false;
    const float *p = v->pos;

    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        if (!gl.texGen[unit].enabled) continue;
        const TexGenTransform *x = &gl.texGenTransform[unit];
        float *tc = (unit == 0)? v->tex : v->texExtra[unit - 1];
        float out[3];
        for (int i = 0; i < 3; i++) out[i] = x->gen[i][0]*p[0] + x->gen[i][1]*p[1] + x->gen[i][2]*p[2] + x->gen[i][3];
        if (x->usesPass)
            for (int i = 0; i < 3; i++) out[i] += x->pass[i][0]*tc[0] + x->pass[i][1]*tc[1] + x->pass[i][2]*tc[2];
        if (x->usesSphere)
        {
            if (!haveSphere) { sphereMap(v, normal, sphere); haveSphere = true; }
            for (int i = 0; i < 3; i++) out[i] += x->sphere[i][0]*sphere[0] + x->sphere[i][1]*sphere[1];
        }
        memcpy(tc, out, sizeof(out));
        if (unit == 0) v->texR = 0.0f;      // Not generated (see Vertex)
    }
}

// Every vertex goes through here: texgen, lighting, then primitive assembly
static void submitLitVertex(Vertex *v, const float normal[3], bool edge)
{
    if (gl.texGen[0].enabled | gl.texGen[1].enabled | gl.texGen[2].enabled) generateTexCoords(v, normal);
    if (gl.lightingEnabled) lightVertexCached(v, normal);
    submitVertex(v, edge);
}

// Capabilities that programs commonly toggle but c3dgl does not implement or that have no effect on this
// framebuffer (color index mode, no sample buffers): stored for glIsEnabled
static const GLenum ignoredCaps[] = {
    GL_DITHER, GL_LINE_SMOOTH, GL_POINT_SMOOTH, GL_POLYGON_SMOOTH, GL_INDEX_LOGIC_OP,
    GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_ONE, GL_SAMPLE_COVERAGE,
};

static int ignoredCapBit(GLenum cap)
{
    for (int i = 0; i < (int)(sizeof(ignoredCaps)/sizeof(ignoredCaps[0])); i++) if (ignoredCaps[i] == cap) return i;
    return -1;
}

//----------------------------------------------------------------------------------
// Platform API (c3dgl.h)
//----------------------------------------------------------------------------------
bool c3dglInit(void)
{
    if (gl.ready) return true;
    memset(&gl, 0, sizeof(gl));
    memset(litCache, 0, sizeof(litCache));     // Generations restart at 0

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
    gl.state.logicOpMode = GL_COPY;
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

        // glTexGen defaults: eye linear, s and t planes along x and y
        TexGenState *g = &gl.texGen[unit];
        for (int c = 0; c < 4; c++) g->mode[c] = GL_EYE_LINEAR;
        g->objectPlane[0][0] = g->eyePlane[0][0] = 1.0f;
        g->objectPlane[1][1] = g->eyePlane[1][1] = 1.0f;
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
    gl.pointSizeMax = C3DGL_MAX_POINT_SIZE;
    gl.pointFadeThreshold = 1.0f;
    gl.pointAttenuation[0] = 1.0f;
    gl.clearColor = 0x000000FF;
    gl.clearDepth = 1.0f;
    gl.state.depthFar = 1.0f;
    gl.polygonMode[0] = gl.polygonMode[1] = GL_FILL;
    gl.currentEdge = true;
    gl.shadeModel = GL_SMOOTH;
    gl.currentNormal[2] = 1.0f;
    initLighting();
    gl.fogMode = GL_EXP;
    gl.fogDensity = gl.fogEnd = 1.0f;
    gl.ignoredCaps = (1u << ignoredCapBit(GL_DITHER)) | (1u << ignoredCapBit(GL_MULTISAMPLE));    // Enabled by default
    gl.sampleCoverage = 1.0f;
    gl.drawBuffer = gl.readBuffer = GL_BACK;
    gl.raster = (RasterState){ .pos = { 0, 0, 0, 1 }, .valid = true, .color = { 255, 255, 255, 255 }, .tex = { 0, 0, 0, 1 } };
    gl.zoomX = gl.zoomY = 1.0f;
    gl.transfer = noTransfer;
    gl.renderMode = GL_RENDER;
    gl.feedbackType = GL_2D;
    gl.hitMinZ = 1.0f;
    for (int i = 0; i < PIXEL_MAP_COUNT; i++) gl.pixelMaps[i].size = 1;    // One entry, 0
    memset(gl.current.color, 255, 4);
    gl.current.tex[2] = 1.0f;
    gl.current.pointSize = -1.0f;   // No point size array: glPointSize

    // Client array defaults: size 4 (3 for normals), GL_FLOAT
    for (int i = 0; i < ARRAY_COUNT; i++)
    {
        gl.arrays[i].size = (i == ARRAY_NORMAL)? 3 : ((i == ARRAY_EDGEFLAG) || (i == ARRAY_POINTSIZE))? 1 : 4;
        gl.arrays[i].type = (i == ARRAY_EDGEFLAG)? GL_UNSIGNED_BYTE : GL_FLOAT;
    }

    for (int i = 0; i < 2 + C3DGL_TEXTURE_UNITS; i++) mat4Identity(&gl.stack[i][0]);

    // Texture 0 of each target (GL 1.0 style code without glBindTexture)
    initTexture(&gl.textures[DEFAULT_TEXTURE_1D]);
    gl.textures[DEFAULT_TEXTURE_1D].target = GL_TEXTURE_1D;
    initTexture(&gl.textures[DEFAULT_TEXTURE_2D]);
    gl.textures[DEFAULT_TEXTURE_2D].target = GL_TEXTURE_2D;

    // Evaluator defaults: grids of 1 segment over [0, 1]
    gl.grid1n = gl.grid2un = gl.grid2vn = 1;
    gl.grid1u2 = gl.grid2u2 = gl.grid2v2 = 1.0f;
    gl.matrixSerial = gl.texMatrixSerial = 1;

    gl.ready = true;
    return true;
}

void c3dglClose(void)
{
    if (gl.frameActive) { flushVertexCache(); C3D_FrameEnd(0); gl.frameActive = false; }

    for (int i = 1; i < TEXTURE_SLOTS; i++) if (gl.textures[i].loaded) C3D_TexDelete(&gl.textures[i].tex);
    processDeferredDeletes();
    free(gl.deferredDeletes);
    for (int i = 0; i < gl.pixelChunkCount; i++) linearFree(gl.pixelChunks[i].data);
    free(gl.pixelChunks);
    free(gl.polyVerts);
    free(gl.polyEdges);
    free(gl.polyPtrs);
    for (int i = 0; i < 2; i++) { free(gl.clipLists[i].verts); free(gl.clipLists[i].edges); }
    free(gl.clipPtrs);
    for (GLuint i = 0; i < gl.bufferCount; i++) free(gl.buffers[i].data);
    free(gl.buffers);
    for (int i = 0; i < 9; i++) { free(gl.map1[i].points); free(gl.map2[i].points); }
    free(gl.evalGrid);
    for (int i = 0; i < gl.listCount; i++) free(gl.lists[i].words);
    free(gl.lists);
    free(gl.listWords);

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
    flushVertexCache();
    C3D_FrameEnd(0);
    gl.frameActive = false;
}

//----------------------------------------------------------------------------------
// OpenGL: state
//----------------------------------------------------------------------------------
static void setCapability(GLenum cap, bool enable)
{
    LIST_SAVE(ENABLE, "ui", cap, (int)enable);
    litStateChanged();          // Lights, GL_NORMALIZE, ...
    int bit = ignoredCapBit(cap);
    if (bit >= 0)
    {
        if (enable) gl.ignoredCaps |= 1u << bit;
        else gl.ignoredCaps &= ~(1u << bit);
        return;
    }

    switch (cap)
    {
        case GL_TEXTURE_1D: gl.texture1D[gl.activeTexture] = enable; break;
        case GL_TEXTURE_2D: gl.texture2D[gl.activeTexture] = enable; break;
        case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
            if (enable) gl.texGen[gl.activeTexture].enabled |= 1u << (cap - GL_TEXTURE_GEN_S);
            else gl.texGen[gl.activeTexture].enabled &= ~(1u << (cap - GL_TEXTURE_GEN_S));
            gl.texGenSerial++;
            break;
        case GL_BLEND: gl.state.blend = enable; break;
        case GL_COLOR_LOGIC_OP: gl.state.logicOp = enable; break;
        case GL_DEPTH_TEST: gl.state.depthTest = enable; break;
        case GL_ALPHA_TEST: gl.state.alphaTest = enable; break;
        case GL_STENCIL_TEST:
            gl.state.stencilTest = enable;
            if (enable) gl.stencilUsed = true;
            break;
        case GL_CULL_FACE: gl.state.cull = enable; break;
        case GL_SCISSOR_TEST: gl.state.scissor = enable; break;
        case GL_FOG: gl.fog = enable; break;
        case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2:
        case GL_CLIP_PLANE3: case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
            if (enable) gl.clipEnabled |= 1u << (cap - GL_CLIP_PLANE0);
            else gl.clipEnabled &= ~(1u << (cap - GL_CLIP_PLANE0));
            gl.clipObjectSerial = 0;
            break;
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
        case GL_POINT_SPRITE_OES: gl.pointSprite = enable; break;
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
        case GL_POINT_SIZE_ARRAY_OES: gl.arrays[ARRAY_POINTSIZE].enabled = enable; break;
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
        case GL_TEXTURE_1D: return gl.texture1D[gl.activeTexture];
        case GL_TEXTURE_2D: return gl.texture2D[gl.activeTexture];
        case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
            return (gl.texGen[gl.activeTexture].enabled >> (cap - GL_TEXTURE_GEN_S)) & 1;
        case GL_BLEND: return gl.state.blend;
        case GL_COLOR_LOGIC_OP: return gl.state.logicOp;
        case GL_DEPTH_TEST: return gl.state.depthTest;
        case GL_ALPHA_TEST: return gl.state.alphaTest;
        case GL_STENCIL_TEST: return gl.state.stencilTest;
        case GL_CULL_FACE: return gl.state.cull;
        case GL_SCISSOR_TEST: return gl.state.scissor;
        case GL_FOG: return gl.fog;
        case GL_CLIP_PLANE0: case GL_CLIP_PLANE1: case GL_CLIP_PLANE2:
        case GL_CLIP_PLANE3: case GL_CLIP_PLANE4: case GL_CLIP_PLANE5:
            return (gl.clipEnabled >> (cap - GL_CLIP_PLANE0)) & 1;
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
        case GL_POINT_SIZE_ARRAY_OES: return gl.arrays[ARRAY_POINTSIZE].enabled;
        case GL_POLYGON_OFFSET_FILL: return gl.offsetFill;
        case GL_POLYGON_OFFSET_LINE: return gl.offsetLine;
        case GL_POLYGON_OFFSET_POINT: return gl.offsetPoint;
        case GL_POINT_SPRITE_OES: return gl.pointSprite;
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
    LIST_SAVE(SHADE_MODEL, "u", mode);
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

// Component (R, G, B, A, depth) of a glPixelTransfer scale/bias, -1 for the other pnames
static int transferComponent(GLenum pname)
{
    switch (pname)
    {
        case GL_RED_SCALE: case GL_RED_BIAS: return 0;
        case GL_GREEN_SCALE: case GL_GREEN_BIAS: return 1;
        case GL_BLUE_SCALE: case GL_BLUE_BIAS: return 2;
        case GL_ALPHA_SCALE: case GL_ALPHA_BIAS: return 3;
        case GL_DEPTH_SCALE: case GL_DEPTH_BIAS: return 4;
        default: return -1;
    }
}

static bool transferIsBias(GLenum pname)
{
    return (pname == GL_RED_BIAS) || (pname == GL_GREEN_BIAS) || (pname == GL_BLUE_BIAS) || (pname == GL_ALPHA_BIAS) ||
           (pname == GL_DEPTH_BIAS);
}

void glPixelTransferf(GLenum pname, GLfloat param)
{
    LIST_SAVE(PIXEL_TRANSFER, "uf", pname, param);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    PixelTransfer *t = &gl.transfer;
    int i = transferComponent(pname);
    if (i >= 0)
    {
        (transferIsBias(pname)? t->bias : t->scale)[i] = param;
        return;
    }
    switch (pname)
    {
        case GL_MAP_COLOR: t->mapColor = (param != 0.0f); break;
        case GL_MAP_STENCIL: t->mapStencil = (param != 0.0f); break;
        case GL_INDEX_SHIFT: t->indexShift = (GLint)lroundf(param); break;
        case GL_INDEX_OFFSET: t->indexOffset = (GLint)lroundf(param); break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glPixelTransferi(GLenum pname, GLint param) { glPixelTransferf(pname, (GLfloat)param); }

// Table of a GL_PIXEL_MAP_* enum, -1 if map is not one
static int pixelMapIndex(GLenum map)
{
    return ((map >= GL_PIXEL_MAP_I_TO_I) && (map <= GL_PIXEL_MAP_A_TO_A))? (int)(map - GL_PIXEL_MAP_I_TO_I) : -1;
}

// glPixelMap with the values as floats (see pixelMap()). Tables that are looked up by index (I_TO_*, S_TO_S) need 2^n
// entries; color components are clamped to [0, 1]
static void setPixelMap(GLenum map, GLsizei mapsize, const GLfloat *values)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    int m = pixelMapIndex(map);
    if (m < 0) { setError(GL_INVALID_ENUM); return; }
    bool pow2 = (m < PIXEL_MAP_R_TO_R);
    if ((mapsize < 1) || (mapsize > C3DGL_MAX_PIXEL_MAP_TABLE) || (pow2 && (mapsize & (mapsize - 1))))
    {
        setError(GL_INVALID_VALUE);
        return;
    }
    PixelMap *p = &gl.pixelMaps[m];
    p->size = mapsize;
    for (int i = 0; i < mapsize; i++)
    {
        float v = values[i];
        p->values[i] = (m < PIXEL_MAP_I_TO_R)? v : !(v > 0.0f)? 0.0f : (v > 1.0f)? 1.0f : v;
    }
}

// glPixelMap{fv,uiv,usv}: unsigned integers of the color tables are normalized (GL 1.1 table 2.9), index tables take
// them as they are. Recorded in display lists with the converted values (an invalid size without them)
static void pixelMap(GLenum map, GLsizei mapsize, const void *values, GLenum type)
{
    int m = pixelMapIndex(map), count = ((mapsize > 0) && (mapsize <= C3DGL_MAX_PIXEL_MAP_TABLE))? mapsize : 0;
    float v[C3DGL_MAX_PIXEL_MAP_TABLE];
    for (int i = 0; i < count; i++)
    {
        if (type == GL_FLOAT) v[i] = ((const GLfloat *)values)[i];
        else if (type == GL_UNSIGNED_INT) v[i] = (float)((m >= PIXEL_MAP_I_TO_R)? ((const GLuint *)values)[i]/4294967295.0 : ((const GLuint *)values)[i]);
        else v[i] = (m >= PIXEL_MAP_I_TO_R)? ((const GLushort *)values)[i]/65535.0f : ((const GLushort *)values)[i];
    }
    if (gl.listCompiling) listSave(LIST_PIXEL_MAP, "uiF", map, mapsize, count, v);
    else setPixelMap(map, mapsize, v);
}

void glPixelMapfv(GLenum map, GLsizei mapsize, const GLfloat *values) { pixelMap(map, mapsize, values, GL_FLOAT); }
void glPixelMapuiv(GLenum map, GLsizei mapsize, const GLuint *values) { pixelMap(map, mapsize, values, GL_UNSIGNED_INT); }
void glPixelMapusv(GLenum map, GLsizei mapsize, const GLushort *values) { pixelMap(map, mapsize, values, GL_UNSIGNED_SHORT); }

// glGetPixelMap{fv,uiv,usv}: color components scaled to the integer range, indices rounded
static void getPixelMap(GLenum map, void *values, GLenum type)
{
    int m = pixelMapIndex(map);
    if (m < 0) { setError(GL_INVALID_ENUM); return; }
    const PixelMap *p = &gl.pixelMaps[m];
    bool color = (m >= PIXEL_MAP_I_TO_R);
    for (int i = 0; i < p->size; i++)
    {
        double v = p->values[i];
        if (type == GL_FLOAT) ((GLfloat *)values)[i] = (GLfloat)v;
        else if (type == GL_UNSIGNED_INT) ((GLuint *)values)[i] = (GLuint)(s64)llround(color? v*4294967295.0 : v);
        else ((GLushort *)values)[i] = (GLushort)(s64)llround(color? v*65535.0 : v);
    }
}

void glGetPixelMapfv(GLenum map, GLfloat *values) { getPixelMap(map, values, GL_FLOAT); }
void glGetPixelMapuiv(GLenum map, GLuint *values) { getPixelMap(map, values, GL_UNSIGNED_INT); }
void glGetPixelMapusv(GLenum map, GLushort *values) { getPixelMap(map, values, GL_UNSIGNED_SHORT); }

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
        case GL_CURRENT_RASTER_POSITION: for (int i = 0; i < 4; i++) v[i] = gl.raster.pos[i]; return 4;
        case GL_CURRENT_RASTER_POSITION_VALID: v[0] = gl.raster.valid; return 1;
        case GL_CURRENT_RASTER_COLOR: for (int i = 0; i < 4; i++) v[i] = gl.raster.color[i]/255.0; *normalized = true; return 4;
        case GL_CURRENT_RASTER_TEXTURE_COORDS: for (int i = 0; i < 4; i++) v[i] = gl.raster.tex[i]; return 4;
        case GL_CURRENT_RASTER_DISTANCE: v[0] = gl.raster.distance; return 1;
        case GL_CURRENT_RASTER_INDEX: v[0] = 1; return 1;       // Color index mode only
        case GL_ZOOM_X: v[0] = gl.zoomX; return 1;
        case GL_ZOOM_Y: v[0] = gl.zoomY; return 1;
        case GL_MAP_COLOR: v[0] = gl.transfer.mapColor; return 1;
        case GL_MAP_STENCIL: v[0] = gl.transfer.mapStencil; return 1;
        case GL_INDEX_SHIFT: v[0] = gl.transfer.indexShift; return 1;
        case GL_INDEX_OFFSET: v[0] = gl.transfer.indexOffset; return 1;
        case GL_RED_SCALE: case GL_GREEN_SCALE: case GL_BLUE_SCALE: case GL_ALPHA_SCALE: case GL_DEPTH_SCALE:
        case GL_RED_BIAS: case GL_GREEN_BIAS: case GL_BLUE_BIAS: case GL_ALPHA_BIAS: case GL_DEPTH_BIAS:
        {
            int i = transferComponent(pname);
            v[0] = (transferIsBias(pname)? gl.transfer.bias : gl.transfer.scale)[i];
            return 1;
        }
        case GL_MAX_PIXEL_MAP_TABLE: v[0] = C3DGL_MAX_PIXEL_MAP_TABLE; return 1;
        case GL_PIXEL_MAP_I_TO_I_SIZE: case GL_PIXEL_MAP_S_TO_S_SIZE: case GL_PIXEL_MAP_I_TO_R_SIZE:
        case GL_PIXEL_MAP_I_TO_G_SIZE: case GL_PIXEL_MAP_I_TO_B_SIZE: case GL_PIXEL_MAP_I_TO_A_SIZE:
        case GL_PIXEL_MAP_R_TO_R_SIZE: case GL_PIXEL_MAP_G_TO_G_SIZE: case GL_PIXEL_MAP_B_TO_B_SIZE:
        case GL_PIXEL_MAP_A_TO_A_SIZE: v[0] = gl.pixelMaps[pname - GL_PIXEL_MAP_I_TO_I_SIZE].size; return 1;
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
        case GL_LIST_BASE: v[0] = gl.listBase; return 1;
        case GL_LIST_INDEX: v[0] = gl.listName; return 1;
        case GL_LIST_MODE: v[0] = gl.listName? gl.listMode : 0; return 1;
        case GL_MAX_LIST_NESTING: v[0] = C3DGL_MAX_LIST_NESTING; return 1;
        case GL_RENDER_MODE: v[0] = gl.renderMode; return 1;
        case GL_FEEDBACK_BUFFER_SIZE: v[0] = gl.feedbackSize; return 1;
        case GL_FEEDBACK_BUFFER_TYPE: v[0] = gl.feedbackType; return 1;
        case GL_SELECTION_BUFFER_SIZE: v[0] = gl.selectSize; return 1;
        case GL_NAME_STACK_DEPTH: v[0] = gl.nameDepth; return 1;
        case GL_MAX_NAME_STACK_DEPTH: v[0] = C3DGL_MAX_NAME_STACK; return 1;
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
        case GL_POINT_SIZE_ARRAY_TYPE_OES: v[0] = gl.arrays[ARRAY_POINTSIZE].type; return 1;
        case GL_POINT_SIZE_ARRAY_STRIDE_OES: v[0] = gl.arrays[ARRAY_POINTSIZE].stride; return 1;
        case GL_ARRAY_BUFFER_BINDING: v[0] = gl.arrayBuffer; return 1;
        case GL_ELEMENT_ARRAY_BUFFER_BINDING: v[0] = gl.elementArrayBuffer; return 1;
        case GL_VERTEX_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_VERTEX].buffer; return 1;
        case GL_NORMAL_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_NORMAL].buffer; return 1;
        case GL_COLOR_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_COLOR].buffer; return 1;
        case GL_TEXTURE_COORD_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_TEXCOORD].buffer; return 1;
        case GL_EDGE_FLAG_ARRAY_BUFFER_BINDING: v[0] = gl.arrays[ARRAY_EDGEFLAG].buffer; return 1;
        case GL_POINT_SIZE_ARRAY_BUFFER_BINDING_OES: v[0] = gl.arrays[ARRAY_POINTSIZE].buffer; return 1;
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
        case GL_LOGIC_OP_MODE: v[0] = gl.state.logicOpMode; return 1;
        case GL_SAMPLE_COVERAGE_VALUE: v[0] = gl.sampleCoverage; return 1;
        case GL_SAMPLE_COVERAGE_INVERT: v[0] = gl.sampleCoverageInvert; return 1;
        case GL_SAMPLE_BUFFERS: case GL_SAMPLES: v[0] = 0; return 1;
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
        case GL_MAX_CLIP_PLANES: v[0] = C3DGL_MAX_CLIP_PLANES; return 1;
        case GL_FOG_MODE: v[0] = gl.fogMode; return 1;
        case GL_FOG_DENSITY: v[0] = gl.fogDensity; return 1;
        case GL_FOG_START: v[0] = gl.fogStart; return 1;
        case GL_FOG_END: v[0] = gl.fogEnd; return 1;
        case GL_FOG_INDEX: v[0] = gl.fogIndex; return 1;
        case GL_FOG_COLOR: for (int i = 0; i < 4; i++) v[i] = gl.fogColor[i]; *normalized = true; return 4;
        case GL_LIGHT_MODEL_AMBIENT: for (int i = 0; i < 4; i++) v[i] = gl.lighting.modelAmbient[i]; *normalized = true; return 4;
        case GL_LIGHT_MODEL_LOCAL_VIEWER: v[0] = gl.lighting.localViewer; return 1;
        case GL_LIGHT_MODEL_TWO_SIDE: v[0] = gl.lighting.twoSide; return 1;
        case GL_COLOR_MATERIAL_FACE: v[0] = gl.lighting.colorMaterialFace; return 1;
        case GL_COLOR_MATERIAL_PARAMETER: v[0] = gl.lighting.colorMaterialMode; return 1;

        case GL_LINE_WIDTH: v[0] = gl.lineWidth; return 1;
        case GL_POINT_SIZE: v[0] = gl.pointSize; return 1;
        case GL_POINT_SIZE_RANGE: case GL_ALIASED_POINT_SIZE_RANGE: v[0] = 1.0; v[1] = C3DGL_MAX_POINT_SIZE; return 2;
        case GL_POINT_SIZE_GRANULARITY: v[0] = 0.0; return 1;     // Any size (points are quads)
        case GL_POINT_SIZE_MIN: v[0] = gl.pointSizeMin; return 1;
        case GL_POINT_SIZE_MAX: v[0] = gl.pointSizeMax; return 1;
        case GL_POINT_FADE_THRESHOLD_SIZE: v[0] = gl.pointFadeThreshold; return 1;
        case GL_POINT_DISTANCE_ATTENUATION: for (int i = 0; i < 3; i++) v[i] = gl.pointAttenuation[i]; return 3;
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
        case GL_TEXTURE_BINDING_1D: v[0] = gl.boundTexture1D[gl.activeTexture]; return 1;
        case GL_TEXTURE_BINDING_2D: v[0] = gl.boundTexture[gl.activeTexture]; return 1;
        case GL_DRAW_BUFFER: v[0] = gl.drawBuffer; return 1;
        case GL_READ_BUFFER: v[0] = gl.readBuffer; return 1;
        case GL_AUX_BUFFERS: v[0] = 0; return 1;
        case GL_DOUBLEBUFFER: v[0] = GL_TRUE; return 1;
        case GL_STEREO: v[0] = GL_FALSE; return 1;
        case GL_MAX_TEXTURE_SIZE: v[0] = C3DGL_MAX_TEXTURE_SIZE; return 1;
        case GL_NUM_COMPRESSED_TEXTURE_FORMATS: v[0] = COMPRESSED_FORMAT_COUNT; return 1;
        case GL_IMPLEMENTATION_COLOR_READ_TYPE_OES: v[0] = GL_UNSIGNED_BYTE; return 1;
        case GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES: v[0] = GL_RGBA; return 1;
        case GL_COMPRESSED_TEXTURE_FORMATS: for (int i = 0; i < COMPRESSED_FORMAT_COUNT; i++) v[i] = compressedFormats[i]; return COMPRESSED_FORMAT_COUNT;

        // Render target: RGBA8 color, D24S8 depth/stencil
        case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: v[0] = 8; return 1;
        case GL_DEPTH_BITS: v[0] = 24; return 1;
        case GL_STENCIL_BITS: v[0] = 8; return 1;

        default:
            // Capabilities can be queried with glGet too
            if ((ignoredCapBit(pname) >= 0) || (pname == GL_TEXTURE_2D) || (pname == GL_BLEND) || (pname == GL_COLOR_LOGIC_OP) || (pname == GL_DEPTH_TEST) ||
                (pname == GL_ALPHA_TEST) || (pname == GL_STENCIL_TEST) || (pname == GL_CULL_FACE) || (pname == GL_SCISSOR_TEST) ||
                (pname == GL_VERTEX_ARRAY) || (pname == GL_TEXTURE_COORD_ARRAY) || (pname == GL_COLOR_ARRAY) || (pname == GL_NORMAL_ARRAY) ||
                (pname == GL_EDGE_FLAG_ARRAY) || (pname == GL_POINT_SIZE_ARRAY_OES) || (pname == GL_POLYGON_OFFSET_FILL) || (pname == GL_POLYGON_OFFSET_LINE) ||
                (pname == GL_POLYGON_OFFSET_POINT) || (pname == GL_AUTO_NORMAL) || (pname == GL_LIGHTING) || (pname == GL_FOG) ||
                ((pname >= GL_LIGHT0) && (pname <= GL_LIGHT7)) || (pname == GL_COLOR_MATERIAL) ||
                ((pname >= GL_CLIP_PLANE0) && (pname < GL_CLIP_PLANE0 + C3DGL_MAX_CLIP_PLANES)) ||
                (pname == GL_NORMALIZE) || (pname == GL_RESCALE_NORMAL) || (pname == GL_POINT_SPRITE_OES) ||
                ((pname >= GL_TEXTURE_GEN_S) && (pname <= GL_TEXTURE_GEN_Q)) ||
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
        case GL_EXTENSIONS:
            return (const GLubyte *)"GL_OES_point_sprite GL_OES_point_size_array GL_OES_compressed_paletted_texture "
                                    "GL_OES_compressed_ETC1_RGB8_texture";
        default: return (const GLubyte *)"";
    }
}

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    LIST_SAVE(VIEWPORT, "iiii", x, y, width, height);
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    gl.state.viewport[0] = x;
    gl.state.viewport[1] = y;
    gl.state.viewport[2] = width;
    gl.state.viewport[3] = height;
}

void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    LIST_SAVE(SCISSOR, "iiii", x, y, width, height);
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    gl.state.scissorBox[0] = x;
    gl.state.scissorBox[1] = y;
    gl.state.scissorBox[2] = width;
    gl.state.scissorBox[3] = height;
}

void glClearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    LIST_SAVE(CLEAR_COLOR, "ffff", red, green, blue, alpha);
    gl.clearColor = ((u32)colorByte(red) << 24) | ((u32)colorByte(green) << 16) | ((u32)colorByte(blue) << 8) | colorByte(alpha);
}

void glClearDepth(GLclampd depth)
{
    LIST_SAVE(CLEAR_DEPTH, "d", depth);
    gl.clearDepth = (depth < 0.0)? 0.0f : (depth > 1.0)? 1.0f : (float)depth;
}

void glClearStencil(GLint s)
{
    LIST_SAVE(CLEAR_STENCIL, "i", s);
    gl.clearStencil = (u8)s;
}

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
    LIST_SAVE(CLEAR, "u", mask);
    if (gl.renderMode != GL_RENDER) return;     // Like Mesa: feedback and selection draw nothing
    // Write masks apply to clears, glDrawBuffer(GL_NONE) clears no color
    bool color = (mask & GL_COLOR_BUFFER_BIT) && (gl.state.colorMask != 0) && (gl.drawBuffer != GL_NONE);
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
    flushVertexCache();

    // Clears run as memory fills outside the command list; split it so earlier draws stay before the clear. The split
    // part is flushed from the CPU cache: C3D_FrameEnd(GX_CMDLIST_FLUSH) (see suspendFrame()) flushes only the last part,
    // and the GPU locks up on a stale command list (real hardware only)
    if (gl.drawnThisFrame) C3D_FrameSplit(GX_CMDLIST_FLUSH);

    // D24S8: stencil in the top byte, depth reversed (see depthFunc())
    u32 depthStencil = ((u32)gl.clearStencil << 24) | (u32)((1.0f - gl.clearDepth)*0xFFFFFF);
    int bits = (color? C3D_CLEAR_COLOR : 0) | ((depth || stencil)? C3D_CLEAR_DEPTH : 0);
    C3D_RenderTargetClear(gl.targets[gl.screen], (C3D_ClearBits)bits, gl.clearColor, depthStencil);
}

void glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    LIST_SAVE(COLOR_MASK, "iiii", red, green, blue, alpha);
    gl.state.colorMask = (red? GPU_WRITE_RED : 0) | (green? GPU_WRITE_GREEN : 0) | (blue? GPU_WRITE_BLUE : 0) | (alpha? GPU_WRITE_ALPHA : 0);
}

void glDepthMask(GLboolean flag)
{
    LIST_SAVE(DEPTH_MASK, "i", flag);
    gl.state.depthMask = flag;
}

void glDepthFunc(GLenum func)
{
    LIST_SAVE(DEPTH_FUNC, "u", func);
    gl.state.depthFunc = func;
}

void glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
    LIST_SAVE(STENCIL_FUNC, "uiu", func, ref, mask);
    gl.state.stencilFunc = func;
    gl.state.stencilRef = (u8)((ref < 0)? 0 : (ref > 255)? 255 : ref);
    gl.state.stencilFuncMask = (u8)mask;
}

void glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
    LIST_SAVE(STENCIL_OP, "uuu", fail, zfail, zpass);
    gl.state.stencilFail = fail;
    gl.state.stencilDepthFail = zfail;
    gl.state.stencilPass = zpass;
}

void glStencilMask(GLuint mask)
{
    LIST_SAVE(STENCIL_MASK, "u", mask);
    gl.state.stencilWriteMask = (u8)mask;
}

void glAlphaFunc(GLenum func, GLclampf ref)
{
    LIST_SAVE(ALPHA_FUNC, "uf", func, ref);
    gl.state.alphaFunc = func;
    gl.state.alphaRef = colorByte(ref);
}

void glBlendFunc(GLenum sfactor, GLenum dfactor)
{
    LIST_SAVE(BLEND_FUNC, "uu", sfactor, dfactor);
    gl.state.blendSrc = sfactor;
    gl.state.blendDst = dfactor;
}

void glLogicOp(GLenum opcode)
{
    LIST_SAVE(LOGIC_OP, "u", opcode);
    if ((opcode < GL_CLEAR) || (opcode > GL_SET)) { setError(GL_INVALID_ENUM); return; }
    gl.state.logicOpMode = opcode;
}

void glSampleCoverage(GLclampf value, GLboolean invert)
{
    LIST_SAVE(SAMPLE_COVERAGE, "fi", value, invert);
    gl.sampleCoverage = (value < 0.0f)? 0.0f : (value > 1.0f)? 1.0f : value;
    gl.sampleCoverageInvert = invert;
}

void glCullFace(GLenum mode)
{
    LIST_SAVE(CULL_FACE, "u", mode);
    gl.state.cullFace = mode;
}

void glFrontFace(GLenum mode)
{
    LIST_SAVE(FRONT_FACE, "u", mode);
    gl.state.frontFace = mode;
}

void glPolygonMode(GLenum face, GLenum mode)
{
    LIST_SAVE(POLYGON_MODE, "uu", face, mode);
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
    LIST_SAVE(POLYGON_OFFSET, "ff", factor, units);
    gl.offsetFactor = factor;
    gl.offsetUnits = units;
}

void glDepthRange(GLclampd zNear, GLclampd zFar)
{
    LIST_SAVE(DEPTH_RANGE, "dd", zNear, zFar);
    gl.state.depthNear = (zNear < 0.0)? 0.0f : (zNear > 1.0)? 1.0f : (float)zNear;
    gl.state.depthFar = (zFar < 0.0)? 0.0f : (zFar > 1.0)? 1.0f : (float)zFar;
}

void glLineWidth(GLfloat width)
{
    LIST_SAVE(LINE_WIDTH, "f", width);
    gl.lineWidth = width;
}

void glPointSize(GLfloat size)
{
    LIST_SAVE(POINT_SIZE, "f", size);
    if (size <= 0.0f) { setError(GL_INVALID_VALUE); return; }
    gl.pointSize = size;
}

// glPointParameter (GL 1.4, ES 1.1)
void glPointParameterfv(GLenum pname, const GLfloat *params)
{
    LIST_SAVE(POINT_PARAMETER, "uF", pname, (pname == GL_POINT_DISTANCE_ATTENUATION)? 3 : 1, params);
    switch (pname)
    {
        case GL_POINT_SIZE_MIN: case GL_POINT_SIZE_MAX: case GL_POINT_FADE_THRESHOLD_SIZE:
            if (params[0] < 0.0f) { setError(GL_INVALID_VALUE); return; }
            if (pname == GL_POINT_SIZE_MIN) gl.pointSizeMin = params[0];
            else if (pname == GL_POINT_SIZE_MAX) gl.pointSizeMax = params[0];
            else gl.pointFadeThreshold = params[0];
            return;
        case GL_POINT_DISTANCE_ATTENUATION:
            memcpy(gl.pointAttenuation, params, sizeof(gl.pointAttenuation));
            return;
        default: setError(GL_INVALID_ENUM); return;
    }
}

void glPointParameterf(GLenum pname, GLfloat param)
{
    if (pname == GL_POINT_DISTANCE_ATTENUATION) { setError(GL_INVALID_ENUM); return; }     // Needs 3 values
    glPointParameterfv(pname, &param);
}

void glPointParameteri(GLenum pname, GLint param) { glPointParameterf(pname, (GLfloat)param); }

void glPointParameteriv(GLenum pname, const GLint *params)
{
    GLfloat f[3] = { (GLfloat)params[0], 0.0f, 0.0f };
    if (pname == GL_POINT_DISTANCE_ATTENUATION) { f[1] = (GLfloat)params[1]; f[2] = (GLfloat)params[2]; }
    glPointParameterfv(pname, f);
}

//----------------------------------------------------------------------------------
// OpenGL: matrices
//----------------------------------------------------------------------------------
void glMatrixMode(GLenum mode)
{
    LIST_SAVE(MATRIX_MODE, "u", mode);
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
    LIST_SAVE(PUSH_MATRIX, "");
    int *depth = &gl.stackDepth[matrixStack()];
    if (*depth + 1 >= C3DGL_MATRIX_STACK) { WARN_ONCE("Matrix stack overflow\n"); setError(GL_STACK_OVERFLOW); return; }

    gl.stack[matrixStack()][*depth + 1] = gl.stack[matrixStack()][*depth];
    (*depth)++;
}

void glPopMatrix(void)
{
    LIST_SAVE(POP_MATRIX, "");
    int *depth = &gl.stackDepth[matrixStack()];
    if (*depth == 0) { WARN_ONCE("Matrix stack underflow\n"); setError(GL_STACK_UNDERFLOW); return; }

    (*depth)--;
    matrixChanged();
}

void glLoadIdentity(void)
{
    LIST_SAVE(LOAD_IDENTITY, "");
    mat4Identity(currentMatrix());
    matrixChanged();
}

void glMultMatrixf(const GLfloat *m)
{
    LIST_SAVE(MULT_MATRIX, "F", 16, m);
    Mat4 mat;
    memcpy(mat.m, m, sizeof(mat.m));
    multCurrent(&mat);
}

void glMultMatrixd(const GLdouble *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = (float)m[i];
    glMultMatrixf(f);
}

void glLoadMatrixf(const GLfloat *m)
{
    LIST_SAVE(LOAD_MATRIX, "F", 16, m);
    memcpy(currentMatrix()->m, m, 16*sizeof(float));
    matrixChanged();
}

void glLoadMatrixd(const GLdouble *m)
{
    GLfloat f[16];
    for (int i = 0; i < 16; i++) f[i] = (float)m[i];
    glLoadMatrixf(f);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(TRANSLATE, "fff", x, y, z);
    Mat4 m;
    mat4Identity(&m);
    m.m[12] = x;
    m.m[13] = y;
    m.m[14] = z;
    multCurrent(&m);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(ROTATE, "ffff", angle, x, y, z);
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
    LIST_SAVE(SCALE, "fff", x, y, z);
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
    LIST_SAVE(ORTHO, "dddddd", left, right, bottom, top, zNear, zFar);
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
    LIST_SAVE(FRUSTUM, "dddddd", left, right, bottom, top, zNear, zFar);
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
    LIST_SAVE(BEGIN, "u", mode);
    gl.inBegin = beginPrimitive(mode);
}

void glEnd(void)
{
    LIST_SAVE(END, "");
    if (gl.inBegin) endPrimitive();
    gl.inBegin = false;
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    LIST_SAVE(VERTEX, "fff", x, y, z);
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
    // The batch of the current primitive needs it already
    if (gl.inBegin && (gl.renderMode == GL_RENDER)) prepareDraw(gl.batch.clipSpace, gl.primitive == GL_POINTS);
}

void glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q)
{
    LIST_SAVE(TEX_COORD, "ffff", s, t, r, q);
    gl.current.tex[0] = s;
    gl.current.tex[1] = t;
    gl.current.tex[2] = q;
    gl.current.texR = r;
    gl.currentTexR[0] = r;
    if (q != 1.0f) markTexQ();
}

void glEdgeFlag(GLboolean flag)
{
    LIST_SAVE(EDGE_FLAG, "i", flag);
    gl.currentEdge = flag;
}

void glEdgeFlagv(const GLboolean *flag) { glEdgeFlag(*flag); }

void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz)
{
    LIST_SAVE(NORMAL, "fff", nx, ny, nz);
    gl.currentNormal[0] = nx;
    gl.currentNormal[1] = ny;
    gl.currentNormal[2] = nz;
}

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    LIST_SAVE(COLOR, "iiii", red, green, blue, alpha);
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
    LIST_SAVE(LIGHT, "uuF", light, pname, lightParamCount(pname), p);
    Light *li = lightFor(light);
    if (li == NULL) return;
    litStateChanged();

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
    LIST_SAVE(LIGHT_MODEL, "uF", pname, lightModelParamCount(pname), p);
    litStateChanged();
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
    LIST_SAVE(MATERIAL, "uuF", face, pname, materialParamCount(pname), p);
    if (!faceValid(face) || (materialParamCount(pname) == 0)) { setError(GL_INVALID_ENUM); return; }
    if ((pname == GL_SHININESS) && ((p[0] < 0.0f) || (p[0] > 128.0f))) { setError(GL_INVALID_VALUE); return; }
    litStateChanged();

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
    LIST_SAVE(COLOR_MATERIAL, "uu", face, mode);
    litStateChanged();
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
// OpenGL: fog (rendered in applyState(), see updateFogLut())
//----------------------------------------------------------------------------------
// p holds 4 values for GL_FOG_COLOR, 1 otherwise; GL_FOG_MODE is the enum value
static void setFog(GLenum pname, const float *p)
{
    LIST_SAVE(FOG, "uF", pname, (pname == GL_FOG_COLOR)? 4 : 1, p);
    switch (pname)
    {
        case GL_FOG_MODE:
        {
            GLenum mode = (GLenum)p[0];
            if ((mode != GL_LINEAR) && (mode != GL_EXP) && (mode != GL_EXP2)) { setError(GL_INVALID_ENUM); return; }
            gl.fogMode = mode;
            break;
        }
        case GL_FOG_DENSITY:
            if (p[0] < 0.0f) { setError(GL_INVALID_VALUE); return; }
            gl.fogDensity = p[0];
            break;
        case GL_FOG_START: gl.fogStart = p[0]; break;
        case GL_FOG_END: gl.fogEnd = p[0]; break;
        case GL_FOG_INDEX: gl.fogIndex = p[0]; break;
        case GL_FOG_COLOR:      // Clamped, like all GLclampf colors
            for (int i = 0; i < 4; i++) gl.fogColor[i] = (p[i] < 0.0f)? 0.0f : (p[i] > 1.0f)? 1.0f : p[i];
            break;
        default: setError(GL_INVALID_ENUM); break;
    }
}

void glFogfv(GLenum pname, const GLfloat *params) { setFog(pname, params); }

void glFogf(GLenum pname, GLfloat param)
{
    if (pname == GL_FOG_COLOR) { setError(GL_INVALID_ENUM); return; }
    setFog(pname, &param);
}

void glFogiv(GLenum pname, const GLint *params)
{
    float f[4];
    if (pname == GL_FOG_COLOR) for (int i = 0; i < 4; i++) f[i] = (float)((2.0*params[i] + 1.0)/4294967295.0);
    else f[0] = (float)params[0];
    setFog(pname, f);
}

void glFogi(GLenum pname, GLint param)
{
    if (pname == GL_FOG_COLOR) { setError(GL_INVALID_ENUM); return; }
    glFogiv(pname, &param);
}

//----------------------------------------------------------------------------------
// OpenGL: user clip planes (clipped on the CPU, see clipPolygon())
//----------------------------------------------------------------------------------
static float *clipPlane(GLenum plane)
{
    if ((plane < GL_CLIP_PLANE0) || (plane >= GL_CLIP_PLANE0 + C3DGL_MAX_CLIP_PLANES)) { setError(GL_INVALID_ENUM); return NULL; }
    return gl.clipPlanes[plane - GL_CLIP_PLANE0];
}

// The plane is stored in eye coordinates: p_eye = p * M^-1 with the current modelview
static void setClipPlane(GLenum plane, const double equation[4])
{
    LIST_SAVE(CLIP_PLANE, "uD", plane, 4, equation);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    float *p = clipPlane(plane);
    if (p == NULL) return;

    Mat4 inv;
    mat4Invert(&gl.stack[0][gl.stackDepth[0]], &inv);
    for (int c = 0; c < 4; c++)
        p[c] = (float)(equation[0]*inv.m[c*4] + equation[1]*inv.m[c*4 + 1] + equation[2]*inv.m[c*4 + 2] + equation[3]*inv.m[c*4 + 3]);
    gl.clipObjectSerial = 0;
}

static const float *getClipPlane(GLenum plane)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return NULL; }
    return clipPlane(plane);
}

void glClipPlane(GLenum plane, const GLdouble *equation) { setClipPlane(plane, equation); }

void glGetClipPlane(GLenum plane, GLdouble *equation)
{
    const float *p = getClipPlane(plane);
    if (p != NULL) for (int i = 0; i < 4; i++) equation[i] = p[i];
}

//----------------------------------------------------------------------------------
// OpenGL: texture coordinate generation (glTexGen) of the active texture unit, see generateTexCoords()
//----------------------------------------------------------------------------------
static int texGenParamCount(GLenum pname) { return ((pname == GL_OBJECT_PLANE) || (pname == GL_EYE_PLANE))? 4 : 1; }

// p: 1 value for GL_TEXTURE_GEN_MODE, 4 for the planes. scalar: from glTexGen{i,f,d}, which only take the mode
static void setTexGen(GLenum coord, GLenum pname, const float *p, bool scalar)
{
    LIST_SAVE(TEX_GEN, "uuiF", coord, pname, (int)scalar, texGenParamCount(pname), p);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((coord < GL_S) || (coord > GL_Q)) { setError(GL_INVALID_ENUM); return; }
    int c = coord - GL_S;
    TexGenState *g = &gl.texGen[gl.activeTexture];
    gl.texGenSerial++;

    switch (pname)
    {
        case GL_TEXTURE_GEN_MODE:
        {
            GLenum mode = (GLenum)p[0];
            if ((mode != GL_OBJECT_LINEAR) && (mode != GL_EYE_LINEAR) && ((mode != GL_SPHERE_MAP) || (c >= 2)))
            {
                setError(GL_INVALID_ENUM);
                return;
            }
            g->mode[c] = mode;
            return;
        }
        case GL_OBJECT_PLANE:
            if (scalar) break;
            memcpy(g->objectPlane[c], p, sizeof(g->objectPlane[c]));
            return;
        case GL_EYE_PLANE:
        {
            if (scalar) break;
            // Stored in eye coordinates: p_eye = p * M^-1 with the current modelview, like clip planes
            Mat4 inv;
            mat4Invert(&gl.stack[0][gl.stackDepth[0]], &inv);
            for (int i = 0; i < 4; i++)
                g->eyePlane[c][i] = p[0]*inv.m[i*4] + p[1]*inv.m[i*4 + 1] + p[2]*inv.m[i*4 + 2] + p[3]*inv.m[i*4 + 3];
            return;
        }
        default: break;
    }
    setError(GL_INVALID_ENUM);
}

void glTexGenf(GLenum coord, GLenum pname, GLfloat param)
{
    const float p[4] = { param, 0.0f, 0.0f, 0.0f };     // Padded: a list records 4 values for the plane names
    setTexGen(coord, pname, p, true);
}

void glTexGeni(GLenum coord, GLenum pname, GLint param) { glTexGenf(coord, pname, (GLfloat)param); }
void glTexGend(GLenum coord, GLenum pname, GLdouble param) { glTexGenf(coord, pname, (GLfloat)param); }
void glTexGenfv(GLenum coord, GLenum pname, const GLfloat *params) { setTexGen(coord, pname, params, false); }

void glTexGeniv(GLenum coord, GLenum pname, const GLint *params)
{
    float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < texGenParamCount(pname); i++) p[i] = (float)params[i];
    setTexGen(coord, pname, p, false);
}

void glTexGendv(GLenum coord, GLenum pname, const GLdouble *params)
{
    float p[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < texGenParamCount(pname); i++) p[i] = (float)params[i];
    setTexGen(coord, pname, p, false);
}

// Returns the count, 0 on error
static int getTexGen(GLenum coord, GLenum pname, float v[4])
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return 0; }
    if ((coord < GL_S) || (coord > GL_Q)) { setError(GL_INVALID_ENUM); return 0; }
    int c = coord - GL_S;
    const TexGenState *g = &gl.texGen[gl.activeTexture];
    switch (pname)
    {
        case GL_TEXTURE_GEN_MODE: v[0] = g->mode[c]; return 1;
        case GL_OBJECT_PLANE: memcpy(v, g->objectPlane[c], 4*sizeof(float)); return 4;
        case GL_EYE_PLANE: memcpy(v, g->eyePlane[c], 4*sizeof(float)); return 4;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetTexGenfv(GLenum coord, GLenum pname, GLfloat *params)
{
    float v[4];
    int n = getTexGen(coord, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
}

void glGetTexGeniv(GLenum coord, GLenum pname, GLint *params)
{
    float v[4];
    int n = getTexGen(coord, pname, v);
    for (int i = 0; i < n; i++) params[i] = (GLint)lroundf(v[i]);
}

void glGetTexGendv(GLenum coord, GLenum pname, GLdouble *params)
{
    float v[4];
    int n = getTexGen(coord, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
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
static const GLenum pointSizeTypes[] = { GL_FLOAT, GL_FIXED, 0 };

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

void glPointSizePointerOES(GLenum type, GLsizei stride, const GLvoid *pointer)
{
    setArray(ARRAY_POINTSIZE, 1, type, stride, pointer, 1 << 1, pointSizeTypes);
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
        case GL_POINT_SIZE_ARRAY_POINTER_OES: *params = (GLvoid *)gl.arrays[ARRAY_POINTSIZE].pointer; break;
        case GL_FEEDBACK_BUFFER_POINTER: *params = gl.feedbackBuffer; break;
        case GL_SELECTION_BUFFER_POINTER: *params = gl.selectBuffer; break;
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
    if (a->type == GL_FLOAT) { memcpy(out, p, (size_t)a->size*sizeof(float)); return true; }

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
        if (unit == 0) v.texR = t[2];
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
    if (arrayActive(ARRAY_POINTSIZE))
    {
        float size[4];
        if (!readArray(&gl.arrays[ARRAY_POINTSIZE], index, size, false)) return;
        v.pointSize = fmaxf(size[0], 0.0f);
    }

    if (!arrayActive(ARRAY_VERTEX))
    {
        memcpy(gl.current.tex, v.tex, sizeof(v.tex));
        gl.current.texR = gl.currentTexR[0] = v.texR;
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
    if (gl.listCompiling) { listArrayElement(i); return; }
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
    if (gl.listCompiling)
    {
        // Compiled as glBegin, glArrayElement per vertex (dereferenced now), glEnd
        if (!arrayActive(ARRAY_VERTEX)) return;
        glBegin(mode);
        for (int i = 0; i < count; i++) listArrayElement(first + i);
        glEnd();
        return;
    }
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
    if (data == NULL) return;

    // In a display list: glBegin, glArrayElement per index (dereferenced now), glEnd, like glDrawArrays
    bool compile = gl.listCompiling;
    if (compile)
    {
        if (!arrayActive(ARRAY_VERTEX)) return;
        glBegin(mode);
    }
    else if (!arraysReady() || !beginPrimitive(mode)) return;

    for (int i = 0; i < count; i++)
    {
        const u8 *p = data + (size_t)i*indexSize;
        int index = 0;
        if (type == GL_UNSIGNED_SHORT) { GLushort v; memcpy(&v, p, 2); index = v; }
        else if (type == GL_UNSIGNED_INT) { GLuint v; memcpy(&v, p, 4); index = (int)v; }
        else index = *p;     // GL_UNSIGNED_BYTE, checked above
        if (compile) listArrayElement(index);
        else submitArrayVertex(index);
    }
    if (compile) glEnd();
    else endPrimitive();
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
// Client pixel images (texture images, glGetTexImage, glReadPixels): layout and conversion from and to RGBA
//----------------------------------------------------------------------------------
// Components of a color format: indices into r, g, b, a, 4 = luminance (r + g + b); count, 0 if not a color format
static int colorComponents(GLenum format, int comp[4])
{
    switch (format)
    {
        case GL_RGBA: comp[0] = 0; comp[1] = 1; comp[2] = 2; comp[3] = 3; return 4;
        case GL_RGB: comp[0] = 0; comp[1] = 1; comp[2] = 2; return 3;
        case GL_RED: comp[0] = 0; return 1;
        case GL_GREEN: comp[0] = 1; return 1;
        case GL_BLUE: comp[0] = 2; return 1;
        case GL_ALPHA: comp[0] = 3; return 1;
        case GL_LUMINANCE: comp[0] = 4; return 1;
        case GL_LUMINANCE_ALPHA: comp[0] = 4; comp[1] = 3; return 2;
        default: return 0;
    }
}

// Packed 16-bit types: valid format, 0 if the type is not packed
static GLenum packedFormat(GLenum type)
{
    switch (type)
    {
        case GL_UNSIGNED_SHORT_5_6_5: return GL_RGB;
        case GL_UNSIGNED_SHORT_4_4_4_4: case GL_UNSIGNED_SHORT_5_5_5_1: return GL_RGBA;
        default: return 0;
    }
}

// PICA format with the bit layout of a packed 16-bit type
static GPU_TEXCOLOR packedTexColor(GLenum type)
{
    return (type == GL_UNSIGNED_SHORT_5_6_5)? GPU_RGB565 : (type == GL_UNSIGNED_SHORT_5_5_5_1)? GPU_RGBA5551 : GPU_RGBA4;
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

// Color image format/type (GL 1.1 tables 3.5 and 3.8, plus the packed 16-bit types): components, bytes per element and
// per group (0 for GL_COLOR_INDEX bitmaps); the error of an invalid pair otherwise
static GLenum colorImageLayout(GLenum format, GLenum type, int *n, int *elemSize, int *groupSize)
{
    int comp[4];
    *n = (format == GL_COLOR_INDEX)? 1 : colorComponents(format, comp);
    if (*n == 0) return GL_INVALID_ENUM;
    if (type == GL_BITMAP)
    {
        *elemSize = *groupSize = 0;
        return (format == GL_COLOR_INDEX)? GL_NO_ERROR : GL_INVALID_ENUM;
    }

    GLenum packed = packedFormat(type);
    if (packed)
    {
        if (format != packed) return GL_INVALID_OPERATION;
        *elemSize = *groupSize = 2;
        return GL_NO_ERROR;
    }
    if ((typeSize(type) == 0) || (type == GL_DOUBLE) || (type == GL_FIXED)) return GL_INVALID_ENUM;
    *elemSize = typeSize(type);
    *groupSize = *n * *elemSize;
    return GL_NO_ERROR;
}

// Bytes per row of an image `width` groups wide (GL 1.1 section 3.6.3): GL_*_ROW_LENGTH groups if set, padded to the
// alignment unless the elements are larger than it
static size_t imageRowBytes(const PixelStore *ps, int width, int elemSize, int groupSize)
{
    size_t bytes = (size_t)((ps->rowLength > 0)? ps->rowLength : width)*groupSize;
    if (elemSize < ps->alignment) bytes = (bytes + ps->alignment - 1)/ps->alignment*ps->alignment;
    return bytes;
}

// One element of a color or depth image as a float (GL 1.1 table 2.9: integers normalized), not clamped
static float loadElementRaw(const u8 *src, GLenum type, bool swap)
{
    union { u8 b[4]; u8 ub; s8 sb; u16 us; s16 ss; u32 ui; s32 si; float f; } e;
    int size = typeSize(type);
    for (int i = 0; i < size; i++) e.b[i] = src[swap? size - 1 - i : i];
    float v;
    switch (type)
    {
        case GL_UNSIGNED_BYTE: v = e.ub/255.0f; break;
        case GL_BYTE: v = (2*e.sb + 1)/255.0f; break;
        case GL_UNSIGNED_SHORT: v = e.us/65535.0f; break;
        case GL_SHORT: v = (2*e.ss + 1)/65535.0f; break;
        case GL_UNSIGNED_INT: v = (float)(e.ui/4294967295.0); break;
        case GL_INT: v = (float)((2.0*e.si + 1.0)/4294967295.0); break;
        default: v = e.f; break;    // GL_FLOAT
    }
    return v;
}

// The same clamped to [0, 1]
static float loadElement(const u8 *src, GLenum type, bool swap)
{
    float v = loadElementRaw(src, type, swap);
    return !(v > 0.0f)? 0.0f : (v > 1.0f)? 1.0f : v;     // NaN: 0
}

// One element of a color index or stencil image (floats truncated)
static s64 loadIndex(const u8 *src, GLenum type, bool swap)
{
    union { u8 b[4]; u8 ub; s8 sb; u16 us; s16 ss; u32 ui; s32 si; float f; } e;
    int size = typeSize(type);
    for (int i = 0; i < size; i++) e.b[i] = src[swap? size - 1 - i : i];
    switch (type)
    {
        case GL_UNSIGNED_BYTE: return e.ub;
        case GL_BYTE: return e.sb;
        case GL_UNSIGNED_SHORT: return e.us;
        case GL_SHORT: return e.ss;
        case GL_UNSIGNED_INT: return e.ui;
        case GL_INT: return e.si;
        default: return (e.f > -2147483648.0f) && (e.f < 2147483648.0f)? (s64)e.f : 0;     // GL_FLOAT
    }
}

// Bit (x, y) of a bitmap laid out as ps describes (GL 1.1 section 3.6.4): rows of whole bytes padded to the alignment,
// the most significant bit first unless GL_UNPACK_LSB_FIRST
static bool bitmapBit(const u8 *data, const PixelStore *ps, int width, int x, int y)
{
    size_t rowBytes = ((size_t)((ps->rowLength > 0)? ps->rowLength : width) + 7)/8;
    rowBytes = (rowBytes + ps->alignment - 1)/ps->alignment*ps->alignment;
    int bit = ps->skipPixels + x;
    u8 b = data[(size_t)(ps->skipRows + y)*rowBytes + bit/8];
    return (ps->lsbFirst? (b >> (bit & 7)) : (b >> (7 - (bit & 7)))) & 1;
}

// A width x height bitmap laid out as ps describes, tightly packed (rows of whole bytes, most significant bit first)
static void packBitmap(const u8 *data, const PixelStore *ps, int width, int height, u8 *dst)
{
    size_t rowBytes = ((size_t)width + 7)/8;
    memset(dst, 0, rowBytes*height);
    for (int y = 0; y < height; y++)
        for (int x = 0; x < width; x++)
            if (bitmapBit(data, ps, width, x, y)) dst[(size_t)y*rowBytes + x/8] |= (u8)(0x80 >> (x & 7));
}

//----------------------------------------------------------------------------------
// Pixel transfer (GL 1.1 section 3.6.3): glPixelTransfer, glPixelMap
//----------------------------------------------------------------------------------
// Whether color images are changed by the transfer (scale/bias of R, G, B, A or GL_MAP_COLOR)
static bool colorTransferActive(void)
{
    const PixelTransfer *t = &gl.transfer;
    bool active = t->mapColor;
    for (int i = 0; i < 4; i++) active = active || (t->scale[i] != 1.0f) || (t->bias[i] != 0.0f);
    return active;
}

static bool depthTransferActive(void) { return (gl.transfer.scale[4] != 1.0f) || (gl.transfer.bias[4] != 0.0f); }

// RGBA components: scaled and biased, clamped to [0, 1], then looked up in the R_TO_R .. A_TO_A tables if GL_MAP_COLOR
static void transferColor(float c[4])
{
    const PixelTransfer *t = &gl.transfer;
    for (int i = 0; i < 4; i++)
    {
        float v = c[i]*t->scale[i] + t->bias[i];
        v = !(v > 0.0f)? 0.0f : (v > 1.0f)? 1.0f : v;
        if (t->mapColor)
        {
            const PixelMap *p = &gl.pixelMaps[PIXEL_MAP_R_TO_R + i];
            v = p->values[(int)lroundf(v*(float)(p->size - 1))];
        }
        c[i] = v;
    }
}

// Window depth: scaled and biased, clamped to [0, 1]
static float transferDepth(float d)
{
    d = d*gl.transfer.scale[4] + gl.transfer.bias[4];
    return !(d > 0.0f)? 0.0f : (d > 1.0f)? 1.0f : d;
}

// Entry `index` of a table looked up by index: wraps around its 2^n entries
static float mapEntry(int map, s64 index)
{
    const PixelMap *p = &gl.pixelMaps[map];
    return p->values[index & (p->size - 1)];
}

// Index arithmetic: shifted by GL_INDEX_SHIFT (left if positive), GL_INDEX_OFFSET added
static s64 shiftIndex(s64 index)
{
    int shift = gl.transfer.indexShift;
    if (shift > 0) index = (shift < 64)? (s64)((u64)index << shift) : 0;
    else if (shift < 0) index = (shift > -64)? (index >> -shift) : (index < 0)? -1 : 0;
    return index + gl.transfer.indexOffset;
}

// A color index to RGBA through the I_TO_R .. I_TO_A tables (RGBA mode: always, whatever GL_MAP_COLOR)
static void indexColor(s64 index, float c[4])
{
    index = shiftIndex(index);
    for (int i = 0; i < 4; i++) c[i] = mapEntry(PIXEL_MAP_I_TO_R + i, index);
}

// A stencil index: shifted and offset, then looked up in S_TO_S if GL_MAP_STENCIL
static s64 transferStencil(s64 s)
{
    s = shiftIndex(s);
    if (gl.transfer.mapStencil) s = llroundf(mapEntry(PIXEL_MAP_S_TO_S, s));
    return s;
}

// Store one element of type `type`: v is a normalized value in [0, 1] (GL 1.1 table 2.9 inverted), or an index
static void storeElement(u8 *dst, GLenum type, double v, bool index, bool swap)
{
    union { u8 b[4]; u8 ub; s8 sb; u16 us; s16 ss; u32 ui; s32 si; float f; } e;
    int size = typeSize(type);
    switch (type)
    {
        case GL_UNSIGNED_BYTE: e.ub = (u8)(index? v : lround(v*255.0)); break;
        case GL_BYTE: e.sb = (s8)(index? v : lround((v*255.0 - 1.0)/2.0)); break;
        case GL_UNSIGNED_SHORT: e.us = (u16)(index? v : lround(v*65535.0)); break;
        case GL_SHORT: e.ss = (s16)(index? v : lround((v*65535.0 - 1.0)/2.0)); break;
        case GL_UNSIGNED_INT: e.ui = (u32)(index? v : llround(v*4294967295.0)); break;
        case GL_INT: e.si = (s32)(index? v : llround((v*4294967295.0 - 1.0)/2.0)); break;
        default: e.f = (float)v; break;     // GL_FLOAT
    }
    for (int i = 0; i < size; i++) dst[i] = e.b[swap? size - 1 - i : i];
}

// Color image (a valid format/type, see colorImageLayout()) as w x h RGBA8 texels, rows from the bottom. GL 1.1 section
// 3.6.3: missing color components are 0, a missing alpha 1, luminance goes to R, G and B
static void unpackColorImage(const u8 *pixels, int w, int h, GLenum format, GLenum type, const PixelStore *ps, u8 *rgba)
{
    int comp[4], n, elemSize, groupSize;
    colorComponents(format, comp);
    colorImageLayout(format, type, &n, &elemSize, &groupSize);
    size_t rowBytes = imageRowBytes(ps, w, elemSize, groupSize);
    bool packed = packedFormat(type) != 0, swap = ps->swapBytes && (elemSize > 1);
    const u8 *image = pixels;
    pixels += (size_t)ps->skipRows*rowBytes + (size_t)ps->skipPixels*groupSize;

    // With the pixel transfer (always for color indices): components as floats, not clamped before it. Unsigned bytes
    // through 256-entry tables: the components are independent, missing ones transferred from their defaults
    bool index = (format == GL_COLOR_INDEX);
    if ((index || colorTransferActive()) && (type == GL_UNSIGNED_BYTE))
    {
        u8 lut[256][4], fill[4];
        float d[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        transferColor(d);
        for (int k = 0; k < 4; k++) fill[k] = colorByte(d[k]);
        for (int i = 0; i < 256; i++)
        {
            float c[4] = { i/255.0f, i/255.0f, i/255.0f, i/255.0f };
            if (index) indexColor(i, c);
            else transferColor(c);
            for (int k = 0; k < 4; k++) lut[i][k] = colorByte(c[k]);
        }
        for (int y = 0; y < h; y++)
        {
            const u8 *src = pixels + (size_t)y*rowBytes;
            u8 *dst = rgba + (size_t)y*w*4;
            for (int x = 0; x < w; x++, src += n, dst += 4)
            {
                if (index) { memcpy(dst, lut[src[0]], 4); continue; }
                memcpy(dst, fill, 4);
                for (int i = 0; i < n; i++)
                {
                    const u8 *e = lut[src[i]];
                    if (comp[i] == 4) { dst[0] = e[0]; dst[1] = e[1]; dst[2] = e[2]; }
                    else dst[comp[i]] = e[comp[i]];
                }
            }
        }
        return;
    }
    if (index || colorTransferActive())
    {
        for (int y = 0; y < h; y++)
        {
            for (int x = 0; x < w; x++)
            {
                const u8 *src = pixels + (size_t)y*rowBytes + (size_t)x*groupSize;
                float c[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
                if (index) indexColor((type == GL_BITMAP)? bitmapBit(image, ps, w, x, y) : loadIndex(src, type, swap), c);
                else
                {
                    if (packed)
                    {
                        int v[4];
                        unpack16(packedTexColor(type), swap? (u16)((src[0] << 8) | src[1]) : (u16)(src[0] | (src[1] << 8)), v);
                        for (int k = 0; k < 4; k++) c[k] = v[k]/255.0f;
                    }
                    else for (int i = 0; i < n; i++)
                    {
                        float v = loadElementRaw(src + i*elemSize, type, swap);
                        if (comp[i] == 4) c[0] = c[1] = c[2] = v;
                        else c[comp[i]] = v;
                    }
                    transferColor(c);
                }
                u8 *dst = rgba + ((size_t)y*w + x)*4;
                for (int k = 0; k < 4; k++) dst[k] = colorByte(c[k]);
            }
        }
        return;
    }

    for (int y = 0; y < h; y++)
    {
        const u8 *src = pixels + (size_t)y*rowBytes;
        if ((type == GL_UNSIGNED_BYTE) && ((format == GL_RGBA) || (format == GL_RGB)))     // The common cases
        {
            u8 *dst = rgba + (size_t)y*w*4;
            if (format == GL_RGBA) memcpy(dst, src, (size_t)w*4);
            else for (int x = 0; x < w; x++, src += 3, dst += 4) { dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 255; }
            continue;
        }
        for (int x = 0; x < w; x++, src += groupSize)
        {
            u8 *dst = rgba + ((size_t)y*w + x)*4;
            if (packed)
            {
                int c[4];
                unpack16(packedTexColor(type), swap? (u16)((src[0] << 8) | src[1]) : (u16)(src[0] | (src[1] << 8)), c);
                for (int k = 0; k < 4; k++) dst[k] = (u8)c[k];
                continue;
            }
            dst[0] = dst[1] = dst[2] = 0;
            dst[3] = 255;
            for (int i = 0; i < n; i++)
            {
                u8 c = (type == GL_UNSIGNED_BYTE)? src[i] : colorByte(loadElement(src + i*elemSize, type, swap));
                if (comp[i] == 4) dst[0] = dst[1] = dst[2] = c;
                else dst[comp[i]] = c;
            }
        }
    }
}

// Store an RGBA8 color as one group of a color image (a valid format/type); luminance is R + G + B, clamped (GL 1.1
// section 4.3.2)
static void storeColor(u8 *dst, const u8 rgba[4], GLenum format, GLenum type, bool swap)
{
    int comp[4], n = colorComponents(format, comp);
    int c[5] = { rgba[0], rgba[1], rgba[2], rgba[3], 0 };
    c[4] = (c[0] + c[1] + c[2] > 255)? 255 : c[0] + c[1] + c[2];
    if (packedFormat(type))
    {
        u16 v = pack16(packedTexColor(type), c);
        dst[swap? 1 : 0] = (u8)v;
        dst[swap? 0 : 1] = (u8)(v >> 8);
    }
    else if (type == GL_UNSIGNED_BYTE) for (int i = 0; i < n; i++) dst[i] = (u8)c[comp[i]];
    else for (int i = 0; i < n; i++) storeElement(dst + i*typeSize(type), type, c[comp[i]]/255.0, false, swap);
}

// storeColor() of float components in [0, 1] (after the pixel transfer)
static void storeColorf(u8 *dst, const float rgba[4], GLenum format, GLenum type, bool swap)
{
    if ((type == GL_UNSIGNED_BYTE) || packedFormat(type))
    {
        const u8 c[4] = { colorByte(rgba[0]), colorByte(rgba[1]), colorByte(rgba[2]), colorByte(rgba[3]) };
        storeColor(dst, c, format, type, swap);
        return;
    }
    int comp[4], n = colorComponents(format, comp);
    float l = rgba[0] + rgba[1] + rgba[2];
    for (int i = 0; i < n; i++)
        storeElement(dst + i*typeSize(type), type, (comp[i] == 4)? ((l > 1.0f)? 1.0f : l) : rgba[comp[i]], false, swap);
}

// Store an index as one element of type `type`, masked to the bits of table 4.6 (GL 1.1); GL_FLOAT takes it as it is
static void storeIndex(u8 *dst, GLenum type, s64 index, bool swap)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE: index &= 0xFF; break;
        case GL_BYTE: index &= 0x7F; break;
        case GL_UNSIGNED_SHORT: index &= 0xFFFF; break;
        case GL_SHORT: index &= 0x7FFF; break;
        case GL_UNSIGNED_INT: index &= 0xFFFFFFFF; break;
        case GL_INT: index &= 0x7FFFFFFF; break;
        default: break;     // GL_FLOAT
    }
    storeElement(dst, type, (double)index, true, swap);
}

// w x h RGBA8 texels into a color image (a valid format/type) laid out as ps describes
static void packColorImage(const u8 *rgba, int w, int h, GLenum format, GLenum type, const PixelStore *ps, u8 *pixels)
{
    int n, elemSize, groupSize;
    colorImageLayout(format, type, &n, &elemSize, &groupSize);
    size_t rowBytes = imageRowBytes(ps, w, elemSize, groupSize);
    bool swap = ps->swapBytes && (elemSize > 1);
    pixels += (size_t)ps->skipRows*rowBytes + (size_t)ps->skipPixels*groupSize;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            storeColor(pixels + (size_t)y*rowBytes + (size_t)x*groupSize, rgba + ((size_t)y*w + x)*4, format, type, swap);
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

// Binding of target on the active unit, NULL if target is not GL_TEXTURE_1D or GL_TEXTURE_2D
static GLuint *textureBinding(GLenum target)
{
    if (target == GL_TEXTURE_2D) return &gl.boundTexture[gl.activeTexture];
    if (target == GL_TEXTURE_1D) return &gl.boundTexture1D[gl.activeTexture];
    return NULL;
}

static Texture *boundTexture(GLenum target)
{
    GLuint *binding = textureBinding(target);
    if ((binding == NULL) || (*binding >= C3DGL_MAX_TEXTURES)) return NULL;
    return &gl.textures[textureSlot(target, *binding)];
}

// Error of a texture call on target that has no texture: unknown target (every target has its default texture)
static GLenum targetError(GLenum target) { return textureBinding(target)? GL_INVALID_OPERATION : GL_INVALID_ENUM; }

static GLuint textureSlotOf(const Texture *t) { return (GLuint)(t - gl.textures); }

// Size of a mipmap level (GL: halved, at least 1)
static int levelSize(int size, int level) { return (size >> level)? (size >> level) : 1; }

// Last level the storage of t can have (1D textures become as high as wide for their mip chain, see ensureMipmapStorage())
static int maxStoredLevel(const Texture *t)
{
    return C3D_TexCalcMaxLevel(t->tex.width, (t->target == GL_TEXTURE_1D)? t->tex.width : t->tex.height);
}

// 1D textures: copy texels x0..x0 + w - 1 of row 0 of stored `level` into all its other rows
static void replicateRows(Texture *t, int level, int x0, int w)
{
    if (t->target != GL_TEXTURE_1D) return;
    int texWidth, texHeight, bpp = t->format.bpp;
    u8 *data = levelData(t, level, &texWidth, &texHeight);
    for (int y = 1; y < texHeight; y++)
        for (int x = x0; x < x0 + w; x++)
            memcpy(data + tiledOffset(texWidth, texHeight, x, y, bpp), data + tiledOffset(texWidth, texHeight, x, 0, bpp), bpp);
}

// Texture about to change: submit pending vertices that use it and rebind it for the next draw
// (C3D_TexBind() only keeps a pointer, changes are not picked up otherwise)
static void textureModified(GLuint slot)
{
    bool used = false;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) used = used || (gl.batch.units[unit].texture == slot);
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
        t->complete = lv->defined && (lv->width == levelSize(base->width, l)) && (lv->height == levelSize(base->height, l)) &&
                      (lv->format == base->format) && (lv->base == base->base) && (lv->border == base->border);
    }
}

// Switch tex to a mip chain (down to 8x8), keeping level 0; false if the size has no levels below 8x8. A 1D texture
// becomes as high as it is wide, so that its levels go down to 8 texels in s
static bool ensureMipmapStorage(Texture *t)
{
    if (t->levels > 1) return true;
    if (maxStoredLevel(t) < 1) return false;

    bool oneD = (t->target == GL_TEXTURE_1D);
    C3D_Tex mip;
    if (!C3D_TexInitMipmap(&mip, t->tex.width, oneD? t->tex.width : t->tex.height, t->format.format))
    {
        LOG("Out of memory for mipmaps\n");
        setError(GL_OUT_OF_MEMORY);
        return false;
    }
    memset(mip.data, 0, C3D_TexCalcTotalSize(mip.size, mip.maxLevel));
    int bpp = t->format.bpp;
    if (oneD)   // Row 0 of level 0, replicated below
    {
        for (int x = 0; x < t->tex.width; x++)
            memcpy((u8 *)mip.data + tiledOffset(mip.width, mip.height, x, 0, bpp),
                   (u8 *)t->tex.data + tiledOffset(t->tex.width, t->tex.height, x, 0, bpp), bpp);
    }
    else memcpy(mip.data, t->tex.data, t->tex.size);    // Level 0 comes first in both

    if (gl.frameActive) deferTextureDelete(&t->tex);
    else C3D_TexDelete(&t->tex);
    t->tex = mip;
    t->levels = mip.maxLevel + 1;
    replicateRows(t, 0, 0, t->tex.width);
    return true;
}

static void flushTexture(Texture *t)
{
    GSPGPU_FlushDataCache(t->tex.data, C3D_TexCalcTotalSize(t->tex.size, t->levels - 1));
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
        lv->width = levelSize(base.width, l);
        lv->height = levelSize(base.height, l);
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
        replicateRows(t, l, 0, w);
    }
}

static void initTexture(Texture *t)
{
    memset(t, 0, sizeof(*t));
    t->used = true;
    t->minFilter = GL_NEAREST_MIPMAP_LINEAR;    // OpenGL defaults
    t->magFilter = GL_LINEAR;
    t->wrapS = t->wrapT = GL_REPEAT;
    t->priority = 1.0f;
}

void glGenTextures(GLsizei n, GLuint *textures)
{
    GLuint id = 1;
    for (int i = 0; i < n; i++)
    {
        while ((id < C3DGL_MAX_TEXTURES) && gl.textures[id].used) id++;
        if (id >= C3DGL_MAX_TEXTURES) { LOG("Out of texture ids\n"); setError(GL_OUT_OF_MEMORY); textures[i] = 0; continue; }

        initTexture(&gl.textures[id]);
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
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
        {
            if (gl.boundTexture[unit] == id) gl.boundTexture[unit] = 0;
            if (gl.boundTexture1D[unit] == id) gl.boundTexture1D[unit] = 0;
        }
    }
}

GLboolean glIsTexture(GLuint texture)
{
    return (texture > 0) && (texture < C3DGL_MAX_TEXTURES) && gl.textures[texture].used;
}

void glBindTexture(GLenum target, GLuint texture)
{
    LIST_SAVE(BIND_TEXTURE, "uu", target, texture);
    GLuint *binding = textureBinding(target);
    if (binding == NULL) { setError(GL_INVALID_ENUM); return; }

    // The first bind decides the texture's dimensionality; binding an unused name creates the texture
    if ((texture != 0) && (texture < C3DGL_MAX_TEXTURES))
    {
        Texture *t = &gl.textures[texture];
        if (!t->used) initTexture(t);
        if ((t->target != 0) && (t->target != target)) { setError(GL_INVALID_OPERATION); return; }
        t->target = target;
    }
    *binding = texture;
}

// Priorities are stored only, all textures are resident (PICA samples them from linear memory)
void glPrioritizeTextures(GLsizei n, const GLuint *textures, const GLclampf *priorities)
{
    if (gl.listCompiling)
    {
        // An invalid count is recorded without the arrays and fails again when the list is executed
        ListWord *w = listBegin(LIST_PRIORITIZE_TEXTURES, (n > 0)? 1 + 2*n : 1);
        if (w == NULL) return;
        w[0].i = n;
        for (int i = 0; i < n; i++) { w[1 + i].u = textures[i]; w[1 + n + i].f = priorities[i]; }
        listEnd();
        return;
    }
    if (n < 0) { setError(GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++)
    {
        // Unused names and 0 are ignored
        if (glIsTexture(textures[i])) gl.textures[textures[i]].priority = (priorities[i] < 0.0f)? 0.0f : (priorities[i] > 1.0f)? 1.0f : priorities[i];
    }
}

GLboolean glAreTexturesResident(GLsizei n, const GLuint *textures, GLboolean *residences)
{
    (void)residences;   // Left untouched when all are resident
    if (n < 0) { setError(GL_INVALID_VALUE); return GL_FALSE; }
    for (int i = 0; i < n; i++) if (!glIsTexture(textures[i])) { setError(GL_INVALID_VALUE); return GL_FALSE; }
    return GL_TRUE;
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
    LIST_SAVE(TEX_ENV, "uuif", target, pname, value, fvalue);
    if (target == GL_POINT_SPRITE_OES)
    {
        if (pname != GL_COORD_REPLACE_OES) { setError(GL_INVALID_ENUM); return; }
        if (value) gl.coordReplace |= 1u << gl.activeTexture;
        else gl.coordReplace &= ~(1u << gl.activeTexture);
        return;
    }
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
        LIST_SAVE(TEX_ENV_COLOR, "F", 4, params);
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
    if ((target == GL_POINT_SPRITE_OES) && (pname == GL_COORD_REPLACE_OES)) { v[0] = (gl.coordReplace >> gl.activeTexture) & 1; return 1; }
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
    LIST_SAVE(ACTIVE_TEXTURE, "u", texture);
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
    LIST_SAVE(MULTI_TEX_COORD, "uffff", target, s, t, r, q);
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

static float clamp01(float v) { return !(v > 0.0f)? 0.0f : (v > 1.0f)? 1.0f : v; }

// glTexParameter of the texture bound to target: v has 4 values for GL_TEXTURE_BORDER_COLOR, otherwise 1
static void setTexParameter(GLenum target, GLenum pname, const GLfloat *v)
{
    LIST_SAVE(TEX_PARAMETER, "uuF", target, pname, (pname == GL_TEXTURE_BORDER_COLOR)? 4 : 1, v);
    Texture *t = boundTexture(target);
    if (t == NULL) { setError(targetError(target)); return; }

    GLint param = (GLint)v[0];
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
                textureModified(textureSlotOf(t));
                generateMipmaps(t);
                flushTexture(t);
            }
            break;
        case GL_TEXTURE_PRIORITY: t->priority = clamp01(v[0]); return;
        case GL_TEXTURE_BORDER_COLOR: for (int i = 0; i < 4; i++) t->borderColor[i] = clamp01(v[i]); return;
        default: setError(GL_INVALID_ENUM); return;
    }

    if (t->loaded)
    {
        textureModified(textureSlotOf(t));
        applyTextureParams(t);
    }
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    if (pname == GL_TEXTURE_BORDER_COLOR) { setError(GL_INVALID_ENUM); return; }   // Vector only
    setTexParameter(target, pname, &param);
}

void glTexParameteri(GLenum target, GLenum pname, GLint param) { glTexParameterf(target, pname, (GLfloat)param); }
void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params) { setTexParameter(target, pname, params); }

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    // Integer colors map [0, INT_MAX] to [0, 1]
    GLfloat v[4] = { (GLfloat)params[0] };
    if (pname == GL_TEXTURE_BORDER_COLOR) for (int i = 0; i < 4; i++) v[i] = (GLfloat)params[i]/2147483647.0f;
    setTexParameter(target, pname, v);
}

// glGetTexParameter: values of the texture bound to the active unit; returns the count, 0 on error
static int getTexParameter(GLenum target, GLenum pname, float v[4])
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError(targetError(target)); return 0; }
    switch (pname)
    {
        case GL_TEXTURE_MIN_FILTER: v[0] = t->minFilter; return 1;
        case GL_TEXTURE_MAG_FILTER: v[0] = t->magFilter; return 1;
        case GL_TEXTURE_WRAP_S: v[0] = t->wrapS; return 1;
        case GL_TEXTURE_WRAP_T: v[0] = t->wrapT; return 1;
        case GL_GENERATE_MIPMAP: v[0] = t->generateMipmap; return 1;
        case GL_TEXTURE_PRIORITY: v[0] = t->priority; return 1;
        case GL_TEXTURE_RESIDENT: v[0] = GL_TRUE; return 1;
        case GL_TEXTURE_BORDER_COLOR: memcpy(v, t->borderColor, 4*sizeof(float)); return 4;
        default: setError(GL_INVALID_ENUM); return 0;
    }
}

void glGetTexParameteriv(GLenum target, GLenum pname, GLint *params)
{
    float v[4];
    int n = getTexParameter(target, pname, v);
    for (int i = 0; i < n; i++)
    {
        if (pname == GL_TEXTURE_BORDER_COLOR) params[i] = (GLint)(v[i]*2147483647.0);
        else params[i] = (GLint)lroundf(v[i]);
    }
}

void glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
    float v[4];
    int n = getTexParameter(target, pname, v);
    for (int i = 0; i < n; i++) params[i] = v[i];
}


// Size check shared by real and proxy textures: level, border and the image size without the border. Non-power-of-two
// sizes are accepted (padded internally)
static bool textureSizeValid(GLint level, GLsizei imageWidth, GLsizei imageHeight, GLint border)
{
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL) || ((border != 0) && (border != 1))) return false;
    return (imageWidth >= 0) && (imageHeight >= 0);
}

static bool textureSizeFits(GLint level, GLsizei imageWidth, GLsizei imageHeight)
{
    int max = C3DGL_MAX_TEXTURE_SIZE >> level, w = 1, h = 1;
    while (w < imageWidth) w <<= 1;
    while (h < imageHeight) h <<= 1;
    return (w <= max) && (h <= max);
}

// Base internal format of a texture internal format (GL 1.1 tables 3.15 and 3.16), 0 if it is not one
static GLenum baseInternalFormat(GLint internalformat)
{
    switch (internalformat)
    {
        case GL_ALPHA: return GL_ALPHA;
        case 1: case GL_LUMINANCE: return GL_LUMINANCE;
        case 2: case GL_LUMINANCE_ALPHA: return GL_LUMINANCE_ALPHA;
        case GL_INTENSITY: return GL_INTENSITY;
        case 3: case GL_RGB: case GL_R3_G3_B2: return GL_RGB;
        case 4: case GL_RGBA: return GL_RGBA;
        default: break;
    }
    if ((internalformat >= GL_ALPHA4) && (internalformat <= GL_ALPHA16)) return GL_ALPHA;
    if ((internalformat >= GL_LUMINANCE4) && (internalformat <= GL_LUMINANCE16)) return GL_LUMINANCE;
    if ((internalformat >= GL_LUMINANCE4_ALPHA4) && (internalformat <= GL_LUMINANCE16_ALPHA16)) return GL_LUMINANCE_ALPHA;
    if ((internalformat >= GL_INTENSITY4) && (internalformat <= GL_INTENSITY16)) return GL_INTENSITY;
    if ((internalformat >= GL_RGB4) && (internalformat <= GL_RGB16)) return GL_RGB;
    if ((internalformat >= GL_RGBA2) && (internalformat <= GL_RGBA16)) return GL_RGBA;
    return 0;
}

static bool unsizedFormat(GLint internalformat)
{
    return ((internalformat >= 1) && (internalformat <= 4)) || (internalformat == GL_ALPHA) || (internalformat == GL_LUMINANCE) ||
           (internalformat == GL_LUMINANCE_ALPHA) || (internalformat == GL_INTENSITY) || (internalformat == GL_RGB) ||
           (internalformat == GL_RGBA);
}

// PICA format storing internal format `internalformat` (base internal format `base`) loaded from data of `type`: the
// closest one for sized formats, intensity as LA8 (I, I); unsized RGB/RGBA keep packed 16-bit data in its format
static TexFormat texStorage(GLint internalformat, GLenum base, GLenum type)
{
    GLenum format = base, storeType = GL_UNSIGNED_BYTE;
    bool unsized = unsizedFormat(internalformat);
    if (base == GL_INTENSITY) format = GL_LUMINANCE_ALPHA;
    else if (base == GL_RGB)
    {
        if ((internalformat == GL_R3_G3_B2) || (internalformat == GL_RGB4) || (internalformat == GL_RGB5) ||
            (unsized && (type == GL_UNSIGNED_SHORT_5_6_5))) storeType = GL_UNSIGNED_SHORT_5_6_5;
    }
    else if (base == GL_RGBA)
    {
        if ((internalformat == GL_RGBA2) || (internalformat == GL_RGBA4)) storeType = GL_UNSIGNED_SHORT_4_4_4_4;
        else if (internalformat == GL_RGB5_A1) storeType = GL_UNSIGNED_SHORT_5_5_5_1;
        else if (unsized && (packedFormat(type) == GL_RGBA)) storeType = type;
    }
    TexFormat f;
    texFormat(format, storeType, &f);
    return f;
}

// First half of glTexImage1D/2D and glCompressedTexImage2D: records proxies, validates and (re)defines `level` of the
// bound texture, the image size given without the border. Returns the texture (NULL on errors and for proxies);
// *stored tells whether `level` has storage for its texels, which the caller then loads before finishTexImage()
static Texture *defineTexImage(GLenum target, GLint level, GLint internalformat, GLenum base, int imageWidth,
                               int imageHeight, GLint border, const TexFormat *f, bool *stored)
{
    *stored = false;

    // Proxy: only record whether the image would be accepted
    if ((target == GL_PROXY_TEXTURE_1D) || (target == GL_PROXY_TEXTURE_2D))
    {
        bool oneD = (target == GL_PROXY_TEXTURE_1D);
        ProxyLevel *p = oneD? &gl.proxy1D[level] : &gl.proxy2D[level];
        memset(p, 0, sizeof(*p));
        if (textureSizeFits(level, imageWidth, imageHeight))
        {
            p->width = imageWidth + 2*border;
            p->height = oneD? 1 : imageHeight + 2*border;
            p->border = border;
            p->internalFormat = internalformat;
            p->base = base;
            p->format = f->format;
        }
        return NULL;
    }

    Texture *t = boundTexture(target);
    if (t == NULL) { setError(targetError(target)); return NULL; }

    if (!textureSizeFits(level, imageWidth, imageHeight))
    {
        LOG("glTexImage: %ix%i exceeds the maximum of %i\n", imageWidth, imageHeight, C3DGL_MAX_TEXTURE_SIZE >> level);
        setError(GL_INVALID_VALUE);
        return NULL;
    }

    TexLevel lv = { true, imageWidth, imageHeight, border, internalformat, base, f->format };
    textureModified(textureSlotOf(t));

    if (level > 0)
    {
        if (!t->loaded) { WARN_ONCE("glTexImage: mipmap level before level 0, ignored\n"); return NULL; }
        t->level[level] = lv;

        // Stored if it fits the chain (sizes and format of level 0), levels below 8x8 only count for completeness
        bool matches = (lv.width == levelSize(t->width, level)) && (lv.height == levelSize(t->height, level)) &&
                       (f->format == t->format.format);
        *stored = matches && (level <= maxStoredLevel(t)) && ensureMipmapStorage(t);
        return t;
    }

    // Level 0: keep the storage (and the other levels) if only the content changes
    int texWidth = nextPow2(imageWidth), texHeight = nextPow2(imageHeight);
    bool same = t->loaded && (imageWidth == t->width) && (imageHeight == t->height) && (f->format == t->format.format);
    if (!same)
    {
        if (t->loaded)
        {
            if (gl.frameActive) deferTextureDelete(&t->tex);
            else C3D_TexDelete(&t->tex);
            t->loaded = false;
        }
        if (!C3D_TexInit(&t->tex, texWidth, texHeight, f->format))
        {
            LOG("glTexImage: out of memory for %ix%i texture\n", texWidth, texHeight);
            setError(GL_OUT_OF_MEMORY);
            return NULL;
        }
        memset(t->tex.data, 0, t->tex.size);
        memset(t->level, 0, sizeof(t->level));
        t->loaded = true;
        t->levels = 1;
        t->format = *f;
        t->width = imageWidth;
        t->height = imageHeight;
    }
    t->base = base;
    t->level[0] = lv;
    *stored = true;
    return t;
}

// Second half: mipmap generation, completeness, cache flush and sampler state after the texels of `level` arrived
static void finishTexImage(Texture *t, GLint level)
{
    if ((level == 0) && t->generateMipmap)
    {
        if (t->format.compressed) WARN_ONCE("GL_GENERATE_MIPMAP: not supported for ETC1 textures\n");
        else generateMipmaps(t);
    }
    updateCompleteness(t);
    flushTexture(t);
    applyTextureParams(t);
}

// Load a w x h color image (a valid format/type) into stored `level` at (x0, y0), converted to the texture's format and
// the base internal format `base` (GL 1.1 table 3.15: luminance and intensity take R); 1D textures get it in all rows
static void loadTexels(Texture *t, int level, int x0, int y0, int w, int h, GLenum format, GLenum type,
                       const GLvoid *pixels, const PixelStore *ps, GLenum base)
{
    // Already in the stored format: copied as it is
    TexFormat f;
    if (texFormat(format, type, &f) && (f.format == t->format.format) && (base != GL_INTENSITY) && !colorTransferActive())
    {
        transferPixels(t, level, x0, y0, w, h, (u8 *)pixels, ps, true);
        replicateRows(t, level, x0, w);
        return;
    }

    size_t count = (size_t)w*h;
    u8 *texels = malloc(count? count*4 : 1);
    if (texels == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    unpackColorImage(pixels, w, h, format, type, ps, texels);

    // RGBA to the stored layout, in place: a stored texel has at most 4 bytes, so texel i never overwrites a later one
    const TexFormat *s = &t->format;
    for (size_t i = 0; i < count; i++)
    {
        int c[4] = { texels[i*4], texels[i*4 + 1], texels[i*4 + 2], texels[i*4 + 3] };
        u8 *q = texels + i*s->bpp;
        if (s->packed16)
        {
            u16 v = pack16(s->format, c);
            memcpy(q, &v, 2);
        }
        else switch (s->format)
        {
            case GPU_RGBA8: q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; q[3] = c[3]; break;
            case GPU_RGB8: q[0] = c[0]; q[1] = c[1]; q[2] = c[2]; break;
            case GPU_LA8: q[0] = c[0]; q[1] = (base == GL_INTENSITY)? c[0] : c[3]; break;
            case GPU_L8: q[0] = c[0]; break;
            default: q[0] = c[3]; break;    // GPU_A8
        }
    }
    const PixelStore tight = { .alignment = 1 };
    transferPixels(t, level, x0, y0, w, h, texels, &tight, true);
    replicateRows(t, level, x0, w);
    free(texels);
}

// The texels of a w x h rectangle at (0, 0) of stored `level` as RGBA8 (malloc'ed), components assigned by the base
// internal format (GL 1.1 table 6.1: luminance and intensity to R)
static u8 *readTexels(Texture *t, int level, int w, int h, GLenum base)
{
    size_t count = (size_t)w*h;
    u8 *texels = malloc(count? count*4 : 1);
    if (texels == NULL) { setError(GL_OUT_OF_MEMORY); return NULL; }
    const PixelStore tight = { .alignment = 1 };
    transferPixels(t, level, 0, 0, w, h, texels, &tight, false);

    // Expanded in place from the back (a stored texel has at most 4 bytes)
    const TexFormat *s = &t->format;
    for (size_t i = count; i-- > 0;)
    {
        const u8 *q = texels + i*s->bpp;
        int c[4] = { 0, 0, 0, 255 };
        if (s->packed16)
        {
            u16 v;
            memcpy(&v, q, 2);
            unpack16(s->format, v, c);
        }
        else switch (s->format)
        {
            case GPU_RGBA8: c[0] = q[0]; c[1] = q[1]; c[2] = q[2]; c[3] = q[3]; break;
            case GPU_RGB8: c[0] = q[0]; c[1] = q[1]; c[2] = q[2]; break;
            case GPU_LA8: c[0] = q[0]; if (base != GL_INTENSITY) c[3] = q[1]; break;
            case GPU_L8: c[0] = q[0]; break;
            default: c[3] = q[0]; break;    // GPU_A8
        }
        for (int k = 0; k < 4; k++) texels[i*4 + k] = (u8)c[k];
    }
    return texels;
}

// glTexImage1D/2D: oneD for the 1D targets, whose image is one row with a border only left and right
static void texImage(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
                     GLenum format, GLenum type, const GLvoid *pixels, bool oneD)
{
    if (oneD? (target != GL_TEXTURE_1D) && (target != GL_PROXY_TEXTURE_1D) :
              (target != GL_TEXTURE_2D) && (target != GL_PROXY_TEXTURE_2D)) { setError(GL_INVALID_ENUM); return; }
    int imageWidth = width - 2*border, imageHeight = oneD? 1 : height - 2*border;
    if (!textureSizeValid(level, imageWidth, imageHeight, border)) { setError(GL_INVALID_VALUE); return; }
    GLenum base = baseInternalFormat(internalformat);
    if (base == 0) { LOG("glTexImage: internal format 0x%x not supported\n", internalformat); setError(GL_INVALID_VALUE); return; }
    int n, elemSize, groupSize;
    GLenum error = colorImageLayout(format, type, &n, &elemSize, &groupSize);
    if (error != GL_NO_ERROR) { LOG("glTexImage: format 0x%x/0x%x not supported\n", format, type); setError(error); return; }

    // Levels > 0 of an unsized internal format keep level 0's storage, so the chain stays in one format
    TexFormat f = texStorage(internalformat, base, type);
    const Texture *bound = boundTexture(target);
    if ((level > 0) && (bound != NULL) && bound->loaded && !bound->format.compressed && unsizedFormat(internalformat) &&
        (bound->base == base)) f = bound->format;

    bool stored;
    Texture *t = defineTexImage(target, level, internalformat, base, imageWidth, imageHeight, border, &f, &stored);
    if (t == NULL) return;

    // The border texels are not stored (PICA has no texture borders): skip them
    PixelStore ps = gl.unpack;
    if (border)
    {
        if (ps.rowLength == 0) ps.rowLength = width;
        if (!oneD) ps.skipRows += border;
        ps.skipPixels += border;
    }
    if (stored && (pixels != NULL)) loadTexels(t, level, 0, 0, imageWidth, imageHeight, format, type, pixels, &ps, base);
    finishTexImage(t, level);
}

void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (gl.listCompiling && (target != GL_PROXY_TEXTURE_2D))     // Proxies are executed immediately
    {
        const GLint args[8] = { (GLint)target, level, internalformat, width, height, border, (GLint)format, (GLint)type };
        bool sizeValid = textureSizeValid(level, width - 2*border, height - 2*border, border) &&
                         textureSizeFits(level, width - 2*border, height - 2*border);
        listSaveImage(LIST_TEX_IMAGE, args, width, height, sizeValid, false, pixels);
        return;
    }
    texImage(target, level, internalformat, width, height, border, format, type, pixels, false);
}

void glTexImage1D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels)
{
    if (gl.listCompiling && (target != GL_PROXY_TEXTURE_1D))
    {
        const GLint args[8] = { (GLint)target, level, internalformat, width, 1, border, (GLint)format, (GLint)type };
        bool sizeValid = textureSizeValid(level, width - 2*border, 1, border) && textureSizeFits(level, width - 2*border, 1);
        listSaveImage(LIST_TEX_IMAGE, args, width, 1, sizeValid, true, pixels);
        return;
    }
    texImage(target, level, internalformat, width, 1, border, format, type, pixels, true);
}

// Paletted format (GL_OES_compressed_paletted_texture): palette entries as GL format/type, index bits; false otherwise
static bool paletteFormat(GLenum internalformat, GLenum *format, GLenum *type, int *entrySize, int *indexBits)
{
    if ((internalformat < GL_PALETTE4_RGB8_OES) || (internalformat > GL_PALETTE8_RGB5_A1_OES)) return false;
    static const struct { GLenum format, type; int size; } entries[5] = {
        { GL_RGB, GL_UNSIGNED_BYTE, 3 }, { GL_RGBA, GL_UNSIGNED_BYTE, 4 }, { GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2 },
        { GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2 }, { GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2 },
    };
    int i = (int)(internalformat - GL_PALETTE4_RGB8_OES);
    *format = entries[i % 5].format;
    *type = entries[i % 5].type;
    *entrySize = entries[i % 5].size;
    *indexBits = (i < 5)? 4 : 8;
    return true;
}

// ETC1 blocks (8 bytes each, row-major from t = 0) into stored level `level`. PICA groups the 4x4 blocks into 8x8 tiles
// (Z order) and reads each block as a little-endian u64, so the bytes are reversed. Flipping the rows like the other
// formats would mean re-encoding the blocks, so they are stored upside down and the texture matrix flips t instead
static void uploadEtc1(Texture *t, int level, int width, int height, const u8 *data)
{
    int texWidth, texHeight;
    u8 *texData = levelData(t, level, &texWidth, &texHeight);
    int blocksX = (width + 3)/4, blocksY = (height + 3)/4;
    for (int by = 0; by < blocksY; by++)
    {
        for (int bx = 0; bx < blocksX; bx++)
        {
            int x = bx*4, y = by*4;
            u8 *dst = texData + ((y >> 3)*(texWidth >> 3) + (x >> 3))*32 + (((x & 4) >> 2) | ((y & 4) >> 1))*8;
            const u8 *src = data + ((size_t)by*blocksX + bx)*8;
            for (int i = 0; i < 8; i++) dst[i] = src[7 - i];
        }
    }
}

// Paletted image: the palette, then the indices of all levels (level 0 first, rows not padded, 4-bit indices high
// nibble first). Each level is expanded to its palette format and loaded like glTexImage2D. level <= 0: levels 0..-level
static void compressedPaletted(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                               GLsizei imageSize, const u8 *data)
{
    GLenum format = 0, type = 0;
    int entrySize = 0, indexBits = 0;
    paletteFormat(internalformat, &format, &type, &entrySize, &indexBits);

    int maxLevel = 0;
    while (((width >> maxLevel) > 1) || ((height >> maxLevel) > 1)) maxLevel++;
    if ((level > 0) || (-level > maxLevel)) { setError(GL_INVALID_VALUE); return; }

    size_t paletteSize = ((size_t)1 << indexBits)*entrySize, expected = paletteSize;
    for (int l = 0; l <= -level; l++)
    {
        size_t w = (width >> l)? (width >> l) : 1, h = (height >> l)? (height >> l) : 1;
        expected += (w*h*indexBits + 7)/8;
    }
    if ((size_t)imageSize != expected) { LOG("glCompressedTexImage2D: imageSize %i, expected %u\n", (int)imageSize, (unsigned)expected); setError(GL_INVALID_VALUE); return; }

    TexFormat f;
    texFormat(format, type, &f);
    u8 *pixels = NULL;
    if (data != NULL)
    {
        pixels = malloc((size_t)width*height*entrySize);
        if (pixels == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    }

    // The expanded texels are tightly packed
    PixelStore saved = gl.unpack;
    gl.unpack = (PixelStore){ .alignment = 1 };
    const u8 *indices = (data != NULL)? data + paletteSize : NULL;
    for (int l = 0; l <= -level; l++)
    {
        int w = (width >> l)? (width >> l) : 1, h = (height >> l)? (height >> l) : 1;
        bool stored;
        Texture *t = defineTexImage(target, l, internalformat, format, w, h, 0, &f, &stored);
        if (indices != NULL)
        {
            for (int i = 0; i < w*h; i++)
            {
                int index = (indexBits == 8)? indices[i] : (indices[i/2] >> ((i & 1)? 0 : 4)) & 15;
                memcpy(pixels + (size_t)i*entrySize, data + (size_t)index*entrySize, entrySize);
            }
            indices += ((size_t)w*h*indexBits + 7)/8;
            if ((t != NULL) && stored) transferPixels(t, l, 0, 0, w, h, pixels, &gl.unpack, true);
        }
        if (t != NULL) finishTexImage(t, l);
        else if (target != GL_PROXY_TEXTURE_2D) break;
    }
    gl.unpack = saved;
    free(pixels);
}

void glCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height,
                            GLint border, GLsizei imageSize, const GLvoid *data)
{
    if (gl.listCompiling && (target != GL_PROXY_TEXTURE_2D))     // Proxies are executed immediately
    {
        // The image is copied now. Sizes beyond any valid image are not: the call fails before reading it anyway
        bool captured = (data != NULL) && (imageSize >= 0) && (imageSize <= 2*C3DGL_MAX_TEXTURE_SIZE*C3DGL_MAX_TEXTURE_SIZE);
        ListWord *w = listBegin(LIST_COMPRESSED_TEX_IMAGE, 8 + (captured? (imageSize + 3)/4 : 0));
        if (w == NULL) return;
        const GLint args[8] = { (GLint)target, level, (GLint)internalformat, width, height, border, imageSize, captured };
        for (int i = 0; i < 8; i++) w[i].i = args[i];
        if (captured) memcpy(&w[8], data, (size_t)imageSize);
        listEnd();
        return;
    }
    if ((target != GL_TEXTURE_2D) && (target != GL_PROXY_TEXTURE_2D)) { setError(GL_INVALID_ENUM); return; }
    GLenum format, type;
    int entrySize, indexBits;
    bool paletted = paletteFormat(internalformat, &format, &type, &entrySize, &indexBits);
    if (!paletted && (internalformat != GL_ETC1_RGB8_OES)) { setError(GL_INVALID_ENUM); return; }

    // Compressed images have no border
    if ((border != 0) || (width < 0) || (height < 0) || (imageSize < 0)) { setError(GL_INVALID_VALUE); return; }
    if (paletted) { compressedPaletted(target, level, internalformat, width, height, imageSize, data); return; }

    // ETC1, sampled natively
    if (!textureSizeValid(level, width, height, 0)) { setError(GL_INVALID_VALUE); return; }
    if (imageSize != ((width + 3)/4)*((height + 3)/4)*8) { setError(GL_INVALID_VALUE); return; }
    TexFormat f = { GPU_ETC1, 0, false, false, true };
    bool stored;
    Texture *t = defineTexImage(target, level, internalformat, GL_RGB, width, height, 0, &f, &stored);
    if (t == NULL) return;
    if (stored && (data != NULL)) uploadEtc1(t, level, width, height, data);
    finishTexImage(t, level);
}

// Neither paletted nor ETC1 images can be updated in part (both extensions require GL_INVALID_OPERATION)
void glCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                               GLenum format, GLsizei imageSize, const GLvoid *data)
{
    LIST_SAVE(COMPRESSED_TEX_SUB_IMAGE, "uiiiiiui", target, level, xoffset, yoffset, width, height, format, imageSize);
    (void)level; (void)xoffset; (void)yoffset; (void)width; (void)height; (void)imageSize; (void)data;
    GLenum f, type;
    int entrySize, indexBits;
    if (target != GL_TEXTURE_2D) setError(GL_INVALID_ENUM);
    else if (paletteFormat(format, &f, &type, &entrySize, &indexBits) || (format == GL_ETC1_RGB8_OES)) setError(GL_INVALID_OPERATION);
    else setError(GL_INVALID_ENUM);
}

static void texSubImage(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                        GLenum format, GLenum type, const GLvoid *pixels)
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError(targetError(target)); return; }
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }
    const TexLevel *lv = &t->level[level];
    if (!t->loaded || !lv->defined || t->format.compressed) { setError(GL_INVALID_OPERATION); return; }
    int n, elemSize, groupSize;
    GLenum error = colorImageLayout(format, type, &n, &elemSize, &groupSize);
    if (error != GL_NO_ERROR) { LOG("glTexSubImage: format 0x%x/0x%x not supported\n", format, type); setError(error); return; }
    if ((width < 0) || (height < 0) || (xoffset < 0) || (yoffset < 0) || (xoffset + width > lv->width) ||
        (yoffset + height > lv->height))
    {
        setError(GL_INVALID_VALUE);
        return;
    }
    if (pixels == NULL) return;

    textureModified(textureSlotOf(t));
    if (level < t->levels) loadTexels(t, level, xoffset, yoffset, width, height, format, type, pixels, &gl.unpack, lv->base);
    if ((level == 0) && t->generateMipmap) generateMipmaps(t);
    flushTexture(t);
}

void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels)
{
    if (gl.listCompiling)
    {
        const GLint args[8] = { (GLint)target, level, xoffset, yoffset, width, height, (GLint)format, (GLint)type };
        bool sizeValid = (width >= 0) && (height >= 0) && (width <= C3DGL_MAX_TEXTURE_SIZE) && (height <= C3DGL_MAX_TEXTURE_SIZE);
        listSaveImage(LIST_TEX_SUB_IMAGE, args, width, height, sizeValid, false, pixels);
        return;
    }
    if (target != GL_TEXTURE_2D) { setError(GL_INVALID_ENUM); return; }
    texSubImage(target, level, xoffset, yoffset, width, height, format, type, pixels);
}

void glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width, GLenum format, GLenum type,
                     const GLvoid *pixels)
{
    if (gl.listCompiling)
    {
        const GLint args[8] = { (GLint)target, level, xoffset, 0, width, 1, (GLint)format, (GLint)type };
        bool sizeValid = (width >= 0) && (width <= C3DGL_MAX_TEXTURE_SIZE);
        listSaveImage(LIST_TEX_SUB_IMAGE, args, width, 1, sizeValid, true, pixels);
        return;
    }
    if (target != GL_TEXTURE_1D) { setError(GL_INVALID_ENUM); return; }
    texSubImage(target, level, xoffset, 0, width, 1, format, type, pixels);
}

// Any color format/type: texels in the stored layout are copied as they are, the others converted like glReadPixels
// (luminance = R + G + B) from the components of table 6.1, without the pixel transfer
void glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid *pixels)
{
    Texture *t = boundTexture(target);
    if (t == NULL) { setError(targetError(target)); return; }
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }
    if ((format == GL_COLOR_INDEX) || (type == GL_BITMAP)) { setError(GL_INVALID_ENUM); return; }
    int n, elemSize, groupSize;
    GLenum error = colorImageLayout(format, type, &n, &elemSize, &groupSize);
    if (error != GL_NO_ERROR) { setError(error); return; }
    if (!t->loaded || !t->level[level].defined) return;
    if (t->format.compressed) { LOG("glGetTexImage: ETC1 textures cannot be read back\n"); setError(GL_INVALID_OPERATION); return; }
    if (level >= t->levels) { WARN_ONCE("glGetTexImage: levels below 8x8 are not stored\n"); return; }

    const TexLevel *lv = &t->level[level];
    TexFormat f;
    if (texFormat(format, type, &f) && (f.format == t->format.format) && (lv->base != GL_INTENSITY))
    {
        transferPixels(t, level, 0, 0, lv->width, lv->height, (u8 *)pixels, &gl.pack, false);
        return;
    }
    u8 *texels = readTexels(t, level, lv->width, lv->height, lv->base);
    if (texels == NULL) return;
    packColorImage(texels, lv->width, lv->height, format, type, &gl.pack, (u8 *)pixels);
    free(texels);
}

// Bits per component of a PICA format holding base internal format `base`: R, G, B, A, L, I
static void formatBits(GPU_TEXCOLOR format, GLenum base, int bits[6])
{
    memset(bits, 0, 6*sizeof(int));
    if (base == GL_INTENSITY) { bits[5] = 8; return; }
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
        case GPU_ETC1: bits[0] = bits[1] = bits[2] = 8; break;
        default: break;
    }
}

void glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }

    // Width, height, border, internal format, format of the level; zero if it has no image
    GLint width = 0, height = 0, border = 0, internalFormat = 0;
    GLenum base = 0;
    GPU_TEXCOLOR format = 0;
    bool hasImage = false;
    switch (target)
    {
        case GL_TEXTURE_1D: case GL_TEXTURE_2D:
        {
            Texture *t = boundTexture(target);
            if ((t != NULL) && t->loaded && t->level[level].defined)
            {
                const TexLevel *lv = &t->level[level];
                width = lv->width + 2*lv->border;
                height = (target == GL_TEXTURE_1D)? 1 : lv->height + 2*lv->border;
                border = lv->border;
                internalFormat = lv->internalFormat;
                base = lv->base;
                format = lv->format;
                hasImage = true;
            }
            break;
        }
        case GL_PROXY_TEXTURE_1D: case GL_PROXY_TEXTURE_2D:
        {
            const ProxyLevel *p = (target == GL_PROXY_TEXTURE_1D)? &gl.proxy1D[level] : &gl.proxy2D[level];
            width = p->width;
            height = p->height;
            border = p->border;
            internalFormat = p->internalFormat;
            base = p->base;
            format = p->format;
            hasImage = (p->width > 0);
            break;
        }
        default: setError(GL_INVALID_ENUM); return;
    }

    int bits[6] = { 0 };
    if (hasImage) formatBits(format, base, bits);
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
        case GL_TEXTURE_INTENSITY_SIZE: *params = bits[5]; break;
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

// Store control points: uorder x vorder points of k components, read with the given strides.
// A display list gets the points packed (strides vorder*k and k); an invalid call is recorded without them and fails
// again, before reading them, when the list is executed
static void defineMap(GLenum target, double u1, double u2, int ustride, int uorder, double v1, double v2, int vstride,
                      int vorder, const void *points, bool isDouble, bool twoD)
{
    bool targetTwoD;
    struct EvalMap *m = mapForTarget(target, &targetTwoD);
    GLenum error = GL_NO_ERROR;
    int k = 0;
    if ((m == NULL) || (targetTwoD != twoD)) error = GL_INVALID_ENUM;
    else
    {
        k = mapComponents[m - (twoD? gl.map2 : gl.map1)];
        if ((uorder < 1) || (uorder > C3DGL_MAX_EVAL_ORDER) || (vorder < 1) || (vorder > C3DGL_MAX_EVAL_ORDER) ||
            (u1 == u2) || (twoD && (v1 == v2)) || (ustride < k) || (twoD && (vstride < k))) error = GL_INVALID_VALUE;
    }
    if (gl.listCompiling && (error != GL_NO_ERROR))
    {
        listSave(LIST_MAP, "uiddiiddiiF", target, twoD, u1, u2, ustride, uorder, v1, v2, vstride, vorder,
                 0, (const float *)NULL);
        return;
    }
    if (error != GL_NO_ERROR) { setError(error); return; }
    if (gl.inBegin && !gl.listCompiling) { setError(GL_INVALID_OPERATION); return; }

    float *data = malloc((size_t)uorder*vorder*k*sizeof(float));
    if (data == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    for (int i = 0; i < uorder; i++)
        for (int j = 0; j < vorder; j++)
            for (int c = 0; c < k; c++)
            {
                size_t src = (size_t)i*ustride + (size_t)j*vstride + c;
                data[(i*vorder + j)*k + c] = isDouble? (float)((const double *)points)[src] : ((const float *)points)[src];
            }

    if (gl.listCompiling)
    {
        listSave(LIST_MAP, "uiddiiddiiF", target, twoD, u1, u2, vorder*k, uorder, v1, v2, k, vorder, uorder*vorder*k, data);
        free(data);
        return;
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

    // Sum over v first: q_i = sum_j bv_j*p_ij (and r_i with the v derivative), then over u
    for (int c = 0; c < k; c++) out[c] = 0.0f;
    if (derivs) for (int c = 0; c < k; c++) du[c] = dv[c] = 0.0f;
    float su = derivs? 1.0f/(m->u2 - m->u1) : 0.0f, sv = derivs? 1.0f/(m->v2 - m->v1) : 0.0f;
    const float *p = m->points;
    for (int i = 0; i < m->uorder; i++)
    {
        float q[4] = { 0.0f, 0.0f, 0.0f, 0.0f }, r[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        for (int j = 0; j < m->vorder; j++, p += k)
        {
            for (int c = 0; c < k; c++) q[c] += bv[j]*p[c];
            if (derivs) for (int c = 0; c < k; c++) r[c] += dbv[j]*p[c];
        }
        for (int c = 0; c < k; c++) out[c] += bu[i]*q[c];
        if (!derivs) continue;
        float wu = dbu[i]*su, wv = bu[i]*sv;
        for (int c = 0; c < k; c++) { du[c] += wu*q[c]; dv[c] += wv*r[c]; }
    }
}

// Highest enabled texcoord map (GL: the one with the most components wins)
static int texcoordMap(const struct EvalMap *maps)
{
    for (int i = MAP_TEX4; i >= MAP_TEX1; i--) if (maps[i].enabled && maps[i].points) return i;
    return -1;
}

// An evaluated vertex before lighting, with its normal (evaluated or the current one)
typedef struct {
    Vertex v;
    float normal[3];
    bool valid;                 // False: w = 0, no vertex
} EvalVertex;

// Build the vertex for evaluated values. normal: evaluated normal, NULL: the current normal
static void buildEvaluated(EvalVertex *e, const float *pos, int posSize, const float *color, const float *tex,
                           int texSize, const float *normal)
{
    Vertex *v = &e->v;
    *v = gl.current;            // Values without a map come from the current state
    if (color != NULL) for (int c = 0; c < 4; c++) v->color[c] = colorByte(color[c]);
    if (tex != NULL)
    {
        v->tex[0] = tex[0];
        v->tex[1] = (texSize > 1)? tex[1] : 0.0f;
        v->tex[2] = (texSize > 3)? tex[3] : 1.0f;
        v->texR = (texSize > 2)? tex[2] : 0.0f;
        if (v->tex[2] != 1.0f) markTexQ();
    }
    memcpy(e->normal, (normal != NULL)? normal : gl.currentNormal, sizeof(e->normal));

    float w = (posSize == 4)? pos[3] : 1.0f;
    e->valid = (w != 0.0f);
    if (!e->valid) { WARN_ONCE("Evaluator: w = 0 (point at infinity) not supported\n"); return; }
    for (int c = 0; c < 3; c++) v->pos[c] = pos[c]/w;
}

static void submitEvaluated(const EvalVertex *e)
{
    if (!e->valid) return;
    Vertex v = e->v;            // Lighting replaces the color
    submitLitVertex(&v, e->normal, gl.currentEdge);
}

void glEvalCoord1f(GLfloat u)
{
    LIST_SAVE(EVAL_COORD1, "f", u);
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

    EvalVertex e;
    buildEvaluated(&e, pos, mapComponents[vertexMap], hasColor? color : NULL, (texMap >= 0)? tex : NULL,
                   mapComponents[texMap >= 0? texMap : 0], hasNormal? normal : NULL);
    submitEvaluated(&e);
}

// Evaluate the 2D maps at (u, v); false without a vertex map (no vertex, like GL)
static bool evalCoord2(float u, float v, EvalVertex *e)
{
    const struct EvalMap *maps = gl.map2;
    int vertexMap = (maps[MAP_VERTEX4].enabled && maps[MAP_VERTEX4].points)? MAP_VERTEX4 :
                    (maps[MAP_VERTEX3].enabled && maps[MAP_VERTEX3].points)? MAP_VERTEX3 : -1;
    if (vertexMap < 0) return false;

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

    buildEvaluated(e, pos, k, hasColor? color : NULL, (texMap >= 0)? tex : NULL, mapComponents[texMap >= 0? texMap : 0],
                   hasNormal? normal : NULL);
    return true;
}

void glEvalCoord2f(GLfloat u, GLfloat v)
{
    LIST_SAVE(EVAL_COORD2, "ff", u, v);
    EvalVertex e;
    if (gl.inBegin && evalCoord2(u, v, &e)) submitEvaluated(&e);
}

void glEvalCoord1d(GLdouble u) { glEvalCoord1f((float)u); }
void glEvalCoord1fv(const GLfloat *u) { glEvalCoord1f(u[0]); }
void glEvalCoord1dv(const GLdouble *u) { glEvalCoord1f((float)u[0]); }
void glEvalCoord2d(GLdouble u, GLdouble v) { glEvalCoord2f((float)u, (float)v); }
void glEvalCoord2fv(const GLfloat *u) { glEvalCoord2f(u[0], u[1]); }
void glEvalCoord2dv(const GLdouble *u) { glEvalCoord2f((float)u[0], (float)u[1]); }

void glMapGrid1f(GLint un, GLfloat u1, GLfloat u2)
{
    LIST_SAVE(MAP_GRID1, "iff", un, u1, u2);
    if (un <= 0) { setError(GL_INVALID_VALUE); return; }
    gl.grid1n = un;
    gl.grid1u1 = u1;
    gl.grid1u2 = u2;
}

void glMapGrid1d(GLint un, GLdouble u1, GLdouble u2) { glMapGrid1f(un, (float)u1, (float)u2); }

void glMapGrid2f(GLint un, GLfloat u1, GLfloat u2, GLint vn, GLfloat v1, GLfloat v2)
{
    LIST_SAVE(MAP_GRID2, "iffiff", un, u1, u2, vn, v1, v2);
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

// Evaluated at execution: a display list uses the grid of the time it is executed
void glEvalPoint1(GLint i)
{
    LIST_SAVE(EVAL_POINT1, "i", i);
    glEvalCoord1f(gridCoord(i, gl.grid1n, gl.grid1u1, gl.grid1u2));
}

void glEvalPoint2(GLint i, GLint j)
{
    LIST_SAVE(EVAL_POINT2, "ii", i, j);
    glEvalCoord2f(gridCoord(i, gl.grid2un, gl.grid2u1, gl.grid2u2), gridCoord(j, gl.grid2vn, gl.grid2v1, gl.grid2v2));
}

// Scratch grid of glEvalMesh2; false (the mesh is evaluated point by point instead) if it cannot grow
static bool reserveEvalGrid(int count)
{
    if (count <= gl.evalGridCapacity) return true;
    void *grid = realloc(gl.evalGrid, (size_t)count*sizeof(EvalVertex));
    if (grid == NULL) return false;
    gl.evalGrid = grid;
    gl.evalGridCapacity = count;
    return true;
}

void glEvalMesh1(GLenum mode, GLint i1, GLint i2)
{
    LIST_SAVE(EVAL_MESH1, "uii", mode, i1, i2);
    if ((mode != GL_POINT) && (mode != GL_LINE)) { setError(GL_INVALID_ENUM); return; }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    glBegin((mode == GL_POINT)? GL_POINTS : GL_LINE_STRIP);
    for (int i = i1; i <= i2; i++) glEvalPoint1(i);
    glEnd();
}

void glEvalMesh2(GLenum mode, GLint i1, GLint i2, GLint j1, GLint j2)
{
    LIST_SAVE(EVAL_MESH2, "uiiii", mode, i1, i2, j1, j2);
    if ((mode != GL_POINT) && (mode != GL_LINE) && (mode != GL_FILL)) { setError(GL_INVALID_ENUM); return; }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    // FILL and LINE use every grid point more than once: the grid is evaluated once up front (the same vertices
    // glEvalPoint2 gives, the maps cannot change during the mesh)
    int nu = i2 - i1 + 1, nv = j2 - j1 + 1;
    if ((mode != GL_POINT) && (nu > 0) && (nv > 0) && reserveEvalGrid(nu*nv))
    {
        EvalVertex *g = gl.evalGrid;
        for (int j = 0; j < nv; j++)
        {
            float v = gridCoord(j1 + j, gl.grid2vn, gl.grid2v1, gl.grid2v2);
            for (int i = 0; i < nu; i++)
                if (!evalCoord2(gridCoord(i1 + i, gl.grid2un, gl.grid2u1, gl.grid2u2), v, &g[j*nu + i])) return;
        }
        #define G(i, j) (&g[((j) - j1)*nu + (i) - i1])
        if (mode == GL_FILL)
        {
            for (int j = j1; j < j2; j++)
            {
                glBegin(GL_QUAD_STRIP);
                for (int i = i1; i <= i2; i++) { submitEvaluated(G(i, j)); submitEvaluated(G(i, j + 1)); }
                glEnd();
            }
        }
        else
        {
            for (int j = j1; j <= j2; j++)
            {
                glBegin(GL_LINE_STRIP);
                for (int i = i1; i <= i2; i++) submitEvaluated(G(i, j));
                glEnd();
            }
            for (int i = i1; i <= i2; i++)
            {
                glBegin(GL_LINE_STRIP);
                for (int j = j1; j <= j2; j++) submitEvaluated(G(i, j));
                glEnd();
            }
        }
        #undef G
        return;
    }

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
// 6.x). Groups of features that do not exist yet (stipple, pixel transfer, accumulation)
// save nothing so far. When a feature moves out of ignoredCaps, its state has to be added here.
//----------------------------------------------------------------------------------
typedef struct {
    GLuint id;                  // Texture bound to the unit (and target) at push time
    GLenum minFilter, magFilter, wrapS, wrapT;
    bool generateMipmap;
    float priority, borderColor[4];
} SavedTexParams;

typedef struct {
    GLbitfield mask;
    DrawState state;
    Vertex current;
    bool currentEdge;
    float currentNormal[3], currentTexR[C3DGL_TEXTURE_UNITS];
    float lineWidth, pointSize;
    float pointSizeMin, pointSizeMax, pointFadeThreshold, pointAttenuation[3];
    bool pointSprite;
    u8 coordReplace;
    GLenum shadeModel, polygonMode[2];
    bool offsetFill, offsetLine, offsetPoint;
    float offsetFactor, offsetUnits;
    u32 ignoredCaps, clearColor;
    float sampleCoverage;
    bool sampleCoverageInvert;
    float clearDepth;
    u8 clearStencil;
    bool texture1D[C3DGL_TEXTURE_UNITS], texture2D[C3DGL_TEXTURE_UNITS];
    SavedTexParams texParams[2][C3DGL_TEXTURE_UNITS];      // GL_TEXTURE_1D, GL_TEXTURE_2D
    TexGenState texGen[C3DGL_TEXTURE_UNITS];
    int activeTexture, matrixMode;
    bool map1Enabled[9], map2Enabled[9], autoNormal;
    int grid1n, grid2un, grid2vn;
    float grid[6];              // grid1u1, grid1u2, grid2u1, grid2u2, grid2v1, grid2v2
    LightingState lighting;
    bool lightingEnabled, colorMaterial, normalize, rescaleNormal;
    u8 lightEnabled;
    bool fog;
    GLenum fogMode;
    float fogDensity, fogStart, fogEnd, fogColor[4], fogIndex;
    float clipPlanes[C3DGL_MAX_CLIP_PLANES][4];
    u8 clipEnabled;
    GLuint listBase;
    GLenum drawBuffer, readBuffer;
    RasterState raster;
    float zoomX, zoomY;
    PixelTransfer transfer;
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
    LIST_SAVE(PUSH_ATTRIB, "u", mask);
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
    a->pointSizeMin = gl.pointSizeMin;
    a->pointSizeMax = gl.pointSizeMax;
    a->pointFadeThreshold = gl.pointFadeThreshold;
    memcpy(a->pointAttenuation, gl.pointAttenuation, sizeof(a->pointAttenuation));
    a->pointSprite = gl.pointSprite;
    a->coordReplace = gl.coordReplace;
    a->shadeModel = gl.shadeModel;
    memcpy(a->polygonMode, gl.polygonMode, sizeof(a->polygonMode));
    a->offsetFill = gl.offsetFill;
    a->offsetLine = gl.offsetLine;
    a->offsetPoint = gl.offsetPoint;
    a->offsetFactor = gl.offsetFactor;
    a->offsetUnits = gl.offsetUnits;
    a->ignoredCaps = gl.ignoredCaps;
    a->sampleCoverage = gl.sampleCoverage;
    a->sampleCoverageInvert = gl.sampleCoverageInvert;
    a->clearColor = gl.clearColor;
    a->clearDepth = gl.clearDepth;
    a->clearStencil = gl.clearStencil;
    memcpy(a->texture1D, gl.texture1D, sizeof(a->texture1D));
    memcpy(a->texture2D, gl.texture2D, sizeof(a->texture2D));
    memcpy(a->texGen, gl.texGen, sizeof(a->texGen));
    for (int unit = 0; unit < 2*C3DGL_TEXTURE_UNITS; unit++)
    {
        GLuint id = (unit < C3DGL_TEXTURE_UNITS)? gl.boundTexture1D[unit] : gl.boundTexture[unit - C3DGL_TEXTURE_UNITS];
        SavedTexParams *p = &a->texParams[unit/C3DGL_TEXTURE_UNITS][unit % C3DGL_TEXTURE_UNITS];
        p->id = id;
        if (id < C3DGL_MAX_TEXTURES)
        {
            const Texture *t = &gl.textures[textureSlot((unit < C3DGL_TEXTURE_UNITS)? GL_TEXTURE_1D : GL_TEXTURE_2D, id)];
            p->minFilter = t->minFilter;
            p->magFilter = t->magFilter;
            p->wrapS = t->wrapS;
            p->wrapT = t->wrapT;
            p->generateMipmap = t->generateMipmap;
            p->priority = t->priority;
            memcpy(p->borderColor, t->borderColor, sizeof(p->borderColor));
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
    a->fog = gl.fog;
    a->fogMode = gl.fogMode;
    a->fogDensity = gl.fogDensity;
    a->fogStart = gl.fogStart;
    a->fogEnd = gl.fogEnd;
    a->fogIndex = gl.fogIndex;
    memcpy(a->fogColor, gl.fogColor, sizeof(a->fogColor));
    memcpy(a->clipPlanes, gl.clipPlanes, sizeof(a->clipPlanes));
    a->clipEnabled = gl.clipEnabled;
    a->listBase = gl.listBase;
    a->drawBuffer = gl.drawBuffer;
    a->readBuffer = gl.readBuffer;
    a->raster = gl.raster;
    a->zoomX = gl.zoomX;
    a->zoomY = gl.zoomY;
    a->transfer = gl.transfer;
}

void glPopAttrib(void)
{
    LIST_SAVE(POP_ATTRIB, "");
    if (gl.attribDepth == 0) { setError(GL_STACK_UNDERFLOW); return; }

    const AttribState *a = &attribStack[--gl.attribDepth];
    litStateChanged();
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
        gl.raster = a->raster;
    }
    if (mask & GL_POINT_BIT)
    {
        gl.pointSize = a->pointSize;
        gl.pointSizeMin = a->pointSizeMin;
        gl.pointSizeMax = a->pointSizeMax;
        gl.pointFadeThreshold = a->pointFadeThreshold;
        memcpy(gl.pointAttenuation, a->pointAttenuation, sizeof(gl.pointAttenuation));
        gl.pointSprite = a->pointSprite;        // GL 2.0: point sprite state is in GL_POINT_BIT
        gl.coordReplace = a->coordReplace;
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
    if (mask & GL_FOG_BIT)
    {
        gl.fog = a->fog;
        gl.fogMode = a->fogMode;
        gl.fogDensity = a->fogDensity;
        gl.fogStart = a->fogStart;
        gl.fogEnd = a->fogEnd;
        gl.fogIndex = a->fogIndex;
        memcpy(gl.fogColor, a->fogColor, sizeof(gl.fogColor));
    }
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
        memcpy(gl.clipPlanes, a->clipPlanes, sizeof(gl.clipPlanes));
        gl.clipEnabled = a->clipEnabled;
        gl.clipObjectSerial = 0;
    }
    if (mask & GL_ENABLE_BIT)
    {
        st->alphaTest = sv->alphaTest;
        st->blend = sv->blend;
        st->logicOp = sv->logicOp;
        st->cull = sv->cull;
        st->depthTest = sv->depthTest;
        st->scissor = sv->scissor;
        st->stencilTest = sv->stencilTest;
        gl.offsetFill = a->offsetFill;
        gl.offsetLine = a->offsetLine;
        gl.offsetPoint = a->offsetPoint;
        memcpy(gl.texture1D, a->texture1D, sizeof(gl.texture1D));
        memcpy(gl.texture2D, a->texture2D, sizeof(gl.texture2D));
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) gl.texGen[unit].enabled = a->texGen[unit].enabled;
        gl.texGenSerial++;
        gl.lightingEnabled = a->lightingEnabled;
        gl.lightEnabled = a->lightEnabled;
        gl.colorMaterial = a->colorMaterial;
        gl.normalize = a->normalize;
        gl.rescaleNormal = a->rescaleNormal;
        gl.fog = a->fog;
        gl.pointSprite = a->pointSprite;
        gl.clipEnabled = a->clipEnabled;
        gl.clipObjectSerial = 0;
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
        st->logicOp = sv->logicOp;
        st->logicOpMode = sv->logicOpMode;
        st->colorMask = sv->colorMask;
        gl.clearColor = a->clearColor;
        gl.drawBuffer = a->drawBuffer;
        caps |= capBits((const GLenum[]){ GL_DITHER, GL_INDEX_LOGIC_OP }, 2);
    }
    if (mask & GL_MULTISAMPLE_BIT)
    {
        gl.sampleCoverage = a->sampleCoverage;
        gl.sampleCoverageInvert = a->sampleCoverageInvert;
        caps |= capBits((const GLenum[]){ GL_MULTISAMPLE, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_SAMPLE_ALPHA_TO_ONE,
                                          GL_SAMPLE_COVERAGE }, 4);
    }
    if (mask & GL_TEXTURE_BIT)
    {
        // Enables, environments, texgen, bindings and the active unit, then the parameters of the textures bound at
        // push time
        for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) st->units[unit].env = sv->units[unit].env;
        memcpy(gl.texture1D, a->texture1D, sizeof(gl.texture1D));
        memcpy(gl.texture2D, a->texture2D, sizeof(gl.texture2D));
        memcpy(gl.texGen, a->texGen, sizeof(gl.texGen));
        gl.texGenSerial++;
        for (int unit = 0; unit < 2*C3DGL_TEXTURE_UNITS; unit++)
        {
            // Textures deleted since the push are not bound again (nor recreated)
            const SavedTexParams *p = &a->texParams[unit/C3DGL_TEXTURE_UNITS][unit % C3DGL_TEXTURE_UNITS];
            GLenum target = (unit < C3DGL_TEXTURE_UNITS)? GL_TEXTURE_1D : GL_TEXTURE_2D;
            GLuint *binding = (unit < C3DGL_TEXTURE_UNITS)? &gl.boundTexture1D[unit] : &gl.boundTexture[unit - C3DGL_TEXTURE_UNITS];
            *binding = (p->id < C3DGL_MAX_TEXTURES) && gl.textures[p->id].used? p->id : 0;
            if (*binding != p->id) continue;

            GLuint slot = textureSlot(target, p->id);
            Texture *t = &gl.textures[slot];
            t->minFilter = p->minFilter;
            t->magFilter = p->magFilter;
            t->wrapS = p->wrapS;
            t->wrapT = p->wrapT;
            t->generateMipmap = p->generateMipmap;
            t->priority = p->priority;
            memcpy(t->borderColor, p->borderColor, sizeof(t->borderColor));
            if (t->loaded)
            {
                textureModified(slot);
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
    if (mask & GL_LIST_BIT) gl.listBase = a->listBase;
    if (mask & GL_PIXEL_MODE_BIT)
    {
        gl.readBuffer = a->readBuffer;
        gl.zoomX = a->zoomX;
        gl.zoomY = a->zoomY;
        gl.transfer = a->transfer;
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
// OpenGL: reading the framebuffer
//----------------------------------------------------------------------------------
#define READ_LINE_BYTES     (C3DGL_SCREEN_HEIGHT*4)

// citro3d only starts the GX queue in C3D_FrameEnd, so to get at the framebuffer a frame in progress is ended without
// presenting it (no target marked as used), which runs the draws so far, and begun again afterwards
static bool suspendFrame(bool used[C3DGL_SCREEN_COUNT])
{
    if (!gl.frameActive) return false;
    flushVertexCache();
    for (int i = 0; i < C3DGL_SCREEN_COUNT; i++) { used[i] = gl.targets[i]->used; gl.targets[i]->used = false; }
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    return true;
}

static void resumeFrame(bool suspended, const bool used[C3DGL_SCREEN_COUNT])
{
    if (!suspended) return;
    C3D_FrameBegin(0);
    C3D_FrameDrawOn(gl.targets[gl.screen]);
    for (int i = 0; i < C3DGL_SCREEN_COUNT; i++) gl.targets[i]->used = used[i];
    gl.batchValid = false;
}

// Copy framebuffer lines [line0, line0 + lines) of the current screen into linear memory (free with linearFree), both
// multiples of 8 (a line of tiles). Line x is window column x with 240 pixels from y = 0 to the top, 4 bytes each:
// color A, B, G, R; depth/stencil the D24S8 word (stored depth = 1 - window depth, stencil in the top byte).
// No frame may be in progress (see suspendFrame()); returns once the GPU and the transfer are done
static u8 *readLines(bool depthStencil, int line0, int lines)
{
    size_t size = (size_t)lines*READ_LINE_BYTES;
    u8 *out = linearAlloc(size);
    if (out == NULL) { LOG("Out of memory for reading pixels\n"); setError(GL_OUT_OF_MEMORY); return NULL; }
    GSPGPU_FlushDataCache(out, size);       // No dirty cache lines may be written back over the transfer

    const C3D_FrameBuf *fb = &gl.targets[gl.screen]->frameBuf;
    u8 *in = (u8 *)(depthStencil? fb->depthBuf : fb->colorBuf) + (size_t)line0*READ_LINE_BYTES;
    u32 dim = GX_BUFFER_DIM(C3DGL_SCREEN_HEIGHT, lines);
    C3D_SyncDisplayTransfer((u32 *)in, dim, (u32 *)out, dim, DISPLAY_TRANSFER_FLAGS | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
    GSPGPU_InvalidateDataCache(out, size);
    return out;
}

static u8 *readFramebuffer(bool depthStencil, int line0, int lines)
{
    bool used[C3DGL_SCREEN_COUNT], suspended = suspendFrame(used);
    u8 *out = readLines(depthStencil, line0, lines);
    resumeFrame(suspended, used);
    return out;
}

// The reverse of readLines() for the depth/stencil buffer (a display transfer from linear to tiled), no frame in progress
static void writeDepthStencilLines(u8 *lineData, int line0, int lines)
{
    GSPGPU_FlushDataCache(lineData, (size_t)lines*READ_LINE_BYTES);
    u8 *out = (u8 *)gl.targets[gl.screen]->frameBuf.depthBuf + (size_t)line0*READ_LINE_BYTES;
    u32 dim = GX_BUFFER_DIM(C3DGL_SCREEN_HEIGHT, lines);
    C3D_SyncDisplayTransfer((u32 *)lineData, dim, (u32 *)out, dim,
                           GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
                           GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
                           GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
}

void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }

    int comp[4], n = colorComponents(format, comp);
    bool depth = (format == GL_DEPTH_COMPONENT), stencil = (format == GL_STENCIL_INDEX);
    if (stencil || depth) n = 1;
    else if (format == GL_COLOR_INDEX) { setError(GL_INVALID_OPERATION); return; }    // RGBA framebuffer only
    else if (n == 0) { setError(GL_INVALID_ENUM); return; }

    GLenum packed = packedFormat(type);
    bool bitmap = (type == GL_BITMAP);
    if (!packed && !bitmap && ((typeSize(type) == 0) || (type == GL_DOUBLE) || (type == GL_FIXED))) { setError(GL_INVALID_ENUM); return; }
    if (bitmap && !stencil) { setError(GL_INVALID_ENUM); return; }
    if (packed && (format != packed)) { setError(GL_INVALID_OPERATION); return; }

    // Window rectangle; pixels outside the window are undefined in GL and left untouched
    int x0 = (x < 0)? 0 : x, x1 = x + width, y0 = (y < 0)? 0 : y, y1 = y + height;
    if (x1 > screenWidth(gl.screen)) x1 = screenWidth(gl.screen);
    if (y1 > C3DGL_SCREEN_HEIGHT) y1 = C3DGL_SCREEN_HEIGHT;
    if ((pixels == NULL) || (x0 >= x1) || (y0 >= y1)) return;

    int line0 = x0 & ~7;
    u8 *fb = readFramebuffer(depth || stencil, line0, ((x1 + 7) & ~7) - line0);
    if (fb == NULL) return;

    // Pack layout (GL 1.1 section 3.6.4, applied to packing): rows of rowLength groups, padded to the alignment
    // when the element size is smaller than it
    const PixelStore *ps = &gl.pack;
    int elemSize = packed? 2 : bitmap? 1 : typeSize(type), groupSize = packed? 2 : n*elemSize;
    size_t rowGroups = (size_t)((ps->rowLength > 0)? ps->rowLength : width);
    size_t rowBytes = bitmap? (rowGroups + 7)/8 : rowGroups*groupSize;
    if (bitmap || (elemSize < ps->alignment)) rowBytes = (rowBytes + ps->alignment - 1)/ps->alignment*ps->alignment;
    bool swap = ps->swapBytes && (elemSize > 1);
    bool transferActive = colorTransferActive(), transferDepthActive = depthTransferActive();

    u8 *base = (u8 *)pixels + (size_t)ps->skipRows*rowBytes;
    for (int wy = y0; wy < y1; wy++)
    {
        u8 *row = base + (size_t)(wy - y)*rowBytes;
        for (int wx = x0; wx < x1; wx++)
        {
            const u8 *p = fb + (size_t)(wx - line0)*READ_LINE_BYTES + (size_t)wy*4;
            int col = ps->skipPixels + (wx - x);

            if (bitmap)     // Stencil bit 0, MSB first unless GL_PACK_LSB_FIRST
            {
                u8 bit = ps->lsbFirst? (u8)(1 << (col & 7)) : (u8)(0x80 >> (col & 7));
                if (transferStencil(p[3]) & 1) row[col/8] |= bit;
                else row[col/8] &= (u8)~bit;
                continue;
            }

            u8 *dst = row + (size_t)col*groupSize;
            if (stencil) { storeIndex(dst, type, transferStencil(p[3]), swap); continue; }
            if (depth)
            {
                u32 d = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
                double z = 1.0 - d/16777215.0;
                storeElement(dst, type, transferDepthActive? transferDepth((float)z) : z, false, swap);
                continue;
            }

            if (transferActive)
            {
                float c[4] = { p[3]/255.0f, p[2]/255.0f, p[1]/255.0f, p[0]/255.0f };
                transferColor(c);
                storeColorf(dst, c, format, type, swap);
                continue;
            }
            const u8 rgba[4] = { p[3], p[2], p[1], p[0] };
            storeColor(dst, rgba, format, type, swap);
        }
    }
    linearFree(fb);
}

//----------------------------------------------------------------------------------
// OpenGL: copying the framebuffer into textures
//----------------------------------------------------------------------------------
// Window rectangle as RGBA8 texels (malloc'ed), read like glReadPixels (pixel transfer included): a frame in progress is
// ended, so draws issued before the copy still see the old texels. Pixels outside the window are 0
static u8 *copyPixels(GLint x, GLint y, GLsizei width, GLsizei height)
{
    size_t count = (size_t)width*height;
    u8 *pixels = calloc(count? count : 1, 4);
    if (pixels == NULL) { setError(GL_OUT_OF_MEMORY); return NULL; }

    PixelStore saved = gl.pack;
    gl.pack = (PixelStore){ .alignment = 1 };
    glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl.pack = saved;
    return pixels;
}

// glCopyTexImage1D/2D: the texels are converted to the internal format like an RGBA image, so luminance and intensity
// are R (glReadPixels sums R + G + B)
static void copyTexImage(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height,
                         GLint border, bool oneD)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (target != (oneD? GL_TEXTURE_1D : GL_TEXTURE_2D)) { setError(GL_INVALID_ENUM); return; }

    // All internal formats but 1..4, all are in the RGBA framebuffer
    if ((baseInternalFormat(internalformat) == 0) || (internalformat <= 4))
    {
        LOG("glCopyTexImage: internal format 0x%x not supported\n", internalformat);
        setError(GL_INVALID_ENUM);
        return;
    }
    int imageWidth = width - 2*border, imageHeight = oneD? 1 : height - 2*border;
    if (!textureSizeValid(level, imageWidth, imageHeight, border)) { setError(GL_INVALID_VALUE); return; }
    if (boundTexture(target) == NULL) { setError(GL_INVALID_OPERATION); return; }
    if (!textureSizeFits(level, imageWidth, imageHeight)) { setError(GL_INVALID_VALUE); return; }

    u8 *pixels = copyPixels(x, y, width, oneD? 1 : height);
    if (pixels == NULL) return;
    PixelStore saved = gl.unpack;
    PixelTransfer savedTransfer = gl.transfer;      // Applied once, by copyPixels()
    gl.unpack = (PixelStore){ .alignment = 1 };
    gl.transfer = noTransfer;
    texImage(target, level, (GLint)internalformat, width, oneD? 1 : height, border, GL_RGBA, GL_UNSIGNED_BYTE, pixels, oneD);
    gl.unpack = saved;
    gl.transfer = savedTransfer;
    free(pixels);
}

static void copyTexSubImage(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                            GLsizei height, bool oneD)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (target != (oneD? GL_TEXTURE_1D : GL_TEXTURE_2D)) { setError(GL_INVALID_ENUM); return; }
    Texture *t = boundTexture(target);
    if (t == NULL) { setError(GL_INVALID_OPERATION); return; }
    if ((level < 0) || (level > MAX_TEXTURE_LEVEL)) { setError(GL_INVALID_VALUE); return; }
    const TexLevel *lv = &t->level[level];
    if (!t->loaded || !lv->defined || t->format.compressed) { setError(GL_INVALID_OPERATION); return; }
    if ((width < 0) || (height < 0) || (xoffset < 0) || (yoffset < 0) || (xoffset + width > lv->width) ||
        (yoffset + height > lv->height))
    {
        setError(GL_INVALID_VALUE);
        return;
    }

    // The texels keep the texture's format
    u8 *pixels = copyPixels(x, y, width, height);
    if (pixels == NULL) return;
    PixelStore saved = gl.unpack;
    PixelTransfer savedTransfer = gl.transfer;
    gl.unpack = (PixelStore){ .alignment = 1 };
    gl.transfer = noTransfer;
    texSubImage(target, level, xoffset, yoffset, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    gl.unpack = saved;
    gl.transfer = savedTransfer;
    free(pixels);
}

void glCopyTexImage2D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border)
{
    LIST_SAVE(COPY_TEX_IMAGE, "uiuiiiiii", target, level, internalformat, x, y, width, height, border, 0);
    copyTexImage(target, level, internalformat, x, y, width, height, border, false);
}

void glCopyTexImage1D(GLenum target, GLint level, GLenum internalformat, GLint x, GLint y, GLsizei width, GLint border)
{
    LIST_SAVE(COPY_TEX_IMAGE, "uiuiiiiii", target, level, internalformat, x, y, width, 1, border, 1);
    copyTexImage(target, level, internalformat, x, y, width, 1, border, true);
}

void glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint x, GLint y, GLsizei width,
                         GLsizei height)
{
    LIST_SAVE(COPY_TEX_SUB_IMAGE, "uiiiiiiii", target, level, xoffset, yoffset, x, y, width, height, 0);
    copyTexSubImage(target, level, xoffset, yoffset, x, y, width, height, false);
}

void glCopyTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLint x, GLint y, GLsizei width)
{
    LIST_SAVE(COPY_TEX_SUB_IMAGE, "uiiiiiiii", target, level, xoffset, 0, x, y, width, 1, 1);
    copyTexSubImage(target, level, xoffset, 0, x, y, width, 1, true);
}

//----------------------------------------------------------------------------------
// OpenGL: drawing pixels (GL). glRasterPos sets a window position, glDrawPixels, glBitmap and glCopyPixels draw rectangles
// there. Color images and bitmaps become textures in per-frame linear memory (see allocPixelMemory()) and are drawn as
// quads in window coordinates through the normal fragment pipeline (drawPixelRect()); texturing does not apply to them.
// PICA cannot output a per-pixel depth, so depth and stencil images are written on the CPU (drawDepthStencil())
//----------------------------------------------------------------------------------
// GL 1.1 section 2.12: transformed and clipped like a point; a clipped raster position is invalid
static void setRasterPos(float x, float y, float z, float w)
{
    LIST_SAVE(RASTER_POS, "ffff", x, y, z, w);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }

    const float *mv = gl.stack[0][gl.stackDepth[0]].m, *proj = gl.stack[1][gl.stackDepth[1]].m;
    float eye[4], clip[4];
    for (int r = 0; r < 4; r++) eye[r] = mv[r]*x + mv[4 + r]*y + mv[8 + r]*z + mv[12 + r]*w;
    for (int r = 0; r < 4; r++) clip[r] = proj[r]*eye[0] + proj[4 + r]*eye[1] + proj[8 + r]*eye[2] + proj[12 + r]*eye[3];

    bool inside = clip[3] > 0.0f;
    for (int i = 0; inside && (i < 3); i++) if (fabsf(clip[i]) > clip[3]) inside = false;
    for (int i = 0; inside && (i < C3DGL_MAX_CLIP_PLANES); i++)
    {
        const float *p = gl.clipPlanes[i];
        if ((gl.clipEnabled & (1u << i)) && (p[0]*eye[0] + p[1]*eye[1] + p[2]*eye[2] + p[3]*eye[3] < 0.0f)) inside = false;
    }
    if (!inside) { gl.raster.valid = false; return; }

    windowCoords(clip, gl.raster.pos);

    // Float matrices put integer positions slightly off (glOrtho(0, 400, ...) maps x = 210 to 209.99998), which
    // glBitmap's floor(x - xorig) would move by a whole pixel: snap what is within 1/1024 of an integer
    for (int i = 0; i < 2; i++)
    {
        float r = roundf(gl.raster.pos[i]);
        if (fabsf(gl.raster.pos[i] - r) < 1.0f/1024.0f) gl.raster.pos[i] = r;
    }
    gl.raster.pos[3] = clip[3];
    gl.raster.valid = true;
    if (gl.renderMode == GL_SELECT) selectHit(gl.raster.pos[2]);
    float ew = (eye[3] != 0.0f)? eye[3] : 1.0f;
    gl.raster.distance = sqrtf(eye[0]*eye[0] + eye[1]*eye[1] + eye[2]*eye[2])/fabsf(ew);

    // The current color, lit like a vertex
    Vertex v = gl.current;
    if (gl.lightingEnabled)
    {
        float iw = (w != 0.0f)? 1.0f/w : 1.0f;
        v.pos[0] = x*iw; v.pos[1] = y*iw; v.pos[2] = z*iw;
        lightVertex(&v, gl.currentNormal);
    }
    memcpy(gl.raster.color, v.color, sizeof(gl.raster.color));

    // Texture coordinates of unit 0 through its texture matrix
    const float *tm = gl.stack[2][gl.stackDepth[2]].m;
    float tc[4] = { gl.current.tex[0], gl.current.tex[1], gl.currentTexR[0], gl.current.tex[2] };
    for (int r = 0; r < 4; r++) gl.raster.tex[r] = tm[r]*tc[0] + tm[4 + r]*tc[1] + tm[8 + r]*tc[2] + tm[12 + r]*tc[3];
}

void glRasterPos4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { setRasterPos(x, y, z, w); }
void glRasterPos2d(GLdouble x, GLdouble y) { setRasterPos((float)x, (float)y, 0.0f, 1.0f); }
void glRasterPos2dv(const GLdouble *v) { setRasterPos((float)v[0], (float)v[1], 0.0f, 1.0f); }
void glRasterPos2f(GLfloat x, GLfloat y) { setRasterPos(x, y, 0.0f, 1.0f); }
void glRasterPos2fv(const GLfloat *v) { setRasterPos(v[0], v[1], 0.0f, 1.0f); }
void glRasterPos2i(GLint x, GLint y) { setRasterPos((float)x, (float)y, 0.0f, 1.0f); }
void glRasterPos2iv(const GLint *v) { setRasterPos((float)v[0], (float)v[1], 0.0f, 1.0f); }
void glRasterPos2s(GLshort x, GLshort y) { setRasterPos(x, y, 0.0f, 1.0f); }
void glRasterPos2sv(const GLshort *v) { setRasterPos(v[0], v[1], 0.0f, 1.0f); }
void glRasterPos3d(GLdouble x, GLdouble y, GLdouble z) { setRasterPos((float)x, (float)y, (float)z, 1.0f); }
void glRasterPos3dv(const GLdouble *v) { setRasterPos((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glRasterPos3f(GLfloat x, GLfloat y, GLfloat z) { setRasterPos(x, y, z, 1.0f); }
void glRasterPos3fv(const GLfloat *v) { setRasterPos(v[0], v[1], v[2], 1.0f); }
void glRasterPos3i(GLint x, GLint y, GLint z) { setRasterPos((float)x, (float)y, (float)z, 1.0f); }
void glRasterPos3iv(const GLint *v) { setRasterPos((float)v[0], (float)v[1], (float)v[2], 1.0f); }
void glRasterPos3s(GLshort x, GLshort y, GLshort z) { setRasterPos(x, y, z, 1.0f); }
void glRasterPos3sv(const GLshort *v) { setRasterPos(v[0], v[1], v[2], 1.0f); }
void glRasterPos4d(GLdouble x, GLdouble y, GLdouble z, GLdouble w) { setRasterPos((float)x, (float)y, (float)z, (float)w); }
void glRasterPos4dv(const GLdouble *v) { setRasterPos((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glRasterPos4fv(const GLfloat *v) { setRasterPos(v[0], v[1], v[2], v[3]); }
void glRasterPos4i(GLint x, GLint y, GLint z, GLint w) { setRasterPos((float)x, (float)y, (float)z, (float)w); }
void glRasterPos4iv(const GLint *v) { setRasterPos((float)v[0], (float)v[1], (float)v[2], (float)v[3]); }
void glRasterPos4s(GLshort x, GLshort y, GLshort z, GLshort w) { setRasterPos(x, y, z, w); }
void glRasterPos4sv(const GLshort *v) { setRasterPos(v[0], v[1], v[2], v[3]); }

void glPixelZoom(GLfloat xfactor, GLfloat yfactor)
{
    LIST_SAVE(PIXEL_ZOOM, "ff", xfactor, yfactor);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    gl.zoomX = xfactor;
    gl.zoomY = yfactor;
}

// Layout of a glDrawPixels image (GL 1.1 tables 3.5 and 3.6, plus the packed 16-bit types): bytes per element and group
// (0 for GL_BITMAP); the error of an invalid format/type otherwise
static GLenum pixelImageLayout(GLenum format, GLenum type, int *elemSize, int *groupSize)
{
    if ((format != GL_STENCIL_INDEX) && (format != GL_DEPTH_COMPONENT))
    {
        int n;
        return colorImageLayout(format, type, &n, elemSize, groupSize);
    }
    if (type == GL_BITMAP)
    {
        *elemSize = *groupSize = 0;
        return (format == GL_STENCIL_INDEX)? GL_NO_ERROR : GL_INVALID_ENUM;
    }
    if (packedFormat(type)) return GL_INVALID_OPERATION;
    if ((typeSize(type) == 0) || (type == GL_DOUBLE) || (type == GL_FIXED)) return GL_INVALID_ENUM;
    *elemSize = *groupSize = typeSize(type);
    return GL_NO_ERROR;
}

// Columns (rows) [*i0, *i1) of an n pixels wide (high) rectangle at window position p with zoom z that land on a screen
// `size` pixels wide (high)
static bool visibleRange(float p, float z, int n, int size, int *i0, int *i1)
{
    if ((z == 0.0f) || (n <= 0)) return false;
    float a = -p/z, b = ((float)size - p)/z, lo = fminf(a, b), hi = fmaxf(a, b);
    *i0 = !(lo > 0.0f)? 0 : (lo >= (float)n)? n : (int)floorf(lo);
    *i1 = !(hi < (float)n)? n : (hi <= 0.0f)? 0 : (int)ceilf(hi);
    return *i0 < *i1;
}

#define PIXEL_TILE  256         // Pixel rectangles are drawn in tiles of up to PIXEL_TILE^2 pixels

// tiledOffset() split into its x and y parts (the Morton bits of x and y are disjoint): offset = tiledX + tiledY
static u32 tiledX(int x, int bpp)
{
    return (u32)(((x >> 3)*64 + ((x & 1) | ((x & 2) << 1) | ((x & 4) << 2)))*bpp);
}

static u32 tiledY(int texWidth, int texHeight, int y, int bpp)
{
#if C3DGL_TEXTURE_FLIP_Y
    y = texHeight - 1 - y;
#else
    (void)texHeight;
#endif
    return (u32)(((y >> 3)*(texWidth >> 3)*64 + (((y & 1) << 1) | ((y & 2) << 2) | ((y & 4) << 3)))*bpp);
}

// Fill w x h texels from (dx, dy) on in a texWidth x texHeight pixel rectangle texture with the source pixels from
// (x0, y0) on (srcWidth: width of the source image)
typedef void (*PixelFill)(const void *src, int srcWidth, u8 *tex, int texWidth, int texHeight, int dx, int dy,
                          int x0, int y0, int w, int h);

typedef struct {
    const u8 *data;
    const PixelStore *ps;
} BitmapSource;

static void fillImage(const void *src, int srcWidth, u8 *tex, int texWidth, int texHeight, int dx, int dy,
                      int x0, int y0, int w, int h)
{
    u32 xOffset[PIXEL_TILE];
    for (int x = 0; x < w; x++) xOffset[x] = tiledX(dx + x, 4);
    for (int y = 0; y < h; y++)
    {
        const u8 *p = (const u8 *)src + ((size_t)(y0 + y)*srcWidth + x0)*4;
        u8 *row = tex + tiledY(texWidth, texHeight, dy + y, 4);
        for (int x = 0; x < w; x++, p += 4)
        {
            u8 *d = row + xOffset[x];       // PICA RGBA8 is ABGR
            d[0] = p[3]; d[1] = p[2]; d[2] = p[1]; d[3] = p[0];
        }
    }
}

static void fillBitmap(const void *src, int srcWidth, u8 *tex, int texWidth, int texHeight, int dx, int dy,
                       int x0, int y0, int w, int h)
{
    const BitmapSource *b = src;
    const PixelStore *ps = b->ps;
    size_t rowBytes = ((size_t)((ps->rowLength > 0)? ps->rowLength : srcWidth) + 7)/8;
    rowBytes = (rowBytes + ps->alignment - 1)/ps->alignment*ps->alignment;
    for (int y = 0; y < h; y++)
    {
        const u8 *bits = b->data + (size_t)(ps->skipRows + y0 + y)*rowBytes;
        u8 *row = tex + tiledY(texWidth, texHeight, dy + y, 1);
        for (int x = 0; x < w; x++)
        {
            int bit = ps->skipPixels + x0 + x;
            bool set = ((ps->lsbFirst? (bits[bit/8] >> (bit & 7)) : (bits[bit/8] >> (7 - (bit & 7)))) & 1) != 0;
            row[tiledX(dx + x, 1)] = set? 255 : 0;
        }
    }
}

// A texture of a pixel rectangle in this frame's pixel memory (see allocPixelMemory()), sampled with GL_NEAREST. The
// C3D_Tex lives next to its texels: the batch refers to it until it is drawn
static C3D_Tex *pixelTexture(int width, int height, GPU_TEXCOLOR format, int bpp)
{
    u8 *mem = allocPixelMemory(128 + (size_t)width*height*bpp);
    if (mem == NULL) return NULL;
    C3D_Tex *tex = (C3D_Tex *)mem;
    memset(tex, 0, sizeof(*tex));
    tex->data = mem + 128;
    tex->fmt = format;
    tex->size = (size_t)width*height*bpp;
    tex->width = (u16)width;
    tex->height = (u16)height;
    tex->param = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) |
                 GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_MODE(GPU_TEX_2D);
    return tex;
}

// Batch state of pixel rectangles: the fragment state of the GL state, on the full screen without texturing or culling
static void usePixelState(const C3D_Tex *tex, PixelMode mode)
{
    DrawState key;
    drawKey(&key, true, false);
    memset(key.units, 0, sizeof(key.units));
    key.spriteUnits = key.texGenUnits = 0;
    key.texMatrixSerial = 0;
    key.texQ = false;
    key.cull = false;
    key.viewport[0] = key.viewport[1] = 0;
    key.viewport[2] = screenWidth(gl.screen);
    key.viewport[3] = C3DGL_SCREEN_HEIGHT;
    key.pixelMode = (u8)mode;
    key.pixelTex = tex;
    if (mode != PIXEL_IMAGE)    // Fragments only where the mask is set, see PixelMode (the GL alpha test was done by the caller)
    {
        key.alphaTest = true;
        key.alphaFunc = (mode == PIXEL_BITMAP)? GL_GREATER : GL_EQUAL;
        key.alphaRef = 0;
    }
    useState(&key);
}

// Quad at the raster depth with the raster color; corner k (counter-clockwise from the bottom left) at window position
// (wx[k], wy[k]) with texcoords (s[k], t[k])
static void emitPixelQuad(const float wx[4], const float wy[4], const float s[4], const float t[4])
{
    // Window depth -> NDC with the depth range (the batch keeps glDepthRange, the fog table depends on it)
    float n = gl.state.depthNear, f = gl.state.depthFar, z = (f != n)? 2.0f*(gl.raster.pos[2] - n)/(f - n) - 1.0f : 0.0f;
    float sw = 2.0f/screenWidth(gl.screen), sh = 2.0f/C3DGL_SCREEN_HEIGHT;
    Vertex q[4];
    memset(q, 0, sizeof(q));
    for (int i = 0; i < 4; i++)
    {
        q[i].pos[0] = wx[i]*sw - 1.0f;
        q[i].pos[1] = wy[i]*sh - 1.0f;
        q[i].pos[2] = z;
        q[i].tex[0] = s[i];
        q[i].tex[1] = t[i];
        q[i].tex[2] = 1.0f;
        memcpy(q[i].color, gl.raster.color, sizeof(q[i].color));
    }
    emitTriangle(&q[0], &q[1], &q[2]);
    emitTriangle(&q[0], &q[2], &q[3]);
}

// A w x h pixel rectangle at window position (x, y) with zoom (zx, zy): the visible part in tiles of up to PIXEL_TILE^2
// pixels, each a texture on a quad over the zoomed pixels. PIXEL_IMAGE: RGBA8 texels of the source (rows from the
// bottom), the bitmap modes: an A8 mask with the raster color
static void drawPixelRect(PixelMode mode, PixelFill fill, const void *src, int w, int h, float x, float y, float zx, float zy)
{
    int i0, i1, j0, j1;
    if (!visibleRange(x, zx, w, screenWidth(gl.screen), &i0, &i1) ||
        !visibleRange(y, zy, h, C3DGL_SCREEN_HEIGHT, &j0, &j1))
        return;

    bool image = (mode == PIXEL_IMAGE);
    for (int ty = j0; ty < j1; ty += PIXEL_TILE)
    {
        for (int tx = i0; tx < i1; tx += PIXEL_TILE)
        {
            int tw = (i1 - tx < PIXEL_TILE)? i1 - tx : PIXEL_TILE, th = (j1 - ty < PIXEL_TILE)? j1 - ty : PIXEL_TILE;
            int texWidth = nextPow2(tw), texHeight = nextPow2(th);
            C3D_Tex *tex = pixelTexture(texWidth, texHeight, image? GPU_RGBA8 : GPU_A8, image? 4 : 1);
            if (tex == NULL) return;
            fill(src, w, tex->data, texWidth, texHeight, 0, 0, tx, ty, tw, th);
            usePixelState(tex, mode);

            float x0 = x + zx*tx, x1 = x + zx*(tx + tw), y0 = y + zy*ty, y1 = y + zy*(ty + th);
            float s1 = (float)tw/texWidth, t1 = (float)th/texHeight;
            emitPixelQuad((const float[4]){ x0, x1, x1, x0 }, (const float[4]){ y0, y0, y1, y1 },
                          (const float[4]){ 0, s1, s1, 0 }, (const float[4]){ 0, 0, t1, t1 });
        }
    }
}

// Bitmaps (glyphs, mostly) are packed into one A8 atlas per frame, so that a run of glBitmap calls is one batch. Shelf
// packing; a new atlas is started when one is full and after every CPU cache flush of the pixel memory (texels written
// after it would not be flushed), see flushVertexCache()
#define BITMAP_ATLAS_SIZE   256

static C3D_Tex *atlasSlot(int w, int h, int *x, int *y)
{
    if (gl.atlas != NULL)
    {
        if (gl.atlasX + w > BITMAP_ATLAS_SIZE) { gl.atlasX = 0; gl.atlasY += gl.atlasRowHeight; gl.atlasRowHeight = 0; }
        if (gl.atlasY + h > BITMAP_ATLAS_SIZE) gl.atlas = NULL;
    }
    if (gl.atlas == NULL)
    {
        gl.atlas = pixelTexture(BITMAP_ATLAS_SIZE, BITMAP_ATLAS_SIZE, GPU_A8, 1);
        if (gl.atlas == NULL) return NULL;
        gl.atlasX = gl.atlasY = gl.atlasRowHeight = 0;
    }
    *x = gl.atlasX;
    *y = gl.atlasY;
    gl.atlasX += w;
    if (h > gl.atlasRowHeight) gl.atlasRowHeight = h;
    return gl.atlas;
}

// glBitmap: the bitmap's pixels at window position (x, y), unzoomed
static void drawBitmap(PixelMode mode, const BitmapSource *src, int w, int h, float x, float y)
{
    if ((w > BITMAP_ATLAS_SIZE) || (h > BITMAP_ATLAS_SIZE))
    {
        drawPixelRect(mode, fillBitmap, src, w, h, x, y, 1.0f, 1.0f);
        return;
    }
    if ((x >= screenWidth(gl.screen)) || (y >= C3DGL_SCREEN_HEIGHT) || (x + w <= 0.0f) || (y + h <= 0.0f)) return;

    ensureFrame();      // Before the slot: a new frame starts a new atlas
    int ax, ay;
    C3D_Tex *atlas = atlasSlot(w, h, &ax, &ay);
    if (atlas == NULL) return;
    fillBitmap(src, w, atlas->data, BITMAP_ATLAS_SIZE, BITMAP_ATLAS_SIZE, ax, ay, 0, 0, w, h);
    usePixelState(atlas, mode);

    const float k = 1.0f/BITMAP_ATLAS_SIZE;
    float s0 = ax*k, s1 = (ax + w)*k, t0 = ay*k, t1 = (ay + h)*k;
    emitPixelQuad((const float[4]){ x, x + w, x + w, x }, (const float[4]){ y, y, y + h, y + h },
                  (const float[4]){ s0, s1, s1, s0 }, (const float[4]){ t0, t0, t1, t1 });
}

// glCopyPixels(GL_COLOR) on the GPU: the framebuffer lines (window columns) of the rectangle are copied into a texture
// by a GX texture copy, queued between the draws before and after it like glClear's memory fill, and drawn like an
// image. The color buffer is tiled like an RGBA8 texture: framebuffer line l (window x) is texture row texHeight - 1 -
// (l - line0), pixel y of a line (window y) texture column y; tile rows of 30 tiles go to a 256 texel wide texture
static void copyColorRect(int x, int y, int w, int h)
{
    int x0 = (x < 0)? 0 : x, x1 = x + w, y0 = (y < 0)? 0 : y, y1 = y + h;      // Pixels outside the window: undefined
    if (x1 > screenWidth(gl.screen)) x1 = screenWidth(gl.screen);
    if (y1 > C3DGL_SCREEN_HEIGHT) y1 = C3DGL_SCREEN_HEIGHT;
    if ((x0 >= x1) || (y0 >= y1)) return;

    int line0 = x0 & ~7, lines = ((x1 + 7) & ~7) - line0, texHeight = nextPow2(lines);
    const int texWidth = 256, lineBytes = C3DGL_SCREEN_HEIGHT*8*4;     // A tile row of the framebuffer
    C3D_Tex *tex = pixelTexture(texWidth, texHeight, GPU_RGBA8, 4);
    if (tex == NULL) return;
    GSPGPU_FlushDataCache(tex->data, tex->size);    // No dirty cache lines may be written back over the copy

    flushVertexCache();
    if (gl.drawnThisFrame) C3D_FrameSplit(GX_CMDLIST_FLUSH);     // Flushed, see glClear()
    u8 *in = (u8 *)gl.targets[gl.screen]->frameBuf.colorBuf + (size_t)line0*READ_LINE_BYTES;
    GX_TextureCopy((u32 *)in, GX_BUFFER_DIM(lineBytes >> 4, 0), (u32 *)tex->data,
                   GX_BUFFER_DIM(lineBytes >> 4, (texWidth*8*4 - lineBytes) >> 4), (u32)(lines/8*lineBytes),
                   GX_TRANSFER_RAW_COPY(1));

    usePixelState(tex, PIXEL_IMAGE);
    float xr = gl.raster.pos[0], yr = gl.raster.pos[1], zx = gl.zoomX, zy = gl.zoomY;
    float wx[4], wy[4], s[4], t[4];
    static const int corner[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
    for (int i = 0; i < 4; i++)
    {
        int sx = corner[i][0]? x1 : x0, sy = corner[i][1]? y1 : y0;
        wx[i] = xr + zx*(sx - x);
        wy[i] = yr + zy*(sy - y);
        s[i] = (float)sy/texWidth;
        t[i] = (float)(texHeight - (sx - line0))/texHeight;
    }
    emitPixelQuad(wx, wy, s, t);
}

static bool compareValues(GLenum func, u32 a, u32 b)
{
    switch (func)
    {
        case GL_NEVER: return false;
        case GL_LESS: return a < b;
        case GL_EQUAL: return a == b;
        case GL_LEQUAL: return a <= b;
        case GL_GREATER: return a > b;
        case GL_NOTEQUAL: return a != b;
        case GL_GEQUAL: return a >= b;
        default: return true;
    }
}

static u8 applyStencilOp(GLenum op, u8 s, u8 ref)
{
    switch (op)
    {
        case GL_ZERO: return 0;
        case GL_REPLACE: return ref;
        case GL_INCR: return (s < 255)? s + 1 : 255;
        case GL_DECR: return s? s - 1 : 0;
        case GL_INVERT: return (u8)~s;
        case GL_INCR_WRAP: return (u8)(s + 1);
        case GL_DECR_WRAP: return (u8)(s - 1);
        default: return s;
    }
}

// Whether a depth (stencil) image changes the depth/stencil buffer at all. The fragments of a depth image have the
// raster color, which goes through the alpha test (and is not written: c3dgl writes only the depth); depth is only
// written with the depth test on. Stencil images are written directly (GL 1.1 section 4.3.1: scissor and writemask)
static bool depthStencilWrites(bool stencil)
{
    const DrawState *st = &gl.state;
    if (stencil) return st->stencilWriteMask != 0;
    if (!st->depthTest && !st->stencilTest) return false;
    if (st->alphaTest && !compareValues(st->alphaFunc, gl.raster.color[3], st->alphaRef)) return false;
    return st->depthMask || (st->stencilTest && st->stencilWriteMask);
}

// A w x h depth (stored form: (1 - window depth)*0xFFFFFF) or stencil image at the raster position with the pixel zoom,
// written on the CPU: the buffer is read like glReadPixels, changed by the scissor, stencil and depth test (fragment
// depth = the image's) and the masks, and written back with a display transfer
static void drawDepthStencil(const u32 *values, int w, int h, bool stencil)
{
    const DrawState *st = &gl.state;
    float xr = gl.raster.pos[0], yr = gl.raster.pos[1], zx = gl.zoomX, zy = gl.zoomY;
    int i0, i1, j0, j1, width = screenWidth(gl.screen);
    if (!visibleRange(xr, zx, w, width, &i0, &i1) || !visibleRange(yr, zy, h, C3DGL_SCREEN_HEIGHT, &j0, &j1)) return;

    // Window rectangle of the visible pixels, clipped to the screen and the scissor box
    float ax = xr + zx*i0, bx = xr + zx*i1, ay = yr + zy*j0, by = yr + zy*j1;
    int x0 = (int)floorf(fminf(ax, bx)), x1 = (int)ceilf(fmaxf(ax, bx));
    int y0 = (int)floorf(fminf(ay, by)), y1 = (int)ceilf(fmaxf(ay, by));
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > width) x1 = width;
    if (y1 > C3DGL_SCREEN_HEIGHT) y1 = C3DGL_SCREEN_HEIGHT;
    if (st->scissor)
    {
        const GLint *sb = st->scissorBox;
        if (x0 < sb[0]) x0 = sb[0];
        if (y0 < sb[1]) y0 = sb[1];
        if (x1 > sb[0] + sb[2]) x1 = sb[0] + sb[2];
        if (y1 > sb[1] + sb[3]) y1 = sb[1] + sb[3];
    }
    if ((x0 >= x1) || (y0 >= y1)) return;

    int line0 = x0 & ~7, lines = ((x1 + 7) & ~7) - line0;
    bool used[C3DGL_SCREEN_COUNT], suspended = suspendFrame(used);
    u8 *fb = readLines(true, line0, lines);
    if (fb == NULL) { resumeFrame(suspended, used); return; }

    u8 ref = st->stencilRef, funcMask = st->stencilFuncMask, writeMask = st->stencilWriteMask;
    for (int wx = x0; wx < x1; wx++)
    {
        // Pixel (i, j) covers the zoomed rectangle from (xr + zx*i, yr + zy*j): fragments at the centers inside it
        float fi = (wx + 0.5f - xr)/zx;
        if (!(fi >= 0.0f) || !(fi < (float)w)) continue;
        int i = (int)fi;
        for (int wy = y0; wy < y1; wy++)
        {
            float fj = (wy + 0.5f - yr)/zy;
            if (!(fj >= 0.0f) || !(fj < (float)h)) continue;
            u32 v = values[(size_t)(int)fj*w + i];

            u8 *p = fb + (size_t)(wx - line0)*READ_LINE_BYTES + (size_t)wy*4;
            u8 s = p[3];
            if (stencil) { p[3] = (u8)((s & ~writeMask) | (v & writeMask)); continue; }

            u32 d = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
            u8 ns = s;
            if (st->stencilTest && !compareValues(st->stencilFunc, ref & funcMask, s & funcMask))
                ns = applyStencilOp(st->stencilFail, s, ref);
            else if (!st->depthTest)
                ns = applyStencilOp(st->stencilPass, s, ref);
            else if (compareValues(st->depthFunc, d, v))     // Stored depth is reversed: z_frag < z_buffer <=> v > d
            {
                if (st->depthMask) d = v;
                ns = applyStencilOp(st->stencilPass, s, ref);
            }
            else ns = applyStencilOp(st->stencilDepthFail, s, ref);
            if (st->stencilTest) s = (u8)((s & ~writeMask) | (ns & writeMask));

            p[0] = (u8)d; p[1] = (u8)(d >> 8); p[2] = (u8)(d >> 16); p[3] = s;
        }
    }
    writeDepthStencilLines(fb, line0, lines);
    linearFree(fb);
    resumeFrame(suspended, used);
}

// glDrawPixels/glBitmap while compiling a list: the image is stored tightly packed (bitmaps most significant bit first)
// with the arguments; move: xorig, yorig, xmove, ymove of glBitmap. Not if the call fails anyway before reading it
static void listSavePixels(ListCommand command, GLsizei width, GLsizei height, GLenum format, GLenum type,
                           const float move[4], const void *pixels)
{
    int elemSize = 0, groupSize = 0;
    bool bits = (type == GL_BITMAP);
    bool captured = (pixels != NULL) && (width > 0) && (height > 0) &&
                    ((command == LIST_BITMAP) || (pixelImageLayout(format, type, &elemSize, &groupSize) == GL_NO_ERROR));
    size_t rowBytes = !captured? 0 : bits? ((size_t)width + 7)/8 : (size_t)width*groupSize;
    ListWord *w = listBegin(command, 9 + (int)((rowBytes*height + 3)/4));
    if (w == NULL) return;

    w[0].i = width;
    w[1].i = height;
    w[2].u = format;
    w[3].u = type;
    w[4].i = (captured? 1 : 0) | (gl.unpack.swapBytes? 2 : 0);
    for (int i = 0; i < 4; i++) w[5 + i].f = move[i];
    if (captured)
    {
        const PixelStore *ps = &gl.unpack;
        u8 *dst = (u8 *)&w[9];
        if (bits) packBitmap(pixels, ps, width, height, dst);
        else
        {
            size_t srcRow = imageRowBytes(ps, width, elemSize, groupSize);
            const u8 *src = (const u8 *)pixels + (size_t)ps->skipRows*srcRow + (size_t)ps->skipPixels*groupSize;
            for (int y = 0; y < height; y++) memcpy(dst + (size_t)y*rowBytes, src + (size_t)y*srcRow, rowBytes);
        }
    }
    listEnd();
}

static void listPixels(const ListWord *w, bool bitmap)
{
    PixelStore saved = gl.unpack;
    gl.unpack = (PixelStore){ .alignment = 1, .swapBytes = (w[4].i & 2) != 0 };
    const void *pixels = (w[4].i & 1)? (const void *)&w[9] : NULL;
    if (bitmap) glBitmap(w[0].i, w[1].i, w[5].f, w[6].f, w[7].f, w[8].f, pixels);
    else glDrawPixels(w[0].i, w[1].i, w[2].u, w[3].u, pixels);
    gl.unpack = saved;
}

void glBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig, GLfloat xmove, GLfloat ymove,
              const GLubyte *bitmap)
{
    if (gl.listCompiling)
    {
        listSavePixels(LIST_BITMAP, width, height, GL_COLOR_INDEX, GL_BITMAP, (const float[4]){ xorig, yorig, xmove, ymove }, bitmap);
        return;
    }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    if (!gl.raster.valid) return;
    if (gl.renderMode != GL_RENDER)
    {
        feedbackRaster(GL_BITMAP_TOKEN);
        gl.raster.pos[0] += xmove;
        gl.raster.pos[1] += ymove;
        return;
    }

    // The fragments have the raster color: the alpha test is decided here, the bitmap modes discard where no bit is set
    u8 alpha = gl.raster.color[3];
    if ((width > 0) && (height > 0) && (bitmap != NULL) &&
        (!gl.state.alphaTest || compareValues(gl.state.alphaFunc, alpha, gl.state.alphaRef)))
    {
        BitmapSource src = { bitmap, &gl.unpack };
        drawBitmap(alpha? PIXEL_BITMAP : PIXEL_BITMAP_ZERO, &src, width, height,
                   floorf(gl.raster.pos[0] - xorig), floorf(gl.raster.pos[1] - yorig));
    }
    gl.raster.pos[0] += xmove;
    gl.raster.pos[1] += ymove;
}

void glDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    if (gl.listCompiling)
    {
        listSavePixels(LIST_DRAW_PIXELS, width, height, format, type, (const float[4]){ 0 }, pixels);
        return;
    }
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    int elemSize, groupSize;
    GLenum error = pixelImageLayout(format, type, &elemSize, &groupSize);
    if (error != GL_NO_ERROR) { setError(error); return; }
    if (gl.renderMode != GL_RENDER) { feedbackRaster(GL_DRAW_PIXEL_TOKEN); return; }
    if (!gl.raster.valid || (width == 0) || (height == 0) || (pixels == NULL)) return;

    bool depth = (format == GL_DEPTH_COMPONENT), stencil = (format == GL_STENCIL_INDEX);
    if ((depth || stencil) && !depthStencilWrites(stencil)) return;

    u32 *values = malloc((size_t)width*height*4);     // RGBA8 texels or depth/stencil values
    if (values == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    if (!depth && !stencil)
    {
        unpackColorImage(pixels, width, height, format, type, &gl.unpack, (u8 *)values);
        drawPixelRect(PIXEL_IMAGE, fillImage, values, width, height, gl.raster.pos[0], gl.raster.pos[1], gl.zoomX, gl.zoomY);
        free(values);
        return;
    }

    const PixelStore *ps = &gl.unpack;
    size_t rowBytes = imageRowBytes(ps, width, elemSize, groupSize);
    const u8 *base = (const u8 *)pixels + (size_t)ps->skipRows*rowBytes + (size_t)ps->skipPixels*groupSize;
    bool swap = ps->swapBytes && (elemSize > 1);
    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            u32 *v = &values[(size_t)y*width + x];
            const u8 *src = base + (size_t)y*rowBytes + (size_t)x*groupSize;
            if (stencil) *v = (u32)transferStencil((type == GL_BITMAP)? bitmapBit(pixels, ps, width, x, y) : loadIndex(src, type, swap));
            else *v = (u32)((1.0f - transferDepth(loadElementRaw(src, type, swap)))*0xFFFFFF);  // Like glClear's depth
        }
    }
    drawDepthStencil(values, width, height, stencil);
    free(values);
}

// GL 1.1 section 4.3.3: the rectangle is read like glReadPixels and drawn like glDrawPixels
void glCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type)
{
    LIST_SAVE(COPY_PIXELS, "iiiiu", x, y, width, height, type);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((type != GL_COLOR) && (type != GL_DEPTH) && (type != GL_STENCIL)) { setError(GL_INVALID_ENUM); return; }
    if ((width < 0) || (height < 0)) { setError(GL_INVALID_VALUE); return; }
    if (gl.renderMode != GL_RENDER) { feedbackRaster(GL_COPY_PIXEL_TOKEN); return; }
    if (!gl.raster.valid || (width == 0) || (height == 0)) return;

    if ((type == GL_COLOR) && !colorTransferActive()) { copyColorRect(x, y, width, height); return; }
    if (type == GL_COLOR)
    {
        // With the pixel transfer: read (and transferred) on the CPU like glReadPixels, then drawn like glDrawPixels
        u8 *pixels = copyPixels(x, y, width, height);
        if (pixels == NULL) return;
        drawPixelRect(PIXEL_IMAGE, fillImage, pixels, width, height, gl.raster.pos[0], gl.raster.pos[1], gl.zoomX, gl.zoomY);
        free(pixels);
        return;
    }

    bool stencil = (type == GL_STENCIL);
    if (!depthStencilWrites(stencil)) return;

    // Source pixels outside the window are undefined in GL: 0 here
    u32 *values = calloc((size_t)width*height, 4);
    if (values == NULL) { setError(GL_OUT_OF_MEMORY); return; }
    int x0 = (x < 0)? 0 : x, x1 = x + width, y0 = (y < 0)? 0 : y, y1 = y + height;
    if (x1 > screenWidth(gl.screen)) x1 = screenWidth(gl.screen);
    if (y1 > C3DGL_SCREEN_HEIGHT) y1 = C3DGL_SCREEN_HEIGHT;
    bool transferDepthActive = depthTransferActive();
    if ((x0 < x1) && (y0 < y1))
    {
        int line0 = x0 & ~7;
        u8 *fb = readFramebuffer(true, line0, ((x1 + 7) & ~7) - line0);
        if (fb == NULL) { free(values); return; }
        for (int wx = x0; wx < x1; wx++)
        {
            for (int wy = y0; wy < y1; wy++)
            {
                const u8 *p = fb + (size_t)(wx - line0)*READ_LINE_BYTES + (size_t)wy*4;
                u32 *v = &values[(size_t)(wy - y)*width + (wx - x)];
                if (stencil) *v = (u32)transferStencil(p[3]);
                else
                {
                    *v = p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16);
                    if (transferDepthActive) *v = (u32)((1.0f - transferDepth(1.0f - *v/16777215.0f))*0xFFFFFF);
                }
            }
        }
        linearFree(fb);
    }
    drawDepthStencil(values, width, height, stencil);
    free(values);
}

//----------------------------------------------------------------------------------
// OpenGL: color buffers (GL). The framebuffer is double-buffered RGBA without stereo or aux buffers; drawing always goes
// to the frame being rendered, which c3dglSwapBuffers() presents, so the front buffer is treated like the back buffer
//----------------------------------------------------------------------------------
// Error of glDrawBuffer/glReadBuffer(mode): buffers that do not exist are GL_INVALID_OPERATION
static GLenum colorBufferError(GLenum mode, bool draw)
{
    switch (mode)
    {
        case GL_NONE: case GL_FRONT_AND_BACK: return draw? GL_NO_ERROR : GL_INVALID_ENUM;
        case GL_FRONT_LEFT: case GL_BACK_LEFT: case GL_FRONT: case GL_BACK: case GL_LEFT: return GL_NO_ERROR;
        case GL_FRONT_RIGHT: case GL_BACK_RIGHT: case GL_RIGHT:
        case GL_AUX0: case GL_AUX1: case GL_AUX2: case GL_AUX3: return GL_INVALID_OPERATION;
        default: return GL_INVALID_ENUM;
    }
}

void glDrawBuffer(GLenum mode)
{
    LIST_SAVE(DRAW_BUFFER, "u", mode);
    GLenum error = colorBufferError(mode, true);
    if (error != GL_NO_ERROR) { setError(error); return; }
    if ((mode != GL_NONE) && (mode != GL_BACK) && (mode != GL_BACK_LEFT))
        WARN_ONCE("glDrawBuffer: the front buffer is drawn like the back buffer (shown after c3dglSwapBuffers)\n");
    gl.drawBuffer = mode;
}

void glReadBuffer(GLenum mode)
{
    LIST_SAVE(READ_BUFFER, "u", mode);
    GLenum error = colorBufferError(mode, false);
    if (error != GL_NO_ERROR) { setError(error); return; }
    gl.readBuffer = mode;
}

//----------------------------------------------------------------------------------
// OpenGL: feedback and selection (GL). glRenderMode, glFeedbackBuffer and glSelectBuffer are executed immediately (not
// compiled into display lists); the primitives are handled in the feedback section
//----------------------------------------------------------------------------------
// Selection: a hit record (name stack depth, min and max window depth scaled to 2^32 - 1, the names), written when the
// name stack changes or selection mode ends after a hit
static void selectValue(GLuint value)
{
    if (gl.selectCount < gl.selectSize) gl.selectBuffer[gl.selectCount] = value;
    if (gl.selectCount <= gl.selectSize) gl.selectCount++;
}

static void writeHitRecord(void)
{
    selectValue((GLuint)gl.nameDepth);
    selectValue((GLuint)(gl.hitMinZ*4294967295.0));
    selectValue((GLuint)(gl.hitMaxZ*4294967295.0));
    for (int i = 0; i < gl.nameDepth; i++) selectValue(gl.names[i]);
    gl.hits++;
    gl.hit = false;
    gl.hitMinZ = 1.0f;
    gl.hitMaxZ = 0.0f;
}

GLint glRenderMode(GLenum mode)
{
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return 0; }
    if ((mode != GL_RENDER) && (mode != GL_FEEDBACK) && (mode != GL_SELECT)) { setError(GL_INVALID_ENUM); return 0; }
    if (((mode == GL_FEEDBACK) && !gl.feedbackBufferSet) || ((mode == GL_SELECT) && !gl.selectBufferSet))
    {
        setError(GL_INVALID_OPERATION);
        return 0;
    }

    // Leaving a mode returns its result: the number of hit records or feedback values, -1 on overflow
    GLint result = 0;
    if (gl.renderMode == GL_SELECT)
    {
        if (gl.hit) writeHitRecord();
        result = (gl.selectCount > gl.selectSize)? -1 : gl.hits;
    }
    else if (gl.renderMode == GL_FEEDBACK) result = (gl.feedbackCount > gl.feedbackSize)? -1 : gl.feedbackCount;

    gl.renderMode = mode;
    gl.feedbackCount = gl.selectCount = gl.hits = 0;
    gl.hit = false;
    gl.hitMinZ = 1.0f;
    gl.hitMaxZ = 0.0f;
    gl.nameDepth = 0;
    return result;
}

void glFeedbackBuffer(GLsizei size, GLenum type, GLfloat *buffer)
{
    if (gl.inBegin || (gl.renderMode == GL_FEEDBACK)) { setError(GL_INVALID_OPERATION); return; }
    if ((type != GL_2D) && (type != GL_3D) && (type != GL_3D_COLOR) && (type != GL_3D_COLOR_TEXTURE) &&
        (type != GL_4D_COLOR_TEXTURE)) { setError(GL_INVALID_ENUM); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    gl.feedbackBuffer = buffer;
    gl.feedbackSize = (buffer != NULL)? size : 0;
    gl.feedbackType = type;
    gl.feedbackBufferSet = true;
}

void glSelectBuffer(GLsizei size, GLuint *buffer)
{
    if (gl.inBegin || (gl.renderMode == GL_SELECT)) { setError(GL_INVALID_OPERATION); return; }
    if (size < 0) { setError(GL_INVALID_VALUE); return; }
    gl.selectBuffer = buffer;
    gl.selectSize = (buffer != NULL)? size : 0;
    gl.selectBufferSet = true;
}

void glPassThrough(GLfloat token)
{
    LIST_SAVE(PASS_THROUGH, "f", token);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_FEEDBACK) return;
    feedbackValue((GLfloat)GL_PASS_THROUGH_TOKEN);
    feedbackValue(token);
}

// The name stack commands are ignored outside selection mode
void glInitNames(void)
{
    LIST_SAVE(INIT_NAMES, "");
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.hit) writeHitRecord();
    gl.nameDepth = 0;
}

void glLoadName(GLuint name)
{
    LIST_SAVE(LOAD_NAME, "u", name);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.nameDepth == 0) { setError(GL_INVALID_OPERATION); return; }
    if (gl.hit) writeHitRecord();
    gl.names[gl.nameDepth - 1] = name;
}

void glPushName(GLuint name)
{
    LIST_SAVE(PUSH_NAME, "u", name);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.hit) writeHitRecord();
    if (gl.nameDepth == C3DGL_MAX_NAME_STACK) { setError(GL_STACK_OVERFLOW); return; }
    gl.names[gl.nameDepth++] = name;
}

void glPopName(void)
{
    LIST_SAVE(POP_NAME, "");
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if (gl.renderMode != GL_SELECT) return;
    if (gl.hit) writeHitRecord();
    if (gl.nameDepth == 0) { setError(GL_STACK_UNDERFLOW); return; }
    gl.nameDepth--;
}

//----------------------------------------------------------------------------------
// OpenGL: display lists (GL)
//
// Between glNewList and glEndList, the gl* function of every command in LIST_COMMANDS records it with listSave() (see
// LIST_SAVE) and returns. Client memory is copied while compiling (pixels, control points, vertex array elements,
// glCallLists names), as GL requires. glCallList executes the commands through the same gl* functions, so a list does
// exactly what the calls would do; in GL_COMPILE_AND_EXECUTE mode each command is executed that way right after it is
// recorded. Errors of recorded commands are raised when the list is executed (some argument checks of the variant
// functions, like glLightf's, happen while compiling)
//----------------------------------------------------------------------------------
static double listDouble(const ListWord *w)
{
    double d;
    memcpy(&d, w, sizeof(d));      // Doubles are only 4-byte aligned in a list
    return d;
}

// glTexImage1D/2D, glTexSubImage1D/2D from a list: the pixels were stored tightly packed, see listSaveImage()
static void listTexImage(const ListWord *w, bool sub)
{
    PixelStore saved = gl.unpack;
    gl.unpack = (PixelStore){ .alignment = 1, .swapBytes = (w[8].i & 2) != 0 };
    const GLvoid *pixels = (w[8].i & 1)? (const GLvoid *)&w[9] : NULL;
    bool oneD = (w[8].i & 4) != 0;
    if (sub && oneD) glTexSubImage1D(w[0].u, w[1].i, w[2].i, w[4].i, w[6].u, w[7].u, pixels);
    else if (sub) glTexSubImage2D(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i, w[5].i, w[6].u, w[7].u, pixels);
    else if (oneD) glTexImage1D(w[0].u, w[1].i, w[2].i, w[3].i, w[5].i, w[6].u, w[7].u, pixels);
    else glTexImage2D(w[0].u, w[1].i, w[2].i, w[3].i, w[4].i, w[5].i, w[6].u, w[7].u, pixels);
    gl.unpack = saved;
}

static void executeCommand(const ListWord *command)
{
    const ListWord *w = command + 1;
    switch ((ListCommand)(command->u & 0xFF))
    {
        #define X(name, call) case LIST_##name: call; break;
        LIST_COMMANDS(X)
        #undef X
    }
}

// Binary search; *index (if given): position of the list, or where it would be inserted
static DisplayList *findList(GLuint name, int *index)
{
    int lo = 0, hi = gl.listCount;
    while (lo < hi)
    {
        int mid = (lo + hi)/2;
        if (gl.lists[mid].name < name) lo = mid + 1;
        else hi = mid;
    }
    if (index != NULL) *index = lo;
    return ((lo < gl.listCount) && (gl.lists[lo].name == name))? &gl.lists[lo] : NULL;
}

// Lists cannot be created or deleted while one is executed (those calls are not compiled), so l stays valid
static void executeList(GLuint name)
{
    if (gl.listDepth >= C3DGL_MAX_LIST_NESTING) return;     // Deeper calls are ignored
    const DisplayList *l = findList(name, NULL);
    if (l == NULL) return;

    gl.listDepth++;
    for (const ListWord *w = l->words, *end = w + l->count; w < end; w += w->u >> 8) executeCommand(w);
    gl.listDepth--;
}

// Append a command with `words` argument words to the list being compiled and return its arguments; NULL if out of
// memory (the command is dropped)
static ListWord *listBegin(ListCommand command, int words)
{
    gl.listLast = -1;
    int needed = gl.listWordCount + 1 + words;
    if ((words >= (1 << 24) - 1) || (needed < 0)) { setError(GL_OUT_OF_MEMORY); return NULL; }
    if (needed > gl.listWordCapacity)
    {
        int capacity = gl.listWordCapacity? gl.listWordCapacity : 256;
        while (capacity < needed) capacity *= 2;
        ListWord *grown = realloc(gl.listWords, (size_t)capacity*sizeof(ListWord));
        if (grown == NULL) { setError(GL_OUT_OF_MEMORY); return NULL; }
        gl.listWords = grown;
        gl.listWordCapacity = capacity;
    }

    ListWord *header = &gl.listWords[gl.listWordCount];
    header->u = (GLuint)command | ((GLuint)(1 + words) << 8);
    gl.listLast = gl.listWordCount;
    gl.listWordCount = needed;
    return header + 1;
}

// After the arguments are written: GL_COMPILE_AND_EXECUTE executes the command, from the list
static void listEnd(void)
{
    if ((gl.listMode != GL_COMPILE_AND_EXECUTE) || (gl.listLast < 0)) return;
    gl.listCompiling = false;
    executeCommand(&gl.listWords[gl.listLast]);
    gl.listCompiling = true;
}

// Record a command. Each character of format is an argument: 'i' int, 'u' unsigned, 'f' float (passed as double),
// 'd' double (2 words), 'F' an int count and a pointer to that many floats, 'D' the same for doubles
static void listSave(ListCommand command, const char *format, ...)
{
    va_list args, sizes;
    va_start(args, format);
    va_copy(sizes, args);
    int words = 0;
    for (const char *c = format; *c != '\0'; c++)
    {
        switch (*c)
        {
            case 'i': (void)va_arg(sizes, int); words++; break;
            case 'u': (void)va_arg(sizes, unsigned); words++; break;
            case 'f': (void)va_arg(sizes, double); words++; break;
            case 'd': (void)va_arg(sizes, double); words += 2; break;
            case 'F': words += va_arg(sizes, int); (void)va_arg(sizes, const float *); break;
            default: words += 2*va_arg(sizes, int); (void)va_arg(sizes, const double *); break;    // 'D'
        }
    }
    va_end(sizes);

    ListWord *w = listBegin(command, words);
    bool saved = (w != NULL);
    for (const char *c = format; saved && (*c != '\0'); c++)
    {
        switch (*c)
        {
            case 'i': (w++)->i = va_arg(args, int); break;
            case 'u': (w++)->u = va_arg(args, unsigned); break;
            case 'f': (w++)->f = (float)va_arg(args, double); break;
            case 'd': { double d = va_arg(args, double); memcpy(w, &d, sizeof(d)); w += 2; break; }
            case 'F':
            {
                int n = va_arg(args, int);
                const float *p = va_arg(args, const float *);
                if (n > 0) memcpy(w, p, (size_t)n*sizeof(float));
                w += n;
                break;
            }
            default:    // 'D'
            {
                int n = va_arg(args, int);
                const double *p = va_arg(args, const double *);
                if (n > 0) memcpy(w, p, (size_t)n*sizeof(double));
                w += 2*n;
                break;
            }
        }
    }
    va_end(args);
    if (saved) listEnd();
}

// glTexImage1D/2D, glTexSubImage1D/2D: args are the 8 integer arguments of the 2D call before the pixels (1D: height 1
// and yoffset 0 filled in). The pixels are read as the unpack state lays them out and stored tightly packed; not if the
// call fails anyway before reading them (sizeValid false or an invalid format/type), it is recorded without them then
static void listSaveImage(ListCommand command, const GLint args[8], GLsizei width, GLsizei height, bool sizeValid,
                          bool oneD, const void *pixels)
{
    int n, elemSize, groupSize;
    bool captured = (pixels != NULL) && sizeValid &&
                    (colorImageLayout((GLenum)args[6], (GLenum)args[7], &n, &elemSize, &groupSize) == GL_NO_ERROR);
    bool bits = ((GLenum)args[7] == GL_BITMAP);
    size_t rowBytes = !captured? 0 : bits? ((size_t)width + 7)/8 : (size_t)width*groupSize;
    ListWord *w = listBegin(command, 9 + (captured? (int)((rowBytes*height + 3)/4) : 0));
    if (w == NULL) return;

    for (int i = 0; i < 8; i++) w[i].i = args[i];
    w[8].i = (captured? 1 : 0) | (gl.unpack.swapBytes? 2 : 0) | (oneD? 4 : 0);
    if (captured && bits) packBitmap(pixels, &gl.unpack, width, height, (u8 *)&w[9]);
    else if (captured)
    {
        const PixelStore *ps = &gl.unpack;
        size_t srcRow = imageRowBytes(ps, width, elemSize, groupSize);
        const u8 *src = (const u8 *)pixels + (size_t)ps->skipRows*srcRow + (size_t)ps->skipPixels*groupSize;
        for (int y = 0; y < height; y++) memcpy((u8 *)&w[9] + (size_t)y*rowBytes, src + (size_t)y*srcRow, rowBytes);
    }
    listEnd();
}

// glArrayElement while compiling: the element of the enabled arrays is recorded as the immediate mode calls it stands
// for (nothing if it is outside its buffer object, like submitArrayVertex())
static void listArrayElement(int index)
{
    float tex[C3DGL_TEXTURE_UNITS][4], color[4] = { 0.0f, 0.0f, 0.0f, 1.0f }, normal[4];
    float pos[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        float *t = tex[unit];
        t[0] = t[1] = t[2] = 0.0f;
        t[3] = 1.0f;
        if (arrayActive(ARRAY_TEXCOORD0 + unit) && !readArray(&gl.arrays[ARRAY_TEXCOORD0 + unit], index, t, false)) return;
    }
    if (arrayActive(ARRAY_COLOR) && !readArray(&gl.arrays[ARRAY_COLOR], index, color, true)) return;
    if (arrayActive(ARRAY_NORMAL) && !readArray(&gl.arrays[ARRAY_NORMAL], index, normal, true)) return;
    const u8 *edge = arrayActive(ARRAY_EDGEFLAG)? arrayElement(&gl.arrays[ARRAY_EDGEFLAG], index) : NULL;
    if (arrayActive(ARRAY_EDGEFLAG) && (edge == NULL)) return;
    if (arrayActive(ARRAY_VERTEX) && !readArray(&gl.arrays[ARRAY_VERTEX], index, pos, false)) return;
    if (arrayActive(ARRAY_POINTSIZE)) WARN_ONCE("Display lists: the point size array is not recorded\n");

    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        if (arrayActive(ARRAY_TEXCOORD0 + unit))
            glMultiTexCoord4f(GL_TEXTURE0 + unit, tex[unit][0], tex[unit][1], tex[unit][2], tex[unit][3]);
    }
    if (arrayActive(ARRAY_COLOR)) glColor4ub(colorByte(color[0]), colorByte(color[1]), colorByte(color[2]), colorByte(color[3]));
    if (arrayActive(ARRAY_NORMAL)) glNormal3f(normal[0], normal[1], normal[2]);
    if (edge != NULL) glEdgeFlag(*edge != 0);
    if (arrayActive(ARRAY_VERTEX)) glVertex4f(pos[0], pos[1], pos[2], pos[3]);
}

// Insert empty lists named name .. name + count - 1 at `index` (none of them exists); false if out of memory
static bool insertLists(int index, GLuint name, int count)
{
    if (gl.listCount + count > gl.listCapacity)
    {
        int capacity = gl.listCapacity? gl.listCapacity : 64;
        while (capacity < gl.listCount + count) capacity *= 2;
        DisplayList *grown = realloc(gl.lists, (size_t)capacity*sizeof(DisplayList));
        if (grown == NULL) { setError(GL_OUT_OF_MEMORY); return false; }
        gl.lists = grown;
        gl.listCapacity = capacity;
    }
    memmove(&gl.lists[index + count], &gl.lists[index], (size_t)(gl.listCount - index)*sizeof(DisplayList));
    for (int i = 0; i < count; i++) gl.lists[index + i] = (DisplayList){ name + i, NULL, 0 };
    gl.listCount += count;
    return true;
}

void glNewList(GLuint list, GLenum mode)
{
    if (gl.inBegin || (gl.listName != 0)) { setError(GL_INVALID_OPERATION); return; }
    if (list == 0) { setError(GL_INVALID_VALUE); return; }
    if ((mode != GL_COMPILE) && (mode != GL_COMPILE_AND_EXECUTE)) { setError(GL_INVALID_ENUM); return; }

    gl.listName = list;
    gl.listMode = mode;
    gl.listWordCount = 0;
    gl.listCompiling = true;
}

// The list gets its new commands only now: until then, calling it (from itself too) executes the old ones
void glEndList(void)
{
    if (gl.listName == 0) { setError(GL_INVALID_OPERATION); return; }
    GLuint name = gl.listName;
    gl.listName = 0;
    gl.listCompiling = false;

    int index;
    DisplayList *l = findList(name, &index);
    if ((l == NULL) && insertLists(index, name, 1)) l = &gl.lists[index];
    if (l == NULL) return;

    // The recorded words become the list, trimmed to size; the next glNewList starts a new buffer
    free(l->words);
    l->words = NULL;
    l->count = gl.listWordCount;
    if (l->count > 0)
    {
        l->words = realloc(gl.listWords, (size_t)l->count*sizeof(ListWord));
        if (l->words == NULL) l->words = gl.listWords;
    }
    else free(gl.listWords);
    gl.listWords = NULL;
    gl.listWordCount = gl.listWordCapacity = 0;
}

void glCallList(GLuint list)
{
    LIST_SAVE(CALL_LIST, "u", list);
    executeList(list);
}

// Bytes per name of a glCallLists type, 0 if the type is invalid
static int callListsSize(GLenum type)
{
    switch (type)
    {
        case GL_BYTE: case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: case GL_2_BYTES: return 2;
        case GL_3_BYTES: return 3;
        case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: case GL_4_BYTES: return 4;
        default: return 0;
    }
}

// Name i of glCallLists, before the list base is added (GL_n_BYTES: big-endian)
static GLuint callListsName(GLenum type, const GLvoid *lists, int i)
{
    const u8 *p = (const u8 *)lists + (size_t)i*callListsSize(type);
    switch (type)
    {
        case GL_BYTE: return (GLuint)(GLint)*(const s8 *)p;
        case GL_UNSIGNED_BYTE: return *p;
        case GL_SHORT: { s16 v; memcpy(&v, p, 2); return (GLuint)(GLint)v; }
        case GL_UNSIGNED_SHORT: { u16 v; memcpy(&v, p, 2); return v; }
        case GL_INT: case GL_UNSIGNED_INT: { u32 v; memcpy(&v, p, 4); return v; }
        case GL_FLOAT: { float v; memcpy(&v, p, 4); return (GLuint)(GLint)v; }
        case GL_2_BYTES: return ((GLuint)p[0] << 8) | p[1];
        case GL_3_BYTES: return ((GLuint)p[0] << 16) | ((GLuint)p[1] << 8) | p[2];
        default: return ((GLuint)p[0] << 24) | ((GLuint)p[1] << 16) | ((GLuint)p[2] << 8) | p[3];    // GL_4_BYTES
    }
}

void glCallLists(GLsizei n, GLenum type, const GLvoid *lists)
{
    int size = callListsSize(type);
    if (gl.listCompiling)
    {
        // The names are read now (as GL_UNSIGNED_INT); an invalid call is recorded without them and fails when executed
        bool valid = (n >= 0) && (size > 0);
        ListWord *w = listBegin(LIST_CALL_LISTS, 2 + (valid? n : 0));
        if (w == NULL) return;
        w[0].i = n;
        w[1].u = valid? GL_UNSIGNED_INT : type;
        for (int i = 0; valid && (i < n); i++) w[2 + i].u = callListsName(type, lists, i);
        listEnd();
        return;
    }
    if (n < 0) { setError(GL_INVALID_VALUE); return; }
    if (size == 0) { setError(GL_INVALID_ENUM); return; }

    // The base is read per name: a called list may change it
    for (int i = 0; i < n; i++) executeList(gl.listBase + callListsName(type, lists, i));
}

void glListBase(GLuint base)
{
    LIST_SAVE(LIST_BASE, "u", base);
    gl.listBase = base;
}

// The first `range` consecutive unused names; they become empty lists. 0 if there is no such range
GLuint glGenLists(GLsizei range)
{
    if (range < 0) { setError(GL_INVALID_VALUE); return 0; }
    if (range == 0) return 0;

    GLuint base = 1;
    int index = 0;
    for (; index < gl.listCount; index++)
    {
        if (gl.lists[index].name - base >= (GLuint)range) break;      // Names before index are all < base
        base = gl.lists[index].name + 1;
    }
    if ((base == 0) || ((GLuint)range - 1 > 0xFFFFFFFFu - base) || !insertLists(index, base, range)) return 0;
    return base;
}

void glDeleteLists(GLuint list, GLsizei range)
{
    if (range < 0) { setError(GL_INVALID_VALUE); return; }

    int first, last;
    findList(list, &first);
    u64 end = (u64)list + (u64)range;
    for (last = first; (last < gl.listCount) && (gl.lists[last].name < end); last++) free(gl.lists[last].words);
    memmove(&gl.lists[first], &gl.lists[last], (size_t)(gl.listCount - last)*sizeof(DisplayList));
    gl.listCount -= last - first;
}

GLboolean glIsList(GLuint list) { return findList(list, NULL) != NULL; }

//----------------------------------------------------------------------------------
// Declared only so that code like GLU links (see gl.h)
//----------------------------------------------------------------------------------
// GL 1.2: no 3D textures
void glTexImage3D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLsizei depth,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    (void)target; (void)level; (void)internalformat; (void)width; (void)height; (void)depth; (void)border;
    (void)format; (void)type; (void)pixels;
    setError(GL_INVALID_ENUM);
}

//----------------------------------------------------------------------------------
// OpenGL ES 1.1: float variants of the double functions and the fixed-point (16.16) API.
// Enum-valued parameters are passed unscaled in the x functions, like in ES
//----------------------------------------------------------------------------------
void glOrthof(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) { glOrtho(left, right, bottom, top, zNear, zFar); }
void glFrustumf(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top, GLfloat zNear, GLfloat zFar) { glFrustum(left, right, bottom, top, zNear, zFar); }
void glDepthRangef(GLclampf zNear, GLclampf zFar) { glDepthRange(zNear, zFar); }
void glClearDepthf(GLclampf depth) { glClearDepth(depth); }

void glClipPlanef(GLenum plane, const GLfloat *equation)
{
    double e[4] = { equation[0], equation[1], equation[2], equation[3] };
    setClipPlane(plane, e);
}

void glGetClipPlanef(GLenum plane, GLfloat *equation)
{
    const float *p = getClipPlane(plane);
    if (p != NULL) memcpy(equation, p, 4*sizeof(float));
}

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
           (pname == GL_TEXTURE_WRAP_S) || (pname == GL_TEXTURE_WRAP_T) || (pname == GL_GENERATE_MIPMAP) || (pname == GL_COORD_REPLACE_OES) ||
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
void glSampleCoveragex(GLclampx value, GLboolean invert) { glSampleCoverage(fixedToFloat(value), invert); }
void glPointParameterx(GLenum pname, GLfixed param) { glPointParameterf(pname, fixedToFloat(param)); }

void glPointParameterxv(GLenum pname, const GLfixed *params)
{
    GLfloat f[3] = { fixedToFloat(params[0]), 0.0f, 0.0f };
    if (pname == GL_POINT_DISTANCE_ATTENUATION) { f[1] = fixedToFloat(params[1]); f[2] = fixedToFloat(params[2]); }
    glPointParameterfv(pname, f);
}
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

void glFogx(GLenum pname, GLfixed param)
{
    if (pname == GL_FOG_COLOR) { setError(GL_INVALID_ENUM); return; }
    float f = (pname == GL_FOG_MODE)? (float)param : fixedToFloat(param);    // The mode is an enum, passed unscaled
    setFog(pname, &f);
}

void glFogxv(GLenum pname, const GLfixed *params)
{
    float f[4];
    if (pname == GL_FOG_COLOR) for (int i = 0; i < 4; i++) f[i] = fixedToFloat(params[i]);
    else f[0] = (pname == GL_FOG_MODE)? (float)params[0] : fixedToFloat(params[0]);
    setFog(pname, f);
}

void glClipPlanex(GLenum plane, const GLfixed *equation)
{
    double e[4];
    for (int i = 0; i < 4; i++) e[i] = equation[i]/65536.0;
    setClipPlane(plane, e);
}

void glGetClipPlanex(GLenum plane, GLfixed *equation)
{
    const float *p = getClipPlane(plane);
    if (p != NULL) for (int i = 0; i < 4; i++) equation[i] = floatToFixed(p[i]);
}

void glGetFixedv(GLenum pname, GLfixed *params)
{
    double v[16];
    bool normalized;
    int n = getState(pname, v, &normalized);
    for (int i = 0; i < n; i++) params[i] = floatToFixed(v[i]);
}
