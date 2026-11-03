// c3dgl example: per-fragment operations, one cell each on the top screen (4x2 grid, 100x120 px per cell).
// Page 1: alpha test, texture environment. Page 2: stencil, glClear with scissor/masks. A switches pages.
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

// Stencil and clear tests (page 2). The frame starts with stencil cleared to 3 by the memory fill

// Scissor box of the cell, inset by `inset` px
void scissorCell(int column, int row, int inset) {
    glEnable(GL_SCISSOR_TEST);
    glScissor(column * CELL_W + inset, (ROWS - 1 - row) * CELL_H + inset, CELL_W - 2 * inset, CELL_H - 2 * inset);
}

// Clear only the stencil of the cell (quad path: stencil without depth)
void clearCellStencil(int column, int row) {
    scissorCell(column, row, 0);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
}

void drawDisc(float x, float y, float r, float z = 0.0f) {
    glBegin(GL_TRIANGLE_FAN);
    glVertex3f(x, y, z);
    for (int i = 0; i <= 32; i++) {
        const float a = 6.2831853f * i / 32;
        glVertex3f(x + r * std::cos(a), y + r * std::sin(a), z);
    }
    glEnd();
}

void drawRect(float x0, float y0, float x1, float y1, float z = 0.0f) {
    glBegin(GL_QUADS);
    glVertex3f(x0, y0, z); glVertex3f(x1, y0, z); glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
    glEnd();
}

// Blue/white vertical stripes over the whole cell
void drawStripes(float z = 0.0f) {
    for (int i = 0; i < 10; i++) {
        if (i % 2) glColor3f(1.0f, 1.0f, 1.0f);
        else glColor3f(0.2f, 0.3f, 0.9f);
        drawRect(-1.0f + 0.2f * i, -1.2f, -0.8f + 0.2f * i, 1.2f, z);
    }
}

// Stencil = 1 inside a disc, nothing drawn to color
void writeStencilDisc() {
    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    drawDisc(0.0f, 0.0f, 0.7f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

// EQUAL 1: stripes only inside the disc
void drawStencilInside() {
    writeStencilDisc();
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    drawStripes();
    glDisable(GL_STENCIL_TEST);
}

// EQUAL 3 (the value of the frame clear): stripes only outside the disc
void drawStencilClearValue() {
    writeStencilDisc();
    glStencilFunc(GL_EQUAL, 3, 0xFF);
    drawStripes();
    glDisable(GL_STENCIL_TEST);
}

// INCR with three overlapping discs, then one color per count: 1 blue, 2 green, 3 yellow
void drawStencilCount(int column, int row) {
    clearCellStencil(column, row);
    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
    drawDisc(-0.3f, 0.25f, 0.5f);
    drawDisc(0.3f, 0.25f, 0.5f);
    drawDisc(0.0f, -0.25f, 0.5f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    const float colors[3][3] = {{0.2f, 0.3f, 0.9f}, {0.2f, 0.8f, 0.3f}, {1.0f, 0.9f, 0.2f}};
    for (int count = 1; count <= 3; count++) {
        glStencilFunc(GL_EQUAL, count, 0xFF);
        glColor3f(colors[count - 1][0], colors[count - 1][1], colors[count - 1][2]);
        drawRect(-1.0f, -1.2f, 1.0f, 1.2f);
    }
    glDisable(GL_STENCIL_TEST);
}

// INVERT through write mask 0x0F on two overlapping squares, then EQUAL 0x0F: magenta squares, empty overlap.
// Without the write mask the value would be 0xFF and nothing would show
void drawStencilInvert(int column, int row) {
    clearCellStencil(column, row);
    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(0x0F);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    drawRect(-0.8f, -0.2f, 0.3f, 0.9f);
    drawRect(-0.3f, -0.9f, 0.8f, 0.2f);
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);

    glStencilFunc(GL_EQUAL, 0x0F, 0xFF);
    glColor3f(0.9f, 0.2f, 0.9f);
    drawRect(-1.0f, -1.2f, 1.0f, 1.2f);
    glDisable(GL_STENCIL_TEST);
}

// glClear with a scissor box 20 px inside the cell: red box with a background-colored border
void drawScissorClear(int column, int row) {
    scissorCell(column, row, 20);
    glClearColor(0.9f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glDisable(GL_SCISSOR_TEST);
}

// Red left/blue right half, then a green-only clear to white 20 px inside the cell:
// red/blue frame around yellow (left) and cyan (right)
void drawColorMaskClear(int column, int row) {
    glColor3f(1.0f, 0.0f, 0.0f); drawRect(-1.0f, -1.2f, 0.0f, 1.2f);
    glColor3f(0.0f, 0.0f, 1.0f); drawRect(0.0f, -1.2f, 1.0f, 1.2f);

    scissorCell(column, row, 20);
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
}

// Depth-only clear keeps stencil: stencil disc, green quad in front, depth cleared, stripes behind with
// EQUAL 1 -> stripes in the disc over green. Uncleared depth or lost stencil: only green
void drawDepthClearKeepsStencil(int column, int row) {
    writeStencilDisc();
    glDisable(GL_STENCIL_TEST);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.2f, 0.7f, 0.3f);
    drawRect(-1.0f, -1.2f, 1.0f, 1.2f, 0.5f);

    scissorCell(column, row, 0);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    drawStripes(-0.5f);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
}

// Stencil-only clear keeps depth: small green square in front, stencil cleared to 0, red quad behind with
// EQUAL 0 -> red around the green square. Lost depth: all red; uncleared stencil: only green
void drawStencilClearKeepsDepth(int column, int row) {
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0.2f, 0.7f, 0.3f);
    drawRect(-0.4f, -0.4f, 0.4f, 0.4f, 0.5f);

    clearCellStencil(column, row);

    glEnable(GL_STENCIL_TEST);
    glStencilFunc(GL_EQUAL, 0, 0xFF);
    glColor3f(0.9f, 0.1f, 0.1f);
    drawRect(-1.0f, -1.2f, 1.0f, 1.2f, -0.5f);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DEPTH_TEST);
}

