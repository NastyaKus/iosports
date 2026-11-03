#!/bin/sh
# Build the iosrt shell APK with the raw Android SDK tools (no Gradle / no network):
# NDK clang -> libiosrt.so, javac + d8 -> classes.dex, aapt2 -> APK, zipalign + apksigner.
# usage: tools/build_apk_manual.sh [out.apk]
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
SDK=${ANDROID_HOME:-/opt/android-sdk}
BT=$SDK/build-tools/35.0.0
JAR=$SDK/platforms/android-35/android.jar
NDK=${ANDROID_NDK:-$SDK/ndk/27.2.12479018}
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
out=${1:-$root/build/iosrt-shell.apk}
W=$root/build/apk
rm -rf "$W"; mkdir -p "$W/gen" "$W/lib/arm64-v8a" "$W/classes" "$W/dex" "$W/assets"

echo "== native: libiosrt.so"
python3 -I "$root/tools/gen_decls.py" "$root/runtime/src/exports" "$W/gen/decls.h"
SRCS=$(find "$root/runtime/src" -name '*.c' -not -path '*/cxx/*' | sort)
CXXINC="-nostdinc++ -isystem $root/runtime/src/cxx/include -isystem $TC/sysroot/usr/include/c++/v1"
# Apple-ABI C++ support (std::__1 with the alternate string layout), linked with a static libc++abi
"$TC/bin/clang++" --target=aarch64-linux-android29 -fPIC -std=c++17 -O2 -g -fno-exceptions $CXXINC \
    -c "$root/runtime/src/cxx/cxx_string.cpp" -o "$W/cxx_string.o"
"$TC/bin/clang" --target=aarch64-linux-android29 -shared -fPIC -std=gnu11 -O2 -g -fsigned-char \
    -Wall -Wno-unused-parameter -Wno-missing-field-initializers -I"$W/gen" -I"$root/runtime/src/exports" \
    -DIOSRT_WITH_CXX -DIOSRT_WITH_ZLIB \
    $SRCS "$root/runtime/src/cxx/cxx_abi.c" "$W/cxx_string.o" "$root/runtime/src/abi/darwin_abi.S" -o "$W/lib/arm64-v8a/libiosrt.so" \
    -Wl,-z,max-page-size=16384 -static-libstdc++ -lc++_static -lc++abi -llog -laaudio -lz -ldl -lm
"$TC/bin/llvm-strip" --strip-debug "$W/lib/arm64-v8a/libiosrt.so"

echo "== demo apps"
sh "$root/tools/build_samples.sh" "$W/assets/samples" >/dev/null

echo "== java"
javac -source 17 -target 17 -nowarn -classpath "$JAR" -d "$W/classes" $(find "$root/android/app/src/main/java" -name '*.java')
"$BT/d8" --min-api 29 --lib "$JAR" --output "$W/dex" $(find "$W/classes" -name '*.class')

echo "== package"
sed 's/<manifest xmlns:android="http:\/\/schemas.android.com\/apk\/res\/android">/<manifest xmlns:android="http:\/\/schemas.android.com\/apk\/res\/android" package="org.iosrt" android:versionCode="2" android:versionName="0.2-stage2">/' \
    "$root/android/app/src/main/AndroidManifest.xml" > "$W/AndroidManifest.xml"
"$BT/aapt2" link -I "$JAR" --manifest "$W/AndroidManifest.xml" -A "$W/assets" \
    --min-sdk-version 29 --target-sdk-version 35 -o "$W/base.apk"
(cd "$W/dex" && zip -q -X "$W/base.apk" classes.dex)
(cd "$W" && zip -q -X -r base.apk lib)
"$BT/zipalign" -f -p 16 "$W/base.apk" "$W/aligned.apk"
KS=$root/build/debug.keystore
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass android -keypass android -alias iosrt \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=iosrt debug" >/dev/null 2>&1
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:android --key-pass pass:android --out "$out" "$W/aligned.apk"
"$BT/apksigner" verify "$out" && echo "APK: $out ($(du -h "$out" | cut -f1))"
