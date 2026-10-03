// c3dgl example: texture features, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// All cells use an asymmetric "F" texture (white F on blue, red block in the texel corner s = 0, t = 0).
// Page 1: texture matrix, the same square with texcoords 0..1 in every cell, only the GL_TEXTURE matrix differs.
// Page 2: texture coordinates: per-vertex q, texcoord array types.
// Page 3: multitexturing (3 units) and GL_COMBINE.
// Page 4: mipmaps on a floor that recedes into the distance.
// Page 5: compressed textures (paletted, ETC1). A switches pages.
// The expected result is printed on the bottom screen.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cstdio>
#include <cstring>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;

// Texel (x, y) of the "F" image of size x size, y = 0 at the bottom: 0 = blue background, 1 = white F, 2 = red marker
int fTexel(int x, int y, int size) {
    // Glyph on a 16x16 grid, scaled for other sizes
    const int gx = x * 16 / size, gy = y * 16 / size;
    const bool stem = gx >= 4 && gx <= 6 && gy >= 2 && gy <= 13;
    const bool top = gx >= 4 && gx <= 12 && gy >= 11 && gy <= 13;
    const bool middle = gx >= 4 && gx <= 10 && gy >= 7 && gy <= 8;
    const bool marker = gx <= 1 && gy <= 1;
    return marker ? 2 : (stem || top || middle) ? 1 : 0;
}

constexpr GLubyte F_COLORS[3][3] = {{40, 70, 200}, {255, 255, 255}, {230, 30, 30}};

// 16x16 (or 12x12 for the NPOT version) RGB image, row 0 = t = 0 = bottom: upright "F", red block bottom left
GLuint createF(int size) {
    GLubyte pixels[16][16][3];
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++)
            for (int c = 0; c < 3; c++) pixels[y][x][c] = F_COLORS[fTexel(x, y, size)][c];

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

// Page 3 ------------------------------------------------------------------------------------------------

// 16x16 GL_LUMINANCE images
GLuint createLuminance(GLubyte (*value)(int x, int y), GLint filter) {
    GLubyte pixels[16][16];
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) pixels[y][x] = value(x, y);
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 16, 16, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    return id;
}

// "Normal map" of 4 vertical stripes, normals encoded as RGB: +z, 45 degrees toward +x, +x, 45 degrees toward -x
GLuint createNormalStripes() {
    static const GLubyte normals[4][3] = {{128, 128, 255}, {218, 128, 218}, {255, 128, 128}, {38, 128, 218}};
    GLubyte pixels[8][8][3];
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            for (int c = 0; c < 3; c++) pixels[y][x][c] = normals[x / 2][c];
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

struct MultiTextures {
    GLuint f, checker, light, rampUp, rampRight, normals;
};

MultiTextures createMultiTextures(GLuint f) {
    MultiTextures t{};
    t.f = f;
    t.checker = createLuminance([](int x, int y) -> GLubyte { return ((x / 2 + y / 2) % 2) ? 255 : 110; }, GL_NEAREST);
    t.light = createLuminance([](int x, int y) -> GLubyte {
        const float dx = x - 7.5f, dy = y - 7.5f;
        const float v = 1.0f - (dx * dx + dy * dy) / 120.0f;
        return static_cast<GLubyte>(v > 0.15f ? v * 255.0f : 0.15f * 255.0f);
    }, GL_LINEAR);
    t.rampUp = createLuminance([](int, int y) -> GLubyte { return static_cast<GLubyte>(40 + y * 14); }, GL_LINEAR);
    t.rampRight = createLuminance([](int x, int) -> GLubyte { return static_cast<GLubyte>(40 + x * 14); }, GL_LINEAR);
    t.normals = createNormalStripes();
    return t;
}

// Bind `texture` to `unit` with mode `mode` (0: disable the unit)
void setUnit(int unit, GLuint texture, GLenum mode) {
    glActiveTexture(GL_TEXTURE0 + unit);
    if (texture == 0) {
        glDisable(GL_TEXTURE_2D);
    } else {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
    }
    glActiveTexture(GL_TEXTURE0);
}

void resetUnits() {
    for (int unit = 0; unit < 3; unit++) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glDisable(GL_TEXTURE_2D);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 1.0f);
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    glActiveTexture(GL_TEXTURE0);
}

