// c3dgl example: primitives and rasterization, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// Page 1: every primitive mode. Face culling (GL_BACK, CCW front) is on for all filled shapes, so a strip, fan or
// quad strip that is assembled with the wrong winding shows up as missing triangles.
// Page 2: flat shading, polygon modes, edge flags, polygon offset, depth range.
// Page 3: evaluators (glMap1/2, glEvalMesh, glEvalCoord). A switches pages.
// The expected result is printed on the bottom screen.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

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

// Page 3 ------------------------------------------------------------------------------------------------

// Cubic Bezier through 4 control points, drawn with glEvalMesh1 as a line; gray control polygon
const GLfloat curvePoints[4][3] = {{-0.8f, -0.8f, 0}, {-0.6f, 0.9f, 0}, {0.6f, -0.9f, 0}, {0.8f, 0.8f, 0}};

void drawControlPolygon() {
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_LINE_STRIP);
    for (const auto& p : curvePoints) glVertex3fv(p);
    glEnd();
}

void drawEvalCurve() {
    drawControlPolygon();
    glMap1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 4, &curvePoints[0][0]);
    glEnable(GL_MAP1_VERTEX_3);
    glMapGrid1f(30, 0.0f, 1.0f);
    glColor3f(1.0f, 0.9f, 0.2f);
    glLineWidth(2.0f);
    glEvalMesh1(GL_LINE, 0, 30);
    glLineWidth(1.0f);
    glDisable(GL_MAP1_VERTEX_3);
}

// Same curve, colors from a MAP1_COLOR_4 (red -> blue), points sent with glEvalCoord1f over a different domain
void drawEvalColorCurve() {
    drawControlPolygon();
    static const GLfloat colors[2][4] = {{1, 0.2f, 0.2f, 1}, {0.3f, 0.5f, 1, 1}};
    glMap1f(GL_MAP1_VERTEX_3, 2.0f, 4.0f, 3, 4, &curvePoints[0][0]);
    glMap1f(GL_MAP1_COLOR_4, 2.0f, 4.0f, 4, 2, &colors[0][0]);
    glEnable(GL_MAP1_VERTEX_3);
    glEnable(GL_MAP1_COLOR_4);
    glLineWidth(3.0f);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i <= 30; i++) glEvalCoord1f(2.0f + 2.0f * i / 30);
    glEnd();
    glLineWidth(1.0f);
    glDisable(GL_MAP1_COLOR_4);
    glDisable(GL_MAP1_VERTEX_3);
}

// glEvalMesh1 GL_POINT: 11 points along the curve
void drawEvalPoints() {
    drawControlPolygon();
    glMap1f(GL_MAP1_VERTEX_3, 0.0f, 1.0f, 3, 4, &curvePoints[0][0]);
    glEnable(GL_MAP1_VERTEX_3);
    glMapGrid1f(10, 0.0f, 1.0f);
    glColor3f(1, 1, 1);
    glPointSize(5.0f);
    glEvalMesh1(GL_POINT, 0, 10);
    glPointSize(1.0f);
    glDisable(GL_MAP1_VERTEX_3);
}

// Rational quadratic (MAP1_VERTEX_4) = exact quarter circle of radius 0.8; gray reference points on the circle
void drawEvalRational() {
    glColor3f(0.5f, 0.5f, 0.5f);
    glPointSize(3.0f);
    glBegin(GL_POINTS);
    for (int i = 0; i <= 12; i++) {
        const float a = 3.14159265f / 2 * i / 12;
        glVertex2f(-0.4f + 0.8f * std::cos(a), -0.4f + 0.8f * std::sin(a));
    }
    glEnd();
    glPointSize(1.0f);

    const float w = 0.70710678f;
    const GLfloat points[3][4] = {{0.4f, -0.4f, 0, 1}, {0.4f * w, 0.4f * w, 0, w}, {-0.4f, 0.4f, 0, 1}};
    glMap1f(GL_MAP1_VERTEX_4, 0.0f, 1.0f, 4, 3, &points[0][0]);
    glEnable(GL_MAP1_VERTEX_4);
    glMapGrid1f(24, 0.0f, 1.0f);
    glColor3f(0.3f, 1.0f, 0.3f);
    glEvalMesh1(GL_LINE, 0, 24);
    glDisable(GL_MAP1_VERTEX_4);
}

