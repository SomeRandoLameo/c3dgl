// c3dgl example: every primitive mode, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// Face culling (GL_BACK, CCW front) is on for all filled shapes, so a strip, fan or quad strip that is
// assembled with the wrong winding shows up as missing triangles. The expected result is printed on
// the bottom screen.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;
constexpr float PI = 3.14159265f;

// Cell (column, row from the top) as viewport; coordinates inside are x -1..1, y -1.2..1.2 (square pixels)
void beginCell(int column, int row) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// GL_POINTS: 3x3 grid, size 1..9 px from the top left
void drawPoints() {
    glColor3f(1.0f, 1.0f, 1.0f);
    for (int i = 0; i < 9; i++) {
        glPointSize(static_cast<float>(i + 1));
        glBegin(GL_POINTS);
        glVertex2f(-0.6f + 0.6f * (i % 3), 0.6f - 0.6f * (i / 3));
        glEnd();
    }
    glPointSize(1.0f);
}

// GL_LINE_STRIP: open zigzag, 3 px wide, color fading red -> yellow
void drawLineStrip() {
    glLineWidth(3.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < 6; i++) {
        glColor3f(1.0f, i / 5.0f, 0.0f);
        glVertex2f(-0.8f + 0.32f * i, (i % 2) ? 0.6f : -0.6f);
    }
    glEnd();
    glLineWidth(1.0f);
}

// GL_LINE_LOOP: closed five-pointed star (the last edge back to the start must be there)
void drawLineLoop() {
    glLineWidth(2.0f);
    glColor3f(0.3f, 1.0f, 0.3f);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 5; i++) {
        const float a = PI / 2 + i * 4 * PI / 5;
        glVertex2f(0.8f * std::cos(a), 0.8f * std::sin(a));
    }
    glEnd();
    glLineWidth(1.0f);
}

// GL_TRIANGLE_STRIP: horizontal ribbon of 8 triangles, blue top edge, pink bottom edge.
// Pairs go top, bottom while moving right, so the first triangle is counter-clockwise
void drawTriangleStrip() {
    glBegin(GL_TRIANGLE_STRIP);
    for (int i = 0; i <= 4; i++) {
        const float x = -0.8f + 0.4f * i;
        glColor3f(0.2f, 0.4f, 1.0f); glVertex2f(x, 0.4f);
        glColor3f(1.0f, 0.4f, 0.8f); glVertex2f(x, -0.4f);
    }
    glEnd();
}

// GL_TRIANGLE_FAN: disc with a white center and a rainbow rim
void drawTriangleFan() {
    constexpr int SEGMENTS = 24;
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1.0f, 1.0f, 1.0f);
    glVertex2f(0.0f, 0.0f);
    for (int i = 0; i <= SEGMENTS; i++) {
        const float a = 2 * PI * i / SEGMENTS;
        glColor3f(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::cos(a - 2.1f), 0.5f + 0.5f * std::cos(a + 2.1f));
        glVertex2f(0.8f * std::cos(a), 0.8f * std::sin(a));
    }
    glEnd();
}

// GL_QUAD_STRIP: vertical ladder of 4 bands, alternating orange/teal. Pairs go right, left while moving
// down, so every quad (v[2i], v[2i+1], v[2i+3], v[2i+2]) is counter-clockwise
void drawQuadStrip() {
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= 4; i++) {
        const float y = 0.8f - 0.4f * i;
        if (i % 2) glColor3f(0.1f, 0.8f, 0.7f);
        else glColor3f(1.0f, 0.6f, 0.1f);
        glVertex2f(0.6f, y);
        glVertex2f(-0.6f, y);
    }
    glEnd();
}

// GL_POLYGON from a client array: yellow hexagon
void drawPolygon() {
    float vertices[6][2];
    for (int i = 0; i < 6; i++) {
        const float a = 2 * PI * i / 6;
        vertices[i][0] = 0.8f * std::cos(a);
        vertices[i][1] = 0.8f * std::sin(a);
    }
    glColor3f(1.0f, 0.9f, 0.2f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glDrawArrays(GL_POLYGON, 0, 6);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// glDrawElements: red square as GL_TRIANGLE_FAN, white GL_LINE_LOOP outline around it from the same array
void drawElements() {
    static const float vertices[] = {
        -0.5f, -0.5f,  0.5f, -0.5f,  0.5f, 0.5f,  -0.5f, 0.5f,      // Square
        -0.7f, -0.7f,  0.7f, -0.7f,  0.7f, 0.7f,  -0.7f, 0.7f,      // Outline
    };
    static const GLushort square[] = {0, 1, 2, 3};
    static const GLubyte outline[] = {4, 5, 6, 7};

    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, vertices);
    glColor3f(0.9f, 0.2f, 0.2f);
    glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, square);
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineWidth(2.0f);
    glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_BYTE, outline);
    glLineWidth(1.0f);
    glDisableClientState(GL_VERTEX_ARRAY);
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

    std::printf("c3dgl primitives test\n\n"
                "Expected on the top screen,\n"
                "left to right, top row:\n"
                "- POINTS: 3x3 squares, 1..9 px\n"
                "- LINE_STRIP: open zigzag,\n"
                "  red -> yellow\n"
                "- LINE_LOOP: closed green star\n"
                "- TRIANGLE_STRIP: ribbon, blue\n"
                "  top to pink bottom, no gaps\n"
                "bottom row:\n"
                "- TRIANGLE_FAN: full rainbow disc\n"
                "- QUAD_STRIP: 4 bands, orange/teal\n"
                "- POLYGON: yellow hexagon\n"
                "- DrawElements: red square in a\n"
                "  closed white outline\n\n"
                "Any hole in a filled shape means\n"
                "wrong winding (culling is on).\n\n"
                "START: exit\n");

    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        beginCell(0, 0); drawPoints();
        beginCell(1, 0); drawLineStrip();
        beginCell(2, 0); drawLineLoop();
        beginCell(3, 0); drawTriangleStrip();
        beginCell(0, 1); drawTriangleFan();
        beginCell(1, 1); drawQuadStrip();
        beginCell(2, 1); drawPolygon();
        beginCell(3, 1); drawElements();

        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
