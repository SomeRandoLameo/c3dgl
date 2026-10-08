// Frame and batch management: render state application, draw submission, GPU mesh cache
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Frame and batch management
//----------------------------------------------------------------------------------
static void freeGpuCache(GpuBufferCache *cache)
{
    gl.gpuCacheBytes -= cache->bytes;
    linearFree(cache->data);
    free(cache);
}

static void invalidateGpuCache(Buffer *b)
{
    GpuBufferCache *cache = b->gpuCache;
    if (cache == NULL) return;
    b->gpuCache = NULL;
    // Commands may reference this memory even after a frame was submitted.
    // frameSerial advances only after the GPU wait, never on FrameSplit/End.
    if (cache->drawnFrame && cache->drawnFrame == gl.frameSerial) {
        cache->next = gl.retiredGpuCaches;
        gl.retiredGpuCaches = cache;
    } else freeGpuCache(cache);
}

static void collectGpuCaches(void)
{
    GpuBufferCache **link = &gl.retiredGpuCaches;
    while (*link) {
        GpuBufferCache *cache = *link;
        if (cache->drawnFrame == gl.frameSerial) { link = &cache->next; continue; }
        *link = cache->next;
        freeGpuCache(cache);
    }
}

// Keep optional cache allocation bounded and leave linear memory for textures.
// Only evict buffers whose last GPU use has completed.
static bool makeGpuCacheRoom(size_t bytes)
{
    collectGpuCaches();
    while (gl.gpuCacheBytes + bytes > C3DGL_GPU_CACHE_BYTES ||
           linearSpaceFree() < bytes + 1024u * 1024u) {
        Buffer *oldest = NULL;
        for (GLuint i = 1; i < gl.bufferCount; i++) {
            GpuBufferCache *cache = gl.buffers[i].gpuCache;
            if (!cache || cache->drawnFrame == gl.frameSerial) continue;
            if (!oldest || cache->drawnFrame < oldest->gpuCache->drawnFrame) oldest = &gl.buffers[i];
        }
        if (!oldest) return false;
        invalidateGpuCache(oldest);
    }
    return true;
}

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

// Render target of the current screen, for the top one the current eye's while stereo is on
static int curTargetIndex(void)
{
    if ((gl.screen == C3DGL_SCREEN_TOP) && gl.stereo && (gl.eye == C3DGL_EYE_RIGHT)) return C3DGL_TARGET_RIGHT;
    return (int)gl.screen;
}

static C3D_RenderTarget *curTarget(void)
{
    return gl.targets[curTargetIndex()];
}

// Link the current screen's target to its display. Done on every switch, as the app may have
// changed the screen format in between (e.g. from the console to graphics)
static u64 gpuWaitTicks;    // Time spent blocked on the GPU or the display, see c3dglGetGpuWaitMs()
static u64 waitTicksTotal;  // The same, never reset (c3dglGetWaitTicksTotal())

// Time blocked on the GPU or the display, from `start` until now
static void addWait(u64 start)
{
    u64 ticks = svcGetSystemTick() - start;
    gpuWaitTicks += ticks;
    waitTicksTotal += ticks;
}

static void linkTarget(void)
{
    gfxScreen_t screen = (gl.screen == C3DGL_SCREEN_BOTTOM)? GFX_BOTTOM : GFX_TOP;
    gfx3dSide_t side = (curTargetIndex() == C3DGL_TARGET_RIGHT)? GFX_RIGHT : GFX_LEFT;
    const u32 flags = screenTransferFlags(screen);
    // About 1.5 ms per call in the Normal benchmark, twice a frame, but it is time spent waiting for the display to
    // be done with the previous frame: not calling it only moves the wait to C3D_FrameBegin
    PROF_ENTER();
    const u64 waitStart = svcGetSystemTick();
    C3D_RenderTargetSetOutput(curTarget(), screen, side, flags);
    addWait(waitStart);
    PROF_LEAVE(PB_LINK_TARGET, 0);
}


