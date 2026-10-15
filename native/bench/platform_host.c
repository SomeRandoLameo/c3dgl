/* Mock platform for the host bench: GPU-visible memory is plain memory, GPU work completes at once and
 * framebuffer reads return black. Only CPU work of the renderer is meaningful. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>

static void *allocate(size_t size, size_t alignment) {
    void *p = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (posix_memalign(&p, alignment, size ? size : 1)) return NULL;
    return p;
}
void *c3dglPlatformAllocate(size_t size, size_t alignment) { return allocate(size, alignment); }
void c3dglPlatformFree(void *p) { free(p); }
uint8_t *c3dglPlatformLinearAllocate(size_t size) { return allocate(size, 128); }
void c3dglPlatformLinearFree(uint8_t *p) { free(p); }
uint8_t *c3dglPlatformVramAllocate(size_t size) { return allocate(size, 128); }
void c3dglPlatformVramFree(uint8_t *p) { free(p); }
uint32_t c3dglPlatformPhysical(const uint8_t *p) { return 0x18000000u + ((uint32_t)(uintptr_t)p & 0x00ffff80u); }
uint64_t c3dglPlatformTick(void) {
    static mach_timebase_info_data_t base;
    if (!base.denom) mach_timebase_info(&base);
    return (uint64_t)((__int128)mach_absolute_time() * base.numer / base.denom * 268111856 / 1000000000);
}
int32_t c3dglPlatformFlush(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }
int32_t c3dglPlatformAcquire(void) { return 0; }
void c3dglPlatformRelease(void) {}
uint64_t c3dglPlatformWait(void) { return 0; }
int32_t c3dglPlatformReadback(uint8_t *p, uint32_t n) { memset(p, 0, n); return 0; }
int32_t c3dglPlatformReadColor(uint8_t *c, uint32_t line0, uint32_t lines, uint8_t *out) {
    (void)c; (void)line0; memset(out, 0, (size_t)lines * 960); return 0;
}
int32_t c3dglPlatformWriteLines(uint8_t *t, uint32_t line0, uint32_t lines, uint8_t *in) {
    (void)t; (void)line0; (void)lines; (void)in; return 0;
}
uint32_t c3dglPlatformGeneration(void) { return 0; }
void c3dglPlatformVblank(void) {}
int32_t c3dglPlatformClear(uint8_t *c, uint8_t *d, uint32_t v, uint32_t n) { (void)c; (void)d; (void)v; (void)n; return 0; }
int32_t c3dglPlatformClearMasked(uint8_t *c, uint8_t *d, uint32_t v, uint32_t dv, uint32_t n, uint32_t m) {
    (void)c; (void)d; (void)v; (void)dv; (void)n; (void)m; return 0;
}
int32_t c3dglPlatformSubmit(uint8_t *c, uint32_t n, uint8_t *color) { (void)c; (void)n; (void)color; return 0; }
