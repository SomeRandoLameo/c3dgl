// c3dgl example: texture features, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// All cells use an asymmetric "F" texture (white F on blue, red block in the texel corner s = 0, t = 0).
// Page 1: texture matrix, the same square with texcoords 0..1 in every cell, only the GL_TEXTURE matrix differs.
// Page 2: texture coordinates: per-vertex q, texcoord array types. A switches pages.
// The expected result is printed on the bottom screen.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cstdio>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;

// 16x16 (or 12x12 for the NPOT version) RGB image, row 0 = t = 0 = bottom: upright "F", red block bottom left
GLuint createF(int size) {
    GLubyte pixels[16][16][3];
    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            // Glyph on a 16x16 grid, scaled down for smaller sizes
            const int gx = x * 16 / size, gy = y * 16 / size;
            const bool stem = gx >= 4 && gx <= 6 && gy >= 2 && gy <= 13;
            const bool top = gx >= 4 && gx <= 12 && gy >= 11 && gy <= 13;
            const bool middle = gx >= 4 && gx <= 10 && gy >= 7 && gy <= 8;
            const bool marker = gx <= 1 && gy <= 1;
            GLubyte* p = pixels[y][x];
            if (marker) { p[0] = 230; p[1] = 30; p[2] = 30; }
            else if (stem || top || middle) { p[0] = p[1] = p[2] = 255; }
            else { p[0] = 40; p[1] = 70; p[2] = 200; }
        }
    }

    // Rows of `size` pixels, tightly packed
    GLubyte packed[16 * 16 * 3];
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size * 3; x++) packed[(y * size) * 3 + x] = pixels[y][x / 3][x % 3];

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, packed);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // REPEAT only on the POT texture (NPOT + REPEAT samples the padding, a known limitation)
    const GLint wrap = (size == 16) ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    return id;
}

// Square of 80x80 px in the cell, texcoords 0..1 counter-clockwise from the bottom left
void drawCell(int column, int row, GLuint texture) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex2f(-0.8f, -0.8f);
    glTexCoord2f(1, 0); glVertex2f(0.8f, -0.8f);
    glTexCoord2f(1, 1); glVertex2f(0.8f, 0.8f);
    glTexCoord2f(0, 1); glVertex2f(-0.8f, 0.8f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

// Set the texture matrix for the next cell (GL_TEXTURE mode), back to modelview afterwards
template <typename F>
void textureMatrix(F setup) {
    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    setup();
    glMatrixMode(GL_MODELVIEW);
}

void resetTextureMatrix() {
    textureMatrix([] {});
}

// Page 2 ------------------------------------------------------------------------------------------------

void beginCell(int column, int row, GLuint texture) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor3f(1, 1, 1);
}

// Trapezoid, bottom edge twice as long as the top. q = edge length makes the mapping perspective correct
// (projective = true): the F looks like it lies on a floor tilted away. Without q (projective = false) both
// triangles are mapped affinely and the F bends along the diagonal
void drawTrapezoid(bool projective) {
    const float q = projective ? 2.0f : 1.0f;
    glBegin(GL_QUADS);
    glTexCoord4f(0, 0, 0, q); glVertex2f(-0.8f, -0.8f);
    glTexCoord4f(q, 0, 0, q); glVertex2f(0.8f, -0.8f);
    glTexCoord4f(1, 1, 0, 1); glVertex2f(0.4f, 0.8f);
    glTexCoord4f(0, 1, 0, 1); glVertex2f(-0.4f, 0.8f);
    glEnd();
}

// Square from texcoord arrays of the given type; size 4 arrays carry q
template <typename T>
void drawArraySquare(GLenum type, int size, const T* texcoords) {
    static const GLfloat positions[8] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.8f, 0.8f, -0.8f, 0.8f};
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, positions);
    glTexCoordPointer(size, type, 0, texcoords);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

void drawTexcoordPage(GLuint tex) {
    beginCell(0, 0, tex); drawTrapezoid(true);
    beginCell(1, 0, tex); drawTrapezoid(false);

    // glTexCoord3f: r is ignored, plain F
    beginCell(2, 0, tex);
    glBegin(GL_QUADS);
    glTexCoord3f(0, 0, 0.7f); glVertex2f(-0.8f, -0.8f);
    glTexCoord3f(1, 0, 0.7f); glVertex2f(0.8f, -0.8f);
    glTexCoord3f(1, 1, 0.7f); glVertex2f(0.8f, 0.8f);
    glTexCoord3f(0, 1, 0.7f); glVertex2f(-0.8f, 0.8f);
    glEnd();

    // GL_SHORT texcoords 0..2: 2x2 tiles
    static const GLshort shorts[8] = {0, 0, 2, 0, 2, 2, 0, 2};
    beginCell(3, 0, tex); drawArraySquare(GL_SHORT, 2, shorts);

    // GL_BYTE texcoords, size 1 (t = 0 for all): the bottom texel row stretched over the square
    static const GLbyte bytes[4] = {0, 1, 1, 0};
    beginCell(0, 1, tex); drawArraySquare(GL_BYTE, 1, bytes);

    // GL_FIXED texcoords (ES) 0..1
    static const GLfixed fixeds[8] = {0, 0, 0x10000, 0, 0x10000, 0x10000, 0, 0x10000};
    beginCell(1, 1, tex); drawArraySquare(GL_FIXED, 2, fixeds);

    // GL_FLOAT size 4: texcoords 0..2 with q = 2, so (s, t) / q = 0..1, a plain F (without the q divide: 2x2 tiles)
    static const GLfloat floats4[16] = {0, 0, 0, 2, 2, 0, 0, 2, 2, 2, 0, 2, 0, 2, 0, 2};
    beginCell(2, 1, tex); drawArraySquare(GL_FLOAT, 4, floats4);

    // GL_DOUBLE texcoords (desktop GL), mirrored in s
    static const GLdouble doubles[8] = {1, 0, 0, 0, 0, 1, 1, 1};
    beginCell(3, 1, tex); drawArraySquare(GL_DOUBLE, 2, doubles);
    glDisable(GL_TEXTURE_2D);
}

