// OpenGL: attribute stacks
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: attribute stacks (glPushAttrib, glPushClientAttrib)
//
// A push saves a snapshot of everything; a pop restores only the groups of the pushed mask (GL 1.1 tables
// 6.x). When a feature moves out of ignoredCaps, its state has to be added here.
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
    bool lineStipple, polygonStipple;
    GLint lineStippleFactor;
    GLushort lineStipplePattern;
    u8 polygonStipplePattern[128];
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
    float clearAccum[4];
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
    a->lineStipple = gl.lineStipple;
    a->lineStippleFactor = gl.lineStippleFactor;
    a->lineStipplePattern = gl.lineStipplePattern;
    a->polygonStipple = gl.polygonStipple;
    memcpy(a->polygonStipplePattern, gl.polygonStipplePattern, sizeof(a->polygonStipplePattern));
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
    memcpy(a->clearAccum, gl.clearAccum, sizeof(a->clearAccum));
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
        gl.lineStipple = a->lineStipple;
        gl.lineStippleFactor = a->lineStippleFactor;
        gl.lineStipplePattern = a->lineStipplePattern;
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
        gl.polygonStipple = a->polygonStipple;
        caps |= capBits((const GLenum[]){ GL_POLYGON_SMOOTH }, 1);
    }
    if ((mask & GL_POLYGON_STIPPLE_BIT) &&
        (memcmp(gl.polygonStipplePattern, a->polygonStipplePattern, sizeof(gl.polygonStipplePattern)) != 0))
    {
        memcpy(gl.polygonStipplePattern, a->polygonStipplePattern, sizeof(gl.polygonStipplePattern));
        gl.stippleTex = NULL;
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
    if (mask & GL_ACCUM_BUFFER_BIT) memcpy(gl.clearAccum, a->clearAccum, sizeof(gl.clearAccum));
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
        gl.lineStipple = a->lineStipple;
        gl.polygonStipple = a->polygonStipple;
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
            bool wrapChanged = (t->wrapS != p->wrapS) || (t->wrapT != p->wrapT);
            t->wrapS = p->wrapS;
            t->wrapT = p->wrapT;
            t->generateMipmap = p->generateMipmap;
            t->priority = p->priority;
            memcpy(t->borderColor, p->borderColor, sizeof(t->borderColor));
            if (t->loaded)
            {
                textureModified(slot);
                applyTextureParams(t);
                if (wrapChanged) { copyOnWrite(t); flushTexture(t); }
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
