// c3dgl example: point parameters and point sprites (OpenGL ES 1.1). The top screen shows one setup per cell (4x2
// grid, 100x120 px per cell). Most cells draw pairs: left the points under test, right a reference drawn without the
// feature (plain points of the expected size, or textured quads with the expected texcoords). Each pair must look
// the same. The last cell is a particle field (soft round sprites with distance attenuation). The bottom screen
// shows the result of the non-visual checks and what each cell must look like.
#include <3ds.h>
#include <GLES/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr int COLUMNS = 4, ROWS = 2;
constexpr int CELL_W = C3DGL_TOP_SCREEN_WIDTH / COLUMNS, CELL_H = C3DGL_SCREEN_HEIGHT / ROWS;
constexpr float NEAR = 1.0f, FAR = 30.0f;
constexpr float HALF_W = 0.5f, HALF_H = HALF_W * CELL_H / CELL_W;     // Frustum at the near plane, square pixels
constexpr int PARTICLES = 256;

int checks, failures;

#define CHECK(cond) check((cond), #cond, __LINE__)

void check(bool ok, const char* what, int line) {
    checks++;
    if (ok) return;
    failures++;
    std::printf("FAIL %i: %s\n", line, what);
}

bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

GLuint quadrants, disc;

// 2x2: red, green in the first row (t = 0), blue, yellow in the second. A sprite shows red top left
GLuint createQuadrants() {
    const GLubyte pixels[4 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 0};
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// 32x32 white disc with an alpha falloff to the edge (soft particle)
GLuint createDisc() {
    static GLubyte pixels[32 * 32 * 2];
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            const float dx = (x + 0.5f - 16.0f) / 16.0f, dy = (y + 0.5f - 16.0f) / 16.0f;
            const float a = 1.0f - std::sqrt(dx * dx + dy * dy);
            pixels[(y * 32 + x) * 2] = 255;
            pixels[(y * 32 + x) * 2 + 1] = (GLubyte)(a <= 0.0f ? 0 : a >= 1.0f ? 255 : a * 255.0f);
        }
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 32, 32, 0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id;
}

