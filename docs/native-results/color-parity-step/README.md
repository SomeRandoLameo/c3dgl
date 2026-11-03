# Exact primitive color parity

The unchanged original primitive scene now matches the C framebuffer exactly:
zero differing pixels across the complete 400x240 image, with GL error zero.
The original texture and cube regression scenes also match exactly, with zero
GL errors. Source, adapter, ROM and GPU image hashes are recorded in `summary.json`.

The reference rounds `glColor*f` to unsigned bytes using clamping and
`(value * 255 + 0.5)`, then normalizes those byte attributes on the GPU with
its PICA float24 shader constant. Rust previously passed unquantized float
colors. CPU quantization alone reduced 4,591 differing pixels to one channel
at one pixel; that intermediate failed exact comparison is retained in
`../color-parity-cpu-step/`. The final path sends byte-valued float attributes
and performs the same shader multiplication. Float/vector, unsigned-byte and
array GL colors share the byte representation. The public native vertex API
retains normalized input and scales it for the internal shader layout.

The embedded shader generator now retains float constants from the assembled
DVLE; initialization uploads their exact values, with no libctru shader helpers.

Validation: 26 host tests, including color limits, all normalized byte values
and both sides of rounding thresholds; complete C/C++ ABI and 3DS builds;
real rust3ds Cargo consumers; dependency audits rejecting Citro3D/GPUCMD/DVLB/
shaderProgram symbols. One Azahar run per backend and original scene.
Full-program compatibility, repeated CPU performance and real-hardware gates
remain separate.
