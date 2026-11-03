// c3dglDrawMeshes() against the GL calls it stands for: the same meshes in the same states, drawn both ways, have to
// give the same top screen byte for byte, leave the same matrix and array state, and take the direct way where they can.
#define C3DGL_PROFILE
#include "../src/c3dgl.c"

typedef struct { s16 pos[3], pad, uv[2]; u8 color[4]; } Packed;
static FILE *logFile;
static int checks, failures;
#define CHECK(expr) do { checks++; if (!(expr)) { failures++; fprintf(logFile,"FAIL line %d: %s\n",__LINE__,#expr); fflush(logFile); } } while (0)

enum { V_TEXTURE = 1, V_FOG = 2, V_FLAT = 4, V_DEPTH = 8, V_BLEND = 16 };
static const int variants[] = { 0, V_TEXTURE, V_FOG, V_FOG | V_TEXTURE, V_FLAT, V_FLAT | V_TEXTURE, V_DEPTH | V_FOG, V_BLEND | V_TEXTURE };

#define MESHES 12
static GLuint vbo[MESHES], ibo, texture;
static C3DGLmesh meshes[MESHES];

// Quads of 16-byte vertices, a few per mesh; mesh 5 has corners of different colors, so flat shading cannot use its
// compact cache and it goes through GL inside the run
static void makeMesh(int k)
{
    Packed v[16];
    for (int q = 0; q < 4; q++)
        for (int c = 0; c < 4; c++)
        {
            Packed *p = &v[q*4 + c];
            memset(p, 0, sizeof(*p));
            p->pos[0] = (s16)((q % 2)*512 + ((c == 1 || c == 2)? 512 : 0));
            p->pos[1] = (s16)((q / 2)*512 + ((c >= 2)? 512 : 0));
            p->pos[2] = (s16)(k*37 % 256);
            p->uv[0] = (s16)((c == 1 || c == 2)? 16384 : 0);
            p->uv[1] = (s16)((c >= 2)? 16384 : 0);
            u8 shade = (u8)(80 + k*13 + q*20);
            p->color[0] = shade; p->color[1] = (u8)(255 - shade); p->color[2] = (u8)(k*20); p->color[3] = 200;
            if (k == 5) p->color[0] = (u8)(c*60);
        }
    glBindBuffer(GL_ARRAY_BUFFER, vbo[k]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    meshes[k].buffer = vbo[k];
    meshes[k].count = 24;
    meshes[k].x = 12.0f + (float)(k % 6)*62.0f;
    meshes[k].y = 20.0f + (float)(k / 6)*100.0f;
    meshes[k].z = -(float)(k % 4);
}

static void projection(float zNear, float zFar)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 400, 0, 240, zNear, zFar);
    glMatrixMode(GL_MODELVIEW);
}

static void scene(int v, bool direct)
{
    glDisable(GL_SCISSOR_TEST); glColorMask(1, 1, 1, 1); glDepthMask(GL_TRUE);
    glClearColor(0.1f, 0.2f, 0.3f, 1); glClearDepth(1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    projection(-10, 10);
    glLoadIdentity();
    glTranslatef(3, 2, 0);
    glShadeModel((v & V_FLAT)? GL_FLAT : GL_SMOOTH);
    if (v & V_TEXTURE) { glEnable(GL_TEXTURE_2D); glMatrixMode(GL_TEXTURE); glLoadIdentity(); glScalef(1.0f/16384, 1.0f/16384, 1); glMatrixMode(GL_MODELVIEW); }
    else glDisable(GL_TEXTURE_2D);
    if (v & V_FOG) { glEnable(GL_FOG); glFogi(GL_FOG_MODE, GL_LINEAR); glFogf(GL_FOG_START, 0); glFogf(GL_FOG_END, 6); }
    else glDisable(GL_FOG);
    if (v & V_DEPTH) { glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); } else glDisable(GL_DEPTH_TEST);
    if (v & V_BLEND) { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); } else glDisable(GL_BLEND);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    const float scale = 1.0f/16;
    for (int run = 0; run < 2; run++)
    {
        // The second run under another projection: with fog on, the fog table follows it
        if (run == 1) projection(-20, 6);
        const C3DGLmesh *part = &meshes[run*6];
        if (direct) c3dglDrawMeshes(part, 6, scale);
        else for (int k = 0; k < 6; k++) drawMeshGL(&part[k], scale);
        // Immediate geometry right after a run: it must get its own matrix, not the last mesh's
        glBegin(GL_TRIANGLES);
        glColor4ub(255, 255, 255, 255); glTexCoord2f(0, 0);
        glVertex3f(150, 200, 0); glVertex3f(180, 200, 0); glVertex3f(165, 230, 0);
        glEnd();
    }
    glDisable(GL_FOG); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_TEXTURE); glLoadIdentity(); glMatrixMode(GL_MODELVIEW);
}

