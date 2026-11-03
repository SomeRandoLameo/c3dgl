/* GPU acceptance test and benchmark. Runs automatically; START exits. */
#include <3ds.h>
#include "c3dgl_native.h"
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../tests/depth_scene.h"

static const C3DGLnativeVertex vertices[] = {
    {{-.8f, -.7f, 0, 1}, {1, 0, 0, 1}},
    {{ .8f, -.7f, 0, 1}, {0, 1, 0, 1}},
    {{   0, .8f, 0, 1}, {0, 0, 1, 1}},
};

static uint8_t pixels[400*240*3];

static int failures;
static unsigned checked_frames;
static FILE *results;
static void report(const char *format, ...) {
    va_list args;
    va_start(args,format); vprintf(format,args); va_end(args);
    if (results) { va_start(args,format); vfprintf(results,format,args); va_end(args); fflush(results); }
}
#define printf report

static int check(int result, const char *where) {
    if (result != 0) {
        printf("FAIL %s: %d\n", where, result);

        failures++;

        return 0;
    }

    return 1;
}

static int pixel(int x, int y, int red, int green, int blue, int tolerance) {
    const uint8_t *p = pixels + (x * 240 + y) * 3;
    if (abs((int)p[2]-red) > tolerance || abs((int)p[1]-green) > tolerance || abs((int)p[0]-blue) > tolerance) {
        printf("FAIL pixel %d,%d = %d,%d,%d\n", x, y, p[2] ,p[1] ,p[0]);
        failures++;
        return 0;
    }

    return 1;
}

static bool matrix_workload;
static bool viewport_workload,gl_frame;
static unsigned matrix_phase;
static bool indexed_workload, update_workload, cube_workload, raster_workload;
static GLuint workload_buffers[2];
static const C3DGLnativeVertex benchmark_vertices[]={
    {{-80,-70,0,1},{1,0,0,1}},{{80,-70,0,1},{0,1,0,1}},{{0,80,0,1},{0,0,1,1}}
};
static void indexed_setup(void) {
    const uint16_t indices[]={0,1,2};
    compact_setup(compact_triangle,sizeof(compact_triangle),indices,sizeof(indices),workload_buffers);
}
static void transform(unsigned draw) {
    const float m[]={.005f,0,0,0, 0,.005f,0,0, 0,0,.01f,0, (draw&1)? .125f:-.125f,0,0,1};
    glLoadMatrixf(m);
}

static int frame(unsigned clear, unsigned draws, bool sync) {
    if (gl_frame) glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    else if (!check(c3dglNativeBegin(clear,sync), "begin")) return 0;

    if (cube_workload) cube_transform(matrix_phase);
    if (update_workload) {
        const int16_t position[]={(int16_t)(-80+((matrix_phase&1)? 5:-5)),-70,0};
        const uint16_t indices[]={0,1,2};
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(position),position);
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,sizeof(indices),indices);
    }
    for (unsigned j = 0; j < draws; j++) {
        if (raster_workload) cube_state(j+matrix_phase);
        if (matrix_workload) transform(j+matrix_phase);
        if (viewport_workload) glViewport((j+matrix_phase)&1?200:0,(j+matrix_phase)&1?120:0,200,120);
        if (indexed_workload) glDrawElements(GL_TRIANGLES,cube_workload?36:3,GL_UNSIGNED_SHORT,NULL);
        else if (!check(c3dglNativeDraw(0,3), "draw")) return 0;
    }

    return check(c3dglNativePresent(), "present");
}

static void validate(unsigned clear, bool triangle) {
    if (!check(c3dglNativeReadback(pixels,sizeof(pixels)), "readback")) return;
    pixel(5, 5, clear >> 24, (clear >> 16) & 255, (clear >> 8) & 255, 0);

    if (triangle) {
        pixel(200, 100, 82, 82, 91, 4);
        pixel(200, 180, 26, 26, 204, 5);
        pixel(100, 60, 188, 29, 38, 5);
    } else pixel(200, 100, clear >> 24, (clear >> 16) & 255, (clear >> 8) & 255, 0);
}


