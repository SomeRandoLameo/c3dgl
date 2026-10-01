// c3dgl example: fog. The top screen shows one fog setup per cell (4x2 grid, 100x120 px per cell). Most cells draw
// pairs of squares: left a fogged square at a known eye depth, right an unfogged square in the color GL's fog
// equation gives for that depth (computed here). Each pair must look the same. The bottom screen shows the result of
// the non-visual checks and what each cell must look like.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;
constexpr float NEAR = 1.0f, FAR = 30.0f;
constexpr float HALF_W = 0.5f, HALF_H = HALF_W * CELL_H / CELL_W;     // Frustum at the near plane, square pixels

int checks, failures;

#define CHECK(cond) check((cond), #cond, __LINE__)

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    failures++;
    std::printf("FAIL %i: %s\n", line, what);
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

struct Color {
    float r, g, b, a;
};

const Color OBJECT = {0.9f, 0.15f, 0.1f, 1.0f};     // Red squares
const Color FOG = {0.75f, 0.8f, 0.9f, 1.0f};        // Light blue-gray fog

// GL fog factor for eye distance c (1 = no fog)
float fogFactor(GLenum mode, float density, float start, float end, float c) {
    float f;
    if (mode == GL_LINEAR) f = (end - c) / (end - start);
    else if (mode == GL_EXP2) f = std::exp(-(density * c) * (density * c));
    else f = std::exp(-density * c);
    return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}

Color fogged(const Color& c, float f) {
    return {f * c.r + (1 - f) * FOG.r, f * c.g + (1 - f) * FOG.g, f * c.b + (1 - f) * FOG.b, c.a};
}

void setFogColor() {
    const GLfloat color[4] = {FOG.r, FOG.g, FOG.b, FOG.a};
    glFogfv(GL_FOG_COLOR, color);
}

// Cell (column, row from the top) as viewport with a perspective projection (near 1, far 30)
void beginCell(int column, int row) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-HALF_W, HALF_W, -HALF_H, HALF_H, NEAR, FAR);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_FOG_BIT | GL_ENABLE_BIT | GL_VIEWPORT_BIT | GL_COLOR_BUFFER_BIT);
    setFogColor();
}

void endCell() { glPopAttrib(); }

// Square at NDC (x, y) with half size s (NDC), at eye depth z < 0 (perspective: object size grows with |z|)
void drawSquare(float x, float y, float s, float z, const Color& c, bool perspective = true) {
    const float kx = perspective ? -z * HALF_W / NEAR : 1.0f, ky = perspective ? -z * HALF_H / NEAR : 1.0f;
    glColor4f(c.r, c.g, c.b, c.a);
    glBegin(GL_QUADS);
    glVertex3f((x - s) * kx, (y - s) * ky, z);
    glVertex3f((x + s) * kx, (y - s) * ky, z);
    glVertex3f((x + s) * kx, (y + s) * ky, z);
    glVertex3f((x - s) * kx, (y + s) * ky, z);
    glEnd();
}

// Four rows of pairs at the eye depths zs: left fogged, right the expected color without fog
void drawPairs(GLenum mode, float density, float start, float end, const float zs[4], bool perspective = true) {
    glFogi(GL_FOG_MODE, mode);
    glFogf(GL_FOG_DENSITY, density);
    glFogf(GL_FOG_START, start);
    glFogf(GL_FOG_END, end);
    for (int i = 0; i < 4; i++) {
        const float y = 0.72f - 0.48f * i;
        glEnable(GL_FOG);
        drawSquare(-0.45f, y, 0.2f, zs[i], OBJECT, perspective);
        glDisable(GL_FOG);
        drawSquare(0.45f, y, 0.2f, zs[i], fogged(OBJECT, fogFactor(mode, density, start, end, -zs[i])), perspective);
    }
}

// 1: GL_LINEAR from 2 to 12 at depths 3, 5.5, 8, 10.5 (f = 0.9, 0.65, 0.4, 0.15)
void drawLinear() {
    const float zs[4] = {-3.0f, -5.5f, -8.0f, -10.5f};
    drawPairs(GL_LINEAR, 1.0f, 2.0f, 12.0f, zs);
}

// 2: GL_EXP, density 0.15
void drawExp() {
    const float zs[4] = {-2.0f, -5.0f, -9.0f, -15.0f};
    drawPairs(GL_EXP, 0.15f, 0.0f, 1.0f, zs);
}

// 3: GL_EXP2, density 0.12
void drawExp2() {
    const float zs[4] = {-2.0f, -6.0f, -10.0f, -14.0f};
    drawPairs(GL_EXP2, 0.12f, 0.0f, 1.0f, zs);
}

