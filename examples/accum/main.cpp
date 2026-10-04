// c3dgl example: the accumulation buffer. Left: motion blur, a cube racing around a circle leaves a fading trail (every
// frame the accumulation buffer is scaled down with GL_MULT and the new frame added with GL_ACCUM). Right: depth of field,
// a row of cubes rendered from 6 eye positions on a lens around the focus on the middle cube, averaged with GL_LOAD /
// GL_ACCUM: the cubes in front and behind it are blurred. The scissor box keeps the two halves apart, accumulation
// buffer operations only change the pixels in it. A: pause, B: accumulation buffer on/off.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kHalf = C3DGL_TOP_SCREEN_WIDTH / 2;

void cube() {
    static const float faces[6][4][3] = {
        {{-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}},       {{1, -1, -1}, {-1, -1, -1}, {-1, 1, -1}, {1, 1, -1}},
        {{1, -1, 1}, {1, -1, -1}, {1, 1, -1}, {1, 1, 1}},       {{-1, -1, -1}, {-1, -1, 1}, {-1, 1, 1}, {-1, 1, -1}},
        {{-1, 1, 1}, {1, 1, 1}, {1, 1, -1}, {-1, 1, -1}},       {{-1, -1, -1}, {1, -1, -1}, {1, -1, 1}, {-1, -1, 1}},
    };
    static const float shade[6] = {1.0f, 0.55f, 0.8f, 0.7f, 0.9f, 0.45f};
    GLfloat color[4];
    glGetFloatv(GL_CURRENT_COLOR, color);
    glBegin(GL_QUADS);
    for (int f = 0; f < 6; f++) {
        glColor3f(color[0] * shade[f], color[1] * shade[f], color[2] * shade[f]);
        for (int v = 0; v < 4; v++) glVertex3fv(faces[f][v]);
    }
    glEnd();
    glColor4fv(color);
}

// Ground grid from z = -2 to -22
void grid() {
    glColor3f(0.35f, 0.4f, 0.55f);
    glBegin(GL_LINES);
    for (int i = -5; i <= 5; i++) {
        glVertex3f(i * 1.0f, -1.2f, -2.0f);
        glVertex3f(i * 1.0f, -1.2f, -22.0f);
    }
    for (int z = 2; z <= 22; z += 2) {
        glVertex3f(-5.0f, -1.2f, -z * 1.0f);
        glVertex3f(5.0f, -1.2f, -z * 1.0f);
    }
    glEnd();
}

// Perspective of one half of the screen; the eye moved by (eyeX, eyeY) in the lens plane with the frustum sheared so that
// the plane at distance focus stays in place (the classic accFrustum of the OpenGL Programming Guide)
void halfProjection(float eyeX, float eyeY, float focus) {
    const float zNear = 1.0f, top = 0.5f, right = top * kHalf / C3DGL_SCREEN_HEIGHT;
    const float dx = -eyeX * zNear / focus, dy = -eyeY * zNear / focus;
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-right + dx, right + dx, -top + dy, top + dy, zNear, 40.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(-eyeX, -eyeY, 0.0f);
}

void motionScene(float time) {
    halfProjection(0.0f, 0.0f, 1.0f);
    glTranslatef(0.0f, 0.0f, -9.0f);
    glRotatef(25.0f, 1.0f, 0.0f, 0.0f);
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 6.0f);
    grid();
    glPopMatrix();

    // Fast cube on a circle, a slow one in the middle
    glPushMatrix();
    glRotatef(time * 4.0f, 0.0f, 1.0f, 0.0f);
    glTranslatef(2.6f, 0.0f, 0.0f);
    glRotatef(time * 9.0f, 1.0f, 0.3f, 0.0f);
    glScalef(0.5f, 0.5f, 0.5f);
    glColor3f(1.0f, 0.55f, 0.15f);
    cube();
    glPopMatrix();
    glPushMatrix();
    glRotatef(time * 0.8f, 0.2f, 1.0f, 0.0f);
    glScalef(0.6f, 0.6f, 0.6f);
    glColor3f(0.3f, 0.6f, 1.0f);
    cube();
    glPopMatrix();
}

void depthScene(float time, float eyeX, float eyeY) {
    const float focus = 9.0f;       // Eye distance of the middle cube
    halfProjection(eyeX, eyeY, focus);
    glTranslatef(0.0f, -0.4f, 0.0f);
    grid();
    static const float colors[5][3] = {
        {1.0f, 0.35f, 0.3f}, {1.0f, 0.8f, 0.2f}, {0.35f, 1.0f, 0.45f}, {0.3f, 0.7f, 1.0f}, {0.8f, 0.45f, 1.0f},
    };
    for (int i = 0; i < 5; i++) {
        glPushMatrix();
        glTranslatef(-1.6f + 0.8f * i, 0.0f, -3.5f - 2.75f * i);
        glRotatef(time + 30.0f * i, 0.3f, 1.0f, 0.1f);
        glScalef(0.45f, 0.45f, 0.45f);
        glColor3fv(colors[i]);
        cube();
        glPopMatrix();
    }
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

    glClearColor(0.08f, 0.08f, 0.15f, 1.0f);
    glClearAccum(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_ACCUM_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_SCISSOR_TEST);

    std::printf("c3dgl accum\n\n\n\n\n"
                "Left: motion blur. Each frame\n"
                "fades the accumulation buffer\n"
                "(GL_MULT 0.7) and adds the new\n"
                "frame (GL_ACCUM 0.3).\n\n"
                "Right: depth of field. 6 views\n"
                "from points on a lens, focused\n"
                "on the middle cube, averaged\n"
                "(GL_LOAD/GL_ACCUM 1/6).\n\n"
                "A: pause   B: accumulation\n"
                "START: exit\n\n");

    // Eye positions on the lens: a ring of 6
    constexpr int kViews = 6;
    float lens[kViews][2];
    for (int i = 0; i < kViews; i++) {
        lens[i][0] = 0.22f * std::cos(i * 2.0f * kPi / kViews);
        lens[i][1] = 0.22f * std::sin(i * 2.0f * kPi / kViews);
    }

    bool paused = false, accum = true;
    float time = 0.0f;
    std::printf("\x1b[s\x1b[19;1HAccumulation: on \x1b[u");
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 down = hidKeysDown();
        if (down & KEY_START) break;
        if (down & KEY_A) paused = !paused;
        if (down & KEY_B) {
            accum = !accum;
            std::printf("\x1b[s\x1b[19;1HAccumulation: %s\x1b[u", accum ? "on " : "off");
        }
        if (!paused) time += 1.0f;

        // Left half: motion blur
        glViewport(0, 0, kHalf, C3DGL_SCREEN_HEIGHT);
        glScissor(0, 0, kHalf, C3DGL_SCREEN_HEIGHT);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        motionScene(time);
        if (accum) {
            glAccum(GL_MULT, 0.7f);
            glAccum(GL_ACCUM, 0.3f);
            glAccum(GL_RETURN, 1.0f);
        }

        // Right half: depth of field
        glViewport(kHalf, 0, kHalf, C3DGL_SCREEN_HEIGHT);
        glScissor(kHalf, 0, kHalf, C3DGL_SCREEN_HEIGHT);
        for (int i = 0; i < (accum ? kViews : 1); i++) {
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            depthScene(time, accum ? lens[i][0] : 0.0f, accum ? lens[i][1] : 0.0f);
            if (accum) glAccum(i ? GL_ACCUM : GL_LOAD, 1.0f / kViews);
        }
        if (accum) glAccum(GL_RETURN, 1.0f);

        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
