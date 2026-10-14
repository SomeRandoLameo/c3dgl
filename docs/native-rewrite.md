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
5. Immediate triangle mode, then level-zero textures/blending and later compatibility groups.

Each checkpoint retains emulator image, resource recovery and CPU gates as separate
acceptance evidence. Development continued after host/build checks at the user's
request to stop lengthy emulator repetitions. Pending emulator gates remain explicit;
compilation and host tests do not establish GPU images or performance. This sequence
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
pass. Depth/culling and the rotating colored cube are implemented. Build and host checks pass; emulator evidence is retained in `native-results/depth-step/`. Immediate triangle mode is now implemented with a dedicated rotating RGB demo. Its 16 host tests and complete C/C++/rust3ds build check pass; emulator validation for this new group is pending.


Level-zero RGB/RGBA textures and RGB/alpha blending are implemented, with UV arrays,
VBOs and immediate coordinates. The new C, C++ and rust3ds texture consumers build
and pass dependency audits; 19 host tests pass. See `native-results/texture-step/`
for scope and build evidence. Mipmaps, additional formats, texture matrices and
emulator image/performance validation for this checkpoint remain pending.

A single native C texture/blend Azahar smoke run is recorded in
`native-results/texture-step/one-run/`: all 18 rendering/image checks pass, GL error
is zero. Its extra memory assertion used an uninitialized VRAM baseline; the
original failed assertion is retained and the corrected harness was rebuilt
without a second emulator launch. This does not complete repeated performance
or independent C++/Rust consumer gates.

Filled primitive compatibility now supports triangle strips/fans, quads, quad
strips and convex polygons across client arrays, indexed VBOs and immediate mode,
plus float/color/UV vector calls and validated u32 index inputs. All 24 host tests
and C/C++/rust3ds builds pass. One native C Azahar run passes 32 checks, including
complete cell comparisons and memory recovery; see `native-results/primitive-step/`.
Independent consumer comparisons and performance gates remain separate.


## Original-example acceptance

Use existing `examples/*/main.*` scene functions and inputs as the primary oracle
for further compatibility work, with a common capture entry point for C and Rust.
Do not replace missing original features with stubs or modified inputs. Extend
shared selection as the port grows and make full original pages runnable. Separate
whole-program support from selected-scene comparisons and keep every mismatch.

`native/compare/` and `native/scripts/compare_old_examples.py` now provide this path.
The first three one-run pairs are in `native-results/old-examples/`: exact F-texture
match, primitive channel differences of at most one, and an NPOT failure in the
original 12x12 cube texture. Address these real-example gaps before claiming full
compatibility; new isolated examples alone do not provide equivalent evidence.


## NPOT texture checkpoint

Level-zero RGB/RGBA images now accept dimensions 1..1024, with power-of-two
GPU storage (minimum 8x8), shader UV scaling and wrap-dependent padding matching
the C renderer. Subimage and wrap updates refresh padding with copy-on-write;
allocation failure preserves the previous texture and parameters. The original
12x12 cube scene now matches the C image exactly. See `native-results/npot-step/`.
The earlier failed capture remains in `native-results/old-examples/`.


## Primitive color parity checkpoint

The original primitive scene now matches the C framebuffer exactly, with zero
GL errors. Rust GL colors use reference byte rounding, byte-valued GPU attributes
and the same float24 GPU normalization constant. CPU quantization alone left one
pixel mismatch; its evidence is retained separately. The unchanged texture and
cube scenes are included as regression comparisons. See
`native-results/color-parity-step/`; 26 host tests and all consumer builds/audits
pass. This does not complete full original programs, repeated performance or
hardware gates.


## Original texcoord page checkpoint

The complete unchanged `drawTexcoordPage` from `examples/texture/main.cpp` now
runs through the shared adapter and matches the C framebuffer exactly, with
zero GL errors. It covers projective q interpolation, nonprojective coordinates,
ignored r, SHORT/BYTE/FIXED/FLOAT/DOUBLE arrays and sizes 1, 2 and 4.
Rust implements `glTexCoord3f/4f` and vector variants, array sizes 1..4 and the
additional coordinate types. PICA unit 0 receives q and divides per fragment;
texture matrices and other texture units remain pending. See
`native-results/texcoord-step/`; 27 host tests and all existing consumer
builds/audits pass. These are one-run emulator comparisons, not repeated
performance or hardware validation.


