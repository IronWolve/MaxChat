# MaxChat-C Full Code Audit — 2026-06-15

**Auditor:** DeepSeek (automated analysis)
**Scope:** All `src/`, `tests/`, `CMakeLists.txt`, config, assets, and project documentation
**Total source files audited:** ~180 source files, ~25,000+ lines

---

## BUGS (by severity)

### CRITICAL

| # | File | Line(s) | Issue |
|---|------|---------|-------|
| C1 | `tests/unit/main_window_link_preview_test.cpp` | 33 | **`#define private public`** before includes — breaks ODR in all translation units that include these headers. Every internal rename or refactor becomes a silent test breakage. This is the single most severe issue in the codebase. |

### MEDIUM

| # | File | Line(s) | Issue |
|---|------|---------|-------|
| M1 | `src/app/AppInfo.h` | 1 | **Missing `#pragma once` (or any include guard).** If included transitively by two paths, causes compiler error on redefinition. |
| M2 | `src/irc/DefaultNetworks.cpp` | 1526-1541 | **IPv6 parsing broken in `parseServerSpec`.** Uses `lastIndexOf(':')` to split host:port. Bare IPv6 addresses (e.g., `2001:db8::1`) cause `lastIndexOf(':')` to match an internal colon, breaking host extraction. |
| M3 | `src/irc/DefaultNetworks.cpp` | 1546-1548 | **`serverSpec` doesn't bracket IPv6.** Serialized spec for IPv6 hosts would be malformed (missing `[...]` wrapping). |
| M4 | `src/spell/HunspellSpellchecker.cpp` | 73-74 | **`isCorrect()` returns `false` when no dictionary loaded.** This flags every word as misspelled if no dictionary is loaded. `WindowsSpeller::isCorrect()` correctly returns `true` in the same situation. Inconsistent behavior. |
| M5 | `src/scripting/ScriptPermissions.h` | (header-only) | **`allowedDirs` excluded from `fromMap()`/`toMap()` serialization.** Users who configure custom allowed directory permissions lose them on restart. All bool flags serialize correctly; the directory list is silently dropped. |

### LOW

| # | File | Line(s) | Issue |
|---|------|---------|-------|
| L1 | `src/core/ChatLineFormatter.cpp` | 118-119 | **`www.`-prefixed URLs get relative `href`.** `www.example.com` gets `href="www.example.com"` instead of `href="http://www.example.com"`. Clicking produces a broken relative URL. |
| L2 | `CMakeLists.txt` | 71-75 | `ScriptPermissions.h` (header-only) not listed in `maxchat_scripting` target. Compiles via include path but should be listed for IDE/correctness. |
| L3 | `CMakeLists.txt` | 82, 1294-1298 | `maxchat_core` linked `PRIVATE` to `maxchat_scripting`. `lua_engine_test` won't inherit `maxchat_core` symbols if `TerminalFrame.cpp` needs them. Fragile. |
| L4 | `PostimagesUploader.cpp` | 70-72 | `direct_link.toString()` called twice when non-empty instead of caching in a local variable. |
| L5 | `OpenGraphFetcher.cpp` | 112 | Redundant `payload.left(maxBytes)` — `payload` is already `maxBytes+1`. |
| L6 | `WindowsSpeller.cpp` | 38 | `CoInitializeEx` return value ignored — potential COM mode conflict with Qt's initialization on some Windows configurations. |

---

## SECURITY ISSUES

