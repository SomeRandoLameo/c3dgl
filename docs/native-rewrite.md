# Native rewrite contract

Reference: `main` commit `9c77e37464d140ae0f1b8470876c24ce55dc7bbc`.
`reference-c3dgl.h` and `reference-gl.h` preserve its public declarations;
`native-reference.json` records call-site dependencies, including macros. This
is a source inventory, not a claim that every listed function must survive.

## Responsibility boundary

The Rust renderer owns command encoding, PICA state, built-in shader upload,
render targets and the frame lifecycle. Shader parsing and general draw batching
will expand with the GL port. The platform adapter owns CPU allocation, linear/VRAM allocation, physical address
translation, cache maintenance, GX/GSP submission and completion waits,
display framebuffer access, swap/VBlank and timing.

Citro3D currently supplies frame queues, render buffers, vertex attributes and
buffers, shader/uniform binding, viewport/scissor, raster/depth/blend/stencil
state, TEV, textures and fog. Each must be replaced explicitly. libctru shader
helpers currently generate commands too: keeping them would conceal a renderer
responsibility inside the platform adapter. The intended GPU core therefore
encodes shader commands itself.

libctru is the retained platform backend (user decision, 2026-10-13).
The application continues to own `gfxInit*`,
`aptMainLoop`, input and `gfxExit`. The renderer must exclusively own GPU work
while initialized. No concurrent Citro3D renderer or external GX queue is
supported. Shutdown waits for all renderer-owned GPU work before releasing
memory; it never shuts down application-owned gfx/GSP services.

Keep libctru for CPU/linear/VRAM allocation, physical address translation,
cache maintenance, GSP/GX submission, APT hooks and display/VBlank operations.
The port replaces Citro3D renderer responsibilities, not libctru services.
An independent system/service backend is outside the roadmap.

## Acceptance gate

The user authorized Azahar as the current functional acceptance environment on
2026-10-12. The emulator gate requires colored GPU rendering, full framebuffer
identity with the C reference, repeated frames, C/C++ and rust3ds consumers, allocation
rollback, GPU memory recovery and a repeatable CPU performance advantage for
each tested workload. Results live in `native-results/`.

The separate hardware gate remains: a real 3DS must render a colored GPU
triangle through init → clear → draw → display transfer → swap → close without
Citro3D. Record device/system version, toolchain, archive undefined-symbol audit,
frame count and init/close repetitions. Check C, C++ and rust3ds consumers. Verify
orientation, color channels, absence of stale frames/hangs, and resource recovery
on repeated initialization and allocation failure. Host command tests cannot
establish any of those hardware properties.

After the verified renderer foundation, port matrices/immediate/arrays/indices, then depth/culling/
blending/textures and remaining compatibility groups against the frozen C API
and examples. Unsupported calls must report errors rather than silently
succeeding. Do not declare full header compatibility before symbols and behavior
are implemented.

## Performance requirement

The Rust implementation must reduce CPU work against the C/Citro3D renderer for
matched rendered output, geometry residency, workload and synchronization.
Measure 1, 64 and 512 triangle draws after warmup in repeated runs. Exclude GPU
and VBlank waiting from CPU-work time; report total frame latency separately.
Both implementations synchronize to the same display cadence. The C benchmark
uses the existing persistent indexed VBO cache, not a deliberately slow client
array path. Every measured frame must finish, with no dropped geometry or errors.
The slowest native CPU measurement must beat the fastest reference measurement
at every tested workload. Preserve this as a regression gate as the GL port grows.

Azahar does not model physical GPU execution duration: its GPU timings in this
test are effectively constant. CPU improvements in the guest are emulator
results, not proof of physical ARM11 or PICA throughput. On hardware, require
repeated workload comparisons and no material GPU/total latency regression too.

## Incremental GL checkpoints

1. Matrix pipeline: modelview/projection stacks and native shader MVP transform.
   Compare the unchanged persistent-triangle workload and a matrix change before
   every draw at 1/64/512 draws. Retain prior native CPU times alongside the C
   comparison; report increases explicitly. See `native-results/matrix-step/`.
2. GL frame state: clear, viewport and queries, with matched state-change workloads.
3. Arrays, indices and VBOs: compare persistent indexed meshes and buffer updates.
   The complete Azahar gate is recorded in `native-results/arrays-step/full/`: three
   runs each of C, C++, rust3ds and C reference, matched full GPU images, resource
   recovery and CPU comparisons for static/matrix/viewport/indexed/update workloads.
4. Depth/culling and a rotating colored cube through the implemented GL subset.
5. Immediate mode, then textures/blending and later compatibility groups.

Each checkpoint keeps the emulator image, resource recovery and CPU gates; the
next group is added only after the current comparison has passed. This sequence
does not declare full GL header compatibility.

The first Rust application compatibility gate is recorded in
`native-results/rust3ds-step/`: a real ctru-rs/cargo-3ds Cargo consumer with
application-owned std runtime, exact GPU image comparisons and independent
CPU gates. Keep that consumer in all subsequent renderer checkpoints. The
C/C++ static library retains its standalone no_std runtime configuration.

The GL clear/viewport checkpoint is recorded in `native-results/frame-step/`.
It includes the real rust3ds consumer in every GPU/performance comparison and
reports incremental CPU costs for both consumers. The arrays/indices/VBO checkpoint is now verified in
`native-results/arrays-step/full/`. Its report also records increased CPU costs
relative to the frame-state checkpoint, while all C-reference performance gates
pass. Depth/culling and the rotating colored cube are implemented. Build and host checks pass; emulator evidence is retained in `native-results/depth-step/`. Immediate mode is the next implementation group.
