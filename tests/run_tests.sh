#!/bin/sh
# iosrt test-suite: builds the runtime (glibc + bionic), the mini-SDK and the
# sample apps, then runs everything under qemu-aarch64 (or natively on arm64).
set -u
root=$(cd "$(dirname "$0")/.." && pwd)
B=$root/build; S=$B/samples; T=$B/tests
mkdir -p "$S" "$T"
QEMU=""
[ "$(uname -m)" = "aarch64" ] || QEMU=${QEMU_AARCH64:-qemu-aarch64-static}
pass=0; fail=0
ok() { pass=$((pass + 1)); echo "  PASS $1"; }
ko() { fail=$((fail + 1)); echo "  FAIL $1"; [ -n "${2:-}" ] && sed 's/^/       /' "$2" | head -20; }

echo "== build"
python3 -I "$root/tools/gen_tbd.py" "$root/runtime/src/exports" "$root/sdk" >/dev/null || exit 1
"$root/runtime/build.sh" all "$B" >/dev/null || { echo "runtime build failed"; exit 1; }
app() { "$root/sdk/build-app.sh" "$@" 2>"$T/build.log" || { cat "$T/build.log"; exit 1; }; }
F="-framework Foundation"
app "$S/hello_c" 10.0 "$root/samples/hello_c/main.c"
app "$S/hello_c.chained" 14.0 --chained "$root/samples/hello_c/main.c"
app "$S/objc_basics" 10.0 "$root/samples/objc_basics/main.m" $F
app "$S/objc_basics.chained" 14.0 --chained "$root/samples/objc_basics/main.m" $F
app "$S/uikit_hello" 12.0 "$root/samples/uikit_hello/main.m" $F -framework UIKit -framework CoreGraphics
app "$S/audio_sine" 12.0 "$root/samples/audio_sine/main.m" $F -framework AudioToolbox -framework AVFoundation
app "$S/cxx_basics" 12.0 "$root/samples/cxx_basics/main.mm" $F
app "$S/neg_unrecognized" 12.0 "$root/tests/negative/unrecognized.m" $F
# link against an API the SDK does not have: flat lookup at runtime -> trap
clang -target arm64-apple-ios12.0 -nostdlibinc -isystem "$root/sdk/include" -c "$root/tests/negative/missing_api.c" -o "$T/missing.o"
/usr/lib/llvm-18/bin/ld64.lld -arch arm64 -platform_version ios 12.0 12.0 -syslibroot "$root/sdk" -L"$root/sdk/usr/lib" \
    -lSystem -undefined dynamic_lookup "$T/missing.o" -o "$S/neg_missing_api" 2>/dev/null
python3 -I "$root/tests/make_negative.py" "$S/hello_c" "$T"
# .app bundle with Info.plist
rm -rf "$S/Counter.app"; mkdir -p "$S/Counter.app"; cp "$S/uikit_hello" "$S/Counter.app/Counter"
cat > "$S/Counter.app/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>Counter</string>
<key>CFBundleIdentifier</key><string>org.iosrt.counter</string></dict></plist>
PL