// 4: GL_LINEAR with an orthographic projection (depth is linear in z there)
void drawOrtho() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, 0.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    const float zs[4] = {-2.0f, -6.0f, -10.0f, -14.0f};
    drawPairs(GL_LINEAR, 1.0f, 1.0f, 16.0f, zs, false);
}

// 5: a floor (checkerboard of 1x1 tiles) receding from z = -1 to -30 under linear fog 3..20: tiles fade smoothly
// into the fog color toward the horizon, no bands
void drawFloor() {
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 3.0f);
    glFogf(GL_FOG_END, 20.0f);
    glEnable(GL_FOG);
    glBegin(GL_QUADS);
    for (int z = 1; z < 30; z++) {
        for (int x = -12; x < 12; x++) {
            const bool light = ((x + z) & 1) != 0;
            glColor3f(light ? 0.95f : 0.2f, light ? 0.6f : 0.3f, light ? 0.2f : 0.1f);
            glVertex3f(x, -1.0f, -z);
            glVertex3f(x, -1.0f, -z - 1.0f);
            glVertex3f(x + 1.0f, -1.0f, -z - 1.0f);
            glVertex3f(x + 1.0f, -1.0f, -z);
        }
    }
    glEnd();
}

// 6: like cell 1, but with glDepthRange(0.25, 0.75): the table follows the depth range, pairs still match
void drawDepthRange() {
    glDepthRange(0.25, 0.75);
    drawLinear();
}

// 7: lines and points (expanded on the CPU, drawn in NDC) get fog by depth too: three horizontal 3 px lines at
// depths 3, 7, 11 and a row of 6 px points below each, all red fading to the fog color; small reference squares on
// the right in the expected color
void drawLinesPoints() {
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 2.0f);
    glFogf(GL_FOG_END, 12.0f);
    glLineWidth(3.0f);
    glPointSize(6.0f);
    const float zs[3] = {-3.0f, -7.0f, -11.0f};
    for (int i = 0; i < 3; i++) {
        const float y = 0.7f - 0.6f * i, z = zs[i], kx = -z * HALF_W / NEAR, ky = -z * HALF_H / NEAR;
        glEnable(GL_FOG);
        glColor3f(OBJECT.r, OBJECT.g, OBJECT.b);
        glBegin(GL_LINES);
        glVertex3f(-0.85f * kx, y * ky, z);
        glVertex3f(0.35f * kx, y * ky, z);
        glEnd();
        glBegin(GL_POINTS);
        for (int p = 0; p < 4; p++) glVertex3f((-0.8f + 0.35f * p) * kx, (y - 0.2f) * ky, z);
        glEnd();
        glDisable(GL_FOG);
        drawSquare(0.65f, y - 0.1f, 0.15f, z, fogged(OBJECT, fogFactor(GL_LINEAR, 1.0f, 2.0f, 12.0f, -z)));
    }
    glLineWidth(1.0f);
    glPointSize(1.0f);
}

// 8: fog keeps alpha: half transparent squares over a white background, blended. Left fogged, right the expected
// color with the same alpha; each pair must match, the white shining through both
void drawAlpha() {
    drawSquare(0.0f, 0.0f, 0.95f, -20.0f, {1.0f, 1.0f, 1.0f, 1.0f});       // Background (unfogged)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    const Color half = {OBJECT.r, OBJECT.g, OBJECT.b, 0.5f};
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_START, 2.0f);
    glFogf(GL_FOG_END, 12.0f);
    const float zs[3] = {-3.0f, -6.0f, -9.0f};
    for (int i = 0; i < 3; i++) {
        const float y = 0.6f - 0.6f * i;
        glEnable(GL_FOG);
        drawSquare(-0.45f, y, 0.22f, zs[i], half);
        glDisable(GL_FOG);
        drawSquare(0.45f, y, 0.22f, zs[i], fogged(half, fogFactor(GL_LINEAR, 1.0f, 2.0f, 12.0f, -zs[i])));
    }
}

