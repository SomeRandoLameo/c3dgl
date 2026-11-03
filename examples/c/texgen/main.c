// c3dgl example: texture coordinate generation (glTexGen). Three rotating tori without texcoords of their own:
//   left:   GL_SPHERE_MAP, a chrome look from a generated sky/ground environment map
//   middle: GL_OBJECT_LINEAR stripes, they stick to the torus while it turns
//   right:  GL_EYE_LINEAR stripes, fixed in eye space: the torus turns through them
// A scrolls the stripes with the texture matrix (applied to the generated coordinates); the circle pad turns the tori.
#include <3ds.h>
#include <stdbool.h>
#include <GL/gl.h>
#include <c3dgl_native.h>
#include "../common.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static const float PI = 3.14159265f;

// Torus around the z axis as indexed triangles, positions and normals only (texgen makes the texcoords)
typedef struct {
    GLfloat* positions;
    GLfloat* normals;
    GLushort* indices;
    int indexCount;
} Torus;

static Torus createTorus(float major, float minor, int rings, int sides) {
    const int vertices = (rings + 1) * (sides + 1);
    Torus t;
    t.positions = malloc(sizeof(GLfloat) * 3 * vertices);
    t.normals = malloc(sizeof(GLfloat) * 3 * vertices);
    t.indexCount = rings * sides * 6;
    t.indices = malloc(sizeof(GLushort) * t.indexCount);
    GLfloat* position = t.positions;
    GLfloat* normal = t.normals;
    for (int i = 0; i <= rings; i++) {
        const float u = i * 2.0f * PI / rings;
        for (int j = 0; j <= sides; j++) {
            const float v = j * 2.0f * PI / sides;
            const float nx = cos(u) * cos(v), ny = sin(u) * cos(v), nz = sin(v);
            *position++ = cos(u) * major + minor * nx;
            *position++ = sin(u) * major + minor * ny;
            *position++ = minor * nz;
            *normal++ = nx;
            *normal++ = ny;
            *normal++ = nz;
        }
    }
    GLushort* index = t.indices;
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < sides; j++) {
            const GLushort a = i * (sides + 1) + j, b = a + sides + 1;
            *index++ = a;
            *index++ = b;
            *index++ = a + 1;
            *index++ = a + 1;
            *index++ = b;
            *index++ = b + 1;
        }
    }
    return t;
}

static void drawTorus(const Torus* t) {
    glVertexPointer(3, GL_FLOAT, 0, t->positions);
    glNormalPointer(GL_FLOAT, 0, t->normals);
    glDrawElements(GL_TRIANGLES, t->indexCount, GL_UNSIGNED_SHORT, t->indices);
}

// Sphere map of a simple world: sky gradient above the horizon, a checkered floor below. Texel (s, t) of a sphere map
// is the reflection of the view direction (0, 0, -1) at the normal n = (2s - 1, 2t - 1, nz)
static GLuint makeEnvironment(void) {
    enum { SIZE = 128 };
    GLubyte* texels = malloc(SIZE * SIZE * 4);
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < SIZE; x++) {
            GLubyte *p = &texels[(y * SIZE + x) * 4];
            const float nx = 2.0f * (x + 0.5f) / SIZE - 1.0f, ny = 2.0f * (y + 0.5f) / SIZE - 1.0f;
            const float nn = nx * nx + ny * ny;
            float c[3] = {0.0f, 0.0f, 0.0f};
            if (nn < 1.0f) {
                const float nz = sqrt(1.0f - nn);
                const float r[3] = {2.0f * nz * nx, 2.0f * nz * ny, 2.0f * nz * nz - 1.0f};
                if (r[1] >= 0.0f) {
                    // Sky: white at the horizon, deep blue above, a warm sun towards +x
                    const float h = r[1];
                    c[0] = 1.0f - 0.8f * h;
                    c[1] = 1.0f - 0.6f * h;
                    c[2] = 1.0f - 0.1f * h;
                    const float sun = pow(fmax(0.0f, r[0] * 0.8f + r[1] * 0.6f), 64.0f);
                    c[0] += sun;
                    c[1] += 0.9f * sun;
                    c[2] += 0.6f * sun;
                } else {
                    // Floor checkers, fading to grey towards the horizon
                    const float fx = r[0] / -r[1], fz = r[2] / -r[1];
                    const bool odd = (((int)(floor(fx))) + ((int)(floor(fz)))) & 1;
                    const float fade = fmin(1.0f, -r[1] * 3.0f);
                    const float base = odd ? 0.15f : 0.75f;
                    c[0] = 0.5f + (base - 0.5f) * fade;
                    c[1] = 0.45f + (base * 0.8f - 0.45f) * fade;
                    c[2] = 0.4f + (base * 0.5f - 0.4f) * fade;
                }
            }
            for (int k = 0; k < 3; k++) p[k] = ((GLubyte)(fmin(1.0f, c[k]) * 255.0f));
            p[3] = 255;
        }
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, SIZE, SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    free(texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    return tex;
}