// Square with the same texcoords 0..1 on all three units; `alphaRamp`: vertex alpha 0 left, 1 right
void multiSquare(int column, int row, bool alphaRamp = false) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    static const float corners[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    glBegin(GL_QUADS);
    for (const auto& c : corners) {
        glColor4f(1, 1, 1, alphaRamp ? c[0] : 1.0f);
        for (int unit = 0; unit < 3; unit++) glMultiTexCoord2f(GL_TEXTURE0 + unit, c[0], c[1]);
        glVertex2f(-0.8f + 1.6f * c[0], -0.8f + 1.6f * c[1]);
    }
    glEnd();
    glColor4f(1, 1, 1, 1);
}

void drawMultitexturePage(const MultiTextures& t) {
    // 1: unit 0 F, unit 1 MODULATE with a round light map: F lit in the center, dark corners
    resetUnits();
    setUnit(0, t.f, GL_MODULATE);
    setUnit(1, t.light, GL_MODULATE);
    multiSquare(0, 0);

    // 2: unit 1 ADD with the checker: F washed out, white where the checker is light
    resetUnits();
    setUnit(0, t.f, GL_MODULATE);
    setUnit(1, t.checker, GL_ADD);
    multiSquare(1, 0);

    // 3: COMBINE INTERPOLATE on unit 1: F (previous) on the left to the checker (texture) on the right,
    //    weighted by the vertex alpha
    resetUnits();
    setUnit(0, t.f, GL_MODULATE);
    setUnit(1, t.checker, GL_COMBINE);
    glActiveTexture(GL_TEXTURE1);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_INTERPOLATE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_PRIMARY_COLOR);
    glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_CONSTANT);
    glActiveTexture(GL_TEXTURE0);
    multiSquare(2, 0, true);
    glActiveTexture(GL_TEXTURE1);       // Back to the defaults for the next cells
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_CONSTANT);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
    glActiveTexture(GL_TEXTURE0);

    // 4: COMBINE DOT3_RGB of the normal stripes with a light along +z (constant (0.5, 0.5, 1)):
    //    white, gray, black, gray stripes
    resetUnits();
    setUnit(0, t.normals, GL_COMBINE);
    const GLfloat light[4] = {0.5f, 0.5f, 1.0f, 1.0f};
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, light);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_DOT3_RGB);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
    multiSquare(3, 0);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);

    // 5: COMBINE SUBTRACT 0.5 with RGB_SCALE 2: white F on dark blue, red block
    resetUnits();
    setUnit(0, t.f, GL_COMBINE);
    const GLfloat half[4] = {0.5f, 0.5f, 0.5f, 1.0f};
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, half);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_SUBTRACT);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_CONSTANT);
    glTexEnvf(GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f);
    multiSquare(0, 1);
    glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PREVIOUS);
    glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);

    // 6: three units: F x vertical ramp x horizontal ramp: dark bottom left, bright top right
    resetUnits();
    setUnit(0, t.f, GL_MODULATE);
    setUnit(1, t.rampUp, GL_MODULATE);
    setUnit(2, t.rampRight, GL_MODULATE);
    multiSquare(1, 1);

    // 7: unit 0 off, F only on unit 1, with unit 1's texture matrix flipping t: F upside down
    resetUnits();
    setUnit(1, t.f, GL_MODULATE);
    glActiveTexture(GL_TEXTURE1);
    glMatrixMode(GL_TEXTURE);
    glTranslatef(0, 1, 0);
    glScalef(1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glActiveTexture(GL_TEXTURE0);
    multiSquare(2, 1);

    // 8: texcoord arrays per unit (glClientActiveTexture): F on unit 0 (0..1), checker on unit 1 (0..4)
    resetUnits();
    setUnit(0, t.f, GL_MODULATE);
    setUnit(1, t.checker, GL_MODULATE);
    glViewport(3 * CELL_W, 0, CELL_W, CELL_H);
    static const GLfloat positions[8] = {-0.8f, -0.8f, 0.8f, -0.8f, 0.8f, 0.8f, -0.8f, 0.8f};
    static const GLfloat tc0[8] = {0, 0, 1, 0, 1, 1, 0, 1};
    static const GLfloat tc1[8] = {0, 0, 4, 0, 4, 4, 0, 4};
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, positions);
    glClientActiveTexture(GL_TEXTURE0);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, tc0);
    glClientActiveTexture(GL_TEXTURE1);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, tc1);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTexture(GL_TEXTURE0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    resetUnits();
}

