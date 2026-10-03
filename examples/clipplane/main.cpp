// c3dgl example: user clip planes (glClipPlane). The top screen shows one clip setup per cell (4x2 grid, 100x120 px
// per cell). Where a cell has a reference, the clipped shape is drawn in the upper half and the shape it must have,
// drawn without clipping, in the lower half. The bottom screen shows the result of the non-visual checks and what
// each cell must look like.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cmath>
#include <cstdio>
#include <initializer_list>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;
constexpr float ASPECT = static_cast<float>(CELL_H) / CELL_W;      // Cells span x -1..1, y -ASPECT..ASPECT
constexpr float PI = 3.14159265f;

int checks, failures;

#define CHECK(cond) check((cond), #cond, __LINE__)

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    failures++;
    std::printf("FAIL %i: %s\n", line, what);
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-4; }

GLuint checkerTexture;

// Cell (column, row from the top) as viewport with an orthographic projection, square pixels
void beginCell(int column, int row) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -ASPECT, ASPECT, -10.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_TRANSFORM_BIT | GL_ENABLE_BIT | GL_LIGHTING_BIT | GL_POLYGON_BIT | GL_LINE_BIT | GL_POINT_BIT |
                 GL_TEXTURE_BIT);
}

void endCell() { glPopAttrib(); }

void setPlane(GLenum plane, double a, double b, double c, double d) {
    const GLdouble eq[4] = {a, b, c, d};
    glClipPlane(plane, eq);
    glEnable(plane);
}

void rect(float x0, float y0, float x1, float y1) {
    glBegin(GL_QUADS);
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glEnd();
}

// 1: smooth colors are interpolated at the cut. Top: a red -> blue square, plane keeps x <= 0; bottom: its left half
// drawn directly (red -> purple)
void drawGradient() {
    setPlane(GL_CLIP_PLANE0, -1, 0, 0, 0);
    glBegin(GL_QUADS);
    glColor3f(1, 0, 0); glVertex2f(-0.8f, 0.15f);
    glColor3f(0, 0, 1); glVertex2f(0.8f, 0.15f);
    glColor3f(0, 0, 1); glVertex2f(0.8f, 1.0f);
    glColor3f(1, 0, 0); glVertex2f(-0.8f, 1.0f);
    glEnd();
    glDisable(GL_CLIP_PLANE0);
    glBegin(GL_QUADS);
    glColor3f(1, 0, 0); glVertex2f(-0.8f, -1.0f);
    glColor3f(0.5f, 0, 0.5f); glVertex2f(0.0f, -1.0f);
    glColor3f(0.5f, 0, 0.5f); glVertex2f(0.0f, -0.15f);
    glColor3f(1, 0, 0); glVertex2f(-0.8f, -0.15f);
    glEnd();
}

// 2: the plane is set under a translation and a 45 degree rotation and stays where it was in eye space when the
// modelview changes. Top: a green square cut along its diagonal; bottom: the remaining triangle drawn directly
void drawTransformed() {
    glPushMatrix();
    glTranslatef(0.0f, 0.6f, 0.0f);
    glRotatef(45.0f, 0.0f, 0.0f, 1.0f);
    setPlane(GL_CLIP_PLANE2, 1, 0, 0, 0);       // Keeps x + y >= 0.6 after the transform
    glPopMatrix();
    glColor3f(0.2f, 0.8f, 0.3f);
    glPushMatrix();
    glTranslatef(0.0f, 0.6f, 0.0f);
    glScalef(0.4f, 0.4f, 1.0f);
    rect(-1.0f, -1.0f, 1.0f, 1.0f);
    glPopMatrix();
    glDisable(GL_CLIP_PLANE2);
    glBegin(GL_TRIANGLES);
    glVertex2f(0.4f, -1.0f);
    glVertex2f(0.4f, -0.2f);
    glVertex2f(-0.4f, -0.2f);
    glEnd();
}

