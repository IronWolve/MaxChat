# MaxChat C++ — Full Source Audit

**Date:** 2026-06-15
**Scope:** All `src/` files — 130+ files, 35,102 lines of C++/Qt
**Auditors:** 6 parallel subagents (irc, core, MainWindow/Dcc/Preferences, ThemeCatalog+dialogs, smaller UI, scripting/services/upload/spell)

---

## Executive Summary

| Category | Critical | High | Medium | Low |
|----------|----------|------|--------|-----|
| SECURITY | 2 | 5 | 9 | 2 |
| BUGS | 2 | 6 | 14 | 8 |
| PERFORMANCE | 0 | 3 | 10 | 5 |
| STYLE | 0 | 1 | 4 | 8 |
| QT-SPECIFIC | 0 | 2 | 3 | 2 |
| ROBUSTNESS | 0 | 4 | 8 | 4 |
| **TOTAL** | **4** | **21** | **48** | **29** |

**Top 5 Most Critical Issues:**
1. Use-after-free in LuaEngine `fireTimer` (scripting/LuaEngine.cpp:778–790)
2. `os.execute` + `send_raw` without permission gates — unrestricted shell+IRC access (scripting/LuaEngine.cpp:552, 227)
3. Path traversal in ThemeCatalog `deleteUserAppTheme` (ui/ThemeCatalog.cpp:1015)
4. Missing SSRF check on image anchor clicks (ui/MainWindow.cpp:7450)
5. `acceptInvalidCertificate_` accepts ALL SSL errors silently (irc/IrcConnection.cpp:362)

---

## 1. SECURITY

### S1. Lua sandbox escape via `os.execute` — CRITICAL
**File:** `scripting/LuaEngine.cpp:552–557`
When `runPrograms` permission is granted, `os.execute` remains available alongside `api.launch`. `os.execute` provides full shell access including stdin/stdout piping and command chaining (`&&`, `||`, `;`). The permission system's granularity is misleading — `runPrograms` effectively means unrestricted shell access.

```cpp
if (!perms.runPrograms) {
    for (const char* fn : {"execute", "getenv", "remove", "rename", "tmpname", "setlocale"}) {
```

### S2. `api.send_raw` has no permission gate — CRITICAL
**File:** `scripting/LuaEngine.cpp:227–230`
Every loaded script can call `api.send_raw()` to inject arbitrary IRC protocol commands (flooding, nickname takeover, kick/ban exploitation). No permission check exists.

### S3. `l_launch` Windows argument re-quoting incomplete
**File:** `scripting/LuaEngine.cpp:160–171`
Only `"` characters are escaped before passing to `ShellExecuteW`. Backslashes preceding quotes and other special characters are not handled.

### S4. `l_http_get` has no URL validation or timeout
**File:** `scripting/LuaEngine.cpp:437–448`
Scripts with `network` permission can call any URL with no timeout, enabling slow-loris style attacks or resource exhaustion.

### S5. `l_read_file` has no size limit
**File:** `scripting/LuaEngine.cpp:204–214`
Scripts can read arbitrarily large files into memory via `f.readAll()`.

### S6. `acceptInvalidCertificate_` accepts ALL SSL errors — HIGH
**File:** `irc/IrcConnection.cpp:362–368`
Calls `socket->ignoreSslErrors()` — silently accepts ALL SSL errors including compromised certificates and MITM. Consider at minimum logging which specific errors were ignored.

### S7. NickServ password in plaintext on non-TLS
**File:** `irc/IrcSession.cpp:413–423`
NickServ IDENTIFY sent in plaintext on non-TLS connections when `passwordAuthAllowed()` returns true. Deliberate design choice documented by warning, but risky.

### S8. Path traversal in `deleteUserAppTheme` — HIGH
**File:** `ui/ThemeCatalog.cpp:1015, 1024`
`isUserThemeId(id)` only checks `startsWith("u-")`. A crafted id like `"u-../../etc"` would pass and traverse directories via `QFile::remove`. Should validate with regex `^u-[a-z0-9-]+$`.

### S9. Missing SSRF check on image anchor clicks — HIGH
**File:** `ui/MainWindow.cpp:7450`
`DirectImage` case in `handleChatAnchorClicked` has no SSRF check. Clicking an `<img>` link opens `ImageViewerDialog` without verifying it's not a private address. Attacker could embed `<img src="http://169.254.169.254/...">` to probe cloud metadata.

