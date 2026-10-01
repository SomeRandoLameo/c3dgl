// c3dgl example: primitives and rasterization, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// Page 1: every primitive mode. Face culling (GL_BACK, CCW front) is on for all filled shapes, so a strip, fan or
// quad strip that is assembled with the wrong winding shows up as missing triangles.
// Page 2: flat shading, polygon modes, edge flags, polygon offset, depth range. A switches pages.
// The expected result is printed on the bottom screen.
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

// Page 2 ------------------------------------------------------------------------------------------------

void rainbow(int i, int count) {
    const float a = 2 * PI * i / count;
    glColor3f(0.5f + 0.5f * std::cos(a), 0.5f + 0.5f * std::cos(a - 2.1f), 0.5f + 0.5f * std::cos(a + 2.1f));
}

// GL_FLAT fan: 8 wedges, each one solid in the color of its LAST vertex (no gradients)
void drawFlatFan() {
    glShadeModel(GL_FLAT);
    glBegin(GL_TRIANGLE_FAN);
    glColor3f(1, 1, 1);
    glVertex2f(0, 0);
    for (int i = 0; i <= 8; i++) {
        rainbow(i, 8);
        glVertex2f(0.8f * std::cos(2 * PI * i / 8), 0.8f * std::sin(2 * PI * i / 8));
    }
    glEnd();
    glShadeModel(GL_SMOOTH);
}

// GL_FLAT polygon: hexagon with a different color per vertex, solid RED (the FIRST vertex) above;
// the same with GL_SMOOTH below for comparison
void drawFlatPolygon() {
    for (int flat = 1; flat >= 0; flat--) {
        glShadeModel(flat ? GL_FLAT : GL_SMOOTH);
        const float cy = flat ? 0.55f : -0.55f;
        glBegin(GL_POLYGON);
        for (int i = 0; i < 6; i++) {
            if (i == 0) glColor3f(1, 0, 0);
            else rainbow(i, 6);
            glVertex2f(0.5f * std::cos(2 * PI * i / 6), cy + 0.5f * std::sin(2 * PI * i / 6));
        }
        glEnd();
    }
    glShadeModel(GL_SMOOTH);
}

