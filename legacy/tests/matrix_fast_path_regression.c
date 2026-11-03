// A new batch whose state differs only in the matrices skips applyState() (useState(), matrixFastPath). The same
// scenes are rendered with and without that path and the top screen has to come out identical, byte for byte.
#define C3DGL_PROFILE
#include "../src/c3dgl.c"

typedef struct { s16 pos[3], pad, uv[2]; u8 color[4]; } Packed;
static Packed vertices[4] = {
    {{0,0,0},0,{0,0},{255,40,40,200}}, {{24,0,0},0,{1,0},{40,255,40,255}},
    {{24,24,0},0,{1,1},{40,40,255,120}}, {{0,24,0},0,{0,1},{255,255,40,255}}
};
static u16 indices[] = {0,1,2,0,2,3};
static FILE *logFile;
static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; fprintf(logFile,"FAIL line %d: %s\n",__LINE__,#expr); fflush(logFile); } } while (0)

enum {
    V_TEXTURE = 1, V_FOG = 2, V_STIPPLE = 4, V_DEPTH = 8, V_BLEND = 16, V_FLAT = 32, V_PIXELS = 64, V_ALPHA = 128
};
static const int variants[] = {
    0, V_TEXTURE, V_FOG, V_FOG | V_TEXTURE, V_STIPPLE, V_STIPPLE | V_TEXTURE, V_DEPTH, V_BLEND | V_TEXTURE,
    V_FLAT, V_PIXELS, V_PIXELS | V_FOG | V_TEXTURE, V_ALPHA | V_TEXTURE, V_DEPTH | V_FOG | V_BLEND | V_FLAT | V_PIXELS
};

static GLuint vbo, ibo, texture;
static u8 stipplePattern[128];
static u8 pixelRect[8*8*4];

static void bindArrays(void)
{
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_SHORT, sizeof(Packed), 0);
    glTexCoordPointer(2, GL_SHORT, sizeof(Packed), (void *)8);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(Packed), (void *)12);
}

static void projection(float zNear, float zFar)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 400, 0, 240, zNear, zFar);
    glMatrixMode(GL_MODELVIEW);
}

// A row of meshes, each with a matrix of its own, as the chunk and entity renderers draw them
static void scene(int v)
{
    glDisable(GL_SCISSOR_TEST); glColorMask(1, 1, 1, 1); glDepthMask(GL_TRUE);
    glClearColor(0.1f, 0.2f, 0.3f, 1); glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    projection(-10, 10);
    glLoadIdentity();
    glShadeModel((v & V_FLAT)? GL_FLAT : GL_SMOOTH);
    if (v & V_TEXTURE) { glEnable(GL_TEXTURE_2D); glMatrixMode(GL_TEXTURE); glLoadIdentity(); glScalef(2, 2, 1); glMatrixMode(GL_MODELVIEW); }
    else glDisable(GL_TEXTURE_2D);
    if (v & V_FOG) { glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 0); glFogf(GL_FOG_END, 8); }
    else glDisable(GL_FOG);
    if (v & V_STIPPLE) { glEnable(GL_POLYGON_STIPPLE); glPolygonStipple(stipplePattern); }
    else glDisable(GL_POLYGON_STIPPLE);
    if (v & V_DEPTH) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); }
    else glDisable(GL_DEPTH_TEST);
    if (v & V_BLEND) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }
    else glDisable(GL_BLEND);
    if (v & V_ALPHA) { glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER, 0.6f); }
    else glDisable(GL_ALPHA_TEST);

    bindArrays();
    for (int i = 0; i < 40; i++)
    {
        // The projection changes halfway: with fog on, the fog table has to follow it
        if (i == 20) projection(-20, 6);

        glPushMatrix();
        glTranslatef(8 + (i % 10)*38, 8 + (i / 10)*56, -(float)(i % 7));
        if (i % 3 == 0) glScalef(1.25f, 0.75f, 1);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

        // Immediate geometry between the cached meshes goes into the ordinary batch
        if (i % 4 == 1)
        {
            glBegin(GL_TRIANGLES);
            glColor4ub(255, 0, 255, 255); glTexCoord2f(0, 0); glVertex3f(4, 30, 1);
            glColor4ub(0, 255, 255, 255); glTexCoord2f(1, 0); glVertex3f(30, 30, 1);
            glColor4ub(255, 255, 255, 255); glTexCoord2f(0, 1); glVertex3f(16, 46, 1);
            glEnd();
        }
        glPopMatrix();

        // Pixel rectangles are drawn in clip space, with state of their own
        if ((v & V_PIXELS) && (i % 9 == 4))
        {
            glRasterPos2i(10 + i*9, 200);
            glDrawPixels(8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixelRect);
            bindArrays();
        }
    }
    glDisable(GL_POLYGON_STIPPLE); glDisable(GL_FOG); glDisable(GL_BLEND); glDisable(GL_ALPHA_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
}

int main(void)
{
    gfxInitDefault(); consoleInit(GFX_BOTTOM, NULL);
    logFile = fopen("sdmc:/c3dgl-matrix-fast-path-regression.log", "w");
    if (!logFile || !c3dglInit()) return 1;
    glViewport(0, 0, 400, 240);

    glGenBuffers(1, &vbo); glGenBuffers(1, &ibo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    u8 texels[8*8*4];
    for (int i = 0; i < 64; i++) { texels[4*i] = i*3; texels[4*i + 1] = 255 - i*3; texels[4*i + 2] = (i & 1)? 220 : 60; texels[4*i + 3] = (i % 5)? 255 : 90; }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (int i = 0; i < 128; i++) stipplePattern[i] = ((i / 4) & 1)? 0xAA : 0x55;
    for (int i = 0; i < 64; i++) { pixelRect[4*i] = 255; pixelRect[4*i + 1] = i*4; pixelRect[4*i + 2] = 0; pixelRect[4*i + 3] = 255; }

    static u8 reference[400*240*4], actual[sizeof(reference)];
    const int count = (int)(sizeof(variants)/sizeof(variants[0]));
    for (int k = 0; k < count; k++)
    {
        const int v = variants[k];
        for (int fast = 0; fast < 2; fast++)
        {
            matrixFastPath = fast;
            const u32 before = prof.calls[PB_APPLY_MATRIX];
            scene(v);
            glReadPixels(0, 0, 400, 240, GL_RGBA, GL_UNSIGNED_BYTE, fast? actual : reference);
            const u32 used = prof.calls[PB_APPLY_MATRIX] - before;
            if (fast) CHECK(used > 0);      // the path was taken
            else CHECK(used == 0);
            c3dglSwapBuffers();
        }
        const bool same = memcmp(reference, actual, sizeof(reference)) == 0;
        if (!same)
        {
            int first = -1, diff = 0;
            for (int i = 0; i < (int)sizeof(reference); i++) if (reference[i] != actual[i]) { if (first < 0) first = i; diff++; }
            fprintf(logFile, "variant 0x%02x: %d bytes differ, first at pixel %d\n", v, diff, first/4);
        }
        CHECK(same);
        fprintf(logFile, "variant 0x%02x done\n", v); fflush(logFile);
    }
    matrixFastPath = true;
    CHECK(glGetError() == GL_NO_ERROR);

    glDeleteTextures(1, &texture); glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ibo);
    c3dglClose();
    fprintf(logFile, "checks=%d failures=%d\n", checks, failures); fclose(logFile);
    printf("Matrix fast path: %d checks, %d failures\n", checks, failures);
    gfxExit(); return failures? 1 : 0;
}
