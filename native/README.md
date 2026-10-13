# Native Rust triangle renderer

`no_std` + `alloc` Rust static library with a C ABI. The renderer generates its
own PICA200 commands, uploads its own vertex shader and renders into VRAM.
Citro3D is not linked. All allocation, GSP/GX, cache, display, event, APT and timing
calls are confined to `platform/libctru.c` and its Rust declarations.

The current scope is top-screen colored triangles with persistent vertex data,
RGBA8/D24S8 targets, GPU clear, asynchronous command submission, display transfer,
VBlank presentation, readback and shutdown. The GL subset adds modelview/projection matrix
stacks, GPU transformation, clear state, viewport and implemented state queries
(see `include/GL/gl.h` for the implemented subset).
This is **not yet the full OpenGL
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

This runs Rust packet/lifecycle tests, C and C++ host link tests, C/C++ 3DS
consumer builds, a real ctru-rs Rust application build and executable symbol
audits. Install cargo-3ds and fetch the pinned Rust consumer dependencies first;
see `rust3ds/README.md`. Native CMake is configured with
`$DEVKITPRO/cmake/3DS.cmake`. Cargo rebuilds `core` and `alloc` for
`armv6k-nintendo-3ds`, with abort semantics and hard-float ABI. The Rust archive is
`build-native/cargo/armv6k-nintendo-3ds/release/libc3dgl.a`.

Run `build-native/c3dgl_native_triangle.3dsx` in Azahar. It automatically checks
240 frames with alternating clears, colored pixels, API errors, eight init/close
cycles and restoration of free linear/VRAM space. It then measures 1, 64 and 512
draws per frame with static matrices and with a new modelview matrix before each
draw, plus a viewport change before each draw, each with 30 warmups and 120 measured frames. Even the one-draw case changes
its matrix between frames. START exits.
The C++ executable performs the identical checks.

For repeatable emulator acceptance, with the emulator's actual SDMC directory:

```
python3 native/scripts/emulator_check.py \
  --emulator /Applications/Azahar.app/Contents/MacOS/azahar \
  --sdmc '/path/to/Azahar/sdmc' \
  --output build-native/emulator-results --runs 3
```

The runner requires all checks to pass, identical full GPU images for both workloads versus the C
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
/* vertices: float4 object-space positions and float4 RGBA */
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

Include `<GL/gl.h>` from the native target to use `glMatrixMode`,
`glLoadIdentity`, `glLoadMatrixf`, `glMultMatrixf`, `glPushMatrix`, `glPopMatrix`,
`glTranslatef`, `glRotatef`, `glScalef`, `glOrtho`, `glFrustum`, `glGetError`,
`glClearColor`, `glClearDepth`/`glClearDepthf`, `glClear`, `glViewport`,
`glGetFloatv` and `glGetIntegerv`.
Modelview has 32 stack entries, projection has 2. GL_TEXTURE, double/fixed-point
entry points and remaining state queries are not implemented yet. The native
header deliberately declares only implemented symbols. Presentation uses
`c3dglSwapBuffers` or the native API. `glClear` starts a frame automatically; only color and
depth bits are supported. Clears cover the whole target regardless of viewport.

Vertices now stay in object space. The shader performs projection × modelview,
screen tilt and depth conversion. Matrix uniforms upload only when changed or
when GPU state is restored; steady frames allocate nothing. The command buffer
is 128 KiB to fit 512 draws with a matrix change before every draw.

The next steps are the remaining OpenGL groups in `docs/native-rewrite.md`.
libctru remains the platform backend; the port replaces Citro3D. The ordinary `alloc` allocator is
separate from GPU-visible linear/VRAM storage. The default C/C++ 3DS archive
remains no_std; host tests and the opt-in rust3ds consumer use Rust std.

## Rust applications

Use `c3dgl-native = { path = "path/to/c3dgl/native", features = ["rust3ds"] }`
as a Cargo dependency next to `ctru-rs`. This selects the application-owned Rust
std allocator/panic runtime and automatically builds/links the libctru adapter.
The default C/C++ static-library configuration remains no_std. See
[`rust3ds/README.md`](rust3ds/README.md) for the tested consumer and ownership rules.

`glViewport` uses landscape coordinates, clamps size to the reported maximum
400×240, and skips zero-size/fully offscreen draws. Viewport commands upload only
when changed or GPU state is restored. Clear state and viewport queries, matrix
mode/current matrices/stack depths and their implemented limits are available.
The extra clear-order scene verifies color-only/depth-only clears, clear after a
draw, a zero-size viewport and a subviewport against the C renderer. Use
`emulator_check.py --frame-state --rust3ds-rom path/to/consumer.3dsx` for this gate.

## Vertex arrays and buffer objects

