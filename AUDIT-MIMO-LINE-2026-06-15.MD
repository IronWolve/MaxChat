# MaxChat-C Full Line-by-Line Audit (MiMo Auto)
**Date**: 2026-06-15  
**Auditor**: MiMo Auto (6 parallel subagents)  
**Scope**: Every source file, test, build config, Lua script, and documentation file — line by line  
**Total findings**: ~300+ across all severity levels

---

## Critical Findings (8)

| # | File | Line | Finding |
|---|------|------|---------|
| 1 | `src/core/SettingsStore.cpp` | 112-138, 150-181 | **Thread safety**: `loadRaw()` and `saveRaw()` access mutable cache fields (`cachedRaw_`, `cachedMtime_`, `cachedSize_`, `cacheValid_`) without synchronization — data race if called from multiple threads (UI + worker) |
| 2 | `src/core/ChatLineFormatter.cpp` | 145-147 | **HTML injection via `colorSpan`**: `color` parameter injected into `style` attribute without escaping. If `options.timestampColor`, `options.bracketColor`, or `options.systemColor` contain `"` or style-breaking characters, it breaks the HTML structure |
| 3 | `src/core/ChatLogStore.cpp` | 86-105 | **No file locking in `appendLine`**: If two processes (or threads) append to the same log file simultaneously, lines can interleave or corrupt. The Python app shares this file |
| 4 | `src/ui/MainWindow.cpp` | 2826-2869 | **`scriptHttpGet` blocks UI thread**: `QEventLoop` blocks the entire window for up to 10 seconds per script HTTP call. DNS resolution hangs or slow servers make the UI unresponsive |
| 5 | `src/ui/PreferencesDialog.cpp` | 414-428 | **Plaintext credential storage**: API keys (`imgbb_api_key`, `imgur_client_id`, `postimages_token`, `imgbox_password`) stored in plain text in `QVariantMap` settings saved to JSON file on disk with no encryption |
| 6 | `assets/scripts/bbs.lua` | 26-27 | **Hardcoded credentials**: `DEMO_USER = "sir_iw"` and `DEMO_PASSWORD = "bbsiscool"` in source, used for plaintext password comparison over IRC (CTCP MC DATA) |
| 7 | `assets/scripts/memo.lua` | 83 | **False success message**: `api.echo("Memo saved for " .. target .. ".")` displayed even if `save_memos` returned without writing (nil file handle). Memo appears saved but is lost |
| 8 | `assets/scripts/run.lua` | 26 | **Arbitrary program execution**: `api.launch(prog)` executes ANY program with no validation beyond the "Run programs" permission checkbox |

---

## High-Priority Findings (20)

