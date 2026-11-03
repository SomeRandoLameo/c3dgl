/* Native Rust immediate mode: interpolated RGB triangle with matrix rotation. */
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl_native.h>
#include <stdio.h>
int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    if (!c3dglInit()) { gfxExit(); return 1; }
    glClearColor(.05f, .08f, .12f, 1);
    float angle = 0;
    int result = 0;
    printf("Native Rust immediate triangle\nSTART exits\n");
    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_START) break;
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        glRotatef(angle, 0, 0, 1);
        glBegin(GL_TRIANGLES);
        glColor4ub(255, 0, 0, 255); glVertex2f(-.7f, -.6f);
        glColor3f(0, 1, 0); glVertex3f(.7f, -.6f, 0);
        glColor4f(0, 0, 1, 1); glVertex4f(0, .7f, 0, 1);
        glEnd();
        if (c3dglNativePresent() || glGetError() != GL_NO_ERROR) {
            printf("Immediate draw failed\n"); result = 1; break;
        }
        angle += .5f;
        if (angle >= 360) angle -= 360;
    }
    c3dglClose(); gfxExit(); return result;
}
