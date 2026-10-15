/* Mock platform for the host bench: GPU-visible memory is plain memory, GPU work completes at once and
 * framebuffer reads return black. Only CPU work of the renderer is meaningful. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach_time.h>
#include <stdio.h>

/* Calls that are system calls on the 3DS (costly in Azahar too): counted and printed at exit. */
enum { LINEAR_ALLOC, VRAM_ALLOC, FLUSH, SUBMIT, WAIT, READ_COLOR, WRITE_LINES, CLEAR, TICK, COUNT };
static const char *names[COUNT] = {"linear alloc", "vram alloc", "flush", "submit", "wait", "read color",
                                   "write lines", "clear", "tick"};
static unsigned long calls[COUNT];
static void report(void) {
    if (!getenv("C3DGL_BENCH_CALLS")) return;
    for (int i = 0; i < COUNT; i++) fprintf(stderr, "%-12s %lu\n", names[i], calls[i]);
}
__attribute__((constructor)) static void install(void) { atexit(report); }

static void *allocate(size_t size, size_t alignment) {
    void *p = NULL;
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (posix_memalign(&p, alignment, size ? size : 1)) return NULL;
    return p;
}
void *c3dglPlatformAllocate(size_t size, size_t alignment) { return allocate(size, alignment); }
void c3dglPlatformFree(void *p) { free(p); }
uint8_t *c3dglPlatformLinearAllocate(size_t size) { calls[LINEAR_ALLOC]++; return allocate(size, 128); }
void c3dglPlatformLinearFree(uint8_t *p) { free(p); }
uint8_t *c3dglPlatformVramAllocate(size_t size) { calls[VRAM_ALLOC]++; return allocate(size, 128); }
void c3dglPlatformVramFree(uint8_t *p) { free(p); }
uint32_t c3dglPlatformPhysical(const uint8_t *p) { return 0x18000000u + ((uint32_t)(uintptr_t)p & 0x00ffff80u); }
uint64_t c3dglPlatformTick(void) {
    calls[TICK]++;
    static mach_timebase_info_data_t base;
    if (!base.denom) mach_timebase_info(&base);
    return (uint64_t)((__int128)mach_absolute_time() * base.numer / base.denom * 268111856 / 1000000000);
}
int32_t c3dglPlatformFlush(const uint8_t *p, uint32_t n) { (void)p; (void)n; calls[FLUSH]++; return 0; }
int32_t c3dglPlatformAcquire(void) { return 0; }
void c3dglPlatformRelease(void) {}
uint64_t c3dglPlatformWait(void) { calls[WAIT]++; return 0; }
int32_t c3dglPlatformReadback(uint8_t *p, uint32_t n) { memset(p, 0, n); return 0; }
int32_t c3dglPlatformReadColor(uint8_t *c, uint32_t line0, uint32_t lines, uint8_t *out) {
    (void)c; (void)line0; calls[READ_COLOR]++; memset(out, 0, (size_t)lines * 960); return 0;
}
int32_t c3dglPlatformWriteLines(uint8_t *t, uint32_t line0, uint32_t lines, uint8_t *in) {
    (void)t; (void)line0; (void)lines; (void)in; calls[WRITE_LINES]++; return 0;
}
uint32_t c3dglPlatformGeneration(void) { return 0; }
void c3dglPlatformVblank(void) {}
int32_t c3dglPlatformClear(uint8_t *c, uint8_t *d, uint32_t v, uint32_t n) { (void)c; (void)d; (void)v; (void)n; calls[CLEAR]++; return 0; }
int32_t c3dglPlatformClearMasked(uint8_t *c, uint8_t *d, uint32_t v, uint32_t dv, uint32_t n, uint32_t m) {
    (void)c; (void)d; (void)v; (void)dv; (void)n; (void)m; calls[CLEAR]++; return 0;
}
int32_t c3dglPlatformSplitCopy(uint8_t *c, uint32_t n, uint8_t *src, uint8_t *dst, uint32_t row, uint32_t gap, uint32_t size) {
    (void)c; (void)n; (void)src; (void)dst; (void)row; (void)gap; (void)size; calls[SUBMIT]++; return 0;
}
int32_t c3dglPlatformSubmit(uint8_t *c, uint32_t n, uint8_t *color) { (void)c; (void)n; (void)color; calls[SUBMIT]++; return 0; }
