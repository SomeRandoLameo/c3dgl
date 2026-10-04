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
#include <stdlib.h>
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_DOUBLE, pixel);
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
// Color of one pixel: 'r' red, 'b' blue (the two halves of the texgen texture), '?' anything else
static char texGenPixel(int x, int y)
{
    GLubyte p[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    if (p[0] == 255 && p[1] == 0 && p[2] == 0) return 'r';
    if (p[0] == 0 && p[1] == 0 && p[2] == 255) return 'b';
    return '?';
}

static void testTexGen(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    // Defaults: eye linear, s and t planes along x and y, all off
    GLint mode = 0;
    GLfloat plane[4];
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mode);
    CHECK(mode == GL_EYE_LINEAR);
    glGetTexGenfv(GL_T, GL_OBJECT_PLANE, plane);
    CHECK(plane[0] == 0.0f && plane[1] == 1.0f && plane[2] == 0.0f && plane[3] == 0.0f);
    glGetTexGenfv(GL_R, GL_EYE_PLANE, plane);
    CHECK(plane[0] == 0.0f && plane[1] == 0.0f && plane[2] == 0.0f && plane[3] == 0.0f);
    CHECK(!glIsEnabled(GL_TEXTURE_GEN_S) && !glIsEnabled(GL_TEXTURE_GEN_Q));

    // Errors: sphere map only for s and t, planes only through the v functions
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_LINEAR);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexGeni(0x1234, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexGenf(GL_S, GL_OBJECT_PLANE, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetTexGeniv(GL_S, 0x1234, &mode);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glGetTexGeniv(GL_T, GL_TEXTURE_GEN_MODE, &mode);
    CHECK(mode == GL_SPHERE_MAP && glGetError() == GL_NO_ERROR);

    // The eye plane is transformed by the inverse modelview: (1, 0, 0, 0) under a translation by 1 in x is x - 1
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(1.0f, 0.0f, 0.0f);
    const GLdouble eyePlane[4] = { 1.0, 0.0, 0.0, 0.0 };
    glTexGendv(GL_S, GL_EYE_PLANE, eyePlane);
    glLoadIdentity();
    GLdouble d[4];
    glGetTexGendv(GL_S, GL_EYE_PLANE, d);
    CHECK(near(d[0], 1.0) && near(d[1], 0.0) && near(d[2], 0.0) && near(d[3], -1.0));
    const GLint objectPlane[4] = { 2, 3, 4, 5 };
    glTexGeniv(GL_Q, GL_OBJECT_PLANE, objectPlane);
    GLint iv[4];
    glGetTexGeniv(GL_Q, GL_OBJECT_PLANE, iv);
    CHECK(iv[0] == 2 && iv[1] == 3 && iv[2] == 4 && iv[3] == 5);

    // Per texture unit; enables in GL_ENABLE_BIT, all of it in GL_TEXTURE_BIT
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_GEN_S);
    glGetTexGeniv(GL_T, GL_TEXTURE_GEN_MODE, &mode);
    CHECK(mode == GL_EYE_LINEAR && glIsEnabled(GL_TEXTURE_GEN_S));
    glActiveTexture(GL_TEXTURE0);
    GLboolean b = GL_TRUE;
    glGetBooleanv(GL_TEXTURE_GEN_S, &b);
    CHECK(b == GL_FALSE);
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_TEXTURE_GEN_T);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glPopAttrib();
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mode);
    CHECK(!glIsEnabled(GL_TEXTURE_GEN_T) && mode == GL_OBJECT_LINEAR);
    glPushAttrib(GL_TEXTURE_BIT);
    glEnable(GL_TEXTURE_GEN_T);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
    glPopAttrib();
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mode);
    CHECK(!glIsEnabled(GL_TEXTURE_GEN_T) && mode == GL_OBJECT_LINEAR);

    // Display lists record glTexGen
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, (const GLfloat[]){ 6.0f, 7.0f, 8.0f, 9.0f });
    glEndList();
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mode);
    CHECK(mode == GL_OBJECT_LINEAR);
    glCallList(list);
    glGetTexGeniv(GL_S, GL_TEXTURE_GEN_MODE, &mode);
    glGetTexGenfv(GL_S, GL_OBJECT_PLANE, plane);
    CHECK(mode == GL_SPHERE_MAP && plane[0] == 6.0f && plane[3] == 9.0f);
    glDeleteLists(list, 1);
    glPopAttrib();
    CHECK(glGetError() == GL_NO_ERROR);

    // Rendering. 8x8 texture, left half red, right half blue: s < 0.5 is red, s > 0.5 blue
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLubyte texels[8*8*4];
    for (int i = 0; i < 8*8; i++)
    {
        bool right = (i % 8) >= 4;
        texels[4*i] = right ? 0 : 255;
        texels[4*i + 1] = 0;
        texels[4*i + 2] = right ? 255 : 0;
        texels[4*i + 3] = 255;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glEnable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(255, 255, 255);

    // Object linear s = x/16 replaces the current s (0.9: blue everywhere)
    const GLfloat sPlane[4] = { 1.0f/16.0f, 0.0f, 0.0f, 0.0f };
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, sPlane);
    glEnable(GL_TEXTURE_GEN_S);
    glTexCoord2f(0.9f, 0.0f);
    glRecti(0, 20, 16, 36);
    CHECK(texGenPixel(3, 28) == 'r' && texGenPixel(12, 28) == 'b');

    // Eye linear: the plane was given under a translation by 100, so s = (x_eye - 100)/16
    glTranslatef(100.0f, 0.0f, 0.0f);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
    glTexGenfv(GL_S, GL_EYE_PLANE, sPlane);
    glLoadIdentity();
    glRecti(100, 20, 116, 36);
    CHECK(texGenPixel(103, 28) == 'r' && texGenPixel(112, 28) == 'b');

    // Generated r goes through the texture matrix (s' = r): s = 0, r = x/16
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, (const GLfloat[]){ 0.0f, 0.0f, 0.0f, 0.0f });
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGenfv(GL_R, GL_OBJECT_PLANE, (const GLfloat[]){ 1.0f/16.0f, 0.0f, 0.0f, -200.0f/16.0f });
    glEnable(GL_TEXTURE_GEN_R);
    glMatrixMode(GL_TEXTURE);
    const GLfloat swapSR[16] = { 0, 0, 0, 0,  0, 1, 0, 0,  1, 0, 0, 0,  0, 0, 0, 1 };   // Column-major: s' = r
    glLoadMatrixf(swapSR);
    glMatrixMode(GL_MODELVIEW);
    glRecti(200, 20, 216, 36);
    CHECK(texGenPixel(203, 28) == 'r' && texGenPixel(212, 28) == 'b');
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glDisable(GL_TEXTURE_GEN_R);

    // Generated q divides (projection mode on unit 0): s = x/16, q = 2
    glTexGenfv(GL_S, GL_OBJECT_PLANE, (const GLfloat[]){ 1.0f/16.0f, 0.0f, 0.0f, -300.0f/16.0f });
    glTexGeni(GL_Q, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGenfv(GL_Q, GL_OBJECT_PLANE, (const GLfloat[]){ 0.0f, 0.0f, 0.0f, 2.0f });
    glEnable(GL_TEXTURE_GEN_Q);
    glRecti(300, 20, 332, 36);
    CHECK(texGenPixel(306, 28) == 'r' && texGenPixel(312, 28) == 'r' && texGenPixel(328, 28) == 'b');
    glDisable(GL_TEXTURE_GEN_Q);

    // Unit 1 (unit 0 off): object linear s = (x - 20)/16
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnable(GL_TEXTURE_2D);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_OBJECT_LINEAR);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, (const GLfloat[]){ 1.0f/16.0f, 0.0f, 0.0f, -20.0f/16.0f });
    glEnable(GL_TEXTURE_GEN_S);
    glRecti(20, 60, 36, 76);
    CHECK(texGenPixel(23, 68) == 'r' && texGenPixel(32, 68) == 'b');
    glDisable(GL_TEXTURE_GEN_S);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glEnable(GL_TEXTURE_2D);

    // Sphere map, seen straight on (eye position ~ (0, 0, -500)): normal (1, 0, 1) reflects to s ~ 0.85, (-1, 0, 1)
    // to s ~ 0.15
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-200.0, 200.0, -120.0, 120.0, -1000.0, 1000.0);
    glMatrixMode(GL_MODELVIEW);
    glTranslatef(0.0f, 0.0f, -500.0f);
    glEnable(GL_NORMALIZE);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
    glEnable(GL_TEXTURE_GEN_S);
    glEnable(GL_TEXTURE_GEN_T);
    glNormal3f(1.0f, 0.0f, 1.0f);
    glRecti(-28, -8, -12, 8);
    glNormal3f(-1.0f, 0.0f, 1.0f);
    glRecti(12, -8, 28, 8);
    CHECK(texGenPixel(180, 120) == 'b' && texGenPixel(220, 120) == 'r');

    glDeleteTextures(1, &tex);
    glPopAttrib();
    CHECK(glGetError() == GL_NO_ERROR);
}

// Window coordinates for the next draws (pushed with the rest of the state by the caller)
static void windowProjection(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// A w x 8 quad at (x, y) with s from 0 to 1 and t from t0 to t1
static void texturedQuad(int x, int y, int w, float t0, float t1)
{
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, t0); glVertex2i(x, y);
    glTexCoord2f(1.0f, t0); glVertex2i(x + w, y);
    glTexCoord2f(1.0f, t1); glVertex2i(x + w, y + 8);
    glTexCoord2f(0.0f, t1); glVertex2i(x, y + 8);
    glEnd();
}

