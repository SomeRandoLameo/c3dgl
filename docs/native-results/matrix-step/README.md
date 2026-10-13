# GL matrix checkpoint — 2026-10-13

Passed in Azahar 2126.1.2 with the same configuration as the original triangle
checkpoint. See `environment.json` and `azahar-summary.json`. No physical 3DS
measurement is claimed.

Implemented: modelview/projection mode, float load/multiply, identity,
push/pop (32 modelview and 2 projection entries), translation, scale, rotation,
orthographic/perspective projection and sticky GL errors. Only implemented
symbols are declared in the native GL header. Vertices stay in object space;
the native vertex shader applies MVP, display rotation and PICA depth conversion.
Uniforms are emitted only after a matrix change or GPU state restoration.
Drawing and frame control still use the native API; viewport, GL clear, queries,
texture matrices, arrays/VBOs, depth/culling and the cube are subsequent checkpoints.

## Verification

- Nine Rust tests, including matrix multiplication order, both stack boundaries,
  first-error retention, rotation, projection, GPU uniform layout, command overflow,
  redundant-upload avoidance, lifecycle and allocation rollback.
- C/C++ ABI link checks and 3DS builds; no Citro3D/GPUCMD/DVLB/shaderProgram symbols.
- Three native C runs, three reference runs and one native C++ run. All native
  runs complete 240 checked frames, eight init/close cycles and six workloads:
  4,564 native GPU frames and 32 checked resource-recovery cycles in total.
- Both full 400×240 GPU images (static and transformed) match byte for byte across
  all seven runs. The static image also matches the original triangle checkpoint.
  Scale/translation is checked on the emulated GPU; rotation and projection
  formulas are checked on the host and still need broader GPU scene coverage.
- Each workload has 30 warmups and 120 measured frames. Static matrices are
  unchanged; the matrix workload alternates translation before each draw and
  between frames, including the one-draw case. Persistent geometry and 60 Hz
  synchronization are preserved. CPU work excludes GPU/VBlank waiting.

![Actual transformed GPU readback](matrix.png)

## CPU comparison with C/Citro3D

Slowest native C run versus fastest reference run, in ms/frame:

| Workload | Draws/frame | C CPU ms | Rust CPU ms | CPU reduction |
| --- | ---: | ---: | ---: | ---: |
| static | 1 | 0.4244 | 0.3338 | 21.3% |
| static | 64 | 1.5876 | 0.4044 | 74.5% |
| static | 512 | 9.8589 | 0.9056 | 90.8% |
| matrix | 1 | 0.4260 | 0.3359 | 21.2% |
| matrix | 64 | 2.3427 | 0.5383 | 77.0% |
| matrix | 512 | 16.0259 | 1.9870 | 87.6% |

All six gates pass. Native total frame latency remains 16.7122 ms. Reference
latency is 16.7121 ms (static) and up to 16.7177 ms (512 matrix changes). Emulator
GPU time is effectively constant and cannot establish physical shader throughput.
The C++ consumer also passes; its timings remain separately available in the raw
report rather than being mixed into the native C comparison.

## Incremental cost relative to the original Rust triangle

Same static workload, comparing conservative maxima from both checkpoints:

| Draws/frame | Previous Rust ms | Current Rust ms | Change |
| ---: | ---: | ---: | ---: |
| 1 | 0.3337 | 0.3338 | +0.03% |
| 64 | 0.4010 | 0.4044 | +0.85% |
| 512 | 0.8789 | 0.9056 | +3.04% |

The 512-draw case adds about 0.027 ms CPU per frame. Report this cost explicitly;
the matrix feature preserves the large advantage against C, but is not free.
The command buffer also grows from 64 to 128 KiB: 512 draws with new uniforms
need 21,504 command words plus frame commands, exceeding the old capacity.
Frame-time allocation remains zero. The initial overflow was fixed and the full
three-run gate repeated after the correction.

## Repeat

Run `native/scripts/check.sh`, then `native/scripts/emulator_check.py` with the
same emulator and SDMC arguments as the previous checkpoint. Use a separate
`--output` directory to preserve history. The runner measures both workloads and
reports static CPU changes against `--baseline-summary` (defaults to the original
triangle report). Direct macOS executable launches may show startup/update
notices; dismiss them and retain the same emulator version for comparisons.
