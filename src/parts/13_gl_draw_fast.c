// Direct-decode fast paths (indexed and array triangles), glDrawElements
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// Fast path for indexed triangles
//----------------------------------------------------------------------------------
// Most of a frame's geometry (chunk meshes, parts of the GUI) is drawn as indexed
// triangles with one shared vertex layout: GL_SHORT position and texcoords plus a
// GL_UNSIGNED_BYTE color. With no lighting, texgen, clip planes or polygon modes the
// generic path costs, per index, three readArray() calls (each a per component type
// switch and a float conversion), a whole Vertex copy, primitive assembly and a second
// Vertex copy - far and away the largest CPU cost of a frame. For that layout the
// vertices are decoded straight into the GPU vertex buffer instead; anything unusual
// falls back to the generic loop.
typedef struct {
    float pos[3];
    float tex[3];
    u8 color[4];
    float depthBias;
} GpuVertex;                        // First GPU_VERTEX_SIZE bytes of a Vertex

static u8 byteColor[256];           // colorByte(i / 255.0f), the normalized byte color
static bool byteColorReady = false;

static void initByteColor(void)
{
    if (byteColorReady) return;
    for (int i = 0; i < 256; i++) byteColor[i] = colorByte((float)i/255.0f);
    byteColorReady = true;
}

static inline s16 loadS16(const u8 *p)
{
    s16 v;
    memcpy(&v, p, 2);
    return v;
}

// Every element of `a` up to `maxIndex` has to be inside its buffer object
// (the generic path skips single vertices that are not: give up on the call instead)
static bool arrayInRange(const ClientArray *a, int maxIndex)
{
    if (a->buffer == 0) return true;
    size_t elem = (size_t)a->size*typeSize(a->type);
    size_t stride = a->stride? (size_t)a->stride : elem;
    return bufferRange(a->buffer, a->pointer, (size_t)maxIndex*stride, elem) != NULL;
}

static const char *fastPathOff = NULL;
static void fastPathReject(const char *why)
{
    PROF_REJECT(why);
    if (fastPathOff == NULL) { fastPathOff = why; LOG("Indexed fast path unused: %s\n", why); }
}

// citro3d's context (see c3dglSubmit())
extern u8 __C3D_Context[];

// A compact cache: the buffer's own vertices, read in their 16-byte layout and drawn with indices. Switching to that
// layout and back (useStandardLayout()) happens only where draws of the two kinds meet, not per draw
static void drawCompactCache(GpuBufferCache *cache)
{
    flush();  // Preserve submission order with ordinary immediate/array geometry.
    PROF_ENTER();
    if (!gl.compactLayout)
    {
        C3D_SetAttrInfo(&gl.compactAttrInfo);
        C3D_FVUnifSet(GPU_VERTEX_SHADER, gl.uLocQBias, 1.0f, 0.0f, 0.0f, 0.0f);
        C3D_FixedAttribSet(4, 0.0f, 0.0f, 1.0f, 0.0f);     // Texcoords of units 1 and 2 (off): q = 1
        C3D_FixedAttribSet(5, 0.0f, 0.0f, 1.0f, 0.0f);
        C3D_FixedAttribSet(3, cache->depthBias, 0.0f, 0.0f, 0.0f);
        gl.compactBias = cache->depthBias;
        gl.compactLayout = true;
        gl.compactBufBound = false;
    }
    else if (memcmp(&gl.compactBias, &cache->depthBias, sizeof(float)) != 0)
    {
        C3D_FixedAttribSet(3, cache->depthBias, 0.0f, 0.0f, 0.0f);
        gl.compactBias = cache->depthBias;
    }
    if (gl.compactBufBound)
    {
        // Compact draws in a row differ in the address of buffer 0 alone: that register instead of citro3d binding the
        // whole configuration again (12 buffers). Its copy in citro3d's context (behind the GX queue and the state
        // flags, see C3D_GetBufInfo()) follows, so a later full bind writes the same. Offset as in BufInfo_Add()
        C3D_BufInfo *bound = (C3D_BufInfo *)(__C3D_Context + 64);
        const u32 offset = osConvertVirtToPhys(cache->data) - bound->base_paddr;
        bound->buffers[0].offset = offset;
        GPUCMD_AddWrite(GPUREG_ATTRIBBUFFER0_OFFSET, offset);
    }
    else
    {
        C3D_BufInfo info;
        BufInfo_Init(&info);
        BufInfo_Add(&info, cache->data, 16, 3, 0x210);
        C3D_SetBufInfo(&info);
        gl.compactBufBound = true;
    }
    C3D_DrawElements(GPU_TRIANGLES, cache->count, C3D_UNSIGNED_SHORT, cache->indices);
    cache->drawnFrame = gl.frameSerial;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
        gl.textures[gl.batch.units[unit].texture].drawnFrame = gl.frameSerial;
    gl.drawnThisFrame = true;
    PROF_LEAVE(PB_DRAWCACHE, 0);
}

