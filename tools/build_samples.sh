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
mkapp "Navigation Demo" NavDemo org.iosrt.sample.nav 12.0 "$root/samples/nav_demo/main.m" $F -framework UIKit -framework CoreGraphics
mkapp "Web Demo" Web org.iosrt.sample.web 12.0 "$root/samples/web_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tests/make_web_bundle.py" "$out/Web Demo.app"
mkapp "OpenGL ES Demo" GLDemo org.iosrt.sample.gl 12.0 "$root/samples/gl_demo/main.m" $F -framework UIKit -framework CoreGraphics -framework QuartzCore -framework OpenGLES -framework CoreMotion
mkapp "Web Script Demo" WebJS org.iosrt.sample.webjs 12.0 "$root/samples/webjs_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tests/make_webjs_bundle.py" "$out/Web Script Demo.app"
mkapp "Storyboard Demo" Storyboard org.iosrt.sample.storyboard 12.0 "$root/samples/storyboard_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tools/mknib.py" --storyboard "$out/Storyboard Demo.app/Main.storyboardc"
python3 -I - "$out/Storyboard Demo.app/Info.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]; d = plistlib.load(open(p, "rb")); d["UIMainStoryboardFile"] = "Main"; plistlib.dump(d, open(p, "wb"))
PY
mkapp "Old NIB Demo" KeyedNib org.iosrt.sample.keyednib 12.0 "$root/samples/keyed_nib_demo/main.m" $F -framework UIKit -framework CoreGraphics
python3 -I "$root/tools/mknib.py" --main-window "$out/Old NIB Demo.app/MainWindow.nib"
python3 -I - "$out/Old NIB Demo.app/Info.plist" <<'PY'
import plistlib, sys
p = sys.argv[1]; d = plistlib.load(open(p, "rb")); d["NSMainNibFile"] = "MainWindow"; d.pop("UILaunchStoryboardName", None); plistlib.dump(d, open(p, "wb"))
PY
mkapp "CoreText Demo" CoreTextDemo org.iosrt.sample.coretext 12.0 "$root/samples/coretext_demo/main.m" $F -framework UIKit -framework CoreGraphics -framework CoreText
mkapp "ObjC Tour" ObjCTour org.iosrt.sample.objc 10.0 "$root/samples/objc_basics/main.m" $F
mkapp "Hello C" HelloC org.iosrt.sample.helloc 10.0 "$root/samples/hello_c/main.c"
mkapp "Sine 440 Hz (RemoteIO)" Sine org.iosrt.sample.sine 12.0 "$root/samples/audio_sine/main.m" $F -framework AudioToolbox -framework AVFoundation
mkapp "Sine 440 Hz (AudioQueue)" AudioQueue org.iosrt.sample.audioqueue 12.0 "$root/samples/audioqueue_demo/main.m" $F -framework AudioToolbox
mkapp "ObjC Tour (iOS14 chained)" ObjCTour14 org.iosrt.sample.objc14 14.0 --chained "$root/samples/objc_basics/main.m" $F
echo "samples -> $out"