//----------------------------------------------------------------------------------
// Non-visual checks
//----------------------------------------------------------------------------------
void testFogApi() {
    GLint v = 0;
    GLfloat f[4];

    // Defaults
    CHECK(!glIsEnabled(GL_FOG));
    glGetIntegerv(GL_FOG_MODE, &v);
    CHECK(v == GL_EXP);
    glGetFloatv(GL_FOG_DENSITY, f);
    CHECK(near(f[0], 1.0f));
    glGetFloatv(GL_FOG_START, f);
    CHECK(near(f[0], 0.0f));
    glGetFloatv(GL_FOG_END, f);
    CHECK(near(f[0], 1.0f));
    glGetFloatv(GL_FOG_COLOR, f);
    CHECK(near(f[0], 0) && near(f[1], 0) && near(f[2], 0) && near(f[3], 0));

    // Set and get
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glGetIntegerv(GL_FOG_MODE, &v);
    CHECK(v == GL_LINEAR);
    glFogf(GL_FOG_START, 2.5f);
    glFogi(GL_FOG_END, 40);
    glGetFloatv(GL_FOG_START, f);
    CHECK(near(f[0], 2.5f));
    glGetFloatv(GL_FOG_END, f);
    CHECK(near(f[0], 40.0f));
    const GLfloat color[4] = {0.25f, 0.5f, 2.0f, -1.0f};       // Clamped to [0, 1]
    glFogfv(GL_FOG_COLOR, color);
    glGetFloatv(GL_FOG_COLOR, f);
    CHECK(near(f[0], 0.25f) && near(f[1], 0.5f) && near(f[2], 1.0f) && near(f[3], 0.0f));
    const GLint icolor[4] = {2147483647, 0, 0, 2147483647};
    glFogiv(GL_FOG_COLOR, icolor);
    glGetFloatv(GL_FOG_COLOR, f);
    CHECK(near(f[0], 1.0f) && near(f[1], 0.0f) && near(f[3], 1.0f));
    GLint ic[4];
    glGetIntegerv(GL_FOG_COLOR, ic);
    CHECK(ic[0] == 2147483647 && ic[1] == 0);
    glEnable(GL_FOG);
    CHECK(glIsEnabled(GL_FOG));
    GLboolean b = GL_FALSE;
    glGetBooleanv(GL_FOG, &b);
    CHECK(b == GL_TRUE);
    glDisable(GL_FOG);

    // ES fixed point: the mode is an enum, passed unscaled
    glFogx(GL_FOG_MODE, GL_EXP2);
    glGetIntegerv(GL_FOG_MODE, &v);
    CHECK(v == GL_EXP2);
    glFogx(GL_FOG_DENSITY, 0x8000);
    glGetFloatv(GL_FOG_DENSITY, f);
    CHECK(near(f[0], 0.5f));
    const GLfixed xcolor[4] = {0x10000, 0x8000, 0, 0x10000};
    glFogxv(GL_FOG_COLOR, xcolor);
    GLfixed x[4];
    glGetFixedv(GL_FOG_COLOR, x);
    CHECK(x[0] == 0x10000 && x[1] == 0x8000 && x[2] == 0);
    CHECK(glGetError() == GL_NO_ERROR);

    // Errors
    glFogi(GL_FOG_MODE, GL_LINEAR_ATTENUATION);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glFogf(GL_FOG_DENSITY, -1.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glFogf(GL_FOG_COLOR, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glFogf(0x1234, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetIntegerv(GL_FOG_MODE, &v);
    CHECK(v == GL_EXP2);                        // Unchanged by the failed calls

    // Attribute stack: FOG_BIT restores everything, ENABLE_BIT only the enable
    glPushAttrib(GL_FOG_BIT);
    glEnable(GL_FOG);
    glFogi(GL_FOG_MODE, GL_LINEAR);
    glFogf(GL_FOG_END, 99.0f);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_FOG));
    glGetIntegerv(GL_FOG_MODE, &v);
    CHECK(v == GL_EXP2);
    glGetFloatv(GL_FOG_END, f);
    CHECK(near(f[0], 40.0f));
    glPushAttrib(GL_ENABLE_BIT);
    glEnable(GL_FOG);
    glFogf(GL_FOG_START, 7.0f);
    glPopAttrib();
    CHECK(!glIsEnabled(GL_FOG));
    glGetFloatv(GL_FOG_START, f);
    CHECK(near(f[0], 7.0f));
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

    std::printf("c3dgl fog\n");
    testFogApi();
    std::printf("\n%i/%i checks passed\n\n"
                "Top screen: left/right squares\n"
                "of every pair must match\n"
                "top row:\n"
                "- LINEAR, EXP, EXP2: red squares\n"
                "  fading into blue-gray, deeper\n"
                "  rows more fogged\n"
                "- LINEAR with glOrtho\n"
                "bottom row:\n"
                "- checker floor fading smoothly\n"
                "  into the fog, no bands\n"
                "- LINEAR with glDepthRange\n"
                "- fogged lines + points, matching\n"
                "  squares on the right\n"
                "- half transparent pairs on a\n"
                "  white background\n\n"
                "START: exit\n",
                checks - failures, checks);

    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        beginCell(0, 0); drawLinear(); endCell();
        beginCell(1, 0); drawExp(); endCell();
        beginCell(2, 0); drawExp2(); endCell();
        beginCell(3, 0); drawOrtho(); endCell();
        beginCell(0, 1); drawFloor(); endCell();
        beginCell(1, 1); drawDepthRange(); endCell();
        beginCell(2, 1); drawLinesPoints(); endCell();
        beginCell(3, 1); drawAlpha(); endCell();
        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
