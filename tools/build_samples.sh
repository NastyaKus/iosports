#!/bin/sh
# Build the demo iOS apps (own source code) with the mini-SDK into <outdir>.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1
mkdir -p "$out"
python3 -I "$root/tools/gen_tbd.py" "$root/runtime/src/exports" "$root/sdk" >/dev/null
B="$root/sdk/build-app.sh"
F="-framework Foundation"
mkapp() { # name exe-name plist-id builder-args...
    name=$1; exe=$2; ident=$3; shift 3
    rm -rf "$out/$name.app"; mkdir -p "$out/$name.app"
    "$B" "$out/$name.app/$exe" "$@"
    cat > "$out/$name.app/Info.plist" <<PL
<?xml version="1.0" encoding="UTF-8"?>
<plist version="1.0"><dict>
<key>CFBundleExecutable</key><string>$exe</string>
<key>CFBundleIdentifier</key><string>$ident</string>
<key>CFBundleDisplayName</key><string>$name</string>
<key>UILaunchStoryboardName</key><string>LaunchScreen</string>
</dict></plist>
PL
}
mkapp "Counter (UIKit)" Counter org.iosrt.sample.counter 12.0 "$root/samples/uikit_hello/main.m" $F -framework UIKit -framework CoreGraphics
mkapp "Graphics Demo" GraphicsDemo org.iosrt.sample.graphics 12.0 "$root/samples/graphics_demo/main.m" $F -framework UIKit -framework CoreGraphics -framework QuartzCore
mkapp "NIB Demo" NibDemo org.iosrt.sample.nib 12.0 "$root/samples/nib_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tools/mknib.py" "$out/NIB Demo.app/Main.nib"
mkapp "ObjC Tour" ObjCTour org.iosrt.sample.objc 10.0 "$root/samples/objc_basics/main.m" $F
mkapp "Hello C" HelloC org.iosrt.sample.helloc 10.0 "$root/samples/hello_c/main.c"
mkapp "Sine 440 Hz (RemoteIO)" Sine org.iosrt.sample.sine 12.0 "$root/samples/audio_sine/main.m" $F -framework AudioToolbox -framework AVFoundation
mkapp "ObjC Tour (iOS14 chained)" ObjCTour14 org.iosrt.sample.objc14 14.0 --chained "$root/samples/objc_basics/main.m" $F
echo "samples -> $out"
