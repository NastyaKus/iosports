# Status — stage 1

Legend: ✅ works and is covered by automated tests · 🟡 built / implemented but
not verified on a real Android device · ❌ not implemented

## Verified (tests/run_tests.sh — 30/30 passing)

Every check runs twice: against the **linux-aarch64 (glibc)** build and the
**android-arm64 (bionic, static)** build of the runtime, executed under
`qemu-aarch64` user-mode emulation (x86_64 host, no KVM).

| Area | Status | Evidence |
|---|---|---|
| Mach-O fat/thin parsing, arm64 slice selection | ✅ | all samples; armv7-only / x86_64 / non-Mach-O correctly rejected |
| FairPlay detection | ✅ | `cryptid=1` binary rejected with a clear message |
| Segment mapping with ASLR slide, protections, I-cache flush | ✅ | all samples |
| `LC_DYLD_INFO` rebase / bind / lazy-bind | ✅ | iOS 10 builds; also 83,738 rebases + 8,957 binds of a real App Store binary (dry-run) |
| `LC_DYLD_CHAINED_FIXUPS` (iOS 14+) | ✅ | iOS 14 builds produce byte-identical output |
| Missing-API trap stubs with symbol names | ✅ | `neg_missing_api` exits 70 naming `_CFNetworkCopySystemProxySettings` |
| Darwin variadic calling convention (printf/NSLog/…) | ✅ | `hello_c`, `objc_basics` goldens |
| Callbacks host → guest (qsort, blocks, timers, render callbacks) | ✅ | samples |
| ObjC classes, inheritance, categories, protocols, `+load`, `+initialize` | ✅ | `objc_basics` |
| Messaging, `super`, forwarding target, unrecognized-selector report | ✅ | `objc_basics`, `neg_unrecognized` |
| ARC (strong/weak/autorelease, `.cxx_destruct`), blocks incl. `__block` | ✅ | `objc_basics` dealloc counts |
| Runtime API (`class_getInstanceMethod`, associated objects, …) | ✅ | `objc_basics` |
| Foundation: strings (UTF-16), numbers, arrays, dictionaries, sets, literals, fast enumeration, data, defaults, timers, run loop, main queue, notifications | ✅ | `objc_basics` golden |
| `.app` bundles with XML/binary Info.plist | ✅ | `Counter.app` test |
| UIKit-lite: `UIApplicationMain`, window/VC/views/labels/buttons, `drawRect:` via CGContext, hit-testing, target/action | ✅ | `uikit_hello` display-list golden with scripted taps |
| AudioToolbox RemoteIO render callback → audio backend | ✅ | `audio_sine`: 1.5 s, 440 Hz ± 1, amplitude 0.50 |
| Compatibility report (`--report`) | ✅ | report tests |

## Built, not device-tested

| Item | Status | Notes |
|---|---|---|
| Android APK (`org.iosrt.shell`, arm64-v8a, min API 29) | 🟡 | builds with Gradle (`android/`) and offline (`tools/build_apk_manual.sh`); signed and verified with `apksigner`. No arm64 device/emulator was available in the cloud environment. |
| JNI bridge, guest process isolation (`:guest`), log/frames/exit callbacks | 🟡 | `platform/android.c`, `RunActivity` |
| IosView rendering of the display list, touch forwarding | 🟡 | same command stream as the tested host dump |
| AAudio output backend | 🟡 | same render path as the tested WAV backend |
| IPA import (extracts `Payload/*.app`) | 🟡 | `MainActivity.importFile` |
| libc++ `std::__1 → std::__ndk1` symbol remapping | 🟡 | `rt_platform_dlsym_fallback` |
| W^X: anonymous RWX/RX mappings for guest code | 🟡 | needs the `execmem` SELinux permission app processes normally have |

## Not implemented (stage 2+)

| Item | Why it matters |
|---|---|
| ❌ C++ / ObjC **exception unwinding** (`@try`, `throw`) | needs an unwinder that understands Mach-O `__unwind_info` / `__eh_frame` |
| ❌ Thread-local variables (`__thread_vars`) | used by some apps / libraries |
| ❌ `forwardInvocation:` / `NSInvocation` / `NSMethodSignature` | proxies, some frameworks |
| ❌ Real CoreGraphics / CoreText / ImageIO / QuartzCore (`CALayer`) | almost every real app UI |
| ❌ Most of UIKit (tables, scroll views, navigation, images, gestures, text input, NIB/storyboards) | real app UIs |
| ❌ Networking (`NSURLSession`, sockets) | many apps |
| ❌ Microphone input, `AUGraph`, `ExtAudioFile`, `AudioConverter`, CoreMIDI, vDSP | audio apps such as Loopy HD |
| ❌ dylibs / embedded frameworks (`MH_DYLIB`), Swift runtime | modern apps |
| ❌ 32-bit (armv7) apps | would need CPU emulation (touchHLE's domain) |
| ❌ FairPlay-encrypted App Store binaries | DRM — only user-decrypted or self-built binaries can run |

## So what does the APK run?

* The bundled demo apps (their own source in `samples/`): a UIKit counter app,
  an Objective-C/Foundation tour, a plain C program, a RemoteIO sine generator,
  and the same ObjC tour built for iOS 14 with chained fixups.
* Any **unencrypted arm64 iOS executable** that only uses the implemented API
  surface (see `runtime/src/exports/*.def`; the `sdk/` lets you build such apps
  without Apple's SDK).
* Any other `.ipa`: the shell shows a compatibility report; running it stops at
  the first unimplemented API with its name (e.g. Loopy HD: ~31 % of imports
  available, would stop early in UIKit/CoreGraphics).
