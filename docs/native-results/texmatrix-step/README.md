# Original texture matrix page

The original first texture page now renders exactly like the C reference:
zero differing pixels in the complete 400x240 capture and GL error zero.
It covers translation, tiling, rotation about the center, time=0 translation,
constant q=2, varying q=1+s and a vertically mirrored 12x12 NPOT F texture.

The original page was inline in `examples/texture/main.cpp`. Its complete body
was moved to `drawTextureMatrixPage(texPot, texNpot, time)`; both interactive
main and the shared capture adapter call that function. A comparison against
HEAD before this change verified that the body differs only in indentation.
Drawing calls, texture inputs and their order were preserved. The adapter
captures the original first-frame time of zero. No missing API stubs are used.

Rust adds the GL_TEXTURE matrix mode, an independent two-entry texture stack,
matrix/depth/limit queries and separate dirty tracking. Three shader uniform
rows compute s, t and q; NPOT scale multiplies the s/t rows, including translation,
after the texture transform. PICA divides by transformed q per fragment.
Changing texture matrices preserves resident mesh caches and avoids rebuilding
MVP state. Frame initialization refreshes the texture uniforms.

Validation: 29 host tests, including independent matrix state, stack push/pop,
overflow/underflow, projective q row, NPOT scaling and command overflow rollback;
C/C++ ABI and 3DS consumer builds; real rust3ds Cargo consumers; native symbol
audits excluding Citro3D/GPUCMD/DVLB/shaderProgram helpers.

The original texcoord page, texture and cube regression scenes match exactly.
The first native primitives launch timed out after 45 seconds, before a fresh
guest report. Its failed launch is retained in the top-level `summary.json`;
that complete five-case run therefore remains failed. The separate
`primitives-retry/` holds the passing follow-up comparison: zero differing pixels
and GL error zero. The timeout cause is not
established; passing other scenes does not explain it.

These are functional emulator scene comparisons. Animated-time sampling,
full interactive applications, repeated CPU performance and physical hardware
validation remain separate. Multitexturing, mipmaps and compression are pending.
