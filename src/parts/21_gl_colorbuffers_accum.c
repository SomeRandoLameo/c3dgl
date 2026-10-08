// OpenGL: color buffers and accumulation buffer
// Part of c3dgl.c, which includes it in order. Not a standalone source: do not add it to the build.
//----------------------------------------------------------------------------------
// OpenGL: color buffers (GL). The framebuffer is double-buffered RGBA without stereo or aux buffers; drawing always goes
// to the frame being rendered, which c3dglSwapBuffers() presents, so the front buffer is treated like the back buffer
//----------------------------------------------------------------------------------
// Error of glDrawBuffer/glReadBuffer(mode): buffers that do not exist are GL_INVALID_OPERATION
static GLenum colorBufferError(GLenum mode, bool draw)
{
    switch (mode)
    {
        case GL_NONE: case GL_FRONT_AND_BACK: return draw? GL_NO_ERROR : GL_INVALID_ENUM;
        case GL_FRONT_LEFT: case GL_BACK_LEFT: case GL_FRONT: case GL_BACK: case GL_LEFT: return GL_NO_ERROR;
        case GL_FRONT_RIGHT: case GL_BACK_RIGHT: case GL_RIGHT:
        case GL_AUX0: case GL_AUX1: case GL_AUX2: case GL_AUX3: return GL_INVALID_OPERATION;
        default: return GL_INVALID_ENUM;
    }
}

void glDrawBuffer(GLenum mode)
{
    LIST_SAVE(DRAW_BUFFER, "u", mode);
    GLenum error = colorBufferError(mode, true);
    if (error != GL_NO_ERROR) { setError(error); return; }
    if ((mode != GL_NONE) && (mode != GL_BACK) && (mode != GL_BACK_LEFT))
        WARN_ONCE("glDrawBuffer: the front buffer is drawn like the back buffer (shown after c3dglSwapBuffers)\n");
    gl.drawBuffer = mode;
}

void glReadBuffer(GLenum mode)
{
    LIST_SAVE(READ_BUFFER, "u", mode);
    GLenum error = colorBufferError(mode, false);
    if (error != GL_NO_ERROR) { setError(error); return; }
    gl.readBuffer = mode;
}

//----------------------------------------------------------------------------------
// OpenGL: accumulation buffer (GL). One per screen in normal memory, allocated at its first use: signed 16 bits per
// component (GL_ACCUM_*_BITS), ACCUM_ONE = 1.0, values saturate at about +-1 (outside [-1, 1] GL leaves the result
// undefined). Window column by column like the framebuffer lines, see readLines(). All operations work on the CPU and
// only on the pixels in the scissor box; GL_ACCUM and GL_LOAD read the color buffer like glReadPixels (wait for the
// GPU), GL_RETURN draws the result as RGBA8 textures over the scissor box (queued like glDrawPixels, no wait)
//----------------------------------------------------------------------------------
#define ACCUM_ONE   32767

// Each word holds two components of a pixel, R | G << 16 and B | A << 16, processed together with the ARMv6 SIMD and
// DSP instructions (saturating halfword adds, 32x16-bit multiplies); [-32768, 32767], -32768 is about -1 too
static u32 *accumBuffer(void)
{
    u32 **acc = &gl.accum[curTargetIndex()];
    if (*acc == NULL)
    {
        *acc = calloc((size_t)screenWidth(gl.screen)*C3DGL_SCREEN_HEIGHT*2, sizeof(u32));
        if (*acc == NULL) { LOG("Out of memory for the accumulation buffer\n"); setError(GL_OUT_OF_MEMORY); }
    }
    return *acc;
}

static u32 *accumPixel(u32 *acc, int x, int y)
{
    return acc + ((size_t)x*C3DGL_SCREEN_HEIGHT + y)*2;
}

static s16 accumClamp(float v)
{
    return (s16)lrintf((v > ACCUM_ONE)? ACCUM_ONE : (v < -ACCUM_ONE)? -ACCUM_ONE : v);
}

static u32 accumPair(s32 low, s32 high)
{
    return (u16)low | ((u32)high << 16);
}

// The window rectangle [x0, x1) x [y0, y1) that accumulation buffer operations change: the scissor box if enabled
static bool accumRect(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = *y0 = 0;
    *x1 = screenWidth(gl.screen);
    *y1 = C3DGL_SCREEN_HEIGHT;
    if (gl.state.scissor)
    {
        const GLint *b = gl.state.scissorBox;
        if (b[0] > *x0) *x0 = b[0];
        if (b[1] > *y0) *y0 = b[1];
        if (b[0] + b[2] < *x1) *x1 = b[0] + b[2];
        if (b[1] + b[3] < *y1) *y1 = b[1] + b[3];
    }
    return (*x0 < *x1) && (*y0 < *y1);
}