static void drawGpuCache(GpuBufferCache *cache)
{
    if (cache->compact) { drawCompactCache(cache); return; }
    flush();  // Preserve submission order with ordinary immediate/array geometry.
    useStandardLayout();
    PROF_ENTER();
    C3D_BufInfo saved = *C3D_GetBufInfo();
    C3D_BufInfo info;
    BufInfo_Init(&info);
    BufInfo_Add(&info, cache->data, GPU_VERTEX_SIZE, 4, 0x3210);
    BufInfo_Add(&info, gl.vboExtra, GPU_EXTRA_SIZE, 2, 0x54);
    C3D_SetBufInfo(&info);
    C3D_DrawArrays(GPU_TRIANGLES, 0, cache->count);
    C3D_SetBufInfo(&saved);
    cache->drawnFrame = gl.frameSerial;
    for (int unit = 0; unit < C3DGL_TEXTURE_UNITS; unit++)
        gl.textures[gl.batch.units[unit].texture].drawnFrame = gl.frameSerial;
    gl.drawnThisFrame = true;
    PROF_LEAVE(PB_DRAWCACHE, 0);
}

// What both direct-decode paths (indexed and array triangles) need from the current state: no lighting,
// texgen, clip planes, polygon modes, second/third texture unit or per-vertex normal/edge/point size. Which of
// vertex, texcoord 0 and color arrays are needed is up to each path.
static bool fastPathStateOk(void)
{
    if (gl.renderMode != GL_RENDER) { fastPathReject("render mode"); return false; }
    if (gl.lightingEnabled) { fastPathReject("lighting"); return false; }
    if (gl.texGen[0].enabled || gl.texGen[1].enabled || gl.texGen[2].enabled) { fastPathReject("texgen"); return false; }
    if (gl.clipEnabled) { fastPathReject("clip"); return false; }
    if ((gl.polygonMode[0] != GL_FILL) || (gl.polygonMode[1] != GL_FILL) || gl.offsetFill) { fastPathReject("polygon mode"); return false; }
    if (gl.batch.units[1].texture || gl.batch.units[2].texture) { fastPathReject("units 1/2 texture"); return false; }
    if (arrayActive(ARRAY_TEXCOORD1) || arrayActive(ARRAY_TEXCOORD2)) { fastPathReject("texcoord 1/2 array"); return false; }
    if (arrayActive(ARRAY_NORMAL) || arrayActive(ARRAY_EDGEFLAG) || arrayActive(ARRAY_POINTSIZE)) { fastPathReject("normal/edge/pointsize array"); return false; }
    if (sizeof(GpuVertex) != GPU_VERTEX_SIZE) { fastPathReject("vertex layout"); return false; }
    return true;
}

