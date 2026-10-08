// OpenGL: textures
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
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

// The GPU may still read the storage of t: a draw of the current frame used it (submitted only when the frame ends), or
// one of the previous frame, which renders until the next frame begins
static bool textureBusy(const Texture *t) { return (t->drawnFrame != 0) && (t->drawnFrame == gl.frameSerial); }

// Storage of t no longer used: deleted once the GPU is done with it
static void releaseTexture(Texture *t)
{
    if (textureBusy(t)) deferTextureDelete(&t->tex);
    else C3D_TexDelete(&t->tex);
    t->drawnFrame = 0;
}

// Texels of t about to change in place (after textureModified()): draws issued before keep the old ones. Between frames
// the next frame is begun, which waits for the GPU; within a frame the texels move to a copy and the old storage is
// deleted once the frame is done
static void copyOnWrite(Texture *t)
{
    if (!textureBusy(t)) return;
    if (!gl.frameActive) { ensureFrame(); return; }
    size_t size = C3D_TexCalcTotalSize(t->tex.size, t->levels - 1);
    void *data = cacheAwareLinearAlloc(size);
    if (data == NULL) { WARN_ONCE("Out of memory for a texture copy, earlier draws of the frame get the new texels\n"); return; }
    memcpy(data, t->tex.data, size);
    releaseTexture(t);
    t->tex.data = data;
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
    if (!C3D_TexInitMipmap(&mip, t->tex.width, oneD? t->tex.width : t->tex.height, t->format.format) &&
        !(reclaimGpuCaches() && C3D_TexInitMipmap(&mip, t->tex.width, oneD? t->tex.width : t->tex.height, t->format.format)))
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

    releaseTexture(t);
    t->tex = mip;
    t->levels = mip.maxLevel + 1;
    replicateRows(t, 0, 0, t->tex.width);
    return true;
}

// Image column (row) whose texels go to padding column (row) x of a level `size` texels wide (high), stored `stored` wide
// (high), for the wrap mode: the sampler wraps or clamps at the stored size, so the padding holds what GL samples past
// the image: the edge (clamp), the image again after its end and before the stored end (repeat; filtering across both
// seams sees the right neighbors), the image mirrored (mirrored repeat)
static int paddingSource(int x, int size, int stored, GLenum wrap)
{
    int pad = x - size, back = stored - x;      // Distance to the image end, to the stored end
    switch (wrap)
    {
        case GL_REPEAT: return (pad < back)? pad % size : size - 1 - (back - 1) % size;
        case GL_MIRRORED_REPEAT: return size - 1 - pad % size;
        default: return size - 1;
    }
}

// Fill the padding of stored `level` around a non-power-of-two image, see paddingSource(); the padded columns first,
// then the padded rows over the full stored width. 1D textures only have padded columns (every row holds the image).
// ETC1 textures keep zeros there
static void fillPadding(Texture *t, int level)
{
    const TexLevel *lv = &t->level[level];
    int bpp = t->format.bpp, texWidth, texHeight;
    if (!lv->defined || (bpp == 0)) return;
    u8 *data = levelData(t, level, &texWidth, &texHeight);
    bool oneD = (t->target == GL_TEXTURE_1D);
    int w = (lv->width < texWidth)? lv->width : texWidth, h = oneD? texHeight : (lv->height < texHeight)? lv->height : texHeight;
    if ((w <= 0) || (h <= 0)) return;

    for (int x = w; x < texWidth; x++)
    {
        int src = paddingSource(x, w, texWidth, t->wrapS);
        for (int y = 0; y < h; y++)
            memcpy(data + tiledOffset(texWidth, texHeight, x, y, bpp), data + tiledOffset(texWidth, texHeight, src, y, bpp), bpp);
    }
    for (int y = h; y < texHeight; y++)
    {
        int src = paddingSource(y, h, texHeight, t->wrapT);
        for (int x = 0; x < texWidth; x++)
            memcpy(data + tiledOffset(texWidth, texHeight, x, y, bpp), data + tiledOffset(texWidth, texHeight, x, src, bpp), bpp);
    }
}

// After the texels or the wrap modes changed: the padding of every stored level, then the CPU cache
static void flushTexture(Texture *t)
{
    for (int level = 0; level < t->levels; level++) fillPadding(t, level);
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
            releaseTexture(t);
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
            if (t->loaded)
            {
                textureModified(textureSlotOf(t));
                copyOnWrite(t);
                flushTexture(t);        // The padding depends on the wrap modes
            }
            break;
        case GL_GENERATE_MIPMAP:
            t->generateMipmap = (param != 0);
            if (t->generateMipmap && t->loaded)
            {
                textureModified(textureSlotOf(t));
                copyOnWrite(t);
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
        if (*stored) copyOnWrite(t);
        return t;
    }

    // Level 0: keep the storage (and the other levels) if only the content changes
    int texWidth = nextPow2(imageWidth), texHeight = nextPow2(imageHeight);
    bool same = t->loaded && (imageWidth == t->width) && (imageHeight == t->height) && (f->format == t->format.format);
    if (!same)
    {
        if (t->loaded)
        {
            releaseTexture(t);
            t->loaded = false;
        }
        if (!C3D_TexInit(&t->tex, texWidth, texHeight, f->format) &&
            !(reclaimGpuCaches() && C3D_TexInit(&t->tex, texWidth, texHeight, f->format)))
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
    else copyOnWrite(t);
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

#ifdef C3DGL_PROFILE
static void glTexImage2DBody(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                          GLint border, GLenum format, GLenum type, const GLvoid *pixels)
#else
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels)
#endif
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

#ifdef C3DGL_PROFILE
void glTexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height,GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    PROF_ENTER();
    glTexImage2DBody(target, level, internalformat, width, height, border, format, type, pixels);
    PROF_LEAVE(PB_TEXTURE, width*height);
}
#endif

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
    copyOnWrite(t);
    if (level < t->levels) loadTexels(t, level, xoffset, yoffset, width, height, format, type, pixels, &gl.unpack, lv->base);
    if ((level == 0) && t->generateMipmap) generateMipmaps(t);
    flushTexture(t);
}

#ifdef C3DGL_PROFILE
static void glTexSubImage2DBody(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                             GLenum format, GLenum type, const GLvoid *pixels)
#else
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, const GLvoid *pixels)
#endif
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

#ifdef C3DGL_PROFILE
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,   GLenum format, GLenum type, const GLvoid *pixels)
{
    PROF_ENTER();
    glTexSubImage2DBody(target, level, xoffset, yoffset, width, height, format, type, pixels);
    PROF_LEAVE(PB_TEXTURE, width*height);
}
#endif

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