// Bicubic patch with a bump; corner colors from a bilinear MAP2_COLOR_4, texcoords from MAP2_TEXTURE_COORD_2
void setupPatch() {
    static GLfloat patch[4][4][3];
    for (int u = 0; u < 4; u++)
        for (int v = 0; v < 4; v++) {
            patch[u][v][0] = -0.8f + 0.533f * u;
            patch[u][v][1] = -0.8f + 0.533f * v;
            patch[u][v][2] = ((u == 1 || u == 2) && (v == 1 || v == 2)) ? 0.8f : 0.0f;
        }
    static const GLfloat colors[2][2][4] = {{{1, 0.2f, 0.2f, 1}, {0.2f, 1, 0.2f, 1}}, {{0.2f, 0.4f, 1, 1}, {1, 1, 0.2f, 1}}};
    static const GLfloat texcoords[2][2][2] = {{{0, 0}, {0, 1}}, {{1, 0}, {1, 1}}};
    glMap2f(GL_MAP2_VERTEX_3, 0, 1, 12, 4, 0, 1, 3, 4, &patch[0][0][0]);
    glMap2f(GL_MAP2_COLOR_4, 0, 1, 8, 2, 0, 1, 4, 2, &colors[0][0][0]);
    glMap2f(GL_MAP2_TEXTURE_COORD_2, 0, 1, 4, 2, 0, 1, 2, 2, &texcoords[0][0][0]);
    glMapGrid2f(12, 0.0f, 1.0f, 12, 0.0f, 1.0f);
}

// Patch seen from above at an angle
void beginPatchCell(int column, int row) {
    beginCell(column, row);
    glRotatef(-50.0f, 1.0f, 0.0f, 0.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MAP2_VERTEX_3);
}

void endPatchCell() {
    glDisable(GL_MAP2_VERTEX_3);
    glDisable(GL_MAP2_COLOR_4);
    glDisable(GL_MAP2_TEXTURE_COORD_2);
    glDisable(GL_DEPTH_TEST);
}

void drawEvalSurfaces(GLuint checker) {
    setupPatch();

    beginPatchCell(0, 1);                   // Filled, colored corners
    glEnable(GL_MAP2_COLOR_4);
    glEvalMesh2(GL_FILL, 0, 12, 0, 12);
    endPatchCell();

    beginPatchCell(1, 1);                   // Wireframe
    glColor3f(0.5f, 0.8f, 1.0f);
    glEvalMesh2(GL_LINE, 0, 12, 0, 12);
    endPatchCell();

    beginPatchCell(2, 1);                   // Textured
    glEnable(GL_MAP2_TEXTURE_COORD_2);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, checker);
    glColor3f(1, 1, 1);
    glEvalMesh2(GL_FILL, 0, 12, 0, 12);
    glDisable(GL_TEXTURE_2D);
    endPatchCell();

    beginPatchCell(3, 1);                   // Points of a coarser grid, with glEvalPoint2 for the corners in red
    glMapGrid2f(6, 0.0f, 1.0f, 6, 0.0f, 1.0f);
    glColor3f(1, 1, 1);
    glPointSize(3.0f);
    glEvalMesh2(GL_POINT, 0, 6, 0, 6);
    glColor3f(1, 0.2f, 0.2f);
    glPointSize(6.0f);
    glBegin(GL_POINTS);
    glEvalPoint2(0, 0); glEvalPoint2(6, 0); glEvalPoint2(0, 6); glEvalPoint2(6, 6);
    glEnd();
    glPointSize(1.0f);
    endPatchCell();
}

GLuint createChecker() {
    GLubyte pixels[8][8][3];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            const bool light = (x / 2 + y / 2) % 2 == 0;
            pixels[y][x][0] = light ? 255 : 40;
            pixels[y][x][1] = light ? 220 : 60;
            pixels[y][x][2] = light ? 120 : 160;
        }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

void printPage(int page) {
    consoleClear();
    std::printf("c3dgl primitives test, page %i/3\n\n\n\n\n", page + 1);
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
    } else if (page == 2) {
        std::printf("Evaluators. Expected,\n"
                    "left to right, top row:\n"
                    "- yellow Bezier curve, gray\n"
                    "  control polygon\n"
                    "- same curve, red -> blue\n"
                    "- 11 white points on the curve\n"
                    "- rational quarter circle (green)\n"
                    "  through the gray points\n"
                    "bottom row (patch with a bump):\n"
                    "- filled, red/green/blue/yellow\n"
                    "  corners\n"
                    "- light blue wireframe\n"
                    "- checker textured\n"
                    "- 7x7 points, red corners\n");
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

static void printStats() {
    // Rows 2-4 of the console, timings of the last frame; the cursor stays where the text ended
    std::printf("\x1b[s");
    std::printf("\x1b[2;1HCPU:     %6.2fms\x1b[K", C3D_GetProcessingTime());
    std::printf("\x1b[3;1HGPU:     %6.2fms\x1b[K", C3D_GetDrawingTime());
    std::printf("\x1b[4;1HCmdBuf:  %6.2f%%\x1b[K", C3D_GetCmdBufUsage()*100.0f);
    std::printf("\x1b[u");
}

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

    const GLuint checker = createChecker();
    int page = 0;
    printPage(page);

    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) {
            page = (page + 1) % 3;
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
        } else if (page == 2) {
            beginCell(0, 0); drawEvalCurve();
            beginCell(1, 0); drawEvalColorCurve();
            beginCell(2, 0); drawEvalPoints();
            beginCell(3, 0); drawEvalRational();
            drawEvalSurfaces(checker);
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
