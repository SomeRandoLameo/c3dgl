// OpenGL: raster position, glDrawPixels, glBitmap, glCopyPixels
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
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

// The polygon stipple pattern as a 32x32 A8 texture (repeated, sampled in projection mode, see applyState()), made at
// the first stippled draw of a frame or after a pattern change
static const C3D_Tex *stippleTexture(void)
{
    if (gl.stippleTex != NULL) return gl.stippleTex;
    C3D_Tex *tex = pixelTexture(32, 32, GPU_A8, 1);
    if (tex == NULL) return NULL;
    tex->param = GPU_TEXTURE_MAG_FILTER(GPU_NEAREST) | GPU_TEXTURE_MIN_FILTER(GPU_NEAREST) |
                 GPU_TEXTURE_WRAP_S(GPU_REPEAT) | GPU_TEXTURE_WRAP_T(GPU_REPEAT) | GPU_TEXTURE_MODE(GPU_TEX_PROJECTION);
    u8 *data = (u8 *)tex->data;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++)
            data[tiledOffset(32, 32, x, y, 1)] = ((gl.polygonStipplePattern[4*y + x/8] << (x & 7)) & 0x80)? 255 : 0;
    gl.stippleTex = tex;
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
    u8 *in = (u8 *)curTarget()->frameBuf.colorBuf + (size_t)line0*READ_LINE_BYTES;
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
    bool used[C3DGL_TARGET_COUNT], suspended = suspendFrame(used);
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