// Cell (column, row from the top) as viewport. Pixel coordinates (origin bottom left of the cell) or a frustum
void beginCell(int column, int row, bool perspective) {
    glViewport(column * CELL_W, (ROWS - 1 - row) * CELL_H, CELL_W, CELL_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    if (perspective) glFrustumf(-HALF_W, HALF_W, -HALF_H, HALF_H, NEAR, FAR);
    else glOrthof(0.0f, CELL_W, 0.0f, CELL_H, -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glPushAttrib(GL_POINT_BIT | GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT);
}

void endCell() { glPopAttrib(); }

// One point through a vertex array (the ES way)
void drawPoint(float x, float y, float z = 0.0f) {
    const GLfloat v[3] = {x, y, z};
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, v);
    glDrawArrays(GL_POINTS, 0, 1);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Square of `size` pixels around (x, y), texcoords like a point sprite: (0, 0) top left, (1, 1) bottom right
void drawSpriteQuad(float x, float y, float size, int units = 1) {
    const float r = 0.5f * size;
    const float corners[4][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
    glBegin(GL_QUADS);
    for (const auto& c : corners) {
        for (int u = 0; u < units; u++) glMultiTexCoord2f(GL_TEXTURE0 + u, c[2], c[3]);
        glVertex2f(x + c[0] * r, y + c[1] * r);
    }
    glEnd();
}

// 1: GL_POINT_SIZE_MIN/MAX clamp glPointSize: 20 with max 12, 2 with min 8. Right: plain 12 and 8
void drawClamp() {
    glColor4f(1.0f, 0.6f, 0.1f, 1.0f);
    glPointSize(20.0f);
    glPointParameterf(GL_POINT_SIZE_MAX, 12.0f);
    drawPoint(30.5f, 85.5f);
    glPointSize(2.0f);
    glPointParameterf(GL_POINT_SIZE_MIN, 8.0f);
    drawPoint(30.5f, 35.5f);

    glPointParameterf(GL_POINT_SIZE_MIN, 0.0f);
    glPointParameterf(GL_POINT_SIZE_MAX, 256.0f);
    glPointSize(12.0f);
    drawPoint(70.5f, 85.5f);
    glPointSize(8.0f);
    drawPoint(70.5f, 35.5f);
}

// Eye distance of a point at (x, y, z)
float eyeDistance(float x, float y, float z) { return std::sqrt(x * x + y * y + z * z); }

// 2: quadratic attenuation (0, 0, 1/16): size = 32/sqrt(d^2/16) = 128/d at eye distances ~4, 8, 16
// 3: same setup with constant + linear terms through the ES fixed-point API: (2, 0.5, 0)
void drawAttenuation(bool fixedPoint) {
    const float zs[3] = {-4.0f, -8.0f, -16.0f};
    if (fixedPoint) {
        const GLfixed att[3] = {2 << 16, 1 << 15, 0};
        glPointParameterxv(GL_POINT_DISTANCE_ATTENUATION, att);
    } else {
        const GLfloat att[3] = {0.0f, 0.0f, 1.0f / 16.0f};
        glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att);
    }
    glPointSize(fixedPoint ? 24.0f : 32.0f);
    glColor4f(0.3f, 0.8f, 1.0f, 1.0f);
    for (int i = 0; i < 3; i++) {
        const float z = zs[i], ky = -z * HALF_H / NEAR, kx = -z * HALF_W / NEAR;
        drawPoint(-0.45f * kx, (0.6f - 0.6f * i) * ky, z);
    }

    // Reference: same positions, constant size from GL's formula
    const GLfloat none[3] = {1.0f, 0.0f, 0.0f};
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, none);
    for (int i = 0; i < 3; i++) {
        const float z = zs[i], ky = -z * HALF_H / NEAR, kx = -z * HALF_W / NEAR;
        const float x = 0.45f * kx, y = (0.6f - 0.6f * i) * ky;
        // Mirrored position: same distance as the left point
        const float d = eyeDistance(-x, y, z);
        const float k = fixedPoint ? 2.0f + 0.5f * d : d * d / 16.0f;
        glPointSize((fixedPoint ? 24.0f : 32.0f) / std::sqrt(k));
        drawPoint(x, y, z);
    }
}

void enableQuadrants() {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, quadrants);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

// 4: sprite with GL_COORD_REPLACE_OES: red top left, green top right, blue bottom left, yellow bottom right
// 5: the same with a texture matrix, which must not apply to sprite coordinates
void drawSprite(bool textureMatrix) {
    enableQuadrants();
    if (textureMatrix) {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        glScalef(3.0f, 3.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
    }
    glEnable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);
    glPointSize(32.0f);
    drawPoint(28.0f, 80.0f);
    glPointSize(16.0f);
    drawPoint(28.0f, 30.0f);
    glDisable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);

    if (textureMatrix) {
        glMatrixMode(GL_TEXTURE);
        glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    drawSpriteQuad(72.0f, 80.0f, 32.0f);
    drawSpriteQuad(72.0f, 30.0f, 16.0f);
}

// 6: sprites without GL_COORD_REPLACE_OES: the whole point samples the vertex texcoord (0.75, 0.25) -> green.
// Lower pair: GL_COORD_REPLACE_OES on, but GL_POINT_SPRITE_OES off -> green too
void drawNoReplace() {
    enableQuadrants();
    glTexCoord2f(0.75f, 0.25f);
    glPointSize(24.0f);
    glEnable(GL_POINT_SPRITE_OES);
    drawPoint(28.0f, 80.0f);
    glDisable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);
    drawPoint(28.0f, 30.0f);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);

    glDisable(GL_TEXTURE_2D);
    glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
    drawPoint(72.0f, 80.0f);
    drawPoint(72.0f, 30.0f);
}

// 7: two units: quadrants on unit 0 and the soft disc on unit 1, both with GL_COORD_REPLACE_OES -> round colored
// sprites fading out to the edge. Upper pair: sprites (left) vs quads; lower pair: only unit 1 replaces, unit 0 gets
// the vertex texcoord (0.25, 0.75) -> a blue disc on both sides
void drawMultitexture() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    enableQuadrants();
    glActiveTexture(GL_TEXTURE1);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, disc);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);

    glEnable(GL_POINT_SPRITE_OES);
    glPointSize(40.0f);
    drawPoint(28.0f, 82.0f);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);
    glMultiTexCoord2f(GL_TEXTURE0, 0.25f, 0.75f);
    drawPoint(28.0f, 32.0f);
    glDisable(GL_POINT_SPRITE_OES);

    drawSpriteQuad(72.0f, 82.0f, 40.0f, 2);
    // Unit 0 constant, unit 1 like a sprite
    const float r = 20.0f, x = 72.0f, y = 32.0f;
    const float corners[4][4] = {{-1, -1, 0, 1}, {1, -1, 1, 1}, {1, 1, 1, 0}, {-1, 1, 0, 0}};
    glBegin(GL_QUADS);
    for (const auto& c : corners) {
        glMultiTexCoord2f(GL_TEXTURE0, 0.25f, 0.75f);
        glMultiTexCoord2f(GL_TEXTURE1, c[2], c[3]);
        glVertex2f(x + c[0] * r, y + c[1] * r);
    }
    glEnd();

    glActiveTexture(GL_TEXTURE1);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);
    glDisable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
}

