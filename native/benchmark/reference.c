/* Same pixels and workload as ../examples/triangle.c; use the C renderer's
 * indexed persistent GPU cache rather than repeatedly decoding client arrays. */
#include <3ds.h>
#include <citro3d.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>

static FILE *results;
static u8 *last_output;
static void report(const char *format, ...) {
    va_list args;
    va_start(args,format); vprintf(format,args); va_end(args);
    if (results) { va_start(args,format); vfprintf(results,format,args); va_end(args); fflush(results); }
}
typedef struct { float x,y,z; u8 rgba[4]; } Vertex;
static const Vertex vertices[] = {
    {-.8f,-.7f,0,{255,0,0,255}},
    { .8f,-.7f,0,{0,255,0,255}},
    {   0, .8f,0,{0,0,255,255}},
};
static const GLushort indices[] = {0,1,2};
int main(void) {
    gfxInitDefault(); consoleInit(GFX_BOTTOM,NULL);
    results=fopen("sdmc:/c3dgl-reference-results.txt","w");
    report("C c3dgl + Citro3D reference\nPersistent cached indexed VBO\n");
    if (!c3dglInit()) { report("FAIL init\n"); return 1; }
    GLuint vbo,ibo;
    glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glGenBuffers(1,&ibo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ibo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices),indices,GL_STATIC_DRAW);
    glVertexPointer(3,GL_FLOAT,sizeof(Vertex),0);
    glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(Vertex),(void *)offsetof(Vertex,rgba));
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glClearColor(24.0f/255,48.0f/255,80.0f/255,1);
    const unsigned loads[]={1,64,512};
    for (unsigned load=0; load<3; load++) {
        u64 total=0; double cpu=0,gpu=0; unsigned samples=0;
        for (unsigned i=0; i<150 && aptMainLoop(); i++) {
            c3dglGetGpuWaitMs(); // Reset explicit GPU/VBlank waiting counter.
            u64 start=svcGetSystemTick();
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            for (unsigned j=0; j<loads[load]; j++) glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
            last_output=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
            c3dglSwapBuffers();
            double cpuMs=(double)(svcGetSystemTick()-start)*1000/SYSCLOCK_ARM11 - c3dglGetGpuWaitMs();
            /* Complete rendering/transfer and expose the same total latency as
             * the native GetStats wait, without adding a presentation VBlank. */
            if (!C3D_FrameBegin(0)) { report("FAIL completion\n"); return 1; }
            float gpuMs; c3dglGetFrameStats(&gpuMs,NULL,NULL);
            C3D_FrameEnd(GX_CMDLIST_FLUSH);
            if (i>=30) { samples++; cpu+=cpuMs; gpu+=gpuMs; total+=svcGetSystemTick()-start; }
        }
        if (samples!=120) { report("FAIL incomplete benchmark\n"); return 1; }
        report("draws=%u CPU %.4f GPU %.4f\n",loads[load],
            cpu/120,gpu/120);
        report("total %.4f ms/frame\n",(double)total*1000/SYSCLOCK_ARM11/120);
    }
    if (!C3D_FrameBegin(0)) return 1;
    svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)last_output,400*240*3);
    FILE *image=fopen("sdmc:/c3dgl-reference.ppm","wb");
    if (image) {
        fprintf(image,"P6\n400 240\n255\n");
        for(int y=239;y>=0;y--) for(int x=0;x<400;x++) {
            const u8 *p=last_output+(x*240+y)*3;
            u8 rgb[]={p[2],p[1],p[0]}; fwrite(rgb,1,3,image);
        }
        fclose(image);
    }
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    report("GL error=%u - START exits\n",glGetError());
    while (aptMainLoop()) { hidScanInput(); if (hidKeysDown()&KEY_START) break; gspWaitForVBlank(); }
    glDeleteBuffers(1,&vbo); glDeleteBuffers(1,&ibo);
    c3dglClose(); if(results) fclose(results); gfxExit(); return 0;
}