// 3: all six planes: four cut a checkerboard to the window |x| <= 0.5, |y| <= 0.6, two remove everything outside
// -1 <= z <= 1, including a magenta square at z = 3 over the whole cell that must not appear
void drawSixPlanes() {
    setPlane(GL_CLIP_PLANE0, 1, 0, 0, 0.5);
    setPlane(GL_CLIP_PLANE1, -1, 0, 0, 0.5);
    setPlane(GL_CLIP_PLANE2, 0, 1, 0, 0.6);
    setPlane(GL_CLIP_PLANE3, 0, -1, 0, 0.6);
    setPlane(GL_CLIP_PLANE4, 0, 0, 1, 1);
    setPlane(GL_CLIP_PLANE5, 0, 0, -1, 1);
    for (int y = 0; y < 6; y++) {
        for (int x = 0; x < 5; x++) {
            const bool light = ((x + y) & 1) != 0;
            glColor3f(light ? 0.95f : 0.25f, light ? 0.85f : 0.25f, light ? 0.3f : 0.6f);
            rect(-1.0f + 0.4f * x, -1.2f + 0.4f * y, -0.6f + 0.4f * x, -0.8f + 0.4f * y);
        }
    }
    glColor3f(1.0f, 0.0f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-1.0f, -1.2f, 3.0f);
    glVertex3f(1.0f, -1.2f, 3.0f);
    glVertex3f(1.0f, 1.2f, 3.0f);
    glVertex3f(-1.0f, 1.2f, 3.0f);
    glEnd();
}

// 4: lines and points, plane keeps x >= 0: 3 px lines (horizontal and slanted) start exactly at the thin white
// boundary line, only the 4 right points of each row of 8 remain
void drawLinesPoints() {
    setPlane(GL_CLIP_PLANE1, 1, 0, 0, 0);
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glColor3f(1.0f, 0.4f, 0.2f);
    glVertex2f(-0.9f, 0.9f); glVertex2f(0.9f, 0.9f);
    glColor3f(0.3f, 0.9f, 0.3f);
    glVertex2f(-0.9f, 0.2f); glVertex2f(0.9f, 0.7f);
    glColor3f(0.4f, 0.6f, 1.0f);
    glVertex2f(-0.9f, 0.5f); glVertex2f(0.9f, 0.0f);
    glColor3f(1.0f, 1.0f, 0.3f);
    glVertex2f(-0.9f, -0.2f); glVertex2f(-0.1f, -0.2f);    // Completely clipped
    glEnd();
    glPointSize(6.0f);
    glColor3f(1.0f, 0.4f, 0.2f);
    glBegin(GL_POINTS);
    for (int row = 0; row < 2; row++)
        for (int i = 0; i < 8; i++) glVertex2f(-0.875f + 0.25f * i, -0.5f - 0.4f * row);
    glEnd();
    glDisable(GL_CLIP_PLANE1);
    glLineWidth(1.0f);
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_LINES);
    glVertex2f(0.0f, -1.2f);
    glVertex2f(0.0f, 1.2f);
    glEnd();
}

// 5: glPolygonMode(GL_LINE) clips the polygon first: no outline along the cut. Top: a triangle cut at y = 0.6
// (open trapezoid, no top edge); bottom: a square cut at x = 0.3 (open on the right)
void drawOutlines() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0f);
    glColor3f(0.4f, 0.9f, 1.0f);
    setPlane(GL_CLIP_PLANE0, 0, -1, 0, 0.6);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, 0.0f);
    glVertex2f(0.8f, 0.0f);
    glVertex2f(0.0f, 1.1f);
    glEnd();
    glDisable(GL_CLIP_PLANE0);
    glColor3f(1.0f, 0.6f, 0.3f);
    setPlane(GL_CLIP_PLANE1, -1, 0, 0, 0.3);
    rect(-0.7f, -1.0f, 0.7f, -0.25f);
}

// Lat/long sphere of radius 1 with normals
void sphere(int slices, int stacks) {
    for (int i = 0; i < stacks; i++) {
        const float t0 = PI * i / stacks - PI / 2, t1 = PI * (i + 1) / stacks - PI / 2;
        glBegin(GL_QUAD_STRIP);
        for (int j = 0; j <= slices; j++) {
            const float p = 2 * PI * j / slices;
            for (float t : {t0, t1}) {      // Counter-clockwise seen from outside
                const float x = std::cos(t) * std::cos(p), y = std::sin(t), z = std::cos(t) * std::sin(p);
                glNormal3f(x, y, z);
                glVertex3f(x, y, z);
            }
        }
        glEnd();
    }
}