// The compact cache (drawCompactCache()) for n indices of `index` into `source`, NULL if the arrays are not one
// 16-byte vertex (short x, y, z, pad, short s, t, ubyte RGBA) or memory is short; the caller then expands as before.
// It holds the vertices 0 .. max index as they are, at a third of an expansion (6 vertices of 32 bytes per quad).
// Flat shading takes the color of a triangle's last vertex: only triangles of one color can share vertices.
static GpuBufferCache *makeCompactCache(Buffer *source, const ClientArray *av, const ClientArray *at,
                                        const ClientArray *ac, const u16 *index, int n)
{
    const uintptr_t base = (uintptr_t)av->pointer;
    if ((av->stride != 16) || (at->stride != 16) || (ac->stride != 16) ||
        ((uintptr_t)at->pointer != base + 8) || ((uintptr_t)ac->pointer != base + 12)) return NULL;
    int maxIndex = 0;
    bool quads = true;      // 0 1 2 0 2 3, 4 5 6 4 6 7, ...: gl.quadIndices serves
    static const u8 quadPattern[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < n; i++)
    {
        if (index[i] > maxIndex) maxIndex = index[i];
        quads = quads && (index[i] == (i/6)*4 + quadPattern[i % 6]);
    }
    const size_t vertexBytes = (size_t)(maxIndex + 1)*16;
    if (base + vertexBytes > (size_t)source->size) return NULL;
    const u8 *vertices = source->data + base;
    if (gl.shadeModel == GL_FLAT)
    {
        for (int i = 0; i < n; i += 3)
        {
            u32 c0, c1, c2;
            memcpy(&c0, vertices + (size_t)index[i]*16 + 12, 4);
            memcpy(&c1, vertices + (size_t)index[i + 1]*16 + 12, 4);
            memcpy(&c2, vertices + (size_t)index[i + 2]*16 + 12, 4);
            if ((c0 != c1) || (c1 != c2)) return NULL;
        }
    }
    if (quads && (gl.quadIndices == NULL))
    {
        gl.quadIndices = linearAlloc(C3DGL_MAX_VERTICES*sizeof(u16));
        if (gl.quadIndices == NULL) return NULL;
        for (int i = 0; i < C3DGL_MAX_VERTICES; i++) gl.quadIndices[i] = (u16)((i/6)*4 + quadPattern[i % 6]);
        GSPGPU_FlushDataCache(gl.quadIndices, C3DGL_MAX_VERTICES*sizeof(u16));
    }
    const size_t bytes = vertexBytes + (quads? 0 : (size_t)n*sizeof(u16));
    if (bytes > C3DGL_GPU_CACHE_BYTES || !makeGpuCacheRoom(bytes)) return NULL;
    GpuBufferCache *cache = calloc(1, sizeof(*cache));
    if (cache == NULL) return NULL;
    cache->data = linearAlloc(bytes);
    if (cache->data == NULL) { free(cache); return NULL; }
    cache->bytes = bytes;
    cache->compact = true;
    memcpy(cache->data, vertices, vertexBytes);
    if (quads) cache->indices = gl.quadIndices;
    else
    {
        u16 *own = (u16 *)(cache->data + vertexBytes);
        memcpy(own, index, (size_t)n*sizeof(u16));
        cache->indices = own;
    }
    GSPGPU_FlushDataCache(cache->data, bytes);
    return cache;
}

