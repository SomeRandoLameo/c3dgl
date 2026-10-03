// c3dgl example: self-check of the non-visual API (glGet*, glGetError, glIsEnabled, matrix loads, entry point
// variants). Failed checks are listed on the bottom screen; the top screen is GREEN when everything passed,
// RED otherwise (display lists included). On top of that, four white squares are drawn with four different vertex calls
// (glRectf, glRecti, glVertex2sv, glVertex4f with w = 2) and must look identical.
#include <3ds.h>
#include <GL/gl.h>
#include <GLES/gl.h>                     // Must coexist with <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

static int checks, failures;

#define CHECK(cond) check((cond), #cond, __LINE__)

static void check(bool ok, const char *what, int line)
{
    checks++;
    if (ok) return;
    failures++;
    printf("FAIL %i: %s\n", line, what);
}

static bool near(double a, double b) { return fabs(a - b) < 1e-4; }

static void testErrors(void)
{
    CHECK(glGetError() == GL_NO_ERROR);

    glEnable(0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM);
    CHECK(glGetError() == GL_NO_ERROR);         // Reading resets it

    // Only the first error is kept
    glMatrixMode(0x1234);
    glViewport(0, 0, -1, -1);
    CHECK(glGetError() == GL_INVALID_ENUM);
    CHECK(glGetError() == GL_NO_ERROR);

    glViewport(0, 0, -1, -1);
    CHECK(glGetError() == GL_INVALID_VALUE);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
    CHECK(glGetError() == GL_INVALID_VALUE);

    glPopMatrix();
    CHECK(glGetError() == GL_STACK_UNDERFLOW);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    unsigned char pixel[4] = {0};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_FLOAT, pixel);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(glIsTexture(tex));
    glDeleteTextures(1, &tex);
    CHECK(!glIsTexture(tex));
    CHECK(!glIsTexture(0));
}

static void testCapabilities(void)
{
    CHECK(glIsEnabled(GL_DITHER));              // OpenGL default
    glDisable(GL_DITHER);
    CHECK(!glIsEnabled(GL_DITHER));
    glDisable(GL_LIGHTING);                     // Accepted silently
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(!glIsEnabled(GL_LIGHTING));

    CHECK(!glIsEnabled(GL_BLEND));
    glEnable(GL_BLEND);
    CHECK(glIsEnabled(GL_BLEND));
    GLboolean b = GL_FALSE;
    glGetBooleanv(GL_BLEND, &b);
    CHECK(b == GL_TRUE);
    glDisable(GL_BLEND);

    glEnableClientState(GL_VERTEX_ARRAY);
    CHECK(glIsEnabled(GL_VERTEX_ARRAY));
    glDisableClientState(GL_VERTEX_ARRAY);
    CHECK(!glIsEnabled(GL_VERTEX_ARRAY));
}

static void testQueries(void)
{
    GLint v[4];
    glGetIntegerv(GL_VIEWPORT, v);
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 400 && v[3] == 240);
    glViewport(10, 20, 30, 40);
    glGetIntegerv(GL_VIEWPORT, v);
    CHECK(v[0] == 10 && v[1] == 20 && v[2] == 30 && v[3] == 40);
    glViewport(0, 0, 400, 240);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, v);
    CHECK(v[0] == 1024);
    glGetIntegerv(GL_DEPTH_BITS, v);
    CHECK(v[0] == 24);
    glGetIntegerv(GL_STENCIL_BITS, v);
    CHECK(v[0] == 8);

    glDepthFunc(GL_LEQUAL);
    glGetIntegerv(GL_DEPTH_FUNC, v);
    CHECK(v[0] == GL_LEQUAL);
    glDepthFunc(GL_LESS);

    glStencilFunc(GL_EQUAL, 5, 0x0F);
    glGetIntegerv(GL_STENCIL_REF, &v[0]);
    glGetIntegerv(GL_STENCIL_VALUE_MASK, &v[1]);
    glGetIntegerv(GL_STENCIL_FUNC, &v[2]);
    CHECK(v[0] == 5 && v[1] == 0x0F && v[2] == GL_EQUAL);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);

    glMatrixMode(GL_PROJECTION);
    glGetIntegerv(GL_MATRIX_MODE, v);
    CHECK(v[0] == GL_PROJECTION);
    glMatrixMode(GL_MODELVIEW);

    GLfloat f[4];
    glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, f);
    CHECK(fabsf(f[0] - 0.25f) < 0.01f && fabsf(f[1] - 0.5f) < 0.01f && fabsf(f[2] - 0.75f) < 0.01f && f[3] == 1.0f);

    // Colors: floats as set (8 bits), integers scaled to [0, INT_MAX]
    glColor3ub(255, 0, 0);
    glGetFloatv(GL_CURRENT_COLOR, f);
    CHECK(f[0] == 1.0f && f[1] == 0.0f && f[2] == 0.0f && f[3] == 1.0f);
    glGetIntegerv(GL_CURRENT_COLOR, v);
    CHECK(v[0] == 2147483647 && v[1] == 0);
    const GLfloat green[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    glColor4fv(green);
    glGetFloatv(GL_CURRENT_COLOR, f);
    CHECK(f[0] == 0.0f && f[1] == 1.0f && f[3] == 0.0f);
    glColor3f(1.0f, 1.0f, 1.0f);

    glTexCoord2i(3, 4);
    glGetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(f[0] == 3.0f && f[1] == 4.0f);

    GLboolean mask[4];
    glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    CHECK(mask[0] && !mask[1] && mask[2] && !mask[3]);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    glGetIntegerv(0x1234, v);
    CHECK(glGetError() == GL_INVALID_ENUM);
}

static void testMatrices(void)
{
    GLfloat m[16], out[16];
    for (int i = 0; i < 16; i++) m[i] = (GLfloat)(i + 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(m);
    glGetFloatv(GL_MODELVIEW_MATRIX, out);
    bool same = true;
    for (int i = 0; i < 16; i++) same = same && (out[i] == m[i]);
    CHECK(same);

    GLdouble d[16];
    glLoadIdentity();
    glTranslated(1.0, 2.0, 3.0);
    glGetDoublev(GL_MODELVIEW_MATRIX, d);
    CHECK(near(d[12], 1.0) && near(d[13], 2.0) && near(d[14], 3.0) && near(d[15], 1.0));

    glLoadIdentity();
    glRotated(90.0, 0.0, 0.0, 1.0);             // x axis -> y axis
    glGetDoublev(GL_MODELVIEW_MATRIX, d);
    CHECK(near(d[0], 0.0) && near(d[1], 1.0));

    for (int i = 0; i < 16; i++) d[i] = (i % 5 == 0) ? 2.0 : 0.0;
    glLoadMatrixd(d);
    glScaled(0.5, 0.5, 0.5);
    glGetFloatv(GL_MODELVIEW_MATRIX, out);
    CHECK(out[0] == 1.0f && out[5] == 1.0f && out[10] == 1.0f && out[15] == 2.0f);

    GLint depth;
    glLoadIdentity();
    glPushMatrix();
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &depth);
    CHECK(depth == 2);
    glPopMatrix();
    glGetIntegerv(GL_MODELVIEW_STACK_DEPTH, &depth);
    CHECK(depth == 1);
}

