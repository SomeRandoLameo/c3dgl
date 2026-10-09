// Defines and types: tuning constants, Vertex, textures, buffers, display lists
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------------------
#ifndef C3DGL_MAX_VERTICES
#define C3DGL_MAX_VERTICES      (64*1024)
#endif
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
#define C3DGL_TARGET_COUNT      3       // top left eye, bottom, top right eye (index C3DGL_TARGET_RIGHT, only with stereo)
#define C3DGL_TARGET_RIGHT      2

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
    u32 drawnFrame;             // gl.frameSerial when a draw last used tex (0: none), see textureBusy()
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
    // Perspective draws store depth linear in the eye distance (W buffering) instead of z/w: the fog table is indexed by
    // depth, and z/w leaves almost all of the range to the first few blocks. wNear, wFar: the projection's planes
    bool wDepth;
    float wNear, wFar;
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
    bool stipple;               // Polygon stipple: stippleTex on PICA unit 0, GL units 0, 1 on PICA units 1, 2
    const C3D_Tex *stippleTex;
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

// Optional immutable expansion of a static indexed VBO. Raw OpenGL storage remains
// authoritative; unsupported state still uses the ordinary vertex pipeline.
typedef struct GpuBufferCache {
    struct GpuBufferCache *next;
    u8 *data;
    size_t bytes;
    u32 drawnFrame;
    u64 indexRevision;
    size_t indexOffset;
    int count;
    ClientArray arrays[3];
    GLenum shadeModel;
    float depthBias;
    bool compact;               // data holds the buffer's own vertices (16 bytes each), drawn indexed (drawCompactCache)
    const u16 *indices;         // compact: the indices, gl.quadIndices or behind the vertices in data
} GpuBufferCache;

#ifndef C3DGL_GPU_CACHE_BYTES
#define C3DGL_GPU_CACHE_BYTES (4u * 1024u * 1024u)
#endif

// Buffer object (VBO), with an optional GPU-ready triangle expansion.
typedef struct {
    bool used;                  // Id handed out by glGenBuffers or created by glBindBuffer
    u8 *data;
    GLsizeiptr size;
    GLenum usage;
    u64 revision;
    GpuBufferCache *gpuCache;
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
    X(CLEAR_ACCUM,      glClearAccum(w[0].f, w[1].f, w[2].f, w[3].f)) \
    X(ACCUM,            glAccum(w[0].u, w[1].f)) \
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
    X(LINE_STIPPLE,     glLineStipple(w[0].i, (GLushort)w[1].u)) \
    X(POLYGON_STIPPLE,  setPolygonStipple(&w[0])) \
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
