# One Azahar texture/blend smoke run

Exactly one emulator process was launched. The binary SHA-256 is recorded in
`summary.json`; original guest output is preserved in `results.txt`, unchanged.
Seven GPU readbacks are saved as PPM; the PNG is a lossless conversion of frame 1.

All 18 rendering/image checks pass: UVs, orientation, repeat/mirror/clamp, changed
linear filtering, queued old-image preservation, updated-image visibility, alpha
blending over geometry/background, reverse subtraction, minimum equation and clear.
GL error is zero. This is a short native C consumer check, not repeated performance
acceptance or an independent run of the C++/Rust consumers.

The nineteenth assertion reports FAIL because its initial VRAM baseline is zero
before the first VRAM allocation. Final free space is the complete 6 MiB VRAM pool
(6,291,456 bytes); linear free space exactly returns to 31,941,632 bytes.
The [libctru allocator](https://github.com/devkitPro/libctru/blob/master/libctru/source/allocator/vram.cpp)
initializes the pool on allocation, while `vramSpaceFree()` only reads pool state.
The installed header defines `OS_VRAM_SIZE` as 0x600000. The existing triangle test
already primes this pool before collecting its baseline. The smoke test now does
likewise. Its original raw failed result remains recorded, and the corrected harness
was rebuilt but deliberately not run a second time.
