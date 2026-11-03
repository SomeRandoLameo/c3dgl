# Examples

The same demo in all three integrations: a spinning, textured, vertex-colored cube
(depth test, back-face culling, client arrays). A toggles nearest/linear filtering,
START exits. Each directory is a standalone downstream project.

| Directory | Integration | Build |
| --- | --- | --- |
| `c/` | C against the C ABI (`c3dgl_native.h`, `GL/gl.h`) | `cmake -S examples/c -B build-example-c -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake && cmake --build build-example-c` |
| `cpp/` | C++17 with RAII wrappers over the same ABI | `cmake -S examples/cpp -B build-example-cpp -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake && cmake --build build-example-cpp` |
| `rust/` | Cargo dependency (`c3dgl::raw`) with ctru-rs | `cd examples/rust && cargo 3ds build --release` (needs `cargo install cargo-3ds`) |

Requires devkitPro (3ds-dev) and the pinned nightly Rust toolchain. The acceptance
and benchmark consumers live in `tests/consumers/` and `rust3ds/`.