static void testArraysAndEs(void)
{
    // Array state and validation
    static const GLshort shorts[8] = {0};
    GLint v = 0;
    glVertexPointer(2, GL_SHORT, 4, shorts);
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &v);
    CHECK(v == 2);
    glGetIntegerv(GL_VERTEX_ARRAY_TYPE, &v);
    CHECK(v == GL_SHORT);
    glGetIntegerv(GL_VERTEX_ARRAY_STRIDE, &v);
    CHECK(v == 4);
    GLvoid *ptr = NULL;
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &ptr);
    CHECK(ptr == shorts);
    glGetIntegerv(GL_COLOR_ARRAY_SIZE, &v);
    CHECK(v == 4);                              // Default
    glVertexPointer(1, GL_FLOAT, 0, shorts);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glVertexPointer(2, GL_UNSIGNED_BYTE, 0, shorts);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glColorPointer(2, GL_FLOAT, 0, shorts);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glNormalPointer(GL_UNSIGNED_BYTE, 0, shorts);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, shorts);
    CHECK(glGetError() == GL_INVALID_ENUM);

    // glInterleavedArrays sets up and enables the arrays of the format
    static const GLfloat interleaved[12] = {0};
    glInterleavedArrays(GL_T2F_N3F_V3F, 0, interleaved);
    glGetIntegerv(GL_TEXTURE_COORD_ARRAY_STRIDE, &v);
    CHECK(v == 32);
    glGetPointerv(GL_NORMAL_ARRAY_POINTER, &ptr);
    CHECK(ptr == (const GLvoid *)&interleaved[2]);
    CHECK(glIsEnabled(GL_NORMAL_ARRAY) && glIsEnabled(GL_TEXTURE_COORD_ARRAY) && !glIsEnabled(GL_COLOR_ARRAY));
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    // Integer colors and normals are normalized, texcoords keep r and q
    GLfloat f[4];
    glColor3b(127, 63, -128);                   // (2c + 1)/255: 1.0, 0.5, -1.0 (clamped to 0)
    glGetFloatv(GL_CURRENT_COLOR, f);
    CHECK(f[0] == 1.0f && fabsf(f[1] - 0.5f) < 0.01f && f[2] == 0.0f && f[3] == 1.0f);
    glColor4us(65535, 0, 65535, 32768);
    glGetFloatv(GL_CURRENT_COLOR, f);
    CHECK(f[0] == 1.0f && f[1] == 0.0f && fabsf(f[3] - 0.5f) < 0.01f);
    glNormal3s(32767, -32768, 0);
    glGetFloatv(GL_CURRENT_NORMAL, f);
    CHECK(near(f[0], 1.0) && near(f[1], -1.0) && fabsf(f[2]) < 1e-4f);
    glTexCoord4f(1, 2, 3, 4);
    glGetFloatv(GL_CURRENT_TEXTURE_COORDS, f);
    CHECK(f[0] == 1 && f[1] == 2 && f[2] == 3 && f[3] == 4);
    glTexCoord2f(0, 0);
    glColor3ub(255, 255, 255);

    // ES fixed point: 1.5 = 0x18000
    glLineWidthx(0x18000);
    GLfixed x = 0;
    glGetFixedv(GL_LINE_WIDTH, &x);
    CHECK(x == 0x18000);
    glLineWidth(1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatex(0x10000, 0x20000, -0x8000);
    GLfloat m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    CHECK(m[12] == 1.0f && m[13] == 2.0f && m[14] == -0.5f);
    glLoadIdentity();
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);    // Enums are not scaled
    CHECK(glGetError() == GL_NO_ERROR);
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

static void testBuffers(void)
{
    GLuint ids[2] = {0, 0};
    glGenBuffers(2, ids);
    CHECK(ids[0] != 0 && ids[1] != 0 && ids[0] != ids[1]);
    CHECK(glIsBuffer(ids[0]) && !glIsBuffer(0));

    // Data, sub data, parameters
    const GLubyte bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    glBindBuffer(GL_ARRAY_BUFFER, ids[0]);
    glBufferData(GL_ARRAY_BUFFER, 8, bytes, GL_DYNAMIC_DRAW);
    GLint v = 0;
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &v);
    CHECK(v == 8);
    glGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_USAGE, &v);
    CHECK(v == GL_DYNAMIC_DRAW);
    glBufferSubData(GL_ARRAY_BUFFER, 6, 4, bytes);
    CHECK(glGetError() == GL_INVALID_VALUE);    // Beyond the end
    glBufferData(GL_ARRAY_BUFFER, 8, bytes, 0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v);
    CHECK(v == (GLint)ids[0]);

    // Arrays remember the buffer bound when their pointer was set; the pointer is an offset
    glVertexPointer(2, GL_SHORT, 0, (const GLvoid *)4);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &v);
    CHECK(v == (GLint)ids[0]);
    GLvoid *ptr = NULL;
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &ptr);
    CHECK(ptr == (GLvoid *)4);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &v);
    CHECK(v == (GLint)ids[0]);                  // Unchanged by rebinding

    // Deleting unbinds everywhere
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ids[0]);
    glDeleteBuffers(2, ids);
    CHECK(!glIsBuffer(ids[0]));
    glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &v);
    CHECK(v == 0);
    glGetIntegerv(GL_VERTEX_ARRAY_BUFFER_BINDING, &v);
    CHECK(v == 0);

    // Errors without a bound buffer, wrong target
    glBufferData(GL_ARRAY_BUFFER, 4, bytes, GL_STATIC_DRAW);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glBindBuffer(0x1234, 1);
    CHECK(glGetError() == GL_INVALID_ENUM);
}

static void testAttribStacks(void)
{
    GLint v = 0;
    GLfloat f[4];

    // ENABLE | COLOR_BUFFER: enables, blend func, clear color come back; depth func (DEPTH_BUFFER) does not
    glDisable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ZERO);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glDepthFunc(GL_LESS);
    glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT);
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &v);
    CHECK(v == 1);
    glEnable(GL_BLEND);
    glEnable(GL_LINE_SMOOTH);                   // Stored-only capability, also restored
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glDepthFunc(GL_GREATER);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_BLEND) && !glIsEnabled(GL_LINE_SMOOTH));
    glGetIntegerv(GL_BLEND_SRC, &v);
    CHECK(v == GL_ONE);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, f);
    CHECK(f[0] == 0.0f);
    glGetIntegerv(GL_DEPTH_FUNC, &v);
    CHECK(v == GL_GREATER);                     // Not in the pushed groups
    glDepthFunc(GL_LESS);

    // Nested: CURRENT inside VIEWPORT
    glColor3f(1, 1, 1);
    glViewport(0, 0, 400, 240);
    glPushAttrib(GL_VIEWPORT_BIT);
    glViewport(1, 2, 3, 4);
    glPushAttrib(GL_CURRENT_BIT);
    glColor3f(0, 1, 0);
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &v);
    CHECK(v == 2);
    glPopAttrib();
    glGetFloatv(GL_CURRENT_COLOR, f);
    CHECK(f[0] == 1.0f && f[1] == 1.0f);
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    CHECK(vp[0] == 1 && vp[3] == 4);            // Still the inner value
    glPopAttrib();
    glGetIntegerv(GL_VIEWPORT, vp);
    CHECK(vp[0] == 0 && vp[2] == 400);

    // TEXTURE: binding, environment and the bound texture's parameters
    GLuint tex[2];
    glGenTextures(2, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glPushAttrib(GL_TEXTURE_BIT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glActiveTexture(GL_TEXTURE1);
    glPopAttrib();
    glGetIntegerv(GL_ACTIVE_TEXTURE, &v);
    CHECK(v == GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &v);
    CHECK(v == (GLint)tex[0]);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &v);
    CHECK(v == GL_NEAREST);
    glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, &v);
    CHECK(v == GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(2, tex);

    // POLYGON, STENCIL, SCISSOR
    glPushAttrib(GL_POLYGON_BIT | GL_STENCIL_BUFFER_BIT | GL_SCISSOR_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glCullFace(GL_FRONT);
    glStencilFunc(GL_EQUAL, 7, 0xFF);
    glEnable(GL_SCISSOR_TEST);
    glScissor(5, 5, 5, 5);
    glPopAttrib();
    GLint modes[2];
    glGetIntegerv(GL_POLYGON_MODE, modes);
    CHECK(modes[0] == GL_FILL && modes[1] == GL_FILL);
    glGetIntegerv(GL_CULL_FACE_MODE, &v);
    CHECK(v == GL_BACK);
    glGetIntegerv(GL_STENCIL_REF, &v);
    CHECK(v == 0);
    CHECK(!glIsEnabled(GL_SCISSOR_TEST));

    // Errors
    glPopAttrib();
    CHECK(glGetError() == GL_STACK_UNDERFLOW);
    GLint max = 0;
    glGetIntegerv(GL_MAX_ATTRIB_STACK_DEPTH, &max);
    CHECK(max >= 16);
    for (int i = 0; i <= max; i++) glPushAttrib(GL_CURRENT_BIT);
    CHECK(glGetError() == GL_STACK_OVERFLOW);
    for (int i = 0; i < max; i++) glPopAttrib();
    glGetIntegerv(GL_ATTRIB_STACK_DEPTH, &v);
    CHECK(v == 0);

    // Client: vertex arrays (with buffer binding) and pixel store
    static const GLfloat data[4] = {0};
    GLuint buf;
    glGenBuffers(1, &buf);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glVertexPointer(2, GL_FLOAT, 0, data);
    glDisableClientState(GL_VERTEX_ARRAY);
    glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glVertexPointer(3, GL_SHORT, 6, (const GLvoid *)0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glClientActiveTexture(GL_TEXTURE2);
    glGetIntegerv(GL_CLIENT_ATTRIB_STACK_DEPTH, &v);
    CHECK(v == 1);
    glPopClientAttrib();
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &v);
    CHECK(v == 4);
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &v);
    CHECK(v == 2);
    GLvoid *ptr = NULL;
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &ptr);
    CHECK(ptr == data && !glIsEnabled(GL_VERTEX_ARRAY));
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &v);
    CHECK(v == 0);
    glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &v);
    CHECK(v == GL_TEXTURE0);
    glPopClientAttrib();
    CHECK(glGetError() == GL_STACK_UNDERFLOW);
    glDeleteBuffers(1, &buf);
}

