// c3dgl example: self-check of the non-visual API (glGet*, glGetError, glIsEnabled, matrix loads, entry point
// variants). Failed checks are listed on the bottom screen; the top screen is GREEN when everything passed,
// RED otherwise. On top of that, four white squares are drawn with four different vertex calls
// (glRectf, glRecti, glVertex2sv, glVertex4f with w = 2) and must look identical.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <math.h>
#include <stdio.h>

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

// Four white 60x60 squares with different vertex calls, pixel coordinates y down
static void drawSquares(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 400.0, 240.0, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glColor3ub(255, 255, 255);

    glRectf(30.0f, 90.0f, 90.0f, 150.0f);
    glRecti(125, 90, 185, 150);

    static const GLshort corners[4][2] = {{215, 90}, {275, 90}, {275, 150}, {215, 150}};
    glBegin(GL_QUADS);
    for (int i = 0; i < 4; i++) glVertex2sv(corners[i]);
    glEnd();

    glBegin(GL_QUADS);                          // Homogeneous: (2x, 2y, 0, 2)
    glVertex4f(620.0f, 180.0f, 0.0f, 2.0f);
    glVertex4f(740.0f, 180.0f, 0.0f, 2.0f);
    glVertex4f(740.0f, 300.0f, 0.0f, 2.0f);
    glVertex4f(620.0f, 300.0f, 0.0f, 2.0f);
    glEnd();
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

    printf("c3dgl api test\n"
           "(C3DGL warnings below come from\n"
           " the error checks, they are expected)\n\n");
    testErrors();
    testCapabilities();
    testQueries();
    testMatrices();
    CHECK(glGetError() == GL_NO_ERROR);         // Nothing left over

    printf("\n%i/%i checks passed\n\n"
           "Expected on the top screen:\n"
           "- GREEN background (all passed)\n"
           "- four identical white squares\n"
           "  in a row\n\n"
           "START: exit\n", checks - failures, checks);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        if (failures == 0) glClearColor(0.1f, 0.5f, 0.2f, 1.0f);
        else glClearColor(0.7f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        drawSquares();
        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