// Stripes along s: a quarter dark red, the rest cream (repeats)
static GLuint makeStripes(void) {
    enum { W = 16, H = 8 };
    GLubyte texels[W * H * 4];
    for (int i = 0; i < W * H; i++) {
        const bool dark = (i % W) < 4;
        texels[4 * i] = dark ? 140 : 255;
        texels[4 * i + 1] = dark ? 20 : 240;
        texels[4 * i + 2] = dark ? 20 : 200;
        texels[4 * i + 3] = 255;
    }
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

static void printStats(void) {
    // Rows 2-5 of the console: timings of the last frame, frames per second over the last second; the cursor stays
    // where the text ended
    printf("\x1b[s");
    printf("\x1b[2;1HCPU:     %6.2fms\x1b[K", statsCpuMs());
    printf("\x1b[3;1HGPU:     %6.2fms\x1b[K", statsGpuMs());
    printf("\x1b[4;1HCmdBuf:  %6.2f%%\x1b[K", statsCmdBufUsage()*100.0f);
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
    printf("\x1b[5;1HFPS:     %6.2f\x1b[K", fps);
    printf("\x1b[u");
}

static void setupScene(void) {
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const double aspect = ((double)(C3DGL_TOP_SCREEN_WIDTH)) / C3DGL_SCREEN_HEIGHT;
    glFrustum(-aspect * 0.5, aspect * 0.5, -0.5, 0.5, 1.0, 50.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Light and eye plane are given with the identity modelview: both stay in eye space
    const GLfloat lightPos[4] = {0.3f, 0.6f, 1.0f, 0.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    const GLfloat stripePlane[4] = {1.5f, 1.5f, 0.0f, 0.0f};
    glTexGenfv(GL_S, GL_EYE_PLANE, stripePlane);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, stripePlane);
    const GLfloat white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, white);
    glMaterialfv(GL_FRONT, GL_SPECULAR, white);
    glMaterialf(GL_FRONT, GL_SHININESS, 40.0f);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHT0);
    glEnable(GL_TEXTURE_2D);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
}

static void drawFrame(const Torus torus, GLuint environment, GLuint stripes, float angle, float offset, float tiltX, float tiltY) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_TEXTURE);
    glLoadIdentity();
    glTranslatef(offset, 0.0f, 0.0f);
    glMatrixMode(GL_MODELVIEW);

    for (int i = 0; i < 3; i++) {
        glPushMatrix();
        glTranslatef((i - 1) * 2.9f, 0.0f, -8.0f);
        glRotatef(tiltX, 1.0f, 0.0f, 0.0f);
        glRotatef(tiltY + angle, 0.0f, 1.0f, 0.0f);
        glRotatef(angle * 0.7f, 0.0f, 0.0f, 1.0f);

        if (i == 0) {
            // Chrome: the environment replaces the color; the texture matrix stays out of it
            glMatrixMode(GL_TEXTURE);
            glPushMatrix();
            glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
            glBindTexture(GL_TEXTURE_2D, environment);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
            glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
            glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
            glEnable(GL_TEXTURE_GEN_S);
            glEnable(GL_TEXTURE_GEN_T);
            drawTorus(&torus);
            glDisable(GL_TEXTURE_GEN_T);
            glMatrixMode(GL_TEXTURE);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
        } else {
            // Lit stripes: s from a plane, t from the current texcoord (rows of the texture are all the same)
            glBindTexture(GL_TEXTURE_2D, stripes);
            glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
            glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, i == 1 ? GL_OBJECT_LINEAR : GL_EYE_LINEAR);
            glEnable(GL_TEXTURE_GEN_S);
            glEnable(GL_LIGHTING);
            drawTorus(&torus);
            glDisable(GL_LIGHTING);
        }
        glDisable(GL_TEXTURE_GEN_S);
        glPopMatrix();
    }
}

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    if (!c3dglInit()) {
        printf("c3dglInit failed\n");
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & KEY_START) break;
            gspWaitForVBlank();
        }
        gfxExit();
        return 1;
    }

    const Torus torus = createTorus(1.0f, 0.45f, 32, 16);
    const GLuint environment = makeEnvironment();
    const GLuint stripes = makeStripes();

    setupScene();

    printf("c3dgl texgen\n\n\n\n\n"
                "Tori without texcoords, glTexGen\n"
                "makes them:\n"
                "- left: GL_SPHERE_MAP (chrome)\n"
                "- middle: GL_OBJECT_LINEAR,\n"
                "  stripes turn with the torus\n"
                "- right: GL_EYE_LINEAR, stripes\n"
                "  stay fixed on the screen\n\n"
                "A: scroll the stripes (texture\n"
                "   matrix)\n"
                "Circle pad: turn the tori\n"
                "START: exit\n\n");
    printf("%s\n", glGetError() == GL_NO_ERROR ? "No GL errors" : "GL ERROR");

    bool scroll = false;
    float angle = 0.0f, offset = 0.0f, tiltX = 30.0f, tiltY = 0.0f;
    while (aptMainLoop()) {
        hidScanInput();
        printStats();
        const u32 keys = hidKeysDown();
        if (keys & KEY_START) break;
        if (keys & KEY_A) scroll = !scroll;
        circlePosition pad;
        hidCircleRead(&pad);
        tiltY += pad.dx / 50.0f;
        tiltX -= pad.dy / 50.0f;

        drawFrame(torus, environment, stripes, angle, offset, tiltX, tiltY);

        c3dglSwapBuffers();
        angle += 1.0f;
        if (scroll) offset += 0.01f;
    }

    glDeleteTextures(1, &environment);
    glDeleteTextures(1, &stripes);
    c3dglClose();
    gfxExit();
    return 0;
}