// glPolygonMode GL_LINE: a quad (4 edges, no diagonal) above, a hexagon outline below
void drawLineMode() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0f);
    glColor3f(0.3f, 1.0f, 0.3f);
    glRectf(-0.6f, 0.2f, 0.6f, 1.0f);
    glColor3f(1.0f, 0.9f, 0.2f);
    glBegin(GL_POLYGON);
    for (int i = 0; i < 6; i++) glVertex2f(0.5f * std::cos(2 * PI * i / 6), -0.55f + 0.5f * std::sin(2 * PI * i / 6));
    glEnd();
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// glPolygonMode GL_POINT: the 4 corners of a quad strip of 2 quads -> 6 points (2 rows of 3), 6 px
void drawPointMode() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
    glPointSize(6.0f);
    glColor3f(1, 1, 1);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < 3; i++) {
        const float x = -0.6f + 0.6f * i;
        glVertex2f(x, 0.5f);
        glVertex2f(x, -0.5f);
    }
    glEnd();
    glPointSize(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Front GL_FILL, back GL_LINE: counter-clockwise (front) triangle filled above, clockwise (back) outlined below
void drawFrontBack() {
    glPolygonMode(GL_FRONT, GL_FILL);
    glPolygonMode(GL_BACK, GL_LINE);
    glLineWidth(2.0f);
    glColor3f(0.2f, 0.6f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.6f, 0.2f); glVertex2f(0.6f, 0.2f); glVertex2f(0.0f, 1.0f);        // CCW
    glVertex2f(-0.6f, -1.0f); glVertex2f(0.0f, -0.2f); glVertex2f(0.6f, -1.0f);     // CW
    glEnd();
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Edge flags: a square from two GL_TRIANGLES in GL_LINE mode, the shared diagonal flagged off -> plain square
void drawEdgeFlags() {
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glLineWidth(2.0f);
    glColor3f(1.0f, 0.5f, 0.1f);
    glBegin(GL_TRIANGLES);
    glEdgeFlag(GL_TRUE);  glVertex2f(-0.6f, -0.6f);
    glEdgeFlag(GL_TRUE);  glVertex2f(0.6f, -0.6f);
    glEdgeFlag(GL_FALSE); glVertex2f(0.6f, 0.6f);      // The flag belongs to the edge starting here: the diagonal
    glEdgeFlag(GL_FALSE); glVertex2f(-0.6f, -0.6f);    // Diagonal again
    glEdgeFlag(GL_TRUE);  glVertex2f(0.6f, 0.6f);
    glEdgeFlag(GL_TRUE);  glVertex2f(-0.6f, 0.6f);
    glEnd();
    glEdgeFlag(GL_TRUE);
    glLineWidth(1.0f);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// Polygon offset: a green plane tilted away from the viewer, red decals on it at the SAME depth with GL_LESS.
// Left decal: glPolygonOffset(-1, -1) pulls it in front, it must be solid red. Right decal: (+1, +1) pushes it
// behind, it must not show at all. (Without any offset the result is undefined: coplanar but different triangles
// z-fight, GL gives no guarantee which one wins.)
void drawPolygonOffset() {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1, 0.1, -0.12, 0.12, 0.1, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -2.5f);
    glRotatef(-65.0f, 1.0f, 0.0f, 0.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.2f, 0.7f, 0.3f);
    glRectf(-1.0f, -1.5f, 1.0f, 1.5f);

    glColor3f(0.9f, 0.1f, 0.1f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-1.0f, -1.0f);
    glRectf(-0.8f, -1.2f, -0.1f, 1.2f);
    glPolygonOffset(1.0f, 1.0f);
    glRectf(0.1f, -1.2f, 0.8f, 1.2f);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_DEPTH_TEST);
}

// glDepthRange: blue square drawn first at z = 0.5 (near) with range [0.5, 1], red square drawn second at
// z = -0.5 (far) with range [0, 0.5] -> the red one ends up in FRONT where they overlap
void drawDepthRange() {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthRange(0.5, 1.0);
    glColor3f(0.2f, 0.4f, 1.0f);
    glBegin(GL_QUADS);
    glVertex3f(-0.8f, -0.2f, 0.5f); glVertex3f(0.3f, -0.2f, 0.5f); glVertex3f(0.3f, 0.9f, 0.5f); glVertex3f(-0.8f, 0.9f, 0.5f);
    glEnd();
    glDepthRange(0.0, 0.5);
    glColor3f(0.9f, 0.1f, 0.1f);
    glBegin(GL_QUADS);
    glVertex3f(-0.3f, -0.9f, -0.5f); glVertex3f(0.8f, -0.9f, -0.5f); glVertex3f(0.8f, 0.2f, -0.5f); glVertex3f(-0.3f, 0.2f, -0.5f);
    glEnd();
    glDepthRange(0.0, 1.0);
    glDisable(GL_DEPTH_TEST);
}

void printPage(int page) {
    consoleClear();
    std::printf("c3dgl primitives test, page %i/2\n\n", page + 1);
    if (page == 0) {
        std::printf("Expected on the top screen,\n"
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
                    "wrong winding (culling is on).\n");
    } else {
        std::printf("Expected on the top screen,\n"
                    "left to right, top row:\n"
                    "- FLAT fan: 8 solid wedges\n"
                    "- FLAT polygon: solid red hexagon\n"
                    "  above a smooth one\n"
                    "- LINE mode: square outline (no\n"
                    "  diagonal), hexagon outline\n"
                    "- POINT mode: 2 rows of 3 points\n"
                    "bottom row:\n"
                    "- front FILL/back LINE: filled\n"
                    "  triangle, outlined one below\n"
                    "- edge flags: square outline,\n"
                    "  no diagonal\n"
                    "- polygon offset: solid red decal\n"
                    "  LEFT on a tilted green plane,\n"
                    "  none on the right (pushed back)\n"
                    "- depth range: red square in\n"
                    "  FRONT of the blue one\n");
    }
    std::printf("\nA: next page   START: exit\n");
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

    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    int page = 0;
    printPage(page);

    while (aptMainLoop()) {
        hidScanInput();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) {
            page = 1 - page;
            printPage(page);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (page == 0) {
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

            glDisable(GL_CULL_FACE);
        } else {
            beginCell(0, 0); drawFlatFan();
            beginCell(1, 0); drawFlatPolygon();
            beginCell(2, 0); drawLineMode();
            beginCell(3, 0); drawPointMode();
            beginCell(0, 1); drawFrontBack();
            beginCell(1, 1); drawEdgeFlags();
            beginCell(2, 1); drawPolygonOffset();
            beginCell(3, 1); drawDepthRange();
        }

        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
