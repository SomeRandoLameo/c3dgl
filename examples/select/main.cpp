// c3dgl example: feedback and selection. A ring of cubes turns in front of the camera; the cursor (circle pad or D-pad)
// picks the nearest cube under it with glRenderMode(GL_SELECT) and gluPickMatrix (one name per cube, the hit with the
// smallest depth wins). The picked cube is rendered once more in GL_FEEDBACK mode (GL_2D, back faces culled) and the
// polygons it gives back are drawn as a yellow outline in window coordinates, which must sit exactly on the cube.
// The bottom screen lists the hit records. A: pause the ring.
#include <3ds.h>
#include <GL/gl.h>
#include <GL/glu.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr int CUBES = 6;
constexpr int SELECT_SIZE = 256, FEEDBACK_SIZE = 4096;

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

void projection() {
    glMatrixMode(GL_PROJECTION);
    const double aspect = static_cast<double>(C3DGL_TOP_SCREEN_WIDTH) / C3DGL_SCREEN_HEIGHT;
    glFrustum(-aspect * 0.5, aspect * 0.5, -0.5, 0.5, 1.0, 30.0);
    glMatrixMode(GL_MODELVIEW);
}

// The ring: cube i gets name i + 1 (the names are ignored outside selection mode)
void scene(float angle, int picked) {
    static const GLubyte colors[CUBES][3] = {
        {230, 70, 70}, {70, 200, 90}, {80, 120, 240}, {230, 200, 60}, {200, 90, 220}, {70, 210, 210},
    };
    glLoadIdentity();
    glTranslatef(0.0f, -0.3f, -9.0f);
    glRotatef(15.0f, 1.0f, 0.0f, 0.0f);
    glInitNames();
    glPushName(0);
    for (int i = 0; i < CUBES; i++) {
        glLoadName(i + 1);
        glPushMatrix();
        glRotatef(angle + i * 360.0f / CUBES, 0.0f, 1.0f, 0.0f);
        glTranslatef(3.2f, 0.0f, 0.0f);
        glRotatef(angle * 2.0f + i * 40.0f, 1.0f, 0.3f, 0.0f);
        glScalef(0.7f, 0.7f, 0.7f);
        const float k = (i + 1 == picked) ? 1.0f : 0.75f;
        glColor3f(colors[i][0] / 255.0f * k, colors[i][1] / 255.0f * k, colors[i][2] / 255.0f * k);
        cube();
        glPopMatrix();
    }
    glPopName();
}