// Page 4 ------------------------------------------------------------------------------------------------

constexpr int MIP_SIZE = 64;    // Levels 64x64 .. 1x1 (0..6), PICA stores 64 .. 8 (0..3)

// Solid color per level: red, green, blue, yellow, then magenta/cyan/white for the levels PICA does not store
constexpr GLubyte LEVEL_COLORS[7][3] = {{220, 40, 40}, {40, 200, 40}, {50, 80, 230}, {230, 220, 40},
                                        {220, 40, 220}, {40, 220, 220}, {255, 255, 255}};

GLuint createLevelColors(int levels) {
    const auto& colors = LEVEL_COLORS;
    static GLubyte pixels[MIP_SIZE * MIP_SIZE * 3];
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int level = 0; level < levels; level++) {
        const int size = MIP_SIZE >> level;
        for (int i = 0; i < size * size; i++)
            for (int c = 0; c < 3; c++) pixels[i * 3 + c] = colors[level][c];
        glTexImage2D(GL_TEXTURE_2D, level, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    }
    return id;
}

// 1 px black/white checker with GL_GENERATE_MIPMAP (or without mipmaps)
GLuint createFineChecker(bool generate, bool rgb565) {
    static GLubyte pixels[MIP_SIZE * MIP_SIZE * 3];
    static GLushort pixels565[MIP_SIZE * MIP_SIZE];
    for (int y = 0; y < MIP_SIZE; y++) {
        for (int x = 0; x < MIP_SIZE; x++) {
            const bool light = (x + y) % 2 == 0;
            for (int c = 0; c < 3; c++) pixels[(y * MIP_SIZE + x) * 3 + c] = light ? 255 : 0;
            pixels565[y * MIP_SIZE + x] = light ? 0xFFFF : 0x001F;     // White / blue
        }
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    if (generate) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (rgb565) glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, MIP_SIZE, MIP_SIZE, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pixels565);
    else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, MIP_SIZE, MIP_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, generate ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST);
    return id;
}

// Green texture with GL_GENERATE_MIPMAP, then level 0 overwritten with red by glTexSubImage2D: all levels red
GLuint createRegenerated() {
    static GLubyte pixels[MIP_SIZE * MIP_SIZE * 3];
    for (int i = 0; i < MIP_SIZE * MIP_SIZE; i++) { pixels[i * 3] = 40; pixels[i * 3 + 1] = 200; pixels[i * 3 + 2] = 40; }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, MIP_SIZE, MIP_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    for (int i = 0; i < MIP_SIZE * MIP_SIZE; i++) { pixels[i * 3] = 220; pixels[i * 3 + 1] = 40; pixels[i * 3 + 2] = 40; }
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, MIP_SIZE, MIP_SIZE, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    return id;
}

struct MipTextures {
    GLuint levels, incomplete, checkerMip, checkerFlat, regenerated, checker565;
};

MipTextures createMipTextures() {
    MipTextures t{};
    t.levels = createLevelColors(7);
    t.incomplete = createLevelColors(3);       // Levels 0..2 only
    t.checkerMip = createFineChecker(true, false);
    t.checkerFlat = createFineChecker(false, false);
    t.regenerated = createRegenerated();
    t.checker565 = createFineChecker(true, true);
    return t;
}

// Floor from z = -1 to z = -40 in a perspective view, texture repeated 4x across and 40x along it
void drawFloor(int column, int row, GLuint texture, GLint minFilter) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1, 0.1, -0.12, 0.12, 0.1, 100.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    if (minFilter != 0) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-2.0f, -1.0f, -1.0f);
    glTexCoord2f(4, 0); glVertex3f(2.0f, -1.0f, -1.0f);
    glTexCoord2f(4, 40); glVertex3f(2.0f, -1.0f, -40.0f);
    glTexCoord2f(0, 40); glVertex3f(-2.0f, -1.0f, -40.0f);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void drawMipmapPage(const MipTextures& t) {
    drawFloor(0, 0, t.levels, GL_LINEAR_MIPMAP_NEAREST);
    drawFloor(1, 0, t.levels, GL_LINEAR_MIPMAP_LINEAR);
    drawFloor(2, 0, t.levels, GL_LINEAR);
    drawFloor(3, 0, t.incomplete, GL_LINEAR_MIPMAP_NEAREST);
    drawFloor(0, 1, t.checkerMip, 0);
    drawFloor(1, 1, t.checkerFlat, 0);
    drawFloor(2, 1, t.regenerated, 0);
    drawFloor(3, 1, t.checker565, 0);
}

