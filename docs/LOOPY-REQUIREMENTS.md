# Loopy HD as a reference target: what a real iOS app needs

Loopy HD 1.6.3 was used **only as a measuring stick** for future compatibility
work. Nothing from the app (code, disassembly, resources) is part of this
repository. The figures below are aggregate counts produced locally by
`iosrt-run --report` (dry-run: the binary is mapped and fixed up, but no guest
code is executed) plus `llvm-objdump` section statistics.

Note: the supplied IPA is a cracked copy (FairPlay already removed). iosrt would
refuse an App Store-encrypted binary (`cryptid != 0`) — running encrypted apps
requires a legitimately decrypted binary from the user's own device.

## Binary facts

| Property | Value |
|---|---|
| Format | fat Mach-O: armv7 + arm64 (arm64 slice used) |
| Minimum iOS / SDK | 10.0 / 10.2 |
| Fixups | `LC_DYLD_INFO_ONLY` — **all 83,738 rebases and 8,957 binds were applied successfully** by the iosrt loader |
| Linked libraries | 28 |
| Objective-C | 656 classes, 31 categories |
| Static initialisers | 36 (`__mod_init_func`) |
| C++ exceptions | yes (`__gcc_except_tab`, `__unwind_info`) |
| Thread-local variables | none |
| Raw `svc` syscalls / direct TLS register access | none |
| UI resources | 16 compiled NIB files (needs NIB loading) |

## Import coverage with iosrt stage 1

**1,209 imported symbols, 372 provided by iosrt (30.8 %), 837 missing**
(543 functions, 153 data symbols, 141 class symbols).

| Library | Missing | Typical missing APIs (public API names) |
|---|---:|---|
| UIKit | 111 | 58 classes: table/scroll/navigation/tab controllers, image views, gesture recognisers, sliders, switches, alerts, activity view controller, web view, pasteboard |
| libSystem | 110 | Mach ports & exception ports (crash reporter), dyld image APIs, BSD sockets, `kqueue`, dispatch sources/barriers/specifics, rwlocks, CommonCrypto (`CC_MD5`, `CC_SHA1`, `CCCrypt`) |
| Foundation | 84 | 49 classes: `NSURL*`, `NSURLSession`, `NSOperationQueue`, `NSInvocation`/`NSMethodSignature`, `NSKeyedArchiver`, `NSJSONSerialization`, `NSRegularExpression`, `NSDateFormatter`, `NSCalendar`, `NSLock`, `NSValue`, `NSIndexPath`, attributed strings |
| CoreGraphics | 82 | paths, bitmap contexts, images, gradients, affine transforms, colour spaces |
| libc++ | 82 | `std::__1` containers/strings/streams — planned via the std::__1 → std::__ndk1 remapping (implemented in `platform/android.c`, untested) |
| CoreFoundation | 75 | CFString/CFArray/CFDictionary families, CFURL, CFRunLoop sources, CFPreferences |
| AudioToolbox | 40 | `AUGraph*`, `ExtAudioFile*`, `AudioConverter*`, `AudioFile*`, `MusicDeviceMIDIEvent`, Inter-App Audio publishing |
| Security | 35 | keychain (`SecItem*`), certificates |
| CoreText | 34 | text layout (framesetter, lines, runs, fonts) |
| Accelerate | 31 | vDSP vector maths, vImage |
| QuartzCore | 30 | `CALayer`, `CAShapeLayer`, `CADisplayLink`, `CATransaction`, animations |
| CoreMIDI | 29 | MIDI clients/ports/endpoints, network MIDI |
| libsqlite3 | 23 | SQLite (straightforward: bundle sqlite) |
| AVFoundation | 11 | asset export/composition, `AVAudioPlayer` |
| ImageIO, AddressBook, libz, SystemConfiguration, CoreMedia, MobileCoreServices, StoreKit, CoreLocation, MessageUI, AddressBookUI | 1–12 each | mostly peripheral features (sharing, purchases, contacts) |
| libobjc | 5 | `_objc_msgForward` (needs NSInvocation forwarding), property introspection |

## What it would take (estimate)

| Phase | Content | Rough size |
|---|---|---|
| 2. Foundation completeness | the 49 missing classes, NSInvocation-based forwarding, keyed archiving | ~8–12 k LOC |
| 3. Graphics | CoreGraphics + CoreText + ImageIO on a real 2D renderer, QuartzCore layer tree + compositor, UIImage | ~20–30 k LOC (or Skia integration) |
| 4. UIKit | the 58 missing classes, NIB loading, gesture recognisers, text input, view-controller containment | ~25–40 k LOC |
| 5. Audio | full-duplex RemoteIO (mic), AUGraph + mixer/converter units, ExtAudioFile/AudioConverter (AAC via AMediaCodec), vDSP, CoreMIDI → AMidi | ~8–12 k LOC |
| 6. System | C++ exception unwinding for Mach-O (`__unwind_info` + `__eh_frame`), libc++ bridging, sockets, Mach-port stubs, CommonCrypto, sqlite, keychain emulation | ~6–10 k LOC |

Inter-App Audio, Audiobus and other iOS-only inter-process audio features have
no Android equivalent and would be stubbed.

**Verdict:** loading and linking a real 64-bit App Store app on Android arm64 is
already demonstrated to work (all fixups apply, ~31 % of imports resolve). Running
it is blocked by framework breadth, not by a fundamental technical barrier:
every remaining item is "more HLE implementation", plus one hard engineering
piece (exception unwinding). For an app the size of Loopy HD this is a
multi-person-month effort.

## Stage 2 result (aggregate, measured locally)

After stage 2, **983 of 1,212 imported symbols (81.1 %)** are provided by iosrt
(stage 1: 30.8 %). Run headless under qemu with lenient handling of the rest,
the app:

* runs all 36 static initialisers and its third-party SDKs' start-up
  (analytics, backend client, crash reporter, audio engine, inter-app audio,
  MIDI) without crashing;
* loads its main view controller from its compiled NIB, decodes its PNG
  resources and lays the screen out for the 320×568 compatibility screen it
  supports;
* receives a scripted tap on a track through its own gesture recognisers, and
  its audio engine starts and stops a recording on the render thread.

What the trace showed was needed beyond "more API", all implemented as generic
runtime features: per-object `@synchronized` (striped locks deadlocked a
task library), per-thread run loops with `performSelector:onThread:`, real
serial dispatch queues and `dispatch_get_specific`, render notifications on
audio units, the NIB loader's handling of archived autoresizing and interaction
flags, and iOS's compatibility ("zoomed") screen for apps without a launch
storyboard. Remaining gaps for this app: microphone input, audio file
conversion, C++ exception unwinding and networking.
