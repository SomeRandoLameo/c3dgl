# Examples

Every example lives in its own folder. There is one self-contained set per language; no set
depends on another one or on any other code outside `include/` and `external/glu`.

| Set | Language | Integration | Examples |
| --- | --- | --- | --- |
| [`c/`](c) | C99 | C ABI (`c3dgl_native.h`, `GL/gl.h`), static library via CMake | `api`, `accum`, `clipplane`, `cube`, `fog`, `fragment`, `glu`, `lighting`, `lists`, `pixels`, `points`, `primitives`, `select`, `stipple`, `texgen`, `texture` |
| [`cpp/`](cpp) | C++17 | same ABI, from C++ | `accum`, `clipplane`, `cube`, `fog`, `fragment`, `glu`, `lighting`, `lists`, `pixels`, `points`, `primitives`, `select`, `stipple`, `texgen`, `texture` |
| [`rust/`](rust) | Rust | Cargo dependency (`use c3dgl::*`) | `api`, `accum`, `clipplane`, `cube`, `fog`, `fragment`, `lighting`, `lists`, `pixels`, `points`, `primitives`, `select`, `stipple`, `texgen`, `texture` (no `glu`, see below) |

The examples are small test programs: the top screen shows the scene, the bottom screen the
expected result and the frame statistics. START exits, A/B and the circle pad are described on
the bottom screen of each example.

## Build

Requires devkitPro (3ds-dev, 3ds-libpng) and, for Rust, the pinned nightly toolchain plus
`cargo install cargo-3ds`.

```sh
export DEVKITPRO=/opt/devkitpro

cmake -S examples/c -B build-examples-c -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
cmake --build build-examples-c

cmake -S examples/cpp -B build-examples-cpp -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/3DS.cmake
cmake --build build-examples-cpp

cd examples/rust && cargo 3ds build --release
```

The `.3dsx` files appear in the build directory (`target/armv6k-nintendo-3ds/release/` for Rust).

## Notes

- `glu` and `select` use Mesa GLU (`external/glu`) in the C and C++ sets. GLU is a C/C++ library
  and not part of the Rust crate, so the Rust set has no `glu` example; its `select` uses a small
  pick-matrix helper instead of `gluPickMatrix`.
- Checked in Azahar (macOS): all 16 C, all 15 C++ and all 15 Rust examples start and render. `clipplane`
  (33/33), `fog` (29/29) and `glu` (20/20) pass all their on-screen checks. `lighting` reports 58/62
  (identical in C and C++) and `api` lists failing checks (among them logic ops, which Azahar on macOS
  does not support in its hardware renderer): these are open renderer issues, not example bugs.
- The Rust set has everything except `glu`.
- `api` runs the same checks in all three languages and fails the same ones (the C and Rust versions stop at the
  same point in Azahar; that is an open renderer/emulator issue, not a porting difference).
- `cube` loads `romfs:/tonk.png` (C, C++: libpng from romfs; Rust: the `png` crate, embedded).