// glClear(GL_ACCUM_BUFFER_BIT)
static void clearAccum(void)
{
    int x0, y0, x1, y1;
    if (!accumRect(&x0, &y0, &x1, &y1)) return;
    u32 *acc = accumBuffer();
    if (acc == NULL) return;
    const float *c = gl.clearAccum;
    u32 rg = accumPair(accumClamp(c[0]*ACCUM_ONE), accumClamp(c[1]*ACCUM_ONE));
    u32 ba = accumPair(accumClamp(c[2]*ACCUM_ONE), accumClamp(c[3]*ACCUM_ONE));
    for (int x = x0; x < x1; x++)
    {
        u32 *a = accumPixel(acc, x, y0);
        for (int y = y0; y < y1; y++, a += 2) { a[0] = rg; a[1] = ba; }
    }
}

// GL_ACCUM (load false) and GL_LOAD: accumulation buffer (+)= value*color buffer
static void accumColor(u32 *acc, int x0, int y0, int x1, int y1, float value, bool load)
{
    // value*c for every 8-bit component c in the low and the high halfword
    u32 low[256], high[256];
    for (int c = 0; c < 256; c++)
    {
        s16 v = accumClamp(value*ACCUM_ONE*c/255.0f);
        low[c] = (u16)v;
        high[c] = (u32)(u16)v << 16;
    }

    int line0 = x0 & ~7;
    u8 *fb = readFramebuffer(false, line0, ((x1 + 7) & ~7) - line0);
    if (fb == NULL) return;
    for (int x = x0; x < x1; x++)
    {
        // Framebuffer bytes A, B, G, R: the word A | B << 8 | G << 16 | R << 24
        const u32 *p = (const u32 *)(fb + (size_t)(x - line0)*READ_LINE_BYTES) + y0;
        u32 *a = accumPixel(acc, x, y0);
        if (load)
        {
            for (int y = y0; y < y1; y++, p++, a += 2)
            {
                a[0] = low[*p >> 24] | high[(*p >> 16) & 0xFF];
                a[1] = low[(*p >> 8) & 0xFF] | high[*p & 0xFF];
            }
        }
        else
        {
            for (int y = y0; y < y1; y++, p++, a += 2)
            {
                a[0] = (u32)__qadd16((int16x2_t)a[0], (int16x2_t)(low[*p >> 24] | high[(*p >> 16) & 0xFF]));
                a[1] = (u32)__qadd16((int16x2_t)a[1], (int16x2_t)(low[(*p >> 8) & 0xFF] | high[*p & 0xFF]));
            }
        }
    }
    linearFree(fb);
}

// The 8-bit color of the low or high value of word times m: f = m (the color per accumulation buffer unit) in 8.24 fixed
// point, the product in 1/256 is rounded and saturated
static u32 accumReturnComponent(s32 f, u32 word, bool high)
{
    s32 v = high? __smlawt(f, (s32)word, 128) : __smlawb(f, (s32)word, 128);
    return (u32)__usat(v >> 8, 8);
}