// C3D_FRAME_SYNCDRAW makes every frame start at a VBlank. That is only there so that a frame is not presented while the
// previous one is still being switched in, which can only happen when a frame is shorter than a refresh. A frame that took
// longer needs no alignment, and waiting for the next VBlank anyway cost half a refresh (8 ms) per frame on average. So the
// frame starts without the wait, and the wait is made before presenting, and only if the previous present was less than
// C3DGL_PRESENT_GAP_MS ago. Define C3DGL_PRESENT_GAP_MS as 0 for the old behaviour.
//
// One refresh (16.7 ms) is the most that can be shown: 16 ms keeps the cap at 60 frames per second. The value was 25
// before; a frame that took 22 to 25 ms then still waited for the VBlank and was held to 33 ms, which cost about 9 ms per
// frame (30.9 against 38.9 frames per second in the Normal benchmark in Azahar).
#ifndef C3DGL_PRESENT_GAP_MS
#define C3DGL_PRESENT_GAP_MS 16
#endif
static u64 lastPresentMs;

static void ensureFrame(void)
{
    if (gl.frameActive) return;

    // SYNCDRAW: waits until the GPU finished the previous frame, so the vertex buffer can be reused
    PROF_ENTER();
    u64 waitStart = svcGetSystemTick();
#if C3DGL_PRESENT_GAP_MS > 0
    C3D_FrameBegin(0);          // still waits until the GPU is done with the previous frame
#else
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
#endif
    addWait(waitStart);
    C3D_FrameDrawOn(curTarget());     // Also resets the viewport, hence batchValid = false
    PROF_LEAVE(PB_FRAME_BEGIN, 0);
    PROF_ENTER2();

    gl.frameActive = true;
    gl.compactBufBound = false;     // (the next compact draw binds its buffer configuration in full)
    gl.drawnThisFrame = false;
    gl.frameSerial++;           // The GPU is done with the previous frame
#ifdef C3DGL_PROFILE_GPU_CACHE
    if ((gl.frameSerial % 120) == 0) {
        LOG("GPU cache hits=%lu misses=%lu bytes=%lu linear_free=%lu\n",
            (unsigned long)gl.gpuCacheHits, (unsigned long)gl.gpuCacheMisses,
            (unsigned long)gl.gpuCacheBytes, (unsigned long)linearSpaceFree());
        gl.gpuCacheHits = gl.gpuCacheMisses = 0;
    }
#endif
    gl.vertexCount = 0;
    gl.batchStart = 0;
    gl.cacheFlushed = 0;
    gl.extraUsed = false;
    gl.batchValid = false;

    processDeferredDeletes();
    collectGpuCaches();

    // The GPU is done with the pixel rectangles of the previous frame; one chunk is kept for the next ones
    for (int i = 1; i < gl.pixelChunkCount; i++) linearFree(gl.pixelChunks[i].data);
    if (gl.pixelChunkCount > 1) gl.pixelChunkCount = 1;
    if (gl.pixelChunkCount) gl.pixelChunks[0].used = gl.pixelChunks[0].flushed = 0;
    gl.atlas = NULL;
    gl.stippleTex = NULL;
    PROF_LEAVE2(PB_FRAME_SETUP, 0);
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
        c->data = cacheAwareLinearAlloc(c->size);
        if (c->data == NULL) { LOG("Out of memory for drawing pixels\n"); setError(GL_OUT_OF_MEMORY); return NULL; }
        c->used = c->flushed = 0;
        gl.pixelChunkCount++;
    }
    u8 *p = c->data + c->used;
    c->used += size;
    return p;
}

// Submit the vertices collected since the last flush with the currently applied state
// Back to the vertex layout of gl.vbo after compact cache draws (drawCompactCache())
static void useStandardLayout(void)
{
    if (!gl.compactLayout) return;
    C3D_SetAttrInfo(&gl.standardAttrInfo);
    C3D_SetBufInfo(&gl.standardBufInfo);
    C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocQBias, 0.0f, 0.0f, 0.0f, 0.0f);
    gl.compactLayout = false;
    gl.compactBufBound = false;
}

static void flush(void)
{
    int count = gl.vertexCount - gl.batchStart;
    if (count <= 0) return;
    useStandardLayout();

    {
        PROF_ENTER();
        C3D_DrawArrays(GPU_TRIANGLES, gl.batchStart, count);
        PROF_LEAVE(PB_FLUSH, count);
    }
    if (gl.batch.units[1].texture || gl.batch.units[2].texture) gl.extraUsed = true;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++) gl.textures[gl.batch.units[unit].texture].drawnFrame = gl.frameSerial;

    gl.batchStart = gl.vertexCount;
    gl.drawnThisFrame = true;
}

