// c3dgl example: per-fragment operations, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// Top row: alpha test. The expected result is printed on the bottom screen.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;
constexpr int SPRITE_SIZE = 32;

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

// Orange sprite whose alpha falls off linearly from 1 in the center to 0 at the edge:
// alpha >= 0.5 is a disc of half the sprite size
GLuint createSprite() {
    GLubyte pixels[SPRITE_SIZE][SPRITE_SIZE][4];
    for (int y = 0; y < SPRITE_SIZE; y++) {
        for (int x = 0; x < SPRITE_SIZE; x++) {
            const float dx = x + 0.5f - SPRITE_SIZE / 2, dy = y + 0.5f - SPRITE_SIZE / 2;
            const float a = 1.0f - std::sqrt(dx * dx + dy * dy) / (SPRITE_SIZE / 2);
            pixels[y][x][0] = 255;
            pixels[y][x][1] = 140;
            pixels[y][x][2] = 0;
            pixels[y][x][3] = static_cast<GLubyte>(a > 0.0f ? a * 255.0f : 0.0f);
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SPRITE_SIZE, SPRITE_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
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

void drawSprite(GLuint sprite, float size, float z) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, sprite);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0); glVertex3f(-size, -size, z);
    glTexCoord2f(1, 0); glVertex3f( size, -size, z);
    glTexCoord2f(1, 1); glVertex3f( size,  size, z);
    glTexCoord2f(0, 1); glVertex3f(-size,  size, z);
    glEnd();
    glDisable(GL_TEXTURE_2D);
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
                "  green all around it\n\n"
                "START: exit\n");

    const GLuint sprite = createSprite();
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        beginCell(0, 0); drawAlphaGradient(GL_GREATER);
        beginCell(1, 0); drawAlphaGradient(GL_LESS);
        beginCell(2, 0); drawCutout(sprite);
        beginCell(3, 0); drawCutoutDepth(sprite);

        c3dglSwapBuffers();
    }

    glDeleteTextures(1, &sprite);
    c3dglClose();
    gfxExit();
    return 0;
}