| # | File | Line | Finding |
|---|------|------|---------|
| 1 | `src/core/ChatLogStore.cpp` | 107-127 | **O(n) tail read**: `recentLines` reads every line to keep last `maxLines` — for 100MB log file, reads 100MB. Should seek from end |
| 2 | `src/irc/IrcConnection.cpp` | 358-369 | **SSL error list not cleared**: When `acceptInvalidCertificate_` is true, `socket->ignoreSslErrors()` called but errors remain — some Qt versions re-emit the signal |
| 3 | `src/irc/IrcSession.cpp` | 1106-1113 | **SASL mechanism not negotiated**: Client assumes `PLAIN` is always supported after CAP ACK — some servers require `EXTERNAL`. Should negotiate mechanism |
| 4 | `src/ui/MainWindow.cpp` | 1459-1460 | **Disconnect All calls single disconnect**: Same method as single "Disconnect" action — should disconnect all networks |
| 5 | `src/ui/GeometryPersist.cpp` | 19 | **Dangling reference risk**: Lambda captures `&store` by reference — safe only because MainWindow outlives all dialogs, but pattern is fragile |
| 6 | `src/ui/ThemeCatalog.cpp` | 443-451, 579, 589 | **Thread-unsafe singletons**: Mutable `appThemes`/`chatThemes` lists not thread-safe; `.first()` on empty registry is UB |
| 7 | `src/ui/AnsiRenderer.cpp` | 107 | **Dead conditional**: Both branches of `code >= 100 ? code - 10 : code - 10` are identical — correct by accident |
| 8 | `src/ui/ServerListDialog.cpp` | 374 | **URL scheme injection**: `QDesktopServices::openUrl` with raw URL string allows `javascript:`/`file:` scheme — must validate scheme is http/https |
| 9 | `src/ui/BanListDialog.cpp` | 39 | **HTML injection**: `channel_` and `networkName` rendered unescaped in QLabel — XSS via crafted channel names |
| 10 | `src/ui/TerminalFrame.cpp` | 63 | **Non-BMP Unicode bug**: `readUtf8Text` assumes each QChar maps to single UTF-8 byte — surrogate pairs break byte counting in terminal protocol |
| 11 | `src/ui/DccManager.h` | 149 | **Raw pointer ownership**: `chats_` stores raw `ChatRuntime*` pointers — if signal/slot causes re-entrant deletion, pointers dangle |
| 12 | `src/ui/DccManager.cpp` | 158 | **False stall cancellation**: `runtimes_.value()` on transfers without runtime entries can cause false-positive stall cancellation |
| 13 | `src/ui/ScriptTerminalDialog.cpp` | 191-202 | **O(n²) text append**: `writeText` appends HTML to QTextBrowser — each append reflows entire document. Should use QTextDocument append |
| 14 | `src/ui/ScriptTerminalManager.cpp` | 78-92 | **Signal on dead dialog**: `deleteLater()` on kill means queued signals can fire on logically-dead dialogs |
| 15 | `src/scripting/LuaEngine.cpp` | — | **`api.http_get` blocks GUI thread**: Should be async or run on worker thread |
| 16 | `tests/main_window_link_preview_test.cpp` | 33-36 | **Undefined behavior**: `#define private public` breaks encapsulation — should use friend declarations or test accessors |
| 17 | `CMakeLists.txt` | 119+ | **Massive test source duplication**: 20+ test targets re-list source files instead of linking library targets — significant maintenance burden |
| 18 | `assets/scripts/memo.lua` | 31-38 | **Data loss on write failure**: `save_memos` uses `io.open(path, "w")` (truncate + write). Mid-write failure destroys existing memo file |
| 19 | `HANDOFF.md` | 684-689 | **Stale documentation**: "Still Deferred" lists Comic/DCC but both are fully complete |
| 20 | `SCRIPTING.md` | 63 | **Timer API misdocumented**: Says `api.timer(ms, fn)` calls `fn()` "every ms" (periodic), but scripts use it as one-shot |

---

## Medium-Priority Findings (50+)

