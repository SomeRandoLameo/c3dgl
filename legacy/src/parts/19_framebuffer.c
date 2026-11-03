// OpenGL: reading and copying the framebuffer
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: reading the framebuffer
//----------------------------------------------------------------------------------
#define READ_LINE_BYTES     (C3DGL_SCREEN_HEIGHT*4)

// citro3d only starts the GX queue in C3D_FrameEnd, so to get at the framebuffer a frame in progress is ended without
// presenting it (no target marked as used), which runs the draws so far, and begun again afterwards
static bool suspendFrame(bool used[C3DGL_TARGET_COUNT])
{
    if (!gl.frameActive) return false;
    flushVertexCache();
    for (int i = 0; i < C3DGL_TARGET_COUNT; i++) { used[i] = (gl.targets[i] != NULL) && gl.targets[i]->used; if (gl.targets[i] != NULL) gl.targets[i]->used = false; }
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    return true;
}

static void resumeFrame(bool suspended, const bool used[C3DGL_TARGET_COUNT])
{
    if (!suspended) return;
    C3D_FrameBegin(0);          // Waits for the GPU
    gl.frameSerial++;
    C3D_FrameDrawOn(curTarget());
    for (int i = 0; i < C3DGL_TARGET_COUNT; i++) if (gl.targets[i] != NULL) gl.targets[i]->used = used[i];
    gl.batchValid = false;
}

// Copy framebuffer lines [line0, line0 + lines) of the current screen into linear memory (free with linearFree), both
// multiples of 8 (a line of tiles). Line x is window column x with 240 pixels from y = 0 to the top, 4 bytes each:
// color A, B, G, R; depth/stencil the D24S8 word (stored depth = 1 - window depth, stencil in the top byte).
// No frame may be in progress (see suspendFrame()); returns once the GPU and the transfer are done
static u8 *readLines(bool depthStencil, int line0, int lines)
{
    size_t size = (size_t)lines*READ_LINE_BYTES;
    u8 *out = cacheAwareLinearAlloc(size);
    if (out == NULL) { LOG("Out of memory for reading pixels\n"); setError(GL_OUT_OF_MEMORY); return NULL; }
    GSPGPU_FlushDataCache(out, size);       // No dirty cache lines may be written back over the transfer

    const C3D_FrameBuf *fb = &curTarget()->frameBuf;
    u8 *in = (u8 *)(depthStencil? fb->depthBuf : fb->colorBuf) + (size_t)line0*READ_LINE_BYTES;
    u32 dim = GX_BUFFER_DIM(C3DGL_SCREEN_HEIGHT, lines);
    C3D_SyncDisplayTransfer((u32 *)in, dim, (u32 *)out, dim, DISPLAY_TRANSFER_FLAGS | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8));
    GSPGPU_InvalidateDataCache(out, size);
    return out;
}

static u8 *readFramebuffer(bool depthStencil, int line0, int lines)
{
    bool used[C3DGL_TARGET_COUNT], suspended = suspendFrame(used);
    u8 *out = readLines(depthStencil, line0, lines);
    resumeFrame(suspended, used);
    return out;
}

// The reverse of readLines() for the depth/stencil buffer (a display transfer from linear to tiled), no frame in progress
static void writeDepthStencilLines(u8 *lineData, int line0, int lines)
{
    GSPGPU_FlushDataCache(lineData, (size_t)lines*READ_LINE_BYTES);
    u8 *out = (u8 *)curTarget()->frameBuf.depthBuf + (size_t)line0*READ_LINE_BYTES;
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