int main(void)
{
    gfxInitDefault(); consoleInit(GFX_BOTTOM, NULL);
    logFile = fopen("sdmc:/c3dgl-draw-meshes-regression.log", "w");
    if (!logFile || !c3dglInit()) return 1;
    glViewport(0, 0, 400, 240);

    glGenBuffers(MESHES, vbo); glGenBuffers(1, &ibo);
    for (int k = 0; k < MESHES; k++) makeMesh(k);
    u16 indices[24];
    for (int q = 0; q < 4; q++) { const u16 p[6] = {0,1,2,0,2,3}; for (int i = 0; i < 6; i++) indices[q*6 + i] = (u16)(q*4 + p[i]); }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo); glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    u8 texels[8*8*4];
    for (int i = 0; i < 64; i++) { texels[4*i] = i*3; texels[4*i + 1] = 255 - i*3; texels[4*i + 2] = (i & 1)? 220 : 60; texels[4*i + 3] = 255; }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, texels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    static u8 reference[400*240*4], actual[sizeof(reference)];
    const int count = (int)(sizeof(variants)/sizeof(variants[0]));
    for (int k = 0; k < count; k++)
    {
        const int v = variants[k];
        // Once without measuring: the caches of this shade model are made by the GL way
        scene(v, false);
        c3dglSwapBuffers();
        scene(v, false);
        glReadPixels(0, 0, 400, 240, GL_RGBA, GL_UNSIGNED_BYTE, reference);
        Mat4 mvGL = gl.stack[0][gl.stackDepth[0]];
        ClientArray arraysGL[3] = { gl.arrays[ARRAY_VERTEX], gl.arrays[ARRAY_TEXCOORD0], gl.arrays[ARRAY_COLOR] };
        GLuint bindingGL = gl.arrayBuffer;
        c3dglSwapBuffers();

        const u32 before = prof.calls[PB_MESHES];
        scene(v, true);
        glReadPixels(0, 0, 400, 240, GL_RGBA, GL_UNSIGNED_BYTE, actual);
        const u32 direct = prof.calls[PB_MESHES] - before;
        c3dglSwapBuffers();

        const bool same = memcmp(reference, actual, sizeof(reference)) == 0;
        if (!same)
        {
            int first = -1, diff = 0;
            for (int i = 0; i < (int)sizeof(reference); i++) if (reference[i] != actual[i]) { if (first < 0) first = i; diff++; }
            fprintf(logFile, "variant 0x%02x: %d bytes differ, first at pixel %d\n", v, diff, first/4);
        }
        CHECK(same);
        // All meshes direct, except mesh 5 under flat shading (its triangles are not one color)
        CHECK(direct == (u32)((v & V_FLAT)? MESHES - 1 : MESHES));
        CHECK(memcmp(&mvGL, &gl.stack[0][gl.stackDepth[0]], sizeof(Mat4)) == 0);
        CHECK(memcmp(arraysGL, &gl.arrays[ARRAY_VERTEX], sizeof(ClientArray)) == 0);
        CHECK(memcmp(&arraysGL[1], &gl.arrays[ARRAY_TEXCOORD0], sizeof(ClientArray)) == 0);
        CHECK(memcmp(&arraysGL[2], &gl.arrays[ARRAY_COLOR], sizeof(ClientArray)) == 0);
        CHECK(bindingGL == gl.arrayBuffer);
        fprintf(logFile, "variant 0x%02x: %u direct\n", v, (unsigned)direct); fflush(logFile);
    }
    CHECK(glGetError() == GL_NO_ERROR);

    glDeleteTextures(1, &texture); glDeleteBuffers(MESHES, vbo); glDeleteBuffers(1, &ibo);
    c3dglClose();
    fprintf(logFile, "checks=%d failures=%d\n", checks, failures); fclose(logFile);
    printf("Draw meshes: %d checks, %d failures\n", checks, failures);
    gfxExit(); return failures? 1 : 0;
}
