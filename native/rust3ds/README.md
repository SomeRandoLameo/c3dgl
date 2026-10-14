# Real rust3ds consumer

This is a Rust application built with the official `rust3ds/ctru-rs` crates and
`cargo-3ds`, using c3dgl as a Cargo dependency. It is not a C program launched
from Rust and does not link a separately built renderer archive.

```toml
[dependencies]
c3dgl-native = { path = "../native", features = ["rust3ds"] }
ctru-rs = { git = "https://github.com/rust3ds/ctru-rs.git" }
```

The crate is imported as `c3dgl`. Its `raw` module exposes the implemented native
and GL entry points and `#[repr(C)]` vertex/stats types. Calls are unsafe: graphics
must already be initialized, all renderer calls must use one application thread,
and the renderer owns GPU submission until close. Keep `ctru::services::gfx::Gfx`
alive until after `c3dglClose()`. Do not use another GX queue or Citro3D renderer
while c3dgl is active. Do not link the C static archive in addition to this crate.

With the `rust3ds` feature, the library uses the application's Rust std allocator
and panic runtime. Its Cargo build script compiles the same libctru platform
adapter with the ARM hard-float ABI and links it automatically. Without that
feature, the existing C/C++ static library stays `no_std` with its standalone
allocator and panic handler. GPU memory always uses linear/VRAM allocation.

## Build and run

Install devkitPro's 3ds-dev toolchain, the pinned nightly Rust toolchain with
rust-src/rustfmt, LLVM/libclang for ctru-sys bindings, and `cargo-3ds`:

```sh
cargo install --locked cargo-3ds
export DEVKITPRO=/opt/devkitpro
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITPRO/tools/bin:$DEVKITARM/bin:$PATH"
cd native/rust3ds
cargo fetch --locked
cargo 3ds build --release --locked
```

`cargo-3ds` must run from this consumer's directory to find project metadata and
package the ELF into a `.3dsx`. On macOS, set `LIBCLANG_PATH` if bindgen cannot
find your libclang. `native/scripts/check_rust3ds.sh` performs an offline rebuild
and rejects Citro3D/GPUCMD/DVLB/shaderProgram symbols in the resulting executable.
The committed lockfile pins the tested rust3ds sources and transitive packages.

The app uses real `Gfx`, `Apt`, `Hid`, `Console`, `Vec` and file I/O. It checks
240 GPU frames and eight init/close cycles with linear/VRAM recovery, then runs
1/64/512 draws with static matrices, per-draw matrix changes and per-draw viewport
changes. Each workload uses 30 warmups and 120 measured frames. It saves complete
GPU readbacks and a clear/draw ordering scene outside the timed loops.

Pass its generated `.3dsx` to `native/scripts/emulator_check.py --rust3ds-rom` to
include it in the same image and CPU gates as the C/C++ consumers. Use
`--frame-state` for the new viewport/clear checkpoint. Results describe Azahar
execution; physical-3DS validation remains separate.

Official tooling: [ctru-rs](https://github.com/rust3ds/ctru-rs),
[cargo-3ds](https://github.com/rust3ds/cargo-3ds).

The consumer also runs the array/VBO checkpoint: client-array and u8/u16 index
readback comparisons, persistent indexed VBO draws, in-frame SubData changes,
BufferData replacement and deletion, and GPU memory recovery after shutdown.
The seven benchmark workloads are static, matrix, viewport, indexed, update, cube and raster;
all run at 1/64/512 draws. Use `emulator_check.py --vbo-state --rust3ds-rom ...`
for the complete C/C++/rust3ds/reference image and performance gate.


The same build now produces `textured.3dsx` from `src/bin/textured.rs`. This consumer
uses the exported texture, UV-array, immediate-mode and blending entry points from
`c3dgl::raw`, with a persistent VBO checkerboard and a transparent textured triangle.
Its executable is included in the native dependency audit. Compilation/linking and
host behavior checks pass; emulator verification for this new consumer is pending.

The build also produces `primitives.3dsx`: six filled primitive modes through
client arrays, u32-indexed VBOs and immediate vector calls, with culling enabled.
It is included in executable dependency audits. Its native C counterpart has
one passing Azahar smoke run; this Cargo consumer has compilation/link verification.
