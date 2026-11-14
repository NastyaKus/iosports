#!/bin/sh
# Build libc++ (runtime/third_party/libcxx, LLVM 18.1) with Apple's arm64 ABI
# configuration (runtime/src/cxx/include: std::__1, alternate string layout,
# Darwin-sized threading objects) into <outdir>/libcxx_apple.a.
# Guest code linked against Apple's libc++.dylib gets these std::__1 symbols.
# usage: tools/build_libcxx.sh <ndk-toolchain-dir> <outdir>
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
TC=$1; out=$2
SRC="$root/runtime/third_party/libcxx/src"
mkdir -p "$out/libcxx" "$out/libcxx_inc"
# <__locale> and <regex> with Apple's ctype masks (tools/patch_libcxx_headers.py): guest code
# reads ctype<char>'s table inline as 32-bit masks with Darwin's _CTYPE_* bits
python3 -I "$root/tools/patch_libcxx_headers.py" "$TC/sysroot/usr/include/c++/v1" "$out/libcxx_inc"
FLAGS="--target=aarch64-linux-android29 -std=c++23 -O2 -g -fPIC -fno-exceptions -fvisibility-inlines-hidden -w
  -nostdinc++ -include $root/runtime/src/cxx/libcxx_compat.h -isystem $root/runtime/src/cxx/include -isystem $out/libcxx_inc
  -isystem $TC/sysroot/usr/include/c++/v1 -I$SRC -D_LIBCPP_BUILDING_LIBRARY -D_LIBCPP_DISABLE_NEW_DELETE_DEFINITIONS"
# not built: new/exception/stdexcept/typeinfo (namespace std, from the NDK's libc++abi),
# verbose_abort (runtime/src/cxx/cxx_string.cpp), random (Apple's random_device is stateless,
# runtime/src/cxx/cxx_abi.c), filesystem, tz, experimental
FILES="algorithm any atomic barrier bind call_once charconv chrono condition_variable condition_variable_destructor
  error_category functional future hash ios ios.instantiations iostream locale memory memory_resource mutex
  mutex_destructor optional ostream print random_shuffle regex shared_mutex string strstream system_error
  thread valarray variant vector fstream legacy_pointer_safety ryu/d2fixed ryu/d2s ryu/f2s"
objs=""; changed=0
for f in $FILES; do
    o="$out/libcxx/$(echo "$f" | tr / _).o"
    if [ ! -f "$o" ] || [ "$SRC/$f.cpp" -nt "$o" ] || [ "$root/runtime/src/cxx/include/__config_site" -nt "$o" ] || \
       [ "$root/runtime/src/cxx/include/__external_threading" -nt "$o" ] || [ "$root/runtime/src/cxx/include/bits/mbstate_t.h" -nt "$o" ] || [ "$out/libcxx_inc/__locale" -nt "$o" ] || [ "$out/libcxx_inc/regex" -nt "$o" ]; then
        "$TC/bin/clang++" $FLAGS -c "$SRC/$f.cpp" -o "$o"; changed=1
    fi
    objs="$objs $o"
done
if [ $changed = 1 ] || [ ! -f "$out/libcxx_apple.a" ]; then
    rm -f "$out/libcxx_apple.a"
    "$TC/bin/llvm-ar" rcs "$out/libcxx_apple.a" $objs
fi
