# iosrt architecture

```
 ┌──────────────────────── Android app process ":guest" ─────────────────────────┐
 │  Java shell (RunActivity / IosView)                                            │
 │      │ JNI: nativeRun / nativeTouch / nativeQuit      ▲ onLog / onFrame / onExit│
 │  ┌───▼──────────────────────── libiosrt.so ──────────┴─────────────────────┐   │
 │  │ platform/android.c   logcat+Java log, display list -> Java, AAudio,     │   │
 │  │                      libc++ std::__1 -> std::__ndk1 remapping           │   │
 │  │ run.c                launcher, guest thread, exit/crash handling        │   │
 │  │ loader/              Mach-O parser, mmap, dyld-info + chained fixups,    │   │
 │  │                      symbol resolution, trap stubs for missing APIs     │   │
 │  │ abi/                 Darwin variadic trampolines, objc_msgSend (asm),   │   │
 │  │                      printf/NSLog formatter (%@)                        │   │
 │  │ objc/                Objective-C 2 runtime + ARC + blocks + C API        │   │
 │  │ libsystem/           Darwin libc/pthread/dispatch/libkern shims          │   │
 │  │ foundation/          NSObject, NSString, collections, NSBundle(+plist), │   │
 │  │                      NSTimer/NSRunLoop, notifications, CF subset        │   │
 │  │ uikit/               UIApplicationMain loop, view tree, display list,   │   │
 │  │                      touches, CoreGraphics recorder                     │   │
 │  │ audio/               AudioToolbox RemoteIO + AVAudioSession             │   │
 │  └──────────────────────────────────▲──────────────────────────────────────┘   │
 │                                     │ native arm64 calls (no emulation)          │
 │  ┌──────────────────────────────────┴──────────────────────────────────────┐   │
 │  │ guest: the app's original Mach-O arm64 code, mapped at a random slide   │   │
 │  └─────────────────────────────────────────────────────────────────────────┘   │
 └────────────────────────────────────────────────────────────────────────────────┘
```

## Launch sequence

1. `.ipa` → the Java shell extracts `Payload/X.app`; `.app` → `Info.plist`
   (XML or binary plist, own parser) gives `CFBundleExecutable`.
2. **Loader** picks the arm64 slice of a fat binary, rejects armv7-only,
   arm64e (pointer authentication) and FairPlay-encrypted (`cryptid != 0`)
   binaries with a precise message.
3. Segments are copied into an anonymous, 16 KiB-aligned reservation (works with
   4 KiB and 16 KiB Android page sizes); the slide is applied by the fixups.
4. **Fixups**: `LC_DYLD_INFO(_ONLY)` rebase/bind/lazy-bind opcode streams (lazy
   binds are resolved eagerly) or `LC_DYLD_CHAINED_FIXUPS`
   (`DYLD_CHAINED_PTR_64`, `_64_OFFSET`; import formats 1–3).
5. **Symbol resolution** against the HLE export table (`exports/*.def`, ~900
   symbols incl. 50+ classes). Unknown imports become:
   * functions → a unique 32-byte trap stub per symbol; calling it ends the guest
     with `called unimplemented function _Foo` (instead of a random crash);
   * `_OBJC_CLASS_$_X` → a placeholder class (subclass of NSObject) so app
     subclasses still realize; first use prints a warning;
   * data symbols (constants, keys, notification names) → zeroed blocks.
6. Memory protections are applied (`__TEXT` R-X; I-cache flushed).
7. **ObjC image mapping**: selector refs are uniqued, protocols canonicalised,
   classes realized (ivar offsets slid if a superclass grew), categories attached.
8. On a dedicated 8 MiB guest thread: `+load`, static initialisers
   (`S_MOD_INIT_FUNC_POINTERS`, `S_INIT_FUNC_OFFSETS`), then `main(argc, argv, envp, apple)`.
9. `exit()`, `abort()`, uncaught exceptions, unrecognized selectors and missing
   APIs unwind to the launcher via `siglongjmp`; SIGSEGV/SIGBUS are reported with
   the faulting guest symbol + offset.

## Darwin arm64 vs AAPCS64 (Linux/Android)