// Color of window pixel (x, y) within 1, alpha too unless a < 0; prints it otherwise
static bool pixelNear(int x, int y, int r, int g, int b, int a)
{
    GLubyte p[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    bool ok = (abs(p[0] - r) <= 1) && (abs(p[1] - g) <= 1) && (abs(p[2] - b) <= 1) && ((a < 0) || (abs(p[3] - a) <= 1));
    if (!ok) printf("  pixel (%i, %i): %i %i %i %i\n", x, y, p[0], p[1], p[2], p[3]);
    return ok;
}

// Internal formats (sized, intensity) loaded from any format/type, glGetTexImage conversions, texture priorities,
// residency and the border color
static void testTexFormats(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    windowProjection();
    GLuint tex[2];
    glGenTextures(2, tex);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    GLint v = 0;
    GLubyte p[64];

    // RGBA data into an RGB texture: alpha is dropped and reads back as 1
    static const GLubyte rgba[4*4] = { 255, 0, 0, 10,  0, 255, 0, 20,  0, 0, 255, 30,  10, 20, 30, 40 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[3] == 255 && p[12] == 10 && p[13] == 20 && p[14] == 30 && p[15] == 255);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &v);
    CHECK(v == GL_RGB8);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_ALPHA_SIZE, &v);
    CHECK(v == 0);

    // Sized 16-bit formats from bytes
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_RED_SIZE, &v);
    CHECK(v == 4);
    GLushort us[4];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, us);
    CHECK(us[0] == 0xF001 && us[1] == 0x0F01);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R3_G3_B2, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_GREEN_SIZE, &v);
    CHECK(v == 6);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, us);
    CHECK(us[0] == 0xF800 && us[1] == 0x07E0);

    // Other types (GL 1.1 table 2.9, clamped), swapped bytes, single components
    static const GLfloat fl[2*4] = { 1.0f, 0.5f, 0.0f, 2.0f,  -1.0f, 0.25f, 1.0f, 0.0f };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_FLOAT, fl);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 128 && p[2] == 0 && p[3] == 255 && p[4] == 0 && p[5] == 64 && p[6] == 255 && p[7] == 0);
    GLfloat f[8];
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, f);
    CHECK(f[0] == 1.0f && near(f[1], 128/255.0) && f[7] == 0.0f);
    static const GLbyte sb[2] = { 127, -128 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 2, 1, 0, GL_LUMINANCE, GL_BYTE, sb);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 0);
    static const GLushort gs[2] = { 0x00FF, 0xFF00 };
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_TRUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 2, 1, 0, GL_ALPHA, GL_UNSIGNED_SHORT, gs);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, GL_FALSE);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_ALPHA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 254 && p[1] == 1);
    static const GLubyte red[2] = { 200, 100 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RED, GL_UNSIGNED_BYTE, red);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 200 && p[1] == 0 && p[2] == 0 && p[3] == 100);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 200 && p[1] == 100);

    // Sub images are converted to the texture's format: luminance data into RGB
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, &red[0]);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[3] == 200 && p[4] == 200 && p[5] == 200 && glGetError() == GL_NO_ERROR);

    // Intensity: I is R of the image, read back as R (table 6.1), sampled as (I, I, I, I)
    static const GLubyte la[2*2] = { 200, 50,  100, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_INTENSITY8, 2, 1, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la);
    GLint lum = -1;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTENSITY_SIZE, &v);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_LUMINANCE_SIZE, &lum);
    CHECK(v == 8 && lum == 0);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 200 && p[1] == 0 && p[3] == 255 && p[4] == 100);

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    glColor4ub(255, 255, 255, 255);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    texturedQuad(20, 20, 8, 0.0f, 1.0f);
    CHECK(pixelNear(21, 21, 200, 200, 200, 200));
    // GL_BLEND with a zero env color: C = Cf*(1 - I), and for intensity also A = Af*(1 - I)
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    texturedQuad(40, 20, 8, 0.0f, 1.0f);
    CHECK(pixelNear(41, 21, 55, 55, 55, 55));
    // GL_ADD: C = Cf + I, A = Af + I (luminance alpha: Af*At); texel 1 on black with alpha 128
    glColor4ub(0, 0, 0, 128);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
    texturedQuad(60, 20, 8, 0.0f, 1.0f);
    CHECK(pixelNear(66, 21, 100, 100, 100, 228));
    // The same luminance alpha texture blends alpha as Af*At
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 2, 1, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, la);
    glColor4ub(255, 255, 255, 255);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    texturedQuad(80, 20, 8, 0.0f, 1.0f);
    CHECK(pixelNear(81, 21, 55, 55, 55, 50));
    glDisable(GL_TEXTURE_2D);

    // Errors
    glTexImage2D(GL_TEXTURE_2D, 0, 5, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_4_4_4_4, rgba);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_BITMAP, rgba);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_FLOAT, f);
    CHECK(glGetError() == GL_INVALID_ENUM);

    // Priorities are stored (clamped), also through display lists as floats; all textures are resident
    const GLclampf prio[2] = { 0.25f, 2.0f };
    glPrioritizeTextures(2, tex, prio);
    GLfloat fv[4] = { 0 };
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, fv);
    CHECK(fv[0] == 0.25f);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, fv);
    CHECK(fv[0] == 1.0f);
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, 0.5f);
    glEndList();
    glCallList(list);
    glDeleteLists(list, 1);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_PRIORITY, fv);
    CHECK(fv[0] == 0.5f);
    GLboolean res[2] = { GL_FALSE, GL_FALSE };
    CHECK(glAreTexturesResident(2, tex, res) == GL_TRUE && res[0] == GL_FALSE);    // Untouched when all are resident
    const GLuint bad[2] = { tex[0], 0 };
    CHECK(glAreTexturesResident(2, bad, res) == GL_FALSE && glGetError() == GL_INVALID_VALUE);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_RESIDENT, &v);
    CHECK(v == GL_TRUE);
    const GLfloat border[4] = { 0.5f, 0.25f, 2.0f, 0.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glGetTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, fv);
    CHECK(fv[0] == 0.5f && fv[1] == 0.25f && fv[2] == 1.0f && fv[3] == 0.0f);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);

    glDeleteTextures(2, tex);
    glPopAttrib();
    CHECK(glGetError() == GL_NO_ERROR);
}

// 1D textures: binding, images with a border, sub images, proxies, rendering (t has no effect), GL_TEXTURE_2D taking
// precedence, mipmaps (also generated), copies from the framebuffer, display lists, attribute stacks
static void test1D(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    windowProjection();
    GLuint tex[3];
    glGenTextures(3, tex);
    GLint v = 0;
    GLubyte p[64*3];

    glBindTexture(GL_TEXTURE_1D, tex[0]);
    glGetIntegerv(GL_TEXTURE_BINDING_1D, &v);
    CHECK(v == (GLint)tex[0]);
    glBindTexture(GL_TEXTURE_2D, tex[0]);                   // Already a 1D texture
    CHECK(glGetError() == GL_INVALID_OPERATION);

    // The border texels are dropped; height 1
    static const GLubyte row[6*3] = { 9, 9, 9,  255, 0, 0,  0, 255, 0,  0, 0, 255,  255, 255, 0,  9, 9, 9 };
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 6, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 6);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_HEIGHT, &v);
    CHECK(v == 1);
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[4] == 255 && p[8] == 255 && p[9] == 255 && p[10] == 255 && p[11] == 0);
    static const GLubyte white[3] = { 255, 255, 255 };
    glTexSubImage1D(GL_TEXTURE_1D, 0, 2, 1, GL_RGB, GL_UNSIGNED_BYTE, white);
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[6] == 255 && p[7] == 255 && p[8] == 255);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 2, 1, GL_RGB, GL_UNSIGNED_BYTE, &row[9]);

    // Proxies
    glTexImage1D(GL_PROXY_TEXTURE_1D, 0, GL_RGBA, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 64);
    glTexImage1D(GL_PROXY_TEXTURE_1D, 0, GL_RGBA, 2048, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 0 && glGetError() == GL_NO_ERROR);

    // Texel i across s, whatever t is
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_1D);
    texturedQuad(100, 100, 32, 5.0f, -3.0f);
    CHECK(pixelNear(101, 101, 255, 0, 0, -1) && pixelNear(109, 104, 0, 255, 0, -1) && pixelNear(117, 106, 0, 0, 255, -1) &&
          pixelNear(130, 107, 255, 255, 0, -1));

    // GL_TEXTURE_2D takes precedence
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glEnable(GL_TEXTURE_2D);
    texturedQuad(140, 100, 32, 0.0f, 1.0f);
    glDisable(GL_TEXTURE_2D);
    CHECK(pixelNear(141, 101, 255, 255, 255, -1));

    // Mipmaps: 64 texels red at level 0, green below (stored down to 8 texels, level 3), the rest only for completeness.
    // A 128 pixel wide quad samples level 0, a 2 pixel wide one level 5, clamped to level 3 (sizes with room for
    // Azahar's resolution scaling, which lowers the LOD)
    glBindTexture(GL_TEXTURE_1D, tex[2]);
    for (int level = 0; level <= 6; level++)
    {
        for (int i = 0; i < 64; i++) { p[i*3] = (level == 0)? 255 : 0; p[i*3 + 1] = (level == 0)? 0 : 255; p[i*3 + 2] = 0; }
        glTexImage1D(GL_TEXTURE_1D, level, GL_RGB, 64 >> level, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    }
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    texturedQuad(150, 100, 128, 0.0f, 1.0f);
    texturedQuad(280, 100, 2, 0.0f, 1.0f);
    CHECK(pixelNear(207, 104, 255, 0, 0, -1) && pixelNear(280, 104, 0, 255, 0, -1));

    // Generated: red and blue texels alternating average to purple from level 1 on
    glTexParameteri(GL_TEXTURE_1D, GL_GENERATE_MIPMAP, GL_TRUE);
    for (int i = 0; i < 64; i++) { p[i*3] = (i & 1)? 0 : 255; p[i*3 + 1] = 0; p[i*3 + 2] = (i & 1)? 255 : 0; }
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 64, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    texturedQuad(290, 100, 2, 0.0f, 1.0f);
    CHECK(pixelNear(290, 104, 128, 0, 128, -1));
    glDisable(GL_TEXTURE_1D);

    // Copies: red at x = 0..3, green at 4..7 of the bottom row
    glBindTexture(GL_TEXTURE_1D, tex[0]);
    glColor3ub(255, 0, 0);
    glRecti(0, 0, 4, 1);
    glColor3ub(0, 255, 0);
    glRecti(4, 0, 8, 1);
    glCopyTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 0, 0, 8, 0);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 255 && p[1] == 0 && p[5*3] == 0 && p[5*3 + 1] == 255);
    glCopyTexSubImage1D(GL_TEXTURE_1D, 0, 0, 4, 0, 2);
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(p[0] == 0 && p[1] == 255 && p[3] == 0 && p[4] == 255 && p[6] == 255);

    // Display lists record 1D images and copies
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 4, 1, GL_RGB, GL_UNSIGNED_BYTE, row);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 1, GL_RGB, GL_UNSIGNED_BYTE, white);
    glEndList();
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
    glCallList(list);
    glDeleteLists(list, 1);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &v);
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGB, GL_UNSIGNED_BYTE, p);
    CHECK(v == 4 && p[0] == 255 && p[1] == 255 && p[3] == 0 && p[4] == 255);

    // Attribute stacks: GL_TEXTURE_BIT has the 1D enable and binding
    glEnable(GL_TEXTURE_1D);
    glPushAttrib(GL_TEXTURE_BIT);
    glDisable(GL_TEXTURE_1D);
    glBindTexture(GL_TEXTURE_1D, tex[2]);
    glPopAttrib();
    glGetIntegerv(GL_TEXTURE_BINDING_1D, &v);
    CHECK(glIsEnabled(GL_TEXTURE_1D) && v == (GLint)tex[0]);
    glDisable(GL_TEXTURE_1D);

    // Errors
    glTexImage1D(GL_TEXTURE_2D, 0, GL_RGB, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_1D, 0, GL_RGB, 1, 1, 0, GL_RGB, GL_UNSIGNED_BYTE, white);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 4, 1, GL_RGB, GL_UNSIGNED_BYTE, white);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glCopyTexImage1D(GL_TEXTURE_1D, 0, GL_RGB, 0, 0, 8, 2);
    CHECK(glGetError() == GL_INVALID_VALUE);

    glDeleteTextures(3, tex);
    glPopAttrib();
    CHECK(glGetError() == GL_NO_ERROR);
}

// glDrawBuffer / glReadBuffer: double-buffered, no stereo or aux buffers; GL_NONE draws and clears no color
static void testColorBuffers(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    windowProjection();
    GLint v[2] = { 0, 0 };
    GLboolean b[2] = { GL_FALSE, GL_TRUE };
    glGetIntegerv(GL_DRAW_BUFFER, &v[0]);
    glGetIntegerv(GL_READ_BUFFER, &v[1]);
    CHECK(v[0] == GL_BACK && v[1] == GL_BACK);
    glGetBooleanv(GL_DOUBLEBUFFER, &b[0]);
    glGetBooleanv(GL_STEREO, &b[1]);
    glGetIntegerv(GL_AUX_BUFFERS, &v[0]);
    CHECK(b[0] == GL_TRUE && b[1] == GL_FALSE && v[0] == 0);

    glDrawBuffer(GL_RIGHT);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glDrawBuffer(GL_AUX0);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glDrawBuffer(0x1234);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glReadBuffer(GL_NONE);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glReadBuffer(GL_FRONT_AND_BACK);
    CHECK(glGetError() == GL_INVALID_ENUM);

    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawBuffer(GL_NONE);
    glClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3ub(0, 0, 255);
    glRecti(10, 10, 20, 20);
    CHECK(pixelNear(15, 15, 255, 0, 0, -1) && pixelNear(100, 100, 255, 0, 0, -1));
    glDrawBuffer(GL_FRONT);                     // Drawn like the back buffer
    glRecti(10, 10, 20, 20);
    glReadBuffer(GL_FRONT_LEFT);
    CHECK(pixelNear(15, 15, 0, 0, 255, -1));

    // Groups: draw buffer in GL_COLOR_BUFFER_BIT, read buffer in GL_PIXEL_MODE_BIT; both in display lists
    glPushAttrib(GL_COLOR_BUFFER_BIT | GL_PIXEL_MODE_BIT);
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_BACK);
    glEndList();
    glCallList(list);
    glDeleteLists(list, 1);
    glGetIntegerv(GL_DRAW_BUFFER, &v[0]);
    glGetIntegerv(GL_READ_BUFFER, &v[1]);
    CHECK(v[0] == GL_NONE && v[1] == GL_BACK);
    glPopAttrib();
    glGetIntegerv(GL_DRAW_BUFFER, &v[0]);
    glGetIntegerv(GL_READ_BUFFER, &v[1]);
    CHECK(v[0] == GL_FRONT && v[1] == GL_FRONT_LEFT);

    glPopAttrib();
    glGetIntegerv(GL_DRAW_BUFFER, &v[0]);
    CHECK(v[0] == GL_BACK && glGetError() == GL_NO_ERROR);
}

