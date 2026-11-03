#!/bin/sh
# Build an iOS arm64 Mach-O against the iosrt mini-SDK (no Apple SDK needed).
# usage: sdk/build-app.sh <out> <min-ios> [--chained] <sources...> [-framework X ...]
set -eu
sdk=$(cd "$(dirname "$0")" && pwd)
out=$1; ver=$2; shift 2
chained=""
if [ "${1:-}" = "--chained" ]; then chained="-fixup_chains"; shift; fi
LD=${LD64:-/usr/lib/llvm-18/bin/ld64.lld}
tmp=$(mktemp -d)
objs=""; fw=""
while [ $# -gt 0 ]; do
  case $1 in
    -framework) fw="$fw -framework $2"; shift 2 ;;
    *.c|*.m)
      o="$tmp/$(basename "$1").o"
      clang -target arm64-apple-ios$ver -nostdlibinc -isystem "$sdk/include" \
            -fobjc-arc -fblocks -fobjc-runtime=ios-$ver -O1 -g0 -Wall -c "$1" -o "$o"
      objs="$objs $o"; shift ;;
    *) echo "unknown arg $1" >&2; exit 2 ;;
  esac
done
$LD -arch arm64 -platform_version ios $ver $ver -syslibroot "$sdk" -F"$sdk/System/Library/Frameworks" \
    -L"$sdk/usr/lib" -lSystem -lobjc -framework CoreFoundation $fw $chained $objs -o "$out"
rm -rf "$tmp"
