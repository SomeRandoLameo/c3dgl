// Client pixel images and pixel transfer
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
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
