#!/bin/sh
# Host bench: the native renderer and the original example scenes built for macOS (mock platform), for CPU
# profiling with a real profiler. Usage: native/bench/build.sh [case...]; binaries in build-native-bench/.
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)
out=${C3DGL_BENCH_OUT:-$root/build-native-bench}
samples=${PERF_SAMPLES:-120}
host=$(rustc -vV | sed -n 's/^host: //p')
mkdir -p "$out"
(cd "$root/native" && CARGO_TARGET_DIR="$out/cargo" cargo build --quiet --release --features host-bench --target "$host")
lib="$out/cargo/$host/release/libc3dgl.a"
cc -O2 -c "$here/platform_host.c" -o "$out/platform.o"
glu="$out/libglu.a"
if [ ! -f "$glu" ]; then
    mkdir -p "$out/glu"
    for f in $(cat "$here/glu_sources.txt"); do
        o="$out/glu/$(echo "$f" | tr '/.' '__').o"
        case "$f" in *.cc) comp="c++ -std=c++11";; *) comp=cc;; esac
        $comp -O2 -w -DLIBRARYBUILD -I"$here/shim" -I"$root/external/glu/include" -I"$root/include" \
            -I"$root/external/glu/src/include" -I"$root/external/glu/src/libnurbs/internals" \
            -I"$root/external/glu/src/libnurbs/interface" -I"$root/external/glu/src/libnurbs/nurbtess" \
            -c "$root/external/glu/src/$f" -o "$o"
    done
    ar rcs "$glu" "$out"/glu/*.o
fi
for case in "${@:-cube}"; do
    upper=$(echo "$case" | tr a-z A-Z)
    c++ -std=c++17 -O2 -g -w -I"$here/shim" -I"$root/include" -I"$root/external/glu/include" -I"$root/native/include" -I/opt/homebrew/include \
        -DC3DGL_COMPARE_NATIVE=1 -DC3DGL_COMPARE_PERF=1 -DPERF_SAMPLES="$samples" -DC3DGL_OLD_"$upper"=1 \
        "$root/native/compare/old_examples.cpp" "$glu" "$lib" "$out/platform.o" -L/opt/homebrew/lib -lpng \
        -o "$out/bench_$case"
done