Native execution works because both sides are AArch64 and the base PCS is the
same. The differences and how they are handled:

| Difference | Handling |
|---|---|
| Variadic arguments are **always on the stack** on Darwin (8-byte slots); `va_list` is a plain pointer | Every exported variadic function (`printf`, `NSLog`, `stringWithFormat:`, `arrayWithObjects:`, `open`, `fcntl`…) goes through a 2-instruction asm trampoline that passes `sp` to a C implementation; `v*printf` take the Darwin `va_list` pointer directly. Own formatter supports `%@`. |
| Callers may rely on **callee-extended < 32-bit return values** (BOOL, char) | IMPs/exports never return `bool`/`char`; they return `long` (`rt_bool`). |
| x18 is reserved on Darwin | Guest never touches it; Android's use is unaffected. |
| `char` is signed on Darwin | Runtime compiled with `-fsigned-char`. |
| `long double` = `double` on Darwin | Formatter strips `L`. |
| Struct layouts / constants differ (`stat`, `dirent`, `O_*` flags, `errno` values, clock ids, `sysconf` names, pthread static initialisers, `FILE*` globals) | Translating wrappers in `libsystem.c`; Darwin pthread initialisers are detected by their signature word and lazily turned into host objects (Darwin sizes are ≥ host sizes). |
| Thread-local variables (`__thread_vars`, `tlv_get_addr`) | **Not supported yet** (see STATUS). |
| ctype macros index `_DefaultRuneLocale` | A compatible rune-locale table is exported. |

## Objective-C runtime

* Works directly on the guest's own `objc_class` / `class_ro_t` structures;
  the runtime-private data pointer lives in the `cache` word (unused by
  compiled code).
* Native (runtime-implemented) classes are static `objc_class` objects with the
  same layout, so guest classes can subclass them, add categories and swizzle.
* `objc_msgSend` / `objc_msgSendSuper(2)` in asm: save all argument registers
  (x0–x8, q0–q7), look up the IMP (per-class cache), restore, tail-branch — so
  stack arguments and struct returns via x8 pass through untouched.
* Lookup fallbacks: `+resolveInstanceMethod:` / `+resolveClassMethod:`,
  `-forwardingTargetForSelector:` (the receiver in the saved x0 is replaced),
  otherwise a clear "unrecognized selector" termination.
* Memory management: hidden 16-byte header (magic + refcount) before each heap
  object; immortal objects (constants inside the image, class objects).
  ARC entry points, autorelease pools, weak references (zeroing on dealloc),
  associated objects, `@synchronized`, `.cxx_construct` / `.cxx_destruct`.
* Blocks: global/stack/malloc block classes, `_Block_copy/_Block_release`,
  `__block` byref variables with copy/dispose helpers.
* Not yet: `NSInvocation`-based forwarding (`forwardInvocation:`),
  exception unwinding (`@try/@catch` needs a Mach-O-aware unwinder), Swift.

## UIKit compatibility layer (design and stage-1 implementation)

Stage 1 (implemented): `UIApplicationMain` → delegate lookup (explicit name or
`*AppDelegate` heuristic) → `didFinishLaunching` → run loop that interleaves
touch delivery, main-queue blocks, `NSTimer`s and rendering. Views are real
ObjC objects; each frame the key window tree is walked into a **display list**
(rect / text / oval / line commands, absolute coordinates in points) that the
platform renders: `IosView` draws it with Android `Canvas`. Custom `-drawRect:`
works through a recording `CGContext`. Touches: hit-testing, responder chain,
`UIControl` target/action.

Planned target design (for real apps):

| iOS concept | Android mapping |
|---|---|
| `CALayer` tree, implicit animations | retained layer tree in the runtime, composited with GLES/Vulkan (Skia-like renderer) on a `SurfaceView`; animation clock driven by `Choreographer` |
| `CGContext` / `CGPath` / `CGImage` / CoreText | software rasteriser (e.g. port of an MIT 2D renderer) or Skia via NDK; CoreText → HarfBuzz/FreeType or Android `Paint` via JNI |
| `UIImage` / ImageIO | `AImageDecoder` (NDK, API 30+) or `BitmapFactory` via JNI |
| Run loop / `CFRunLoop` | guest main thread with its own loop; wakeups from Android `Looper` via an eventfd |
| Touches, gestures | `MotionEvent` → `UITouch` phases; gesture recognisers implemented in runtime |
| Text input | `InputConnection` ↔ `UITextInput` |
| NIB / storyboard | `NSKeyedUnarchiver` over compiled `.nib` (binary plist / NIBArchive) |
| `UIScreen` scale, orientation, safe areas | from `DisplayMetrics`, `WindowInsets` |

