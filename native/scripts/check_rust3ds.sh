#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
: "${DEVKITPRO:=/opt/devkitpro}"
: "${DEVKITARM:=$DEVKITPRO/devkitARM}"
: "${CARGO_TARGET_DIR:=$root/build-native-rust3ds}"
export DEVKITPRO DEVKITARM CARGO_TARGET_DIR
cd "$root/native/rust3ds"
cargo fmt --check
cargo 3ds build --release --locked --offline
for consumer in c3dgl-rust3ds-triangle textured primitives; do
    elf="$CARGO_TARGET_DIR/armv6k-nintendo-3ds/release/$consumer.elf"
    "$DEVKITARM/bin/arm-none-eabi-nm" "$elf" > "$CARGO_TARGET_DIR/$consumer-symbols.txt"
    if grep -E '(C3D_|GPUCMD_|DVLB_|shaderProgram)' "$CARGO_TARGET_DIR/$consumer-symbols.txt"; then
        echo "Unexpected Citro3D/GPU encoder dependency in rust3ds $consumer" >&2
        exit 1
    fi
done
echo 'PASS: rust3ds Cargo dependency, ctru-rs std runtime, 3DSX build and symbol audit.'
