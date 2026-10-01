// c3dgl example: per-fragment operations, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// Top row: alpha test, bottom row: texture environment. The expected result is printed on the bottom screen.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;
constexpr int SPRITE_SIZE = 32;
constexpr int CHECKER_SIZE = 8;

// Cell (column, row from the top) as viewport; coordinates inside are x -1..1, y -1.2..1.2 (square pixels).
// z = 1 is nearest
void beginCell(int column, int row) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.2, 1.2, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

void setTextureParams(GLint filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

GLuint createTexture(GLenum format, int size, const void* pixels, GLint filter) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, format, size, size, 0, format, GL_UNSIGNED_BYTE, pixels);
    setTextureParams(filter);
    return id;
}

float spriteAlpha(int x, int y) {
    const float dx = x + 0.5f - SPRITE_SIZE / 2, dy = y + 0.5f - SPRITE_SIZE / 2;
    const float a = 1.0f - std::sqrt(dx * dx + dy * dy) / (SPRITE_SIZE / 2);
    return a > 0.0f ? a : 0.0f;
}

// Orange sprite whose alpha falls off linearly from 1 in the center to 0 at the edge:
// alpha >= 0.5 is a disc of half the sprite size
GLuint createSprite() {
    GLubyte pixels[SPRITE_SIZE][SPRITE_SIZE][4];
    for (int y = 0; y < SPRITE_SIZE; y++) {
        for (int x = 0; x < SPRITE_SIZE; x++) {
            pixels[y][x][0] = 255;
            pixels[y][x][1] = 140;
            pixels[y][x][2] = 0;
            pixels[y][x][3] = static_cast<GLubyte>(spriteAlpha(x, y) * 255.0f);
        }
    }
    return createTexture(GL_RGBA, SPRITE_SIZE, pixels, GL_LINEAR);
}

// Alpha-only (GL_ALPHA) version of the sprite: just the soft disc
GLuint createAlphaDisc() {
    GLubyte pixels[SPRITE_SIZE][SPRITE_SIZE];
    for (int y = 0; y < SPRITE_SIZE; y++) {
        for (int x = 0; x < SPRITE_SIZE; x++) pixels[y][x] = static_cast<GLubyte>(spriteAlpha(x, y) * 255.0f);
    }
    return createTexture(GL_ALPHA, SPRITE_SIZE, pixels, GL_LINEAR);
}

// White/black 2x2 px checker, 4x4 squares, as GL_RGB or GL_LUMINANCE
GLuint createChecker(GLenum format) {
    GLubyte pixels[CHECKER_SIZE * CHECKER_SIZE * 3];
    const int bpp = (format == GL_RGB) ? 3 : 1;
    for (int y = 0; y < CHECKER_SIZE; y++) {
        for (int x = 0; x < CHECKER_SIZE; x++) {
            const GLubyte v = ((x / 2) + (y / 2)) % 2 == 0 ? 255 : 0;
            for (int i = 0; i < bpp; i++) pixels[(y * CHECKER_SIZE + x) * bpp + i] = v;
        }
    }
    return createTexture(format, CHECKER_SIZE, pixels, GL_NEAREST);
}

void drawOutline(float size) {
    glColor3f(0.5f, 0.5f, 0.5f);
    glBegin(GL_LINE_LOOP);
    glVertex2f(-size, -size); glVertex2f(size, -size); glVertex2f(size, size); glVertex2f(-size, size);
    glEnd();
}

// White quad with vertex alpha 0 (left) -> 1 (right), alpha tested with func/0.5, inside a gray outline
void drawAlphaGradient(GLenum func) {
    drawOutline(0.82f);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(func, 0.5f);
    glBegin(GL_QUADS);
    glColor4f(1, 1, 1, 0); glVertex2f(-0.8f, -0.8f);
    glColor4f(1, 1, 1, 1); glVertex2f( 0.8f, -0.8f);
    glColor4f(1, 1, 1, 1); glVertex2f( 0.8f,  0.8f);
    glColor4f(1, 1, 1, 0); glVertex2f(-0.8f,  0.8f);
    glEnd();
    glDisable(GL_ALPHA_TEST);
}

// Textured quad from (x0, y0) to (x1, y1) in the current color
void drawTexturedQuad(GLuint texture, float x0, float y0, float x1, float y1, float z = 0.0f) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(x0, y0, z);
    glTexCoord2f(1, 0); glVertex3f(x1, y0, z);
    glTexCoord2f(1, 1); glVertex3f(x1, y1, z);
    glTexCoord2f(0, 1); glVertex3f(x0, y1, z);
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

void drawSprite(GLuint sprite, float size, float z) {
    glColor3f(1, 1, 1);
    drawTexturedQuad(sprite, -size, -size, size, size, z);
}

// Cutout sprite (alpha >= 0.5) over blue/white stripes, no blending: a hard-edged orange disc
void drawCutout(GLuint sprite) {
    glBegin(GL_QUADS);
    for (int i = 0; i < 8; i++) {
        const float x = -0.8f + 0.2f * i;
        if (i % 2) glColor3f(1.0f, 1.0f, 1.0f);
        else glColor3f(0.2f, 0.3f, 0.9f);
        glVertex2f(x, -0.8f); glVertex2f(x + 0.2f, -0.8f); glVertex2f(x + 0.2f, 0.8f); glVertex2f(x, 0.8f);
    }
    glEnd();

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 0.5f);
    drawSprite(sprite, 0.8f, 0.0f);
    glDisable(GL_ALPHA_TEST);
}

