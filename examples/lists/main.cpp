// c3dgl example: display lists. Three lit gears (the classic "gears" scene), each compiled into a display list once
// (geometry, normals, material and shade model) and drawn with glCallList every frame. A switches to drawing the same
// gears in immediate mode, which must look identical; the circle pad turns the view.
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <citro3d.h>

#include <cmath>
#include <cstdio>

namespace {

constexpr float PI = 3.14159265f;

// A gear in the xy plane, centered on the z axis: a hole of radius `inner`, `teeth` teeth of depth `toothDepth`
// around radius `outer`, `width` thick. Flat shaded, except the smooth hole
void gear(float inner, float outer, float width, int teeth, float toothDepth) {
    const float r0 = inner, r1 = outer - toothDepth / 2.0f, r2 = outer + toothDepth / 2.0f;
    const float da = 2.0f * PI / teeth / 4.0f;     // A tooth: rise, top, fall, gap
    const float z = width * 0.5f;

    glShadeModel(GL_FLAT);

    // Front and back faces: rings between the hole and the tooth roots, then the faces of the teeth
    for (int side = 0; side < 2; side++) {
        const float s = side ? -1.0f : 1.0f;
        glNormal3f(0.0f, 0.0f, s);
        glBegin(GL_QUAD_STRIP);
        for (int i = 0; i <= teeth; i++) {
            const float angle = i * 2.0f * PI / teeth;
            const float a = angle, b = angle + 3.0f * da;
            if (side == 0) {
                glVertex3f(r0 * std::cos(a), r0 * std::sin(a), z);
                glVertex3f(r1 * std::cos(a), r1 * std::sin(a), z);
                if (i < teeth) {
                    glVertex3f(r0 * std::cos(a), r0 * std::sin(a), z);
                    glVertex3f(r1 * std::cos(b), r1 * std::sin(b), z);
                }
            } else {
                glVertex3f(r1 * std::cos(a), r1 * std::sin(a), -z);
                glVertex3f(r0 * std::cos(a), r0 * std::sin(a), -z);
                if (i < teeth) {
                    glVertex3f(r1 * std::cos(b), r1 * std::sin(b), -z);
                    glVertex3f(r0 * std::cos(a), r0 * std::sin(a), -z);
                }
            }
        }
        glEnd();

        glBegin(GL_QUADS);
        for (int i = 0; i < teeth; i++) {
            const float angle = i * 2.0f * PI / teeth;
            const float rs[4] = {r1, r2, r2, r1};
            for (int k = 0; k < 4; k++) {
                const int corner = side ? 3 - k : k;     // Counter-clockwise seen from outside
                const float a = angle + corner * da;
                glVertex3f(rs[corner] * std::cos(a), rs[corner] * std::sin(a), s * z);
            }
        }
        glEnd();
    }

    // Outward faces of the teeth: rise, top, fall and the gap to the next tooth, each with its own normal
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i < teeth; i++) {
        const float angle = i * 2.0f * PI / teeth;
        const float a[5] = {angle, angle + da, angle + 2.0f * da, angle + 3.0f * da, angle + 4.0f * da};
        const float r[5] = {r1, r2, r2, r1, r1};
        for (int k = 0; k < 4; k++) {
            // Normal of the face from point k to point k + 1: perpendicular to it in the xy plane
            const float x0 = r[k] * std::cos(a[k]), y0 = r[k] * std::sin(a[k]);
            const float x1 = r[k + 1] * std::cos(a[k + 1]), y1 = r[k + 1] * std::sin(a[k + 1]);
            glVertex3f(x0, y0, z);
            glVertex3f(x0, y0, -z);
            glNormal3f(y1 - y0, x0 - x1, 0.0f);
        }
    }
    glVertex3f(r1, 0.0f, z);
    glVertex3f(r1, 0.0f, -z);
    glEnd();

    // The hole, smooth shaded with normals pointing inwards
    glShadeModel(GL_SMOOTH);
    glBegin(GL_QUAD_STRIP);
    for (int i = 0; i <= teeth; i++) {
        const float angle = i * 2.0f * PI / teeth;
        glNormal3f(-std::cos(angle), -std::sin(angle), 0.0f);
        glVertex3f(r0 * std::cos(angle), r0 * std::sin(angle), -z);
        glVertex3f(r0 * std::cos(angle), r0 * std::sin(angle), z);
    }
    glEnd();
}

struct GearDef {
    GLfloat color[4];
    float inner, outer, width;
    int teeth;
};

const GearDef gears[3] = {
    {{0.8f, 0.1f, 0.0f, 1.0f}, 1.0f, 4.0f, 1.0f, 20},
    {{0.0f, 0.8f, 0.2f, 1.0f}, 0.5f, 2.0f, 2.0f, 10},
    {{0.2f, 0.2f, 1.0f, 1.0f}, 1.3f, 2.0f, 0.5f, 10},
};

// Everything a gear's list contains
void drawGear(int i) {
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, gears[i].color);
    gear(gears[i].inner, gears[i].outer, gears[i].width, gears[i].teeth, 0.7f);
}

void drawScene(GLuint lists, bool useLists, float angle, float viewX, float viewY) {
    glPushMatrix();
    glRotatef(viewX, 1.0f, 0.0f, 0.0f);
    glRotatef(viewY, 0.0f, 1.0f, 0.0f);

    // Position and rotation of each gear, so that the teeth mesh
    const float place[3][3] = {{-3.0f, -2.0f, angle}, {3.1f, -2.0f, -2.0f * angle - 9.0f},
                               {-3.1f, 4.2f, -2.0f * angle - 25.0f}};
    for (int i = 0; i < 3; i++) {
        glPushMatrix();
        glTranslatef(place[i][0], place[i][1], 0.0f);
        glRotatef(place[i][2], 0.0f, 0.0f, 1.0f);
        if (useLists) glCallList(lists + i);
        else drawGear(i);
        glPopMatrix();
    }
    glPopMatrix();
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

    // One list per gear, compiled once
    const GLuint lists = glGenLists(3);
    for (int i = 0; i < 3; i++) {
        glNewList(lists + i, GL_COMPILE);
        drawGear(i);
        glEndList();
    }

    const GLfloat lightPos[4] = {5.0f, 5.0f, 10.0f, 0.0f};
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = static_cast<double>(C3DGL_TOP_SCREEN_WIDTH) / C3DGL_SCREEN_HEIGHT;
    glFrustum(-aspect, aspect, -1.0, 1.0, 5.0, 60.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -40.0f);
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_NORMALIZE);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    std::printf("c3dgl display lists\n\n\n\n\n"
                "Three gears, each one a display\n"
                "list (glNewList once, glCallList\n"
                "every frame). Lists and immediate\n"
                "mode must look identical.\n\n"
                "A: lists / immediate mode\n"
                "Circle pad: turn the view\n"
                "START: exit\n\n");
    std::printf("%s\n", glGetError() == GL_NO_ERROR ? "No GL errors" : "GL ERROR");

    bool useLists = true;
    float angle = 0.0f, viewX = 20.0f, viewY = 30.0f;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) useLists = !useLists;
        circlePosition pad;
        hidCircleRead(&pad);
        viewY += pad.dx / 50.0f;
        viewX -= pad.dy / 50.0f;
        std::printf("\x1b[s\x1b[16;1HDrawing: %s\x1b[K\x1b[u", useLists ? "display lists" : "immediate mode");

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        drawScene(lists, useLists, angle, viewX, viewY);
        c3dglSwapBuffers();
        angle += 2.0f;
    }

    glDeleteLists(lists, 3);
    c3dglClose();
    gfxExit();
    return 0;
}