// 6: a lit sphere with its top cap cut off, tilted toward the viewer: blue outside, the inside (back faces,
// two-sided lighting) yellow through the opening
void drawSphere() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5 * ASPECT, 0.5 * ASPECT, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glTranslatef(0.0f, 0.0f, -3.2f);
    glRotatef(35.0f, 1.0f, 0.0f, 0.0f);
    setPlane(GL_CLIP_PLANE3, 0, -1, 0, 0.35);       // Keeps y <= 0.35 in sphere coordinates

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    const GLfloat light[4] = {0.3f, 1.0f, 1.0f, 0.0f}, outside[4] = {0.2f, 0.4f, 1.0f, 1.0f},
                  inside[4] = {1.0f, 0.85f, 0.2f, 1.0f};
    glPushMatrix();
    glLoadIdentity();
    glLightfv(GL_LIGHT0, GL_POSITION, light);
    glPopMatrix();
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, outside);
    glMaterialfv(GL_BACK, GL_AMBIENT_AND_DIFFUSE, inside);
    sphere(24, 16);
}

void bindChecker() {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, checkerTexture);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

// 7: a textured square cut by six planes to a regular hexagon: the checker stays straight and aligned with the
// square's edges, its squares are not distorted at the cuts
void drawHexagon() {
    for (int i = 0; i < 6; i++) {
        const float a = PI / 3 * i + PI / 6;
        setPlane(GL_CLIP_PLANE0 + i, -std::cos(a), -std::sin(a), 0, 0.75);
    }
    bindChecker();
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-0.9f, -0.9f);
    glTexCoord2f(1, 0); glVertex2f(0.9f, -0.9f);
    glTexCoord2f(1, 1); glVertex2f(0.9f, 0.9f);
    glTexCoord2f(0, 1); glVertex2f(-0.9f, 0.9f);
    glEnd();
}

// 8: a textured floor in perspective, cut by the eye space plane z >= -6 set before the camera moves: the floor ends
// in a straight horizontal edge, the checker stays perspective-correct up to it
void drawFloor() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.5, 0.5, -0.5 * ASPECT, 0.5 * ASPECT, 1.0, 40.0);
    glMatrixMode(GL_MODELVIEW);
    setPlane(GL_CLIP_PLANE4, 0, 0, 1, 6);
    glRotatef(10.0f, 1.0f, 0.0f, 0.0f);
    glTranslatef(0.0f, -1.0f, 0.0f);
    bindChecker();
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-4.0f, 0.0f, -1.0f);
    glTexCoord2f(4, 0); glVertex3f(4.0f, 0.0f, -1.0f);
    glTexCoord2f(4, 30); glVertex3f(4.0f, 0.0f, -31.0f);
    glTexCoord2f(0, 30); glVertex3f(-4.0f, 0.0f, -31.0f);
    glEnd();
}

void createChecker() {
    GLubyte texels[8 * 8 * 4];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            GLubyte* t = &texels[(y * 8 + x) * 4];
            const bool light = ((x / 2 + y / 2) & 1) != 0;
            t[0] = light ? 240 : 40;
            t[1] = light ? 240 : 90;
            t[2] = light ? 240 : 200;
            t[3] = 255;
        }
    }
    glGenTextures(1, &checkerTexture);
    glBindTexture(GL_TEXTURE_2D, checkerTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
}

//----------------------------------------------------------------------------------
// Non-visual checks
//----------------------------------------------------------------------------------
bool planeIs(GLenum plane, double a, double b, double c, double d) {
    GLdouble e[4] = {9, 9, 9, 9};
    glGetClipPlane(plane, e);
    return near(e[0], a) && near(e[1], b) && near(e[2], c) && near(e[3], d);
}