// Returns true if the call was submitted
static bool indexedTriangleFastPath(GLenum mode, GLenum type, const u8 *data, int count)
{
    if ((mode != GL_TRIANGLES) || (type != GL_UNSIGNED_SHORT)) { fastPathReject("mode/type"); return false; }
    if (!fastPathStateOk()) return false;
    if (!arrayActive(ARRAY_VERTEX) || !arrayActive(ARRAY_TEXCOORD0) || !arrayActive(ARRAY_COLOR)) { fastPathReject("missing array"); return false; }

    const ClientArray *av = &gl.arrays[ARRAY_VERTEX];
    const ClientArray *at = &gl.arrays[ARRAY_TEXCOORD0];
    const ClientArray *ac = &gl.arrays[ARRAY_COLOR];
    if ((av->type != GL_SHORT) || (av->size != 3)) { fastPathReject("vertex array type"); return false; }
    if ((at->type != GL_SHORT) || (at->size != 2)) { fastPathReject("texcoord array type"); return false; }
    if ((ac->type != GL_UNSIGNED_BYTE) || (ac->size != 4)) { fastPathReject("color array type"); return false; }

    int n = count - (count % 3);
    if (n <= 0) return true;
    // Cache only immutable-usage buffer-backed input. Each key includes the
    // complete layout, index storage generation/range and baked vertex state.
    // Matrices, textures, fog and all other draw state remain in applyState().
    Buffer *source = NULL;
    GpuBufferCache *cache = NULL;
    bool cacheable = gl.ready && av->buffer && av->buffer == at->buffer &&
        av->buffer == ac->buffer && gl.elementArrayBuffer && n <= C3DGL_MAX_VERTICES &&
        (size_t)n * GPU_VERTEX_SIZE <= C3DGL_GPU_CACHE_BYTES;
    if (cacheable) {
        source = &gl.buffers[av->buffer];
        Buffer *ib = &gl.buffers[gl.elementArrayBuffer];
        cacheable = source->usage == GL_STATIC_DRAW && ib->usage == GL_STATIC_DRAW;
        if (cacheable) {
            cache = source->gpuCache;
            size_t offset = (size_t)(data - ib->data);
            if (cache && cache->indexRevision == ib->revision && cache->indexOffset == offset &&
                cache->count == n && cache->shadeModel == gl.shadeModel &&
                memcmp(&cache->depthBias, &gl.current.depthBias, sizeof(float)) == 0 &&
                memcmp(&cache->arrays[0], av, sizeof(ClientArray)) == 0 &&
                memcmp(&cache->arrays[1], at, sizeof(ClientArray)) == 0 &&
                memcmp(&cache->arrays[2], ac, sizeof(ClientArray)) == 0) {
#ifdef C3DGL_PROFILE_GPU_CACHE
                gl.gpuCacheHits++;
#endif
                drawGpuCache(cache);
                PROF_PATH(PB_EL_CACHE);
                return true;
            }
            invalidateGpuCache(source);
            cache = NULL;
        }
    }
    // The buffer's own vertices if their layout allows, an expansion otherwise
    if (cacheable && arrayInRange(av, 0)) {
        GpuBufferCache *compact = makeCompactCache(source, av, at, ac, (const u16 *)data, n);
        if (compact) {
            compact->count = n;
            compact->indexRevision = gl.buffers[gl.elementArrayBuffer].revision;
            compact->indexOffset = (size_t)(data - gl.buffers[gl.elementArrayBuffer].data);
            compact->arrays[0] = *av; compact->arrays[1] = *at; compact->arrays[2] = *ac;
            compact->shadeModel = gl.shadeModel;
            compact->depthBias = gl.current.depthBias;
#ifdef C3DGL_PROFILE_GPU_CACHE
            gl.gpuCacheMisses++;
#endif
            source->gpuCache = compact;
            gl.gpuCacheBytes += compact->bytes;
            drawGpuCache(compact);
            PROF_PATH(PB_EL_FAST);
            return true;
        }
    }
    // A cache miss is validated and converted below exactly like the existing
    // fast path. Optional allocation failure simply uses the per-frame buffer.
    if (cacheable && makeGpuCacheRoom((size_t)n * GPU_VERTEX_SIZE)) {
        cache = calloc(1, sizeof(*cache));
        if (cache) {
            cache->bytes = (size_t)n * GPU_VERTEX_SIZE;
            cache->data = linearAlloc(cache->bytes);
            if (!cache->data) { free(cache); cache = NULL; }
        }
    }
    if (!cache && n > C3DGL_MAX_VERTICES - gl.vertexCount)
    {
        // Preserve the generic path's complete-triangle prefix without
        // decoding every remaining vertex just to drop it. In large worlds
        // that fallback used more CPU time than drawing the terrain itself.
        reserveVertices(n);    // Report exhaustion once, as the generic path does
        n = (C3DGL_MAX_VERTICES - gl.vertexCount) / 3 * 3;
        if (n == 0) return true;
    }

    const u16 *index = (const u16 *)data;
    int maxIndex = 0;
    for (int i = 0; i < n; i++) if (index[i] > maxIndex) maxIndex = index[i];
    if (!arrayInRange(av, maxIndex) || !arrayInRange(at, maxIndex) || !arrayInRange(ac, maxIndex)) {
        if (cache) { linearFree(cache->data); free(cache); }
        return false;
    }

    const u8 *vbase = bufferRange(av->buffer, av->pointer, 0, 0);
    const u8 *tbase = bufferRange(at->buffer, at->pointer, 0, 0);
    const u8 *cbase = bufferRange(ac->buffer, ac->pointer, 0, 0);
    if ((vbase == NULL) || (tbase == NULL) || (cbase == NULL)) {
        if (cache) { linearFree(cache->data); free(cache); }
        return false;
    }

    const int vstride = av->stride? av->stride : 3*(int)sizeof(s16);
    const int tstride = at->stride? at->stride : 2*(int)sizeof(s16);
    const int cstride = ac->stride? ac->stride : 4;
    const float depthBias = gl.current.depthBias;
    const bool flat = (gl.shadeModel == GL_FLAT);

    initByteColor();
    u8 *out = cache? cache->data : gl.vbo + (size_t)gl.vertexCount*GPU_VERTEX_SIZE;
    for (int i = 0; i < n; i += 3, out += 3*GPU_VERTEX_SIZE)
    {
        // GL_FLAT: the color of all three vertices comes from the provoking vertex,
        // the last one of the triangle (GL 1.1 section 2.13.8)
        const u8 *flatColor = flat? cbase + (size_t)index[i + 2]*cstride : NULL;
        for (int k = 0; k < 3; k++)
        {
            const int v = index[i + k];
            const u8 *pv = vbase + (size_t)v*vstride;
            const u8 *pt = tbase + (size_t)v*tstride;
            const u8 *pc = flat? flatColor : cbase + (size_t)v*cstride;
            GpuVertex *g = (GpuVertex *)(out + (size_t)k*GPU_VERTEX_SIZE);
            g->pos[0] = loadS16(pv);
            g->pos[1] = loadS16(pv + 2);
            g->pos[2] = loadS16(pv + 4);
            g->tex[0] = loadS16(pt);
            g->tex[1] = loadS16(pt + 2);
            g->tex[2] = 1.0f;               // Size 2 texcoords leave q at 1
            g->color[0] = byteColor[pc[0]];
            g->color[1] = byteColor[pc[1]];
            g->color[2] = byteColor[pc[2]];
            g->color[3] = byteColor[pc[3]];
            g->depthBias = depthBias;
        }
    }
    if (cache) {
        cache->count = n;
        cache->indexRevision = gl.buffers[gl.elementArrayBuffer].revision;
        cache->indexOffset = (size_t)(data - gl.buffers[gl.elementArrayBuffer].data);
        cache->arrays[0] = *av; cache->arrays[1] = *at; cache->arrays[2] = *ac;
        cache->shadeModel = gl.shadeModel;
        cache->depthBias = depthBias;
#ifdef C3DGL_PROFILE_GPU_CACHE
        gl.gpuCacheMisses++;
#endif
        source->gpuCache = cache;
        gl.gpuCacheBytes += cache->bytes;
        GSPGPU_FlushDataCache(cache->data, cache->bytes);
        drawGpuCache(cache);
    } else gl.vertexCount += n;
    PROF_PATH(PB_EL_FAST);
    return true;
}