// Four white 60x60 squares with different vertex calls, pixel coordinates y down
// Compressed textures: paletted images expand to the palette format (read back with glGetTexImage), ETC1 is stored
// as is; formats, image sizes and levels are validated
// glReadPixels within a frame: orientation (GL window coordinates, bottom-left origin), formats and types, pack
// layout, clipping, depth and stencil, drawing on after a read. Leaves a frame in progress, the main loop clears it
static void testReadPixels(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.2f, 0.4f, 0.6f, 0.8f);       // 51, 102, 153, 204
    glClearDepth(0.25);
    glClearStencil(0x5B);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glColor3ub(255, 0, 0);
    glRecti(0, 0, 8, 8);                        // Bottom left
    glColor3ub(0, 0, 255);
    glRecti(392, 232, 400, 240);                // Top right

    GLubyte p[64];
    memset(p, 0, sizeof(p));
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);
    glReadPixels(396, 236, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255 && p[3] == 255);
    glReadPixels(200, 120, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 51 && p[1] == 102 && p[2] == 153 && p[3] == 204);

    // Rows go up from y: row 0 is y = 7 (red), row 1 y = 8 (clear color); columns 7 and 8 likewise
    glReadPixels(7, 7, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[4] == 51 && p[8] == 51 && p[12] == 51);

    // Formats and types
    glReadPixels(3, 3, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0);
    glReadPixels(200, 120, 1, 1, GL_ALPHA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 204);
    glReadPixels(200, 120, 1, 1, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 204);          // L = R + G + B, clamped
    GLfloat f[4];
    glReadPixels(200, 120, 1, 1, GL_RGBA, GL_FLOAT, f);
    CHECK(near(f[0], 0.2) && near(f[1], 0.4) && near(f[2], 0.6) && near(f[3], 0.8));
    GLushort us[2];
    glReadPixels(3, 3, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, us);
    CHECK(us[0] == 0xF800);
    glReadPixels(396, 236, 1, 1, GL_BLUE, GL_UNSIGNED_SHORT, us);
    CHECK(us[0] == 0xFFFF);
    GLshort ss[1];
    glReadPixels(3, 3, 1, 1, GL_RED, GL_SHORT, ss);
    CHECK(ss[0] == 32767);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE);
    glReadPixels(3, 3, 1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, us);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE);
    CHECK(us[0] == 0x00F8);

    // Pack layout: rows of 3 RGB pixels = 9 bytes, rows padded to 12 by GL_PACK_ALIGNMENT 4; skipped pixels stay untouched
    memset(p, 0xAA, sizeof(p));
    glPixelStorei(GL_PACK_ROW_LENGTH, 3);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    glReadPixels(6, 7, 2, 2, GL_RGB, GL_UNSIGNED_BYTE, p);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    CHECK(p[0] == 0xAA && p[3] == 255 && p[6] == 255 && p[9] == 0xAA);     // Row 0: y = 7, x = 6, 7 red
    CHECK(p[12] == 0xAA && p[15] == 51 && p[18] == 51 && p[21] == 0xAA);   // Row 1 at 12: y = 8
    CHECK(glGetError() == GL_NO_ERROR);

    // Clipped: only the pixel inside the window is written
    memset(p, 0xAA, sizeof(p));
    glReadPixels(-1, -1, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 0xAA && p[4] == 0xAA && p[8] == 0xAA && p[12] == 255);

    // Depth (cleared to 0.25, a quad at z = 0 is at 0.5) and stencil
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glRecti(100, 100, 108, 108);
    glDisable(GL_DEPTH_TEST);
    glReadPixels(50, 50, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, f);
    glReadPixels(104, 104, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, f + 1);
    CHECK(fabsf(f[0] - 0.25f) < 1e-3f && fabsf(f[1] - 0.5f) < 1e-3f);
    glReadPixels(50, 50, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 0x5B);
    glReadPixels(50, 50, 8, 1, GL_STENCIL_INDEX, GL_BITMAP, p);
    CHECK(p[0] == 0xFF);                        // Bit 0 of 0x5B

    // Drawing goes on after a read, earlier draws stay
    glColor3ub(0, 255, 0);
    glRecti(200, 200, 208, 208);
    glReadPixels(204, 204, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    glReadPixels(3, 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p + 4);
    CHECK(p[0] == 0 && p[1] == 255 && p[2] == 0 && p[4] == 255 && p[5] == 0);

    // Errors and the ES implementation read format
    glReadPixels(0, 0, 1, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, p);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, p);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glReadPixels(0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_BITMAP, p);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glReadPixels(0, 0, 1, 1, 0x1234, GL_UNSIGNED_BYTE, p);
    CHECK(glGetError() == GL_INVALID_ENUM);
    GLint v[2];
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_FORMAT_OES, v);
    glGetIntegerv(GL_IMPLEMENTATION_COLOR_READ_TYPE_OES, v + 1);
    CHECK(v[0] == GL_RGBA && v[1] == GL_UNSIGNED_BYTE);

    glPopAttrib();
}