void testClipApi() {
    GLint v = 0;

    // Defaults
    glGetIntegerv(GL_MAX_CLIP_PLANES, &v);
    CHECK(v == 6);
    for (int i = 0; i < 6; i++) {
        CHECK(!glIsEnabled(GL_CLIP_PLANE0 + i));
        CHECK(planeIs(GL_CLIP_PLANE0 + i, 0, 0, 0, 0));
    }

    // Stored in eye coordinates: transformed by the inverse modelview
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glTranslatef(2.0f, 0.0f, 0.0f);
    const GLdouble xPlane[4] = {1, 0, 0, 0};
    glClipPlane(GL_CLIP_PLANE0, xPlane);            // x_obj >= 0 is x_eye >= 2
    CHECK(planeIs(GL_CLIP_PLANE0, 1, 0, 0, -2));
    glLoadIdentity();
    glScalef(2.0f, 4.0f, 1.0f);
    const GLdouble yPlane[4] = {0, 1, 0, -1};
    glClipPlane(GL_CLIP_PLANE1, yPlane);            // y_obj >= 1 is y_eye >= 4
    CHECK(planeIs(GL_CLIP_PLANE1, 0, 0.25, 0, -1));
    glLoadIdentity();
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    glClipPlane(GL_CLIP_PLANE2, xPlane);            // x_obj >= 0 is y_eye >= 0
    CHECK(planeIs(GL_CLIP_PLANE2, 0, 1, 0, 0));
    glPopMatrix();
    CHECK(planeIs(GL_CLIP_PLANE0, 1, 0, 0, -2));    // Unaffected by later matrix changes

    // ES float and fixed point
    const GLfloat fPlane[4] = {0.5f, -1.0f, 2.0f, 3.0f};
    glClipPlanef(GL_CLIP_PLANE3, fPlane);
    GLfloat f[4];
    glGetClipPlanef(GL_CLIP_PLANE3, f);
    CHECK(near(f[0], 0.5) && near(f[1], -1.0) && near(f[2], 2.0) && near(f[3], 3.0));
    const GLfixed xPlaneFixed[4] = {0x8000, 0, -0x10000, 0x20000};
    glClipPlanex(GL_CLIP_PLANE4, xPlaneFixed);
    GLfixed x[4];
    glGetClipPlanex(GL_CLIP_PLANE4, x);
    CHECK(x[0] == 0x8000 && x[1] == 0 && x[2] == -0x10000 && x[3] == 0x20000);

    // Enables
    glEnable(GL_CLIP_PLANE5);
    CHECK(glIsEnabled(GL_CLIP_PLANE5));
    GLboolean b = GL_FALSE;
    glGetBooleanv(GL_CLIP_PLANE5, &b);
    CHECK(b == GL_TRUE);
    glDisable(GL_CLIP_PLANE5);
    CHECK(!glIsEnabled(GL_CLIP_PLANE5));
    CHECK(glGetError() == GL_NO_ERROR);

    // Errors
    glClipPlane(GL_CLIP_PLANE0 + 6, xPlane);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glEnable(GL_CLIP_PLANE0 + 6);
    CHECK(glGetError() == GL_INVALID_ENUM);
    GLdouble e[4];
    glGetClipPlane(GL_LIGHT0, e);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glBegin(GL_POINTS);
    glClipPlane(GL_CLIP_PLANE0, yPlane);
    glEnd();
    CHECK(glGetError() == GL_INVALID_OPERATION);
    CHECK(planeIs(GL_CLIP_PLANE0, 1, 0, 0, -2));    // Unchanged by the failed calls

    // Attribute stack: TRANSFORM_BIT restores planes and enables, ENABLE_BIT only the enables
    glPushAttrib(GL_TRANSFORM_BIT);
    glEnable(GL_CLIP_PLANE1);
    glClipPlane(GL_CLIP_PLANE0, yPlane);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_CLIP_PLANE1));
    CHECK(planeIs(GL_CLIP_PLANE0, 1, 0, 0, -2));
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_CLIP_PLANE2);
    glClipPlane(GL_CLIP_PLANE0, yPlane);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_CLIP_PLANE2));
    CHECK(planeIs(GL_CLIP_PLANE0, 0, 1, 0, -1));
    CHECK(glGetError() == GL_NO_ERROR);
}

} // namespace

int main() {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, nullptr);

    if (!c3dglInit()) {
        std::printf("c3dglInit failed\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        gfxExit();
        return 1;
    }

    std::printf("c3dgl clip planes\n");
    testClipApi();
    std::printf("\n%i/%i checks passed\n\n"
                "Top screen, upper/lower halves\n"
                "must match where there are two\n"
                "top row:\n"
                "- red->blue square cut at x=0\n"
                "- square cut along a diagonal\n"
                "- checker window, no magenta\n"
                "- lines/points right of the\n"
                "  white line only\n"
                "bottom row:\n"
                "- outlines open at the cuts\n"
                "- blue sphere, cap cut off,\n"
                "  yellow inside\n"
                "- checker hexagon\n"
                "- floor ending in a straight\n"
                "  edge\n\n"
                "START: exit\n",
                checks - failures, checks);

    createChecker();
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        beginCell(0, 0); drawGradient(); endCell();
        beginCell(1, 0); drawTransformed(); endCell();
        beginCell(2, 0); drawSixPlanes(); endCell();
        beginCell(3, 0); drawLinesPoints(); endCell();
        beginCell(0, 1); drawOutlines(); endCell();
        beginCell(1, 1); drawSphere(); endCell();
        beginCell(2, 1); drawHexagon(); endCell();
        beginCell(3, 1); drawFloor(); endCell();
        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