// Page 5 ------------------------------------------------------------------------------------------------

// "F" as a paletted texture: palette entries 0..2 for background, F and marker (entrySize bytes each), then the
// 4- or 8-bit indices of all texels without row padding
GLuint createPalettedF(GLenum format, int size, const void* entries, int entrySize) {
    const bool fourBit = format <= GL_PALETTE4_RGB5_A1_OES;
    const int paletteBytes = (fourBit ? 16 : 256) * entrySize;
    static GLubyte data[256 * 4 + 16 * 16];
    std::memset(data, 0, sizeof(data));
    std::memcpy(data, entries, 3 * entrySize);
    GLubyte* indices = data + paletteBytes;
    for (int i = 0; i < size * size; i++) {
        const int index = fTexel(i % size, i / size, size);
        if (fourBit) indices[i / 2] |= index << ((i & 1) ? 0 : 4);     // First texel in the high nibble
        else indices[i] = index;
    }
    const int imageSize = paletteBytes + (fourBit ? (size * size + 1) / 2 : size * size);

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, format, size, size, 0, imageSize, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

// All MIP_SIZE levels in one paletted image (level = -6): palette entry n is the color of level n
GLuint createPalettedLevels() {
    constexpr int LEVELS = 7;
    static GLubyte data[16 * 2 + (MIP_SIZE * MIP_SIZE * 4 / 3 + 16) / 2];
    std::memset(data, 0, sizeof(data));
    GLushort* palette = reinterpret_cast<GLushort*>(data);
    for (int level = 0; level < LEVELS; level++) {
        const GLubyte* c = LEVEL_COLORS[level];
        palette[level] = static_cast<GLushort>((c[0] >> 3) << 11 | (c[1] >> 3) << 6 | (c[2] >> 3) << 1 | 1);
    }
    GLubyte* indices = data + 16 * 2;
    for (int level = 0; level < LEVELS; level++) {
        const int texels = (MIP_SIZE >> level) * (MIP_SIZE >> level);
        for (int i = 0; i < texels; i++) indices[i / 2] |= level << ((i & 1) ? 0 : 4);
        indices += (texels + 1) / 2;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glCompressedTexImage2D(GL_TEXTURE_2D, -(LEVELS - 1), GL_PALETTE4_RGB5_A1_OES, MIP_SIZE, MIP_SIZE, 0,
                           static_cast<GLsizei>(indices - data), data);
    return id;
}

// ETC1 block in individual mode: two sub-blocks with 4-bit colors (columns 0-1 and 2-3, or with flip rows 0-1 and
// 2-3), modifier table 0 with every pixel at +2
void etc1Block(GLubyte* out, const GLubyte* c1, const GLubyte* c2, bool flip) {
    for (int i = 0; i < 3; i++) out[i] = static_cast<GLubyte>((c1[i] * 15 + 127) / 255 << 4 | (c2[i] * 15 + 127) / 255);
    out[3] = flip ? 1 : 0;
    out[4] = out[5] = out[6] = out[7] = 0;
}

// "F" in ETC1 with one solid color per 4x4 block, size a multiple of 4. Block (0, 0) is red at the bottom (rows 0-1)
// and yellow above, block (1, 0) red left and yellow right. The bottom right block is light gray with a black L
// along its bottom and right edges (per-pixel modifiers of table 7)
GLuint createEtc1F(int size) {
    static const GLubyte red[3] = {230, 30, 30}, yellow[3] = {230, 220, 40}, gray[3] = {128, 128, 128};
    static GLubyte data[8 * 8 * 8];
    const int blocks = size / 4;
    for (int by = 0; by < blocks; by++) {
        for (int bx = 0; bx < blocks; bx++) {
            GLubyte* block = data + (by * blocks + bx) * 8;
            const GLubyte* color = F_COLORS[fTexel(bx * 4 + 2, by * 4 + 2, size)];
            etc1Block(block, color, color, false);
            if (by == 0 && bx == 0) etc1Block(block, red, yellow, true);
            if (by == 0 && bx == 1) etc1Block(block, red, yellow, false);
            if (by == 0 && bx == blocks - 1) {
                etc1Block(block, gray, gray, false);
                block[3] = 7 << 5 | 7 << 2;      // Tables 7: +47, +183, -47, -183
                // Pixel (x, y) is bit 4x + y of both index halves; index 3 (-183) is black, 0 (+47) light gray
                unsigned bits = 0;
                for (int x = 0; x < 4; x++)
                    for (int y = 0; y < 4; y++)
                        if (x == 3 || y == 0) bits |= 1u << (4 * x + y);
                block[4] = block[6] = static_cast<GLubyte>(bits >> 8);
                block[5] = block[7] = static_cast<GLubyte>(bits);
            }
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, size, size, 0, blocks * blocks * 8, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return id;
}

// ETC1 levels 64x64 .. 1x1, each a solid level color
GLuint createEtc1Levels() {
    static GLubyte data[16 * 16 * 8];
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    for (int level = 0; level < 7; level++) {
        const int size = MIP_SIZE >> level, blocks = (size + 3) / 4;
        for (int i = 0; i < blocks * blocks; i++) etc1Block(data + i * 8, LEVEL_COLORS[level], LEVEL_COLORS[level], false);
        glCompressedTexImage2D(GL_TEXTURE_2D, level, GL_ETC1_RGB8_OES, size, size, 0, blocks * blocks * 8, data);
    }
    return id;
}

struct CompressedTextures {
    GLuint rgb8, rgba4Npot, rgba8Alpha, paletteLevels, etc1, etc1Npot, etc1Levels;
};

CompressedTextures createCompressedTextures() {
    CompressedTextures t{};
    t.rgb8 = createPalettedF(GL_PALETTE4_RGB8_OES, 16, F_COLORS, 3);

    // RGBA4 entries 0xRGBA, 13x13: an odd texel count, so rows start in the middle of an index byte
    const GLushort rgba4[3] = {0x24CF, 0xFFFF, 0xE22F};
    t.rgba4Npot = createPalettedF(GL_PALETTE4_RGBA4_OES, 13, rgba4, 2);

    // Background alpha 0: cut away by the alpha test
    const GLubyte rgba8[3][4] = {{40, 70, 200, 0}, {255, 255, 255, 255}, {230, 30, 30, 255}};
    t.rgba8Alpha = createPalettedF(GL_PALETTE8_RGBA8_OES, 16, rgba8, 4);

    t.paletteLevels = createPalettedLevels();
    t.etc1 = createEtc1F(32);
    t.etc1Npot = createEtc1F(28);     // 7x7 blocks, padded to 32
    t.etc1Levels = createEtc1Levels();
    return t;
}

void drawCompressedPage(const CompressedTextures& t) {
    resetTextureMatrix();
    drawCell(0, 0, t.rgb8);
    drawCell(1, 0, t.rgba4Npot);

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.5f);
    drawCell(2, 0, t.rgba8Alpha);
    glDisable(GL_ALPHA_TEST);

    drawFloor(3, 0, t.paletteLevels, GL_LINEAR_MIPMAP_NEAREST);

    drawCell(0, 1, t.etc1);

    // ETC1 is stored upside down, the texture matrix must still compose: F upside down and shifted left
    textureMatrix([] {
        glTranslatef(0.25f, 1.0f, 0.0f);
        glScalef(1.0f, -1.0f, 1.0f);
    });
    glBindTexture(GL_TEXTURE_2D, t.etc1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    drawCell(1, 1, t.etc1);
    resetTextureMatrix();

    drawCell(2, 1, t.etc1Npot);
    drawFloor(3, 1, t.etc1Levels, GL_LINEAR_MIPMAP_NEAREST);
}

void printPage(int page) {
    consoleClear();
    std::printf("c3dgl texture test, page %i/5\n\n\n\n\n", page + 1);
    if (page == 4) {
        std::printf("Compressed textures. Expected,\n"
                    "left to right, top row:\n"
                    "- PALETTE4_RGB8: upright F, red\n"
                    "  block bottom left\n"
                    "- PALETTE4_RGBA4 13x13: same F\n"
                    "- PALETTE8_RGBA8, alpha test:\n"
                    "  white F and red block, no blue\n"
                    "- PALETTE4_RGB5_A1 mipmaps: red,\n"
                    "  green, blue, yellow bands\n"
                    "bottom row (ETC1, blocky F):\n"
                    "- upright F; bottom left: red\n"
                    "  stripe under yellow, then red|\n"
                    "  yellow; bottom right: gray with\n"
                    "  black L at bottom/right edge\n"
                    "- matrix: F upside down, shifted\n"
                    "  left by a quarter\n"
                    "- NPOT 28x28: upright F\n"
                    "- ETC1 mipmaps: color bands\n");
    } else if (page == 0) {
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
    } else if (page == 3) {
        std::printf("Mipmaps on a floor. Expected,\n"
                    "left to right, top row:\n"
                    "- MIPMAP_NEAREST: color bands red,\n"
                    "  green, blue, yellow (far)\n"
                    "- MIPMAP_LINEAR: same, blended\n"
                    "- GL_LINEAR: only level 0 (red)\n"
                    "- incomplete levels: texturing\n"
                    "  off, plain white floor\n"
                    "bottom row:\n"
                    "- generated mipmaps: checker\n"
                    "  fades to smooth gray\n"
                    "- no mipmaps: flickering moire\n"
                    "  in the distance\n"
                    "- regenerated after\n"
                    "  glTexSubImage2D: all red\n"
                    "- RGB565 generated: blue/white\n"
                    "  fades to smooth light blue\n");
    } else if (page == 2) {
        std::printf("Multitexturing. Expected,\n"
                    "left to right, top row:\n"
                    "- F x round light: F bright in\n"
                    "  the center, dark corners\n"
                    "- ADD checker: F washed out,\n"
                    "  white checker squares\n"
                    "- INTERPOLATE by vertex alpha:\n"
                    "  F left, checker right\n"
                    "- DOT3: white, gray, black,\n"
                    "  gray vertical stripes\n"
                    "bottom row:\n"
                    "- SUBTRACT 0.5, scale 2: white F\n"
                    "  on dark blue, red block\n"
                    "- 3 units: F dark bottom left,\n"
                    "  bright top right\n"
                    "- unit 1 only, own texture\n"
                    "  matrix: F upside down\n"
                    "- array texcoords per unit:\n"
                    "  F with a fine checker on it\n");
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

static void printStats() {
    // Rows 2-5 of the console: timings of the last frame, frames per second over the last second; the cursor stays
    // where the text ended
    std::printf("\x1b[s");
    std::printf("\x1b[2;1HCPU:     %6.2fms\x1b[K", C3D_GetProcessingTime());
    std::printf("\x1b[3;1HGPU:     %6.2fms\x1b[K", C3D_GetDrawingTime());
    std::printf("\x1b[4;1HCmdBuf:  %6.2f%%\x1b[K", C3D_GetCmdBufUsage()*100.0f);
    static u64 fpsStart;
    static int fpsFrames;
    static float fps;
    const u64 now = osGetTime();
    if (fpsStart == 0) fpsStart = now;
    fpsFrames++;
    if (now - fpsStart >= 1000) {
        fps = fpsFrames*1000.0f/(now - fpsStart);
        fpsFrames = 0;
        fpsStart = now;
    }
    std::printf("\x1b[5;1HFPS:     %6.2f\x1b[K", fps);
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

    const GLuint texPot = createF(16), texNpot = createF(12);
    const MultiTextures multi = createMultiTextures(texPot);
    const MipTextures mips = createMipTextures();
    const CompressedTextures compressed = createCompressedTextures();
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    float time = 0.0f;
    int page = 0;
    printPage(page);

    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) {
            page = (page + 1) % 5;
            printPage(page);
        }

        glClear(GL_COLOR_BUFFER_BIT);
        if (page == 4) {
            drawCompressedPage(compressed);
            c3dglSwapBuffers();
            continue;
        }
        if (page == 3) {
            drawMipmapPage(mips);
            c3dglSwapBuffers();
            continue;
        }
        if (page == 2) {
            drawMultitexturePage(multi);
            c3dglSwapBuffers();
            continue;
        }
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
