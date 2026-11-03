# Complete original texcoord page

The common capture adapter invokes the unchanged original `createF(16)` and
`drawTexcoordPage` from `examples/texture/main.cpp`. All eight page cells run
with the original inputs: projective and affine trapezoids, r ignored, SHORT
coordinates, BYTE size 1, FIXED coordinates, FLOAT size 4 with q=2, and mirrored
DOUBLE coordinates. The complete 400x240 frame matches the C reference exactly,
with zero GL errors. Hashes and readbacks are retained in `summary.json` and
case directories. Original source files are unchanged; no missing API stubs.

Rust now supports immediate `glTexCoord3f/4f` and their vector variants;
texcoord arrays of size 1..4 in BYTE, SHORT, FLOAT, FIXED and DOUBLE; and
per-fragment projective division on PICA unit 0. Its shader writes every output
component, including unused w, and routes q through the texcoord0w attribute.
The existing padded NPOT scaling applies to s/t before q division.

Validation: 27 host tests, including missing-coordinate defaults, signed/fixed
conversion and unaligned double stride; C/C++ ABI, existing native 3DS and
real rust3ds builds; dependency audits excluding Citro3D and libctru GPU
encoder/shader helpers. One Azahar run per renderer and case. Selected existing
texture and cube scenes serve as regression checks. Texture matrices,
multitexturing, later original pages, repeated CPU gates and hardware validation
remain pending.