// Depth of window pixel (x, y) within 1e-3
static bool depthNear(int x, int y, float d)
{
    GLfloat f = -1.0f;
    glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &f);
    bool ok = fabsf(f - d) < 1e-3f;
    if (!ok) printf("  depth (%i, %i): %f\n", x, y, f);
    return ok;
}

static GLubyte stencilAt(int x, int y)
{
    GLubyte s = 0;
    glReadPixels(x, y, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &s);
    return s;
}

// glRasterPos (transform, clipping, lit color, queries), glDrawPixels (formats, unpack modes, zoom, tiles, fragment
// operations, depth and stencil), glBitmap, glCopyPixels, attribute groups, display lists and errors
static void testPixels(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLfloat f[4];
    GLint v[4];
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v);
    CHECK(f[0] == 0.0f && f[1] == 0.0f && f[2] == 0.0f && f[3] == 1.0f && v[0] == 1);
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f);
    CHECK(f[0] == 1.0f && f[1] == 1.0f && f[2] == 1.0f && f[3] == 1.0f);
    glGetFloatv(GL_ZOOM_X, f);
    glGetFloatv(GL_ZOOM_Y, f + 1);
    CHECK(f[0] == 1.0f && f[1] == 1.0f);

    // Raster position: transformed to window coordinates, current color and texcoords (texture matrix applied)
    windowProjection();
    glColor4ub(10, 20, 30, 40);
    glTexCoord2f(0.5f, 0.25f);
    glMatrixMode(GL_TEXTURE);
    glTranslatef(1.0f, 0.0f, 0.0f);
    glMatrixMode(GL_MODELVIEW);
    glRasterPos3f(30.0f, 40.0f, 0.5f);         // Ortho z: window depth 0.25
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    CHECK(near(f[0], 30.0) && near(f[1], 40.0) && near(f[2], 0.25) && near(f[3], 1.0));
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f);
    CHECK(fabsf(f[1] - 20/255.0f) < 1e-3f && fabsf(f[3] - 40/255.0f) < 1e-3f);
    glGetFloatv(GL_CURRENT_RASTER_TEXTURE_COORDS, f);
    CHECK(near(f[0], 1.5) && near(f[1], 0.25) && near(f[3], 1.0));
    glGetFloatv(GL_CURRENT_RASTER_DISTANCE, f);
    CHECK(fabsf(f[0] - sqrtf(30*30 + 40*40 + 0.25f)) < 1e-3f);
    glTranslatef(5.0f, 0.0f, 0.0f);
    glRasterPos4f(20.0f, 40.0f, 0.0f, 2.0f);    // (10, 20) moved by 5
    glLoadIdentity();
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    CHECK(near(f[0], 15.0) && near(f[1], 20.0) && near(f[3], 2.0));

    // Clipped: invalid, the rest stays
    glRasterPos2i(-5, 10);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v);
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    CHECK(v[0] == 0 && near(f[0], 15.0));
    glRasterPos2i(5, 10);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v);
    CHECK(v[0] == 1);
    GLdouble plane[4] = { -1.0, 0.0, 0.0, 4.0 };    // x <= 4
    glClipPlane(GL_CLIP_PLANE0, plane);
    glEnable(GL_CLIP_PLANE0);
    glRasterPos2i(5, 10);
    glGetIntegerv(GL_CURRENT_RASTER_POSITION_VALID, v);
    CHECK(v[0] == 0);
    glDisable(GL_CLIP_PLANE0);

    // Lit raster color: ambient only
    glEnable(GL_LIGHTING);
    GLfloat white[4] = { 1, 1, 1, 1 }, ambient[4] = { 0.5f, 0.25f, 1.0f, 1.0f };
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, white);
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
    glRasterPos2i(5, 10);
    glDisable(GL_LIGHTING);
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f);
    CHECK(fabsf(f[0] - 0.5f) < 0.01f && fabsf(f[1] - 0.25f) < 0.01f && f[2] == 1.0f && f[3] == 1.0f);

    // glDrawPixels: rows go up from the raster position
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(0.5);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    const GLubyte image[16] = { 255, 0, 0, 255,  0, 255, 0, 255,      // Bottom row: red, green
                                0, 0, 255, 255,  255, 255, 255, 128 }; // Top row: blue, white
    glRasterPos2i(20, 20);
    glDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, image);
    CHECK(pixelNear(20, 20, 255, 0, 0, 255) && pixelNear(21, 20, 0, 255, 0, 255));
    CHECK(pixelNear(20, 21, 0, 0, 255, 255) && pixelNear(21, 21, 255, 255, 255, 128));
    CHECK(pixelNear(22, 20, 0, 0, 0, 0) && pixelNear(20, 22, 0, 0, 0, 0) && pixelNear(19, 19, 0, 0, 0, 0));

    // Zoom (pixel (i, j) covers [x + zx*i, x + zx*(i + 1)) x ...), negative zoom mirrors
    glPixelZoom(2.0f, 3.0f);
    glRasterPos2i(40, 20);
    glDrawPixels(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    CHECK(pixelNear(40, 20, 255, 0, 0, 255) && pixelNear(41, 22, 255, 0, 0, 255) && pixelNear(42, 20, 0, 255, 0, 255));
    CHECK(pixelNear(43, 22, 0, 255, 0, 255) && pixelNear(44, 20, 0, 0, 0, 0) && pixelNear(40, 23, 0, 0, 0, 0));
    glPixelZoom(-1.0f, 1.0f);
    glRasterPos2i(60, 20);
    glDrawPixels(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    CHECK(pixelNear(59, 20, 255, 0, 0, 255) && pixelNear(58, 20, 0, 255, 0, 255) && pixelNear(60, 20, 0, 0, 0, 0));
    glPixelZoom(1.0f, 1.0f);

    // Formats, types and unpack modes
    const GLfloat lum[2] = { 0.5f, 1.0f };
    glRasterPos2i(70, 20);
    glDrawPixels(2, 1, GL_LUMINANCE, GL_FLOAT, lum);
    CHECK(pixelNear(70, 20, 128, 128, 128, 255) && pixelNear(71, 20, 255, 255, 255, 255));
    const GLushort rgb565[1] = { 0x07E0 };
    glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rgb565);
    CHECK(pixelNear(70, 20, 0, 255, 0, 255));
    // Rows of 8 bytes (alignment 8), each starting with a skipped pixel: red, then blue
    const GLubyte rows2[16] = { 1, 2, 3, 255, 0, 0, 9, 9,  4, 5, 6, 0, 0, 255, 9, 9 };
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glRasterPos2i(80, 20);
    glDrawPixels(1, 2, GL_RGB, GL_UNSIGNED_BYTE, rows2);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    CHECK(pixelNear(80, 20, 255, 0, 0, 255) && pixelNear(80, 21, 0, 0, 255, 255));

    // More than one tile (256 pixels): a 300 x 1 ramp
    GLubyte *ramp = malloc(300*3);
    for (int i = 0; i < 300; i++) { ramp[3*i] = (GLubyte)(i & 0xFF); ramp[3*i + 1] = (GLubyte)(i >> 8); ramp[3*i + 2] = 7; }
    glRasterPos2i(10, 30);
    glDrawPixels(300, 1, GL_RGB, GL_UNSIGNED_BYTE, ramp);
    free(ramp);
    CHECK(pixelNear(10, 30, 0, 0, 7, 255) && pixelNear(265, 30, 255, 0, 7, 255) && pixelNear(266, 30, 0, 1, 7, 255));
    CHECK(pixelNear(309, 30, 43, 1, 7, 255) && pixelNear(310, 30, 0, 0, 0, 0));

    // Partly off screen
    glRasterPos2i(398, 20);
    glDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, image);
    CHECK(pixelNear(399, 21, 255, 255, 255, 128));

    // Fragment operations: blending, depth test with the raster depth (cleared to 0.5), scissor
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    glRasterPos2i(20, 20);
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image + 4);      // Green onto red
    glDisable(GL_BLEND);
    CHECK(pixelNear(20, 20, 255, 255, 0, 255));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glRasterPos3f(100.0f, 20.0f, -0.5f);       // Window depth 0.75: behind
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    glRasterPos3f(101.0f, 20.0f, 0.5f);        // 0.25: in front
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    glDisable(GL_DEPTH_TEST);
    CHECK(pixelNear(100, 20, 0, 0, 0, 0) && pixelNear(101, 20, 255, 0, 0, 255) && depthNear(101, 20, 0.25f));
    glEnable(GL_SCISSOR_TEST);
    glScissor(111, 0, 10, 240);
    glRasterPos2i(110, 20);
    glDrawPixels(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    glDisable(GL_SCISSOR_TEST);
    CHECK(pixelNear(110, 20, 0, 0, 0, 0) && pixelNear(111, 20, 0, 255, 0, 255));

    // Depth images: written with the depth test (color untouched), stencil images directly. Runs of 8 x 4 pixels per
    // value: Azahar's upscaled readbacks mix neighboring pixels
    GLfloat depths[64], depths2[64];
    GLubyte stencils[64];
    for (int i = 0; i < 64; i++)
    {
        depths[i] = (i % 16 < 8)? 0.25f : 0.75f;
        depths2[i] = 0.5f;
        stencils[i] = (i % 16 < 8)? 0x3C : 0xFF;
    }
    const GLubyte stencilBits[16] = { 0xFF, 0, 0, 0,  0xFF, 0, 0, 0,  0xFF, 0, 0, 0,  0xFF, 0, 0, 0 };   // Rows of 4 bytes
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glRasterPos2i(120, 20);
    glDrawPixels(16, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths);
    CHECK(depthNear(123, 21, 0.25f) && depthNear(131, 21, 0.75f) && depthNear(145, 21, 0.5f));
    glDepthFunc(GL_LESS);
    glDrawPixels(16, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths2);
    CHECK(depthNear(123, 21, 0.25f) && depthNear(131, 21, 0.5f) && pixelNear(123, 21, 0, 0, 0, 0));
    glDisable(GL_DEPTH_TEST);
    glDrawPixels(16, 4, GL_DEPTH_COMPONENT, GL_FLOAT, depths);      // Depth test off: depth not written
    CHECK(depthNear(131, 21, 0.5f));
    glStencilMask(0x0F);
    glDrawPixels(16, 4, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencils);
    glStencilMask(0xFF);
    CHECK(stencilAt(123, 21) == 0x0C && stencilAt(131, 21) == 0x0F && stencilAt(145, 21) == 0);
    glRasterPos2i(150, 20);
    glDrawPixels(16, 4, GL_STENCIL_INDEX, GL_BITMAP, stencilBits);
    CHECK(stencilAt(153, 21) == 1 && stencilAt(161, 21) == 0);
    CHECK(depthNear(153, 21, 0.5f));            // Stencil images leave depth

    // glCopyPixels: color (with zoom), depth, stencil
    glRasterPos2i(140, 40);
    glPixelZoom(2.0f, 1.0f);
    glCopyPixels(20, 20, 2, 2, GL_COLOR);
    glPixelZoom(1.0f, 1.0f);
    CHECK(pixelNear(141, 40, 255, 255, 0, 255) && pixelNear(142, 40, 0, 255, 0, 255) && pixelNear(143, 41, 255, 255, 255, 128));
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glRasterPos2i(170, 20);
    glCopyPixels(120, 20, 16, 4, GL_DEPTH);
    glDisable(GL_DEPTH_TEST);
    glCopyPixels(120, 20, 16, 4, GL_STENCIL);
    CHECK(depthNear(173, 21, 0.25f) && depthNear(181, 21, 0.5f) && stencilAt(173, 21) == 0x0C && stencilAt(181, 21) == 0x0F);

    // glBitmap: the raster color where bits are set, at floor(raster - origin); the raster position moves
    const GLubyte bits[2] = { 0xA5, 0x0F };     // Bottom row 10100101, top row 00001111
    glColor3ub(255, 0, 0);
    glRasterPos2f(150.5f, 60.5f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBitmap(8, 2, 0.5f, 0.0f, 10.0f, -1.0f, bits);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    CHECK(pixelNear(150, 60, 255, 0, 0, 255) && pixelNear(151, 60, 0, 0, 0, 0) && pixelNear(152, 60, 255, 0, 0, 255));
    CHECK(pixelNear(157, 60, 255, 0, 0, 255) && pixelNear(153, 61, 0, 0, 0, 0) && pixelNear(154, 61, 255, 0, 0, 255));
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    CHECK(near(f[0], 160.5) && near(f[1], 59.5));
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE);
    glColor4ub(0, 0, 255, 0);                   // Alpha 0 is written too
    glRasterPos2i(170, 60);
    glBitmap(8, 1, 0.0f, 0.0f, 0.0f, 0.0f, bits + 1);    // LSB first: 11110000
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    CHECK(pixelNear(170, 60, 0, 0, 255, 0) && pixelNear(173, 60, 0, 0, 255, 0) && pixelNear(174, 60, 0, 0, 0, 0));
    glEnable(GL_ALPHA_TEST);                    // On the raster alpha
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4ub(0, 255, 0, 100);
    glRasterPos2i(180, 60);
    glBitmap(8, 1, 0.0f, 0.0f, 0.0f, 0.0f, bits);
    glColor4ub(0, 255, 0, 200);
    glRasterPos2i(190, 60);
    glBitmap(8, 1, 0.0f, 0.0f, 0.0f, 0.0f, bits);
    glDisable(GL_ALPHA_TEST);
    CHECK(pixelNear(180, 60, 0, 0, 0, 0) && pixelNear(190, 60, 0, 255, 0, 200) && pixelNear(191, 60, 0, 0, 0, 0));
    glBitmap(0, 0, 0.0f, 0.0f, 5.0f, 0.0f, NULL);     // Only moves
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    CHECK(near(f[0], 195.0));
    glRasterPos2i(-1, 0);                       // Invalid: nothing drawn, no move
    glBitmap(8, 1, 0.0f, 0.0f, 5.0f, 0.0f, bits);
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    CHECK(near(f[0], 195.0));

    // Attribute groups: raster position in GL_CURRENT_BIT, zoom in GL_PIXEL_MODE_BIT
    glRasterPos2i(1, 2);
    glPixelZoom(3.0f, 4.0f);
    glPushAttrib(GL_CURRENT_BIT | GL_PIXEL_MODE_BIT);
    glRasterPos2i(5, 6);
    glPixelZoom(1.0f, 1.0f);
    glPopAttrib();
    glGetFloatv(GL_CURRENT_RASTER_POSITION, f);
    glGetFloatv(GL_ZOOM_Y, f + 2);
    CHECK(near(f[0], 1.0) && near(f[1], 2.0) && f[2] == 4.0f);
    glPixelZoom(1.0f, 1.0f);

    // Display lists: images are copied with the unpack state of glNewList time
    GLuint list = glGenLists(1);
    GLubyte listImage[6] = { 255, 0, 255,  0, 255, 0 }, listBits[1] = { 0xC0 };
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    glNewList(list, GL_COMPILE);
    glRasterPos2i(200, 20);
    glDrawPixels(1, 1, GL_RGB, GL_UNSIGNED_BYTE, listImage);         // Skip 1: green
    glPixelZoom(1.0f, 2.0f);
    glColor3ub(255, 255, 0);
    glRasterPos2i(210, 20);
    glBitmap(3, 1, 0.0f, 0.0f, 1.0f, 0.0f, listBits);   // Skip 1: bits 1, 0, 0 -> one pixel set
    glCopyPixels(200, 20, 1, 1, GL_COLOR);      // At (211, 20) after the bitmap's move, 1 x 2
    glEndList();
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    memset(listImage, 0, sizeof(listImage));
    listBits[0] = 0;
    glPixelZoom(1.0f, 1.0f);
    glCallList(list);
    glDeleteLists(list, 1);
    glGetFloatv(GL_ZOOM_Y, f);
    CHECK(f[0] == 2.0f);
    glPixelZoom(1.0f, 1.0f);
    CHECK(pixelNear(200, 20, 0, 255, 0, 255) && pixelNear(210, 20, 255, 255, 0, 255) && pixelNear(210, 21, 0, 0, 0, 0));
    CHECK(pixelNear(211, 20, 0, 255, 0, 255) && pixelNear(211, 21, 0, 255, 0, 255) && pixelNear(212, 20, 0, 0, 0, 0));

    // Errors
    glDrawPixels(-1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glDrawPixels(1, 1, GL_RGBA, GL_BITMAP, image);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glDrawPixels(1, 1, GL_DEPTH_COMPONENT, GL_BITMAP, image);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_SHORT_5_6_5, image);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glDrawPixels(1, 1, 0x1234, GL_UNSIGNED_BYTE, image);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glCopyPixels(0, 0, 1, 1, GL_RGBA);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glCopyPixels(0, 0, -1, 1, GL_COLOR);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glBitmap(-1, 1, 0.0f, 0.0f, 0.0f, 0.0f, bits);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glBegin(GL_POINTS);
    glRasterPos2i(0, 0);
    glEnd();
    CHECK(glGetError() == GL_INVALID_OPERATION);

    glPopAttrib();
    glGetFloatv(GL_CURRENT_RASTER_COLOR, f);
    CHECK(f[0] == 1.0f && f[3] == 1.0f && glGetError() == GL_NO_ERROR);
}