### S10. DNS rebinding in SSRF checks
**File:** `services/LinkPreviewClassifier.cpp:268–281, 300–332`
Between DNS resolution check and actual TCP connect, DNS record could be swapped (classic DNS rebinding). Well-known limitation of this pattern.

### S11. TOCTOU in SSRF redirect check
**File:** `services/ImageFetcher.cpp:80–92`
Async DNS resolution for redirects creates a window where multiple redirects could be in-flight simultaneously.

### S12. HTML injection via unsanitized canonical URL
**File:** `services/LinkPreviewRenderer.cpp:79–83`
`card.canonicalUrl` from attacker-controlled OG tags embedded into `href` attributes. Edge cases with crafted Unicode may bypass `toHtmlEscaped()`.

### S13. API key in URL query parameter
**File:** `upload/ImgbbUploader.cpp:44–47`
API key appears in URL, may be logged by proxies/CDNs. Limitation of imgbb API design.

### S14. No URL scheme validation before `openUrl`
**File:** `ui/ServerListDialog.cpp:374`
No validation that website is a valid URL. Could open `javascript:` or other dangerous schemes.

### S15. `safePathPart` fragile path traversal guard
**File:** `core/ChatLogStore.cpp:20–30`
Safety relies on combination of regex + dot-stripping + empty-check rather than an explicit path traversal guard.

### S16. No validation of imported NetworkConfig values
**File:** `core/NetworkImport.cpp:29–66`
Imported network configs are arbitrary `QVariantMap`s with no type validation.

### S17. Channel key not validated for spaces/special chars
**File:** `ui/ChannelModesDialog.cpp:209`
`applyMode(QStringLiteral("+k %1").arg(key_))` — if `key_` contains spaces, mode string could be malformed.

---

## 2. BUGS

### B1. Use-after-free in `fireTimer` — CRITICAL
**File:** `scripting/LuaEngine.cpp:778–790`
If a timer callback calls `api.cancel_timer(id)` (which deletes the `ScriptTimer` entry) and then the Lua function errors, `lua_pcall` returns non-OK and the error handler accesses the freed `entry`.

### B2. `ScriptPermissions::toMap/fromMap` drops `allowedDirs` — HIGH
**File:** `scripting/ScriptPermissions.h:30–48`
`allowedDirs` is not serialized in `toMap()` and not restored in `fromMap()`. Permissions saved to settings lose the allowed directories list permanently.

### B3. "Disconnect All"/"Reconnect All" only affect active network — HIGH
**File:** `ui/MainWindow.cpp:1449–1693`
Menu labels promise all-network operation but `disconnectFromCurrentServer()` and `reconnectCurrentServer()` only affect the active network. Real user-facing bug.

### B4. `registrationTimeoutMs` uses `connectTimeoutMs` — HIGH
**File:** `ui/MainWindow.cpp:9057`
`config.registrationTimeoutMs = plan.connectTimeoutMs` — copy-paste error; registration timeout should be separate and typically longer.

### B5. `noteUserActivity`/`triggerAutoAway` skip default connection — HIGH
**File:** `ui/MainWindow.cpp:6170–6195`
Only iterates `m_connectionsByNetwork`, skipping the primary `m_connection`. Away status won't clear on the primary connection.

### B6. `ansiRenderer.cpp` dead ternary — HIGH
**File:** `ui/AnsiRenderer.cpp:107`
`const int fgCode = code >= 100 ? code - 10 : code - 10;` — both branches identical. Works by coincidence. Should be `const int fgCode = code - 10;`.

### B7. Regex compiled every call in CommandAlias
**File:** `core/CommandAlias.cpp:27, 35, 57`
`QRegularExpression` constructed on every call to `splitSlashCommand`, `argumentTokens`, and `applyTemplate`. Should be `static` constants.

### B8. `sortedUniqueMembers` is O(n²)
**File:** `core/ChatBufferStore.cpp:36–43`
`containsNick` does `std::any_of` over the growing `out` list each iteration. For large channels with thousands of members, this is a real concern. Use `QSet<QString>` sidecar.

### B9. `l_append_file` ignores write errors
**File:** `scripting/LuaEngine.cpp:199`
`QFile::write()` return value is discarded. Disk-full or I/O errors silently ignored.

### B10. Re-entrancy risk in `dispatch` via `currentNetwork_`
**File:** `scripting/LuaEngine.cpp:939`
If a timer fires during dispatch, `currentNetwork_` shared state is overwritten.

### B11. `addChannelsBulk` has no deduplication
**File:** `ui/ChannelListDialog.cpp:210–221`
Bulk insert can create duplicate channel entries (unlike `addChannel` which checks).