// Discarded fragments must not write depth: the cutout sprite is drawn in front FIRST, then a green
// quad behind it. The green must show all around the disc (no background-colored square)
void drawCutoutDepth(GLuint sprite) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GEQUAL, 0.5f);
    drawSprite(sprite, 0.8f, 0.5f);
    glDisable(GL_ALPHA_TEST);

    glColor3f(0.2f, 0.7f, 0.3f);
    glBegin(GL_QUADS);
    glVertex3f(-0.8f, -0.8f, -0.5f); glVertex3f(0.8f, -0.8f, -0.5f);
    glVertex3f(0.8f, 0.8f, -0.5f); glVertex3f(-0.8f, 0.8f, -0.5f);
    glEnd();

    glDisable(GL_DEPTH_TEST);
}

// Texture environment: a quad in the top half and one in the bottom half of the cell
void drawTopBottom(GLenum mode, GLuint top, GLuint bottom, bool blendBottom) {
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
    drawTexturedQuad(top, -0.8f, 0.1f, 0.8f, 1.1f);
    if (blendBottom) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    drawTexturedQuad(bottom, -0.8f, -1.1f, 0.8f, -0.1f);
    glDisable(GL_BLEND);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

struct Textures {
    GLuint sprite, alphaDisc, rgbChecker, luminanceChecker;
};

// GL_MODULATE, green vertex color: RGB checker -> green/black; GL_ALPHA disc -> soft green disc (not black)
void drawModulate(const Textures& t) {
    glColor3f(0.2f, 0.9f, 0.2f);
    drawTopBottom(GL_MODULATE, t.rgbChecker, t.alphaDisc, true);
}

// GL_REPLACE, green vertex color: RGB checker -> white/black (no green); RGBA sprite -> soft orange disc
void drawReplace(const Textures& t) {
    glColor3f(0.2f, 0.9f, 0.2f);
    drawTopBottom(GL_REPLACE, t.rgbChecker, t.sprite, true);
}

// GL_DECAL, green vertex color, no blending: opaque green square with the orange disc fading into it
void drawDecal(const Textures& t) {
    glColor3f(0.2f, 0.9f, 0.2f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    drawTexturedQuad(t.sprite, -0.8f, -0.8f, 0.8f, 0.8f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

// Luminance checker. GL_BLEND, yellow vertex color, blue env color: blue (white squares)/yellow (black squares).
// GL_ADD, dark red vertex color: white/dark red
void drawBlendAdd(const Textures& t) {
    const GLfloat blue[4] = {0.1f, 0.3f, 1.0f, 1.0f};
    glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, blue);
    glColor3f(1.0f, 0.9f, 0.1f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
    drawTexturedQuad(t.luminanceChecker, -0.8f, 0.1f, 0.8f, 1.1f);

    glColor3f(0.5f, 0.0f, 0.0f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
    drawTexturedQuad(t.luminanceChecker, -0.8f, -1.1f, 0.8f, -0.1f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
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

    std::printf("c3dgl fragment test\n\n"
                "Expected on the top screen,\n"
                "left to right, top row (alpha test):\n"
                "- GREATER 0.5: white RIGHT half\n"
                "  of the gray outline\n"
                "- LESS 0.5: white LEFT half\n"
                "- cutout: hard orange disc on\n"
                "  blue/white stripes\n"
                "- cutout + depth: orange disc,\n"
                "  green all around it\n"
                "bottom row (texture env):\n"
                "- MODULATE: green/black checker;\n"
                "  soft green disc (alpha tex)\n"
                "- REPLACE: white/black checker;\n"
                "  soft orange disc\n"
                "- DECAL: orange disc fading\n"
                "  into an opaque green square\n"
                "- BLEND: blue/yellow checker;\n"
                "  ADD: white/dark red checker\n\n"
                "START: exit\n");

    const Textures textures = {createSprite(), createAlphaDisc(), createChecker(GL_RGB), createChecker(GL_LUMINANCE)};
    const GLuint sprite = textures.sprite;
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        beginCell(0, 0); drawAlphaGradient(GL_GREATER);
        beginCell(1, 0); drawAlphaGradient(GL_LESS);
        beginCell(2, 0); drawCutout(sprite);
        beginCell(3, 0); drawCutoutDepth(sprite);
        beginCell(0, 1); drawModulate(textures);
        beginCell(1, 1); drawReplace(textures);
        beginCell(2, 1); drawDecal(textures);
        beginCell(3, 1); drawBlendAdd(textures);

        c3dglSwapBuffers();
    }

    const GLuint ids[] = {textures.sprite, textures.alphaDisc, textures.rgbChecker, textures.luminanceChecker};
    glDeleteTextures(4, ids);
    c3dglClose();
    gfxExit();
    return 0;
}
