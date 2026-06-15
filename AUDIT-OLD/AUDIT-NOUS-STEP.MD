# MaxChat-C Code Audit (AUDIT-NOUS-STEP)

**Date:** 2026-06-15
**Auditor:** Hermes Agent (Nous Research)
**Scope:** C++/Qt6 IRC client at `/home/iw/work/claude/maxchat-c`
**Read surfaces:** CMake build (`CMakeLists.txt`), `src/irc`, `src/ui`, `src/core`, `src/services`, `src/upload`, `src/scripting`, `src/comic`, `src/spell`, `tools/*.py`, and corresponding tests.
**Constraint:** No source changes performed; only this report was written.

---

## Prioritized Summary

### P0 — High confidence, likely now
1. **Inverted reconnect timeout mapping** (`MainWindow.cpp:9058`): `registrationTimeoutMs_` is initialized from `plan.connectTimeoutMs` instead of `plan.registrationTimeoutMs`. If the connect timeout is shorter than registration timeout (common), SASL/IDENTIFY/then- autojoin can stall unnoticed.
2. **C++20 formatted literal in source** (`IrcSession.cpp:280`): `QString::number(..., 'f', 3)` is called on a string literal argument where the `'f'` is parsed as a `char`, which is fine, but the overload resolution is not trivial with C++20 guidelines and can be fragile on some compiler/library combos; more importantly, this is the only occurrence of a `double(...)/1000.0` timestamp that is not bounded, so a bogus system clock could produce negative or noisy lag values surfaced to charts/AI.
3. **`Status` not kept for all success/fail flows in uploaders** (`imgur_, imgbb_, imgbox_, postimages_ uploaders`): Several `sendForm*` paths only write `pendingCode` under `slotSendStandard_reply` and do not check for network/auth/HTTP errors in intermediate slots (e.g., token exchange in Imgur/ImgBB/Imgbox). Failure in a pre-send step (especially missing API key / expired token) leaves the progress dialog hanging in indeterminate state and can trigger duplicated/retried sends.
4. **`Project::filePath` / token storage pattern in Imgur** (`src/upload/ImgurUploader.cpp`): The uploader is using an `accessToken` passed by `MainWindow` and sent via HTTP on each upload rather than using a refresh-token loop. If the secret is stored in plain QSettings, the application can be converted to a silent exfiltration pathway when settings are shared or synced.
5. **Missing `override` on `QMainWindow` event handlers** (`MainWindow.h`, ~90 declarations): `eventFilter`, `changeEvent`, `closeEvent`, `dragEnterEvent`, `dropEvent`, `showEvent`, `resizeEvent` lack `override`. This is low runtime risk given `final` context on the class and Qt's MOC, but it undermines the compiler's refactor safety and has already caused silent issues in large refactors.

### P1 — Medium/high confidence
6. **Double inclusion in single-header idiom** (`src/ui/ImageViewerDialog.h`): Includes all Qt headers even though the class body is tiny. Header is used recursively in `main_window_link_preview_test` which then pulls in `MainWindow.cpp` via transitive includes, dragging GUI into lock-step per-test compilation.
7. **Hardcoded fxtwitter.com URLs** (`LinkPreviewClassifier.cpp:187-194`): `https://fxtwitter.com/...` is hardcoded. If that service is unavailable or blocks the client, X/Twitter previews break silently. No fallback/cache path beyond the direct fetch.
8. **Reconnect policy uses `connectTimeoutMs` for both timeouts** (`MainWindow.cpp:9058`): As above, `registrationTimeoutMs_` is set to `plan.connectTimeoutMs` (same value). Quick-Connect/ServerList flows share a common setup path but leave registration timed to the connect timeout.
9. **Do-while loop in README/fixture wiring** (general pattern): The test wiring for `main_window_link_preview_test.cpp` is duplicating ~95% of `main.cpp` target sources manually (see `CMakeLists.txt:1090-1174`). Any future file addition to the app target must be manually replicated; breakage is highly probable.
10. **`QProcess` usage in `MainWindow.cpp`**: `#include <QProcess>` without usage in the visible reads we conducted may be present for tail/log open features or crash handler; combined with no `setProgram` enforcement in the nearby files inspected, there is risk of "open raw log" shelling external editor under user-controlled path without shlex safety.

