# iosrt — experimental iOS compatibility runtime for Android arm64

iosrt loads **original iOS Mach-O arm64 executables** and runs their code
**natively** on Android/Linux arm64. It provides its own Mach-O loader and dyld,
its own Objective-C runtime that understands Apple's binary metadata, and
high-level-emulated (HLE) system frameworks implemented on top of Android:
Foundation, a UIKit subset, CoreGraphics drawing and AudioToolbox RemoteIO.

This is **stage 1** of a research project. It runs self-built iOS apps that stay
within the implemented API surface. Real App Store apps load and link, but stop
at the first missing framework API. See [docs/STATUS.md](docs/STATUS.md).

No Apple code and no third-party app code or resources are in this
repository. The demo apps in `samples/` are original code built with the
included mini-SDK.

## Repository layout

| Path | Contents |
|---|---|
| `runtime/` | the runtime, about 7.4 k lines of C plus AArch64 asm: `loader/`, `abi/`, `objc/`, `libsystem/`, `foundation/`, `uikit/`, `audio/`, `platform/android.c` (JNI, AAudio), `run.c`, `cli/iosrt-run.c` |
| `runtime/src/exports/*.def` | the API surface: Mach-O symbol → implementation. It is the single source for the runtime table and the SDK stubs. |
| `sdk/` | mini-SDK for building test iOS apps without Xcode: headers, generated `.tbd` stubs, `build-app.sh` (`clang -target arm64-apple-iosX` + `ld64.lld`) |
| `samples/` | demo iOS apps: C, Objective-C/Foundation, UIKit, RemoteIO audio |
| `tests/` | automated test suite: goldens, WAV analysis, negative cases |
| `android/` | Android shell app (Gradle, Java, CMake) |
| `tools/` | generators (`gen_tbd.py`, `gen_decls.py`), `build_samples.sh`, `build_apk_manual.sh` (offline APK build) |
| `docs/` | [research & licenses](docs/RESEARCH.md), [architecture](docs/ARCHITECTURE.md), [Loopy HD requirements](docs/LOOPY-REQUIREMENTS.md), [status](docs/STATUS.md) |

## Quick start (Linux host)

The runtime needs these tools: `clang`/`lld` 18 with `ld64.lld`, `qemu-user-static` (only on non-arm64 hosts), `libc6-dev-arm64-cross`, and the Android NDK r27 for the bionic build.

```sh
tests/run_tests.sh                       # builds everything and runs 15 checks for each of the 2 runtime builds

# build and run your own iOS app
sdk/build-app.sh build/hello 12.0 samples/hello_c/main.c
qemu-aarch64-static build/iosrt-run-linux-aarch64 build/hello

# UIKit app without a screen: dump the rendered display list after scripted taps
IOSRT_HEADLESS_FRAMES=6 IOSRT_SCRIPT="3:tap:187,205" \
  qemu-aarch64-static build/iosrt-run-linux-aarch64 build/samples/uikit_hello

# what does an arbitrary app need? (static check, no code is executed)
qemu-aarch64-static build/iosrt-run-linux-aarch64 --report Some.app
```

## Android APK

```sh
cd android && gradle assembleDebug        # needs network for the Android Gradle plugin
# or fully offline, using SDK/NDK tools only:
tools/build_apk_manual.sh build/iosrt-shell.apk
```

The shell lists the bundled demo apps and can import `.ipa`, zipped `.app` or bare Mach-O files. Each app gets two buttons:

- **▶ run**: starts the app in a separate `:guest` process.
- **Compatibility report**: shows the static analysis.

UIKit output is drawn by a native view, touches are forwarded, and RemoteIO audio plays through AAudio.

## Runtime environment variables

| Variable | Effect |
|---|---|
| `IOSRT_VERBOSE=1` | loader and runtime tracing |
| `IOSRT_LOG_PLAIN=1` | `NSLog` without the timestamp prefix |
| `IOSRT_HEADLESS_FRAMES=n` | run the UIKit loop for n frames, then print the display list |
| `IOSRT_SCRIPT="3:tap:x,y;9:quit"` | scripted input for headless runs |
| `IOSRT_DUMP_FRAMES=1` | print every changed frame |
| `IOSRT_AUDIO_WAV=out.wav` | host audio backend writes a WAV file |
| `IOSRT_HOME=dir` | sandbox home (`Documents`, `Library`, `tmp`) |
