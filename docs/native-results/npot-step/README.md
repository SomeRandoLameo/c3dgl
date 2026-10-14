# NPOT texture checkpoint

The unchanged original cube scene (12x12 checker) and texture scene (16x16 F)
are compared through the existing common capture adapter. One run per renderer
and scene, with original source, ROM and framebuffer hashes in `summary.json`.

Both scenes match byte-for-byte across all 96,000 pixels, with GL error zero.
The former NPOT rejection is retained in `../old-examples/`.

Implementation: separate logical and padded dimensions, minimum 8x8 Morton-tiled
RGBA8 GPU storage, vertex shader UV scaling, and the same repeat/mirrored-repeat/
clamp edge padding as the C renderer. Subimage and wrap changes regenerate padding
in new storage so queued draws retain their old image. Allocation/flush failure
leaves the previous object intact. Like the C reference, GPU wrap occurs at the
padded dimensions; this is not a claim of exact arbitrary NPOT repeat periods.

Validation: 25 host tests including NPOT dimensions/padding, subimage edges,
wrap allocation rollback and shader packets; C/C++ ABI and 3DS consumers;
real rust3ds Cargo builds; native executable audits excluding Citro3D and
libctru GPU encoder/shader helpers. The initial rust3ds build lacked the ARM
linker in PATH; the corrected build passes. Azahar required a launch outside
the filesystem sandbox after sandboxed starts exited before running the scene.

These are selected scenes, not complete original applications. Repeated CPU
performance gates and physical hardware validation remain pending. Primitive
float-color quantization differences from the preceding checkpoint remain open.