`glVertexPointer`, `glColorPointer`, client-state enable/disable, `glDrawArrays`
and `glDrawElements` support `GL_TRIANGLES`. Vertex formats are SHORT and FLOAT
with 2–4 components; colors support BYTE, UNSIGNED_BYTE, SHORT, UNSIGNED_SHORT
and FLOAT with 3–4 components. Integer colors are normalized. Disabled color
arrays supply white. Stride zero means tightly packed. DrawElements accepts
UNSIGNED_BYTE or UNSIGNED_SHORT and emits a real PICA indexed draw. Incomplete
trailing triangles are ignored. The converted vertex range is limited to 4096.

`glGenBuffers`, `glBindBuffer`, `glDeleteBuffers`, `glBufferData` and
`glBufferSubData` implement ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER. Pointer calls
capture the currently bound array buffer; with a buffer bound, pointers are byte
offsets. Index offsets use the element buffer bound at draw time. Data allocation
accepts null data (initialized to zero); subdata bounds are checked before mutation.

Client memory is copied on every draw. Fully buffer-backed draw descriptions
cache converted float4 vertices and u16 indices in GPU-visible linear memory;
unchanged draws reuse that storage across frames without geometry uploads.
Updates invalidate only meshes referencing the changed buffer. Up to 64 cached
meshes are retained; evicted, updated, deleted and client-array storage remains
alive until the next frame has waited for GPU completion. Changes within a frame
therefore preserve the geometry seen by previous queued draws. Matrix and viewport
changes do not invalidate geometry. Native and GL draws explicitly select their
geometry base, so they can be mixed.

Host lifecycle tests cover client and indexed draws, cross-frame cache reuse,
unrelated-buffer updates, rejected subdata ranges, in-frame invalidation and
shutdown with retained geometry. Packet tests check the indexed draw command.

Run the array acceptance scene in Azahar for both C and C++ consumers:

```
python3 native/scripts/emulator_check.py --arrays-only \
  --emulator /path/to/azahar --sdmc /path/to/actual/sdmc \
  --output docs/native-results/arrays-step --runs 1
```

The scene compares every GPU-readback byte against native draw baselines for
client arrays, u8/u16 indices, cached VBO draws over three frames, SubData updates
between draws, data replacement and deletion before submission. The runner
requires fresh images, all eight scene checks and identical C/C++ final images;
this mode does not apply a performance comparison against the C renderer.

For the complete arrays checkpoint, include the Rust application and the C
reference renderer, with three runs of every consumer:

```
python3 native/scripts/emulator_check.py --vbo-state \
  --emulator /path/to/azahar --sdmc /path/to/actual/sdmc \
  --rust3ds-rom build-native-rust3ds/armv6k-nintendo-3ds/release/c3dgl-rust3ds-triangle.3dsx \
  --baseline-summary docs/native-results/frame-step/azahar-summary.json \
  --output docs/native-results/arrays-step/full --runs 3
```

`--vbo-state` requires rust3ds, retains the static/matrix/viewport regression
workloads, and adds persistent indexed meshes plus a 6-byte vertex and 6-byte
index update in each frame. All consumers use the same compact short3/pad/short2/ubyte4 vertex
layout and u16 indices for these workloads. Each workload uses 30 warmup and 120
measured frames at 1/64/512 draws. The gate compares the slowest native C, C++ and
Rust result separately with the fastest C-reference result. It compares all
seven GPU images across runs and consumers, verifies that updates change pixels,
and records CPU increases relative to the previous checkpoint. C/C++ and rust3ds
also check recovery of linear/VRAM memory after the VBO scenes.


## Depth and culling

`glEnable`/`glDisable` and `glIsEnabled` implement DEPTH_TEST and CULL_FACE.
DepthFunc supports all eight comparisons, with reversed PICA depth comparisons;
DepthMask controls draw writes and GL depth clears. DepthRange/DepthRangef clamp
both endpoints independently and support reversed and constant ranges. CullFace
supports FRONT, BACK and FRONT_AND_BACK, with FrontFace CW/CCW. Both-face culling
suppresses draws. Float/integer/boolean state queries expose these settings.
Unsupported capabilities and invalid enumerants report GL errors. State packets
are emitted only when changed, or after GPU restoration; geometry stays cached
when depth/cull state changes.

`emulator_check.py --depth-state --rust3ds-rom ...` includes all previous
regression workloads, a persistent indexed rotating cube and depth/range/winding
changes before every cube draw. A 20-cell pixel scene covers comparisons, masked
writes and clears, disabled depth writes, reversed/constant ranges and culling.
The reference benchmark directly includes the unchanged C implementation, as
the repository's regression tests do, to inspect its private GPU-cache pointer.
Every workload must obtain a compact cache, and unchanged frames must retain it.
The compact SHORT input is necessary: C FLOAT arrays do not use this cache.
The C generic primitive path supplies the one FRONT_AND_BACK correctness cell,
because its indexed path fails to reject both faces; measured draws all remain
indexed. Run three repetitions for C, C++, rust3ds and the C reference.