// glPixelTransfer and glPixelMap: state, queries, errors, attribute group and display lists, and their effect on
// glDrawPixels (color, color index, depth, stencil), glReadPixels, glCopyPixels and texture images (not glGetTexImage).
// Images are drawn zoomed to 8 x 4 per pixel and checked in the middle: Azahar's upscaled readbacks mix neighbors
static void testPixelTransfer(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLfloat f[4], mf[4];
    GLint v[4];
    GLuint mui[4];
    GLushort mus[4];

    // Defaults
    glGetIntegerv(GL_MAP_COLOR, v);
    glGetIntegerv(GL_MAP_STENCIL, v + 1);
    glGetIntegerv(GL_INDEX_SHIFT, v + 2);
    glGetIntegerv(GL_INDEX_OFFSET, v + 3);
    CHECK(v[0] == 0 && v[1] == 0 && v[2] == 0 && v[3] == 0);
    glGetFloatv(GL_RED_SCALE, f);
    glGetFloatv(GL_ALPHA_BIAS, f + 1);
    glGetFloatv(GL_DEPTH_SCALE, f + 2);
    CHECK(f[0] == 1.0f && f[1] == 0.0f && f[2] == 1.0f);
    glGetIntegerv(GL_MAX_PIXEL_MAP_TABLE, v);
    glGetIntegerv(GL_PIXEL_MAP_I_TO_I_SIZE, v + 1);
    glGetIntegerv(GL_PIXEL_MAP_A_TO_A_SIZE, v + 2);
    CHECK(v[0] == 256 && v[1] == 1 && v[2] == 1);
    mf[0] = -1.0f;
    glGetPixelMapfv(GL_PIXEL_MAP_R_TO_R, mf);
    CHECK(mf[0] == 0.0f);

    // Setting and querying, errors
    glPixelTransferf(GL_GREEN_BIAS, 0.25f);
    glPixelTransferi(GL_INDEX_SHIFT, -2);
    glPixelTransferf(GL_INDEX_OFFSET, 3.0f);
    glPixelTransferi(GL_MAP_STENCIL, GL_TRUE);
    glGetFloatv(GL_GREEN_BIAS, f);
    glGetIntegerv(GL_INDEX_SHIFT, v);
    glGetIntegerv(GL_INDEX_OFFSET, v + 1);
    glGetIntegerv(GL_MAP_STENCIL, v + 2);
    CHECK(f[0] == 0.25f && v[0] == -2 && v[1] == 3 && v[2] == 1);
    glPixelTransferf(GL_ZOOM_X, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glPixelTransferf(GL_RED_SCALE, 2.0f);
    glEnd();
    CHECK(glGetError() == GL_INVALID_OPERATION);

    // Pixel maps: unsigned integers normalized for the color tables, as they are for the index tables; colors clamped
    const GLuint ui[2] = { 0xFFFFFFFF, 0 };
    const GLushort us[4] = { 7, 1, 2, 65535 };
    const GLfloat fl[3] = { 2.0f, 0.5f, -1.0f };
    glPixelMapuiv(GL_PIXEL_MAP_I_TO_R, 2, ui);
    glPixelMapusv(GL_PIXEL_MAP_S_TO_S, 4, us);
    glPixelMapfv(GL_PIXEL_MAP_G_TO_G, 3, fl);         // Color-to-color tables can have any size
    glGetIntegerv(GL_PIXEL_MAP_I_TO_R_SIZE, v);
    glGetIntegerv(GL_PIXEL_MAP_S_TO_S_SIZE, v + 1);
    glGetIntegerv(GL_PIXEL_MAP_G_TO_G_SIZE, v + 2);
    CHECK(v[0] == 2 && v[1] == 4 && v[2] == 3 && glGetError() == GL_NO_ERROR);
    glGetPixelMapfv(GL_PIXEL_MAP_I_TO_R, mf);
    CHECK(mf[0] == 1.0f && mf[1] == 0.0f);
    glGetPixelMapfv(GL_PIXEL_MAP_S_TO_S, mf);
    CHECK(mf[0] == 7.0f && mf[3] == 65535.0f);
    glGetPixelMapfv(GL_PIXEL_MAP_G_TO_G, mf);
    CHECK(mf[0] == 1.0f && mf[1] == 0.5f && mf[2] == 0.0f);
    glGetPixelMapuiv(GL_PIXEL_MAP_G_TO_G, mui);
    glGetPixelMapusv(GL_PIXEL_MAP_G_TO_G, mus);
    CHECK(mui[0] == 0xFFFFFFFF && mui[2] == 0 && mus[1] == 32768);
    glGetPixelMapuiv(GL_PIXEL_MAP_S_TO_S, mui);
    glGetPixelMapusv(GL_PIXEL_MAP_S_TO_S, mus);
    CHECK(mui[0] == 7 && mui[2] == 2 && mus[3] == 65535);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 3, fl);         // Index tables: 2^n entries
    CHECK(glGetError() == GL_INVALID_VALUE);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 0, fl);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 257, fl);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glPixelMapfv(GL_PIXEL_MAP_A_TO_A + 1, 1, fl);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetPixelMapfv(GL_PIXEL_MAP_I_TO_R_SIZE, mf);
    CHECK(glGetError() == GL_INVALID_ENUM);

    // Attribute groups: the transfer state is in GL_PIXEL_MODE_BIT, the maps in none
    glPushAttrib(GL_PIXEL_MODE_BIT);
    glPixelTransferf(GL_GREEN_BIAS, 0.5f);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 1, fl + 1);
    glPopAttrib();
    glGetFloatv(GL_GREEN_BIAS, f);
    glGetIntegerv(GL_PIXEL_MAP_I_TO_R_SIZE, v);
    CHECK(f[0] == 0.25f && v[0] == 1);
    glPopAttrib();                              // The transfer back to the defaults
    glPushAttrib(GL_ALL_ATTRIB_BITS);

    windowProjection();
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClearDepth(0.5);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glPixelZoom(8.0f, 4.0f);

    // Color images: scale and bias on the unclamped components (float 2.0 scaled by 0.25), then the color tables
    const GLfloat red2[4] = { 2.0f, 0.0f, 0.0f, 1.0f };
    const GLubyte rgba[8] = { 255, 0, 0, 255,  0, 255, 0, 255 };
    const GLfloat invert[2] = { 1.0f, 0.0f };
    glPixelTransferf(GL_RED_SCALE, 0.25f);
    glPixelTransferf(GL_GREEN_BIAS, 0.25f);
    glRasterPos2i(10, 10);
    glDrawPixels(1, 1, GL_RGBA, GL_FLOAT, red2);
    glPixelTransferf(GL_RED_SCALE, 1.0f);
    glPixelTransferf(GL_GREEN_BIAS, 0.0f);
    glPixelMapfv(GL_PIXEL_MAP_R_TO_R, 2, invert);
    glPixelMapfv(GL_PIXEL_MAP_G_TO_G, 2, invert);
    glPixelTransferi(GL_MAP_COLOR, GL_TRUE);    // B_TO_B and A_TO_A are still { 0 }
    glRasterPos2i(20, 10);
    glDrawPixels(2, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
    CHECK(pixelNear(14, 12, 128, 64, 0, 255) && pixelNear(24, 12, 0, 255, 0, 0) && pixelNear(32, 12, 255, 0, 0, 0));

    // glReadPixels: transferred before the conversion (luminance = R + G + B afterwards)
    GLfloat rf[4] = { -1.0f, -1.0f, -1.0f, -1.0f };
    GLubyte lum = 0;
    glPixelTransferf(GL_RED_SCALE, 0.5f);
    glPixelTransferf(GL_BLUE_BIAS, 1.0f);
    glReadPixels(32, 12, 1, 1, GL_RGBA, GL_FLOAT, rf);
    glReadPixels(32, 12, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, &lum);
    glPixelTransferf(GL_RED_SCALE, 1.0f);
    glPixelTransferf(GL_BLUE_BIAS, 0.0f);
    CHECK(near(rf[0], 0.5) && rf[1] == 0.0f && rf[2] == 1.0f && rf[3] == 0.0f && lum == 255);

    // Color index images: shifted, offset and looked up in the I_TO_* tables (wrapping around), not scaled or biased
    const GLfloat iR[4] = { 1, 0, 0, 1 }, iG[4] = { 0, 1, 0, 1 }, iB[4] = { 0, 0, 1, 1 }, iA[4] = { 1, 1, 1, 1 };
    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 4, iR);   // 0 red, 1 green, 2 blue, 3 white
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 4, iG);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 4, iB);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 4, iA);
    const GLubyte indices[4] = { 0, 2, 4, 7 };
    glPixelTransferi(GL_INDEX_SHIFT, -1);       // 0, 1, 2, 3
    glPixelTransferi(GL_INDEX_OFFSET, 1);       // 1, 2, 3, 0
    glPixelTransferf(GL_RED_SCALE, 0.0f);
    glRasterPos2i(10, 20);
    glDrawPixels(4, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, indices);
    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    const GLubyte indexBits[1] = { 0x40 };      // 0, 1
    glRasterPos2i(50, 20);
    glDrawPixels(2, 1, GL_COLOR_INDEX, GL_BITMAP, indexBits);
    glPixelTransferf(GL_RED_SCALE, 1.0f);
    CHECK(pixelNear(14, 22, 0, 255, 0, 255) && pixelNear(22, 22, 0, 0, 255, 255) && pixelNear(30, 22, 255, 255, 255, 255));
    CHECK(pixelNear(38, 22, 255, 0, 0, 255) && pixelNear(54, 22, 255, 0, 0, 255) && pixelNear(62, 22, 0, 255, 0, 255));

    // Depth images: scale and bias, also when read
    const GLfloat depth[1] = { 0.5f };
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_ALWAYS);
    glPixelTransferf(GL_DEPTH_SCALE, 0.5f);
    glPixelTransferf(GL_DEPTH_BIAS, 0.5f);
    glRasterPos2i(10, 30);
    glDrawPixels(1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, depth);
    glPixelTransferf(GL_DEPTH_SCALE, 1.0f);
    glPixelTransferf(GL_DEPTH_BIAS, 0.0f);
    CHECK(depthNear(14, 32, 0.75f));
    glPixelTransferf(GL_DEPTH_BIAS, -0.25f);
    CHECK(depthNear(100, 100, 0.25f));          // Cleared to 0.5
    glPixelTransferf(GL_DEPTH_BIAS, 0.0f);

    // Stencil images: shift and offset, the S_TO_S table with GL_MAP_STENCIL; when read too
    const GLubyte stencil[1] = { 3 };
    const GLfloat stencilMap[2] = { 5.0f, 9.0f };
    glPixelTransferi(GL_INDEX_SHIFT, 1);
    glPixelTransferi(GL_INDEX_OFFSET, 1);
    glRasterPos2i(10, 40);
    glDrawPixels(1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil);    // 7
    glPixelTransferi(GL_INDEX_SHIFT, 0);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glPixelMapfv(GL_PIXEL_MAP_S_TO_S, 2, stencilMap);
    glPixelTransferi(GL_MAP_STENCIL, GL_TRUE);
    glRasterPos2i(20, 40);
    glDrawPixels(1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil);    // 3 & 1 -> 9
    glPixelTransferi(GL_MAP_STENCIL, GL_FALSE);
    CHECK(stencilAt(14, 42) == 7 && stencilAt(24, 42) == 9);
    glPixelTransferi(GL_INDEX_OFFSET, 100);
    CHECK(stencilAt(24, 42) == 109);
    glPixelTransferi(GL_INDEX_OFFSET, 0);

    // glCopyPixels: transferred once (green 64 biased by 0.25 is 128, not 192); depth and stencil too
    glPixelZoom(1.0f, 1.0f);
    glPixelTransferf(GL_GREEN_BIAS, 0.25f);
    glRasterPos2i(100, 10);
    glCopyPixels(10, 10, 8, 4, GL_COLOR);
    glPixelTransferf(GL_GREEN_BIAS, 0.0f);
    CHECK(pixelNear(104, 12, 128, 128, 0, 255));
    glPixelTransferf(GL_DEPTH_BIAS, 0.125f);
    glPixelTransferi(GL_INDEX_OFFSET, 1);
    glRasterPos2i(110, 30);
    glCopyPixels(10, 30, 8, 4, GL_DEPTH);
    glRasterPos2i(110, 40);
    glCopyPixels(10, 40, 8, 4, GL_STENCIL);
    glPixelTransferf(GL_DEPTH_BIAS, 0.0f);
    glPixelTransferi(GL_INDEX_OFFSET, 0);
    glDisable(GL_DEPTH_TEST);
    CHECK(depthNear(114, 32, 0.875f) && stencilAt(114, 42) == 8);

    // Textures: loaded and copied with the transfer (copies once), glGetTexImage without it
    GLuint tex;
    GLubyte texels[8*8*4], texIndices[64];
    for (int i = 0; i < 64; i++) texIndices[i] = (GLubyte)(i & 3);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, texIndices);
    glPixelTransferf(GL_RED_SCALE, 0.0f);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    CHECK(texels[0] == 255 && texels[1] == 0 && texels[5] == 255 && texels[10] == 255 && texels[12] == 255 && texels[15] == 255);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);    // Red scaled to 0
    glPixelTransferf(GL_RED_SCALE, 1.0f);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    CHECK(texels[0] == 0 && texels[3] == 255 && texels[5] == 255);
    glPixelTransferf(GL_GREEN_BIAS, 0.25f);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 12, 12, 1, 1);     // (128, 64, 0)
    glPixelTransferf(GL_GREEN_BIAS, 0.0f);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    CHECK(abs(texels[0] - 128) <= 2 && abs(texels[1] - 128) <= 2);
    const GLubyte texBits[8] = { 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40 };     // Columns 0, 1: index 0, 1
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_COLOR_INDEX, GL_BITMAP, texBits);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    CHECK(texels[0] == 255 && texels[1] == 0 && texels[4] == 0 && texels[5] == 255 && texels[8] == 255);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, texels);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_BITMAP, texBits);
    CHECK(glGetError() == GL_INVALID_ENUM);

    // Display lists: glPixelTransfer and glPixelMap are recorded (the values copied), bitmap texture images too
    GLfloat listMap[2] = { 0.0f, 1.0f };
    GLubyte listBits[8] = { 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80 };      // Column 0: index 1
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glPixelTransferf(GL_BLUE_SCALE, 0.5f);
    glPixelMapfv(GL_PIXEL_MAP_B_TO_B, 2, listMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_COLOR_INDEX, GL_BITMAP, listBits);
    glEndList();
    listMap[1] = 0.5f;
    memset(listBits, 0, sizeof(listBits));
    glGetFloatv(GL_BLUE_SCALE, f);
    CHECK(f[0] == 1.0f);
    glCallList(list);
    glDeleteLists(list, 1);
    glGetFloatv(GL_BLUE_SCALE, f);
    glGetPixelMapfv(GL_PIXEL_MAP_B_TO_B, mf);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    CHECK(f[0] == 0.5f && mf[1] == 1.0f && texels[0] == 0 && texels[1] == 255 && texels[4] == 255 && texels[5] == 0);
    glDeleteTextures(1, &tex);

    glPopAttrib();
    glGetFloatv(GL_BLUE_SCALE, f);
    CHECK(f[0] == 1.0f && glGetError() == GL_NO_ERROR);
}

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

