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
app "$S/graphics_demo" 12.0 "$root/samples/graphics_demo/main.m" $F -framework UIKit -framework CoreGraphics -framework QuartzCore
rm -rf "$S/NibDemo.app"; mkdir -p "$S/NibDemo.app"
app "$S/NibDemo.app/NibDemo" 12.0 "$root/samples/nib_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tools/mknib.py" "$S/NibDemo.app/Main.nib"
cat > "$S/NibDemo.app/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>NibDemo</string>
<key>CFBundleIdentifier</key><string>org.iosrt.nibdemo</string><key>UILaunchStoryboardName</key><string>LaunchScreen</string></dict></plist>
PL
app "$S/nav_demo" 12.0 "$root/samples/nav_demo/main.m" $F -framework UIKit -framework CoreGraphics
rm -rf "$S/Web.app"; mkdir -p "$S/Web.app"
app "$S/Web.app/Web" 12.0 "$root/samples/web_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tests/make_web_bundle.py" "$S/Web.app"
app "$S/sys_demo" 12.0 "$root/samples/sys_demo/main.m" $F
rm -rf "$S/Archive.app"; mkdir -p "$S/Archive.app"
app "$S/Archive.app/Archive" 12.0 "$root/samples/archive_demo/main.m" $F
python3 -I "$root/tests/make_archive_fixture.py" "$S/Archive.app"
rm -rf "$S/KeyedNib.app"; mkdir -p "$S/KeyedNib.app"
app "$S/KeyedNib.app/KeyedNib" 12.0 "$root/samples/keyed_nib_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tools/mknib.py" --main-window "$S/KeyedNib.app/MainWindow.nib"
cat > "$S/KeyedNib.app/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>KeyedNib</string>
<key>CFBundleIdentifier</key><string>org.iosrt.keyednib</string><key>NSMainNibFile</key><string>MainWindow</string></dict></plist>
PL
rm -rf "$S/Storyboard.app"; mkdir -p "$S/Storyboard.app"
app "$S/Storyboard.app/Storyboard" 12.0 "$root/samples/storyboard_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tools/mknib.py" --storyboard "$S/Storyboard.app/Main.storyboardc"
cat > "$S/Storyboard.app/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict><key>CFBundleExecutable</key><string>Storyboard</string>
<key>CFBundleIdentifier</key><string>org.iosrt.storyboard</string><key>UIMainStoryboardFile</key><string>Main</string>
<key>UILaunchStoryboardName</key><string>LaunchScreen</string></dict></plist>
PL
app "$S/coretext_demo" 12.0 "$root/samples/coretext_demo/main.m" $F -framework UIKit -framework CoreGraphics -framework CoreText
app "$S/gl_demo" 12.0 "$root/samples/gl_demo/main.m" $F -framework UIKit -framework CoreGraphics -framework QuartzCore -framework OpenGLES -framework CoreMotion
app "$S/audioqueue_demo" 12.0 "$root/samples/audioqueue_demo/main.m" $F -framework AudioToolbox
rm -rf "$S/WebJS.app"; mkdir -p "$S/WebJS.app"
app "$S/WebJS.app/WebJS" 12.0 "$root/samples/webjs_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tests/make_webjs_bundle.py" "$S/WebJS.app"
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
  # 3a. NIB loading: outlets, actions, custom class, awakeFromNib (NIB generated by tools/mknib.py)
  IOSRT_HEADLESS_FRAMES=8 IOSRT_SCRIPT="4:tap:160,185;6:tap:160,185" $R "$S/NibDemo.app" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen ' "$T/out.txt" >"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/nib_demo.txt" "$T/events.txt" >"$T/diff.txt"; then ok "nib_demo (NIBArchive: outlets, actions, class swapping, awakeFromNib)"; else ko "nib_demo rc=$rc" "$T/diff.txt"; fi
  # 3b. graphics stack: drawing, layers, paging scroll view, gestures, controls, alert controller
  IOSRT_HEADLESS_FRAMES=30 IOSRT_SCRIPT="3:drag:300,400,40,400;8:tap:100,180;12:drag:121,295,320,295;16:tap:65,355;20:tap:187,422;25:tap:187,360" \
    $R "$S/graphics_demo" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen \|Gradient stops\|^clear$\|(transformed)' "$T/out.txt" >"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/graphics_demo.txt" "$T/events.txt" >"$T/diff.txt" && grep -q '^image ' "$T/out.txt"; then ok "graphics_demo (CG/CA drawing, scroll paging, gestures, controls, alert)"; else ko "graphics_demo rc=$rc" "$T/diff.txt"; fi
  # 3c. navigation in a custom container, UITableViewController, section footers
  IOSRT_HEADLESS_FRAMES=18 IOSRT_SCRIPT="3:down:187,145;3:move:188,146;3:move:188,145;3:up:188,146;6:tap:187,77;9:tap:187,145;12:tap:187,121;15:tap:187,145" \
    $R "$S/nav_demo" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen [0-9]' "$T/out.txt" >"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/nav_demo.txt" "$T/events.txt" >"$T/diff.txt" && grep -q 'Footer text explaining' "$T/out.txt"; then ok "nav_demo (custom nav container, UITableViewController, section footers)"; else ko "nav_demo rc=$rc" "$T/diff.txt"; fi
  # 3d. UIWebView with bundled pages: CSS subset, links via the delegate, unknown schemes, goBack
  IOSRT_HEADLESS_FRAMES=14 IOSRT_SCRIPT="3:tap:80,150;5:tap:80,196;7:tap:80,104" $R "$S/Web.app" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/web_demo.txt" "$T/out.txt" >"$T/diff.txt"; then ok "web_demo (bundled HTML/CSS pages, link navigation, back list)"; else ko "web_demo rc=$rc" "$T/diff.txt"; fi
  # 3e. system odds and ends: setjmp/longjmp, Mach clocks, BSD hash tables, URL file APIs
  $R "$S/sys_demo" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/sys_demo.txt" "$T/out.txt" >"$T/diff.txt"; then ok "sys_demo (setjmp/longjmp, clock services, hash tables, URL file APIs)"; else ko "sys_demo rc=$rc" "$T/diff.txt"; fi
  # 3f. NSKeyedArchiver/Unarchiver: round trips, a foreign archive, our binary plist read back by plistlib
  rm -f "$T/home/tmp/roundtrip.archive"
  $R "$S/Archive.app" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/archive_demo.txt" "$T/out.txt" >"$T/diff.txt" && \
     python3 -I -c 'import plistlib,sys; a=plistlib.load(open(sys.argv[1],"rb")); assert a["$archiver"]=="NSKeyedArchiver" and len(a["$objects"])>10' "$T/home/tmp/roundtrip.archive" 2>>"$T/diff.txt"; then
    ok "archive_demo (keyed archives: object graphs, cycles, class fallbacks, bplist interop)"; else ko "archive_demo rc=$rc" "$T/diff.txt"; fi
  # 3g. pre-storyboard launch: NSMainNibFile in the old keyed-archive nib format
  IOSRT_HEADLESS_FRAMES=8 IOSRT_SCRIPT="4:tap:160,222;6:tap:160,222" $R "$S/KeyedNib.app" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen ' "$T/out.txt" >"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/keyed_nib_demo.txt" "$T/events.txt" >"$T/diff.txt"; then ok "keyed_nib_demo (NSMainNibFile, keyed-archive nib, delegate from the nib)"; else ko "keyed_nib_demo rc=$rc" "$T/diff.txt"; fi
  # 3h. storyboards: UIMainStoryboardFile, scenes, lazily loaded views, embed and show segues
  IOSRT_HEADLESS_FRAMES=10 IOSRT_SCRIPT="4:tap:120,222" $R "$S/Storyboard.app" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen ' "$T/out.txt" >"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/storyboard_demo.txt" "$T/events.txt" >"$T/diff.txt"; then ok "storyboard_demo (main storyboard, scenes, embed/show segues, prepareForSegue)"; else ko "storyboard_demo rc=$rc" "$T/diff.txt"; fi
  # 3i. CoreText: lines, runs, glyphs, truncation, framesetter (drawn runs are in the frame trace)
  IOSRT_HEADLESS_FRAMES=3 $R "$S/coretext_demo" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen ' "$T/out.txt" >"$T/events.txt"
  grep '^ctrun ' "$T/out.txt" >>"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/coretext_demo.txt" "$T/events.txt" >"$T/diff.txt"; then ok "coretext_demo (CTLine/CTRun/CTFramesetter, attributed runs)"; else ko "coretext_demo rc=$rc" "$T/diff.txt"; fi
  # 3j. scripted page: JS steps through sections, hit tests via elementFromPoint, iframe command bridge, timers
  IOSRT_HEADLESS_FRAMES=16 IOSRT_SCRIPT="4:tap:293,395;6:tap:75,395;8:tap:293,395;10:tap:293,395;12:tap:336,20" $R "$S/WebJS.app" >"$T/out.txt" 2>&1; rc=$?
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen ' "$T/out.txt" >"$T/events.txt"
  if [ $rc = 0 ] && diff -u "$root/tests/golden/webjs_demo.txt" "$T/events.txt" >"$T/diff.txt"; then ok "webjs_demo (JavaScript + DOM: click handlers, elementFromPoint, iframe bridge, timers)"; else ko "webjs_demo rc=$rc" "$T/diff.txt"; fi
  # 3k. OpenGL ES: EAGL contexts (ES 2, ES 1 + OES FBOs, sharegroups), layer-backed renderbuffer, present into the layer (null GL)
  rm -f "$T/gl.png"
  IOSRT_ACCEL=0.12,-0.25,-0.96 IOSRT_HEADLESS_FRAMES=8 IOSRT_PNG_OUT="$T/gl.png" $R "$S/gl_demo" >"$T/out.txt" 2>&1; rc=$?
  # accelerometer deliveries are timer-paced: checked apart from the ordered events
  grep -v '^[a-zA-Z0-9]* [-0-9.]*,[-0-9.]* \|^screen \|^UIAccelerometer\|^CoreMotion' "$T/out.txt" >"$T/events.txt"
  grep -q '^UIAccelerometer: shared 1, 0.12 -0.25 -0.96' "$T/out.txt" && grep -q '^CoreMotion: available 1, acceleration 0.12 -0.25 -0.96' "$T/out.txt" || rc=99
  # the presented frame: blue, with the scissored red rectangle at the bottom left (GL's origin)
  px=$(python3 -I -c "
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB'); w, h = im.size
print(im.getpixel((10, 10)), im.getpixel((10, h - 10)), im.getpixel((w - 10, h - 10)))" "$T/gl.png" 2>/dev/null)
  if [ $rc = 0 ] && diff -u "$root/tests/golden/gl_demo.txt" "$T/events.txt" >"$T/diff.txt" && grep -q '^image 0,0 375x667 px=750x1334' "$T/out.txt" && [ "$px" = "(26, 51, 204) (230, 26, 26) (26, 51, 204)" ]; then ok "gl_demo (EAGL + renderbuffer from CAEAGLLayer + present, ES 1/2 contexts, accelerometer)"; else echo "       pixels: $px"; ko "gl_demo rc=$rc" "$T/diff.txt"; fi
  # 4. AudioToolbox RemoteIO -> WAV backend
  rm -f "$T/sine.wav"
  IOSRT_AUDIO_WAV=$T/sine.wav $R "$S/audio_sine" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && grep -q "many render callbacks" "$T/out.txt" && python3 -I "$root/tests/check_wav.py" "$T/sine.wav" >"$T/wav.txt"; then ok "audio_sine ($(cat "$T/wav.txt"))"; else ko "audio_sine rc=$rc" "$T/out.txt"; fi
  # 4a. AudioQueue output: primed buffers refilled from the callback -> WAV backend
  rm -f "$T/aq.wav"
  IOSRT_AUDIO_WAV=$T/aq.wav $R "$S/audioqueue_demo" >"$T/out.txt" 2>&1; rc=$?
  if [ $rc = 0 ] && diff -u "$root/tests/golden/audioqueue_demo.txt" "$T/out.txt" >"$T/diff.txt" && python3 -I "$root/tests/check_wav.py" "$T/aq.wav" >"$T/wav.txt"; then ok "audioqueue_demo ($(cat "$T/wav.txt"))"; else ko "audioqueue_demo rc=$rc" "$T/diff.txt"; fi
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
