# Native Rust triangle renderer

`no_std` + `alloc` Rust static library with a C ABI. The renderer generates its
own PICA200 commands, uploads its own vertex shader and renders into VRAM.
Citro3D is not linked. All allocation, GSP/GX, cache, display, event, APT and timing
calls are confined to `platform/libctru.c` and its Rust declarations.

The current scope is top-screen colored triangles with persistent vertex data,
RGBA8/D24S8 targets, GPU clear, asynchronous command submission, display transfer,
VBlank presentation, readback and shutdown. This is **not yet the full OpenGL
port**. Existing setup names `c3dglInit`, `c3dglClose`, `c3dglSwapBuffers` are
exported; use `c3dgl_native.h` for the currently implemented drawing API. Do not
link the native and C renderer into the same program.

## Build and verification

Install devkitPro and the pinned Rust toolchain in `rust-toolchain.toml`.
From the repository root:

```
export DEVKITPRO=/opt/devkitpro
native/scripts/check.sh
```

This runs Rust packet/lifecycle tests, actual C and C++ host link tests, both 3DS
consumer builds and an executable symbol audit. Native CMake is configured with
`$DEVKITPRO/cmake/3DS.cmake`. Cargo rebuilds `core` and `alloc` for
`armv6k-nintendo-3ds`, with abort semantics and hard-float ABI. The Rust archive is
`build-native/cargo/armv6k-nintendo-3ds/release/libc3dgl.a`.

Run `build-native/c3dgl_native_triangle.3dsx` in Azahar. It automatically checks
240 frames with alternating clears, colored pixels, API errors, eight init/close
cycles and restoration of free linear/VRAM space. It then measures 1, 64 and 512
draws per frame, each with 30 warmups and 120 measured frames. START exits.
The C++ executable performs the identical checks.

For repeatable emulator acceptance, with the emulator's actual SDMC directory:

```
python3 native/scripts/emulator_check.py \
  --emulator /Applications/Azahar.app/Contents/MacOS/azahar \
  --sdmc '/path/to/Azahar/sdmc' \
  --output build-native/emulator-results --runs 3
```

The runner requires all checks to pass, identical full GPU images versus the C
reference, and the slowest native CPU result to beat the fastest C result for
every tested workload. Guest CPU work excludes explicitly tracked GPU/VBlank
waiting. Total frame time uses identical 60 Hz synchronization. Emulator GPU
and CPU timing cannot establish performance on actual 3DS hardware.

## Embed in an application

```
set(C3DGL_NATIVE_BUILD_ABI_TEST OFF CACHE BOOL "" FORCE)
set(C3DGL_NATIVE_BUILD_TRIANGLE OFF CACHE BOOL "" FORCE)
set(C3DGL_NATIVE_BUILD_CPP_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory(path/to/c3dgl/native native-build)
target_link_libraries(my_app PRIVATE c3dgl::native)
```

C usage after `gfxInitDefault()`:

```
#include <c3dgl_native.h>
if (!c3dglInit()) { /* handle failure */ }
/* vertices: float4 OpenGL clip-space positions and float4 RGBA */
if (c3dglNativeUpload(vertices, vertex_count) != 0) { /* handle failure */ }
while (aptMainLoop()) {
    if (c3dglNativeBegin(0x183050ff, true) != 0) break;
    if (c3dglNativeDraw(0, vertex_count) != 0) break;
    if (c3dglNativePresent() != 0) break;
}
c3dglClose();
/* application eventually calls gfxExit() */
```

All API calls run on one application thread. The application owns graphics/GSP
initialization and shutdown; the renderer exclusively owns GPU submissions while
initialized. Top display format must be RGB8 at 400x240, with stereo disabled.
Mixing another active GX queue or renderer is unsupported.

Vertices upload between frames and remain in linear storage. Steady frames
allocate nothing and upload no vertex data. Initial state and shader commands
are encoded once and replayed after APT restoration; each draw emits only the
required PICA array sequence. Cache maintenance covers uploaded vertex ranges
and the submitted command bytes, not the whole linear heap. Shutdown drains GPU
work before freeing buffers. All five GPU allocations unwind on failure.

The next steps are the actual OpenGL groups in `docs/native-rewrite.md` and the
later independent system/service backend. The ordinary `alloc` allocator is
separate from GPU-visible linear/VRAM storage. Rust std is enabled only for host
tests, never for the 3DS archive.
