# iosrt: рабочие заметки

Короткий контекст проекта и места в коде, к которым я постоянно возвращаюсь.
Обновлять после каждой фазы. Подробности: `docs/STATUS.md` (что работает)
и `docs/ARCHITECTURE.md` (как устроено).

## Что это

Совместимый рантайм: исходный arm64 Mach-O из iOS-приложения запускается
нативно на Android (aarch64). ObjC-рантайм, Foundation, UIKit, CoreGraphics,
CoreText, AudioToolbox и другие фреймворки реализованы как HLE-экспорты в C.
Результат собирается в APK: оболочка, в которую можно импортировать `.ipa` или `.app`.

## Правила (от Кирилла, действуют всегда)

- Чужие IPA (Loopy HD, DeathWorm, Gismeteo, GunLayer) — только источник
  требований к совместимости. Их код и ресурсы **не попадают в репозиторий**,
  лежат только в scratchpad (`…/scratchpad/apps/`, `…/scratchpad/loopy/`).
  В доках — только агрегаты.
- Не подменять задачу «похожим Android-приложением». Всё делается как общая
  часть рантайма плюс свои сэмплы в `samples/`.
- Ветка `ccr-f2eafec3-fvz7zc`. Коммиты заканчиваются строками
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` и
  `Claude-Session: https://claude.ai/code/session_01QzS8eQHQpD67mB1jEdk3Vr`.
  Модельных ID в коде, доках и коммитах быть не должно.
- В конце фазы: тесты, коммит, push, APK Кириллу (SendUserFile).

## Команды

```sh
runtime/build.sh linux|android|all          # build/iosrt-run-{linux-aarch64,android-arm64}
tests/run_tests.sh                          # всё под qemu, обе сборки; сейчас 57/57
tools/build_apk_manual.sh <out.apk>         # APK без gradle (NDK + d8 + aapt2)
sdk/build-app.sh <out> 12.0 main.m -framework UIKit ...   # сборка своего сэмпла
# реальное приложение в трассе (только android-сборка: в ней есть C++):
RUNTIME=build/iosrt-run-android-arm64 IOSRT_PNG_OUT=x.png TRACE_OUT=dir TRACE_TIMEOUT=600 \
  tools/trace_app.sh "<App>.app" <frames> "30:tap:x,y;50:down:..;51:move:..;52:up:..;60:wait:ms"
```

Полезные env: `IOSRT_STRICT=0` (lenient, по умолчанию на Android),
`IOSRT_VERBOSE=1` (бэктрейсы у soft error), `IOSRT_DUMP_CLASS=A,B` (методы классов),
`IOSRT_HEADLESS_FRAMES`, `IOSRT_SCRIPT`, `IOSRT_PNG_OUT`, `IOSRT_LOG_PLAIN=1`.
`ipsw` для анализа собран в `scratchpad/ipsw/ipsw` (`class-dump --arch arm64`).

## Куда смотреть в коде

| Что | Где |
|---|---|
| Экспорты HLE: `SYM(lib, _name, impl)`, `CLASS(...)` | `runtime/src/exports/*.def`; `uikit.def` генерит `tools/gen_uikit_def.py` (классы `*Impl` и `PRIVATE_*` пропускаются), `cxx_gen.def` — `tools/gen_cxx_def.py`, tbd для SDK — `tools/gen_tbd.py` |
| Разрешение символов, fallback через dlsym | `loader/symbols.c` `rt_resolve_symbol` → `rt_platform_dlsym_fallback` (`platform/android.c`, слабый дефолт в `util.c`) |
| Классы и методы в C | `NEWCLASS`, `IM`, `CM` (`uikit/uikit_internal.h`), `RT_SEND0..3`, `RT_CLASS`, `RT_DEFINE_CLASS` (`internal.h`) |
| Платформа (вывод, кадр, звук) | `struct rt_platform` в `internal.h`, Android — `platform/android.c` |
| Тачи, скрипт ввода, headless | `uikit/uikit.c`: `deliver_touch` (slop 2 pt), `run_script`, `render_frame` |
| View controllers, навигация, таблицы | `uikit/controls2.c` (`nav_*`, `vc_*`, `rt_vc_appear`, `rt_vc_load_view`) |
| NIB / keyed archives / storyboard | `uikit/nib.c`, `foundation/archiver.c`, `uikit/storyboard.c` |
| Слои и композитинг | `uikit/layer.c` (`rt_layer_render`, `draw_contents`) |
| UIWebView | `uikit/webview.c` + движок `web{dom,css,layout,js}.c`, интерфейс `webengine.h`; QuickJS в `runtime/third_party/quickjs` |
| OpenGL ES / EAGL | `gl/gl.c` (драйвер, EGL, обёртки), `gl/eagl.c` (EAGLContext, present в слой), `gl/gl_null.c` (софт-заглушка без драйвера), `gl/pvrtc.c`; экспорты `exports/gl.def`, привязка в `loader/symbols.c` (lib `OpenGLES` → `rt_gl_resolve`) |
| Звук | `audio/audio.c` (RemoteIO, AUGraph, бэкенды), `audio/queue.c` (AudioQueue поверх внутреннего RemoteIO) |
| Акселерометр | `motion/motion.c` (UIAccelerometer, CMMotionManager, NDK-датчик), `exports/motion.def` |
| Отчёт совместимости (`--report`, вердикт) | `run.c` `report_bundle()` |
| Старый GNU libstdc++ (list, rb-tree, throw) | `cxx/cxx_abi.c`, экспорты в `exports/cxx.def` |