static void save_image(unsigned workload) {
#ifdef __cplusplus
    FILE *image=fopen(workload==8?"sdmc:/c3dgl-native-cpp-depth.ppm":workload==7?"sdmc:/c3dgl-native-cpp-raster.ppm":workload==6?"sdmc:/c3dgl-native-cpp-cube.ppm":workload==5?"sdmc:/c3dgl-native-cpp-update.ppm":workload==4?"sdmc:/c3dgl-native-cpp-indexed.ppm":workload==3?"sdmc:/c3dgl-native-cpp-state.ppm":workload==2?"sdmc:/c3dgl-native-cpp-viewport.ppm":workload==1?"sdmc:/c3dgl-native-cpp-matrix.ppm":"sdmc:/c3dgl-native-cpp.ppm","wb");
#else
    FILE *image=fopen(workload==8?"sdmc:/c3dgl-native-depth.ppm":workload==7?"sdmc:/c3dgl-native-raster.ppm":workload==6?"sdmc:/c3dgl-native-cube.ppm":workload==5?"sdmc:/c3dgl-native-update.ppm":workload==4?"sdmc:/c3dgl-native-indexed.ppm":workload==3?"sdmc:/c3dgl-native-state.ppm":workload==2?"sdmc:/c3dgl-native-viewport.ppm":workload==1?"sdmc:/c3dgl-native-matrix.ppm":"sdmc:/c3dgl-native.ppm","wb");
#endif
    if (!image) return;
    fprintf(image,"P6\n400 240\n255\n");
    for (int y=239;y>=0;y--) for (int x=0;x<400;x++) {
        const uint8_t *p=pixels+(x*240+y)*3;
        uint8_t rgb[]={p[2],p[1],p[0]}; fwrite(rgb,1,3,image);
    }
    fclose(image);
}

/* Compare complete GPU readbacks against the already verified native draw path. */
static uint8_t array_expected[sizeof(pixels)];
static void array_compare(const char *name) {
    check(c3dglNativePresent(),name);
    check(c3dglNativeReadback(pixels,sizeof(pixels)),"array readback");
    if (memcmp(pixels,array_expected,sizeof(pixels))) {
        printf("FAIL array image %s\n",name); failures++;
    } else printf("PASS array image %s\n",name);
    if (glGetError()!=GL_NO_ERROR) {printf("FAIL array GL error %s\n",name);failures++;}
}
static void array_scene(void) {
    glLoadIdentity(); glViewport(0,0,400,240);
    check(c3dglNativeUpload(vertices,3),"array baseline upload");
    frame(0x183050ff,1,false);
    check(c3dglNativeReadback(array_expected,sizeof(array_expected)),"array baseline readback");
    glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(4,GL_FLOAT,sizeof(vertices[0]),vertices);
    glColorPointer(4,GL_FLOAT,sizeof(vertices[0]),vertices[0].color);
    check(c3dglNativeBegin(0x183050ff,false),"client arrays begin");
    glDrawArrays(GL_TRIANGLES,0,3); array_compare("client arrays");
    const uint8_t indices8[]={2,0,1};
    const uint16_t indices16[]={2,0,1};
    check(c3dglNativeBegin(0x183050ff,false),"client indices begin");
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_BYTE,indices8); array_compare("client u8 indices");
    check(c3dglNativeBegin(0x183050ff,false),"client indices16 begin");
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,indices16); array_compare("client u16 indices");
    GLuint buffers[2]; glGenBuffers(2,buffers);
    glBindBuffer(GL_ARRAY_BUFFER,buffers[0]);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STATIC_DRAW);
    glVertexPointer(4,GL_FLOAT,sizeof(vertices[0]),NULL);
    glColorPointer(4,GL_FLOAT,sizeof(vertices[0]),(const void *)16);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(indices16),indices16,GL_STATIC_DRAW);
    for (unsigned i=0;i<3;i++) {
        check(c3dglNativeBegin(0x183050ff,false),"cached elements begin");
        glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL); array_compare("cached VBO elements");
    }
    C3DGLnativeVertex both[6];
    memcpy(both,vertices,sizeof(vertices)); memcpy(both+3,vertices,sizeof(vertices));
    for(unsigned i=3;i<6;i++) {both[i].color[0]=1;both[i].color[1]=1;both[i].color[2]=0;}
    check(c3dglNativeUpload(both,6),"update baseline upload");
    check(c3dglNativeBegin(0x183050ff,false),"update baseline begin");
    glViewport(0,0,200,240);check(c3dglNativeDraw(0,3),"baseline left");
    glViewport(200,0,200,240);check(c3dglNativeDraw(3,3),"baseline right");
    check(c3dglNativePresent(),"update baseline present");
    check(c3dglNativeReadback(array_expected,sizeof(array_expected)),"update baseline readback");
    check(c3dglNativeBegin(0x183050ff,false),"subdata begin");
    glViewport(0,0,200,240);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL);
    for(unsigned i=0;i<3;i++) glBufferSubData(GL_ARRAY_BUFFER,i*sizeof(vertices[0])+16,16,both[i+3].color);
    glViewport(200,0,200,240);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL);
    array_compare("in-frame subdata preserves previous draw");
    /* Delete while the queued draw still references its geometry. */
    check(c3dglNativeBegin(0x183050ff,false),"delete begin");
    glViewport(0,0,200,240);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_DYNAMIC_DRAW);
    glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL);
    glBufferData(GL_ARRAY_BUFFER,sizeof(vertices),both+3,GL_DYNAMIC_DRAW);
    glViewport(200,0,200,240);glDrawElements(GL_TRIANGLES,3,GL_UNSIGNED_SHORT,NULL);
    glDeleteBuffers(2,buffers);array_compare("in-frame data replacement and deletion");