void transformCube(float angle, int i) {
    glLoadIdentity();
    glTranslatef(0.0f, -0.3f, -9.0f);
    glRotatef(15.0f, 1.0f, 0.0f, 0.0f);
    glRotatef(angle + i * 360.0f / CUBES, 0.0f, 1.0f, 0.0f);
    glTranslatef(3.2f, 0.0f, 0.0f);
    glRotatef(angle * 2.0f + i * 40.0f, 1.0f, 0.3f, 0.0f);
    glScalef(0.7f, 0.7f, 0.7f);
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

    static GLuint selectBuffer[SELECT_SIZE];
    static GLfloat feedbackBuffer[FEEDBACK_SIZE];
    glSelectBuffer(SELECT_SIZE, selectBuffer);
    glFeedbackBuffer(FEEDBACK_SIZE, GL_2D, feedbackBuffer);
    glClearColor(0.08f, 0.08f, 0.15f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    std::printf("c3dgl select\n\n\n\n\n"
                "Cursor: circle pad / D-pad.\n"
                "The nearest cube under the cursor\n"
                "is picked by GL_SELECT with\n"
                "gluPickMatrix (brighter); its front\n"
                "faces come back from GL_FEEDBACK\n"
                "and are outlined in yellow.\n\n"
                "A: pause   START: exit\n\n");

    bool paused = false;
    float angle = 0.0f, cursorX = 200.0f, cursorY = 120.0f;
    int picked = 0;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 down = hidKeysDown(), held = hidKeysHeld();
        if (down & KEY_START) break;
        if (down & KEY_A) paused = !paused;
        if (!paused) angle += 0.4f;

        circlePosition pad;
        hidCircleRead(&pad);
        cursorX += pad.dx / 40.0f + ((held & KEY_DRIGHT) ? 2.0f : 0.0f) - ((held & KEY_DLEFT) ? 2.0f : 0.0f);
        cursorY += pad.dy / 40.0f + ((held & KEY_DUP) ? 2.0f : 0.0f) - ((held & KEY_DDOWN) ? 2.0f : 0.0f);
        cursorX = std::fmin(std::fmax(cursorX, 0.0f), C3DGL_TOP_SCREEN_WIDTH - 1.0f);
        cursorY = std::fmin(std::fmax(cursorY, 0.0f), C3DGL_SCREEN_HEIGHT - 1.0f);

        // Selection: a 5x5 pixel pick region around the cursor, the hit with the smallest depth wins
        GLint viewport[4];
        glGetIntegerv(GL_VIEWPORT, viewport);
        glRenderMode(GL_SELECT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPickMatrix(cursorX, cursorY, 5.0, 5.0, viewport);
        projection();
        scene(angle, 0);
        const int hits = glRenderMode(GL_RENDER);
        picked = 0;
        GLuint nearest = 0xFFFFFFFF;
        std::printf("\x1b[15;1HHits: %i\x1b[K\n", hits);
        const GLuint *record = selectBuffer;
        for (int h = 0; h < hits; h++) {
            const GLuint names = record[0], zmin = record[1];
            if (h < 6)
                std::printf("  cube %lu  z %.4f .. %.4f\x1b[K\n", static_cast<unsigned long>(record[3]),
                            zmin / 4294967295.0, record[2] / 4294967295.0);
            if ((names > 0) && (zmin < nearest)) {
                nearest = zmin;
                picked = static_cast<int>(record[2 + names]);
            }
            record += 3 + names;
        }
        for (int h = hits < 0 ? 0 : hits; h < 6; h++) std::printf("\x1b[K\n");

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        projection();
        scene(angle, picked);

        // Feedback: the window coordinates of the picked cube's front faces
        int values = 0;
        if (picked > 0) {
            glRenderMode(GL_FEEDBACK);
            transformCube(angle, picked - 1);
            cube();
            values = glRenderMode(GL_RENDER);
        }
        std::printf("\x1b[23;1HPicked: %i, feedback values: %i\x1b[K", picked, values);

        // Outlines and cursor in window coordinates
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, C3DGL_TOP_SCREEN_WIDTH, 0.0, C3DGL_SCREEN_HEIGHT, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glDisable(GL_DEPTH_TEST);
        glLineWidth(2.0f);
        glColor3f(1.0f, 0.9f, 0.2f);
        for (int i = 0; i < values;) {
            const GLenum token = static_cast<GLenum>(feedbackBuffer[i++]);
            if (token != GL_POLYGON_TOKEN) break;       // Only polygons are fed back here
            const int n = static_cast<int>(feedbackBuffer[i++]);
            glBegin(GL_LINE_LOOP);
            for (int k = 0; k < n; k++, i += 2) glVertex2f(feedbackBuffer[i], feedbackBuffer[i + 1]);
            glEnd();
        }
        glLineWidth(1.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_LINES);
        glVertex2f(cursorX - 8.0f, cursorY); glVertex2f(cursorX - 2.0f, cursorY);
        glVertex2f(cursorX + 2.0f, cursorY); glVertex2f(cursorX + 8.0f, cursorY);
        glVertex2f(cursorX, cursorY - 8.0f); glVertex2f(cursorX, cursorY - 2.0f);
        glVertex2f(cursorX, cursorY + 2.0f); glVertex2f(cursorX, cursorY + 8.0f);
        glEnd();
        glEnable(GL_DEPTH_TEST);

        c3dglSwapBuffers();
    }

    c3dglClose();
    gfxExit();
    return 0;
}
