# GarageBand 2.2.2 (iOS): what it needs from iosrt

Aggregates from the compatibility report and headless traces of a user-provided,
decrypted IPA. No code or resources of the app are in this repository.

## The bundle

| | |
|---|---|
| Binaries | executable 50 MB (arm64 slice of a fat armv7/arm64 binary), one embedded framework; all `cryptid=0` in the provided copy |
| Code | ~28 MB of arm64 code, 1,844 Objective-C classes, 100 categories, 1,910 static initialisers |
| C++ | heavy (audio engine, Poco): 3.9 MB of exception tables, exceptions used in normal control flow |
| UI | 165 NIBs (`NSMainNibFile`), landscape only, `Assets.car` asset catalog, iPhone and iPad layouts |
| Data | Core Data model (`.momd`), sqlite3, iCloud/ubiquity calls |
| Size | 1.6 GB unpacked (sound libraries) |

## Imports at the start (iosrt before this work: 58% provided)

1,085 imported symbols were missing, by library: libc++ 173, CoreFoundation 155, libSystem 143,
Accelerate 90, Foundation 60, UIKit 49, AVFoundation 45, JavaScriptCore 41, AudioToolbox 30,
Security 29, MediaPlayer 24, libsqlite3 23, QuartzCore 23, CoreGraphics 20, CoreMedia 15,
CoreData 13, CoreMIDI 12, and small numbers from ~20 other frameworks.

## What the launch needed (in the order the traces hit it)

1. Embedded framework loading (`@rpath`, two-level binds).
2. Apple's libc++ for real: iostreams and locale in static initialisers (`std::__1`, Darwin
   `mbstate_t`, ctype masks and pthread object layouts).
3. Objective-C runtime details: `+setVersion:`, `initWithBytesNoCopy:...`, `NSScanner` with
   character sets, `NSNotificationQueue`, `UIAppearance`, StoreKit's payment queue.
4. Memory checks: `kern.memorystatus_level`, `sysctl({CTL_HW, HW_USERMEM})`, `ledger()` footprint.
5. Mach semaphores, `OSAtomic*`/`OSQueue`, `CFRunLoopTimer`, CF path and URL functions.
6. C++ and Objective-C exception unwinding (thrown and caught during startup).
7. vDSP FFTs and vector routines, `fscanf`.
8. The main nib loaded even when `main()` names the delegate class.

After these, the app reaches its run loop and shows the instrument browser (headless trace,
null GL).

## Still missing for the first screen to look right

* `Assets.car` (compiled asset catalog): instrument images, backgrounds.
* Layout details of the instrument carousel.
* Localised strings in some labels.

## Later

Core Data and sqlite3, JavaScriptCore, Metal (the app falls back when it is missing),
microphone input, MIDI, the audio units the instruments use.