### Core/IRC (12)
| File | Line | Finding |
|------|------|---------|
| `ChatBufferStore.cpp` | 34-44 | `sortedUniqueMembers` is O(n²) — should use QSet for dedup |
| `ChatBufferStore.cpp` | 61, 219 | `removeFirst()` is O(n) for QList — consider QDeque |
| `ChatLogStore.cpp` | 22-23 | `safePathPart` regex replaces `\` and `/` with `_` — Windows paths mangled |
| `ChatLogStore.cpp` | 56-65 | `logFilePath` mask replacement order: `%Y` before `%y` edge case |
| `ChatLineFormatter.cpp` | 22-34 | `separatorPlain` and `separatorHtml` are identical — merge or document |
| `CommandAlias.cpp` | 127-149 | Cycle detection is command-name-based — maxDepth limit prevents infinite loops |
| `UrlDetector.cpp` | 16-27 | `trimTrailingPunctuation` is O(n²) for bracket counting on long URLs |
| `NetworkImport.cpp` | 42-56 | `mergeImportedNetworks` doesn't handle duplicate imported keys |
| `FloodGuard.cpp` | 32-34 | Window cleanup is O(n) — acceptable for small maxMessages |
| `SettingsStore.cpp` | 141-148 | Shallow merge in `loadWithDefaults()` — doesn't deep-merge nested QVariantMaps |
| `IrcSession.cpp` | 334-1012 | `handleLine` is 700+ lines — candidate for dispatch table refactoring |
| `IrcFormat.cpp` | 136-168 | `stripFormatting` doesn't handle 3-digit mIRC colors (100+) |

### UI Part 1 (15)
| File | Line | Finding |
|------|------|---------|
| `MainWindow.h` | 26, 31 | Duplicate `#include <QSet>` |
| `MainWindow.cpp` | 1504-1505 | `viewMenu->addSeparator()` called twice — double separator |
| `MainWindow.cpp` | 434 | `m_previewImageCache` grows unbounded — no eviction policy |
| `MainWindow.cpp` | 189-194 | `addMember` does linear scan — O(n²) during NAMES sync |
| `PreferencesDialog.cpp` | 264-428 | `settings()` dereferences many widget pointers without null checks |
| `PreferencesDialog.cpp` | 57-68 | `directorySizeBytes` blocks UI thread for large log directories |
| `PreferencesDialog.cpp` | 1037 | `delete scriptDirs_->takeItem(-1)` — fragile if nothing selected |
| `PreferencesDialog.cpp` | 47 | `setComboByData` sets index 0 if not found — silent selection change |
| `SpellcheckHighlighter.cpp` | 84 | URL regex `\S+` is greedy — matches trailing punctuation |
| `ThemeCatalog.cpp` | 914 | `writeJsonFile` uses `QFile::write` without checking return value |
| `ThemeCatalog.cpp` | 157 | Theme files read entirely into memory — no size limit |
| `ToastWidget.cpp` | 16 | Fixed width 340px not DPI-aware |
| `SoundPlayer.cpp` | 64 | `effect_->setVolume(0.9)` hardcoded — should respect user preference |
| `main.cpp` | 35-52 | Static `QTranslator` objects never uninstalled |
| `GeometryPersist.cpp` | 20 | `saveGeometry().toBase64()` stored as QString — ~33% larger than raw bytes |

### UI Part 2 (20)
| File | Line | Finding |
|------|------|---------|
| `DccManager.cpp` | 114-118 | `newToken()` produces only 32-bit tokens — 64+ bits more robust |
| `DccManager.cpp` | 264 | `destPath` loop limit of 10000 — falls through to overwrite |
| `DccManager.cpp` | 354 | `QFile` parented to socket — close on unopened file is inconsistent |
| `DccManager.cpp` | 407 | `pumpSend` no guard against concurrent invocations |
| `DccTransfersDialog.cpp` | 177 | `refresh()` copies entire vector on every call |
| `DccTransfersDialog.cpp` | 176-213 | `refresh()` recreates all QTableWidgetItems — causes flicker |
| `MediaPlayerDialog.cpp` | 48 | `qint64` duration truncated to `int` slider range — overflow risk |
| `AudioPlayerBar.cpp` | 58 | Same `int` overflow risk as MediaPlayerDialog |
| `ChannelListDialog.cpp` | 210-234 | Filter logic duplicated between `addChannelsBulk` and `applyFilter` |
| `ComicView.cpp` | 109-112 | `setPanels` copies entire QPixmap vector — should use move semantics |
| `ComicView.cpp` | 158-163 | `panelRects()` recomputed on every paint — should cache |
| `ServerListDialog.cpp` | 208-209 | Passwords stored in plaintext QVariantMap |
| `BanListDialog.cpp` | 132-146 | Row removed before server confirms ban removal — UI out of sync |
| `RawLogDialog.cpp` | 46-59 | `setLines` creates huge string; `appendLine` triggers repaint per line |
| `ImageViewerDialog.cpp` | 22 | `MaxImageBytes` 25 MiB — decoded pixel data could be hundreds of MiB |
| `ScriptTerminalDialog.cpp` | 466-502 | HTML string built with concatenation for 3200-cell grid |
| `ScriptTerminalDialog.cpp` | 387-413 | Binary search creates new QFontMetrics per iteration |
| `SystemInfo.cpp` | 173-196 | `gpuName()` creates QOffscreenSurface — expensive, fails headless |
| `SystemInfo.cpp` | 251 | `QStorageInfo::mountedVolumes()` called every time — should cache |
| `AppIcon.cpp` | 18-46 | `bubblePixmap` creates new QPixmap/QPainter every call |