// Texture 0 of each target is a texture of its own (GL 1.0 style code without glBindTexture)
static void testDefaultTextures(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    GLint v = -1;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &v);
    CHECK(v == 0);
    CHECK(glIsTexture(0) == GL_FALSE);

    // Defaults of the unloaded texture, then a 2x2 image: red, green / blue, white
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &v);
    CHECK(v == GL_NEAREST_MIPMAP_LINEAR);
    static const GLubyte rgba[16] = { 255, 0, 0, 255,  0, 255, 0, 255,  0, 0, 255, 255,  255, 255, 255, 255 };
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 2);

    // The 1D default is separate from the 2D one
    static const GLubyte lum[4] = { 10, 20, 30, 40 };
    glTexImage1D(GL_TEXTURE_1D, 0, GL_LUMINANCE, 4, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, lum);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 4);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 2);
    glGetTexParameteriv(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, &v);
    CHECK(v == GL_NEAREST_MIPMAP_LINEAR);

    // Deleting a bound texture binds texture 0 again, which keeps its image and parameters
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 0);
    glDeleteTextures(1, &tex);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &v);
    CHECK(v == 0);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 2);
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &v);
    CHECK(v == GL_NEAREST);

    // GL_TEXTURE_BIT saves and restores its parameters
    glPushAttrib(GL_TEXTURE_BIT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glPopAttrib();
    glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, &v);
    CHECK(v == GL_REPEAT);

    // Drawn with it: one 4x4 block per texel
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2i(300, 100);
    glTexCoord2f(1.0f, 0.0f); glVertex2i(308, 100);
    glTexCoord2f(1.0f, 1.0f); glVertex2i(308, 108);
    glTexCoord2f(0.0f, 1.0f); glVertex2i(300, 108);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    GLubyte p[16];
    glReadPixels(301, 101, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    glReadPixels(306, 101, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p + 4);
    glReadPixels(301, 106, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p + 8);
    glReadPixels(306, 106, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p + 12);
    CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0);
    CHECK(p[4] == 0 && p[5] == 255 && p[6] == 0);
    CHECK(p[8] == 0 && p[9] == 0 && p[10] == 255);
    CHECK(p[12] == 255 && p[13] == 255 && p[14] == 255);

    // Copies into it
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 300, 100, 8, 8, 0);
    CHECK(glGetError() == GL_NO_ERROR);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &v);
    CHECK(v == 8);

    glPopAttrib();
}

// Feedback (tokens in window coordinates, clipped, culled, polygon modes, raster tokens, overflow) and selection (hit
// records, name stack); nothing is drawn meanwhile
static bool nearf(float a, float b) { return fabsf(a - b) < 1e-3f; }

// Values of a feedback vertex in window coordinates
static bool fbVertex(const GLfloat *f, float x, float y, float z)
{
    return nearf(f[0], x) && nearf(f[1], y) && nearf(f[2], z);
}

static bool hitDepth(GLuint z, double expected) { return fabs(z/4294967295.0 - expected) < 1e-4; }