struct Particle {
    GLfloat pos[3];
    GLubyte color[4];
};
Particle particles[PARTICLES];

void createParticles() {
    u32 seed = 12345;
    auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.0f; };
    for (auto& p : particles) {
        // Uniform in a ball of radius 2
        float x, y, z;
        do { x = 2 * rnd() - 1; y = 2 * rnd() - 1; z = 2 * rnd() - 1; } while (x * x + y * y + z * z > 1.0f);
        p.pos[0] = 2 * x; p.pos[1] = 2 * y; p.pos[2] = 2 * z;
        p.color[0] = (GLubyte)(128 + 127 * rnd());
        p.color[1] = (GLubyte)(64 + 191 * rnd());
        p.color[2] = (GLubyte)(255 * rnd());
        p.color[3] = 96;
    }
}

// 8: particle field: soft sprites, size by distance (0, 0, 1) -> 48/d px (at most 20), additive blending, turning
void drawParticles(float angle) {
    glTranslatef(0.0f, 0.0f, -5.0f);
    glRotatef(angle, 0.3f, 1.0f, 0.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, disc);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glEnable(GL_POINT_SPRITE_OES);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);
    const GLfloat att[3] = {0.0f, 0.0f, 1.0f};
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, att);
    glPointParameterf(GL_POINT_SIZE_MAX, 20.0f);
    glPointSize(48.0f);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(Particle), particles[0].pos);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Particle), particles[0].color);
    glDrawArrays(GL_POINTS, 0, PARTICLES);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);
}