### B12. Null pointer dereference in DccTransfersDialog
**File:** `ui/DccTransfersDialog.cpp:114–115, 126`
`table_->item(row, N)` not null-checked before `->data()`.

### B13. `parseServerSpec` accepts port 0
**File:** `core/ConnectionPlan.cpp:1536–1540`
No range validation — port of 0 passes `ok` check. Should validate `> 0 && <= 65535`.

### B14. `recentLines` reads entire file into memory
**File:** `core/ChatLogStore.cpp:118–125`
For large log files (years of chat), reads every line keeping only last N. O(n) disk reads and O(n) memory. Should use file seek approach.

### B15. `mergeImportedNetworks` doesn't deduplicate
**File:** `core/NetworkImport.cpp:42–56`
Duplicate imported keys result in both a merged version and a raw version in output.

### B16. `FloodGuard::messageTimes_` grows unbounded for distinct keys
**File:** `core/FloodGuard.cpp:30`
Each unique key gets its own `QList<qint64>`. Stale entries only cleaned per-key on access.

### B17. `linkifyUrls` replacement loop can re-match inside `<a>` tags
**File:** `core/ChatLineFormatter.cpp:113–122`
Guard only checks for `"` before the match. Other HTML contexts are unprotected.

### B18. `SettingsStore` cache invalidation race
**File:** `core/SettingsStore.cpp:116–118`
Two rapid saves within same millisecond with same size could use stale cache.

### B19. DccManager `offerChat` wrong error message
**File:** `ui/DccManager.cpp:852–909`
"file transfers are disabled" for a chat offer — misleading.

### B20. DccManager chat buffer not flushed on disconnect
**File:** `ui/DccManager.cpp:1082–1093`
Partial data retained in buffer after peer disconnect.

### B21. `PreferencesDialog` null dereference if tab not built
**File:** `ui/PreferencesDialog.cpp:394–398`
`linkImages_`, `linkMedia_`, `linkXCards_`, `linkWebCards_` dereferenced without null checks.

### B22. `normalizedHost` incomplete prefix stripping
**File:** `services/LinkPreviewClassifier.cpp:21–27`
Only strips `www.` and `mobile.` — missing `m.`, `amp.`, `blog.`, `app.`.

### B23. `ImageFetcher` Content-Type check too strict
**File:** `services/ImageFetcher.cpp:112–115`
Valid images served with `application/octet-stream` are rejected.

### B24. `OpenGraphFetcher` duplicated helpers
**File:** `services/OpenGraphFetcher.cpp:20–24`
`normalizedMaxBytes` and `normalizedTimeoutMs` are identical copies of ImageFetcher.cpp versions.

### B25. `WindowsSpeller::isCorrect` returns true on API failure
**File:** `spell/WindowsSpeller.cpp:86`
Spell check API failures silently treated as correct spelling.

### B26. `MediaPlayerDialog`/`AudioPlayerBar` qint64→int truncation
**File:** `ui/MediaPlayerDialog.cpp:48, 52` / `ui/AudioPlayerBar.cpp:58, 64`
`static_cast<int>(duration)` truncates `qint64`. Not practical concern but technically lossy.

---

## 3. PERFORMANCE

### P1. `buffer_.contains("\r\n")` is O(n) on every readyRead — HIGH
**File:** `irc/IrcConnection.cpp:299`
Called on every `readyRead`. Under flood conditions (approaching 4MB), this could add up. Should use a tracked flag.

### P2. `SpellTextEdit::paintEvent` re-runs spellchecker per-block per-paint — HIGH
**File:** `ui/SpellTextEdit.cpp:38–87`
Calls `misspelledWordRanges()` for every text block on every paint event. O(blocks × words) per frame. Significant perf concern vs QSyntaxHighlighter approach.

### P3. `refreshComic()` re-reads settings per-nick — HIGH
**File:** `ui/MainWindow.cpp:8215–8499`
`comicCharacterForNick()` calls `m_settings.loadWithDefaults()` for every unique nick, causing repeated JSON parsing.

### P4. `applyCurrentSettings()` parses JSON 5 times
**File:** `ui/MainWindow.cpp:9377–9683`
Reads and parses entire settings file roughly 5 times during a single call.

### P5. `setMaxLinesPerBuffer` / `recentLines` use `removeFirst()` in loop
**File:** `core/ChatBufferStore.cpp:61–63` / `core/ChatLogStore.cpp:118–125`
`QList::removeFirst()` is O(n) per call. Should use `QList::remove(0, excess)`.

