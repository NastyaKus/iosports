#!/bin/sh
# Build samples/dylib_demo as an app bundle with an embedded framework, a dylib
# and a plug-in bundle: <out>/DylibDemo.app
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
out=$1; ver=${2:-12.0}; chained=${3:-}
B="$root/sdk/build-app.sh"; src="$root/samples/dylib_demo"
app="$out/DylibDemo.app"
rm -rf "$app"; mkdir -p "$app/Frameworks/DemoKit.framework" "$app/PlugIns/Demo.bundle"
"$B" "$app/Frameworks/libhelper.dylib" $ver $chained --dylib @rpath/libhelper.dylib "$src/helper.c"
"$B" "$app/Frameworks/DemoKit.framework/DemoKit" $ver $chained --dylib @rpath/DemoKit.framework/DemoKit \
    "$src/demokit.m" "$src/demokit_cxx.cpp" -framework Foundation -rpath @loader_path/.. "$app/Frameworks/libhelper.dylib"
"$B" "$app/PlugIns/Demo.bundle/Demo" $ver $chained --bundle "$src/plugin.m" -framework Foundation
"$B" "$app/DylibDemo" $ver $chained "$src/main.m" "$src/main_cxx.cpp" -framework Foundation \
    -rpath @executable_path/Frameworks "$app/Frameworks/DemoKit.framework/DemoKit"
printf 'Hello from DemoKit' > "$app/Frameworks/DemoKit.framework/greeting.txt"
plist() { # file key/value pairs...
    f=$1; shift
    { echo '<?xml version="1.0" encoding="UTF-8"?>'; echo '<plist version="1.0"><dict>'
      while [ $# -gt 0 ]; do echo "<key>$1</key><string>$2</string>"; shift 2; done
      echo '</dict></plist>'; } > "$f"
}
plist "$app/Info.plist" CFBundleExecutable DylibDemo CFBundleIdentifier org.iosrt.sample.dylib CFBundleName DylibDemo
plist "$app/Frameworks/DemoKit.framework/Info.plist" CFBundleExecutable DemoKit CFBundleIdentifier org.iosrt.demokit CFBundleName DemoKit
plist "$app/PlugIns/Demo.bundle/Info.plist" CFBundleExecutable Demo CFBundleIdentifier org.iosrt.demoplugin NSPrincipalClass DemoPlugin