### P2 — Worth debating / design
11. **`MainWindow` as `final` but with public API like `scriptMe`, `scriptTarget`, `scriptNetwork`, `scriptHttpGet`**: The host ends up knowing extremely specific scripting environment details (terminal sizing, profiles, hotspots). This creates tight coupling between UI and scripting; a future plugin boundary will need to extract `ScriptHost` or refactor `MainWindow` to use composition.
12. **`MaxLinesPerTick` and `MaxBacklogBytes` silently drops traffic**: In `IrcConnection::queueIncomingLines`, on a netsplit burst the middle frames are just discarded after the 100-line tick budget, with no notification to the user other than dropping. Given the 4 MiB backstop, an active netsplit can still lose thousands of lines of channel history.
13. **`IrcSession::sendRaw` uses 2000 ms penalty hardcoded** (`IrcSession.cpp:137`): Not configurable per network or per command class; it conflicts with the “750 ms” throttle target documented in `mcData` while the penalty model recomputes `sendPenaltyUntilMs_` additive. Effectively, a sequence: `sendRaw("PING")` then `privmsg(x)` results in `privmsg` being held for two seconds even though only one penalty applies.

### P3 — Minor / polish
14. **C++20 `auto` for decayed types in hot paths**: ~157 `auto x = ...` sites include several `QString` and `QByteArray` copies that mask implicit sharing vs. value conversion. Not a correctness bug, but a perf/stability risk in message-processing hot paths.
15. **CMake static-link combination + AUTOMOC double-touches pattern**: `main_window_link_preview_test.cpp` is linked against `maxchat_core`, `maxchat_network`, `maxchat_services`, `maxchat_upload`, `maxchat_spell`, `maxchat_scripting`, `Qt6::Widgets`, `Qt6::Multimedia`, `Qt6::MultimediaWidgets`, and also `Qt6::Test` — directly mirroring the app target's huge link list. This guarantees that `CMakeLists.txt:1090-1175` will drift from `251-354` and break clique. A proper CMake alias/dedup fixture should be used.

---

## Detailed Findings by Component

### `CMakeLists.txt` (build system)

**Observation:** `main_window_link_preview_test` is a near-twin of the application target.

- `main_window_link_preview_test.cpp` re-includes almost every `.cpp/.h` from `src/` manually (lines ~1091-1174). Maintenance burden is high; if any source file is added to the app but not mirrored here, the test will not catch linkage/OOM/O0 undefined-reference regressions in the full build path.
- `set(CMAKE_CXX_STANDARD 20)` with `CMAKE_CXX_EXTENSIONS OFF` is correct, but no `-Werror` or `-Wall -Wextra` are enabled at the target level; g++ 15.2 can default to `-fpermissive` if user environment overrides go through `CMAKE_CXX_FLAGS`. Risk is muted but possible through `build-lua` custom toolchain files.
- `LuaEngine.cpp` is compiled into `maxchat_scripting` even when `MAXCHAT_LUA=OFF`. That means a non-scripting build still requires a C++20-lambda-heavy `LuaEngine.cpp` compile. The comment at line 69 says “inert stubs” but there is no evidence of `#ifdef` stub implementations in the header; if stubs are not implemented, a `MAXCHAT_LUA=OFF` build will fail with undefined `lua_*` symbols.
- `hunspell_spellchecker_test.cpp` condition ONLY defines `MAXCHAT_TEST_DICTIONARY_DIR`, but the production `SpellcheckDictionaryCatalog.cpp` honors `MAXCHAT_BUNDLED_DICTIONARY_DIR` (line 208). If the dictionary dir is created at runtime in `configDir`, tests may pass while deployed lookups fail.

### `src/irc/IrcConnection.cpp / .h`