void printPage(int page) {
    consoleClear();
    std::printf("c3dgl fragment test, page %i/2\n\n\n\n\n", page + 1);
    if (page == 0) {
        std::printf("Expected on the top screen,\n"
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
                    "  ADD: white/dark red checker\n");
    } else {
        std::printf("Expected on the top screen,\n"
                    "left to right, top row (stencil):\n"
                    "- EQUAL: stripes INSIDE a disc\n"
                    "- clear value: stripes OUTSIDE\n"
                    "  the disc\n"
                    "- INCR: 3 discs, blue/green/yellow\n"
                    "  for 1/2/3 overlaps\n"
                    "- INVERT + write mask: 2 magenta\n"
                    "  squares, overlap empty\n"
                    "bottom row (glClear):\n"
                    "- scissor: red box, dark border\n"
                    "- color mask: yellow | cyan in a\n"
                    "  red | blue frame\n"
                    "- depth only: stripes in a disc\n"
                    "  on green\n"
                    "- stencil only: green square in\n"
                    "  a red cell\n");
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

    const Textures textures = {createSprite(), createAlphaDisc(), createChecker(GL_RGB), createChecker(GL_LUMINANCE)};
    const GLuint sprite = textures.sprite;
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    int page = 0;
    printPage(page);

    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) {
            page = 1 - page;
            printPage(page);
        }

        if (page == 0) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            beginCell(0, 0); drawAlphaGradient(GL_GREATER);
            beginCell(1, 0); drawAlphaGradient(GL_LESS);
            beginCell(2, 0); drawCutout(sprite);
            beginCell(3, 0); drawCutoutDepth(sprite);
            beginCell(0, 1); drawModulate(textures);
            beginCell(1, 1); drawReplace(textures);
            beginCell(2, 1); drawDecal(textures);
            beginCell(3, 1); drawBlendAdd(textures);
        } else {
            // Memory fill path: no scissor, full masks, depth + stencil together
            glClearStencil(3);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

            beginCell(0, 0); drawStencilInside();
            beginCell(1, 0); drawStencilClearValue();
            beginCell(2, 0); drawStencilCount(2, 0);
            beginCell(3, 0); drawStencilInvert(3, 0);
            beginCell(0, 1); drawScissorClear(0, 1);
            beginCell(1, 1); drawColorMaskClear(1, 1);
            beginCell(2, 1); drawDepthClearKeepsStencil(2, 1);
            beginCell(3, 1); drawStencilClearKeepsDepth(3, 1);
        }

        c3dglSwapBuffers();
    }

    const GLuint ids[] = {textures.sprite, textures.alphaDisc, textures.rgbChecker, textures.luminanceChecker};
    glDeleteTextures(4, ids);
    c3dglClose();
    gfxExit();
    return 0;
}
