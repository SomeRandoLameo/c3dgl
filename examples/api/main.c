// c3dgl example: self-check of the non-visual API (glGet*, glGetError, glIsEnabled, matrix loads, entry point
// variants). Failed checks are listed on the bottom screen; the top screen is GREEN when everything passed,
// RED otherwise. On top of that, four white squares are drawn with four different vertex calls
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