### P6. `supportedCommandNames()` allocates on every call
**File:** `irc/CommandParser.cpp:103–150`
Returns new `QStringList` every call. Used for tab-completion — should be `static` or cached.

### P7. `mircColors()` uses QHash for sequential int keys
**File:** `irc/IrcFormat.cpp:9–63`
99 sequential integer keys. `std::array<QString, 99>` would be faster and more cache-friendly.

### P8. `rememberUrlsFromLine()` O(n*m) dedup
**File:** `ui/MainWindow.cpp:7003–7041`
Linear scan of `m_urlList` for every URL. Could use `QSet<QString>` alongside for O(1).

### P9. `loadPrefs` reads from disk on every `api.get` call
**File:** `scripting/LuaEngine.cpp:383`
Each `api.get(key)` opens, reads, and parses entire prefs.json. Same for `api.set`.

### P10. `ScriptTerminalDialog` paste guard UTF-8 copy
**File:** `ui/ScriptTerminalDialog.cpp:67`
`text.toUtf8().size()` creates full UTF-8 copy just to measure paste size.

### P11. `TerminalFrame::readUtf8Text` per-char encoding
**File:** `ui/TerminalFrame.cpp:62–69`
Each character individually converted: `QString(ops.at(*pos)).toUtf8()`. Creates N temporary `QByteArray` objects.

### P12. `directorySizeBytes()` walks entire log dir on dialog open
**File:** `ui/PreferencesDialog.cpp:57–68`
Recursively walks all logs. For years of logs, could take seconds.

### P13. `ChannelListDialog::addChannel` calls `sortItems()` per row
**File:** `ui/ChannelListDialog.cpp:195–199`
O(n² log n) total for streaming LIST results.

### P14. `DccTransfersDialog::refresh()` rebuilds entire table every second
**File:** `ui/DccTransfersDialog.cpp:176–213`
Creates/destroys all `QTableWidgetItem`s each second. Full transfer list copied by value.

### P15. `languageSpecs()` allocates new list on every call
**File:** `spell/SpellcheckDictionaryCatalog.cpp:16–76`
Should be `static const`.

### P16. `ComicView::panelRects()` recomputed on every paint + hit test
**File:** `ui/ComicView.cpp:158–163, 140`
Double computation per click + repaint. Could cache when size changes.

### P17. `AppIcon::emojiFamilies()` creates new QStringList each call
**File:** `ui/AppIcon.cpp:10–16`
Could be `static const`.

---

## 4. ROBUSTNESS / MISSING ERROR HANDLING

### R1. `l_http_get` cannot distinguish empty body from failure
**File:** `scripting/LuaEngine.cpp:440–444`
Both return `nil`. Scripts have no way to tell if request failed or returned empty body.

### R2. Hunspell is not thread-safe
**File:** `spell/HunspellSpellchecker.cpp`
`Hunspell` instances are not thread-safe. No documentation or enforcement.

### R3. COM initialization leak in WindowsSpeller
**File:** `spell/WindowsSpeller.cpp:38`
`CoInitializeEx` called without matching `CoUninitialize`.

### R4. `--selftest` not removed from argv
**File:** `main.cpp:58–65`
Flag remains in `argv` after detection. Could affect downstream code.

### R5. `importSettings()` no size limit on imported JSON
**File:** `ui/MainWindow.cpp:3853–3909`
Multi-gigabyte file would be read entirely into memory at line 3860 (`file.readAll()`).

### R6. `checkForUpdates()` dangling `this` in lambda
**File:** `ui/MainWindow.cpp:2551–2596`
Network reply connected to `this` — if window closes while in-flight, lambda references destroyed object.

### R7. `renderActiveBuffer()` full re-render on every switch
**File:** `ui/MainWindow.cpp:7234–7318`
No early-exit or incremental rendering. Expensive for thousands of buffered lines.

### R8. `DccManager::destPath()` fallback can clobber existing files
**File:** `ui/DccManager.cpp:264–273`
If all 10,000 slots taken, final fallback returns original candidate even if it exists.

### R9. `DccManager::finishTransfer()` leaves `.part` on rename failure
**File:** `ui/DccManager.cpp:764–811`
`QFile::rename()` failure leaves `.part` file behind with no cleanup.

### R10. `GeometryPersist` lambda captures `&store` by reference
**File:** `ui/GeometryPersist.cpp:19–21`
Potential dangling if `SettingsStore` outlives dialog.

### R11. `ImgboxUploader` no timeout on token creation
**File:** `upload/ImgboxUploader.cpp:47–48`
Unlike image fetchers, token request has no timeout mechanism.