### UIWebView: the built-in web engine

Apps use `UIWebView` mostly for pages shipped in the bundle (help, tutorials,
licenses), often scripted, and often as a channel to native code: the page
loads `myscheme://command` in a hidden iframe, the app answers in
`webView:shouldStartLoadWithRequest:navigationType:`, and the app asks the page
questions with `stringByEvaluatingJavaScriptFromString:`. The runtime renders
such pages itself (`runtime/src/uikit/web*.c`, interface in `webengine.h`):

| Stage | File | What it does |
|---|---|---|
| DOM | `webdom.c` | forgiving HTML tree builder, tree edits, `innerHTML`/`outerHTML`, CSS selector matching |
| Style | `webcss.c` | sheets, `@media`, cascade (UA < author < style attribute < `!important`), computed values |
| Layout | `weblayout.c` | block/inline flow, floats, inline-blocks, positioned boxes against their containing blocks; a sorted display list plus one border box per element; `elementFromPoint`-style hit testing |
| Script | `webjs.c` | QuickJS (`runtime/third_party/quickjs`, MIT) with one JS object per DOM node; a JS prelude provides events, `classList`, timers, `location`, `console`, storage |
| View | `webview.c` | loading and navigation, the `UIScrollView` + document view pair, taps → click events → links |

Layout runs again when the DOM or an inline style changes; geometry getters
(`offsetWidth`, `getBoundingClientRect`, `elementFromPoint`) lay out first.

## Core Audio compatibility layer

Stage 1 (implemented): RemoteIO output units — `AudioComponentFindNext`,
`AudioComponentInstanceNew`, `AudioUnitSet/GetProperty` (stream format, render
callback, EnableIO, max frames), `AudioOutputUnitStart/Stop`. The backend pulls
audio by calling the guest's render callback with a real `AudioBufferList` and
`AudioTimeStamp`, converting float32 / int16 / 8.24 fixed-point
(interleaved or not) to float. Backends: **AAudio** low-latency stream on
Android, paced **WAV writer** on the host (used by the tests).
`AVAudioSession` returns sensible values.

Next steps toward an audio app like Loopy HD:

| iOS API | Android mapping |
|---|---|
| RemoteIO input (bus 1, `AudioUnitRender` on the mic) | AAudio input stream in the same full-duplex callback (or Oboe) |
| `AUGraph`, mixer / converter units | in-runtime graph of HLE audio units (mixer, format converter, varispeed) |
| `ExtAudioFile`, `AudioFile`, `AudioConverter` | WAV/CAF parsers in runtime; AAC/M4A via `AMediaCodec` + `AMediaExtractor` |
| `AVAudioSession` categories, interruptions, route changes | `AudioManager` / `AudioDeviceCallback` via JNI |
| CoreMIDI | `AMidi` (NDK) + `android.media.midi` |
| Accelerate / vDSP | NEON implementations of the used vDSP functions |
| Inter-App Audio, Audiobus, Ableton Link | no Android equivalent (IAA/Audiobus); Link has an Android SDK — would need a bridge |

## Testing approach

* The host is x86_64 without KVM, so the Android emulator cannot run arm64
  images. Instead, the runtime is built twice — linux-aarch64 (glibc) and
  **android-arm64 (bionic, static)** — and the test apps run under
  `qemu-aarch64` user-mode emulation. The bionic build exercises the same
  libc the APK uses.
* Test apps are original source code in `samples/`, compiled with clang for
  `arm64-apple-ios10.0` / `ios14.0` and linked by `ld64.lld` against the
  generated mini-SDK `.tbd` stubs — real iOS Mach-O executables, no Apple SDK.