// glCopyTexImage2D / glCopyTexSubImage2D within a frame: texel rows go up from y like glReadPixels, component selection
// per internal format (luminance = R), borders, sub-copies keeping the texture's format, draws before a copy keeping
// the old texels, errors. Leaves a frame in progress, the main loop clears it
static void testCopyTexImage(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.2f, 0.4f, 0.6f, 0.8f);       // 51, 102, 153, 204
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(255, 0, 0);
    glRecti(0, 0, 4, 4);                        // Red bottom left, green to its right, clear color above
    glColor3ub(0, 255, 0);
    glRecti(4, 0, 8, 4);

    GLuint tex[2];
    glGenTextures(2, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    GLubyte p[8*8*4];
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);                    // Texel (0, 0)
    CHECK(p[5*4] == 0 && p[5*4 + 1] == 255);                                        // (5, 0)
    CHECK(p[5*32] == 51 && p[5*32 + 1] == 102 && p[5*32 + 2] == 153 && p[5*32 + 3] == 204);    // (0, 5)

    // Components by internal format: luminance is R, alpha is A
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 0, 0, 8, 8, 0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[5] == 0 && p[5*8] == 51);
    GLint v = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &v);
    CHECK(v == GL_LUMINANCE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 0, 0, 8, 8, 0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[5*8] == 204);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 0, 0, 8, 8, 0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, p);
    CHECK(p[5*16] == 51 && p[5*16 + 1] == 204);

    // Border: the image is the inner 2x2 of the 4x4 rectangle at (3, 0), i.e. x = 4..5, y = 1..2 (green)
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 3, 0, 4, 4, 1);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 0 && p[1] == 255 && p[8] == 0 && p[9] == 255);       // Rows padded to 8 bytes

    // Sub-copy into an RGBA4 texture: texels (2, 3) and (3, 4) get red and the clear color, the rest stays 0
    GLushort us[8*8];
    memset(us, 0, sizeof(us));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, us);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 2, 3, 3, 3, 2, 2);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, us);
    CHECK(us[3*8 + 2] == 0xF00F && us[4*8 + 3] == 0x369C && us[3*8 + 1] == 0 && us[5*8 + 2] == 0);

    // A quad drawn before the copy keeps the old texels (white), one drawn after it samples the copy (red)
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    memset(p, 255, sizeof(p));
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    for (int i = 0; i < 2; i++)
    {
        int x = 100 + 20*i;
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2i(x, 100);
        glTexCoord2f(1.0f, 0.0f); glVertex2i(x + 8, 100);
        glTexCoord2f(1.0f, 1.0f); glVertex2i(x + 8, 108);
        glTexCoord2f(0.0f, 1.0f); glVertex2i(x, 108);
        glEnd();
        if (i == 0) glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 8, 8);
    }
    glDisable(GL_TEXTURE_2D);
    glReadPixels(101, 101, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    glReadPixels(121, 101, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p + 4);
    CHECK(p[0] == 255 && p[1] == 255 && p[2] == 255);
    CHECK(p[4] == 255 && p[5] == 0 && p[6] == 0);
    CHECK(glGetError() == GL_NO_ERROR);

    // Errors
    glCopyTexImage2D(GL_TEXTURE_2D, 0, 3, 0, 0, 8, 8, 0);                     // 1..4 are not allowed for copies
    CHECK(glGetError() == GL_INVALID_ENUM);
    glCopyTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, -1, 8, 0);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 2);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 0, 0, 8, 8);                  // Beyond the 8x8 image
    CHECK(glGetError() == GL_INVALID_VALUE);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 0, 0, 4, 4);                  // Level 1 not defined
    CHECK(glGetError() == GL_INVALID_OPERATION);
    static const GLubyte etc1[8] = {0};
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 4, 4, 0, 8, etc1);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 4, 4);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glBindTexture(GL_TEXTURE_2D, 0);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 8, 0);
    CHECK(glGetError() == GL_INVALID_OPERATION);

    glDeleteTextures(2, tex);
    glPopAttrib();
}

static void testCompressed(void)
{
    GLint n = 0, formats[16] = {0};
    glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &n);
    CHECK(n == 11);
    glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, formats);
    CHECK(formats[0] == GL_PALETTE4_RGB8_OES && formats[9] == GL_PALETTE8_RGB5_A1_OES && formats[10] == GL_ETC1_RGB8_OES);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    CHECK(strstr(ext, "GL_OES_compressed_paletted_texture") != NULL);
    CHECK(strstr(ext, "GL_OES_compressed_ETC1_RGB8_texture") != NULL);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);

    // 3x1 PALETTE4_RGB8: indices 1, 2, 3, high nibble first, the last nibble unused
    unsigned char p4[16*3 + 2] = {0};
    for (int i = 0; i < 4; i++) { p4[i*3] = (unsigned char)(10*i); p4[i*3 + 1] = (unsigned char)(10*i + 1); p4[i*3 + 2] = (unsigned char)(10*i + 2); }
    p4[48] = 0x12;
    p4[49] = 0x30;
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGB8_OES, 3, 1, 0, sizeof(p4), p4);
    CHECK(glGetError() == GL_NO_ERROR);
    unsigned char rgb[9] = {0};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    CHECK(rgb[0] == 10 && rgb[1] == 11 && rgb[2] == 12 && rgb[3] == 20 && rgb[6] == 30 && rgb[8] == 32);
    GLint value = 0;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &value);
    CHECK(value == GL_PALETTE4_RGB8_OES);

    // 2x2 PALETTE8_R5_G6_B5 with both levels (level = -1): 512 bytes palette, 4 + 1 indices
    unsigned char p8[512 + 5] = {0};
    unsigned short *palette = (unsigned short *)p8;
    palette[7] = 0xF800;
    palette[200] = 0x07E0;
    palette[255] = 0x001F;
    p8[512] = 7; p8[513] = 200; p8[514] = 255; p8[515] = 0; p8[516] = 7;
    glCompressedTexImage2D(GL_TEXTURE_2D, -1, GL_PALETTE8_R5_G6_B5_OES, 2, 2, 0, sizeof(p8), p8);
    CHECK(glGetError() == GL_NO_ERROR);
    unsigned short texels[4] = {0};
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, texels);
    CHECK(texels[0] == 0xF800 && texels[1] == 0x07E0 && texels[2] == 0x001F && texels[3] == 0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 1, GL_TEXTURE_WIDTH, &value);
    CHECK(value == 1);

    // Errors
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGB8_OES, 3, 1, 0, sizeof(p4) - 1, p4);
    CHECK(glGetError() == GL_INVALID_VALUE);    // imageSize
    glCompressedTexImage2D(GL_TEXTURE_2D, 1, GL_PALETTE4_RGB8_OES, 3, 1, 0, sizeof(p4), p4);
    CHECK(glGetError() == GL_INVALID_VALUE);    // Paletted levels are <= 0
    glCompressedTexImage2D(GL_TEXTURE_2D, -2, GL_PALETTE8_R5_G6_B5_OES, 2, 2, 0, sizeof(p8), p8);
    CHECK(glGetError() == GL_INVALID_VALUE);    // 2x2 has 2 levels
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_PALETTE4_RGB8_OES, 3, 1, 1, sizeof(p4), p4);
    CHECK(glGetError() == GL_INVALID_VALUE);    // Border
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 3, 1, 0, sizeof(p4), p4);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_PALETTE4_RGB8_OES, sizeof(p4), p4);
    CHECK(glGetError() == GL_INVALID_OPERATION);

    // ETC1: 8 bytes per 4x4 block, 6x5 = 2x2 blocks
    unsigned char etc1[32] = {0};
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 6, 5, 0, 24, etc1);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 6, 5, 0, 32, etc1);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &value);
    CHECK(value == 5);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    CHECK(glGetError() == GL_INVALID_OPERATION);    // Cannot be read back
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_ETC1_RGB8_OES, 8, etc1);
    CHECK(glGetError() == GL_INVALID_OPERATION);

    // Proxy: too large, then fitting
    glCompressedTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 2048, 8, 0, 512*2*8, NULL);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &value);
    CHECK(value == 0);
    glCompressedTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 64, 8, 0, 16*2*8, NULL);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &value);
    CHECK(value == 64);
    CHECK(glGetError() == GL_NO_ERROR);

    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glDeleteTextures(1, &tex);
}