## Original texture matrix page checkpoint

GL_TEXTURE has its own matrix stack, queries and dirty state. Shader texture
rows apply the matrix before NPOT scaling and per-fragment q division. The first
original texture page is extracted into a shared function with its body unchanged
except indentation, and matches the C image exactly at the original time=0.
The texcoord, texture and cube scenes also match. An initial primitives timeout
is retained separately from its retry; see `native-results/texmatrix-step/`.
29 host tests and existing C/C++/rust3ds builds/audits pass. Multitexturing,
mipmaps, compression, repeated CPU measurements and hardware gates remain open.


## Stack-frame regression and TexEnv groundwork checkpoint

The TexEnv/multitexture groundwork (larger `GpuVertex`, per-unit state) grew
`Renderer` to 11 KiB and `c3dglInit`'s stack frame to 33 KiB, overflowing the
32 KiB 3DS main-thread stack: every native program crashed in `c3dglInit`. The
renderer is now allocated and initialized in place on the heap (frame: 196 bytes)
and `check.sh` rejects `c3dglInit` frames above 8 KiB. The attribute stride is
derived from `GpuVertex`. The earlier `rg`-based symbol audits were vacuous on
machines without ripgrep and now use `grep`. All five unchanged original scenes
(primitives, texture, cube, texcoords, texmatrix) match the C framebuffer exactly;
see `native-results/texenv-step/` (one run each). 


## Multitexture checkpoint

Three texture units are now emitted to the GPU: the vertex shader reads five
attributes (position, color, three texcoord sets) and outputs texcoord0/1/2
through per-unit texture-matrix rows (c5..c11); texture units 0..2 have their own
image registers and TEV stages 0..2 carry the per-unit TexEnv words (modulate,
add, replace, decal, blend and COMBINE with sources, operands, constants and
scales). The complete unchanged `drawMultitexturePage` of `examples/texture/main.cpp`
(eight cells: modulate, add, COMBINE interpolate/dot3/subtract with RGB_SCALE, three
units, unit-1 texture matrix, per-unit client arrays) matches the C framebuffer
exactly; all six selected original scenes match. See `native-results/multitexture-step/`
(one run each; not repeated performance or hardware validation). Mipmaps,
GL_GENERATE_MIPMAP, compressed textures and the remaining example groups are open.


## Mipmap checkpoint

Mipmaps now work on the native path: `glTexImage2D`/`glTexSubImage2D` on levels
0..10, stored chains down to 8x8, `GL_GENERATE_MIPMAP` (2x2 box filter as in the C
renderer), GL completeness (a mipmap min filter without all levels disables the
unit, like the C renderer), the LOD register, nearest/linear mip filtering and
per-level NPOT padding. `GL_UNSIGNED_SHORT_5_6_5` uploads are stored as real
RGB565 texels (PICA format 3): expanding them to RGBA8 changed sampling by one
colour level in 531 pixels, native storage is exact. Unit 0 uses projection mode
only after a texcoord with q != 1 (sticky) or with a projective texture matrix,
as in the C renderer; always-on projection changed the sampler's LOD. All seven
selected original scenes, including the complete unchanged `drawMipmapPage`, match
the C framebuffer exactly; see `native-results/mipmap-step/` (one run each).
Compressed (ETC1/paletted) textures, 1D textures and the remaining example groups
are open. Host tests: 30 pass.


## Compressed texture and alpha test checkpoint

