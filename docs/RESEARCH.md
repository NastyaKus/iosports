# Research: existing iOS-on-other-OS projects and what can be reused

Goal of this research: decide how to build a reusable runtime that executes the
*original* code of iOS apps (Mach-O arm64) on Android arm64, and check which
existing open-source code can legally be reused.

## Survey

| Project | What it does | CPU strategy | iOS versions / arch | License | Reusable for iosrt? |
|---|---|---|---|---|---|
| **touchHLE** | High-level emulator of early iPhone OS: provides its own Foundation, UIKit, OpenGL ES, OpenAL… instead of Apple's frameworks. Has an Android port (app picker GUI). | Emulates 32-bit ARM with *dynarmic* (JIT) | iPhone OS 2.x–3.x, armv6/armv7 **only**; the authors state 64-bit iOS will never be supported | Source MPL-2.0, binaries GPLv3+ (dependency compatibility) | **Ideas yes, code mostly no.** It is Rust, all guest pointers are 32-bit (`u32` guest memory model) and every framework call goes through the emulated CPU's ABI. A native arm64 runtime has a completely different calling and memory model. MPL-2.0 is file-level copyleft, so individual algorithms/files could be reused if kept under MPL in separate files. |
| **"tapHLE"** | Searched for it: no project by this name was found. Most likely a mix-up with touchHLE. | — | — | — | — |
| **Darling** | Runs macOS (not iOS) binaries on Linux. Own dyld, libSystem built from Apple open-source, Cocoa largely missing. | Native x86_64 (Mach-O loaded by its own `mldr`), Linux kernel module/shim for Mach syscalls | macOS, x86_64 | GPLv3 + APSL 2.0 parts | Architecture reference only: macOS-focused, x86_64, GPL would force the whole runtime to GPL, and its libobjc/CoreFoundation come from Apple sources (excluded by the task rules). |
| **ipasim** | Windows research project: translates iOS apps for Windows using WinObjC. | Recompilation of ARM code | 2019-era iOS, Windows only | MIT-ish | Not applicable (Windows, recompilation approach). |
| **WinObjC (Microsoft)** | Objective-C / partial UIKit on Windows for *recompiled* source code. | n/a (source-level) | — | MIT | Possible future reference for UIKit class semantics; source-level port, not binary compatible. |
| **GNUstep libobjc2** | Objective-C runtime | n/a | — | MIT | **Not binary-compatible** with Apple's ObjC2 metadata (different class/method/ivar layout, selectors, message lookup). Cannot load Apple-compiled classes. |
| **Apple objc4, CF-lite (APSL)** | Apple's own open-source runtime/CoreFoundation | n/a | — | APSL 2.0 | Excluded by the task constraints ("no proprietary Apple code"); also APSL is incompatible with GPL. |
| **Apportable / SpriteBuilder Android** (historic) | Compiled iOS *source* for Android | n/a | — | proprietary / defunct | Not usable. |

## Conclusion

No existing project runs **64-bit iOS** binaries on Android, and none provides a
reusable native-arm64 Objective-C runtime that understands Apple's binary
metadata without using Apple's own code. Therefore iosrt was written from
scratch:

* **Mach-O loader + dyld fixups**: own implementation from the public file-format description.
* **Objective-C runtime**: own implementation of the ObjC2 ABI (class_ro_t, method lists incl. relative lists, non-fragile ivars, categories, protocols, selector refs, ARC entry points, blocks).
* **Frameworks**: HLE ("high-level emulation", the touchHLE idea) — the runtime itself provides Foundation / UIKit / AudioToolbox classes and functions.

The key difference from touchHLE: guest and host are both arm64, so **guest
code runs natively on the CPU** — no JIT, no instruction emulation. Only the
binary format, the Darwin calling-convention quirks and the system libraries
have to be bridged.

## License choice for iosrt

All code in this repository was written for this project. Nothing was copied
from touchHLE, Darling, objc4 or any Apple SDK. If MPL-2.0 files from touchHLE
are ever reused (e.g. specific algorithms), they must stay in separate files
under MPL-2.0. The repository owner should pick an explicit license before
publishing; MIT or MPL-2.0 would both keep that option open.