// Expected result of a GL logic op on one byte
static GLubyte logicOpResult(GLenum op, GLubyte s, GLubyte d)
{
    switch (op)
    {
        case GL_CLEAR: return 0;
        case GL_AND: return s & d;
        case GL_AND_REVERSE: return s & ~d;
        case GL_COPY: return s;
        case GL_AND_INVERTED: return ~s & d;
        case GL_NOOP: return d;
        case GL_XOR: return s ^ d;
        case GL_OR: return s | d;
        case GL_NOR: return ~(s | d);
        case GL_EQUIV: return ~(s ^ d);
        case GL_INVERT: return ~d;
        case GL_OR_REVERSE: return s | ~d;
        case GL_COPY_INVERTED: return ~s;
        case GL_OR_INVERTED: return ~s | d;
        case GL_NAND: return ~(s & d);
        default: return 0xFF;      // GL_SET
    }
}

// glLogicOp: all 16 ops on RGBA read back with glReadPixels, precedence over blending, clears unaffected, queries,
// attribute groups. Multisampling state (no sample buffers: stored only, drawing unchanged). Leaves a frame in progress
static void testLogicOpAndMultisample(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLint v = 0;
    CHECK(!glIsEnabled(GL_COLOR_LOGIC_OP) && !glIsEnabled(GL_INDEX_LOGIC_OP));
    glGetIntegerv(GL_LOGIC_OP_MODE, &v);
    CHECK(v == GL_COPY);

    // Destination 0x5A 0xA5 0x0F 0xF0, source 0x33 0x55 0xFF 0x00 (the clear quad, if any, ignores the logic op)
    const GLubyte dst[4] = { 0x5A, 0xA5, 0x0F, 0xF0 }, src[4] = { 0x33, 0x55, 0xFF, 0x00 };
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_SET);
    glClearColor(dst[0]/255.0f, dst[1]/255.0f, dst[2]/255.0f, dst[3]/255.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);                         // Ignored while the logic op is on
    glBlendFunc(GL_ZERO, GL_ONE);
    glColor4ub(src[0], src[1], src[2], src[3]);
    for (int i = 0; i < 16; i++)
    {
        glLogicOp(GL_CLEAR + i);
        glRecti(4*i, 0, 4*i + 4, 4);
    }
    CHECK(glGetError() == GL_NO_ERROR);
    GLubyte p[4];
    int failed = 0;
    for (int i = 0; i < 16; i++)
    {
        GLubyte px[4];
        glReadPixels(4*i + 2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        bool ok = true;
        for (int c = 0; c < 4; c++) ok = ok && (px[c] == logicOpResult(GL_CLEAR + i, src[c], dst[c]));
        failed += !ok;
        CHECK(ok);
    }
    // Azahar's hardware renderers on macOS (Vulkan/MoltenVK, OpenGL) skip logic ops; the software renderer has them
    if (failed) printf("%i logic ops wrong: Azahar on macOS needs\nthe software renderer for them\n", failed);

    // Disabled: blending is back (GL_ZERO, GL_ONE keeps the destination)
    glDisable(GL_COLOR_LOGIC_OP);
    glRecti(100, 0, 104, 4);
    glReadPixels(102, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == dst[0] && p[1] == dst[1] && p[2] == dst[2] && p[3] == dst[3]);

    // Errors, glGet, attribute groups (COLOR_BUFFER and ENABLE)
    glLogicOp(GL_XOR);
    glLogicOp(0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_LOGIC_OP_MODE, &v);
    CHECK(v == GL_XOR);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glEnable(GL_COLOR_LOGIC_OP);
    glEnable(GL_INDEX_LOGIC_OP);                // Color index mode: stored only
    glLogicOp(GL_NAND);
    GLboolean b = GL_FALSE;
    glGetBooleanv(GL_COLOR_LOGIC_OP, &b);
    CHECK(b && glIsEnabled(GL_LOGIC_OP));
    glPopAttrib();
    glGetIntegerv(GL_LOGIC_OP_MODE, &v);
    CHECK(v == GL_XOR && !glIsEnabled(GL_COLOR_LOGIC_OP) && !glIsEnabled(GL_INDEX_LOGIC_OP));
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_COLOR_LOGIC_OP);
    glLogicOp(GL_NAND);
    glPopAttrib();
    glGetIntegerv(GL_LOGIC_OP_MODE, &v);
    CHECK(!glIsEnabled(GL_COLOR_LOGIC_OP) && v == GL_NAND);

    // Multisampling: defaults, no sample buffers
    GLfloat f = 0.0f;
    CHECK(glIsEnabled(GL_MULTISAMPLE) && !glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) &&
          !glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE) && !glIsEnabled(GL_SAMPLE_COVERAGE));
    glGetIntegerv(GL_SAMPLE_BUFFERS, &v);
    CHECK(v == 0);
    glGetIntegerv(GL_SAMPLES, &v);
    CHECK(v == 0);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &f);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &b);
    CHECK(f == 1.0f && !b);
    glSampleCoverage(0.25f, GL_TRUE);
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &f);
    glGetBooleanv(GL_SAMPLE_COVERAGE_INVERT, &b);
    CHECK(f == 0.25f && b);
    glSampleCoverage(2.0f, GL_FALSE);           // Clamped
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &f);
    CHECK(f == 1.0f);
    glSampleCoveragex(0x8000, GL_FALSE);        // ES fixed point: 0.5
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &f);
    CHECK(f == 0.5f);

    // Without sample buffers the sample operations do not change what is drawn
    glPushAttrib(GL_MULTISAMPLE_BIT);
    glEnable(GL_SAMPLE_ALPHA_TO_ONE);
    glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glEnable(GL_SAMPLE_COVERAGE);
    glDisable(GL_MULTISAMPLE);
    glSampleCoverage(0.0f, GL_FALSE);
    glDisable(GL_BLEND);
    glColor4ub(10, 20, 30, 40);
    glRecti(110, 0, 114, 4);
    glReadPixels(112, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 10 && p[1] == 20 && p[2] == 30 && p[3] == 40);
    CHECK(glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE) && !glIsEnabled(GL_MULTISAMPLE));
    glPopAttrib();
    glGetFloatv(GL_SAMPLE_COVERAGE_VALUE, &f);
    CHECK(f == 0.5f && glIsEnabled(GL_MULTISAMPLE) && !glIsEnabled(GL_SAMPLE_ALPHA_TO_ONE) &&
          !glIsEnabled(GL_SAMPLE_ALPHA_TO_COVERAGE) && !glIsEnabled(GL_SAMPLE_COVERAGE));
    CHECK(glGetError() == GL_NO_ERROR);

    glPopAttrib();
}