#ifdef __cplusplus
    FILE *image=fopen("sdmc:/c3dgl-native-cpp-arrays.ppm","wb");
#else
    FILE *image=fopen("sdmc:/c3dgl-native-arrays.ppm","wb");
#endif
    if (image) {
        fprintf(image,"P6\n400 240\n255\n");
        for(int y=239;y>=0;y--) for(int x=0;x<400;x++) {
            const uint8_t *p=pixels+(x*240+y)*3;const uint8_t rgb[]={p[2],p[1],p[0]};fwrite(rgb,1,3,image);
        } fclose(image);
    } else {printf("FAIL array image export\n");failures++;}
    glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
    glViewport(0,0,400,240);check(c3dglNativeUpload(vertices,3),"restore baseline geometry");
}

static void depth_scene(void) {
    scene_defaults();GLuint buffers[2];scene_setup(depth_vertices,sizeof(depth_vertices),depth_indices,sizeof(depth_indices),buffers);
    glClearColor(24.f/255,48.f/255,80.f/255,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    for(unsigned cell=0;cell<20;cell++) depth_cell(cell);
    check(c3dglNativePresent(),"depth present");check(c3dglNativeReadback(pixels,sizeof(pixels)),"depth readback");
    for(unsigned cell=0;cell<20;cell++) pixel((cell%5)*80+40,(cell/5)*60+30,depth_expected[cell][0],depth_expected[cell][1],depth_expected[cell][2],0);
    save_image(8);if(glGetError()!=GL_NO_ERROR) {printf("FAIL depth GL error\n");failures++;}
    if(!failures) printf("PASS 20 depth/cull cells\n");
    glDeleteBuffers(2,buffers);scene_defaults();glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
}

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM,NULL);
    #ifdef __cplusplus
    results=fopen("sdmc:/c3dgl-native-cpp-results.txt","w");
#else
    results=fopen("sdmc:/c3dgl-native-results.txt","w");
