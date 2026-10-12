#ifndef C3DGL_NATIVE_H
#define C3DGL_NATIVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All renderer entry points must be called on one application thread.
 * Application owns gfxInitDefault()/gfxExit(); top screen must be RGB8, 400x240.
 * Renderer has exclusive ownership of GPU submission between Init and Close.
 * Return codes: 0 success; -1 unavailable; -2 invalid call; -3 platform failure;
 * -4 command overflow (previous valid draws still present). */
typedef struct {
    float position[4]; /* OpenGL clip space x,y,z,w; library applies screen tilt. */
    float color[4];    /* RGBA, 0..1 */
} C3DGLnativeVertex;
typedef struct {
    uint64_t cpu_ticks, gpu_ticks, wait_ticks;
    uint32_t command_bytes, draws;
} C3DGLnativeStats;
bool c3dglInit(void);
void c3dglClose(void);
void c3dglSwapBuffers(void);
int c3dglNativeUpload(const C3DGLnativeVertex *vertices, size_t count);
int c3dglNativeBegin(uint32_t clear_rgba, bool sync_vblank);
int c3dglNativeDraw(size_t first, size_t count);
int c3dglNativePresent(void);
int c3dglNativeGetStats(C3DGLnativeStats *stats);
/* Copies displayed pixels after GPU completion: BGR8, column major, y inverted.
 * Requires at least 400*240*3 bytes. For validation, outside timed frames. */
int c3dglNativeReadback(uint8_t *pixels, size_t capacity);

/* Low-level encoder, independent of renderer initialization.
 * Storage must be aligned, valid and non-overlapping. Returns words written,
 * or zero without changing output on invalid encoding/capacity. */
size_t c3dglNativeEncodeCommand(uint32_t *output, size_t capacity,
    uint16_t reg, uint8_t mask, bool incremental,
    const uint32_t *values, size_t count);
#ifdef __cplusplus
}
#endif
#endif