static void testFeedback(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    windowProjection();

    GLint v = -1;
    glGetIntegerv(GL_RENDER_MODE, &v);
    CHECK(v == GL_RENDER);
    glGetIntegerv(GL_FEEDBACK_BUFFER_TYPE, &v);
    CHECK(v == GL_2D);
    glGetIntegerv(GL_MAX_NAME_STACK_DEPTH, &v);
    CHECK(v >= 64);

    // Errors: no buffer yet, bad arguments
    CHECK(glRenderMode(GL_FEEDBACK) == 0 && glGetError() == GL_INVALID_OPERATION);
    CHECK(glRenderMode(GL_SELECT) == 0 && glGetError() == GL_INVALID_OPERATION);
    CHECK(glRenderMode(0x1234) == 0 && glGetError() == GL_INVALID_ENUM);
    static GLfloat fb[256];
    glFeedbackBuffer(-1, GL_3D, fb);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glFeedbackBuffer(256, 0x1234, fb);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glPopName();                                // Ignored outside selection mode
    glLoadName(1);
    CHECK(glGetError() == GL_NO_ERROR);

    // GL_3D: a point (and one outside), a pass-through, a line clipped at the window edge, a triangle; z = 0 is window
    // depth 0.5
    glFeedbackBuffer(256, GL_3D, fb);
    void *ptr = NULL;
    glGetPointerv(GL_FEEDBACK_BUFFER_POINTER, &ptr);
    glGetIntegerv(GL_FEEDBACK_BUFFER_SIZE, &v);
    CHECK(ptr == fb && v == 256);
    CHECK(glRenderMode(GL_FEEDBACK) == 0);
    glGetIntegerv(GL_RENDER_MODE, &v);
    CHECK(v == GL_FEEDBACK);
    glFeedbackBuffer(256, GL_3D, fb);
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glBegin(GL_POINTS);
    glVertex2f(10.0f, 20.0f);
    glVertex2f(-5.0f, 20.0f);
    glEnd();
    glPassThrough(7.0f);
    glBegin(GL_LINES);
    glVertex2f(100.0f, 50.0f);
    glVertex2f(500.0f, 50.0f);
    glEnd();
    glBegin(GL_TRIANGLES);
    glVertex2f(100.0f, 100.0f);
    glVertex2f(200.0f, 100.0f);
    glVertex2f(150.0f, 200.0f);
    glEnd();
    CHECK(glRenderMode(GL_RENDER) == 24);
    CHECK(fb[0] == GL_POINT_TOKEN && fbVertex(&fb[1], 10, 20, 0.5f));
    CHECK(fb[4] == GL_PASS_THROUGH_TOKEN && fb[5] == 7.0f);
    CHECK(fb[6] == GL_LINE_RESET_TOKEN && fbVertex(&fb[7], 100, 50, 0.5f) && fbVertex(&fb[10], 400, 50, 0.5f));
    CHECK(fb[13] == GL_POLYGON_TOKEN && fb[14] == 3 && fbVertex(&fb[15], 100, 100, 0.5f) &&
          fbVertex(&fb[18], 200, 100, 0.5f) && fbVertex(&fb[21], 150, 200, 0.5f));

    // GL_2D: line strips and loops reset the stipple once, separate lines each time; a polygon stays one; a quad
    // clipped at x = 0; a culled triangle; outlines and vertices of polygons (an edge flag off)
    glFeedbackBuffer(256, GL_2D, fb);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_LINE_LOOP);
    glVertex2i(10, 10); glVertex2i(20, 10); glVertex2i(20, 20);
    glEnd();
    glBegin(GL_LINES);
    glVertex2i(10, 10); glVertex2i(20, 10); glVertex2i(30, 10); glVertex2i(40, 10);
    glEnd();
    int n = glRenderMode(GL_RENDER);
    CHECK(n == 5*5);
    CHECK(fb[0] == GL_LINE_RESET_TOKEN && fb[5] == GL_LINE_TOKEN && fb[10] == GL_LINE_TOKEN);
    CHECK(nearf(fb[11], 20) && nearf(fb[12], 20) && nearf(fb[13], 10) && nearf(fb[14], 10));    // Closing segment
    CHECK(fb[15] == GL_LINE_RESET_TOKEN && fb[20] == GL_LINE_RESET_TOKEN && nearf(fb[21], 30));

    glRenderMode(GL_FEEDBACK);
    glBegin(GL_POLYGON);
    glVertex2i(10, 10); glVertex2i(30, 10); glVertex2i(40, 20); glVertex2i(30, 30); glVertex2i(10, 30);
    glEnd();
    glRecti(-100, 50, 100, 60);
    glEnable(GL_CULL_FACE);
    glBegin(GL_TRIANGLES);
    glVertex2i(10, 10); glVertex2i(10, 20); glVertex2i(20, 10);     // Clockwise: back facing
    glEnd();
    glDisable(GL_CULL_FACE);
    n = glRenderMode(GL_RENDER);
    CHECK(n == 12 + 10);
    CHECK(fb[0] == GL_POLYGON_TOKEN && fb[1] == 5 && nearf(fb[6], 40));
    CHECK(fb[12] == GL_POLYGON_TOKEN && fb[13] == 4 && nearf(fb[14], 0) && nearf(fb[16], 100) && nearf(fb[20], 0));

    glRenderMode(GL_FEEDBACK);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glBegin(GL_TRIANGLES);
    glVertex2i(10, 10);
    glEdgeFlag(GL_FALSE);
    glVertex2i(20, 10);
    glEdgeFlag(GL_TRUE);
    glVertex2i(20, 20);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glBegin(GL_TRIANGLES);
    glVertex2i(10, 10); glVertex2i(20, 10); glVertex2i(20, 20);
    glEnd();
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    n = glRenderMode(GL_RENDER);
    CHECK(n == 2*5 + 3*3);
    CHECK(fb[0] == GL_LINE_RESET_TOKEN && fb[5] == GL_LINE_TOKEN && nearf(fb[6], 20) && nearf(fb[8], 10));
    CHECK(fb[10] == GL_POINT_TOKEN && fb[13] == GL_POINT_TOKEN && fb[16] == GL_POINT_TOKEN && nearf(fb[17], 20));

    // User clip planes come first: x >= 50
    static const GLdouble plane[4] = { 1.0, 0.0, 0.0, -50.0 };
    glClipPlane(GL_CLIP_PLANE0, plane);
    glEnable(GL_CLIP_PLANE0);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_LINES);
    glVertex2i(0, 10); glVertex2i(100, 10);
    glEnd();
    glDisable(GL_CLIP_PLANE0);
    CHECK(glRenderMode(GL_RENDER) == 5 && nearf(fb[1], 50) && nearf(fb[3], 100));

    // Depth clipping: identity projection, a triangle reaching z = 2 is cut at the far plane (window depth 1)
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glFeedbackBuffer(256, GL_3D, fb);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.5f, -0.5f, 0.0f); glVertex3f(0.5f, -0.5f, 0.0f); glVertex3f(0.0f, 0.5f, 2.0f);
    glEnd();
    CHECK(glRenderMode(GL_RENDER) == 2 + 4*3);
    CHECK(fb[1] == 4 && nearf(fb[4], 0.5f) && nearf(fb[10], 1.0f) && nearf(fb[13], 1.0f));
    windowProjection();

    // GL_3D_COLOR: 8-bit colors, flat shading takes the last vertex of a line
    glFeedbackBuffer(256, GL_3D_COLOR, fb);
    glRenderMode(GL_FEEDBACK);
    glShadeModel(GL_FLAT);
    glBegin(GL_LINES);
    glColor4f(1.0f, 0.0f, 0.0f, 1.0f); glVertex2i(10, 10);
    glColor4f(0.0f, 0.5f, 1.0f, 0.25f); glVertex2i(20, 10);
    glEnd();
    glShadeModel(GL_SMOOTH);
    CHECK(glRenderMode(GL_RENDER) == 1 + 2*7);
    CHECK(nearf(fb[4], 0.0f) && fabsf(fb[5] - 0.5f) < 0.01f && nearf(fb[6], 1.0f) && fabsf(fb[7] - 0.25f) < 0.01f);
    CHECK(nearf(fb[11], 0.0f) && nearf(fb[13], 1.0f));

    // GL_4D_COLOR_TEXTURE: w and texcoords through the texture matrix, also r from a texcoord array
    glFeedbackBuffer(256, GL_4D_COLOR_TEXTURE, fb);
    glMatrixMode(GL_TEXTURE);
    glTranslatef(0.5f, 0.0f, 0.0f);
    glMatrixMode(GL_MODELVIEW);
    glRenderMode(GL_FEEDBACK);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glTexCoord4f(0.25f, 0.5f, 0.75f, 1.0f);
    glBegin(GL_POINTS);
    glVertex2i(10, 10);
    glEnd();
    static const GLfloat arrayPos[2] = { 30.0f, 40.0f }, arrayTex[3] = { 0.0f, 0.125f, 0.375f };
    glVertexPointer(2, GL_FLOAT, 0, arrayPos);
    glTexCoordPointer(3, GL_FLOAT, 0, arrayTex);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glDrawArrays(GL_POINTS, 0, 1);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    CHECK(glRenderMode(GL_RENDER) == 2*13);
    CHECK(fbVertex(&fb[1], 10, 10, 0.5f) && nearf(fb[4], 1.0f) && nearf(fb[8], 1.0f));
    CHECK(nearf(fb[9], 0.75f) && nearf(fb[10], 0.5f) && nearf(fb[11], 0.75f) && nearf(fb[12], 1.0f));
    CHECK(nearf(fb[14], 30.0f) && nearf(fb[22], 0.5f) && nearf(fb[23], 0.125f) && nearf(fb[24], 0.375f));

    // Raster tokens: the raster position (glBitmap still moves it); nothing for an invalid one
    static const GLubyte pixel[4] = { 0, 0, 255, 255 };
    glFeedbackBuffer(256, GL_3D, fb);
    glRenderMode(GL_FEEDBACK);
    glRasterPos2i(30, 40);
    glBitmap(0, 0, 0.0f, 0.0f, 5.0f, 0.0f, NULL);
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glCopyPixels(0, 0, 1, 1, GL_COLOR);
    glRasterPos2i(-10, 0);
    glBitmap(0, 0, 0.0f, 0.0f, 5.0f, 0.0f, NULL);
    CHECK(glRenderMode(GL_RENDER) == 3*4);
    CHECK(fb[0] == GL_BITMAP_TOKEN && fbVertex(&fb[1], 30, 40, 0.5f));
    CHECK(fb[4] == GL_DRAW_PIXEL_TOKEN && fbVertex(&fb[5], 35, 40, 0.5f));
    CHECK(fb[8] == GL_COPY_PIXEL_TOKEN && nearf(fb[9], 35));

    // Overflow: -1, the buffer is filled as far as it goes
    fb[3] = -2.0f;
    glFeedbackBuffer(3, GL_3D, fb);
    glRenderMode(GL_FEEDBACK);
    glBegin(GL_POINTS);
    glVertex2i(10, 10);
    glEnd();
    CHECK(glRenderMode(GL_RENDER) == -1 && fb[0] == GL_POINT_TOKEN && nearf(fb[2], 10) && fb[3] == -2.0f);

    // Nothing is drawn or cleared
    glColor3f(0.0f, 0.0f, 1.0f);
    glRecti(380, 220, 390, 230);
    glFeedbackBuffer(256, GL_2D, fb);
    glRenderMode(GL_FEEDBACK);
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);
    glRecti(380, 220, 390, 230);
    glRasterPos2i(380, 220);
    glDrawPixels(1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glRenderMode(GL_RENDER);
    CHECK(pixelNear(385, 225, 0, 0, 255, 255));

    // Display lists record glPassThrough and the name stack commands
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glPassThrough(9.0f);
    glInitNames();
    glPushName(5);
    glRecti(10, 10, 20, 20);
    glEndList();
    glRenderMode(GL_FEEDBACK);
    glCallList(list);
    CHECK(glRenderMode(GL_RENDER) == 2 + 2 + 4*2 && fb[0] == GL_PASS_THROUGH_TOKEN && fb[1] == 9.0f);

    // Selection: a hit record per name stack change after a hit, with the window depth range
    static GLuint sel[64];
    glSelectBuffer(64, sel);
    glGetPointerv(GL_SELECTION_BUFFER_POINTER, &ptr);
    glGetIntegerv(GL_SELECTION_BUFFER_SIZE, &v);
    CHECK(ptr == sel && v == 64);
    CHECK(glRenderMode(GL_SELECT) == 0);
    glLoadName(1);
    CHECK(glGetError() == GL_INVALID_OPERATION);        // Empty stack
    glPopName();
    CHECK(glGetError() == GL_STACK_UNDERFLOW);
    glInitNames();
    glPushName(1);
    glRecti(10, 10, 20, 20);                            // Hit at depth 0.5
    glLoadName(2);
    glBegin(GL_POINTS);
    glVertex2i(-5, 10);                                 // Outside: no hit
    glEnd();
    glLoadName(3);
    glPushName(4);
    glBegin(GL_LINES);
    glVertex3f(10.0f, 10.0f, 0.5f);                     // Window depth 0.25 .. 0.75
    glVertex3f(20.0f, 10.0f, -0.5f);
    glEnd();
    glGetIntegerv(GL_NAME_STACK_DEPTH, &v);
    CHECK(v == 2);
    glLoadName(5);
    glEnable(GL_CULL_FACE);
    glRecti(20, 10, 10, 20);                            // Clockwise: culled, no hit
    glDisable(GL_CULL_FACE);
    glLoadName(6);
    glRasterPos3f(10.0f, 10.0f, -0.8f);                 // A valid raster position hits (depth 0.9)
    glPopName();
    glPopName();
    glCallList(list);                                   // glInitNames, name 5, a hit
    CHECK(glRenderMode(GL_RENDER) == 4);
    CHECK(sel[0] == 1 && hitDepth(sel[1], 0.5) && hitDepth(sel[2], 0.5) && sel[3] == 1);
    CHECK(sel[4] == 2 && hitDepth(sel[5], 0.25) && hitDepth(sel[6], 0.75) && sel[7] == 3 && sel[8] == 4);
    CHECK(sel[9] == 2 && hitDepth(sel[10], 0.9) && hitDepth(sel[11], 0.9) && sel[12] == 3 && sel[13] == 6);
    CHECK(sel[14] == 1 && hitDepth(sel[15], 0.5) && sel[17] == 5);
    glGetIntegerv(GL_NAME_STACK_DEPTH, &v);
    CHECK(v == 0);

    // Name stack overflow, selection buffer overflow
    glSelectBuffer(2, sel);
    glRenderMode(GL_SELECT);
    for (int i = 0; i < 64; i++) glPushName(i);
    CHECK(glGetError() == GL_NO_ERROR);
    glPushName(64);
    CHECK(glGetError() == GL_STACK_OVERFLOW);
    glRecti(10, 10, 20, 20);
    CHECK(glRenderMode(GL_RENDER) == -1);
    glDeleteLists(list, 1);

    // glRenderMode between glBegin and glEnd
    glBegin(GL_POINTS);
    CHECK(glRenderMode(GL_SELECT) == 0);
    glEnd();
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glGetIntegerv(GL_RENDER_MODE, &v);
    CHECK(v == GL_RENDER);

    glPopAttrib();
}