// Color of window pixel (x, y)
static bool pixelIs(int x, int y, GLubyte r, GLubyte g, GLubyte b)
{
    GLubyte p[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    return (p[0] == r) && (p[1] == g) && (p[2] == b);
}

static bool modelviewTranslation(float x, float y, float z)
{
    GLfloat m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    return near(m[12], x) && near(m[13], y) && near(m[14], z);
}

// Display lists: names, compile modes, state and errors at execution time, commands executed immediately while
// compiling, nesting (late binding, self calls up to the nesting limit), glCallLists with glListBase, client data
// (vertex arrays, pixels, control points) copied at compile time, drawing from lists. Leaves a frame in progress
static void testDisplayLists(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Defaults and names
    GLint v[4] = { -1, -1, -1, -1 };
    glGetIntegerv(GL_LIST_BASE, &v[0]);
    glGetIntegerv(GL_LIST_INDEX, &v[1]);
    glGetIntegerv(GL_LIST_MODE, &v[2]);
    glGetIntegerv(GL_MAX_LIST_NESTING, &v[3]);
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] >= 64);
    GLuint base = glGenLists(3);
    CHECK(base != 0 && glIsList(base) && glIsList(base + 2) && !glIsList(base + 3) && !glIsList(0));
    CHECK(glGenLists(0) == 0 && glGetError() == GL_NO_ERROR);
    CHECK(glGenLists(-1) == 0 && glGetError() == GL_INVALID_VALUE);
    GLuint more = glGenLists(2);
    CHECK(more >= base + 3);
    glDeleteLists(base + 1, 1);                 // A gap of one name, then two lists
    CHECK(!glIsList(base + 1) && glIsList(base));
    CHECK(glGenLists(1) == base + 1);           // The gap is reused
    glDeleteLists(more, 2);
    glDeleteLists(0, -1);
    CHECK(glGetError() == GL_INVALID_VALUE);

    // Errors of glNewList / glEndList
    glNewList(0, GL_COMPILE);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glNewList(base, 0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glEndList();
    CHECK(glGetError() == GL_INVALID_OPERATION);

    // GL_COMPILE: recorded only. Queries, client state and pixel store run immediately; errors come at execution
    glNewList(base, GL_COMPILE);
    glGetIntegerv(GL_LIST_INDEX, &v[0]);
    glGetIntegerv(GL_LIST_MODE, &v[1]);
    CHECK(v[0] == (GLint)base && v[1] == GL_COMPILE);
    glNewList(base + 1, GL_COMPILE);            // Nested glNewList: error, compiling goes on
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glLineWidth(3.0f);
    glTranslatef(1.0f, 2.0f, 3.0f);
    glMatrixMode(0x1234);                       // Invalid: recorded, fails when the list runs
    glEnableClientState(GL_NORMAL_ARRAY);       // Client state: executed now
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glEndList();
    CHECK(glGetError() == GL_NO_ERROR);
    CHECK(!glIsEnabled(GL_BLEND) && modelviewTranslation(0.0f, 0.0f, 0.0f));
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &v[0]);
    CHECK(glIsEnabled(GL_NORMAL_ARRAY) && v[0] == 2);
    glDisableClientState(GL_NORMAL_ARRAY);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glGetIntegerv(GL_LIST_INDEX, &v[0]);
    glGetIntegerv(GL_LIST_MODE, &v[1]);
    CHECK(v[0] == 0 && v[1] == 0);

    glCallList(base);
    CHECK(glGetError() == GL_INVALID_ENUM);
    GLfloat f[4];
    glGetFloatv(GL_LINE_WIDTH, f);
    glGetIntegerv(GL_BLEND_SRC, &v[0]);
    glGetIntegerv(GL_BLEND_DST, &v[1]);
    CHECK(glIsEnabled(GL_BLEND) && f[0] == 3.0f && v[0] == GL_SRC_ALPHA && v[1] == GL_ONE);
    CHECK(modelviewTranslation(1.0f, 2.0f, 3.0f));
    glCallList(base);                           // Again: the translation adds up
    glGetError();
    CHECK(modelviewTranslation(2.0f, 4.0f, 6.0f));
    glDisable(GL_BLEND);
    glLineWidth(1.0f);
    glLoadIdentity();

    // GL_COMPILE_AND_EXECUTE: executed while compiling too; glEndList replaces the old contents
    glNewList(base, GL_COMPILE_AND_EXECUTE);
    glLineWidth(5.0f);
    glGetFloatv(GL_LINE_WIDTH, f);
    CHECK(f[0] == 5.0f);
    glScalef(2.0f, 2.0f, 2.0f);
    glEndList();
    CHECK(glGetError() == GL_NO_ERROR);
    glLineWidth(1.0f);
    glLoadIdentity();
    glCallList(base);
    glGetFloatv(GL_LINE_WIDTH, f);
    GLfloat m[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    CHECK(f[0] == 5.0f && m[0] == 2.0f && m[12] == 0.0f);   // The new contents only (no translation)
    glLineWidth(1.0f);

    // Nesting: base + 1 calls base + 2, which is defined later (lists are looked up when executed)
    glNewList(base + 1, GL_COMPILE);
    glTranslatef(10.0f, 0.0f, 0.0f);
    glCallList(base + 2);
    glEndList();
    glNewList(base + 2, GL_COMPILE);
    glTranslatef(0.0f, 20.0f, 0.0f);
    glEndList();
    glLoadIdentity();
    glCallList(base + 1);
    CHECK(modelviewTranslation(10.0f, 20.0f, 0.0f));

    // A list calling itself stops at the nesting limit
    glNewList(base + 2, GL_COMPILE);
    glTranslatef(1.0f, 0.0f, 0.0f);
    glCallList(base + 2);
    glEndList();
    glLoadIdentity();
    glCallList(base + 2);
    glGetIntegerv(GL_MAX_LIST_NESTING, &v[0]);
    CHECK(modelviewTranslation((float)v[0], 0.0f, 0.0f));

    // While a list is compiled with GL_COMPILE_AND_EXECUTE, calling it runs its old contents
    glNewList(base + 1, GL_COMPILE);            // base + 1: translate by 100 in y
    glTranslatef(0.0f, 100.0f, 0.0f);
    glEndList();
    glLoadIdentity();
    glNewList(base + 1, GL_COMPILE_AND_EXECUTE);
    glCallList(base + 1);                       // Old contents: y + 100
    glTranslatef(1.0f, 0.0f, 0.0f);
    glEndList();
    CHECK(modelviewTranslation(1.0f, 100.0f, 0.0f));
    glLoadIdentity();
    glCallList(base + 1);                       // New contents: the call (now of the new list itself)...
    glGetFloatv(GL_MODELVIEW_MATRIX, m);
    CHECK(m[12] == (float)v[0]);                // ... recurses to the limit: x + 1 per level

    // glCallLists: types, list base (also from a list), errors
    GLuint lists = glGenLists(4);
    for (int i = 0; i < 4; i++) {
        glNewList(lists + i, GL_COMPILE);
        glTranslatef((float)(1 << i), 0.0f, 0.0f);     // 1, 2, 4, 8
        glEndList();
    }
    glListBase(lists);
    glGetIntegerv(GL_LIST_BASE, &v[0]);
    CHECK(v[0] == (GLint)lists);
    const GLubyte ub[3] = { 0, 2, 3 };
    glLoadIdentity();
    glCallLists(3, GL_UNSIGNED_BYTE, ub);
    CHECK(modelviewTranslation(13.0f, 0.0f, 0.0f));
    const GLubyte twoBytes[4] = { 0, 1, 0, 2 };  // GL_2_BYTES: big-endian 1 and 2
    const GLfloat floats[2] = { 3.0f, 0.0f };
    glLoadIdentity();
    glCallLists(2, GL_2_BYTES, twoBytes);
    glCallLists(2, GL_FLOAT, floats);
    CHECK(modelviewTranslation(15.0f, 0.0f, 0.0f));
    glCallLists(1, 0x1234, ub);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glCallLists(-1, GL_UNSIGNED_BYTE, ub);
    CHECK(glGetError() == GL_INVALID_VALUE);

    // In a list: the names are copied at compile time, the base is the one when the list runs
    GLubyte names[2] = { 1, 2 };
    glNewList(base, GL_COMPILE);
    glListBase(lists + 1);                      // Recorded
    glCallLists(2, GL_UNSIGNED_BYTE, names);
    glEndList();
    names[0] = names[1] = 0;
    glGetIntegerv(GL_LIST_BASE, &v[0]);
    CHECK(v[0] == (GLint)lists);                // glListBase was not executed
    glLoadIdentity();
    glCallList(base);                           // lists + 2, lists + 3
    CHECK(modelviewTranslation(12.0f, 0.0f, 0.0f));
    glPushAttrib(GL_LIST_BIT);
    glListBase(7);
    glPopAttrib();
    glGetIntegerv(GL_LIST_BASE, &v[0]);
    CHECK(v[0] == (GLint)(lists + 1));
    glListBase(0);
    glDeleteLists(lists, 4);
    CHECK(!glIsList(lists) && !glIsList(lists + 3));
    glLoadIdentity();
    glCallList(lists);                          // Deleted: nothing happens
    CHECK(modelviewTranslation(0.0f, 0.0f, 0.0f) && glGetError() == GL_NO_ERROR);

    // Lighting: a light position is transformed by the modelview when the list runs
    const GLfloat lightPos[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
    glNewList(base, GL_COMPILE);
    glLightfv(GL_LIGHT1, GL_POSITION, lightPos);
    glMaterialf(GL_FRONT, GL_SHININESS, 17.0f);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    glEndList();
    glLoadIdentity();
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);         // x -> y
    glCallList(base);
    glLoadIdentity();
    glGetLightfv(GL_LIGHT1, GL_POSITION, f);
    GLfloat shininess;
    glGetMaterialfv(GL_FRONT, GL_SHININESS, &shininess);
    glGetIntegerv(GL_LIGHT_MODEL_TWO_SIDE, &v[0]);
    CHECK(near(f[0], 0.0) && near(f[1], 1.0) && shininess == 17.0f && v[0] == GL_TRUE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE);

    // Evaluator control points are copied at compile time
    GLfloat points[2][3] = { { 1.0f, 2.0f, 3.0f }, { 4.0f, 5.0f, 6.0f } };
    glNewList(base, GL_COMPILE);
    glMap1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 2, &points[0][0]);
    glEndList();
    points[1][2] = 99.0f;
    glCallList(base);
    GLfloat coeff[6];
    glGetMapfv(GL_MAP1_VERTEX_3, GL_COEFF, coeff);
    CHECK(coeff[0] == 1.0f && coeff[5] == 6.0f);

    // Drawing. Begin/End and glRect in a list, nothing is drawn while compiling with GL_COMPILE
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(0, 0, 255);
    glNewList(base, GL_COMPILE);
    glColor3ub(255, 0, 0);
    glBegin(GL_QUADS);
    glVertex2i(0, 0); glVertex2i(8, 0); glVertex2i(8, 8); glVertex2i(0, 8);
    glEnd();
    glColor3ub(0, 255, 0);
    glRecti(8, 0, 16, 8);
    glEndList();
    glGetFloatv(GL_CURRENT_COLOR, f);           // Not changed by compiling
    CHECK(f[0] == 0.0f && f[2] == 1.0f);
    CHECK(pixelIs(4, 4, 0, 0, 0));
    glCallList(base);
    CHECK(pixelIs(4, 4, 255, 0, 0) && pixelIs(12, 4, 0, 255, 0));
    glPushMatrix();
    glTranslatef(100.0f, 0.0f, 0.0f);           // The same list elsewhere
    glCallList(base);
    glPopMatrix();
    CHECK(pixelIs(104, 4, 255, 0, 0) && pixelIs(112, 4, 0, 255, 0));

    // Vertex arrays are dereferenced at compile time: changing them afterwards does not change the list
    GLshort quad[4][2] = { { 20, 0 }, { 28, 0 }, { 28, 8 }, { 20, 8 } };
    GLubyte colors[4][4];
    for (int i = 0; i < 4; i++) { colors[i][0] = 255; colors[i][1] = 255; colors[i][2] = 0; colors[i][3] = 255; }
    const GLubyte indices[6] = { 0, 1, 2, 0, 2, 3 };
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(2, GL_SHORT, 0, quad);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
    glNewList(base + 1, GL_COMPILE);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);        // Yellow at x = 20..28
    glTranslatef(10.0f, 0.0f, 0.0f);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, indices);     // x = 30..38
    glTranslatef(10.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);                          // x = 40..48
    for (int i = 0; i < 4; i++) glArrayElement(i);
    glEnd();
    glEndList();
    for (int i = 0; i < 4; i++) { colors[i][0] = 0; quad[i][1] += 100; }
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    CHECK(pixelIs(24, 4, 0, 0, 0));
    glLoadIdentity();
    glCallList(base + 1);
    glLoadIdentity();
    CHECK(pixelIs(24, 4, 255, 255, 0) && pixelIs(34, 4, 255, 255, 0) && pixelIs(44, 4, 255, 255, 0));
    glGetFloatv(GL_CURRENT_COLOR, f);
    CHECK(f[0] == 1.0f && f[1] == 1.0f && f[2] == 0.0f);    // glArrayElement in a list sets the current color

    // Texture images are copied at compile time, unpacked with the pixel store state of then
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    GLubyte image[2][3][4];                     // 3 pixels per row, 2 of them used (row length 3, skip 1 pixel)
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 3; x++) {
            image[y][x][0] = (GLubyte)(10*y + x);
            image[y][x][1] = 1; image[y][x][2] = 2; image[y][x][3] = 3;
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 3);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glNewList(base + 2, GL_COMPILE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA, 64, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);     // Executed now
    glEndList();
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v[0]);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v[1]);
    CHECK(v[0] == 64 && v[1] == 0);
    memset(image, 0, sizeof(image));
    glCallList(base + 2);
    GLubyte texels[2][2][4];
    memset(texels, 0xAA, sizeof(texels));
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &v[0]);
    CHECK(texels[0][0][0] == 1 && texels[0][1][0] == 2 && texels[1][0][0] == 11 && texels[1][1][0] == 12 &&
          texels[1][1][3] == 3 && v[0] == GL_NEAREST);
    glDeleteTextures(1, &tex);

    glDeleteLists(base, 3);
    CHECK(glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

static void drawSquares(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 240.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, -35, 0);                    // Row 1 at y = 15..75
    glColor3ub(255, 255, 255);

    glRectf(30.0f, 50.0f, 90.0f, 110.0f);
    glRecti(125, 50, 185, 110);

    static const GLshort corners[4][2] = {{215, 50}, {275, 50}, {275, 110}, {215, 110}};
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) glVertex2sv(corners[i]);
    glEnd();

    glBegin(GL_QUADS);                          // Homogeneous: (2x, 2y, 0, 2)
    glVertex4f(620.0f, 100.0f, 0.0f, 2.0f);
    glVertex4f(740.0f, 100.0f, 0.0f, 2.0f);
    glVertex4f(740.0f, 220.0f, 0.0f, 2.0f);
    glVertex4f(620.0f, 220.0f, 0.0f, 2.0f);
    glEnd();
}