// Before the command list goes to the GPU (C3D_FrameSplit, C3D_FrameEnd): the GPU only reads the vertex buffer
// from then on, so the vertices written since the last submission are flushed from the CPU cache in one go
// (instead of a GSP call per batch)
#ifdef C3DGL_PROFILE
static void flushVertexCacheBody(void)
#else
static void flushVertexCache(void)
#endif
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

#ifdef C3DGL_PROFILE
static void flushVertexCache(void)
{
    PROF_ENTER();
    flushVertexCacheBody();
    PROF_LEAVE(PB_FLUSH_CACHE, 0);
}
#endif

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
static GPU_TEVSRC texSource(int unit)
{
    int pica = unit + gl.texUnitShift;
    return (GPU_TEVSRC)(GPU_TEXTURE0 + ((pica < C3DGL_TEXTURE_UNITS)? pica : C3DGL_TEXTURE_UNITS - 1));
}
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
// texcoords, transformed on the CPU already). pica: the PICA unit sampling it, whose uniforms are set
static void applyTextureMatrix(int unit, int pica, const Texture *t, bool identity, bool *projective)
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
        C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[pica] + row, r[0], r[1], r[2], r[3]);
    }
    C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[pica] + 2, tm->m[3], tm->m[7], tm->m[11], tm->m[15]);

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

// Polygon stipple: TexEnv stage 3 gives the fragments outside the pattern (stipple texel alpha 0) an alpha that fails
// the alpha test, which is set up here; returns true for alpha 1 (alpha + (1 - texel alpha)), false for alpha 0
// (alpha - (1 - texel alpha)). Both saturate exactly and keep the alpha in the pattern (an interpolation rounds on real
// hardware: an alpha of 1/255 came out as 0). A test that passes 0 and 1 becomes alpha != 0, so fragments of alpha 0 are
// dropped as well; alpha != r (0 < r < 1) becomes alpha != 0 too, so fragments of alpha r are no longer dropped then
static bool stippleAlphaTest(const DrawState *s, GPU_TESTFUNC *func, int *ref)
{
    GLenum f = s->alphaTest? s->alphaFunc : GL_ALWAYS;
    int r = s->alphaRef;
    *func = testFunc(f);
    *ref = r;
    switch (f)
    {
        case GL_NEVER: case GL_GREATER: return false;
        case GL_LESS: return true;
        case GL_LEQUAL: if (r < 255) return true; break;
        case GL_GEQUAL: if (r > 0) return false; break;
        case GL_EQUAL: return r == 0;
        case GL_NOTEQUAL:
            if ((r == 0) || (r == 255)) return r == 255;
            WARN_ONCE("Polygon stipple with glAlphaFunc(GL_NOTEQUAL): fragments of the reference alpha are drawn\n");
            break;
        default: break;
    }
    *func = GPU_NOTEQUAL;
    *ref = 0;
    return false;
}

// Set the GPU state of a batch. prev: the state applied for the previous batch (NULL: unknown), only what differs
// from it is set: citro3d re-sends every group that is set, changed or not
#define CHANGED(field) ((prev == NULL) || (memcmp(&s->field, &prev->field, sizeof(s->field)) != 0))

