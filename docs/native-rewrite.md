# Native rewrite contract

Reference: `main` commit `9c77e37464d140ae0f1b8470876c24ce55dc7bbc`.
`reference-c3dgl.h` and `reference-gl.h` preserve its public declarations;
`native-reference.json` records call-site dependencies, including macros. This
is a source inventory, not a claim that every listed function must survive.

## Responsibility boundary

The Rust renderer will own command encoding, PICA state, shader binary parsing
and upload, render targets, draw batching and the frame lifecycle. The platform
adapter will own CPU allocation, linear/VRAM allocation, physical address
translation, cache maintenance, GX/GSP submission and completion waits,
display framebuffer access, swap/VBlank and timing.

Citro3D currently supplies frame queues, render buffers, vertex attributes and
buffers, shader/uniform binding, viewport/scissor, raster/depth/blend/stencil
state, TEV, textures and fog. Each must be replaced explicitly. libctru shader
helpers currently generate commands too: keeping them would conceal a renderer
responsibility inside the platform adapter. The intended GPU core therefore
encodes shader commands itself.

For the first libctru backend the application continues to own `gfxInit*`,
`aptMainLoop`, input and `gfxExit`. The renderer must exclusively own GPU work
while initialized. No concurrent Citro3D renderer or external GX queue is
supported. Shutdown waits for all renderer-owned GPU work before releasing
memory; it never shuts down application-owned gfx/GSP services.

The later service backend must negotiate display/GSP ownership explicitly.
Static linking does not remove libctru calls or make independent GPU owners
safe. Keep that backend separate from the initial libctru adapter.

## Acceptance gate

The first milestone is accepted only after a real 3DS renders a colored GPU
triangle through init → clear → draw → display transfer → swap → close without
Citro3D. Record device/system version, toolchain, archive undefined-symbol audit,
frame count and init/close repetitions. Check both C and C++ consumers. Verify
orientation, color channels, absence of stale frames/hangs, and resource recovery
on repeated initialization and allocation failure. Host command tests cannot
establish any of those hardware properties.

After this gate, port matrices/immediate/arrays/indices, then depth/culling/
blending/textures and remaining compatibility groups against the frozen C API
and examples. Unsupported calls must report errors rather than silently
succeeding. Do not declare full header compatibility before symbols and behavior
are implemented.