// Pixel (x, y) is white (true) or black (false); prints it otherwise
static bool whiteAt(int x, int y, bool white)
{
    int c = white? 255 : 0;
    return pixelNear(x, y, c, c, c, -1);
}

// Line stipple and polygon stipple: defaults, clamping, queries, attribute groups, display lists, unpack/pack of the
// pattern, then rendered and read back: dashes along both axes and directions, the counter carried along strips and
// reset by separate lines and outlines, wide lines, and the window-aligned polygon pattern (orthographic, with
// perspective, textured, with the alpha test)
static void testStipple(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLint v = -1;
    GLubyte mask[8*33];
    CHECK(!glIsEnabled(GL_LINE_STIPPLE) && !glIsEnabled(GL_POLYGON_STIPPLE));
    glGetIntegerv(GL_LINE_STIPPLE_PATTERN, &v);
    CHECK(v == 0xFFFF);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &v);
    CHECK(v == 1);
    memset(mask, 0, sizeof(mask));
    glGetPolygonStipple(mask);
    bool ones = true;
    for (int i = 0; i < 128; i++) ones = ones && (mask[i] == 0xFF);
    CHECK(ones && (mask[128] == 0));

    glLineStipple(0, 0x1234);       // The factor is clamped to 1..256
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &v);
    CHECK(v == 1);
    glLineStipple(300, 0xF0F0);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &v);
    CHECK(v == 256);
    glGetIntegerv(GL_LINE_STIPPLE_PATTERN, &v);
    CHECK(v == 0xF0F0);
    glEnable(GL_LINE_STIPPLE);
    GLboolean b = GL_FALSE;
    glGetBooleanv(GL_LINE_STIPPLE, &b);
    CHECK(b == GL_TRUE);
    glDisable(GL_LINE_STIPPLE);
    CHECK(glGetError() == GL_NO_ERROR);

    // Attribute groups: GL_LINE_BIT (pattern, factor, enable), GL_POLYGON_BIT (enable), GL_POLYGON_STIPPLE_BIT (pattern),
    // GL_ENABLE_BIT (both enables)
    glLineStipple(3, 0x00FF);
    glPushAttrib(GL_LINE_BIT);
    glLineStipple(5, 0xAAAA);
    glEnable(GL_LINE_STIPPLE);
    glPopAttrib();
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &v);
    CHECK(v == 3);
    glGetIntegerv(GL_LINE_STIPPLE_PATTERN, &v);
    CHECK(v == 0x00FF);
    CHECK(!glIsEnabled(GL_LINE_STIPPLE));
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_LINE_STIPPLE);
    glEnable(GL_POLYGON_STIPPLE);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_LINE_STIPPLE) && !glIsEnabled(GL_POLYGON_STIPPLE));
    glPushAttrib(GL_POLYGON_BIT);
    glEnable(GL_POLYGON_STIPPLE);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_POLYGON_STIPPLE));

    GLubyte pattern[128];
    for (int i = 0; i < 128; i++) pattern[i] = (GLubyte)i;
    glPushAttrib(GL_POLYGON_STIPPLE_BIT);
    glPolygonStipple(pattern);
    glGetPolygonStipple(mask);
    CHECK(memcmp(mask, pattern, 128) == 0);
    glPopAttrib();
    glGetPolygonStipple(mask);
    CHECK((mask[0] == 0xFF) && (mask[127] == 0xFF));

    // Unpacked like a bitmap (LSB first, row length, skip pixels), packed with the pack state
    GLubyte wide[8*32];
    memset(wide, 0, sizeof(wide));
    for (int y = 0; y < 32; y++) wide[8*y + 1] = 0x01;      // LSB first, skip 4: bit 8 - 4 = x 4 of each row
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 64);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 4);
    glPolygonStipple(wide);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glGetPolygonStipple(mask);
    CHECK((mask[0] == 0x08) && (mask[1] == 0) && (mask[124] == 0x08));
    memset(mask, 0xEE, sizeof(mask));
    glPixelStorei(GL_PACK_ROW_LENGTH, 40);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 4);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    glGetPolygonStipple(mask);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    // Rows of 40 bits padded to 8 bytes (alignment 4), the first row skipped; x 4 lands on bit 8 (byte 1, MSB), the
    // bits before column 4 and after column 35 keep their 0xEE
    CHECK((mask[0] == 0xEE) && (mask[8] == 0xE0) && (mask[9] == 0x80) && (mask[12] == 0x0E) && (mask[8*32 + 1] == 0x80));

    // Display lists: recorded with the unpack state at compile time
    GLuint list = glGenLists(1);
    for (int i = 0; i < 128; i++) pattern[i] = (GLubyte)(0x80 >> (i & 7));
    glNewList(list, GL_COMPILE);
    glLineStipple(7, 0x1111);
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_TRUE);            // Not recorded, executed now
    glPolygonStipple(pattern);
    glEndList();
    glPixelStorei(GL_UNPACK_LSB_FIRST, GL_FALSE);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &v);
    CHECK(v == 3);
    glGetPolygonStipple(mask);
    CHECK(mask[0] == 0x08);
    glCallList(list);
    glGetIntegerv(GL_LINE_STIPPLE_REPEAT, &v);
    CHECK(v == 7);
    glGetPolygonStipple(mask);
    CHECK((mask[0] == 0x01) && (mask[1] == 0x02) && (mask[7] == 0x80));
    glDeleteLists(list, 1);

    // Rendered lines: white on black in window coordinates
    windowProjection();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0x00FF);
    glBegin(GL_LINES);
    glVertex2f(10.0f, 100.5f); glVertex2f(60.0f, 100.5f);       // Fragments x 10.. (s = x - 10)
    glVertex2f(200.5f, 20.0f); glVertex2f(200.5f, 60.0f);       // y 20..
    glVertex2f(60.0f, 140.5f); glVertex2f(10.0f, 140.5f);       // Right to left: x 59, 58, ...
    glVertex2f(10.0f, 130.5f); glVertex2f(14.0f, 130.5f);       // Separate lines: the second one starts again
    glVertex2f(14.0f, 130.5f); glVertex2f(30.0f, 130.5f);
    glEnd();
    glBegin(GL_LINE_STRIP);                                     // A strip carries the counter over
    glVertex2f(10.0f, 120.5f); glVertex2f(14.0f, 120.5f); glVertex2f(30.0f, 120.5f);
    glEnd();
    glLineStipple(3, 0x0001);
    glBegin(GL_LINES);
    glVertex2f(10.0f, 110.5f); glVertex2f(70.0f, 110.5f);       // On for s 0..2 and 48..50
    glEnd();
    glLineStipple(1, 0x00FF);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glVertex2f(10.0f, 200.5f); glVertex2f(60.0f, 200.5f);       // Rows 199..201
    glEnd();
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glRectf(80.5f, 160.5f, 120.5f, 180.5f);                     // Each outline starts again
    glRectf(80.5f, 190.5f, 120.5f, 210.5f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_LINE_STIPPLE);

    CHECK(whiteAt(10, 100, true) && whiteAt(17, 100, true) && whiteAt(18, 100, false) && whiteAt(25, 100, false) &&
          whiteAt(26, 100, true) && whiteAt(17, 101, false) && whiteAt(9, 100, false));
    CHECK(whiteAt(200, 20, true) && whiteAt(200, 27, true) && whiteAt(200, 28, false) && whiteAt(200, 35, false) &&
          whiteAt(200, 36, true));
    CHECK(whiteAt(59, 140, true) && whiteAt(52, 140, true) && whiteAt(51, 140, false) && whiteAt(44, 140, false) &&
          whiteAt(43, 140, true));
    CHECK(whiteAt(17, 120, true) && whiteAt(18, 120, false) && whiteAt(21, 120, false) && whiteAt(26, 120, true));
    CHECK(whiteAt(17, 130, true) && whiteAt(18, 130, true) && whiteAt(21, 130, true) && whiteAt(22, 130, false));
    CHECK(whiteAt(12, 110, true) && whiteAt(13, 110, false) && whiteAt(57, 110, false) && whiteAt(58, 110, true) &&
          whiteAt(60, 110, true) && whiteAt(61, 110, false));
    CHECK(whiteAt(17, 199, true) && whiteAt(17, 201, true) && whiteAt(18, 199, false) && whiteAt(18, 201, false) &&
          whiteAt(26, 200, true));
    CHECK(whiteAt(87, 160, true) && whiteAt(88, 160, false) && whiteAt(87, 190, true) && whiteAt(88, 190, false));

    // Polygon stipple: the diagonal x % 32 == y % 32 of the window, whatever the viewport
    for (int y = 0; y < 32; y++) { memset(&pattern[4*y], 0, 4); pattern[4*y + y/8] = (GLubyte)(0x80 >> (y & 7)); }
    glPolygonStipple(pattern);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_POLYGON_STIPPLE);
    glViewport(3, 5, 400, 240);
    glRectf(97.0f, 15.0f, 161.0f, 79.0f);                       // Window 100..164, 20..84
    glViewport(0, 0, 400, 240);
    CHECK(whiteAt(100, 36, true) && whiteAt(101, 36, false) && whiteAt(132, 36, true) && whiteAt(100, 68, true) &&
          whiteAt(121, 57, true) && whiteAt(121, 58, false) && whiteAt(99, 35, false));

    // Checker (x + y even) with perspective, a red texture on unit 0 and unit 1 (moved to PICA units 1 and 2)
    for (int y = 0; y < 32; y++) memset(&pattern[4*y], (y & 1)? 0x55 : 0xAA, 4);
    glPolygonStipple(pattern);
    GLuint tex[2];
    glGenTextures(2, tex);
    static const GLubyte red[4] = { 255, 0, 0, 255 }, yellow[4] = { 255, 255, 0, 255 };
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);     // 1x1 in 8x8: no padding sampled
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, yellow);
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);    // yellow * red = red
    glActiveTexture(GL_TEXTURE0);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -0.6, 0.6, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.0f);
    glRotatef(-60.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(20.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(0.5f, 0.5f);
    glVertex2f(-2.0f, -2.0f); glVertex2f(2.0f, -2.0f); glVertex2f(2.0f, 2.0f); glVertex2f(-2.0f, 2.0f);
    glEnd();
    glActiveTexture(GL_TEXTURE1);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_TEXTURE_2D);
    bool checker = true;
    for (int y = 110; y < 130; y += 3)
        for (int x = 190; x < 212; x += 5)
            checker = checker && pixelNear(x, y, ((x + y) & 1)? 0 : 255, 0, 0, -1);
    CHECK(checker);
    glDeleteTextures(2, tex);

    // Alpha test: fragments that fail it stay away; without it alpha 1/255 is drawn (stipple off: the whole quad) and
    // alpha keeps its exact value (alpha 0 is dropped then, see README)
    windowProjection();
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    glColor4f(0.0f, 1.0f, 0.0f, 0.25f);
    glRectf(10.0f, 10.0f, 20.0f, 20.0f);
    glColor4f(0.0f, 1.0f, 0.0f, 0.75f);
    glRectf(30.0f, 10.0f, 40.0f, 20.0f);
    glAlphaFunc(GL_LESS, 0.5f);
    glRectf(50.0f, 10.0f, 60.0f, 20.0f);
    glColor4f(0.0f, 1.0f, 0.0f, 0.25f);
    glRectf(70.0f, 10.0f, 80.0f, 20.0f);
    glDisable(GL_ALPHA_TEST);
    glColor4ub(0, 0, 255, 1);
    glRectf(90.0f, 10.0f, 100.0f, 20.0f);
    glColor4ub(255, 0, 0, 200);
    glRectf(130.0f, 10.0f, 140.0f, 20.0f);
    glColor4ub(255, 0, 0, 255);
    glRectf(150.0f, 10.0f, 160.0f, 20.0f);
    glDisable(GL_POLYGON_STIPPLE);
    glColor4ub(0, 0, 255, 1);
    glRectf(110.0f, 10.0f, 120.0f, 20.0f);
    CHECK(pixelNear(10, 10, 0, 0, 0, 255) && pixelNear(11, 11, 0, 0, 0, 255));
    CHECK(pixelNear(30, 10, 0, 255, 0, -1) && pixelNear(31, 10, 0, 0, 0, 255) && pixelNear(50, 10, 0, 0, 0, 255));
    CHECK(pixelNear(70, 10, 0, 255, 0, -1) && pixelNear(71, 10, 0, 0, 0, 255));
    CHECK(pixelNear(90, 10, 0, 0, 255, 1) && pixelNear(91, 10, 0, 0, 0, 255) && pixelNear(111, 10, 0, 0, 255, 1));
    GLubyte alpha[3][4];
    glReadPixels(90, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha[0]);
    glReadPixels(130, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha[1]);
    glReadPixels(150, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, alpha[2]);
    CHECK((alpha[0][3] == 1) && (alpha[1][3] == 200) && (alpha[2][3] == 255));     // Exact in the pattern
    CHECK(pixelNear(131, 10, 0, 0, 0, 255) && pixelNear(151, 10, 0, 0, 0, 255));
    CHECK(glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

// Accumulation buffer: queries, clamping, errors, attribute group, display lists, then rendered and read back: clear,
// load/accumulate (averaging two frames), add, mult, return with clamping, scissor box, color mask, return without the
// other fragment operations, draws before and after the operations, nothing in feedback mode
static void testAccum(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLint bits[4] = { 0 };
    glGetIntegerv(GL_ACCUM_RED_BITS, &bits[0]);
    glGetIntegerv(GL_ACCUM_GREEN_BITS, &bits[1]);
    glGetIntegerv(GL_ACCUM_BLUE_BITS, &bits[2]);
    glGetIntegerv(GL_ACCUM_ALPHA_BITS, &bits[3]);
    CHECK((bits[0] == 16) && (bits[1] == 16) && (bits[2] == 16) && (bits[3] == 16));
    GLfloat c[4] = { -9.0f, -9.0f, -9.0f, -9.0f };
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, c);
    CHECK((c[0] == 0.0f) && (c[1] == 0.0f) && (c[2] == 0.0f) && (c[3] == 0.0f));
    glClearAccum(2.0f, -3.0f, 0.5f, -0.25f);        // Clamped to [-1, 1]
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, c);
    CHECK((c[0] == 1.0f) && (c[1] == -1.0f) && (c[2] == 0.5f) && (c[3] == -0.25f));

    glAccum(0x1234, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glAccum(GL_RETURN, 1.0f);
    glEnd();
    CHECK(glGetError() == GL_INVALID_OPERATION);
    glClear(GL_CURRENT_BIT);
    CHECK(glGetError() == GL_INVALID_VALUE);

    // GL_ACCUM_BUFFER_BIT holds the clear value, GL_COLOR_BUFFER_BIT does not
    glPushAttrib(GL_ACCUM_BUFFER_BIT);
    glClearAccum(0.1f, 0.2f, 0.3f, 0.4f);
    glPopAttrib();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, c);
    CHECK((c[0] == 1.0f) && (c[3] == -0.25f));
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glClearAccum(0.1f, 0.2f, 0.3f, 0.4f);
    glPopAttrib();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, c);
    CHECK(fabsf(c[0] - 0.1f) < 1e-6f);

    // Rendered: window coordinates, the accumulation buffer returned with value 1 is read back
    windowProjection();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearAccum(0.25f, 0.5f, 0.75f, 1.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0f);
    CHECK(pixelNear(0, 0, 64, 128, 191, 255) && pixelNear(399, 239, 64, 128, 191, 255));
    glAccum(GL_RETURN, 4.0f);                       // Clamped to 1
    CHECK(pixelNear(200, 100, 255, 255, 255, 255));

    // Two frames averaged; GL_LOAD replaces, GL_ACCUM adds
    glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_LOAD, 0.5f);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(0.0f, 1.0f, 0.0f);
    glRectf(100.0f, 100.0f, 110.0f, 110.0f);        // Drawn before the read: in the accumulated image
    glAccum(GL_ACCUM, 0.5f);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0f);
    CHECK(pixelNear(10, 10, 128, 0, 128, 255) && pixelNear(105, 105, 128, 128, 0, 255));

    // GL_MULT and GL_ADD on every component, negative values clamp to 0 when returned
    glAccum(GL_MULT, 0.5f);
    glAccum(GL_RETURN, 2.0f);
    CHECK(pixelNear(10, 10, 128, 0, 128, 255));
    glAccum(GL_ADD, 0.25f);                         // (0.25, 0, 0.25, 0.5) + 0.25
    glAccum(GL_RETURN, 1.0f);
    CHECK(pixelNear(10, 10, 128, 64, 128, 191));
    glAccum(GL_ADD, -1.0f);
    glAccum(GL_RETURN, 1.0f);
    CHECK(pixelNear(10, 10, 0, 0, 0, 0));
    glAccum(GL_ADD, -1.0f);
    glAccum(GL_ADD, 0.5f);                          // -1 (clamped) + 0.5
    glAccum(GL_RETURN, -1.0f);
    CHECK(pixelNear(10, 10, 128, 128, 128, 128));

    // Scissor box: clears and operations change only the pixels in it, the return writes only them
    glClearAccum(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(20, 20, 10, 10);
    glClearAccum(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_ADD, -0.5f);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(0, 0, 25, 240);
    glEnable(GL_SCISSOR_TEST);
    glAccum(GL_RETURN, 1.0f);
    glDisable(GL_SCISSOR_TEST);
    CHECK(pixelNear(20, 20, 128, 128, 128, 128) && pixelNear(24, 29, 128, 128, 128, 128));
    CHECK(pixelNear(19, 20, 0, 0, 0, 255) && pixelNear(20, 30, 0, 0, 0, 255) && pixelNear(25, 20, 0, 0, 255, 255));

    // The return goes through the color mask, but not blending, alpha, depth or stencil tests, logic ops or fog; draws
    // after it are drawn over it
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearAccum(1.0f, 0.5f, 1.0f, 1.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ZERO, GL_ZERO);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_NEVER, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_NEVER);
    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_NEVER, 0, 0xFF);
    glEnable(GL_FOG);
    glColorMask(GL_TRUE, GL_TRUE, GL_FALSE, GL_TRUE);
    glAccum(GL_RETURN, 1.0f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_FOG);
    glColor3f(0.0f, 1.0f, 0.0f);
    glRectf(50.0f, 50.0f, 60.0f, 60.0f);
    CHECK(pixelNear(40, 40, 255, 128, 0, 255) && pixelNear(55, 55, 0, 255, 0, 255));
    glDrawBuffer(GL_NONE);                          // Returns nothing
    glClearAccum(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0f);
    glDrawBuffer(GL_BACK);
    CHECK(pixelNear(40, 40, 255, 128, 0, 255));

    // Display lists: compiled, executed when called
    GLuint list = glGenLists(1);
    glNewList(list, GL_COMPILE);
    glClearAccum(0.0f, 1.0f, 0.0f, 1.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0f);
    glEndList();
    glGetFloatv(GL_ACCUM_CLEAR_VALUE, c);
    CHECK(c[1] == 0.0f);
    CHECK(pixelNear(40, 40, 255, 128, 0, 255));
    glCallList(list);
    CHECK(pixelNear(40, 40, 0, 255, 0, 255));
    glDeleteLists(list, 1);

    // Feedback mode: no accumulation buffer operation
    GLfloat feedback[8];
    glFeedbackBuffer(8, GL_2D, feedback);
    glRenderMode(GL_FEEDBACK);
    glAccum(GL_ADD, -1.0f);
    glRenderMode(GL_RENDER);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glAccum(GL_RETURN, 1.0f);
    CHECK(pixelNear(40, 40, 0, 255, 0, 255));
    CHECK(glGetError() == GL_NO_ERROR);
    glPopAttrib();
}

