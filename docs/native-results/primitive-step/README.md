# Filled primitives checkpoint

Triangles, triangle strips/fans, quads, quad strips and convex polygons now share
triangle assembly across arrays, indexed VBOs and immediate mode. Added float
vertex/color/UV vectors, byte color vectors and checked u32 index inputs.
The public native vertex ABI and GPU triangle command path are unchanged.

All 24 host tests pass. Coverage includes strip winding/degenerate parity,
incomplete tails, output limits, captured colors/UVs, unaligned vector reads,
mode-specific cache reuse, queued EBO replacement and rejection of u32::MAX.
C/C++ and three Cargo consumers build/link and pass GPU dependency audits.
Full output: `build-check.txt`.

Exactly one native C Azahar process was launched. `one-run/results.txt` records
32 passing checks: zero GL errors, 18 colored cell samples, 12 complete-cell
comparisons between array/VBO/immediate paths, and exact linear/VRAM recovery.
Culling is enabled throughout. The VRAM pool is primed before measuring its
baseline. The actual GPU PPM and ROM/image hashes are retained in `one-run/`.
C++ and Rust consumers have build verification, without additional emulator runs.
No performance improvement or physical hardware execution is claimed.
