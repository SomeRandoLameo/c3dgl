// c3dgl example: drawing pixels. A spinning cube with a text label at one of its corners (glRasterPos in 3D, glBitmap
// text depth tested against the cube), its reflection copied below it with glCopyPixels (zoom 1 x -0.5), and an animated
// glDrawPixels image with a pulsing glPixelZoom next to its mirror image (zoom -1). The text is the console font as
// glBitmap display lists, drawn with glCallLists. A: pause the animation.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int IMAGE_SIZE = 64;

// One display list per character (32..127) with the glBitmap of the console font, advancing 8 pixels
GLuint createFont() {
    const ConsoleFont &font = consoleGetDefault()->font;
    const GLuint base = glGenLists(128);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int c = 32; c < 128; c++) {
        // Console font rows go down, bitmap rows up
        GLubyte rows[8];
        const u8 *glyph = font.gfx + 8 * (c - font.asciiOffset);
        for (int r = 0; r < 8; r++) rows[r] = glyph[7 - r];
        glNewList(base + c, GL_COMPILE);
        glBitmap(8, 8, 0.0f, 0.0f, 8.0f, 0.0f, rows);
        glEndList();
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    return base;
}

// Text at the current raster position
void text(GLuint font, const char *s) {
    glListBase(font);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE, s);
}

void windowText(GLuint font, int x, int y, const char *s) {
    glRasterPos2i(x, y);
    text(font, s);
}

// Plasma, RGB: the sum of three sine waves (per column, row and diagonal) through a fixed palette
void makeImage(GLubyte *image, float t) {
    static GLubyte palette[256][3];
    static bool paletteReady = false;
    if (!paletteReady) {
        for (int i = 0; i < 256; i++) {
            const float v = (i / 255.0f * 6.0f - 3.0f) * 2.0f;
            for (int c = 0; c < 3; c++) palette[i][c] = static_cast<GLubyte>(127.5f + 127.5f * std::sin(v + c * 2.1f));
        }
        paletteReady = true;
    }
    float waveX[IMAGE_SIZE], waveY[IMAGE_SIZE], waveXY[2 * IMAGE_SIZE];
    for (int i = 0; i < IMAGE_SIZE; i++) {
        waveX[i] = std::sin(i * 0.2f + t);
        waveY[i] = std::sin(i * 0.15f - t * 1.3f);
    }
    for (int i = 0; i < 2 * IMAGE_SIZE; i++) waveXY[i] = std::sin(i * 0.1f + t * 0.7f);
    for (int y = 0; y < IMAGE_SIZE; y++) {
        for (int x = 0; x < IMAGE_SIZE; x++) {
            const float v = waveX[x] + waveY[y] + waveXY[x + y];      // -3..3
            const GLubyte *c = palette[static_cast<int>((v + 3.0f) * (255.0f / 6.0f))];
            GLubyte *p = image + 3 * (y * IMAGE_SIZE + x);
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
        }
    }
}

void cube() {
    static const float faces[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},       {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},       {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},       {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
    };
    static const GLubyte colors[6][3] = {
        {230, 60, 60}, {60, 200, 80}, {70, 110, 230}, {230, 200, 50}, {200, 80, 220}, {60, 210, 210},
    };
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glColor3ubv(colors[f]);
        for (int v = 0; v < 4; v++) glVertex3fv(faces[f][v]);
    }
    glEnd();
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

    const GLuint font = createFont();
    static GLubyte image[IMAGE_SIZE * IMAGE_SIZE * 3];
    glClearColor(0.08f, 0.08f, 0.15f, 1.0f);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    std::printf("c3dgl pixels\n\n\n\n\n"
                "Left: a cube with a glBitmap label\n"
                "at a corner (glRasterPos in 3D,\n"
                "depth tested), its reflection by\n"
                "glCopyPixels (zoom 1 x -0.5).\n"
                "Right: glDrawPixels with a pulsing\n"
                "zoom, mirrored by zoom -1.\n"
                "Text: glBitmap display lists.\n\n"
                "A: pause\n"
                "START: exit\n\n");
    std::printf("%s\n", glGetError() == GL_NO_ERROR ? "No GL errors" : "GL ERROR");

    bool paused = false;
    float t = 0.0f;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) paused = !paused;
        if (!paused) t += 1.0f / 60.0f;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // The cube in the upper left, in perspective
        glViewport(0, 0, C3DGL_TOP_SCREEN_WIDTH, C3DGL_SCREEN_HEIGHT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const double aspect = static_cast<double>(C3DGL_TOP_SCREEN_WIDTH) / C3DGL_SCREEN_HEIGHT;
        glFrustum(-aspect * 0.5, aspect * 0.5, -0.5, 0.5, 1.0, 20.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glTranslatef(-1.6f, 1.0f, -6.0f);
        glRotatef(t * 40.0f, 0.3f, 1.0f, 0.1f);
        glScalef(0.6f, 0.6f, 0.6f);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        cube();

        // Label just outside a corner: hidden by the cube when the corner turns away
        glColor3ub(255, 255, 255);
        glRasterPos3f(1.15f, 1.15f, 1.15f);
        text(font, "<corner");
        glDisable(GL_DEPTH_TEST);

        // Window coordinates from here on
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, C3DGL_TOP_SCREEN_WIDTH, 0.0, C3DGL_SCREEN_HEIGHT, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        // Reflection: the band above y = 112 copied upside down and squashed below it
        glRasterPos2i(0, 110);
        glPixelZoom(1.0f, -0.5f);
        glCopyPixels(0, 112, 200, 112, GL_COLOR);

        // Animated image, zoomed around its center, and its mirror image
        makeImage(image, t * 2.0f);
        const float zoom = 1.25f + 0.25f * std::sin(t * 2.0f);
        const float half = IMAGE_SIZE * zoom * 0.5f;
        glRasterPos2f(270.0f - half, 150.0f - half);
        glPixelZoom(zoom, zoom);
        glDrawPixels(IMAGE_SIZE, IMAGE_SIZE, GL_RGB, GL_UNSIGNED_BYTE, image);
        glRasterPos2i(370, 30);
        glPixelZoom(-1.0f, 1.0f);
        glDrawPixels(IMAGE_SIZE, IMAGE_SIZE, GL_RGB, GL_UNSIGNED_BYTE, image);
        glPixelZoom(1.0f, 1.0f);

        glColor3ub(255, 220, 90);
        windowText(font, 8, 228, "glBitmap / glDrawPixels / glCopyPixels");
        glColor3ub(150, 200, 255);
        windowText(font, 40, 4, "glCopyPixels");
        windowText(font, 310, 18, "zoom -1");
        windowText(font, 236, 214, "glPixelZoom");

        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
