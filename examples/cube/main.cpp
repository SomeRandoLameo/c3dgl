// c3dgl example: plain OpenGL 1.1 on the 3DS, no raylib.
// Doubles as a visual test for the parts raylib_test does not cover; the expected
// result is printed on the bottom screen:
//   - textured cube with depth test + back-face culling, drawn into the right half viewport
//   - NPOT texture (12x12, padded to 16x16 internally)
//   - second cube in the top left with a 256x256 PNG from romfs (loaded with libpng), linear filtering
//   - scissor box in GL coordinates (bottom-left origin) and GL_LINES outline around it
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <png.h>

#include <cstdio>
#include <vector>

namespace {

constexpr int TEX_SIZE = 12;    // Not a power of two on purpose

struct Face {
    float vertices[4][3];       // Counter-clockwise seen from outside
    GLubyte color[3];
};

constexpr Face CUBE[] = {
    {{{-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}}, {255, 80, 80}},    // +z
    {{{ 1, -1, -1}, {-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}}, {80, 255, 80}},    // -z
    {{{-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}, {-1,  1, -1}}, {80, 80, 255}},    // -x
    {{{ 1, -1,  1}, { 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}}, {255, 255, 80}},   // +x
    {{{-1,  1,  1}, { 1,  1,  1}, { 1,  1, -1}, {-1,  1, -1}}, {80, 255, 255}},   // +y
    {{{-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}}, {255, 80, 255}},   // -y
};

constexpr float UVS[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};

constexpr const char* IMAGE_PATH = "romfs:/tonk.png";

GLuint createCheckerTexture() {
    GLubyte pixels[TEX_SIZE][TEX_SIZE][4];
    for (int y = 0; y < TEX_SIZE; y++) {
        for (int x = 0; x < TEX_SIZE; x++) {
            const bool light = ((x / 2) + (y / 2)) % 2 == 0;
            const GLubyte v = light ? 255 : 90;
            pixels[y][x][0] = pixels[y][x][1] = pixels[y][x][2] = v;
            pixels[y][x][3] = 255;
        }
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SIZE, TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// PNG as RGBA8 texture, 0 on failure. Rows are stored top to bottom, so v = 0 is the top of the image
GLuint loadPngTexture(const char* path) {
    png_image image = {};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_file(&image, path)) {
        std::printf("%s: %s\n", path, image.message);
        return 0;
    }

    image.format = PNG_FORMAT_RGBA;
    std::vector<GLubyte> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
        std::printf("%s: %s\n", path, image.message);
        png_image_free(&image);
        return 0;
    }

    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// Cube with the texture on every face, in the given viewport. Face colors tint the texture,
// white leaves it unchanged
void drawCube(GLuint texture, float angle, int x, int y, int w, int h, bool tinted) {
    glViewport(x, y, w, h);

    const float aspect = static_cast<float>(w) / h;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-0.1 * aspect, 0.1 * aspect, -0.1, 0.1, 0.1, 100.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(25.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(angle, 0.0f, 1.0f, 0.0f);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBegin(GL_QUADS);
    for (const Face& face : CUBE) {
        if (tinted) glColor4ub(face.color[0], face.color[1], face.color[2], 255);
        else glColor4ub(255, 255, 255, 255);
        for (int i = 0; i < 4; i++) {
            // Vertices go counter-clockwise from the bottom left, the image's v = 0 is its top row
            glTexCoord2f(UVS[i][0], tinted ? UVS[i][1] : 1.0f - UVS[i][1]);
            glVertex3f(face.vertices[i][0], face.vertices[i][1], face.vertices[i][2]);
        }
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);

    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

void drawOverlay() {
    // Full screen, y down like most 2D code
    glViewport(0, 0, C3DGL_TOP_SCREEN_WIDTH, C3DGL_SCREEN_HEIGHT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, C3DGL_TOP_SCREEN_WIDTH, C3DGL_SCREEN_HEIGHT, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Scissor box in GL coordinates: 10 px from the left, 10 px from the BOTTOM, 120x50
    const int sx = 10, sy = 10, sw = 120, sh = 50;
    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);

    // Large quad, only the scissor box of it must be visible
    glColor4ub(255, 200, 0, 255);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(200.0f, 0.0f);
    glVertex2f(200.0f, 240.0f);
    glVertex2f(0.0f, 240.0f);
    glEnd();
    glDisable(GL_SCISSOR_TEST);

    // Outline 2 px outside the scissor box (y down: top = height - (sy + sh))
    const float left = sx - 2.0f, right = sx + sw + 2.0f;
    const float top = C3DGL_SCREEN_HEIGHT - (sy + sh) - 2.0f, bottom = C3DGL_SCREEN_HEIGHT - sy + 2.0f;
    glColor4ub(255, 255, 255, 255);
    glBegin(GL_LINES);
    glVertex2f(left, top);     glVertex2f(right, top);
    glVertex2f(right, top);    glVertex2f(right, bottom);
    glVertex2f(right, bottom); glVertex2f(left, bottom);
    glVertex2f(left, bottom);  glVertex2f(left, top);
    glEnd();
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
    romfsInit();
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

    std::printf("c3dgl cube test\n\n\n\n\n"
                "Expected on the top screen:\n"
                "- cube in the RIGHT half, solid\n"
                "  (no see-through faces), 6x6\n"
                "  checker per face, no dark band\n"
                "- yellow box BOTTOM LEFT, inside\n"
                "  a white outline with 2px gap\n"
                "- cube TOP LEFT with the tonk\n"
                "  image upright on the side faces\n\n"
                "START: exit\n");

    const GLuint texture = createCheckerTexture();
    const GLuint image = loadPngTexture(IMAGE_PATH);
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    float angle = 0.0f;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // Checker cube in the right half, image cube in the top left quarter (GL viewport: y up)
        const int halfW = C3DGL_TOP_SCREEN_WIDTH / 2, h = C3DGL_SCREEN_HEIGHT;
        drawCube(texture, angle, halfW, 0, halfW, h, true);
        drawCube(image, -angle, 0, h / 2, halfW, h / 2, false);
        drawOverlay();
        c3dglSwapBuffers();

        angle += 1.0f;
    }

    glDeleteTextures(1, &image);
    glDeleteTextures(1, &texture);
    c3dglClose();
    gfxExit();
    romfsExit();
    return 0;
}
