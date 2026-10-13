# Native texture/blend checkpoint

Implemented one 2D unit, resident level-zero RGB/RGBA unsigned-byte images,
8..1024 power-of-two dimensions, unpack alignment, nearest/linear filtering,
repeat/mirrored/clamp-to-edge wrapping, modulate/replace, UV client/VBO arrays and
immediate UVs. Blend factors/equations can be selected independently for RGB/alpha.
Mipmap sampling is incomplete until supported; this revision implements no mip levels.

The complete `native/scripts/check.sh` passes, including 19 host tests, C/C++ ABI,
all native 3DS consumers, both rust3ds consumers and independent GPU dependency
audits. The new executables are `c3dgl_native_textured`,
`c3dgl_native_textured_cpp` and Rust `textured`. Full output: `build-check.txt`.

Host assertions cover byte/channel/row conversion, unpack stride, bounds and
incomplete state; rejected allocation preserves the old image; queued draws keep
old images through replacement/deletion until a completed GPU wait. UV VBO writes
invalidate dependent meshes, and texture updates leave resident geometry reusable.
Packet tests cover texture binding, texture-only cache clear, disabled/incomplete
combiners, blending and command overflow without partial writes.

The public native upload ABI remains 32 bytes per vertex; internal resident
vertices are now 48 bytes with UVs. Subimage writes copy and flush the full image
into a new allocation to preserve queued draws. No memory/CPU throughput improvement
is claimed. No emulator or hardware execution was performed for this checkpoint;
GPU pixel correctness, hardware resource recovery and performance gates are pending.

Register layouts were checked against the upstream devkitPro
[texture encoder](https://github.com/devkitPro/citro3d/blob/master/source/texture.c),
[blend encoder](https://github.com/devkitPro/citro3d/blob/master/source/effect.c),
and [shader output mapping](https://github.com/devkitPro/libctru/blob/master/libctru/source/gpu/shbin.c).
The native implementation emits its own packets and does not link those GPU encoders.