// Second row: four ORANGE (1, 0.5, 0) 60x60 squares from arrays of different types, colors in the array's own
// type. A wrong normalization shows as a different color (white/yellow), a wrong position type as a misplaced square
static void drawArraySquares(void)
{
    glLoadIdentity();
    glTranslatef(0, -80, 0);                    // Row 2 at y = 90..150
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // GL_BYTE positions (unit square, scaled by the modelview) and GL_BYTE colors: 127 = 1.0, 63 = 0.5
    static const GLbyte bytePos[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    static const GLbyte byteColor[16] = {127, 63, -128, 127, 127, 63, -128, 127, 127, 63, -128, 127, 127, 63, -128, 127};
    glPushMatrix();
    glTranslatef(30, 170, 0);
    glScalef(60, 60, 1);
    glVertexPointer(2, GL_BYTE, 0, bytePos);
    glColorPointer(4, GL_BYTE, 0, byteColor);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glPopMatrix();

    // GL_SHORT positions with a stride, GL_UNSIGNED_SHORT colors (size 3)
    static const GLshort shortPos[4][3] = {{125, 170, 99}, {185, 170, 99}, {185, 230, 99}, {125, 230, 99}};
    static const GLushort ushortColor[12] = {65535, 32768, 0, 65535, 32768, 0, 65535, 32768, 0, 65535, 32768, 0};
    glVertexPointer(2, GL_SHORT, 3 * sizeof(GLshort), shortPos);
    glColorPointer(3, GL_UNSIGNED_SHORT, 0, ushortColor);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // ES: GL_FIXED positions and colors, matrices set with the x API
    static const GLfixed fixedPos[8] = {0, 0, 0x10000, 0, 0x10000, 0x10000, 0, 0x10000};
    static const GLfixed fixedColor[16] = {0x10000, 0x8000, 0, 0x10000, 0x10000, 0x8000, 0, 0x10000,
                                           0x10000, 0x8000, 0, 0x10000, 0x10000, 0x8000, 0, 0x10000};
    glPushMatrix();
    glTranslatex(215 << 16, 170 << 16, 0);
    glScalex(60 << 16, 60 << 16, 0x10000);
    glVertexPointer(2, GL_FIXED, 0, fixedPos);
    glColorPointer(4, GL_FIXED, 0, fixedColor);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glPopMatrix();
    glDisableClientState(GL_COLOR_ARRAY);

    // glInterleavedArrays GL_C4UB_V2F, vertices sent with glArrayElement inside glBegin/glEnd
    static struct { GLubyte color[4]; GLfloat pos[2]; } interleaved[4] = {
        {{255, 128, 0, 255}, {310, 170}}, {{255, 128, 0, 255}, {370, 170}},
        {{255, 128, 0, 255}, {370, 230}}, {{255, 128, 0, 255}, {310, 230}},
    };
    glInterleavedArrays(GL_C4UB_V2F, 0, interleaved);
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) glArrayElement(i);
    glEnd();

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glColor3ub(255, 255, 255);
}