static void applyMatrixState(const DrawState *s, const DrawState *prev, bool stippleChanged);

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
    if (CHANGED(alphaTest) || CHANGED(alphaFunc) || CHANGED(alphaRef) || CHANGED(stipple))
    {
        // Stage 3 passes the color through; with polygon stipple alpha = stipple? alpha : the failing alpha
        C3D_TexEnv *env = C3D_GetTexEnv(3);
        C3D_TexEnvInit(env);
        if (s->stipple)
        {
            GPU_TESTFUNC func;
            int ref;
            bool high = stippleAlphaTest(s, &func, &ref);
            C3D_AlphaTest(true, func, ref);
            C3D_TexEnvSrc(env, C3D_Alpha, GPU_PREVIOUS, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
            C3D_TexEnvOpAlpha(env, GPU_TEVOP_A_SRC_ALPHA, GPU_TEVOP_A_ONE_MINUS_SRC_ALPHA, GPU_TEVOP_A_SRC_ALPHA);
            C3D_TexEnvFunc(env, C3D_Alpha, high? GPU_ADD : GPU_SUBTRACT);
        }
        else C3D_AlphaTest(s->alphaTest, testFunc(s->alphaFunc), s->alphaRef);
    }

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
    // Pixel rectangles have all units unused but set stage 0 themselves. With polygon stipple GL unit n is PICA unit
    // n + 1 (unit 2 is unused then, see drawKey()), PICA unit 0 samples the pattern
    bool pixelChanged = CHANGED(pixelMode) || CHANGED(pixelTex);
    bool stippleChanged = CHANGED(stipple);
    // The vertex shader computes the texcoords of units 1 and 2 only for draws that sample them (not used with stipple)
    if ((prev == NULL) || stippleChanged || CHANGED(units[1].texture) || CHANGED(units[2].texture))
        C3D_BoolUnifSet(GPU_VERTEX_SHADER, gl.uLocUnits12, (s->units[1].texture != 0) || (s->units[2].texture != 0));
    gl.texUnitShift = s->stipple? 1 : 0;
    if (s->stipple && (stippleChanged || CHANGED(stippleTex))) C3D_TexBind(0, (C3D_Tex *)s->stippleTex);
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
    {
        int pica = unit + gl.texUnitShift;
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
        if ((prev != NULL) && !((unit == 0) && pixelChanged) && !stippleChanged && (memcmp(u, &prev->units[unit], sizeof(*u)) == 0) &&
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
            if (pica < C3DGL_TEXTURE_UNITS) C3D_TexBind(pica, pica? &gl.dummyTexture : NULL);
            continue;
        }

        Texture *t = &gl.textures[u->texture];
        bool projective;
        applyTextureMatrix(unit, pica, t, sprite || texGen, &projective);

        // Unit 0 can let PICA divide s and t by q per pixel (projection mode); units 1/2 divide per vertex in the shader
        if (pica == 0)
        {
            projective = projective || s->texQ;
            t->tex.param = (t->tex.param & ~GPU_TEXTURE_MODE(7)) | GPU_TEXTURE_MODE(projective? GPU_TEX_PROJECTION : GPU_TEX_2D);
        }
        else t->tex.param = (t->tex.param & ~GPU_TEXTURE_MODE(7)) | GPU_TEXTURE_MODE(GPU_TEX_2D);

        C3D_TexBind(pica, &t->tex);
        setupTexEnv(env, unit, &u->env, t->format.format, t->base == GL_INTENSITY);
    }

    applyMatrixState(s, prev, stippleChanged);
}

// The part of the GPU state that follows the modelview and projection matrices: the MVP uniform and the stipple
// texcoord rows. (The fog table follows the projection as well, see updateFogLut().)
static void applyMatrixState(const DrawState *s, const DrawState *prev, bool stippleChanged)
{
    if (CHANGED(clipSpace) || CHANGED(matrixSerial))
    {
        Mat4 mvp = gl.post;
        if (!s->clipSpace) mat4Mul(&mvp, &gl.post, projectionModelview());
        C3D_Mtx mtx;
        mat4ToC3D(&mvp, &mtx);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, gl.uLocMvp, &mtx);
    }

    // Stipple texcoords (s, t, q) = (window x/32, window y/32, 1)*w_clip from the object position, PICA divides by q
    // per pixel: pixel centers sample texel centers. Stippled batches are never in clip space
    if (stippleChanged) C3D_BoolUnifSet(GPU_VERTEX_SHADER, gl.uLocStipple, s->stipple);
    if (s->stipple && (stippleChanged || CHANGED(matrixSerial) || CHANGED(viewport)))
    {
        const float *m = projectionModelview()->m;
        float halfW = 0.5f*(float)s->viewport[2], halfH = 0.5f*(float)s->viewport[3];
        float ox = (float)s->viewport[0] + halfW, oy = (float)s->viewport[1] + halfH;
        float row[3][4];
        for (int i = 0; i < 4; i++)
        {
            row[0][i] = (m[4*i]*halfW + m[4*i + 3]*ox)/32.0f;
            row[1][i] = (m[4*i + 1]*halfH + m[4*i + 3]*oy)/32.0f;
            row[2][i] = m[4*i + 3];
        }
        for (int r = 0; r < 3; r++) C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocTexMat[0] + r, row[r][0], row[r][1], row[r][2], row[r][3]);
    }
}

#undef CHANGED

