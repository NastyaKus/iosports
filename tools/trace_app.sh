#!/bin/sh
# Run an iOS .app / Mach-O headless under iosrt in lenient mode and summarise
# which missing APIs it hit, in first-use order.
# usage: tools/trace_app.sh <app> [frames] [script]   (env: RUNTIME=build/iosrt-run-linux-aarch64)
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
app=$1; frames=${2:-30}; script=${3:-}
R=${RUNTIME:-$root/build/iosrt-run-linux-aarch64}
QEMU=""; [ "$(uname -m)" = "aarch64" ] || QEMU=qemu-aarch64-static
out=${TRACE_OUT:-$root/build/trace}
mkdir -p "$out"
IOSRT_STRICT=0 IOSRT_LOG_PLAIN=1 IOSRT_HEADLESS_FRAMES=$frames IOSRT_SCRIPT=$script IOSRT_DUMP_DIR=$out \
  IOSRT_HOME=$out/home timeout ${TRACE_TIMEOUT:-300} $QEMU "$R" "$app" >"$out/stdout.txt" 2>"$out/stderr.txt"
echo "exit code: $?"
echo "--- missing APIs (first use order):"
grep -o 'missing: .*' "$out/stderr.txt" | sed 's/^missing: //' | head -${TRACE_MAX:-60}
echo "--- total missing: $(grep -c 'missing: ' "$out/stderr.txt")"
echo "--- last stderr lines:"; tail -8 "$out/stderr.txt"
echo "--- stdout tail:"; tail -5 "$out/stdout.txt"
