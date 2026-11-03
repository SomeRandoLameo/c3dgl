// Capture entry point of the api example: every check of the original main (runTests(), extracted unchanged) and the
// frame of its main loop. The original is plain C, so this adapter is too.
#define main c3dglOriginalInteractiveMain
#include "../../examples/api/main.c"
#undef main
#ifdef C3DGL_COMPARE_NATIVE
#include <c3dgl_native.h>
#define BACKEND "native"
#else
#define BACKEND "reference"
#endif
#define CASE "api"
static uint8_t capture[400 * 240 * 3];

int main(void) {
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);
    FILE *report = fopen("sdmc:/c3dgl-old-" CASE "-" BACKEND "-results.txt", "w");
    if (!report) { gfxExit(); return 1; }
    if (!c3dglInit()) { fprintf(report, "FAIL renderer init\n"); fclose(report); gfxExit(); return 1; }
    glClearColor(.12f, .12f, .15f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    checkLog = fopen("sdmc:/c3dgl-old-" CASE "-" BACKEND "-checks.log", "w");
    runTests();
    if (checkLog) {
        fprintf(checkLog, "checks=%d failures=%d\n", checks, failures);
        fclose(checkLog);
        checkLog = NULL;
    }
    // The frame of the original main loop
    if (failures == 0) glClearColor(0.1f, 0.5f, 0.2f, 1.0f);
    else glClearColor(0.7f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    drawSquares();
    drawArraySquares();
    drawBufferSquares();
#ifndef C3DGL_COMPARE_NATIVE
    const uint8_t *display = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, NULL, NULL);
#endif
    c3dglSwapBuffers();
    int readback = 0;
#ifdef C3DGL_COMPARE_NATIVE
    readback = c3dglNativeReadback(capture, sizeof(capture));
#else
    C3D_FrameSync();
    readback = svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, (u32)display, sizeof(capture));
    memcpy(capture, display, sizeof(capture));
#endif
    GLenum error = glGetError();
    FILE *image = fopen("sdmc:/c3dgl-old-" CASE "-" BACKEND ".ppm", "wb");
    if (image) {
        fprintf(image, "P6\n400 240\n255\n");
        for (int y = 239; y >= 0; y--)
            for (int x = 0; x < 400; x++) {
                const uint8_t *p = capture + (x * 240 + y) * 3;
                const uint8_t rgb[] = {p[2], p[1], p[0]};
                fwrite(rgb, 1, 3, image);
            }
        fclose(image);
    }
    c3dglClose();
    if (failures) fprintf(report, "original checks failed: %d of %d\n", failures, checks);
    const bool passed = error == GL_NO_ERROR && readback == 0 && image != NULL && failures == 0;
    fprintf(report, "%s: old example=%s backend=%s GL=%u readback=%d image=%d\n", passed ? "ALL CHECKS PASSED" : "FAILED",
            CASE, BACKEND, error, readback, image != NULL);
    fclose(report);
    gfxExit();
    return passed ? 0 : 1;
}
