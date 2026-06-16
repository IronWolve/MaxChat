# MaxChat C++/Qt

Native C++/Qt6 port of MaxChat, a feature-rich IRC client. This is a behavior
port (not line-by-line translation) of the Python/PySide MaxChat application. All
major features from the original are implemented, including several security
improvements unique to this C++ version.

## Build

### Debug

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Release

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

### Build options

Lua scripting is a core dependency and is always built.

| Option | Default | Effect when OFF |
|--------|---------|-----------------|
| `MAXCHAT_TERMINAL` | ON | No script terminal / BBS UI — a lean "vanilla IRC client". Bundled BBS scripts degrade gracefully (`api.terminal_*` return false). |
| `MAXCHAT_OS_SPELL` | OFF | (Windows only) native ISpellChecker backend instead of the bundled engine. |

```bash
cmake -S . -B build-lean -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DMAXCHAT_TERMINAL=OFF        # IRC client without the terminal/BBS feature
```

### Run

```bash
./build/maxchat-c
./build/maxchat-c --selftest
./build-release/maxchat-c --selftest
```

### Translations

UI strings marked with `tr()` are translatable. Translation sources live in
`translations/*.ts`; the build compiles them to `.qm` and embeds them in the
binary (loaded at startup per the `interface_language` setting / system locale).

To add or update a language:

```bash
# 1. regenerate the .ts files from the current tr() strings
cmake --build build --target update_translations
# 2. translate translations/maxchat_<lang>.ts  (Qt Linguist, or edit the XML)
# 3. add the new .ts to TS_FILES in CMakeLists.txt, then rebuild
```

A partial German translation (`maxchat_de.ts`, menu bar + tray) ships as the
reference. Marking the rest of the UI is incremental — wrap more user-facing
`QStringLiteral("…")` as `tr("…")`, then re-run `update_translations`.

### Windows

```cmd
build.bat            # MinGW default
build.bat msvc       # MSVC
build.bat noterm     # without the terminal / BBS UI (vanilla IRC client)
build.bat osspell    # with Windows ISpellChecker COM backend
build.bat tests      # build, then run the ctest suite
```

## Features

**IRC:** Full IRCv3 protocol (CAP, SASL PLAIN, away-notify, server-time), flood
queue with ircII penalty model, read-idle watchdog, UTF-8-safe line splitting,
outbound flood queue, multi-network with per-network connections.

**Chat:** mIRC control codes (0–98 + hex), colored nick labels (palette/IRC modes),
right-aligned nick columns, timestamps, marker line, join/part hiding, 60+
commands (superset of original), tab completion cycling, input history.

**Services:** OpenGraph/X/Twitter/Mastodon link preview cards, direct image
thumbnails, audio/video preview rows, SSRF-safe async fetcher with per-hop
redirect + DNS re-validation.

**Image hosting:** Pluggable upload backends (ImgBB, Imgur, Postimages, Imgbox)
with paste/drop-to-upload via drag-and-drop or clipboard.

**Spellcheck:** Vendored Hunspell 1.7.2 (all platforms) + Windows ISpellChecker
COM backend, right-click suggestions, add-to-dictionary.

**DCC:** Active/passive SEND, RESUME/ACCEPT, DCC CHAT, accept policy
(ask/trusted/all), port range, transfers dialog with rate/ETA, 64-bit ack
tracking, CSPRNG tokens.

**Comic mode:** .avb/.bgb decoder, character model, 9 emotions with text/emoji
guessing, panel rendering with smart balloon tails, panel cache, zoom view,
per-buffer opt-in, emotion picker, per-channel overrides, character assignment,
copy/save panel PNG.

**Themes:** 36 app + 24 chat themes, wallpapers (8 generated + bundled), OS
nostalgia packs (Win95/XP/11, Aqua), theme packs (import/export), theme editor,
chat opacity, live preview, user themes.

**Scripting:** Lua 5.4 with full sandbox (no io/os/package by default),
permissions tab (read files, write files, run programs, load modules, network
access), 5 bundled examples, api table (echo/say/send_raw/http_get etc.),
script data directory.

**BBS / MC DATA:** Terminal windows with ANSI renderer, CP437 box-drawing,
MCB1 1-bit bitmap format with RLE + Z85 armor, Bayer dithering, 160×50
quadrant glyphs.

**Notifications:** Toast widget, OS notifications, DND, tray icon + menu,
taskbar flash, notification sounds (.wav), CTCP SOUND receive.

**Security:** SSRF protection (per-hop redirect + DNS re-validation), DCC
size-0 guard, comic decoder OOM cap, CR/LF injection strip, CSPRNG tokens,
CTCP rate-limit, wallpaper path sanitization, topic PlainText.

## Tests

53 CTest targets covering IRC protocol, chat rendering, services, dialogs,
comic, DCC, scripting, terminal, themes, and core models.

```bash
ctest --test-dir build --output-on-failure
```

## Documentation

- `HANDOFF.md` — current development status and ground truth
- `GUI.MD` — complete GUI reference (windows, dialogs, menus, themes)
- `INPUT_FOCUS_DESIGN.md` — key redirect and geometry persistence design
- `SCRIPTING.md` — Lua scripting API and sandbox reference
- `PORT_PLAN.md` — historic planning document (all milestones complete)
- `STATUS.md` — development changelog and current state

## Dependencies

- Qt 6.10+ (Core, Gui, Widgets, Network, Multimedia, MultimediaWidgets, Test)
- Lua 5.4 (vendored)
- Hunspell (vendored)
- Pillow (for tools/mcb1_convert.py only)
- CMake 3.24+, Ninja, C++20 compiler