## Подводные камни ABI

- Darwin пакует мелкие аргументы на стеке по 4 байта, AAPCS64 — по 8.
  Функции с ≥2 мелкими стековыми аргументами нужно оборачивать: GL —
  `glTexSubImage3D`, `glBlitFramebuffer`, `glCompressedTexSubImage3D`.
- `BOOL`/`GLboolean` в возвращаемом значении: маскировать `& 0xff`.
- В `darwin_abi.S` новый код ставить в `.text` до `.note.GNU-stack`.
- Совпадение имени с символом NDK libc++ даёт duplicate: переименовать в `iosrt_*`
  и экспортировать вручную (так сделан `iosrt_gnu_throw_bad_alloc`).
- `uikit.def` не парсит анонимные struct в параметрах: использовать `const void *`.

## Уже сделано (не переделывать)

- Панель Loopy: touch slop, appearance forwarding (iOS 5+), `initWithRootViewController:`
  через `pushViewController:` (коммит `bfdeb10`). Пилюля BPM проверена трассой:
  TAP/BPM открывается, значок часов справа возвращает, такт тянется.
- Туториалы Loopy: JS + DOM в UIWebView, Next/Back/Close работают (`24a9d40`).
- OpenGL ES через EAGL (`48c0441`), AudioQueue и акселерометр: DeathWorm в трассе проходит
  инициализацию и игровой цикл без недостающих API (в null-GL кадр чёрный, на телефоне — драйвер).
- GunLayer не падает на старте: GNU libstdc++, CFNotificationCenter,
  `objectsForKeys:notFoundMarker:`, rlimits (`a00294a`). Дальше ему нужны GL и dylib.
- Фаза 3a: keyed archives, старые NIB, storyboard'ы, CoreText, setjmp, mach-часы.

## Координаты Loopy в трассе (экран 320×568 pt)

▲/▼ экран управления — (34,537) / (34,31); пилюля-часы — (88,537); значок
часов справа на панели — (296,537); Help в меню — (160,518); «Track basics» в
Help — (100,497); в туториале Next — (284,546), Close — (279,23).

## План (порядок согласован с Кириллом)

1. Фаза 3b (идёт): ✅ панель Loopy, ✅ JS в UIWebView, ✅ OpenGL ES, ✅ AudioQueue,
   ✅ акселерометр → GLKit → звук дальше (декодеры WAV/CAF/MP3/AAC, AVAudioPlayer,
   AudioServices, OpenAL, ExtAudioFile, AudioConverter, микрофон) → Keychain,
   GameKit/StoreKit «недоступно». Детальный план:
   `/root/.claude/plans/root-claude-uploads-1d44effc-ef14-59e7-dapper-shannon.md`.
2. Фаза 3c: загрузка `MH_DYLIB` из `.app/Frameworks` (GunLayer `enhance.framework`,
   основа под Swift).
3. Потом сеть, Core Data, клавиатура.

## Приложения Кирилла (что им нужно)

- **Loopy HD**: UIKit, AudioUnit/RemoteIO, UIWebView-туториалы; не хватает микрофона,
  AudioConverter.
- **DeathWorm**: GLES 1.1 (OES FBO), AudioQueue, UIAccelerometer — всё есть; проверить на телефоне.
- **GunLayer**: Adobe AIR, `enhance.framework` (dylib), GLES, старый libstdc++.
- **Gismeteo**: Swift, Core Data, SpriteKit, MapKit — самая дальняя цель.
