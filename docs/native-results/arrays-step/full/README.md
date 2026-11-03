> Correction, 2026-10-13: the functional GPU/image evidence below remains valid, but the C timing baseline used FLOAT arrays and did not hit its persistent GPU cache. These numbers therefore do not establish a matched-residency performance advantage. The corrected benchmark uses the C compact SHORT layout, explicitly asserts cache residency and reuse, and is recorded in the depth/culling checkpoint at `docs/native-results/depth-step/`.

# Complete arrays, indices and VBO emulator gate

2026-10-13: **PASS** in Azahar. Three runs each of native C, native C++, the real ctru-rs/rust3ds application and the C/Citro3D reference renderer.

All 12 completed reports contain 15 workload measurements: static, matrix, viewport, persistent indexed VBO and buffer update, each at 1/64/512 draws. Each measurement uses 30 warmup and 120 measured frames. CPU work excludes explicitly measured GPU/VBlank waiting.

All 84 full GPU-readback images match across consumers and repetitions for their corresponding scene. The indexed image equals the static baseline; the update image differs and matches the reference. Each native consumer passes all eight client-array/index/cache/in-frame-update/deletion image checks and recovers its linear/VRAM space after closing the VBO renderer. The lifecycle scenes also complete eight init/close cycles and 240 checked frames in each native run.

## Matched VBO workload CPU time

Values are milliseconds per frame. Native columns show the slowest of three runs; the reference column shows the fastest. All 45 individual CPU comparisons (three native consumers × five workloads × three draw counts) pass.

| Workload | Draws | Native C | Native C++ | rust3ds | C reference |
|---|---:|---:|---:|---:|---:|
| indexed | 1 | 0.3358 | 0.3358 | 0.3358 | 0.4247 |
| indexed | 64 | 0.5201 | 0.5201 | 0.5172 | 1.5881 |
| indexed | 512 | 1.8301 | 1.8301 | 1.8071 | 9.8609 |
| update | 1 | 0.4085 | 0.4085 | 0.4084 | 0.4255 |
| update | 64 | 0.5928 | 0.5928 | 0.5898 | 1.5888 |
| update | 512 | 1.9028 | 1.9028 | 1.8798 | 9.8617 |

The new VBO workloads reduce measured guest CPU work by 4.0–81.7% versus the C reference. The vertex layouts, indices, rendered output, update bytes and synchronization are matched. Updates change the first vertex position by ±0.0625 each frame (12 bytes), and update the element buffer (6 bytes).

## CPU increases from the previous checkpoint

The existing draw workloads became more expensive relative to the frame-state checkpoint. These increases remain visible rather than being hidden by the larger advantage against the C renderer. The measurements include the added draw-path selection in the acceptance programs and explicit geometry binding in the native renderer. All native measured total-frame times remain 16.7122 ms/frame; emulator GPU time is approximately constant.

### Native C

| Workload / draws | Previous CPU ms | Current CPU ms | Change |
|---|---:|---:|---:|
| static / 1 | 0.3340 | 0.3345 | +0.1% |
| static / 64 | 0.4072 | 0.4365 | +7.2% |
| static / 512 | 0.9268 | 1.1617 | +25.3% |
| matrix / 1 | 0.3363 | 0.3368 | +0.1% |
| matrix / 64 | 0.5514 | 0.5782 | +4.9% |
| matrix / 512 | 2.0920 | 2.3059 | +10.2% |
| viewport / 1 | 0.3350 | 0.3355 | +0.1% |
| viewport / 64 | 0.4741 | 0.4990 | +5.3% |
| viewport / 512 | 1.4683 | 1.6670 | +13.5% |

### rust3ds

| Workload / draws | Previous CPU ms | Current CPU ms | Change |
|---|---:|---:|---:|
| static / 1 | 0.3338 | 0.3345 | +0.2% |
| static / 64 | 0.3981 | 0.4351 | +9.3% |
| static / 512 | 0.8542 | 1.1502 | +34.7% |
| matrix / 1 | 0.3360 | 0.3366 | +0.2% |
| matrix / 64 | 0.5335 | 0.5693 | +6.7% |
| matrix / 512 | 1.9488 | 2.2352 | +14.7% |
| viewport / 1 | 0.3348 | 0.3354 | +0.2% |
| viewport / 64 | 0.4538 | 0.4892 | +7.8% |
| viewport / 512 | 1.3060 | 1.5887 | +21.6% |

## Build and rollback checks

`build-check.txt` records the full passing `native/scripts/check.sh`: 13 Rust tests, C/C++ host ABI, native C/C++ 3DS builds, the actual rust3ds Cargo consumer and symbol audits. Host tests also inject a failed mesh allocation and reject a color-array range beyond VBO storage; both paths preserve live GPU allocation counts and allow subsequent valid draws. All four ROM hashes remain identical after the final build verification.

## Corrected test error and provenance

The first rust3ds update payload was incorrectly inferred as f64, while the declared vertex format was f32. The cross-consumer full-frame comparison detected the wrong update image. The payload is now explicitly `[f32; 3]`; all three accepted rust3ds runs use the corrected ROM. The rejected report/image and original manifest are retained in `../discarded-rust-f64/` and are excluded from acceptance. `resume-provenance.json` records which already-successful runs of unchanged C/C++/reference binaries were reused after verifying their original SHA-256 hashes.

## Reproduction

```
python3 native/scripts/emulator_check.py --vbo-state \
  --emulator /tmp/C3DGL-Azahar.app/Contents/MacOS/azahar \
  --sdmc '/Users/elias/Library/Application Support/Azahar/sdmc' \
  --rust3ds-rom build-native-rust3ds/armv6k-nintendo-3ds/release/c3dgl-rust3ds-triangle.3dsx \
  --baseline-summary docs/native-results/frame-step/azahar-summary.json \
  --output docs/native-results/arrays-step/full --runs 3
```

Use `--resume` to revalidate the saved runs against the exact executable manifest. The runner rejects changed executables, missing checks, missing fresh GPU images, mismatched images, and any failed CPU comparison. All findings here concern emulated guest execution; the separate real-3DS gate remains open.
