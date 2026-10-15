#!/bin/sh
# Host bench CPU time per frame of each scene (microseconds), best of three runs. native/bench/build.sh first.
set -eu
out=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)/build-native-bench
cd "$out" && mkdir -p 'sdmc:'
for bin in bench_*; do
    case "$bin" in *.dSYM) continue;; esac
    c=${bin#bench_}; best=
    for i in 1 2 3; do
        ./"$bin" > /dev/null 2>&1 || { echo "$c FAILED"; continue 2; }
        v=$(sed -n 's/.* CPU \([0-9.]*\) .*/\1/p' "sdmc:/c3dgl-old-$c-native-results.txt")
        v=$(echo "$v * 1000" | bc -l)
        if [ -z "$best" ] || [ "$(echo "$v < $best" | bc -l)" = 1 ]; then best=$v; fi
    done
    printf '%-14s %10.2f us\n' "$c" "$best"
done
