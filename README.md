# iosrt — experimental iOS compatibility runtime for Android arm64

iosrt loads **original iOS Mach-O arm64 executables** and runs their code
**natively** on Android (and Linux) arm64. There is no CPU emulation and no
recompilation. iosrt provides:

- its own Mach-O loader and dyld;
- its own Objective-C runtime that reads Apple's binary metadata;
- system frameworks re-implemented on top of Android (HLE): Foundation, UIKit,
  CoreGraphics, CoreText, QuartzCore, OpenGL ES with EAGL, AudioToolbox,
  AVFoundation audio, CoreMotion and more.

The result is an **Android APK**. You import an `.ipa` (or a zipped `.app`, or a
bare Mach-O) and run it.

> **Status: stage 3, phase 3b — experimental.** Self-built apps within the
> implemented API surface run fully. Real App Store apps (user-decrypted) now
> start, and several are partly usable on a phone: a 2016 OpenGL ES game
> renders with sound and touch, and a music app's UI, menus and help
> tutorials work. Swift apps, Core Data and embedded frameworks are not
> supported yet. Details: [docs/STATUS.md](docs/STATUS.md).

No Apple code and no third-party app code or resources are in this
repository. The demo apps in `samples/` are original code, built with the
included mini-SDK.

## What works

| Area | Highlights |
|---|---|
| Loading | Mach-O arm64 executables, dyld-info and chained fixups, lazy and weak binding, Objective-C classes, categories, protocols, ivar sliding, `+load`, C++ static initializers |
| ABI | Darwin arm64 conventions bridged to AAPCS64 (variadic calls, small stack arguments, `BOOL` results), `setjmp`/`longjmp`, Mach clocks |
| Objective-C | message dispatch and caches, ARC entry points, blocks, autorelease pools, associated objects, `@synchronized`, KVC |
| Foundation | strings, collections, numbers, dates, data, URLs, file manager, bundles, user defaults, run loops, timers, GCD, operation queues, notifications, plists (XML and binary), keyed archiving, `NSXMLParser`, `NSAttributedString` |
| C++ | Apple libc++ ABI (`std::__1`) on Android's libc++, plus the old GNU libstdc++ used by older apps |
| UIKit | views and layers, windows, view controllers, navigation, tab bars, tables, scroll views, controls, gestures, alerts, NIBs (NIBArchive and keyed), storyboards and segues, `UIWebView`, landscape-only apps |
| Drawing | own software rasterizer: CoreGraphics paths, gradients, images, bitmap contexts, text; CoreText lines, runs and frames; CALayer compositing and animations |
| Web pages | `UIWebView` with a built-in engine: DOM, CSS cascade (`@media`, selectors, floats, positioning, z-index) and JavaScript through QuickJS, plus the app↔page bridge (`stringByEvaluatingJavaScriptFromString:`, custom URL schemes) |
| OpenGL ES | ES 1.1/2.0/3.0 through EAGL on the device's GLES driver (EGL), the layer-backed renderbuffer and present, ABI wrappers, PVRTC decoding, Apple extensions; a software "null GL" on hosts without a GPU |
| Audio | RemoteIO and AUGraph on AAudio, AudioQueue, AVAudioPlayer, system sounds, ExtAudioFile / AudioFile / AudioConverter, file decoding (WAV/AIFF/CAF natively, AAC/MP3 through Android MediaCodec), AVAudioSession |
| Sensors | accelerometer through `UIAccelerometer` and CoreMotion (Android sensor via the NDK) |
| Tooling | static compatibility report with a verdict per app, a tracer for real apps, headless rendering to PNG, scripted touches |

What is **not** there yet: Swift apps, embedded frameworks (`MH_DYLIB`), Core
Data and sqlite, networking (apps behave as offline), the software keyboard,
multi-touch, microphone input, GLKit, OpenAL, Metal, 32-bit apps and
FairPlay-encrypted binaries. See [docs/STATUS.md](docs/STATUS.md).

## Repository layout