// glDrawArrays(GL_TRIANGLES) for the layout the Tesselator uses (GUI, text, clouds, entities): GL_FLOAT position (3),
// optionally GL_FLOAT texcoord (2) and GL_UNSIGNED_BYTE color (4). Decoded straight into the GPU vertex buffer like
// the indexed path; without a texcoord or color array every vertex takes the current texcoord or color, as in the
// generic loop (filled rectangles come without texcoords). Anything else takes the generic per-vertex loop.
// Returns true if the call was submitted.
static bool arrayTriangleFastPath(GLenum mode, GLint first, GLsizei count)
{
    if (mode != GL_TRIANGLES) { fastPathReject("mode"); return false; }
    if (first < 0) return false;
    if (!fastPathStateOk()) return false;
    if (!arrayActive(ARRAY_VERTEX)) { fastPathReject("missing vertex array"); return false; }

    const bool hasTex = arrayActive(ARRAY_TEXCOORD0);
    const bool hasColor = arrayActive(ARRAY_COLOR);
    const ClientArray *av = &gl.arrays[ARRAY_VERTEX];
    const ClientArray *at = &gl.arrays[ARRAY_TEXCOORD0];
    const ClientArray *ac = &gl.arrays[ARRAY_COLOR];
    if ((av->type != GL_FLOAT) || (av->size != 3)) { fastPathReject("vertex array type"); return false; }
    if (hasTex && ((at->type != GL_FLOAT) || (at->size != 2))) { fastPathReject("texcoord array type"); return false; }
    if (hasColor && ((ac->type != GL_UNSIGNED_BYTE) || (ac->size != 4))) { fastPathReject("color array type"); return false; }

    int n = count - (count % 3);
    if (n <= 0) return true;
    if (n > C3DGL_MAX_VERTICES - gl.vertexCount)
    {
        // Same complete-triangle prefix as the generic path, without decoding what gets dropped
        reserveVertices(n);
        n = (C3DGL_MAX_VERTICES - gl.vertexCount) / 3 * 3;
        if (n == 0) return true;
    }

    const int last = first + n - 1;
    if (!arrayInRange(av, last) || (hasTex && !arrayInRange(at, last)) || (hasColor && !arrayInRange(ac, last))) return false;
    const u8 *vbase = bufferRange(av->buffer, av->pointer, 0, 0);
    const u8 *tbase = hasTex? bufferRange(at->buffer, at->pointer, 0, 0) : NULL;
    const u8 *cbase = hasColor? bufferRange(ac->buffer, ac->pointer, 0, 0) : NULL;
    if ((vbase == NULL) || (hasTex && (tbase == NULL)) || (hasColor && (cbase == NULL))) return false;

    const int vstride = av->stride? av->stride : 3*(int)sizeof(float);
    const int tstride = at->stride? at->stride : 2*(int)sizeof(float);
    const int cstride = ac->stride? ac->stride : 4;
    const bool flat = (gl.shadeModel == GL_FLAT);

    // The attributes no array provides, and the depth bias, as the generic loop takes them from the current values
    GpuVertex base;
    memcpy(&base, &gl.current, sizeof(base));
    if (flat) base.depthBias = 0.0f;    // Flat shading goes through shadeVertex, which sets the (disabled) polygon offset

    initByteColor();
    u8 *out = gl.vbo + (size_t)gl.vertexCount*GPU_VERTEX_SIZE;
    for (int i = 0; i < n; i += 3, out += 3*GPU_VERTEX_SIZE)
    {
        // GL_FLAT: the provoking vertex is the last one of the triangle
        const u8 *flatColor = (flat && hasColor)? cbase + (size_t)(first + i + 2)*cstride : NULL;
        for (int k = 0; k < 3; k++)
        {
            const int v = first + i + k;
            GpuVertex *g = (GpuVertex *)(out + (size_t)k*GPU_VERTEX_SIZE);
            *g = base;
            memcpy(g->pos, vbase + (size_t)v*vstride, 3*sizeof(float));
            if (hasTex)
            {
                memcpy(g->tex, tbase + (size_t)v*tstride, 2*sizeof(float));
                g->tex[2] = 1.0f;           // Size 2 texcoords leave q at 1
            }
            if (hasColor)
            {
                const u8 *pc = flat? flatColor : cbase + (size_t)v*cstride;
                g->color[0] = byteColor[pc[0]];
                g->color[1] = byteColor[pc[1]];
                g->color[2] = byteColor[pc[2]];
                g->color[3] = byteColor[pc[3]];
            }
        }
    }
    gl.vertexCount += n;
    PROF_PATH(PB_ARR_FAST);
    return true;
}