### Comic/Upload/Spell/Services/Scripting (10)
| File | Finding |
|------|---------|
| `ComicCharacter.cpp` | `loadCharacter()` static cache leaks all `Character*` objects |
| All uploaders | No request timeouts — add `QNetworkRequest::setTransferTimeout()` |
| `ScriptPermissions.cpp` | `toMap`/`fromMap` silently drop `allowedDirs` — serialization loses permissions |
| `ComicRenderer.cpp` | Global variables `g_comicFontFamily`, `g_actionIncludeName` are thread-safety hazard |
| `ImgboxUploader.cpp` | Stores plaintext credentials for object's lifetime — clear after token creation |
| `LuaEngine.cpp` | `api.http_get` blocks GUI thread |
| `bbs.lua` | `math.randomseed(os.time())` has 1-second resolution — identical seeds |
| `bbs.lua` | Password compared case-sensitively while user is case-insensitive |
| `seen.lua` | Unbounded preference growth per unique nick |
| `weather.lua` | Units hardcoded to US; no metric switch |

### Tests/Build (8)
| File | Finding |
|------|---------|
| `CMakeLists.txt` | `Qt6::MultimediaWidgets` required unconditionally — breaks headless CI |
| `CMakeLists.txt` | `HUNSPELL_FOUND` hardcoded to TRUE — misleading for future system Hunspell |
| `build.bat` | `RUN_TESTS` variable never set — tests never auto-run |
| `build.bat` | Qt version search list hardcoded — newer versions won't be found |
| `.gitignore` | Missing: `*.a`, `*.lib`, `*.pdb`, `CMakeCache.txt`, `CMakeFiles/`, `.idea/`, `.vscode/` |
| `open_graph_fetcher_test.cpp` | Missing `wait()` on async signal — potential flaky test |
| `default_networks_test.cpp` | Brittle exact-count assertion (146 networks) |
| `hunspell_spellchecker_test.cpp` | Silent `continue` instead of `QSKIP` for missing prerequisites |

### Documentation (8)
| File | Finding |
|------|---------|
| `README.md` | Lists Comic/DCC as "not ported" — both fully ported |
| `README.md` | Missing Qt::Multimedia and Qt::Svg from linked libraries list |
| `HANDOFF.md` | Three sections claim "Latest Completed Slice" — only line 6 is actual latest |
| `HANDOFF.md` | "Still Deferred: Comic Mode. DCC." — stale, both complete |
| `SCRIPTING.md` | Timer API documented as periodic; scripts use as one-shot |
| `SCRIPTING.md` | Bundled examples table lists 5 scripts; actual directory has 9 |
| `SCRIPTING_DESIGN.md` | Status says "IN PROGRESS" but all steps complete |
| `PORT_PLAN.md` | Milestone 9 says "defer scripting" but scripting is fully implemented |

---

## Low-Priority Findings (40+)

Key low-severity items across all modules:

