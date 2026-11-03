# rust3ds compatibility checkpoint — 2026-10-13

Passed: real Cargo dependency consumer built with cargo-3ds 0.1.5 and the pinned
nightly-2026-10-01 toolchain. ctru-rs 0.7.1 / ctru-sys 0.5.0 are locked to git
commit `9576b695897a7474829f490d696807b26a208747`; see the consumer Cargo.lock for
all dependencies. Same Azahar 2126.1.2/Vulkan configuration as the matrix checkpoint.

The `rust3ds` feature selects the application-owned std allocator and panic
runtime and builds the libctru adapter automatically. The C/C++ static library
remains no_std by default. The app uses real Gfx/Apt/Hid/Console, Vec and file I/O,
not a prebuilt C acceptance program.

Three native C runs, three C-reference runs, one C++ consumer and three rust3ds
runs pass. All twenty full GPU readbacks (static and matrix workloads) match the
corresponding C output byte for byte. Each Rust app checks 240 frames, eight
init/close cycles and exact linear/VRAM recovery, then completes six timed
workloads with 30 warmups and 120 measurements each. Application allocations
also work independently of GPU memory. No Citro3D/GPUCMD/DVLB/shaderProgram symbols
appear in the Rust consumer executable. C/C++ build and ABI gates still pass.

The matrix-only C consumer timings are unchanged from the previous checkpoint.
Conservative CPU comparison: slowest rust3ds run vs fastest C-reference run:

| Workload | Draws/frame | C CPU ms | rust3ds CPU ms | Reduction |
| --- | ---: | ---: | ---: | ---: |
| static | 1 | 0.4244 | 0.3335 | 21.4% |
| static | 64 | 1.5876 | 0.3888 | 75.5% |
| static | 512 | 9.8589 | 0.7814 | 92.1% |
| matrix | 1 | 0.4260 | 0.3353 | 21.3% |
| matrix | 64 | 2.3427 | 0.5033 | 78.5% |
| matrix | 512 | 16.0259 | 1.7081 | 89.3% |

Rust app total latency remains 16.7122 ms/frame. Direct Cargo/LTO calls can be
optimized differently from C FFI calls, so these are reported separately.
Measurements are guest CPU timings in Azahar, not physical-3DS throughput.
Startup-dialog timeouts were resumed using complete archived runs; no incomplete
sample was accepted. `environment-build.json` records the tested executable hashes.

This checkpoint predates the next GL clear/viewport stage. Repeat its two-workload
report with the corresponding binaries; use `--frame-state` for the subsequent
nine-workload binaries. The current Rust consumer source includes that next stage.
