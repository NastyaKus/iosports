#!/bin/sh
# Build iosrt-run for linux-aarch64 (glibc, static) and android-arm64 (bionic, static).
# The Android build also contains the Apple-ABI C++ support (runtime/src/cxx).
# usage: runtime/build.sh [linux|android|all] [outdir]
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
target=${1:-all}
out=${2:-$root/build}
mkdir -p "$out/gen" "$out/obj"
NDK=${ANDROID_NDK:-/opt/android-sdk/ndk/27.2.12479018}
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
CSRCS="$(find "$here/src" -name '*.c' -not -path '*/platform/android*' -not -path '*/cxx/*' | sort) $here/src/abi/darwin_abi.S $here/cli/iosrt-run.c"
CFLAGS="-std=gnu11 -O2 -g -fsigned-char -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -I$out/gen -I$here/src/exports"
CXXINC="-nostdinc++ -isystem $here/src/cxx/include -isystem $TC/sysroot/usr/include/c++/v1"

# C++ support objects (Android toolchain) -> regenerate the std::__1 export list
"$TC/bin/clang++" --target=aarch64-linux-android29 -std=c++17 -O2 -g -fno-exceptions $CXXINC \
    -c "$here/src/cxx/cxx_string.cpp" -o "$out/obj/cxx_string.o"
"$TC/bin/clang" --target=aarch64-linux-android29 $CFLAGS -c "$here/src/cxx/cxx_abi.c" -o "$out/obj/cxx_abi.o"
python3 -I "$root/tools/gen_cxx_def.py" "$TC/bin/llvm-nm" "$here/src/exports/cxx_gen.def" "$out/obj/cxx_string.o" "$out/obj/cxx_abi.o"
python3 -I "$root/tools/gen_decls.py" "$here/src/exports" "$out/gen/decls.h"

case $target in
  linux|all)
    echo "== building linux-aarch64 (no C++ support)"
    clang --target=aarch64-linux-gnu $CFLAGS $CSRCS -o "$out/iosrt-run-linux-aarch64" -static -fuse-ld=lld -lpthread -lm ;; esac
case $target in
  android|all)
    echo "== building android-arm64"
    "$TC/bin/clang" --target=aarch64-linux-android29 $CFLAGS -DIOSRT_WITH_CXX -DIOSRT_WITH_ZLIB $CSRCS \
        "$out/obj/cxx_string.o" "$out/obj/cxx_abi.o" -o "$out/iosrt-run-android-arm64" -static -lc++_static -lc++abi -lz -lm ;; esac
