/* c3dgl C example: a spinning, textured and vertex-colored cube.
 *
 * Uses the fixed-function GL API (client arrays, depth test, back-face culling,
 * 2D texture) on top of libctru. A toggles nearest/linear filtering, START exits. */
#include <3ds.h>
#include <GL/gl.h>
#include <c3dgl_native.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    float position[3];
    float uv[2];
    uint8_t color[4];
} Vertex;

#define FACE(nx, ny, nz, ux, uy, uz, vx, vy, vz, r, g, b)                                            \
    {{(nx - ux - vx) * .5f, (ny - uy - vy) * .5f, (nz - uz - vz) * .5f}, {0, 0}, {r, g, b, 255}},   \
    {{(nx + ux - vx) * .5f, (ny + uy - vy) * .5f, (nz + uz - vz) * .5f}, {1, 0}, {r, g, b, 255}},   \
    {{(nx + ux + vx) * .5f, (ny + uy + vy) * .5f, (nz + uz + vz) * .5f}, {1, 1}, {r, g, b, 255}},   \
    {{(nx - ux + vx) * .5f, (ny - uy + vy) * .5f, (nz - uz + vz) * .5f}, {0, 1}, {r, g, b, 255}}

/* Each face is given by its normal and two tangent axes; winding is counter-clockwise from outside. */
static const Vertex vertices[24] = {
    FACE(0, 0, 1, 1, 0, 0, 0, 1, 0, 255, 255, 255),   /* +Z */
    FACE(0, 0, -1, -1, 0, 0, 0, 1, 0, 255, 160, 160),  /* -Z */
    FACE(1, 0, 0, 0, 0, -1, 0, 1, 0, 160, 255, 160),   /* +X */
    FACE(-1, 0, 0, 0, 0, 1, 0, 1, 0, 160, 160, 255),   /* -X */
    FACE(0, 1, 0, 1, 0, 0, 0, 0, -1, 255, 255, 160),   /* +Y */
    FACE(0, -1, 0, 1, 0, 0, 0, 0, 1, 160, 255, 255),   /* -Y */
};

static uint16_t indices[36];
static uint8_t texels[16 * 16 * 4];

static void set_filter(GLint filter) {
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    if (!c3dglInit()) {
        printf("c3dglInit failed\n");
        svcSleepThread(2000000000LL);
        gfxExit();
        return 1;
    }

    /* Two triangles per face. */
    for (unsigned face = 0; face < 6; face++) {
        static const uint16_t quad[6] = {0, 1, 2, 0, 2, 3};
        for (unsigned i = 0; i < 6; i++) indices[face * 6 + i] = (uint16_t)(face * 4 + quad[i]);
    }

    /* 16x16 checkerboard of 4x4-pixel cells. */
    for (unsigned y = 0; y < 16; y++) {
        for (unsigned x = 0; x < 16; x++) {
            uint8_t *p = &texels[(y * 16 + x) * 4];
            uint8_t v = ((x / 4) ^ (y / 4)) & 1 ? 255 : 90;
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    int linear = 0;
    set_filter(GL_NEAREST);
    glEnable(GL_TEXTURE_2D);

    glVertexPointer(3, GL_FLOAT, sizeof(Vertex), &vertices[0].position);
    glTexCoordPointer(2, GL_FLOAT, sizeof(Vertex), &vertices[0].uv);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Vertex), &vertices[0].color);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -.6, .6, 1, 10); /* 400x240 top screen is 5:3 */
    glMatrixMode(GL_MODELVIEW);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(.09f, .14f, .24f, 1);

    float angle = 0;
    unsigned frame = 0;
    int status = 0;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_START) break;
        if (down & KEY_A) {
            linear = !linear;
            set_filter(linear ? GL_LINEAR : GL_NEAREST);
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glLoadIdentity();
        glTranslatef(0, 0, -2.6f);
        glRotatef(30, 1, 0, 0);
        glRotatef(angle, 0, 1, 0);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, indices);

        if (c3dglNativePresent() != 0 || glGetError() != GL_NO_ERROR) {
            printf("frame %u failed\n", frame);
            status = 1;
            break;
        }

        if (frame++ % 30 == 0) {
            C3DGLnativeStats stats;
            if (c3dglNativeGetStats(&stats) == 0)
                printf("\x1b[1;1Hc3dgl C example\nCPU %6.3f ms  draws %lu   \n%s filtering\n\nA: toggle filter\nSTART: exit\n",
                       (double)stats.cpu_ticks * 1000 / SYSCLOCK_ARM11, (unsigned long)stats.draws,
                       linear ? "linear " : "nearest");
        }
        angle += .8f;
        if (angle >= 360) angle -= 360;
    }

    glDeleteTextures(1, &texture);
    c3dglClose();
    gfxExit();
    return status;
}