// The GL calls c3dglDrawMeshes() stands for, for one mesh
static void drawMeshGL(const C3DGLmesh *m, GLfloat scale)
{
    glPushMatrix();
    glTranslatef(m->x, m->y, m->z);
    glScalef(scale, scale, scale);
    glBindBuffer(GL_ARRAY_BUFFER, m->buffer);
    glVertexPointer(3, GL_SHORT, 16, (const GLvoid *)0);
    glTexCoordPointer(2, GL_SHORT, 16, (const GLvoid *)8);
    glColorPointer(4, GL_UNSIGNED_BYTE, 16, (const GLvoid *)12);
    glDrawElements(GL_TRIANGLES, m->count, GL_UNSIGNED_SHORT, (const GLvoid *)0);
    glPopMatrix();
}

// The array state glVertexPointer/glTexCoordPointer/glColorPointer leave for a mesh in `buffer` (what its cache key holds)
static void meshArrays(GLuint buffer, ClientArray out[3])
{
    static const GLint sizes[3] = { 3, 2, 4 };
    static const GLenum types[3] = { GL_SHORT, GL_SHORT, GL_UNSIGNED_BYTE };
    static const uintptr_t offsets[3] = { 0, 8, 12 };
    memset(out, 0, 3*sizeof(ClientArray));      // (cache keys are compared with memcmp, padding included)
    for (int i = 0; i < 3; i++)
    {
        out[i].enabled = true;
        out[i].pointer = (const void *)offsets[i];
        out[i].buffer = buffer;
        out[i].size = sizes[i];
        out[i].type = types[i];
        out[i].stride = 16;
    }
}