// Color of window pixel (x, y) within tol (filtered texels in Azahar's upscaled renderers); prints it otherwise
static bool pixelAbout(int x, int y, int r, int g, int b, int tol)
{
    GLubyte p[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
    bool ok = (abs(p[0] - r) <= tol) && (abs(p[1] - g) <= tol) && (abs(p[2] - b) <= tol);
    if (!ok) printf("  pixel (%i, %i): %i %i %i\n", x, y, p[0], p[1], p[2]);
    return ok;
}

// Wrap modes on a non-power-of-two texture (12x10, stored 16x16): the padding holds what GL samples past the image, so
// linear filtering across the edges and clamped coordinates past 1 see the image, not the padding. Blue first column
// and row, red elsewhere, magnified 10 times
static void testNpotWrap(void)
{
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    GLubyte texels[10][12][4];
    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 12; x++)
        {
            bool blue = (x == 0) || (y == 0);
            texels[y][x][0] = blue? 0 : 255;
            texels[y][x][1] = 0;
            texels[y][x][2] = blue? 255 : 0;
            texels[y][x][3] = 255;
        }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 12, 10, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    windowProjection();
    glEnable(GL_TEXTURE_2D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    // s over 120 pixels at the middle of row 4, t over 120 pixels in the middle of column 5
    const float tRow = 4.5f/10.0f, sCol = 5.5f/12.0f;
    for (int pass = 0; pass < 2; pass++)
    {
        bool repeat = (pass == 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repeat? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repeat? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, tRow); glVertex2i(10, 50);
        glTexCoord2f(1.0f, tRow); glVertex2i(130, 50);
        glTexCoord2f(1.0f, tRow); glVertex2i(130, 60);
        glTexCoord2f(0.0f, tRow); glVertex2i(10, 60);
        glTexCoord2f(sCol, 0.0f); glVertex2i(200, 50);
        glTexCoord2f(sCol, 0.0f); glVertex2i(210, 50);
        glTexCoord2f(sCol, 1.0f); glVertex2i(210, 170);
        glTexCoord2f(sCol, 1.0f); glVertex2i(200, 170);
        glTexCoord2f(0.0f, tRow); glVertex2i(10, 80);       // Clamped up to s = 2
        glTexCoord2f(2.0f, tRow); glVertex2i(130, 80);
        glTexCoord2f(2.0f, tRow); glVertex2i(130, 90);
        glTexCoord2f(0.0f, tRow); glVertex2i(10, 90);
        glEnd();

        // Last pixel: 0.45 of the texel past the image end; first pixel: 0.45 of the texel before the image start.
        // Repeat: the other edge (blue, red); clamp: the edge itself
        if (repeat)
        {
            CHECK(pixelAbout(129, 55, 140, 0, 115, 25) && pixelAbout(10, 55, 115, 0, 140, 25));
            CHECK(pixelAbout(205, 169, 140, 0, 115, 25) && pixelAbout(205, 50, 115, 0, 140, 25));
        }
        else
        {
            CHECK(pixelAbout(129, 55, 255, 0, 0, 25) && pixelAbout(10, 55, 0, 0, 255, 25));
            CHECK(pixelAbout(205, 169, 255, 0, 0, 25) && pixelAbout(205, 50, 0, 0, 255, 25));
            CHECK(pixelAbout(120, 85, 255, 0, 0, 25) && pixelAbout(100, 85, 255, 0, 0, 25));
        }
    }
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &tex);
    CHECK(glGetError() == GL_NO_ERROR);
    glPopAttrib();
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
    testTexGen();
    testTexFormats();
    test1D();
    testColorBuffers();
    testPixels();
    testPixelTransfer();
    testDefaultTextures();
    testFeedback();
    testStipple();
    testAccum();
    testNpotWrap();
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