void printPage(int page) {
    consoleClear();
    std::printf("c3dgl texture test, page %i/2\n\n", page + 1);
    if (page == 0) {
        std::printf("Texture matrix. Expected on the\n"
                    "top screen, left to right, top row:\n"
                    "- identity: upright white F,\n"
                    "  red block bottom left\n"
                    "- translate s by 0.25: F shifted\n"
                    "  LEFT by a quarter, wrapping\n"
                    "- scale 2: 2x2 small F tiles\n"
                    "- rotate 90 around the center:\n"
                    "  F lying on its back, rotated\n"
                    "  counter-clockwise (red block\n"
                    "  bottom right)\n"
                    "bottom row:\n"
                    "- scrolling diagonally\n"
                    "- projective q = 2: the bottom\n"
                    "  left quarter of the F, 2x\n"
                    "- projective q = 1 + s: F corner\n"
                    "  in perspective, texels wider\n"
                    "  to the right\n"
                    "- NPOT 12x12, scale -1 in t:\n"
                    "  F upside down\n");
    } else {
        std::printf("Texture coordinates. Expected,\n"
                    "left to right, top row:\n"
                    "- trapezoid with q: F on a floor\n"
                    "  tilted away, bars straight\n"
                    "- same without q: F bent along\n"
                    "  the diagonal (affine)\n"
                    "- glTexCoord3f: plain upright F\n"
                    "- GL_SHORT 0..2: 2x2 F tiles\n"
                    "bottom row:\n"
                    "- GL_BYTE size 1: the bottom\n"
                    "  texel row stretched: red\n"
                    "  stripe left, rest blue\n"
                    "- GL_FIXED: plain upright F\n"
                    "- size 4, (2s, 2t, q = 2): plain\n"
                    "  upright F (2x2 tiles = q lost)\n"
                    "- GL_DOUBLE: F mirrored left-right\n");
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

    const GLuint texPot = createF(16), texNpot = createF(12);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    float time = 0.0f;
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

        glClear(GL_COLOR_BUFFER_BIT);
        if (page == 1) {
            drawTexcoordPage(texPot);
            c3dglSwapBuffers();
            continue;
        }

        resetTextureMatrix();
        drawCell(0, 0, texPot);

        textureMatrix([] { glTranslatef(0.25f, 0.0f, 0.0f); });
        drawCell(1, 0, texPot);

        textureMatrix([] { glScalef(2.0f, 2.0f, 1.0f); });
        drawCell(2, 0, texPot);

        // Rotating the texcoords by -90 around the center turns the image by +90 (counter-clockwise) on screen
        textureMatrix([] {
            glTranslatef(0.5f, 0.5f, 0.0f);
            glRotatef(-90.0f, 0.0f, 0.0f, 1.0f);
            glTranslatef(-0.5f, -0.5f, 0.0f);
        });
        drawCell(3, 0, texPot);

        textureMatrix([time] { glTranslatef(time, time * 0.5f, 0.0f); });
        drawCell(0, 1, texPot);

        // q = 2 everywhere: (s, t) / 2
        textureMatrix([] {
            const GLfloat m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2};
            glLoadMatrixf(m);
        });
        drawCell(1, 1, texPot);

        // q = 1 + s: s / (1 + s) runs 0..0.5, slower on the right
        textureMatrix([] {
            const GLfloat m[16] = {1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            glLoadMatrixf(m);
        });
        drawCell(2, 1, texPot);

        textureMatrix([] {
            glTranslatef(0.0f, 1.0f, 0.0f);
            glScalef(1.0f, -1.0f, 1.0f);
        });
        drawCell(3, 1, texNpot);

        resetTextureMatrix();
        c3dglSwapBuffers();
        time += 1.0f / 240.0f;
    }

    glDeleteTextures(1, &texPot);
    glDeleteTextures(1, &texNpot);
    c3dglClose();
    gfxExit();
    return 0;
}