for flavor in linux-aarch64 android-arm64; do
  R="$QEMU $B/iosrt-run-$flavor"
  echo "== runtime: $flavor"
  export IOSRT_LOG_PLAIN=1 IOSRT_HOME=$T/home
  # 1. plain C, Darwin variadics, rebases, callbacks
  $R "$S/hello_c" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 7 ] && diff -u "$root/tests/golden/hello_c.txt" "$T/out.txt" >"$T/diff.txt"; then ok "hello_c (dyld-info fixups)"; else ko "hello_c rc=$rc" "$T/diff.txt"; fi
  $R "$S/hello_c.chained" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 7 ] && diff -u "$root/tests/golden/hello_c.txt" "$T/out.txt" >"$T/diff.txt"; then ok "hello_c (chained fixups)"; else ko "hello_c.chained rc=$rc" "$T/diff.txt"; fi
  # 2. Objective-C runtime + Foundation
  $R "$S/objc_basics" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/objc_basics.txt" "$T/out.txt" >"$T/diff.txt"; then ok "objc_basics (dyld-info fixups)"; else ko "objc_basics rc=$rc" "$T/diff.txt"; fi
  $R "$S/objc_basics.chained" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/objc_basics.txt" "$T/out.txt" >"$T/diff.txt"; then ok "objc_basics (chained fixups)"; else ko "objc_basics.chained rc=$rc" "$T/diff.txt"; fi
  # 3. UIKit-lite: launch, layout, scripted taps, display list
  IOSRT_HEADLESS_FRAMES=6 IOSRT_SCRIPT="3:tap:187,205;4:tap:187,205;5:tap:10,600" $R "$S/uikit_hello" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/uikit_hello.txt" "$T/out.txt" >"$T/diff.txt"; then ok "uikit_hello (rendered frame trace + touches)"; else ko "uikit_hello rc=$rc" "$T/diff.txt"; fi
  IOSRT_HEADLESS_FRAMES=2 $R "$S/Counter.app" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && grep -q '"taps: 0' "$T/out.txt"; then ok ".app bundle via Info.plist"; else ko ".app bundle rc=$rc" "$T/out.txt"; fi
  # no launch storyboard / launch images: iOS compatibility screen (320x480, scaled up)
  if grep -q 'launched, window {{0, 0}, {320, 480}}' "$T/out.txt"; then ok "compatibility screen for legacy bundles"; else ko "compatibility screen" "$T/out.txt"; fi
  # 4. AudioToolbox RemoteIO -> WAV backend
  rm -f "$T/sine.wav"
  IOSRT_AUDIO_WAV=$T/sine.wav $R "$S/audio_sine" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && grep -q "many render callbacks" "$T/out.txt" && python3 -I "$root/tests/check_wav.py" "$T/sine.wav" >"$T/wav.txt"; then ok "audio_sine ($(cat "$T/wav.txt"))"; else ko "audio_sine rc=$rc" "$T/out.txt"; fi
  # 4b. C++ on the Apple libc++ ABI (only the Android build links the C++ support)
  if [ $flavor = android-arm64 ]; then
    $R "$S/cxx_basics" >"$T/out.txt" 2>&1; rc=$?
    if [ $rc = 0 ] && diff -u "$root/tests/golden/cxx_basics.txt" "$T/out.txt" >"$T/diff.txt"; then ok "cxx_basics (std::__1 ABI, threads, RTTI)"; else ko "cxx_basics rc=$rc" "$T/diff.txt"; fi
  fi
  # 5. negative cases
  check_reject() { # file pattern label
    $R "$1" >"$T/out.txt" 2>&1; rc=$?
    if [ $rc != 0 ] && grep -q "$2" "$T/out.txt"; then ok "rejects $3"; else ko "$3 rc=$rc" "$T/out.txt"; fi
  }
  check_reject "$T/encrypted" "FairPlay-encrypted" "FairPlay-encrypted binary"
  check_reject "$T/x86_64" "need arm64" "x86_64 binary"
  check_reject "$T/armv7_only" "no arm64 slice" "armv7-only fat binary"
  check_reject "$T/garbage" "not a Mach-O" "non-Mach-O file"
  $R "$S/neg_missing_api" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 70 ] && grep -q "unimplemented function _CFNetworkCopySystemProxySettings" "$T/out.txt" && grep -q "before missing call" "$T/out.txt"; then ok "missing API traps with its name"; else ko "missing API rc=$rc" "$T/out.txt"; fi
  $R "$S/neg_unrecognized" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 71 ] && grep -q "noSuchMethod\]: unrecognized selector" "$T/out.txt"; then ok "unrecognized selector reported"; else ko "unrecognized selector rc=$rc" "$T/out.txt"; fi
  # 6. compatibility report
  $R --report "$S/objc_basics" >"$T/out.txt" 2>&1
  if grep -q "missing: 0" "$T/out.txt" && grep -q "objc classes: 4, categories: 2" "$T/out.txt"; then ok "--report on a fully supported app"; else ko "--report" "$T/out.txt"; fi
  $R --report "$S/neg_missing_api" >"$T/out.txt" 2>&1
  if grep -q "missing: 1" "$T/out.txt" && grep -q "_CFNetworkCopySystemProxySettings" "$T/out.txt"; then ok "--report lists missing symbols"; else ko "--report missing" "$T/out.txt"; fi
done
echo "== $pass passed, $fail failed"
[ $fail = 0 ]