// Rare allocation-pressure path: finish queued draws before discarding caches,
// then continue the same logical frame. Required allocations retain priority.
static bool reclaimGpuCaches(void)
{
    if (!gl.ready || !gl.gpuCacheBytes) return false;
    bool used[C3DGL_TARGET_COUNT];
    bool active = gl.frameActive;
    if (active) {
        for (int i = 0; i < C3DGL_TARGET_COUNT; i++) used[i] = (gl.targets[i] != NULL) && gl.targets[i]->used;
        flushVertexCache();
        C3D_FrameEnd(GX_CMDLIST_FLUSH);
    }
    C3D_FrameBegin(0); // Wait for all references to immutable cache storage.
    gl.frameSerial++;
    if (active) {
        C3D_FrameDrawOn(curTarget());
        for (int i = 0; i < C3DGL_TARGET_COUNT; i++) if (gl.targets[i] != NULL) gl.targets[i]->used = used[i];
    } else C3D_FrameEnd(0);
    gl.batchValid = false;
    collectGpuCaches();
    for (GLuint i = 1; i < gl.bufferCount; i++) invalidateGpuCache(&gl.buffers[i]);
    return true;
}

static bool textureValid(GLuint slot)
{
    return (slot > 0) && (slot < TEXTURE_SLOTS) && gl.textures[slot].loaded;
}

// Slot in gl.textures of a texture bound to target: its id, texture 0 is the target's default texture
static GLuint textureSlot(GLenum target, GLuint id)
{
    return id? id : (target == GL_TEXTURE_1D)? DEFAULT_TEXTURE_1D : DEFAULT_TEXTURE_2D;
}

// C3DGL_MATRIX_FAST_PATH=0 turns it off for comparisons; the regression test switches it at run time
#ifndef C3DGL_MATRIX_FAST_PATH
#define C3DGL_MATRIX_FAST_PATH 1
#endif
static bool matrixFastPath = C3DGL_MATRIX_FAST_PATH;

// Start a new batch if key differs from the applied state. memcmp: keys are built with zeroed padding
static void useState(const DrawState *key)
{
    ensureFrame();

    if (gl.batchValid)
    {
        // Chunk meshes, entities and the like are drawn one after another with a matrix of their own and nothing else
        // changed: applyState() would compare every group only to find the matrices different, so they are set
        // directly. One comparison tells both: the key against the batch state with the key's matrix serial
        const u32 applied = gl.batch.matrixSerial;
        const bool otherMatrix = matrixFastPath && (key->matrixSerial != applied);
        gl.batch.matrixSerial = otherMatrix? key->matrixSerial : applied;
        const bool same = memcmp(key, &gl.batch, sizeof(DrawState)) == 0;
        gl.batch.matrixSerial = applied;
        if (same && !otherMatrix) return;
        if (same)
        {
            flush();
            PROF_ENTER();
            if (key->fog) updateFogLut(key);        // (follows the projection)
            applyMatrixState(key, &gl.batch, false);
            gl.batch.matrixSerial = key->matrixSerial;
            PROF_LEAVE(PB_APPLY_MATRIX, 0);
            return;
        }
    }

    {
        flush();
        {
            PROF_ENTER();
            applyState(key, gl.batchValid? &gl.batch : NULL);
            PROF_LEAVE(PB_APPLY_STATE, 0);
        }
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

    // Polygon stipple applies to filled polygons, which are not drawn in clip space. The pattern takes PICA unit 0
    if (gl.polygonStipple && !clipSpace)
    {
        if (key.units[2].texture) WARN_ONCE("Polygon stipple needs texture unit 2, which is in use: drawn without stipple\n");
        else if ((key.stippleTex = stippleTexture()) != NULL) key.stipple = true;
    }

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
    PROF_ENTER();
    DrawState key;
    drawKey(&key, clipSpace, points);
    useState(&key);
    PROF_LEAVE(PB_PREPARE, 0);
}

// How many vertices of gl.vbo the frame's draws may fill: all but the reserve
static int vertexLimit(void)
{
    return C3DGL_MAX_VERTICES - gl.vertexReserve;
}

static bool reserveVertices(int count)
{
    if (gl.vertexCount + count <= vertexLimit()) return true;

    WARN_ONCE("Vertex buffer full (%i vertices per frame), dropping geometry\n", C3DGL_MAX_VERTICES);
    return false;
}