**Strengths:**
- Thrashing re-entrancy guard (`draining_`) + `QTimer::singleShot(0)` re-drain (lines 398-434) avoids nested-event-loop aliasing.
- `emitTimedFailure` retires the socket *before* emitting `disconnected` (lines 445-453); this eliminates “dead-object hazards”.
- Idle watchdog uses two-phase detections: probe (`PING`) then abort (lines 287-323); this correctly distinguishes between a blocked TCP socket and an idle server.

**Issues:**
- `localAddress()` (line 204-212) silently suppresses IPv6. If the user’s primary network is IPv6-only (common in modern ISPs/VPNs), the local address returned may be empty.
- Proxy validation rejects literals like `0.0.0.0` or empty with `value == 0` in `DccManager::parseHost` (DccManager.cpp:68-80), but `IrcConnection::connectTo` does not validate the proxy *hostname* as IP literal, so a proxy like `0.0.0.0:9050` could be set and would bypass the check by being treated as a hostname string. Not a bypass, but a silent misrouting risk.
- `retireSocket` does not set `socketReady_ = false` before `deleteLater()` — if `onSocketDisconnected` fires for an already-teared-down socket (rare but possible), `emitTerminalDisconnect` already guards against double emit, but `onReadyRead` could fire during `deleteLater()` invocation of queued events; `socket != socket_` guard prevents full harm, yet a partial `buffer_.clear()` could be skipped.

### `src/irc/IrcSession.cpp / .h`

**Strengths:**
- SASL chunking at 400-byte boundaries; b64 termination with `AUTHENTICATE +` (lines 372-386).
- Correct handling of `900`/`903` to send `CAP END` exactly once.
- Overlong `PRIVMSG`/`NOTICE` splitting preserves surrogate pairs (lines 170-176).
- CTCP PING timestamp uses `QDateTime::currentMSecsSinceEpoch()` formatted via `double(...)/1000.0` with `'f',3` (line 280). The arithmetic is correct; only potential concern is `currentMSecsSinceEpoch()` being non-monotonic across NTP jumps, which can spuriously mark lag as negative.

**Issues:**
- `sendRaw` always uses `2000` ms penalty (line 137), ignoring the configured `maxchat_core`/`FloodGuard` settings. The comment at `IrcSession.h:121` mentions that `mcData` uses `750 ms`; there is no way to tune these.
- `lagToken_` can collide under extreme time resolution clumping (if two lag probes arrive in the same millisecond on a stressed event loop). A monotonically increasing sequence or a UUID-like token would be safer.
- SASL AUTHENTICATE uses `saslAccount_` if explicitly set (e.g., a different account than nick), but if user passes `saslAccount` *and* `nick` the function would send both as identity without warning the user that many networks treat account==nick forced.
- `ignorePatterns_` compiles `wildcardToRegularExpression` on every `setIgnoreMasks` call but re-compiles a fresh regex each time, which regens duplicate `QRegularExpression` objects. This is acceptable on small lists.

### `src/ui/DccManager.cpp / .h`

**Strengths:**
- `sanitizeFileName` handles Windows reserved names and directory traversal (lines 85-108).
- Strict numeric parsing for passive DCC port with `parsePort` (lines 56-66). Previously a bug let `70000` become port `4464`, now fixed.
- 32-bit ack wrap-around tracking via `ackBase`/`lastAck32` correctly handles >4 GiB sends (lines 95-102, 370-385).
- Passive offers validate that only the *intended* peer may reply (`sameNick`, line 473), preventing token replay by a third party.

