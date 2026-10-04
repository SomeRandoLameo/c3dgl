// c3dgl example: line and polygon stipple. Left: stippled lines (dashes, factor 2, dash-dot, dots, a wide line), a
// turning star drawn as one line loop (the pattern runs on around the corners) and two rectangles with complementary
// halftone patterns that overlap into one solid rectangle. Right, in perspective: a cube with screen-door transparency
// (polygon stipple, textured) circles an opaque cube whose edges are outlined with a dashed glPolygonMode(GL_LINE)
// pass. The stipple pattern is fixed to the window, so it does not move with the turning cube. A: pause.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>

namespace {

void cube() {
    static const float faces[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},       {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},       {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},       {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
    };
    static const float shade[6] = {1.0f, 0.55f, 0.8f, 0.7f, 0.9f, 0.45f};
    static const float tex[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    GLfloat color[4];
    glGetFloatv(GL_CURRENT_COLOR, color);
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glColor3f(color[0] * shade[f], color[1] * shade[f], color[2] * shade[f]);
        for (int v = 0; v < 4; v++) {
            glTexCoord2fv(tex[v]);
            glVertex3fv(faces[f][v]);
        }
    }
    glEnd();
    glColor4fv(color);
}

// 32x32 patterns, rows from the bottom, most significant bit first
void checkerPattern(GLubyte pattern[128], bool odd) {
    for (int y = 0; y < 32; y++)
        for (int i = 0; i < 4; i++) pattern[4 * y + i] = ((y & 1) != odd) ? 0x55 : 0xAA;
}

// Halftone dots of 4x4 cells: the cell's center 2x2 pixels (or everything else)
void dotPattern(GLubyte pattern[128], bool inverse) {
    for (int y = 0; y < 32; y++) {
        const bool middle = ((y & 3) == 1) || ((y & 3) == 2);
        for (int i = 0; i < 4; i++) {
            const GLubyte dots = middle ? 0x66 : 0x00;
            pattern[4 * y + i] = inverse ? static_cast<GLubyte>(~dots) : dots;
        }
    }
}

void line(float x0, float y0, float x1, float y1) {
    glBegin(GL_LINES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glEnd();
}

void lines(float angle) {
    glEnable(GL_LINE_STIPPLE);
    glColor3f(1.0f, 1.0f, 1.0f);
    glLineStipple(1, 0x00FF);
    line(10.0f, 225.5f, 190.0f, 225.5f);
    glColor3f(1.0f, 0.8f, 0.3f);
    glLineStipple(2, 0x0F0F);
    line(10.0f, 213.5f, 190.0f, 213.5f);
    glColor3f(0.4f, 1.0f, 0.5f);
    glLineStipple(1, 0x1C47);
    line(10.0f, 201.5f, 190.0f, 201.5f);
    glColor3f(0.5f, 0.7f, 1.0f);
    glLineStipple(1, 0xAAAA);
    line(10.0f, 189.5f, 190.0f, 189.5f);
    glColor3f(1.0f, 0.4f, 0.4f);
    glLineWidth(3.0f);
    glLineStipple(3, 0x3F3F);
    line(10.0f, 176.5f, 190.0f, 176.5f);
    glLineWidth(1.0f);

    // Star: one loop, smooth colors
    glLineStipple(2, 0x3F07);
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < 5; i++) {
        const float a = (angle + i * 144.0f) * 3.14159265f / 180.0f;
        glColor3f(0.6f + 0.4f * std::cos(a), 0.6f + 0.4f * std::sin(a), 1.0f);
        glVertex2f(70.0f + 55.0f * std::sin(a), 100.0f + 55.0f * std::cos(a));
    }
    glEnd();
    glDisable(GL_LINE_STIPPLE);
}

void halftone() {
    GLubyte pattern[128];
    glEnable(GL_POLYGON_STIPPLE);
    dotPattern(pattern, false);
    glPolygonStipple(pattern);
    glColor3f(1.0f, 0.85f, 0.2f);
    glRectf(135.0f, 20.0f, 185.0f, 110.0f);
    dotPattern(pattern, true);
    glPolygonStipple(pattern);
    glColor3f(0.2f, 0.5f, 1.0f);
    glRectf(150.0f, 35.0f, 195.0f, 160.0f);     // Overlap: the yellow dots and their blue complement fill it solid
    glDisable(GL_POLYGON_STIPPLE);
}

void cubes(float angle, GLuint texture) {
    glViewport(200, 0, 200, 240);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.42, 0.42, -0.5, 0.5, 1.0, 30.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -9.0f);
    glRotatef(20.0f, 1.0f, 0.0f, 0.0f);

    // Opaque cube with a dashed outline (polygon offset keeps the outline in front of its faces)
    glPushMatrix();
    glRotatef(angle * 0.7f, 0.3f, 1.0f, 0.0f);
    glScalef(0.7f, 0.7f, 0.7f);
    glColor3f(0.3f, 0.5f, 0.9f);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    cube();
    glDisable(GL_POLYGON_OFFSET_FILL);
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(1, 0x0F0F);
    glColor3f(1.0f, 1.0f, 1.0f);
    cube();
    glDisable(GL_LINE_STIPPLE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glPopMatrix();

    // Screen door cube, textured: half of its pixels are left out
    GLubyte pattern[128];
    checkerPattern(pattern, false);
    glPolygonStipple(pattern);
    glEnable(GL_POLYGON_STIPPLE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPushMatrix();
    glRotatef(angle, 0.0f, 1.0f, 0.0f);
    glTranslatef(0.0f, 0.0f, 2.2f);
    glRotatef(angle * 2.0f, 1.0f, 0.4f, 0.2f);
    glScalef(0.75f, 0.75f, 0.75f);
    glColor3f(1.0f, 0.6f, 0.2f);
    cube();
    glPopMatrix();
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_POLYGON_STIPPLE);
    glViewport(0, 0, C3DGL_TOP_SCREEN_WIDTH, C3DGL_SCREEN_HEIGHT);
}

void printStats() {
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

}  // namespace

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

    // 4x4 checker of light and dark texels
    GLubyte texels[16];
    for (int i = 0; i < 16; i++) texels[i] = (((i & 3) + (i >> 2)) & 1) ? 255 : 110;
    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 4, 4, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, texels);

    glClearColor(0.08f, 0.08f, 0.15f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    std::printf("c3dgl stipple\n\n\n\n\n"
                "Left: stippled lines (dashes,\n"
                "factor 2, dash-dot, dots, wide),\n"
                "a star as one line loop and two\n"
                "halftone rectangles whose dots\n"
                "fill each other in where they\n"
                "overlap (solid there).\n\n"
                "Right: a textured screen door\n"
                "cube (polygon stipple, pattern\n"
                "fixed to the window) circles an\n"
                "opaque cube with dashed edges.\n\n"
                "A: pause   START: exit\n");

    bool paused = false;
    float angle = 0.0f;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 down = hidKeysDown();
        if (down & KEY_START) break;
        if (down & KEY_A) paused = !paused;
        if (!paused) angle += 0.5f;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, C3DGL_TOP_SCREEN_WIDTH, 0.0, C3DGL_SCREEN_HEIGHT, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        lines(angle);
        halftone();
        glEnable(GL_DEPTH_TEST);
        cubes(angle, texture);
        c3dglSwapBuffers();
    }

    glDeleteTextures(1, &texture);
    c3dglClose();
    gfxExit();
    return 0;
}
