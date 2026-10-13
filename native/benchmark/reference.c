/* Same pixels and workload as ../examples/triangle.c; use the C renderer's
 * indexed persistent GPU cache rather than repeatedly decoding client arrays. */
#include <3ds.h>
#include <citro3d.h>
#include <GL/gl.h>
#include <c3dgl.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include "../../src/c3dgl.c"
#define C3DGL_REFERENCE_TEST
#include "../tests/depth_scene.h"

static FILE *results;
static u8 *last_output;
static void report(const char *format, ...) {
    va_list args;
    va_start(args,format); vprintf(format,args); va_end(args);
    if (results) { va_start(args,format); vfprintf(results,format,args); va_end(args); fflush(results); }
}
typedef struct { float x,y,z; u8 rgba[4]; } ReferenceVertex;
static const ReferenceVertex vertices[] = {
    {-.8f,-.7f,0,{255,0,0,255}},
    { .8f,-.7f,0,{0,255,0,255}},
    {   0, .8f,0,{0,0,255,255}},
};
static const GLushort indices[] = {0,1,2};
static void transform(unsigned draw) {
    const float m[]={.005f,0,0,0, 0,.005f,0,0, 0,0,.01f,0, (draw&1)? .125f:-.125f,0,0,1};
    glLoadMatrixf(m);
}
static void save_image(const char *name) {
    svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)last_output,400*240*3);
    FILE *image=fopen(name,"wb");
    if (!image) { report("FAIL image\n"); return; }
    fprintf(image,"P6\n400 240\n255\n");
    for(int y=239;y>=0;y--) for(int x=0;x<400;x++) {
        const u8 *p=last_output+(x*240+y)*3;
        u8 rgb[]={p[2],p[1],p[0]}; fwrite(rgb,1,3,image);
    }
    fclose(image);
}
int main(void) {
    gfxInitDefault(); consoleInit(GFX_BOTTOM,NULL);
    results=fopen("sdmc:/c3dgl-reference-results.txt","w");
    report("C c3dgl + Citro3D reference\nPersistent cached indexed VBO\nReference Z-buffer only (C3DGL_W_DEPTH=0)\n");
    if (!c3dglInit()) { report("FAIL init\n"); return 1; }
    GLuint initial[2];compact_setup(compact_triangle,sizeof(compact_triangle),indices,sizeof(indices),initial);
    GLuint vbo=initial[0],ibo=initial[1];
    glClearColor(24.0f/255,48.0f/255,80.0f/255,1);
    const unsigned loads[]={1,64,512};
    for (unsigned load=0; load<21; load++) {
        bool matrix_workload=load>=3 && load<6;
        bool viewport_workload=load>=6 && load<9;
        bool update_workload=load>=12 && load<15;
        GpuBufferCache *resident=NULL;
        bool cube_workload=load>=15,raster_workload=load>=18;
        if(load==15) {GLuint buffers[2];glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ibo);cube_setup(buffers);vbo=buffers[0];ibo=buffers[1];}
        glViewport(0,0,400,240);
        glLoadIdentity();
        if(!cube_workload) glScalef(.01f,.01f,.01f);
        u64 total=0; double cpu=0,gpu=0; unsigned samples=0;
        for (unsigned i=0; i<150 && aptMainLoop(); i++) {
            c3dglGetGpuWaitMs(); // Reset explicit GPU/VBlank waiting counter.
            u64 start=svcGetSystemTick();
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            if (cube_workload) cube_transform(i);
            if (update_workload) {
                const int16_t position[]={-80+((i&1)? 5:-5),-70,0};
                glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(position),position);
                glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,sizeof(indices),indices);
            }
            for (unsigned j=0; j<loads[load%3]; j++) {
                if (raster_workload) cube_state(j+i);
                if (matrix_workload) transform(j+i);
                if (viewport_workload) glViewport((j+i)&1?200:0,(j+i)&1?120:0,200,120);
                glDrawElements(GL_TRIANGLES,cube_workload?36:3,GL_UNSIGNED_SHORT,0);
            }
            last_output=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);
            c3dglSwapBuffers();
            double cpuMs=(double)(svcGetSystemTick()-start)*1000/SYSCLOCK_ARM11 - c3dglGetGpuWaitMs();
            /* Complete rendering/transfer and expose the same total latency as
             * the native GetStats wait, without adding a presentation VBlank. */
            if (!C3D_FrameBegin(0)) { report("FAIL completion\n"); return 1; }
            float gpuMs; c3dglGetFrameStats(&gpuMs,NULL,NULL);
            if(!update_workload) {
                if(resident && resident!=gl.buffers[vbo].gpuCache) {report("FAIL reference rebuilt unchanged GPU cache\n");return 1;}
                resident=gl.buffers[vbo].gpuCache;
            }
            if(i==0) {
                if(gl.buffers[vbo].gpuCache==NULL || !gl.buffers[vbo].gpuCache->compact) {report("FAIL reference GPU cache not resident\n");return 1;}
                report("PASS reference resident GPU cache workload=%s draws=%u\n",raster_workload?"raster":cube_workload?"cube":update_workload?"update":load>=9?"indexed":viewport_workload?"viewport":matrix_workload?"matrix":"static",loads[load%3]);
            }
            C3D_FrameEnd(GX_CMDLIST_FLUSH);
            if (i>=30) { samples++; cpu+=cpuMs; gpu+=gpuMs; total+=svcGetSystemTick()-start; }
        }
        if (samples!=120) { report("FAIL incomplete benchmark\n"); return 1; }
        report("workload=%s draws=%u CPU %.4f GPU %.4f\n",raster_workload?"raster":cube_workload?"cube":update_workload?"update":load>=9?"indexed":viewport_workload?"viewport":matrix_workload?"matrix":"static",loads[load%3],
            cpu/120,gpu/120);
        report("total %.4f ms/frame\n",(double)total*1000/SYSCLOCK_ARM11/120);
        if (load%3==2) {
            if (!C3D_FrameBegin(0)) return 1;
            save_image(load==20?"sdmc:/c3dgl-reference-raster.ppm":load==17?"sdmc:/c3dgl-reference-cube.ppm":load==14?"sdmc:/c3dgl-reference-update.ppm":load==11?"sdmc:/c3dgl-reference-indexed.ppm":load==8?"sdmc:/c3dgl-reference-viewport.ppm":load==2?"sdmc:/c3dgl-reference.ppm":"sdmc:/c3dgl-reference-matrix.ppm");
            C3D_FrameEnd(GX_CMDLIST_FLUSH);
        }
    }
    glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ibo);
    GLuint reset[2];scene_defaults();scene_setup((const SceneVertex *)vertices,sizeof(vertices),indices,sizeof(indices),reset);vbo=reset[0];ibo=reset[1];
    glLoadIdentity(); glViewport(0,0,400,240);
    glClearColor(.75f,.25f,.125f,1); glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
    glClearColor(24.f/255,48.f/255,80.f/255,1); glClear(GL_COLOR_BUFFER_BIT);
    glViewport(0,0,0,0); glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
    glViewport(0,0,400,240); glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
    glClear(GL_DEPTH_BUFFER_BIT); glViewport(0,0,200,120);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
    last_output=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL); c3dglSwapBuffers();
    if (!C3D_FrameBegin(0)) return 1;
    save_image("sdmc:/c3dgl-reference-state.ppm");
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    /* Same final array scene as native C/C++ and Rust: previous draws retain old data. */
    ReferenceVertex yellow[3];
    for(unsigned i=0;i<3;i++) {yellow[i]=vertices[i];yellow[i].rgba[0]=255;yellow[i].rgba[1]=255;yellow[i].rgba[2]=0;}
    glViewport(0,0,200,240);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
    glBufferData(GL_ARRAY_BUFFER,sizeof(yellow),yellow,GL_DYNAMIC_DRAW);
    glViewport(200,0,200,240);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,0);
    glDeleteBuffers(1,&vbo);glDeleteBuffers(1,&ibo);
    last_output=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);c3dglSwapBuffers();
    if (!C3D_FrameBegin(0)) return 1;
    save_image("sdmc:/c3dgl-reference-arrays.ppm");
    C3D_FrameEnd(GX_CMDLIST_FLUSH);
    scene_defaults();GLuint depth_buffers[2];scene_setup(depth_vertices,sizeof(depth_vertices),depth_indices,sizeof(depth_indices),depth_buffers);
    glClearColor(24.f/255,48.f/255,80.f/255,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    for(unsigned cell=0;cell<20;cell++) depth_cell(cell);
    last_output=gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);c3dglSwapBuffers();
    if(!C3D_FrameBegin(0)) return 1;
    svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE,(u32)last_output,400*240*3);
    for(unsigned cell=0;cell<20;cell++) {
        const uint8_t *p=last_output+(((cell%5)*80+40)*240+(cell/5)*60+30)*3;
        if(p[2]!=depth_expected[cell][0] || p[1]!=depth_expected[cell][1] || p[0]!=depth_expected[cell][2]) {report("FAIL depth cell %u: %u,%u,%u\n",cell,p[2],p[1],p[0]);return 1;}
    }
    save_image("sdmc:/c3dgl-reference-depth.ppm");C3D_FrameEnd(GX_CMDLIST_FLUSH);
    report("PASS 20 depth/cull cells\n");glDeleteBuffers(2,depth_buffers);scene_defaults();
    report("GL error=%u - START exits\n",glGetError());
    while (aptMainLoop()) { hidScanInput(); if (hidKeysDown()&KEY_START) break; gspWaitForVBlank(); }
    glDeleteBuffers(1,&vbo); glDeleteBuffers(1,&ibo);
    c3dglClose(); if(results) fclose(results); gfxExit(); return 0;
}