void testPointApi() {
    GLfloat f[4];
    GLint i[4];
    GLboolean b;

    // Defaults
    glGetFloatv(GL_POINT_SIZE_MIN, f);
    CHECK(f[0] == 0.0f);
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, f);
    CHECK(f[0] == 1.0f && f[1] >= 64.0f);
    const float maxSize = f[1];
    glGetFloatv(GL_POINT_SIZE_MAX, f);
    CHECK(f[0] == maxSize);
    glGetFloatv(GL_POINT_FADE_THRESHOLD_SIZE, f);
    CHECK(f[0] == 1.0f);
    glGetFloatv(GL_POINT_DISTANCE_ATTENUATION, f);
    CHECK(f[0] == 1.0f && f[1] == 0.0f && f[2] == 0.0f);
    CHECK(!glIsEnabled(GL_POINT_SPRITE_OES));
    for (int unit = 0; unit < 3; unit++) {
        glActiveTexture(GL_TEXTURE0 + unit);
        glGetTexEnviv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, i);
        CHECK(i[0] == GL_FALSE);
    }
    glActiveTexture(GL_TEXTURE0);
    const char* ext = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    CHECK(ext && std::strstr(ext, "GL_OES_point_sprite"));
    CHECK(glGetError() == GL_NO_ERROR);

    // Setters, all variants
    glPointParameterf(GL_POINT_SIZE_MIN, 2.5f);
    glPointParameteri(GL_POINT_SIZE_MAX, 40);
    glPointParameterx(GL_POINT_FADE_THRESHOLD_SIZE, 3 << 16);
    const GLint iatt[3] = {1, 2, 3};
    glPointParameteriv(GL_POINT_DISTANCE_ATTENUATION, iatt);
    glGetFloatv(GL_POINT_SIZE_MIN, f);
    CHECK(near(f[0], 2.5f));
    glGetIntegerv(GL_POINT_SIZE_MAX, i);
    CHECK(i[0] == 40);
    GLfixed x[3];
    glGetFixedv(GL_POINT_FADE_THRESHOLD_SIZE, x);
    CHECK(x[0] == 3 << 16);
    glGetFloatv(GL_POINT_DISTANCE_ATTENUATION, f);
    CHECK(f[0] == 1.0f && f[1] == 2.0f && f[2] == 3.0f);
    const GLfixed xatt[3] = {1 << 16, 0, 1 << 15};
    glPointParameterxv(GL_POINT_DISTANCE_ATTENUATION, xatt);
    glGetFixedv(GL_POINT_DISTANCE_ATTENUATION, x);
    CHECK(x[0] == 1 << 16 && x[1] == 0 && x[2] == 1 << 15);

    // Sprite enable and coord replace per unit (also through the fixed-point call, a boolean is passed unscaled)
    glEnable(GL_POINT_SPRITE_OES);
    glGetBooleanv(GL_POINT_SPRITE_OES, &b);
    CHECK(b == GL_TRUE);
    glActiveTexture(GL_TEXTURE1);
    glTexEnvx(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_TRUE);
    glGetTexEnvxv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, x);
    CHECK(x[0] == GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glGetTexEnviv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, i);
    CHECK(i[0] == GL_FALSE);
    CHECK(glGetError() == GL_NO_ERROR);

    // Errors
    glPointParameterf(GL_POINT_DISTANCE_ATTENUATION, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glPointParameterf(GL_POINT_SIZE_MIN, -1.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glPointParameterf(GL_POINT_SIZE, 1.0f);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glPointSize(0.0f);
    CHECK(glGetError() == GL_INVALID_VALUE);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glTexEnvi(GL_TEXTURE_ENV, GL_COORD_REPLACE_OES, GL_TRUE);
    CHECK(glGetError() == GL_INVALID_ENUM);
    glGetFloatv(GL_POINT_SIZE_MIN, f);
    CHECK(near(f[0], 2.5f));                    // Unchanged by the failed calls

    // Attribute stack: POINT_BIT restores the parameters and sprite state, ENABLE_BIT only the enable
    glPushAttrib(GL_POINT_BIT);
    glPointParameterf(GL_POINT_SIZE_MIN, 9.0f);
    glDisable(GL_POINT_SPRITE_OES);
    glActiveTexture(GL_TEXTURE1);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);
    glActiveTexture(GL_TEXTURE0);
    glPopAttrib();
    glGetFloatv(GL_POINT_SIZE_MIN, f);
    CHECK(near(f[0], 2.5f));
    CHECK(glIsEnabled(GL_POINT_SPRITE_OES));
    glActiveTexture(GL_TEXTURE1);
    glGetTexEnviv(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, i);
    CHECK(i[0] == GL_TRUE);
    glTexEnvi(GL_POINT_SPRITE_OES, GL_COORD_REPLACE_OES, GL_FALSE);
    glActiveTexture(GL_TEXTURE0);
    glPushAttrib(GL_ENABLE_BIT);
    glDisable(GL_POINT_SPRITE_OES);
    glPointParameterf(GL_POINT_SIZE_MIN, 4.0f);
    glPopAttrib();
    CHECK(glIsEnabled(GL_POINT_SPRITE_OES));
    glGetFloatv(GL_POINT_SIZE_MIN, f);
    CHECK(near(f[0], 4.0f));

    // Back to the defaults
    glDisable(GL_POINT_SPRITE_OES);
    glPointParameterf(GL_POINT_SIZE_MIN, 0.0f);
    glPointParameterf(GL_POINT_SIZE_MAX, maxSize);
    glPointParameterf(GL_POINT_FADE_THRESHOLD_SIZE, 1.0f);
    const GLfloat none[3] = {1.0f, 0.0f, 0.0f};
    glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, none);
    CHECK(glGetError() == GL_NO_ERROR);
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

    std::printf("c3dgl points\n\n\n\n\n");
    testPointApi();
    std::printf("%i/%i checks passed\n\n"
                "Top screen: left/right of every\n"
                "pair must match\n"
                "top row:\n"
                "- orange squares 12px and 8px\n"
                "  (size clamped by min/max)\n"
                "- cyan squares, smaller with\n"
                "  distance (attenuation)\n"
                "- the same with the ES x API\n"
                "- sprites: red top left, green\n"
                "  top right, blue bottom left,\n"
                "  yellow bottom right\n"
                "bottom row:\n"
                "- same sprites (texture matrix\n"
                "  must be ignored)\n"
                "- four green squares\n"
                "- soft round 4-color sprite,\n"
                "  soft blue disc below\n"
                "- turning ball of glowing\n"
                "  particles\n\n"
                "START: exit\n",
                checks - failures, checks);

    quadrants = createQuadrants();
    disc = createDisc();
    createParticles();
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f);

    float angle = 0.0f;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        if (hidKeysDown() & KEY_START) break;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        beginCell(0, 0, false); drawClamp(); endCell();
        beginCell(1, 0, true); drawAttenuation(false); endCell();
        beginCell(2, 0, true); drawAttenuation(true); endCell();
        beginCell(3, 0, false); drawSprite(false); endCell();
        beginCell(0, 1, false); drawSprite(true); endCell();
        beginCell(1, 1, false); drawNoReplace(); endCell();
        beginCell(2, 1, false); drawMultitexture(); endCell();
        beginCell(3, 1, true); drawParticles(angle); endCell();
        c3dglSwapBuffers();
        angle += 0.5f;
    }

    glDeleteTextures(1, &disc);
    glDeleteTextures(1, &quadrants);
    c3dglClose();
    gfxExit();
    return 0;
}
