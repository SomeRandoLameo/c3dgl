/* Frame statistics shown on the console by the examples, from the renderer's own counters. */
#ifndef C3DGL_EXAMPLE_COMMON_H
#define C3DGL_EXAMPLE_COMMON_H
#include <3ds.h>
#include <c3dgl_native.h>

/* Milliseconds spent by the CPU / GPU for the last frame. */
static inline float statsCpuMs(void) {
    C3DGLnativeStats s = {0};
    c3dglNativeGetStats(&s);
    return (float)((double)s.cpu_ticks * 1000 / SYSCLOCK_ARM11);
}
static inline float statsGpuMs(void) {
    C3DGLnativeStats s = {0};
    c3dglNativeGetStats(&s);
    return (float)((double)s.gpu_ticks * 1000 / SYSCLOCK_ARM11);
}
/* Fraction of the 128 KiB GPU command buffer used by the last frame. */
static inline float statsCmdBufUsage(void) {
    C3DGLnativeStats s = {0};
    c3dglNativeGetStats(&s);
    return (float)s.command_bytes / (32768.0f * 4.0f);
}
#endif