### R12. `chatLogStore::appendLine` doesn't lock the file
**File:** `core/ChatLogStore.cpp:96–104`
Concurrent writes can interleave.

### R13. `DccTransfersDialog` signal connection with no disconnect
**File:** `ui/DccTransfersDialog.cpp:141`
If `manager_` is destroyed before dialog, crashes.

### R14. `MainWindow` notification probe dangling `this`
**File:** `ui/MainWindow.cpp:864–891`
QProcess lambda captures `this` — if MainWindow destroyed before process finishes.

---

## 5. STYLE / MAINTAINABILITY

### God Class: MainWindow — 9,853 lines
**File:** `ui/MainWindow.cpp`
Handles IRC connections, comic rendering, DCC transfers, scripting, notifications, spellcheck, tray icon, link previews, and menu construction. 200+ private methods. Should be decomposed.

### Dead Code: Duplicate command dispatch
**File:** `ui/MainWindow.cpp:5258–5631`
If-chain and switch statement handle the same command types. Switch duplicates are dead code.

### FriendsNotifyDialog ≈ IgnoreListDialog
**Files:** `ui/FriendsNotifyDialog.cpp` vs `ui/IgnoreListDialog.cpp`
Near-identical structure — significant code duplication. Candidate for generic list dialog template.

### Duplicate `splitFirstToken` with different semantics
**Files:** `irc/McDataCodec.cpp:12` vs `irc/CommandParser.cpp:14`
Two different functions with same name, different trim behavior. Confusing.

### `handleLine()` is 678 lines with ~60 branches
**File:** `irc/IrcSession.cpp:334–1012`
Consider extracting numeric handlers into a map for maintainability.

### Magic Numbers Throughout
- `1000` (ChatBufferStore maxLines), `6667`/`6697` (default ports), `15000` (timeout)
- `315` (comic panel size), `4`/`40` (nick column width bounds), `8` (alias maxDepth)
- `86400` (seconds/day in SystemInfo), `340` (toast width)

### Duplicate `separatorPlain`/`separatorHtml`
**File:** `core/ChatLineFormatter.cpp:26–34`
Both return `" "` with unused `options` parameter.

### Duplicate padding logic
**File:** `core/ChatLineFormatter.cpp:208–220 vs 231–236`
Could extract shared helper.

### 167 ungrouped member variables in MainWindow.h
**File:** `ui/MainWindow.h:420–586`
No section comments — nearly impossible to navigate.

### 132 raw widget pointers in PreferencesDialog.h
**File:** `ui/PreferencesDialog.h:79–210`
Consider grouping into small structs (e.g., `FontControls { family; size; bold; }`).

---

## 6. Qt-SPECIFIC

### No thread safety documentation/contracts
**Files:** `core/ChatBufferStore.h`, `core/SettingsStore.h`, `spell/HunspellSpellchecker.cpp`
All classes lack mutex. If accessed from multiple threads, data races occur. Undocumented single-thread contract.

### Mutable static singletons not thread-safe
**File:** `ui/ThemeCatalog.cpp:443–451`
`mutableAppThemes`, `mutableChatThemes` use `static` locals. `reloadThemes()` not atomic.

### `DccManager::rt()` uses `operator[]` on QHash
**File:** `ui/DccManager.cpp:122`
Inserts default if key is missing — could silently create empty Runtime objects. Should use `.value()`.

### `ChatBufferStore` dual data structures can drift
**File:** `core/ChatBufferStore.cpp:88–94`
`order_` and `buffersByKey_` could become inconsistent. Single `QHash` with insertion-order simpler.

### `QHash<QString, ChatRuntime*>` raw pointers
**File:** `ui/DccManager.h:149`
Manual deletion in destructor. Consider `std::unique_ptr`.

---

## Appendix: Files Audited

| Module | Files | Lines |
|--------|-------|-------|
| src/irc/ | 16 | ~3,415 |
| src/core/ | 20 | ~3,244 |
| src/ui/ (MainWindow, Preferences, DccManager) | 6 | ~14,461 |
| src/ui/ (ThemeCatalog + dialogs) | 16 | ~4,066 |
| src/ui/ (smaller dialogs) | 52 | ~5,000+ |
| src/scripting/ | 4 | ~1,431 |
| src/services/ | 12 | ~1,030 |
| src/upload/ | 10 | ~500 |
| src/spell/ | 6 | ~522 |
| src/main.cpp | 1 | 103 |
| src/app/ | 2 | 33 |
| **Total** | **~130** | **~35,102** |