`glAlphaFunc`/`GL_ALPHA_TEST` write the PICA alpha-test register (queries included).
Textures now have a storage format per chain: RGBA8, RGB565, RGBA4, RGBA5551 and
native ETC1 (4x4 blocks reordered into PICA's 8x8 tile order, stored upside down with
the texture matrix mapping t to q - t, as in the C renderer). `glCompressedTexImage2D`
accepts ETC1 and the ten paletted OES formats, including paletted mip chains
(negative levels); packed client types (5_6_5, 4_4_4_4, 5_5_5_1) keep their precision.
Partial updates of compressed images report GL_INVALID_OPERATION. The complete
unchanged `drawCompressedPage` (paletted RGB8/RGBA4 NPOT/RGBA8 with alpha test/paletted
levels, ETC1, flipped ETC1 through the texture matrix, NPOT ETC1, ETC1 levels) and all
eight selected original scenes match the C framebuffer exactly; see
`native-results/compressed-step/` (one run each). 1D textures, sized internal formats,
multiple texture environments beyond the example pages and the other examples remain
open. Host tests: 32 pass.


## Scissor, points and lines checkpoint

`glScissor`/`GL_SCISSOR_TEST` write the PICA scissor registers for the rotated
target (clamped like the C renderer). `GL_POINTS`, `GL_LINES`, `GL_LINE_LOOP` and
`GL_LINE_STRIP` with `glPointSize`/`glLineWidth` are expanded on the CPU to
screen-aligned NDC quads (w clipping, square caps, perpendicular half width, as in
the C renderer) and drawn with an identity projection and without culling; the
renderer switches between this clip-space state and the normal MVP state per draw.
Immediate mode and client/VBO arrays share the path; expanded geometry is
transient. The complete unchanged `drawOverlay` of `examples/cube/main.cpp`
(scissored quad plus four `GL_LINES`) and all nine selected original scenes match
the C framebuffer exactly (`native-results/lines-step/`, one run each). Open: line
stipple, smooth lines, point sprites/attenuation/size arrays, polygon modes and
offset, user clip planes, and a scissored `glClear` (the memory fill still clears the
whole buffer). Host tests: 34 pass.


## Polygon pipeline and evaluator checkpoint

`glShadeModel`, `glPolygonMode` (per face), `glPolygonOffset`, `glEdgeFlag` and
`glRect*` are implemented, with GL provoking vertices (last vertex, first for
`GL_POLYGON`). When any of them is active, primitives are assembled on the CPU:
fills stay object-space triangles, outlines and vertices become clip-space quads that
are culled on the CPU, edge flags select outline edges and vertices. The polygon
offset of filled polygons is folded into the z row of the uploaded MVP (z' = z + bias w),
one draw per bias, with no shader change. Evaluators (`glMap1/2`, `glMapGrid`,
`glEvalCoord/Point/Mesh`, `glGetMap`) evaluate Bezier maps on the CPU into immediate
vertices (colors, texcoords, rational vertices). The complete unchanged pages 1, 2 and
3 of `examples/primitives/main.cpp` (points, lines, strips and fans, elements; flat
shading, polygon modes, edge flags, polygon offset, depth range; curves, rational
curve, colored/wire/textured bicubic patches and point meshes) match the C
framebuffer exactly, as do the eleven selected original scenes
(`native-results/primitives-step/`, one run each). Open: line stipple and smooth
lines, point sprites/attenuation/size arrays, edge-flag arrays, evaluated normals
(no lighting yet), two-sided lighting, user clip planes, scissored `glClear` and the
remaining examples. Host tests: 40 pass.


## Lighting and attribute stack checkpoint

CPU vertex lighting (lights, materials, color material, light model, two-sided
lighting with back colors, normalization/rescaling, spot cones, local viewer) is ported
from the C renderer and uses libm `powf`/`cosf`/`sqrtf` like it does. Normals come from
`glNormal*`, `glNormalPointer` or evaluators; lit vertices bypass the persistent mesh
cache. `glPushAttrib/glPopAttrib` and `glPushClientAttrib/glPopClientAttrib` snapshot the
implemented state and restore the pushed groups (current, point, line, polygon, lighting,
depth buffer, viewport, transform, enable, color buffer, eval, texture, scissor, client
vertex arrays and pixel store). The unchanged cells of the original lighting main loop
match the C framebuffer exactly, as do the eleven earlier scenes
(`native-results/lighting-step/`, one run each). New state of later features (fog,
stencil, clip planes, stipple, pixel state, lists) must join the snapshot when added.
Host tests: 44 pass.