| # | Severity | File | Line(s) | Issue |
|---|----------|------|---------|-------|
| S1 | **Low-Medium** | `ChatLineFormatter.cpp`, `IrcFormat.cpp` | Multiple | **XSS in color/style HTML attributes.** `defaultForeground`, `defaultBackground`, `bodyColor` from user settings are inserted unsanitized into `<span style="color:...">`. A `"` in the value breaks out of the attribute. Risk is low since it's local client settings, but would be exploitable if themes are ever loaded from external sources. Should use `toHtmlEscaped()`. |
| S2 | Low | `ChatLineFormatter.cpp` | 118-119 | `www.` URLs linked as relative paths (duplicate of L1 from security angle). |
| S3 | Informational | `IrcSession.cpp` | 148-150 | CR/LF stripping prevents IRC command injection — good. |
| S4 | Informational | `IrcSession.cpp` | 561-563 | CTCP reply rate limiting prevents reflection attacks — good. |
| S5 | Informational | `IrcSession.cpp` | 1014-1025 | Plaintext password protection (`passwordAuthAllowed`) — good. |
| S6 | Informational | `IrcRedaction.cpp` | 9-28 | Password redaction in logs — good. |
| S7 | Informational | `McDataCodec.cpp` | 28-40 | MC DATA token validation restricts chars — good. |
| S8 | Informational | `ImageFetcher.cpp` | (multiple) | SSRF gate on every redirect hop — good. |
| S9 | Informational | `LinkPreviewRenderer.cpp` | (multiple) | `toHtmlEscaped()` on all user content; canonical URL checked for `http`/`https` only — good. |
| S10 | Informational | `ImgurUploader.cpp` | 27 | CR/LF removal from Client-ID prevents HTTP header injection — good. |
| S11 | Informational | `LuaEngine.cpp` | (multiple) | Robust sandboxing (limited libs, no bytecode, `os.exit` removed, `package.loadlib` nil'd, `io.open` guarded) — good. |

---

## PERFORMANCE ISSUES

| # | File | Line(s) | Issue |
|---|------|---------|-------|
| P1 | `src/core/DefaultNetworks.cpp` | 21-1509 | `defaultNetworks()` returns ~90-element `QList<NetworkDefaults>` by value every call. Could return `static const` reference. |
| P2 | `src/core/SettingsStore.cpp` | 283-452 | `defaultSettings()` returns ~90-key `QVariantMap` by value every call. |
| P3 | Scattered | Multiple | **Regex objects constructed per-call** in many functions (`CommandAlias.cpp`, `CommandParser.cpp`, `ConnectionPlan.cpp`, `ChatLogStore.cpp`, `IrcRedaction.cpp`, `IrcSession.cpp`). Should be `static const QRegularExpression`. |
| P4 | `src/core/ChatBufferStore.cpp` | 34-44 | `sortedUniqueMembers()` uses O(n²) dedup (scanning growing list with `containsNick` + sort). Could use `QSet`. |
| P5 | `src/core/ChatBufferStore.cpp` | 61-64 | `setMaxLinesPerBuffer()` uses O(n²) `removeFirst()` in loop instead of `erase()`. |
| P6 | `src/comic/ComicEmotion.cpp` | (multiple) | Repeated `QString::fromUtf8()` conversions of literal strings/emoji in hot path. Should use static `QLatin1String` or pre-allocated static QStrings. |

---

## CODE QUALITY ISSUES

| # | File | Line(s) | Issue |
|---|------|---------|-------|
| Q1 | `src/core/ChatLineFormatter.cpp` | 130-134 | `stripFormatting(text)` called twice (else branch + linkifyUrls). Could compute once. |
| Q2 | `src/core/ChatLineFormatter.cpp` | 139-140, 146, 189, 258 | Color values inserted into HTML `style` attributes without `toHtmlEscaped()`. |
| Q3 | `CMakeLists.txt` | 161 | Redundant `set_target_properties(maxchat_upload PROPERTIES AUTOMOC ON)` — `CMAKE_AUTOMOC` is globally ON. |
| Q4 | `CMakeLists.txt` | 27 | `find_package(Qt6 ... REQUIRED ... Test)` requires Qt6::Test even for non-test builds. Should be conditional. |
| Q5 | `src/comic/ComicRenderer.cpp` | (multiple) | Fragile global mutable state (`g_comicFontFamily`, `g_actionIncludeName`) set per render. |
| Q6 | `src/comic/ComicCharacter.cpp` | 201-213 | Static character cache never frees `new`'d Character objects (intentional session cache, but worth noting). |
| Q7 | `src/core/IrcFormat.cpp` | 190, 193, 214-216 | Color values from themes inserted unsanitized into HTML `style` attribute values. |
| Q8 | `src/services/ImageFetcher.cpp`, `OpenGraphFetcher.cpp` | (multiple) | `normalizedMaxBytes()` / `normalizedTimeoutMs()` duplicated across both files. Extract to shared utility. |

---

## TODO / FIXME / HACK COMMENTS

| # | File | Line | Comment |
|---|------|------|---------|
| T1 | `src/core/SettingsStore.cpp` | 436-438 | `// TODO(release): flip this default to true when going live` — `update_check` default is `false`. |

---

## DOCUMENTATION / PROJECT FILE ISSUES

### URGENT (actively misleading)

| # | File | Issue |
|---|------|-------|
| D1 | `README.md` | **Severely outdated.** Says Comic/DCC/Multimedia/notifications not ported — all are done. No mention of Lua scripting, themes expansion, upload service, MC DATA BBS, terminal subsystem. |
| D2 | `STATUS.md` | **Severely outdated (last updated 2026-06-09).** Lists Comic Mode, DCC, preferences UI, full commands as "not ported". Missing ~25 newer tests. No mention of Lua/terminal/BBS/themes/upload. |
| D3 | `PORT_PLAN.md` | Milestones 7 (DCC), 8 (Comic), 9 (Scripting) still described as future — all fully done. Target-link rules say Multimedia "deferred". |

### MEDIUM PRIORITY

| # | File | Issue |
|---|------|-------|
| D4 | `MC_DATA_PHASES.md` (in DEVDOCS/) | **Phase 11 header says "DONE 2026-06-12" but all 12 checklist items are unchecked.** Self-contradictory. |
| D5 | `DEV_NOTES.md` | "PARITY GAP DEEP DIVE vs Python app" section (lines 529-573) lists almost everything as missing — all now implemented. Severely misleading. |
| D6 | `HANDOFF.md` | Structural chaos: multiple "Latest Completed Slice" headers. "Still Deferred" section completely wrong (lists Comic Mode, DCC, WAV/MP3/video — all done). |
| D7 | `.gitignore` | Missing internal docs (`AUDIT.md`, `DEV_NOTES.md`, `DEVDOCS/`, `GUI.MD`, design docs, `PORT_PLAN.md`) and `dist-win/` per release-hygiene rule in AUDIT.md. |

### LOW PRIORITY

| # | File | Issue |
|---|------|-------|
| D8 | `AUDIT.md` | Test counts stale (says 53, actual 56). Missing 2026-06-12 content (MC DATA BBS, themes pack, DCC hardening). |
| D9 | `THIRD_PARTY_NOTICES.md` | Lua section says "When built with scripting enabled (`-DMAXCHAT_LUA=ON`)" — scripting is now ON by default. |
| D10 | `SCRIPTING_DESIGN.md` | Windows-build gate note says build with `-DMAXCHAT_LUA=ON` — Lua is now default. |
| D11 | `sync-to-win.sh` | References `WINDOWS_SETUP.md` which does not exist (safely guarded but misleading dangling reference). |
| D12 | `GUI.MD` | Minor: Mislabel of "Disconnect All" / "Reconnect All" (call current-server, not all-networks). Two consecutive separators in View menu. Trailing separator in Operator submenu. Stale placeholder text in MainWindow.cpp reference. |

---

## TEST ISSUES

| # | File | Issue |
|---|------|-------|
| X1 | `main_window_link_preview_test.cpp:33` | `#define private public` (duplicate of C1, listed here for tests section) |
| X2 | Various | 16 test targets compile sources directly rather than linking a library. Necessary given the architecture (UI components compiled into executable, not library), but fragile and increases compile time. |
| X3 | Various | All `QTEST_MAIN` / `QTEST_GUILESS_MAIN` / `QTEST_APPLESS_MAIN` variants used correctly based on each test's needs. |

---

## POSITIVE HIGHLIGHTS

- **Extensive test coverage:** 56 unit tests, with good use of `offscreen` platform for headless CI.
- **Strong security posture:** CR/LF stripping, SSRF protection, sandboxed Lua engine, password redaction, CTCP rate limiting.
- **Good Qt practices:** `QPointer` guards in deferred calls, `[[nodiscard]]` on public APIs, re-entrancy guards, proper parent ownership in UI.
- **Well-documented design docs:** `GUI.MD`, `CHAT_VIEW_DESIGN.md`, `MC_DATA.md` are excellent references.
- **Comprehensive build system:** Cross-platform CMake with vendored dependencies, `build.bat` for Windows, `sync-to-win.sh` for deployment.
- **Clean code organization:** Logical separation by domain (core/irc/ui/services/upload/spell/scripting/comic). Consistent naming conventions.

---

## SUMMARY STATISTICS

| Metric | Value |
|--------|-------|
| Critical bugs | 1 |
| Medium bugs | 5 |
| Low bugs | 6 |
| Security issues (low-medium) | 2 |
| Security hardening (good) | 9 |
| Performance issues | 6 |
| Code quality issues | 8 |
| TODO/FIXME/HACK | 1 |
| Documentation issues (urgent) | 3 |
| Documentation issues (medium) | 4 |
| Documentation issues (low) | 5 |
| Test issues | 3 |
| **Total actionable items** | **~44** |

---

## TOP RECOMMENDATIONS

1. **Fix `#define private public`** in `main_window_link_preview_test.cpp` — replace with `friend class` or use `QTest` accessors.
2. **Add `#pragma once`** to `AppInfo.h`.
3. **Add `allowedDirs` serialization** to `ScriptPermissions.h`.
4. **Fix IPv6 parsing** in `DefaultNetworks.cpp` — use `lastIndexOf(':')` only on bracketed hosts or use `QUrl`.
5. **Fix Hunspell `isCorrect()`** to return `true` when no dictionary is loaded (match WindowsSpeller behavior).
6. **Add `http://` prefix** for `www.` URLs in `ChatLineFormatter.cpp`.
7. **Update README.md, STATUS.md, PORT_PLAN.md** to reflect current implementation state.
8. **Add internal docs to `.gitignore`** before any public release.
9. **Make regex objects `static const`** in hot-path functions.
10. **Fix `MC_DATA_PHASES.md` Phase 11** header/checkbox contradiction.
