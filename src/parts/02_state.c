// Global state (the gl struct), matrix helpers, GL -> PICA enum mapping
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Global state
//----------------------------------------------------------------------------------
static struct {
    bool ready;
    C3D_RenderTarget *targets[C3DGL_TARGET_COUNT];
    C3DGLscreen screen;                 // Screen drawn on, see c3dglSetScreen()
    C3DGLeye eye;                       // Eye of the top screen drawn on while stereo is on, see c3dglSetEye()
    bool stereo;                        // gfxSet3D(true), the right eye target exists
    DVLB_s *dvlb;
    shaderProgram_s program;
    int uLocMvp, uLocTexMat[C3DGL_TEXTURE_UNITS], uLocStipple, uLocQBias, uLocUnits12;
    // Vertex layouts: the standard one of gl.vbo and expanded caches, and the compact one that reads a buffer's own
    // 16-byte vertices (short position, short texcoord, ubyte color) from its cache (drawCompactCache())
    C3D_AttrInfo standardAttrInfo, compactAttrInfo;
    C3D_BufInfo standardBufInfo;
    bool compactLayout;                 // the compact layout is set on the GPU state
    int vertexReserve;                  // vertices of gl.vbo kept back from the draws (c3dglReserveVertices())
    bool compactBufBound;               // ...and its buffer configuration: a draw only moves buffer 0 (drawCompactCache())
    float compactBias;                  // depth bias last set as its fixed attribute
    u16 *quadIndices;                   // 0 1 2 0 2 3, 4 5 6 4 6 7, ... in linear memory, C3DGL_MAX_VERTICES of them
    int texUnitShift;                   // PICA unit of GL unit 0 in the batch being set up (1 with polygon stipple)
    Mat4 post;                          // OpenGL clip space -> PICA clip space (rotation, depth range)

    // Frame and vertex batching
    bool frameActive;
    bool drawnThisFrame;
#ifdef C3DGL_PROFILE_GPU_CACHE
    u32 gpuCacheHits, gpuCacheMisses;
#endif
    u64 bufferRevision;
    size_t gpuCacheBytes;
    GpuBufferCache *retiredGpuCaches;
    u32 frameSerial;                    // Counts the waits for the GPU (frame begun, frame resumed), see textureBusy()
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
    bool lineStipple;                   // GL_LINE_STIPPLE
    GLint lineStippleFactor;            // glLineStipple, 1..256
    GLushort lineStipplePattern;
    u32 stippleCounter;                 // Fragments of the strip so far, see emitStippledLine()
    bool polygonStipple;                // GL_POLYGON_STIPPLE
    u8 polygonStipplePattern[128];      // glPolygonStipple: 32 rows of 4 bytes from the bottom, most significant bit first
    const C3D_Tex *stippleTex;          // The pattern as a texture in this frame's pixel memory, NULL: not made yet
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

    // Accumulation buffer (GL), see glAccum()
    u32 *accum[C3DGL_TARGET_COUNT];     // Per screen, NULL until used
    float clearAccum[4];                // glClearAccum
} gl;

// Display list recording, see the display list section
static void listSave(ListCommand command, const char *format, ...);
static ListWord *listBegin(ListCommand command, int words);
static void listEnd(void);
static void listSaveImage(ListCommand command, const GLint args[8], GLsizei width, GLsizei height, bool sizeValid,
                          bool oneD, const void *pixels);
static void clearAccum(void);
static void listArrayElement(int index);
static void listPixels(const ListWord *w, bool bitmap);
static void setRasterPos(float x, float y, float z, float w);
static void setPixelMap(GLenum map, GLsizei mapsize, const GLfloat *values);
static void initTexture(Texture *t);
static const C3D_Tex *stippleTexture(void);
static void setPolygonStipple(const ListWord *w);
static void packBitmap(const u8 *data, const PixelStore *ps, int width, int height, u8 *dst);
static bool reclaimGpuCaches(void);

// Optional mesh caches must yield memory to ordinary OpenGL allocations.
static void *cacheAwareLinearAlloc(size_t bytes)
{
    void *data = linearAlloc(bytes);
    if (!data && reclaimGpuCaches()) data = linearAlloc(bytes);
    return data;
}

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
