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
    if (fastPathOff == NULL) { fastPathOff = why; LOG("Indexed fast path unused: %s\n", why); }
}

static void drawGpuCache(GpuBufferCache *cache)
{
    flush();  // Preserve submission order with ordinary immediate/array geometry.
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
}

// What both direct-decode paths (indexed and array triangles) need from the current state: no lighting,
// texgen, clip planes, polygon modes, second/third texture unit or per-vertex normal/edge/point size, and
// vertex, texcoord 0 and color arrays.
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
    if (!arrayActive(ARRAY_VERTEX) || !arrayActive(ARRAY_TEXCOORD0) || !arrayActive(ARRAY_COLOR)) { fastPathReject("missing array"); return false; }
    if (sizeof(GpuVertex) != GPU_VERTEX_SIZE) { fastPathReject("vertex layout"); return false; }
    return true;
}

// Returns true if the call was submitted
static bool indexedTriangleFastPath(GLenum mode, GLenum type, const u8 *data, int count)
{
    if ((mode != GL_TRIANGLES) || (type != GL_UNSIGNED_SHORT)) { fastPathReject("mode/type"); return false; }
    if (!fastPathStateOk()) return false;

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
                return true;
            }
            invalidateGpuCache(source);
            cache = NULL;
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
    return true;
}

// glDrawArrays(GL_TRIANGLES) for the layout the Tesselator uses (GUI, text, clouds, entities): GL_FLOAT position (3)
// and texcoord (2), GL_UNSIGNED_BYTE color (4). Decoded straight into the GPU vertex buffer like the indexed path;
// anything else takes the generic per-vertex loop. Returns true if the call was submitted.
static bool arrayTriangleFastPath(GLenum mode, GLint first, GLsizei count)
{
    if (mode != GL_TRIANGLES) { fastPathReject("mode"); return false; }
    if (first < 0) return false;
    if (!fastPathStateOk()) return false;

    const ClientArray *av = &gl.arrays[ARRAY_VERTEX];
    const ClientArray *at = &gl.arrays[ARRAY_TEXCOORD0];
    const ClientArray *ac = &gl.arrays[ARRAY_COLOR];
    if ((av->type != GL_FLOAT) || (av->size != 3)) { fastPathReject("vertex array type"); return false; }
    if ((at->type != GL_FLOAT) || (at->size != 2)) { fastPathReject("texcoord array type"); return false; }
    if ((ac->type != GL_UNSIGNED_BYTE) || (ac->size != 4)) { fastPathReject("color array type"); return false; }

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
    if (!arrayInRange(av, last) || !arrayInRange(at, last) || !arrayInRange(ac, last)) return false;
    const u8 *vbase = bufferRange(av->buffer, av->pointer, 0, 0);
    const u8 *tbase = bufferRange(at->buffer, at->pointer, 0, 0);
    const u8 *cbase = bufferRange(ac->buffer, ac->pointer, 0, 0);
    if ((vbase == NULL) || (tbase == NULL) || (cbase == NULL)) return false;

    const int vstride = av->stride? av->stride : 3*(int)sizeof(float);
    const int tstride = at->stride? at->stride : 2*(int)sizeof(float);
    const int cstride = ac->stride? ac->stride : 4;
    const float depthBias = gl.current.depthBias;
    const bool flat = (gl.shadeModel == GL_FLAT);

    initByteColor();
    u8 *out = gl.vbo + (size_t)gl.vertexCount*GPU_VERTEX_SIZE;
    for (int i = 0; i < n; i += 3, out += 3*GPU_VERTEX_SIZE)
    {
        // GL_FLAT: the provoking vertex is the last one of the triangle
        const u8 *flatColor = flat? cbase + (size_t)(first + i + 2)*cstride : NULL;
        for (int k = 0; k < 3; k++)
        {
            const int v = first + i + k;
            const u8 *pc = flat? flatColor : cbase + (size_t)v*cstride;
            GpuVertex *g = (GpuVertex *)(out + (size_t)k*GPU_VERTEX_SIZE);
            memcpy(g->pos, vbase + (size_t)v*vstride, 3*sizeof(float));
            memcpy(g->tex, tbase + (size_t)v*tstride, 2*sizeof(float));
            g->tex[2] = 1.0f;               // Size 2 texcoords leave q at 1
            g->color[0] = byteColor[pc[0]];
            g->color[1] = byteColor[pc[1]];
            g->color[2] = byteColor[pc[2]];
            g->color[3] = byteColor[pc[3]];
            g->depthBias = depthBias;
        }
    }
    gl.vertexCount += n;
    return true;
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