void c3dglDrawMeshes(const C3DGLmesh *meshes, int n, float scale)
{
    if (n <= 0) return;
    // Straight from the caches only where glDrawElements would take the cache too and nothing but the matrix changes
    // from mesh to mesh: no display list, modelview matrix mode, the fast path's state, the three arrays on, no stipple
    // (its pattern coordinates follow the matrix too)
    const Buffer *ib = (gl.elementArrayBuffer && gl.elementArrayBuffer < gl.bufferCount)? &gl.buffers[gl.elementArrayBuffer] : NULL;
    bool direct = gl.ready && matrixFastPath && !gl.listCompiling && (gl.renderMode == GL_RENDER) && (gl.matrixMode == 0) &&
                  (ib != NULL) && (ib->usage == GL_STATIC_DRAW) && !gl.polygonStipple &&
                  gl.arrays[ARRAY_VERTEX].enabled && gl.arrays[ARRAY_TEXCOORD0].enabled && gl.arrays[ARRAY_COLOR].enabled &&
                  fastPathStateOk();
    bool prepared = false, drewDirect = false;
    for (int i = 0; i < n; i++)
    {
        const C3DGLmesh *m = &meshes[i];
        GpuBufferCache *cache = NULL;
        if (direct && m->buffer && (m->buffer < gl.bufferCount))
        {
            cache = gl.buffers[m->buffer].gpuCache;
            ClientArray arrays[3];
            meshArrays(m->buffer, arrays);
            if ((cache == NULL) || !cache->compact || (cache->count != m->count) || (cache->indexRevision != ib->revision) ||
                (cache->indexOffset != 0) || (cache->shadeModel != gl.shadeModel) ||
                (memcmp(&cache->depthBias, &gl.current.depthBias, sizeof(float)) != 0) ||
                (memcmp(cache->arrays, arrays, sizeof(arrays)) != 0)) cache = NULL;
        }
        if (cache == NULL) { drawMeshGL(m, scale); continue; }

        PROF_ENTER();
        if (!prepared || gl.batch.matrixSerial != ~0u)
        {
            // The state of the meshes, once (and again after a mesh that went through GL); the matrix follows per mesh
            prepareDraw(false, false);
            prepared = true;
        }
        // The matrices exactly as glTranslatef, glScalef, projectionModelview() and applyMatrixState() compute them
        Mat4 mv = gl.stack[0][gl.stackDepth[0]], t;
        mat4Identity(&t);
        t.m[12] = m->x; t.m[13] = m->y; t.m[14] = m->z;
        mat4Mul(&mv, &mv, &t);
        mat4Identity(&t);
        t.m[0] = scale; t.m[5] = scale; t.m[10] = scale;
        mat4Mul(&mv, &mv, &t);
        Mat4 pmv, mvp;
        mat4Mul(&pmv, &gl.stack[1][gl.stackDepth[1]], &mv);
        mat4Mul(&mvp, &gl.post, &pmv);
        C3D_Mtx mtx;
        mat4ToC3D(&mvp, &mtx);
        C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, gl.uLocMvp, &mtx);
        // The uploaded matrix belongs to no matrix serial: the next draw sets its own
        gl.batch.matrixSerial = ~0u;
        PROF_LEAVE(PB_MESHES, 0);
        drawGpuCache(cache);
        drewDirect = true;
    }
    if (drewDirect)
    {
        // The array state the GL calls would have left
        const C3DGLmesh *last = &meshes[n - 1];
        glBindBuffer(GL_ARRAY_BUFFER, last->buffer);
        glVertexPointer(3, GL_SHORT, 16, (const GLvoid *)0);
        glTexCoordPointer(2, GL_SHORT, 16, (const GLvoid *)8);
        glColorPointer(4, GL_UNSIGNED_BYTE, 16, (const GLvoid *)12);
    }
}

#ifdef C3DGL_PROFILE
static void glDrawElementsBody(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
#else
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
#endif
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

    if (!compile && indexedTriangleFastPath(mode, type, data, count))
    {
        endPrimitive();
        return;
    }

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

#ifdef C3DGL_PROFILE
void glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
    PROF_ENTER();
    PROF_PATH(PB_EL_GENERIC);
    glDrawElementsBody(mode, count, type, indices);
    PROF_LEAVE(profPath, count);
}
#endif