// Third row (y = 165..225): four CYAN squares from buffer objects
typedef struct { GLshort pos[2]; GLubyte color[4]; } VboVertex;
static GLuint vbo, ibo, vboColors;

static void createBuffers(void)
{
    // Interleaved positions and colors of all four squares, 4 corners each
    VboVertex vertices[16];
    static const GLshort columns[4] = {30, 125, 215, 310};     // Same as the rows above
    for (int q = 0; q < 4; q++) {
        const GLshort x = columns[q], y = 165;
        const GLshort corners[4][2] = {{x, y}, {(GLshort)(x + 60), y}, {(GLshort)(x + 60), y + 60}, {x, y + 60}};
        for (int c = 0; c < 4; c++) {
            VboVertex *v = &vertices[q * 4 + c];
            v->pos[0] = corners[c][0];
            v->pos[1] = corners[c][1];
            v->color[0] = 0; v->color[1] = 230; v->color[2] = 230; v->color[3] = 255;
        }
    }
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    // Indices for square 2 (two triangles), at an offset of 4 bytes in the element buffer
    const GLushort indices[8] = {0xDEAD, 0xBEEF, 4, 5, 6, 4, 6, 7};
    glGenBuffers(1, &ibo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    // Separate color buffer, rewritten every frame with glBufferSubData (square 3)
    glGenBuffers(1, &vboColors);
    glBindBuffer(GL_ARRAY_BUFFER, vboColors);
    glBufferData(GL_ARRAY_BUFFER, 16 * 4, NULL, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

static void drawBufferSquares(void)
{
    glLoadIdentity();
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    // 1: glDrawArrays from the interleaved VBO
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glVertexPointer(2, GL_SHORT, sizeof(VboVertex), (const GLvoid *)0);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(VboVertex), (const GLvoid *)4);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // 2: glDrawElements with indices from the element buffer, at offset 4
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, (const GLvoid *)4);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    // 3: positions from the VBO, colors from a buffer updated by glBufferSubData: cyan
    GLubyte colors[16 * 4];
    for (int i = 0; i < 16; i++) { colors[i * 4] = 0; colors[i * 4 + 1] = 230; colors[i * 4 + 2] = 230; colors[i * 4 + 3] = 255; }
    glBindBuffer(GL_ARRAY_BUFFER, vboColors);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(colors), colors);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, (const GLvoid *)0);
    glDrawArrays(GL_TRIANGLE_FAN, 8, 4);

    // 4: VBO positions mixed with a client-side color array (pointer set with no buffer bound)
    static const GLubyte clientColors[16 * 4] = {
        [48] = 0, 230, 230, 255, 0, 230, 230, 255, 0, 230, 230, 255, 0, 230, 230, 255 };
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, clientColors);
    glDrawArrays(GL_TRIANGLE_FAN, 12, 4);

    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glColor3ub(255, 255, 255);
}

static void printStats(void)
{
    // Rows 2-5 of the console: timings of the last frame, frames per second over the last second; the cursor stays
    // where the text ended
    printf("\x1b[s");
    printf("\x1b[2;1HCPU:     %6.2fms\x1b[K", C3D_GetProcessingTime());
    printf("\x1b[3;1HGPU:     %6.2fms\x1b[K", C3D_GetDrawingTime());
    printf("\x1b[4;1HCmdBuf:  %6.2f%%\x1b[K", C3D_GetCmdBufUsage()*100.0f);
    static u64 fpsStart;
    static int fpsFrames;
    static float fps;
    const u64 now = osGetTime();
    if (fpsStart == 0) fpsStart = now;
    fpsFrames++;
    if (now - fpsStart >= 1000)
    {
        fps = fpsFrames*1000.0f/(now - fpsStart);
        fpsFrames = 0;
        fpsStart = now;
    }
    printf("\x1b[5;1HFPS:     %6.2f\x1b[K", fps);
    printf("\x1b[u");
}

int main(void)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    if (!c3dglInit()) {
        printf("c3dglInit failed\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        gfxExit();
        return 1;
    }

    printf("c3dgl api test\n\n\n\n\n"
           "(C3DGL warnings below come from\n"
           " the error checks, they are expected)\n\n");
    testErrors();
    testCapabilities();
    testQueries();
    testMatrices();
    testArraysAndEs();
    testBuffers();
    testAttribStacks();
    testCompressed();
    testReadPixels();
    testCopyTexImage();
    testLogicOpAndMultisample();
    testDisplayLists();
    createBuffers();
    CHECK(glGetError() == GL_NO_ERROR);         // Nothing left over

    printf("\n%i/%i checks passed\n\n"
           "Expected on the top screen:\n"
           "- GREEN background (all passed)\n"
           "- four identical white squares\n"
           "  in a row, below them four\n"
           "  identical ORANGE squares and\n"
           "  four identical CYAN squares\n\n"
           "START: exit\n", checks - failures, checks);

    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        if (hidKeysDown() & KEY_START) break;

        if (failures == 0) glClearColor(0.1f, 0.5f, 0.2f, 1.0f);
        else glClearColor(0.7f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        drawSquares();
        drawArraySquares();
        drawBufferSquares();
        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