#endif
    printf("Rust native / no Citro3D\nGPU acceptance + performance\n");

    /* libctru lazily creates its VRAM pool on the first allocation. Prime the
     * platform pool before recording free space, outside renderer lifetime. */
    void *prime = vramAlloc(128);
    if (!prime) { printf("FAIL VRAM pool init\n"); failures++; }
    vramFree(prime);
    size_t linear_free = linearSpaceFree(), vram_free = vramSpaceFree();
    for (unsigned cycle = 0; cycle < 8 && !failures; cycle++) {
        if (!c3dglInit()) {
            printf("FAIL init\n");
            failures++;
            break;
        }

        check(c3dglNativeUpload(vertices,3), "upload");
        /* Validate API errors before submitting any GPU work. */
        if (c3dglNativeDraw(0, 3) != -2 || c3dglNativeUpload(vertices, 4097) != -2) failures++;

        for (unsigned i = 0; i < 30 && !failures && aptMainLoop(); i++) {
            unsigned clear = (i & 1) ? 0x183050ff : 0x704020ff;
            if (!frame(clear, i%3 ? 1 : 0, true)) break;

            validate(clear, i%3 != 0);
            checked_frames++;
        }

        c3dglClose();
        c3dglClose(); // Idempotent shutdown.
        if (linearSpaceFree()!=linear_free || vramSpaceFree()!=vram_free) {
            printf("FAIL GPU allocation leak\n"); failures++;
        }

        printf("cycle %u/8 errors=%d\n",cycle+1,failures);
    }

    if (checked_frames != 240) { printf("FAIL incomplete frame test: %u/240\n",checked_frames); failures++; }
    if (!failures && !c3dglInit()) { printf("FAIL benchmark init\n"); failures++; }
    if (!failures) {
        check(c3dglNativeUpload(benchmark_vertices, 3), "benchmark upload");
        printf("PASS 240 frames, 8 init/close\n");

        const unsigned loads[] = {1, 64, 512};
        for (unsigned load = 0; load < 21 && !failures; load++) {
            unsigned draws = loads[load%3];
            matrix_workload=load>=3 && load<6;
            viewport_workload=load>=6 && load<9;
            indexed_workload=load>=9;update_workload=load>=12 && load<15;
            cube_workload=load>=15;raster_workload=load>=18;
            if (load==15) {glDeleteBuffers(2,workload_buffers);cube_setup(workload_buffers);}
            if (load==9) indexed_setup();
            gl_frame=true;
            glViewport(0,0,400,240);
            glClearColor(24.f/255,48.f/255,80.f/255,1);
            glLoadIdentity();
            if(!cube_workload) glScalef(.01f,.01f,.01f);

            u64 cpu = 0, gpu = 0, total = 0;
            unsigned samples=0;

            for (unsigned i = 0; i < 150 && aptMainLoop(); i++) {
                matrix_phase=i;
                u64 start = svcGetSystemTick();

                if (!frame(0x183050ff, draws, true)) break;

                C3DGLnativeStats stats;
                if (!check(c3dglNativeGetStats(&stats), "stats")) break;

                if (i>=30) { samples++;
                    cpu += stats.cpu_ticks;
                    gpu += stats.gpu_ticks;
                    total += svcGetSystemTick() - start;
                }
            }
            if (samples!=120) { printf("FAIL incomplete benchmark: %u/120\n",samples); failures++; break; }

            printf(
                "workload=%s draws=%u CPU %.4f GPU %.4f\n",
                raster_workload?"raster":cube_workload?"cube":update_workload?"update":indexed_workload?"indexed":viewport_workload?"viewport":matrix_workload?"matrix":"static", draws,
                (double)cpu * 1000 / SYSCLOCK_ARM11 / 120,
                (double)gpu * 1000 / SYSCLOCK_ARM11 / 120
            );

            printf(
                "total %.4f ms/frame\n",
                (double)total * 1000 / SYSCLOCK_ARM11 / 120
            );
            if (load%3==2) {
                check(c3dglNativeReadback(pixels,sizeof(pixels)),"workload readback");
                save_image(load>=9?load/3+1:load/3);
            }
        }

        if (glGetError()!=GL_NO_ERROR) { printf("FAIL matrix GL error\n"); failures++; }

        glDeleteBuffers(2,workload_buffers);
        glDisableClientState(GL_VERTEX_ARRAY);glDisableClientState(GL_COLOR_ARRAY);
        indexed_workload=false;update_workload=false;cube_workload=false;raster_workload=false;
        scene_defaults();
        check(c3dglNativeUpload(vertices,3),"restore native geometry");
        matrix_workload=false;
        viewport_workload=false;
        glViewport(0,0,400,240);
        glLoadIdentity();
        glClearColor(.75f,.25f,.125f,1);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        check(c3dglNativeDraw(0,3),"state draw before clear");
        glClearColor(24.f/255,48.f/255,80.f/255,1);
        glClear(GL_COLOR_BUFFER_BIT);
        glViewport(0,0,0,0);
        check(c3dglNativeDraw(0,3),"zero viewport draw");
        glViewport(0,0,400,240);
        check(c3dglNativeDraw(0,3),"state full draw");
        glClear(GL_DEPTH_BUFFER_BIT);
        glViewport(0,0,200,120);
        check(c3dglNativeDraw(0,3),"state subviewport draw");
        check(c3dglNativePresent(),"state present");
        check(c3dglNativeReadback(pixels,sizeof(pixels)),"state readback");
        save_image(3);
        glViewport(0,0,400,240);
        if (glGetError()!=GL_NO_ERROR) {printf("FAIL frame GL error\n");failures++;}
        array_scene();
        depth_scene();
        /* Leave the verified triangle displayed after benchmarks. */
        frame(0x183050ff, 1, true);
        validate(0x183050ff, true);

        c3dglClose();
        if (linearSpaceFree()!=linear_free || vramSpaceFree()!=vram_free) {
            printf("FAIL VBO GPU allocation leak\n");failures++;
        } else printf("PASS VBO GPU memory recovery\n");
    }

    printf("%s - START exits\n", failures ? "FAILED" : "ALL CHECKS PASSED");

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown()&KEY_START) break;
        gspWaitForVBlank();
    }

    if (results) fclose(results);
    gfxExit();
    return failures ? 1 : 0;
}
