#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$root"
: "${DEVKITPRO:=/opt/devkitpro}"
export DEVKITPRO
host_target=$(rustc -vV | sed -n 's/^host: //p')
python3 native/scripts/generate_shader.py --check
cargo fmt --manifest-path native/Cargo.toml --check
cargo test --offline --features host-tests --manifest-path native/Cargo.toml
cargo build --offline --release --target "$host_target" -Zbuild-std=core,alloc --manifest-path native/Cargo.toml
mkdir -p build-native-host
cc -Wall -Wextra -Werror -c native/tests/platform_host.c -o build-native-host/platform.o
cc -Wall -Wextra -Werror -Inative/include native/tests/abi.c \
    "native/target/$host_target/release/libc3dgl.a" build-native-host/platform.o -o build-native-host/abi-c
c++ -Wall -Wextra -Werror -x c++ -Inative/include -c native/tests/abi.c -o build-native-host/abi-cpp.o
c++ build-native-host/abi-cpp.o "native/target/$host_target/release/libc3dgl.a" \
    build-native-host/platform.o -o build-native-host/abi-cpp
build-native-host/abi-c
build-native-host/abi-cpp
cmake -S native -B build-native -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake"
cmake --build build-native
cmake -S native/benchmark -B build-native-reference -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/3DS.cmake"
cmake --build build-native-reference
for consumer in c3dgl_native_triangle c3dgl_native_triangle_cpp c3dgl_native_cube c3dgl_native_immediate c3dgl_native_textured c3dgl_native_textured_cpp c3dgl_native_texture_smoke c3dgl_native_primitives c3dgl_native_primitive_smoke c3dgl_native_primitives_cpp; do
    "$DEVKITPRO/devkitARM/bin/arm-none-eabi-nm" "build-native/$consumer.elf" > "build-native-host/$consumer-symbols.txt"
    if grep -E '(C3D_|GPUCMD_|DVLB_|shaderProgram)' "build-native-host/$consumer-symbols.txt"; then
        echo "Unexpected Citro3D/libctru GPU encoder or shader helper in $consumer" >&2
        exit 1
    fi
done
"$root/native/scripts/check_rust3ds.sh"
echo 'PASS: Rust tests, C/C++ ABI, rust3ds consumer, 3DS builds and native dependency audits.'
