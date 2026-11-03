#!/bin/sh
# Build iosrt-run for linux-aarch64 (glibc, static) and android-arm64 (bionic, static).
# usage: runtime/build.sh [linux|android|all] [outdir]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
target=${1:-all}
out=${2:-$root/build}
mkdir -p "$out/gen"
python3 -I "$root/tools/gen_decls.py" "$here/src/exports" "$out/gen/decls.h"
SRCS="$(find "$here/src" -name '*.c' -not -path '*/platform/android*' | sort) $here/src/abi/darwin_abi.S $here/cli/iosrt-run.c"
CFLAGS="-std=gnu11 -O2 -g -fsigned-char -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$out/gen -I$here/src/exports"
NDK=${ANDROID_NDK:-/opt/android-sdk/ndk/27.2.12479018}
build() { # name cc target extra
    echo "== building $1"
    $2 --target=$3 $CFLAGS $SRCS -o "$out/iosrt-run-$1" -static -lpthread -lm $4
}
case $target in
  linux|all) build linux-aarch64 clang aarch64-linux-gnu "-fuse-ld=lld" ;; esac
case $target in
  android|all) build android-arm64 "$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/clang" aarch64-linux-android29 "" ;; esac