// GL_RETURN: value*accumulation buffer, clamped to [0, 1], into the color buffer through the scissor test and color mask
// only (GL 1.1 section 4.2.4): RGBA8 textures in tiles of up to PIXEL_TILE^2 pixels drawn like a pixel rectangle without
// any other fragment operation
static void accumReturn(u32 *acc, int x0, int y0, int x1, int y1, float value)
{
    if ((gl.drawBuffer == GL_NONE) || (gl.state.colorMask == 0)) return;

    // m in 8.24 fixed point fits 32 bits for |value| < 16; larger values (rare) take the float path
    float m = value*255.0f/ACCUM_ONE;
    bool fast = fabsf(value) < 16.0f;
    s32 f = fast? (s32)lrintf(m*16777216.0f) : 0;
    float sw = 2.0f/screenWidth(gl.screen), sh = 2.0f/C3DGL_SCREEN_HEIGHT;
    for (int ty = y0; ty < y1; ty += PIXEL_TILE)
    {
        for (int tx = x0; tx < x1; tx += PIXEL_TILE)
        {
            int tw = (x1 - tx < PIXEL_TILE)? x1 - tx : PIXEL_TILE, th = (y1 - ty < PIXEL_TILE)? y1 - ty : PIXEL_TILE;
            int texWidth = nextPow2(tw), texHeight = nextPow2(th);
            C3D_Tex *tex = pixelTexture(texWidth, texHeight, GPU_RGBA8, 4);
            if (tex == NULL) return;

            u32 yOffset[PIXEL_TILE];
            for (int y = 0; y < th; y++) yOffset[y] = tiledY(texWidth, texHeight, y, 4);
            for (int x = 0; x < tw; x++)
            {
                u8 *col = (u8 *)tex->data + tiledX(x, 4);
                const u32 *a = accumPixel(acc, tx + x, ty);
                for (int y = 0; y < th; y++, a += 2)
                {
                    u32 r, g, b, alpha;     // PICA RGBA8 is ABGR: the word A | B << 8 | G << 16 | R << 24
                    if (fast)
                    {
                        r = accumReturnComponent(f, a[0], false);
                        g = accumReturnComponent(f, a[0], true);
                        b = accumReturnComponent(f, a[1], false);
                        alpha = accumReturnComponent(f, a[1], true);
                    }
                    else
                    {
                        float c[4] = { (s16)a[0]*m, (s16)(a[0] >> 16)*m, (s16)a[1]*m, (s16)(a[1] >> 16)*m };
                        u32 out[4];
                        for (int i = 0; i < 4; i++) out[i] = (c[i] <= 0.0f)? 0 : (c[i] >= 255.0f)? 255 : (u32)(c[i] + 0.5f);
                        r = out[0]; g = out[1]; b = out[2]; alpha = out[3];
                    }
                    *(u32 *)(col + yOffset[y]) = alpha | (b << 8) | (g << 16) | (r << 24);
                }
            }

            DrawState key;
            memset(&key, 0, sizeof(key));
            key.clipSpace = true;
            key.viewport[2] = screenWidth(gl.screen);
            key.viewport[3] = C3DGL_SCREEN_HEIGHT;
            key.scissor = gl.state.scissor;
            memcpy(key.scissorBox, gl.state.scissorBox, sizeof(key.scissorBox));
            key.colorMask = gl.state.colorMask;
            key.depthFar = 1.0f;
            key.pixelMode = PIXEL_IMAGE;
            key.pixelTex = tex;
            useState(&key);

            float wx0 = tx*sw - 1.0f, wx1 = (tx + tw)*sw - 1.0f, wy0 = ty*sh - 1.0f, wy1 = (ty + th)*sh - 1.0f;
            float s1 = (float)tw/texWidth, t1 = (float)th/texHeight;
            Vertex q[4];
            memset(q, 0, sizeof(q));
            for (int i = 0; i < 4; i++)
            {
                bool right = (i == 1) || (i == 2), top = (i >= 2);
                q[i].pos[0] = right? wx1 : wx0;
                q[i].pos[1] = top? wy1 : wy0;
                q[i].tex[0] = right? s1 : 0.0f;
                q[i].tex[1] = top? t1 : 0.0f;
                q[i].tex[2] = 1.0f;
                memset(q[i].color, 255, sizeof(q[i].color));
            }
            emitTriangle(&q[0], &q[1], &q[2]);
            emitTriangle(&q[0], &q[2], &q[3]);
        }
    }
}

void glAccum(GLenum op, GLfloat value)
{
    LIST_SAVE(ACCUM, "uf", op, value);
    if (gl.inBegin) { setError(GL_INVALID_OPERATION); return; }
    if ((op != GL_ACCUM) && (op != GL_LOAD) && (op != GL_RETURN) && (op != GL_MULT) && (op != GL_ADD))
    {
        setError(GL_INVALID_ENUM);
        return;
    }
    if (gl.renderMode != GL_RENDER) return;     // Like Mesa: nothing changes in feedback and selection mode

    int x0, y0, x1, y1;
    if (!accumRect(&x0, &y0, &x1, &y1)) return;
    u32 *acc = accumBuffer();
    if (acc == NULL) return;

    switch (op)
    {
        case GL_ACCUM: case GL_LOAD: accumColor(acc, x0, y0, x1, y1, value, op == GL_LOAD); break;
        case GL_RETURN: accumReturn(acc, x0, y0, x1, y1, value); break;
        case GL_ADD:    // In two halves, so that +-2 saturates the whole range
        {
            s16 k = accumClamp(value*ACCUM_ONE/2.0f);
            int16x2_t kk = (int16x2_t)accumPair(k, k);
            for (int x = x0; x < x1; x++)
            {
                u32 *a = accumPixel(acc, x, y0);
                for (int i = 0; i < (y1 - y0)*2; i++) a[i] = (u32)__qadd16(__qadd16((int16x2_t)a[i], kk), kk);
            }
            break;
        }
        case GL_MULT:   // value in 16.16 fixed point (beyond +-32767 every nonzero value saturates anyway)
        {
            s32 f = (s32)lrintf(((value > 32767.0f)? 32767.0f : (value < -32767.0f)? -32767.0f : value)*65536.0f);
            for (int x = x0; x < x1; x++)
            {
                u32 *a = accumPixel(acc, x, y0);
                for (int i = 0; i < (y1 - y0)*2; i++)
                    a[i] = accumPair(__ssat(__smlawb(f, (s32)a[i], 0), 16), __ssat(__smlawt(f, (s32)a[i], 0), 16));
            }
            break;
        }
    }
}

void glClearAccum(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    LIST_SAVE(CLEAR_ACCUM, "ffff", red, green, blue, alpha);
    const float c[4] = { red, green, blue, alpha };
    for (int i = 0; i < 4; i++) gl.clearAccum[i] = (c[i] < -1.0f)? -1.0f : (c[i] > 1.0f)? 1.0f : c[i];
}
