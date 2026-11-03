// Frame statistics shown on the console by the examples, from the renderer's own counters.
#pragma once
#include <3ds.h>
#include <c3dgl_native.h>

namespace stats {
inline C3DGLnativeStats get() {
    C3DGLnativeStats s{};
    c3dglNativeGetStats(&s);
    return s;
}
// Milliseconds spent by the CPU / GPU for the last frame.
inline float cpuMs() { return static_cast<float>(static_cast<double>(get().cpu_ticks) * 1000 / SYSCLOCK_ARM11); }
inline float gpuMs() { return static_cast<float>(static_cast<double>(get().gpu_ticks) * 1000 / SYSCLOCK_ARM11); }
// Fraction of the 128 KiB GPU command buffer used by the last frame.
inline float cmdBufUsage() { return static_cast<float>(get().command_bytes) / (32768.f * 4.f); }
}  // namespace stats