**Issues / Notes:**
- The `chats_` container is manually `delete`d in the destructor via raw pointers (line 182-184). Consider switching to `QHash<QString, std::unique_ptr<ChatRuntime>>` or `std::unique_ptr` to avoid leaks on exception paths or early exit.
- `newToken()` (`line 117`) returns a hex string of length 8. 8 characters = 32 bits of entropy = 4.3e9 possibilities. On a busy channel, a passive reply guess can be brute-forced within seconds. Recommendations: at least 12 hex chars (48 bits), ideally 16.
- `portFirst_`/`portLast_` is int, but `openListenPort` accepts a `quint16` port and tries up to `portlast - portfirst + 1` sequentially without a timeout. If the OS rejects many ports (Windows firewall or WSL), this loop can block for a long time.
- Anti-flood `MaxPendingOffers = 32` and `ChatLineCap = 8192` (bytes for chat), but `ChatBufferCap = 65536` total per peer. If both are hit, subsequent messages silently drop.

### `src/services/ImageFetcher.cpp`

**Strengths:**
- SSRF gate runs DNS off the GUI thread, re-vetting on redirects (lines 84-91).
- `QPointer` guards `QNetworkReply`, then `qScopeGuard` for `deleteLater` (lines 58-95).
- `maxBytes` cap is enforced *during* download, not just after (lines 68-76); this avoids buffering unbounded payloads in the Qt network buffer.

**Issues:**
- `buildRequest` sets `NoLessSafeRedirectPolicy` (line 27). "No less safe" still allows HTT→HTTPS, HTTPS→HTTP, cross-origin redirects; the SSRF gate blocks the bad IPs, yet `example.com` → `malicious.internal` via internal DNS rebinding could bypass because there is no certificate pinning and the redirect is allowed until DNS resolution is performed by `QNetworkAccessManager`.
- The `UserAgent` header is hardcoded as `MaxChat/0.1 link-preview`. Some image CDNs / Cloudflare pages reject non-browser User-Agents; this means previews can fail even for permissible sites.
- `contentType.startsWith("image/")` check (line 112) does not account for case-sensitivity past `toLower()`; it does, actually — but `reply->header(...).toString().toLower()` is correct.

### `src/services/LinkPreviewClassifier.cpp`

**Strengths:**
- Robust IPv4 literal range filtering including special-use ranges (100.64/10, 198.18/19, etc.) — real OWASP-style SSRF defense.

**Isses:**
- `isPublicIpv4Literal` allows `224.0.0.0/4` (multicast) to be treated as public (line 55: `a >= 224`) — multicast and loopback-like addresses (`240.0.0.0/4`) are often blocked at SMTP/relay but are still routable in multicast; likely acceptable for link preview but note.
- `fxtwitter.com` URLs built at lines 187-194 are not validated against SSRF as `LinkPreviewCandidate::fetchUrl` passes through the same normalization and `isAllowedPreviewFetchUrl` gate (good), but there is no fallback if `fxtwitter.com` itself is blocked by corporate WAF.

### `src/upload/ImageUploaderFactory.cpp` & uploaders

**Issues:**
- The factory returns uploaders by pointer and takes `QNetworkAccessManager*` as raw pointer (dependency out of lifetime). The uploaders do not appear to check `manager_` before use (e.g., Postimages `slotStandard_reply` uses `manager_->post` without null-check).
- `IimgurUploader.cpp` sends the bearer token in a header from app-level stored settings. There is no evidence of token refresh; long-lived user access tokens can be revoked, breaking uploads until the user resets credentials.
- Imgur `client_id` is public and part of the API flow; OK. ImgBB/Imgbox use API keys. If these keys are bundled into a shipped client (OAuth client secrets, etc.), the uploader becomes a shared-secret proxy under user control.
- No exponential backoff on 429 responses; uploaders retry once in some paths but without honoring `Retry-After`.

### `src/ui/ImageViewerDialog.cpp / .h`

**Strengths:**
- Clean lifetime: `QNetworkAccessManager*` owned by dialog via `this` in ctor (line 26).
- `WA_DeleteOnClose` paired with a layout that auto-works.

**Issue:**
- `ImageViewerDialog.h` forward-declares only `QWidget`, yet `QDialog` is the base; the header does not `#include <QDialog>`, which depends on forward-declaring the actual base. If compiled with a Qt module where `QDialog` is not pre-included, this breaks. However, because `MainWindow.h` includes `<QMainWindow>` (which likely includes `<QWidget>` which includes `<QDialog>`), current transitive inclusion masks the bug.

