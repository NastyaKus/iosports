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

## The first screen

9. `Assets.car`: ~2,900 names / ~6,900 renditions, LZFSE-compressed bitmaps (v1 and v2
   blocks), LZVN, packed atlases referenced by link renditions, JPEG data renditions.
10. Auto Layout in NIBs (the first-launch screens and button bars are constraint-based).
11. Appearance order: content of the first-launch screens is built in `viewWillLayoutSubviews`,
    and its fade-in animation starts in `viewDidAppear:`.
12. The screen size: `UILaunchImages~iphone` lists landscape entries with portrait sizes; the
    app's phone layouts expect at least 568×320 (iPhone 6 size, 667×375, is used).
13. NIB booleans (`UIHidden`, `UIClipsToBounds`, …): NIBArchive value type 4 is NO, 5 is YES.
14. `UIButton` title/image edge insets inset the content rect (curated-sound buttons).

Taps go through Welcome → Get Started → the instrument browser; the carousel swipes.

## Creating a song (tap on an instrument card)

15. Older file APIs (`createDirectoryAtPath:attributes:`, `writeToFile:atomically:`,
    `stringWithFileSystemRepresentation:length:`), set/index-set/dictionary bulk operations.
16. `CFStringGetListOfAvailableEncodings` (the song engine walks it while loading channel strips).
17. `NSUndoManager`, `NSHashTable`, `NSFileCoordinator` (the save goes through its accessor block).
18. Nil-tolerant `bringSubviewToFront:` and friends; `maskView`, snapshots, trait collections.

With these, the app creates and saves `My Song.band` (project data, metadata), builds the
song screen and the Keyboard instrument's controller.

## Playing the Keyboard

19. `-[NSString compare:options:range:locale:]` (missing, it answered "equal" and broke the
    sorted instrument index: "EXS instrument not found"), NSFileAttributes accessors.
20. A recursive `NSDirectoryEnumerator` with attributes, NSURL resource values from stat(2).
21. UIView legacy animations call the delegate's `animationDidStop:finished:context:` by default
    (the song-opening transition waits for it before the loading overlay goes away).
22. `insertSubview:belowSubview:` with a sibling in another view puts the view on top.
23. KVC with struct values (the instrument skins set frames, centers and transforms by key).

Then the Keyboard screen appears (Grand Piano, Sustain, keys C3–C4) and touches on the keys
play the sampled piano through RemoteIO (one finger and two-finger chords in a trace).

## Still missing on the first screen

* "do not localize" placeholder labels (hidden on iOS by later code paths), Smart Piano icon.
* `UIMotionEffect` (parallax), `UIStackView`.

## Later

Core Data and sqlite3, JavaScriptCore, Metal (the app falls back when it is missing),
microphone input, MIDI, the audio units the instruments use.
