# Native Rust bootstrap

This is the beginning of `native-rewrite`, not a working renderer. The C renderer
and examples at the repository root remain unchanged. Do not link both renderers
into one program once native exports the existing GL/platform symbols.

## Build

Rust is pinned by `rust-toolchain.toml`. From this directory:

```
cargo test --features host-tests
cargo build --release --target armv6k-nintendo-3ds -Zbuild-std=core,alloc
```

The target uses the built-in ARMv6K Nintendo 3DS ABI, including hard-float.
The artifact is `target/armv6k-nintendo-3ds/release/libc3dgl.a`.
`alloc` uses callbacks from the separate platform adapter; GPU storage must not
use this ordinary CPU allocator. Panic aborts and does not unwind across C.

A devkitPro application can use `add_subdirectory(path/to/native native-build)`
and link `c3dgl::native`. This target links libctru, never Citro3D. The original
root `c3dgl::c3dgl` target still builds the C reference with Citro3D.

For the host C ABI smoke test, rebuild core/alloc with abort semantics:

```
cargo build --release -Zbuild-std=core,alloc --target aarch64-apple-darwin
cc -Iinclude tests/abi.c tests/platform_host.c \
  target/aarch64-apple-darwin/release/libc3dgl.a -o target/abi-test
./target/abi-test
```

Use your host triple in place of `aarch64-apple-darwin` on another platform.
`host-tests` enables std only for unit tests; do not enable it for the 3DS library.
The encoder has no global state. C buffers must be valid, aligned and disjoint.

## Remaining first milestone

- GPU-visible linear command/vertex storage and VRAM color/depth targets.
- PICA baseline state, viewport, vertex layout, shader upload and draw trigger.
- Command-list finalization, cache flush and checked GX submission.
- Clear, display transfer, VBlank/swap and completion before resource release.
- Minimal C and C++ triangle applications and hardware verification over repeated
  frames, allocation failures and init/close cycles.

No existing GL entry point is currently implemented by the Rust library. There
is deliberately no successful `c3dglInit` stub or software triangle substituted
for the missing GPU renderer.