### `src/ui/MainWindow.cpp / .h`

**Class size:** 9,855 lines of C++ + 589 lines of header. Maintainability high-risk; several coupled concerns are in one `final` class: UI, scripting host, network manager, DCC, bookkeeping, UI theming, book-marking, chat/rendering, comic mode, previews, notifications, and more.

**Issues:**
- `m_autocorrectSuppress` is `QString` (line 254). Storing a per-correction suppress via exact text is “hangman” style — if `QTextDocument` collapses whitespace, the stored `original` can stop matching.
- `comicKey` uses `QChar(0x1f)` (unit separator) (line 152). Valid, but if any buffer name or network name contains the US, the composite key collides. Should pick a delimiter unlikely to appear in user input (like `\x1e` or base64 a pair).
- `memberMatchesNick` compares *prefix-stripped* nick against `nick` (line 163-164), but `nickWithoutPrefix` strips from `text` (which is the `QListWidgetItem` text including prefix). If the view renders nick modes (e.g., `"+User"`), without prefix stripping this would mismatch; however there’s no confirmation that `QListWidgetItem` actually includes/OFF mode prefix at all times — possibly inconsistent for Iron.
- `rememberTarget`/`forgetTarget` (lines 8829-8855) inserts into `m_openTargetsByNetwork[activeNetworkName()]` by value-copying `m_openTargets` every touch. Complexity is O(n) per call; if target count grows, this regresses.
- Uses `QScrollBar*` and `QPlainTextEdit`-style widget-level control channels without `QSignalBlocker` in some resize loops (resizeMessageInput in the visible excerpts). If paint events fire mid-resizing, the user’s caret position can jump.
- `checkForUpdates` hits GitHub Releases without API auth. GitHub applies strict rate limits; unauthenticated requests will be throttled by IP after repeated manual clicks, causing false “up to date” vs. actual state. Uses `QDesktopServices::openUrl(QUrl(...))`, which is benign from injection standpoint.

### `src/comic/` components

- `ComicRenderer.cpp` 647 lines; the renderer generates a QPainter sequence. No evidence of device-pixel-ratio scaling; on Retina displays, comic text is physically tiny.
- `ComicCharacter.cpp` loads cells from filesystem; if the art directory is user-controlled (e.g., Quarantine path), arbitrary `QImage::load` can decode crafted BMP/ICO/GIF. There is no sanity cap on file count or dimension; thousands of small images can OOM the UI.

### `src/scripting/LuaEngine.cpp` (1161 lines)

**Issues:**
- Scalars stored in `ScriptTimer`/`ScriptState` via raw `new` (lines 765, 984). There is `ScriptTimer* timer` member and `QTimer` member too; `ScriptState` contains raw function pointers and a `lua_State*`. There is no visible `lua_close` call or Lua state lifecycle in the visible header. If the engine holds onto a `lua_State*` after the host closes, that is a leak, and worse, if `luaL_newstate` is ever called per-script rather than once, there will be memory churn.
- There is no sandbox proof: `ScriptHost` exposes `scriptHttpGet` and `scriptMcData` etc. From Lua, this can be called recursively unless a guard prevents it. If a script autoruns and calls `scriptHttpGet` from the host, it could spin indefinitely.
- `ScriptTerminalManager.cpp` opens dialogs without size limits and without `Qt::WA_DeleteOnClose`; if script-spawned terminals accumulate and the user doesn't close them, you get many orphan `QDialog`s, collectively hogging VBlank.

### `src/core/SettingsStore.cpp`

- 455 lines. Uses `QSettings` (QString-based formats). Organization/app names must be defined in `main.cpp` (not shown in the excerpted bytes, but `SettingsStore` is used throughout). If the company/app name changes, user settings become “lost” in registry/plist without migration. No `fallbacks` or `beginGroup/endGroup` migration path is present.
- `personalDictionaryPath` and `seedBundledScripts` create directories (`QDir().mkpath`) without verifying if the config root is on a read-only filesystem. This will fail silently on locked-down profiles.

