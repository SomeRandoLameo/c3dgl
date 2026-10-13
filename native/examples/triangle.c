/* GPU acceptance test and benchmark. Runs automatically; START exits. */
#include <3ds.h>
#include "c3dgl_native.h"
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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
static unsigned matrix_phase;

static void transform(unsigned draw) {
    const float m[]={.5f,0,0,0, 0,.5f,0,0, 0,0,1,0, (draw&1)? .125f:-.125f,0,0,1};
    glLoadMatrixf(m);
}

static int frame(unsigned clear, unsigned draws, bool sync) {
    if (!check(c3dglNativeBegin(clear,sync), "begin")) return 0;

    for (unsigned j = 0; j < draws; j++) {
        if (matrix_workload) transform(j+matrix_phase);
        if (!check(c3dglNativeDraw(0,3), "draw")) return 0;
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


static void save_image(bool transformed) {
#ifdef __cplusplus
    FILE *image=fopen(transformed?"sdmc:/c3dgl-native-cpp-matrix.ppm":"sdmc:/c3dgl-native-cpp.ppm","wb");
#else
    FILE *image=fopen(transformed?"sdmc:/c3dgl-native-matrix.ppm":"sdmc:/c3dgl-native.ppm","wb");
#endif
    if (!image) return;
    fprintf(image,"P6\n400 240\n255\n");
    for (int y=239;y>=0;y--) for (int x=0;x<400;x++) {
        const uint8_t *p=pixels+(x*240+y)*3;
        uint8_t rgb[]={p[2],p[1],p[0]}; fwrite(rgb,1,3,image);
    }
    fclose(image);
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
        check(c3dglNativeUpload(vertices, 3), "upload");
        printf("PASS 240 frames, 8 init/close\n");

        const unsigned loads[] = {1, 64, 512};
        for (unsigned load = 0; load < 6 && !failures; load++) {
            unsigned draws = loads[load%3];
            matrix_workload=load>=3;
            glLoadIdentity();

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
                matrix_workload?"matrix":"static", draws,
                (double)cpu * 1000 / SYSCLOCK_ARM11 / 120,
                (double)gpu * 1000 / SYSCLOCK_ARM11 / 120
            );

            printf(
                "total %.4f ms/frame\n",
                (double)total * 1000 / SYSCLOCK_ARM11 / 120
            );
        }

        if (glGetError()!=GL_NO_ERROR) { printf("FAIL matrix GL error\n"); failures++; }
        check(c3dglNativeReadback(pixels,sizeof(pixels)),"matrix readback");
        save_image(true);
        matrix_workload=false;
        glLoadIdentity();
        /* Leave the verified triangle displayed after benchmarks. */
        frame(0x183050ff, 1, true);
        validate(0x183050ff, true);
        save_image(false);

        c3dglClose();
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