| Path | Contents |
|---|---|
| `runtime/src/` | the runtime (about 28 k lines of C plus AArch64 asm): `loader/`, `abi/`, `objc/`, `libsystem/`, `foundation/`, `cxx/`, `uikit/` (UIKit, CoreGraphics, CoreText, layers, web engine), `gl/` (OpenGL ES, EAGL), `audio/`, `motion/`, `platform/android.c` (JNI, AAudio), `run.c` |
| `runtime/src/exports/*.def` | the API surface: Mach-O symbol → implementation. It is the single source for the runtime's export table and the SDK's `.tbd` stubs. |
| `runtime/third_party/` | vendored permissive code: QuickJS (MIT); `runtime/src/third_party/stb` (public domain) |
| `sdk/` | mini-SDK for building test iOS apps without Xcode: headers, generated `.tbd` stubs, `build-app.sh` (`clang -target arm64-apple-iosX` + `ld64.lld`) |
| `samples/` | original demo apps: C, Objective-C, UIKit, NIB/storyboard, CoreText, web pages with JavaScript, OpenGL ES, audio files and queues, XML |
| `tests/` | automated suite: golden outputs, pixel and WAV checks, negative cases |
| `android/` | the Android shell app (Java, JNI) |
| `tools/` | generators, `build_samples.sh`, `build_apk_manual.sh` (offline APK build), `trace_app.sh` (trace a real app) |
| `docs/` | [status](docs/STATUS.md), [architecture](docs/ARCHITECTURE.md), [research and licenses](docs/RESEARCH.md), [Loopy HD requirements](docs/LOOPY-REQUIREMENTS.md) |

## Quick start (Linux host)

You need `clang`/`lld` 18 with `ld64.lld`, `qemu-user-static` (only on
non-arm64 hosts), `libc6-dev-arm64-cross`, and the Android NDK r27 for the
bionic build.

```sh
tests/run_tests.sh             # builds everything; 63 checks across the glibc and bionic runtime builds

# build and run your own iOS app
sdk/build-app.sh build/hello 12.0 samples/hello_c/main.c
qemu-aarch64-static build/iosrt-run-linux-aarch64 build/hello

# a UIKit app without a screen: scripted taps, the rendered frame as PNG
IOSRT_HEADLESS_FRAMES=6 IOSRT_SCRIPT="3:tap:187,205" IOSRT_PNG_OUT=frame.png \
  qemu-aarch64-static build/iosrt-run-linux-aarch64 build/samples/uikit_hello

# what does an arbitrary app need? (static check, no code is executed)
qemu-aarch64-static build/iosrt-run-android-arm64 --report Some.app

# run a real app headless and list the APIs it reaches that are missing
RUNTIME=build/iosrt-run-android-arm64 tools/trace_app.sh Some.app 120 "60:tap:160,300"
```

## Android APK

```sh
tools/build_apk_manual.sh build/iosrt-shell.apk   # offline: NDK, d8, aapt2, apksigner
# or: cd android && gradle assembleDebug         # needs network for the Android Gradle plugin
```

The shell lists the bundled demo apps and can import `.ipa`, zipped `.app` or
bare Mach-O files. Each app has two buttons:

- **▶ run**: starts the app in a separate process. Its log is shown under the app.
- **Compatibility report**: shows the static analysis and a verdict.

UIKit and OpenGL ES output is shown in a native view. Touches are forwarded,
audio plays through AAudio, and the accelerometer reads the phone's sensor.

## Runtime environment variables

| Variable | Effect |
|---|---|
| `IOSRT_VERBOSE=1` | loader and runtime tracing, backtraces for soft errors |
| `IOSRT_STRICT=0/1` | lenient mode (missing APIs return 0/nil; the default on Android) or strict mode (missing APIs stop the app) |
| `IOSRT_LOG_PLAIN=1` | `NSLog` without the timestamp prefix |
| `IOSRT_HEADLESS_FRAMES=n` | run the UIKit loop for n frames, then print the display list |
| `IOSRT_SCRIPT="3:tap:x,y;5:down:x,y;6:move:x,y;7:up:x,y;8:wait:ms;9:quit"` | scripted input for headless runs |
| `IOSRT_PNG_OUT=file.png` / `IOSRT_DUMP_FRAMES=1` | save the last frame / print every changed frame |
| `IOSRT_DUMP_CLASS=A,B` | list the methods of classes |
| `IOSRT_AUDIO_WAV=out.wav` | the host audio backend writes a WAV file |
| `IOSRT_GL=null` | use the software null GL even when a GPU driver is present |
| `IOSRT_ACCEL=x,y,z` | a fixed accelerometer reading (in g) |
| `IOSRT_PORTRAIT=1` / `IOSRT_NO_COMPAT_SCREEN=1` | ignore landscape-only orientations / the compatibility screen size |
| `IOSRT_HOME=dir` | sandbox home (`Documents`, `Library`, `tmp`) |

## Licenses

iosrt's own code is in this repository. The vendored components are QuickJS
(MIT, `runtime/third_party/quickjs/LICENSE`) and stb (public domain). Apple
platform headers are not used: the mini-SDK headers are written from public
documentation, and the OpenGL ES headers from the Khronos specifications.