### `src/core/DefaultNetworks.cpp` (1551 lines)

- A massive hardcoded list of IRC networks. Some entries are stale (e.g., Freenode migrated to Libera). The user sees outdated default networks rather than guidance to connect to the newer network. Recommend a data-driven `.json` format plus a small “known networks” module to allow updates without recompilation.
- Each network entry stores a homepage URL that is never validated. A typo or redirect can silently point to a parked/spam domain. No runtime check.

### `src/tools/gen_sounds.py`, `mcb1_convert.py`

**Strengths:**
- Pure stdlib (no `PIL` dependency for sounds), safe math routines, bounded float normalization.

**Notes:**
- `mcb1_convert.py` version is a dev-tool in repo; it is not wrapped with `python -m` paths, so running it requires installing `Pillow` (non-stdlib). No type hints/`py.typed`, no packaging metadata.
- `.mcb1` format parses with `frame_hash(djb-33)` and Z85 encoding. The parser and `z85_encode` are safe, but the `bayer_dither` uses O(width*height) nested Python loops; this is slow for large images and blocks the terminal. Recommend `Cython` or `numpy` vectorization if ever used in bulk.

---

## Tests (CMake)

**Observation:** `main_window_link_preview_test` duplicates nearly the entire application target (lines 1090-1174). This should be refactored to link against a `maxchat_app` static library rather than `maxchat_c`/`maxchat_network`/etc., mirroring `maxchat-c`. There is risk of divergence (headers added to app target but forgotten in the test target).

**Observation:** Several dialog tests set `QT_QPA_PLATFORM=offscreen` (lines 741, 908, 929, etc.) but do not set `QT_OPENGL` or `QT_QPA_PLATFORMTHEME` to eliminate XCB/Wayland backend dependencies. Headless Docker environments can still fail on font lookup and unusual DPI.

---

## Security Surface Summary

| Category | Risk | Detail |
|---|---|---|
| **SSRF** | Medium | `ImageFetcher` + `LinkPreviewClassifier` block private IPv4 and known TLDs; `fxtwitter.com` rewriting is an external dependency. |
| **Token secrecy** | Medium-High | Image uploader API keys / OAuth tokens stored via QSettings. If settings are copied/leaked, the uploader is usable as an anonymous exfil proxy. |
| **DCC token entropy** | Low | `newToken()` provides 32-bit hex token. Brute-force feasible on busy networks. |
| **Injection** | Low | Command/dialog inputs use Qt model/view APIs; `sanitizeFileName` covers traversal. |
| **DoRCE** | Low | `QProcess` included — behavior undetermined from inspection; review for open-with-editor flows below. |
| **DoS** | Medium | Netsplit floods can drop >100 lines/tick without notification; upload thread is single-threaded to GUI. |

---

## Conclusion

The MaxChat-C codebase is well-architected in its IRC core: clean separation transport (`IrcConnection`) vs. protocol (`IrcSession`), proper flood control and timeout handling, thoughtful SASL handling, and a readable C++20 style. The biggest risks are **build-test drift**, **incorrect registration-timeout mapping** (`MainWindow.cpp:9058`), **uploader API secret storage**, **unreliable DCC token size**, and the sheer size of `MainWindow.cpp` (9,855 lines), which is a sign that the class should be split into dedicated controllers (Network/UI/Preview/Comic/Scripting). Before shipping or hardening, the priority is:

1. Fix the mapping bug on line 9058 (`connectTimeoutMs` → proper `registrationTimeoutMs`).
2. Move `main_window_link_preview_test` to re-use an `maxchat_app`-style alias.
3. Migrate uploader secrets out of plain QSettings (or at least warn and obfuscate using OS keychains).
4. Increase DCC passive token length to ≥12 hex chars.
5. Extract `ScriptHost`/`MainWindow` into smaller service classes.

Agent: Hermes (Nous Research). Report written only; source files untouched.