- `MainWindow.h`: Duplicate forward declarations; diamond inheritance risk
- `SettingsStore.cpp`: Redundant `mkpath` if directory exists; `prepareImportedSettings` forces re-merge
- `ChatBufferStore.cpp`: `ensureBuffer` returns by value (small struct — fine)
- `FloodGuard.cpp`: `recordMessage` default-constructs empty list — correct Qt behavior
- `DefaultNetworks.cpp`: Large static data (1500+ lines); all networks share same default nick
- `IrcMessage.cpp`: `parseMessage` doesn't validate structure — acceptable for socket input
- `IrcFormat.cpp`: `toHtml` creates nested spans — verbose but correct
- `ReconnectPlanner.cpp`: `ServerRetryLimit` of 3 — reasonable default
- `TerminalProfile.cpp`: Unknown profile IDs silently get "free" default
- `SpellTextEdit.cpp`: No spellcheck for multi-line pastes — single-line input anyway
- `SpellcheckHighlighter.cpp`: `commandScanStart` only handles subset of commands
- `Notifier.cpp`: Toast stacking uses shallow copy — fine
- `SoundPlayer.cpp`: `resolveSoundPath` strips directory components — good security
- `main.cpp`: No signal handling for graceful shutdown — normal for desktop Qt apps
- `DccManager.cpp`: Constants in anonymous namespace — could be constexpr
- `ComicView.cpp`: `sheet()` uses Format_RGB32 — loses alpha
- `ComicView.cpp`: `panels_.at(i).save()` doesn't check return value
- `UrlListDialog.cpp`: `appendUrls` doesn't deduplicate
- `ChatFindDialog.cpp`: Signals emitted even with empty search text
- Various test files: Good happy-path coverage, limited edge-case testing
- `bbs.lua`: `server.pages` and `server.connects` counters never reset
- `dice.lua`: Output uses emoji — no font fallback
- `url_logger.lua`: No file size limit for `urls.log`
- `reminder.lua`: No maximum timeout guard
- `gen_sounds.py`: Assumes run from within project directory
- Multiple documentation files: Stale status sections, inconsistent test counts

---

## Cross-Cutting Patterns

1. **Thread safety is pervasive**: No class documents thread-safety contracts. SettingsStore, ThemeCatalog, LuaEngine all have unprotected mutable state
2. **Credential storage is plaintext**: API keys, passwords stored in QVariantMap → JSON file. No encryption or OS keychain integration
3. **UI thread blocking**: `scriptHttpGet`, `api.http_get`, `directorySizeBytes`, `gpuName()` all block the main thread
4. **Missing input validation**: Port ranges, channel names, URL schemes, program execution — validated too late or not at all
5. **Documentation drift**: README, HANDOFF, SCRIPTING, PORT_PLAN all have stale status sections
6. **Test infrastructure**: Source file duplication across 20+ test targets; `#define private public` UB; missing async test guards
7. **Performance anti-patterns**: O(n) tail reads, O(n²) dedup, copying vectors instead of moving, recreating objects per paint/call

---

## Priority Recommendations

### Immediate (Critical)
1. Add mutex/atomic to SettingsStore cache fields
2. HTML-escape color parameters in ChatLineFormatter
3. Add file locking or accept interleaving in ChatLogStore::appendLine
4. Move scriptHttpGet to async pattern (QNetworkAccessManager + signal)
5. Encrypt or OS-keychain-store API keys in PreferencesDialog
6. Remove hardcoded credentials from bbs.lua
7. Add save-success check to memo.lua
8. Add program validation/allowlist to run.lua

### Short-term (High)
1. Refactor IrcSession::handleLine into dispatch table
2. Validate URL schemes before QDesktopServices::openUrl
3. Escape HTML in BanListDialog channel/network display
4. Fix non-BMP Unicode handling in TerminalFrame
5. Add `wait()` to async test assertions
6. Link test targets against library targets instead of duplicating sources
7. Update stale documentation (README, HANDOFF, SCRIPTING, PORT_PLAN)

### Medium-term (Medium)
1. Add eviction to m_previewImageCache
2. Use move semantics in ComicView::setPanels
3. Cache QStorageInfo::mountedVolumes() and gpuName() results
4. Add DPI-awareness to ToastWidget width
5. Extract shared removeSelected pattern from IgnoreListDialog/FriendsNotifyDialog
6. Add QSKIP for missing test prerequisites
7. Fix brittle exact-count network assertions
