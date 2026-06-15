# MaxChat C++ Line Audit — XAI — 2026-06-15

## Objective

Perform a full line-by-line audit of the MaxChat C++ port in `/home/iw/work/claude/maxchat-c`, with attention to code correctness, security, stability, release readiness, test coverage, build hygiene, design quality, and implementation drift from intended MaxChat behavior.

## Scope

Reviewed line-by-line:

- `CMakeLists.txt`
- `build.bat`
- `sync-to-win.sh`
- Core C++ source and headers under `src/` (155 files, ~35,000 LOC)
- Unit tests under `tests/unit/` (53 test targets)
- Lua scripts under `assets/scripts/`
- Helper tools under `tools/`
- Runtime resource manifest `resources/maxchat.qrc`
- Primary docs: `README.md`, `SCRIPTING.md`, `DEVDOCS/MC_DATA.md`, `DEVDOCS/MC_DATA_PHASES.md`

Inventoried but not manually byte-reviewed:

- Binary assets: fonts, icons, sounds, wallpapers
- Hunspell dictionary `.aff` / `.dic` wordlists
- Third-party vendored code (`third_party/lua`, `third_party/hunspell`)
- Build directories and generated CMake output

Scale checked:

- Tracked files: 413
- Reviewed source/docs/tests/tools line count: ~43,993 lines
- Current HEAD: `cc88713` (`mc-data-terminal` branch)

---

## Verification

Commands run:

```text
env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -j2
find src tests tools assets -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.lua" \) | wc -l
git ls-files Testing/Temporary
rg TODO/FIXME/security patterns across source, tests, docs, scripts, and build files
```

Results:

- **52 of 53 tests pass** with `QT_QPA_PLATFORM=offscreen` (98%).
- **1 failure:** `main_window_link_preview` — `themeMenuChangesSavedThemeAndApplicationStyle` expects `qApp->styleSheet().isEmpty()` after "Themes Off", but `applyTheme()` still sets `chromeFontCss`.
- Plain `ctest` without offscreen fails 4–5 GUI/media tests in headless shells (missing `xcb` display) — environmental, not product bugs.
- Tracked generated CTest artifacts: `Testing/Temporary/CTestCostData.txt`, `Testing/Temporary/LastTest.log`.
- Only 2 explicit `TODO(release)` markers in `src/` (`SettingsStore.cpp:436`, `PreferencesDialog.cpp:736`).

---

## Priority Findings

### P0 — Active DCC Accepts First TCP Connection Without Peer Verification

Evidence:

- `src/ui/DccManager.cpp:324-334` — active **send** listener
- `src/ui/DccManager.cpp:894-904` — active **chat** listener

Unlike passive receive (`601-607`) and passive chat (`1029-1035`), active-mode listeners accept the first inbound TCP peer with no `peerAddress()` check and no token verification.

Impact:

- A LAN attacker or port scanner that reaches the listening port can intercept or block file sends and chat sessions.

Recommendation:

- Verify connecting IP against the advertised peer host when known.
- Require passive mode for sends, or bind token to active sends too.

---

### P0 — DCC Chat Session Leak on Re-Offer

Evidence:

- `src/ui/DccManager.cpp:867-870` — `offerChat()` inserts without teardown
- `src/ui/DccManager.cpp:1002-1005` — `acceptIncomingChat()` correctly tears down first

Impact:

- Leaked `QTcpServer`/`QTcpSocket`, duplicate signal handlers, possible cross-session data delivery.

Recommendation:

- Call `teardownChat(key)` before insert, mirroring `acceptIncomingChat()`.

---

### P0 — Lua IRC/MC APIs Bypass Permission Model

Evidence:

- `src/scripting/LuaEngine.cpp:227-229` — `api.send_raw` ungated
- `src/scripting/LuaEngine.cpp:232-255` — `api.mc_send` / `api.mc_reply` ungated
- `src/scripting/LuaEngine.cpp:856-858` — always registered
- `src/ui/MainWindow.cpp:2653-2657`, `2660-2677` — host forwards without policy

Impact:

- Any loaded script — even with all permissions off — can JOIN/PART/MODE/QUIT, flood, impersonate, or emit MC DATA.

Recommendation:

- Add `ScriptPermissions::ircWrite` and `mcData` (default off); gate destructive APIs; keep `echo`/`notify` safe.

---

### P1 — Themes Off Test And Implementation Disagree

Evidence:

- `src/ui/MainWindow.cpp:9265-9269`
- `tests/unit/main_window_link_preview_test.cpp:464-466`

The test expects `qApp->styleSheet().isEmpty()` after selecting Themes Off. The implementation intentionally leaves `chromeFontCss` installed even for the system theme:

```cpp
qApp->setStyleSheet(chromeFontCss);
```

Impact:

- Current test suite is red: 52/53 passing offscreen.
- Product behavior needs a decision: either "Themes Off" truly means no application stylesheet, or it means no color/theme stylesheet while preserving the app chrome font.

Recommendation:

- If "Themes Off" should mean no stylesheet at all, move chrome font handling out of app-wide QSS and use `QApplication::setFont` only.
- If current behavior is intentional, update the test to assert theme CSS is absent while chrome font CSS remains.

---

### P1 — Local Secrets Stored In Plain Settings

Evidence:

- `src/core/SettingsStore.cpp:38`, `300-307`
- `src/core/ConnectionPlan.cpp:99-111`
- `src/ui/ServerListDialog.cpp:92-108`
- `src/ui/PreferencesDialog.cpp:2167-2283`

Stored sensitive values include:

- NickServ/SASL password
- Server PASS password
- Proxy password
- ImgBB API key
- Imgur Client-ID
- Postimages API token
- Imgbox username/password

Impact:

- Anyone with access to `~/.config/maxchat/settings.json` can read saved secrets.
- Backups and shared configs leak credentials.

Recommendation:

- Add visible warning near saved credentials if this remains intentional.
- Prefer OS credential storage: Windows Credential Manager, libsecret/KWallet on Linux, Keychain on macOS.
- At minimum, separate "remember password/token" from session-only credentials; `chmod 600` on config dir.

---

### P1 — IRC Target Parameter Injection

Evidence:

- `src/irc/IrcSession.cpp:266-271` — `PRIVMSG %1 :%2` with unvalidated target
- `src/irc/IrcSession.cpp:309-310` — `JOIN %1` with unvalidated channel
- `src/irc/IrcSession.cpp:327-331` — `PASS`, `NICK`, `USER` with unvalidated params
- `src/irc/CommandParser.cpp:238-239` — `/msg` target not validated

Impact:

- Targets containing spaces, `:`, or control characters can inject extra IRC parameters on the wire.

Recommendation:

- Central `IrcTargetValidator` used by session, parser, and UI; reject invalid nick/channel patterns before formatting.

---

### P1 — `scriptHttpGet` Blocks GUI Thread

Evidence:

- `src/ui/MainWindow.cpp:2813-2878`
- Nested `QEventLoop::exec()` at lines 2832 and 2869

Impact:

- UI freeze up to ~20 seconds per script HTTP call; nested blocking loops on the GUI thread.

Recommendation:

- Move to async `QNetworkAccessManager` + script callback; never nest blocking loops on GUI thread.

---

### P1 — DCC Passive Tokens Are Only 32 Bits

Evidence:

- `src/ui/DccManager.cpp:114-118`

The token is one 32-bit value formatted as 8 hex chars. Sender-nick binding is good, but token entropy is thin.

Recommendation:

- Use at least 128 bits of random token material (e.g. 32 hex chars).

---

### P1 — DCC CTCP Routed to Wrong Network

Evidence:

- `src/ui/MainWindow.cpp:921-925` — `DccManager::ctcpToSend` calls `connection().ctcp(...)`
- `src/ui/MainWindow.cpp:3966-3970` — `connection()` resolves **active** network only

Impact:

- Multi-network DCC CTCP negotiation can go to the wrong server.

Recommendation:

- Track per-transfer/chat network in `DccManager`; route through `connectionForNetwork(net)`.

---

### P1 — MC DATA/BBS Demo Login Must Not Become Real Auth

Evidence:

- `assets/scripts/bbs.lua:26-27`
- `assets/scripts/bbs.lua:1025-1043`
- `DEVDOCS/MC_DATA.md:710-717`

The BBS demo has the account `sir_iw / bbsiscool`. MC DATA v1 is plaintext and must not carry secrets.

Impact:

- Fine for a demo account.
- Not acceptable for real accounts, private mail, or moderation powers.

Recommendation:

- Keep labeled demo-only.
- Before real BBS accounts, add `MC SEC` or another reviewed authenticated/encrypted layer.

---

### P2 — OpenGraph Fetcher Caps Body Too Late

Evidence:

- `src/services/OpenGraphFetcher.cpp:60-128`
- `src/services/ImageFetcher.cpp:66-76` (good streaming abort pattern)

`ImageFetcher` aborts as soon as `downloadProgress` exceeds `maxBytes`. `OpenGraphFetcher` only reads `maxBytes + 1` in `finished`.

Impact:

- Large HTML responses can be buffered by Qt before the application truncates them — avoidable memory pressure.

Recommendation:

- Copy the `downloadProgress` early-abort pattern from `ImageFetcher` into `OpenGraphFetcher`.

---

### P2 — SSRF Redirect Race in Preview Fetchers

Evidence:

- `src/services/ImageFetcher.cpp:78-92`
- `src/services/OpenGraphFetcher.cpp:78-88`
- `src/services/LinkPreviewClassifier.cpp:300-331`

Redirect vetting uses async DNS. Qt may follow a redirect to `127.0.0.1` or `169.254.169.254` before `abort()` runs.

Impact:

- Metadata SSRF to internal services via malicious redirect chain.

Recommendation:

- Disable auto-redirect and handle manually after synchronous DNS check, or block until resolution completes.

---

### P2 — Lua File Jail Symlink Escape

Evidence:

- `src/scripting/LuaEngine.cpp:496-527`

`fileAccessAllowed()` uses `QFileInfo::absoluteFilePath()` without canonicalizing symlinks.

Impact:

- A symlink inside an allowed dir can escape the jail.

Recommendation:

- Use `canonicalFilePath()` and re-check `within()` after resolution; reject symlinks pointing outside roots.

---

### P2 — Uploaders Need Timeouts, Response Caps, URL Validation

Evidence:

- `src/upload/ImgbbUploader.cpp:44-78` — API key in query string
- `src/upload/ImgurUploader.cpp:45-77`
- `src/upload/PostimagesUploader.cpp:52-78`
- `src/upload/ImgboxUploader.cpp:44-131`
- `tests/unit/image_uploader_test.cpp:173-199`

No request timeouts, no response body caps, returned URLs not scheme-validated before `emit uploaded`.

Recommendation:

- Add per-request timeout handling.
- Cap response body size before JSON parsing.
- Validate returned URLs are `http`/`https` only.
- Move ImgBB API key out of query string.

---

### P2 — Server Homepage Button Opens Unvalidated URLs

Evidence:

- `src/ui/ServerListDialog.cpp:371-375`
- `src/ui/UrlListDialog.cpp:16-31`

`UrlListDialog` has a good `openableUrl()` guard. `ServerListDialog::openHomepage()` directly hands the stored website string to `QDesktopServices::openUrl`.

Recommendation:

- Reuse the `UrlListDialog` scheme guard or add a shared helper for external URL opening.

---

### P2 — Generated CTest Files Are Tracked

Evidence:

- `git ls-files Testing/Temporary`
- `.gitignore:1-14`

Tracked: `Testing/Temporary/CTestCostData.txt`, `Testing/Temporary/LastTest.log`.

Recommendation:

- Add `Testing/` to `.gitignore`.
- `git rm --cached` the tracked files.

---

### P2 — `mcb1_convert.py` Crashes On Non-Byte-Aligned Sizes

Evidence:

- `tools/mcb1_convert.py:125-132`

`raw_bytes(bits)` assumes bit count is divisible by 8. Custom sizes like `81x25` can crash.

Recommendation:

- Pad the final byte with zero bits.
- Add a smoke test for non-byte-aligned dimensions.

---

### P3 — Version And Product Name Still Say Dev/Experiment

Evidence:

- `CMakeLists.txt:3-6`
- `src/app/AppInfo.cpp:13-18`

`VERSION 0.1.0`, `DESCRIPTION "Native C++/Qt port experiment for MaxChat"`, app reports `0.1.0-dev`.

Recommendation:

- Update version/display strings before packaging.

---

### P3 — `MainWindow.cpp` God Object (~9,853 Lines)

Evidence:

- `src/ui/MainWindow.cpp` — IRC routing, scripting, DCC, MC DATA, themes, comic, previews, notifications, settings, tree UI

Impact:

- Regression risk, coupling, difficult testing. Theme test failure is symptomatic.

Recommendation:

- Extract `IrcUiController`, `ScriptHostImpl`, `ThemeController`, `LinkPreviewController` behind narrow interfaces.

---

## Line-by-Line Findings

Format: `LINE | SEVERITY | CATEGORY | FINDING | RECOMMENDATION`

Severity: **CRITICAL** | **HIGH** | **MEDIUM** | **LOW** | **INFO**
Category: **BUG** | **SECURITY** | **DESIGN** | **MAINTAINABILITY** | **IMPROVEMENT**

---

### `src/irc/IrcConnection.h`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 24-36 | MED | SECURITY | `ConnectConfig` holds passwords as plain `QString` in memory | Secure credential type; zero on disconnect |
| 27 | MED | SECURITY | `acceptInvalidCertificate` enables MITM when true | Default false; prominent UI warning |
| 119-129 | LOW | DESIGN | `buffer_`, `draining_` assume single-threaded Qt event loop | Document thread affinity |

---

### `src/irc/IrcConnection.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 15-29 | INFO | IMPROVEMENT | Incoming limits (`MaxIncomingLineBytes`, `MaxBacklogBytes`, `MaxLinesPerTick`) well documented | Keep aligned with ISUPPORT |
| 107-109 | MED | SECURITY | Passwords copied into `Registration` without clearing prior values | Zero sensitive fields on reconnect |
| 140-145 | MED | SECURITY | Proxy credentials passed to `QNetworkProxy` in plaintext | Expected; avoid logging proxy config |
| 286-304 | LOW | SECURITY | Flood/backlog caps mitigate DoS | Good |
| 312 | LOW | CORRECTNESS | Keepalive `PING :maxchat-keepalive` hardcoded | OK |
| 358-368 | MED | SECURITY | `acceptInvalidCertificate_` calls `ignoreSslErrors()` — full MITM | Log fingerprint when accepted |
| 362-368 | LOW | CORRECTNESS | Cert rejection waits for connect timeout | Explicit `abort()` for faster feedback |
| 393-434 | LOW | CORRECTNESS | Re-entrancy guard on `queueIncomingLines` is sound | Good pattern |
| 414-416 | LOW | CORRECTNESS | Oversized complete lines dropped silently | Optional metric |
| 445-452 | LOW | CORRECTNESS | `emitTimedFailure` prevents double-disconnect | Good |
| 178-182 | LOW | DESIGN | `disconnectFromServer()` only `abort()`s; cleanup via async `disconnected` | Document |

---

### `src/irc/IrcSession.h`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 22-31 | MED | SECURITY | `Registration` stores passwords in plain `QString` | Secure string wrapper; clear on disconnect |
| 114-123 | LOW | DESIGN | `PendingLine` queue can grow if send blocked | Cap queue size |
| 139-141 | MED | SECURITY | `serverPassword_`, `saslPassword_` retained for session lifetime | Clear after successful auth |

---

### `src/irc/IrcSession.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 18 | INFO | SECURITY | `McDataMaxPayloadBytes = 350` wire cap | Good |
| 145-150 | INFO | SECURITY | CRLF stripped on outbound — primary IRC injection defense | Extend to `\0` if needed |
| 153-196 | LOW | CORRECTNESS | Long PRIVMSG/NOTICE split at UTF-8 boundaries | Good; recursive depth on huge paste |
| 180 | LOW | DESIGN | Recursive `sendRaw` during split deepens call stack | Iterative chunk enqueue |
| 204-205 | LOW | CORRECTNESS | PING/PONG bypass flood penalty via `free` flag | Correct |
| 217-263 | INFO | SECURITY | Send-queue flood model (ircII-style penalty window) | Good |
| 240-255 | MED | CORRECTNESS | Short write parks partial line; **entire queue cleared** on `written < 0` | Retry or reconnect instead of dropping all |
| 266-267 | **HIGH** | SECURITY | `PRIVMSG %1 :%2` — target not validated (spaces inject extra params) | Validate target: `^[^\s,:]+$` |
| 270-271 | MED | SECURITY | `action` embeds text in CTCP; inner `\x01` can break framing | Strip `\x01` from action text |
| 274-286 | MED | SECURITY | `ctcp` uppercases command but not target; arg not sanitized | Validate target; strip control chars |
| 297-306 | INFO | SECURITY | MC DATA payload capped at 350 bytes on send | Good |
| 309-310 | MED | SECURITY | `JOIN %1` — channel with spaces injects extra params | Require `#`/`&` prefix, no whitespace |
| 327-331 | MED | SECURITY | `PASS`, `NICK`, `USER` — spaces break RFC framing | Validate nick/username |
| 328 | MED | SECURITY | `PASS` sent before SASL when both configured | Prefer SASL-only when offered |
| 342 | LOW | CORRECTNESS | Auto-PONG to server PING | Required behavior |
| 368-374 | MED | SECURITY | SASL PLAIN buffer lingers in memory | Zero buffer after base64 encode |
| 375-386 | INFO | SECURITY | SASL chunked at 400 bytes | Good |
| 413-419 | **HIGH** | SECURITY | Fallback `PRIVMSG NickServ :IDENTIFY` reuses SASL password | Separate NickServ password field |
| 550-554 | MED | SECURITY | DCC args forwarded raw to UI | Validate DCC token format |
| 558-563 | LOW | SECURITY | CTCP auto-reply throttled ~1/sec | Good |
| 566-572 | LOW | SECURITY | SOUND filename passed upstream | Validate basename in session |
| 112-114 | MED | SECURITY | Ignore masks → `wildcardToRegularExpression` — catastrophic backtracking risk | Cap pattern length |
| 1014-1024 | INFO | SECURITY | `passwordAuthAllowed()` blocks plaintext auth unless TLS/opt-in | Good default |
| 1039-1048 | LOW | CORRECTNESS | Regex match per message per ignore pattern — O(patterns × messages) | Compile once; limit count |

---

### `src/irc/McDataCodec.h` / `McDataCodec.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| McDataCodec.h:9-13 | LOW | DESIGN | `McDataMessage` holds unbounded `payload` QString | Document max; enforce on parse |
| McDataCodec.cpp:12-26 | INFO | CORRECTNESS | Leading-only whitespace strip preserves payload bytes | Correct for BBS framing |
| McDataCodec.cpp:28-40 | LOW | SECURITY | `validToken` restricts service/verb charset | Good |
| McDataCodec.cpp:44-63 | MED | SECURITY | No max length on parsed payload | Cap at 350 bytes on parse |
| McDataCodec.cpp:61-62 | LOW | CORRECTNESS | Service lowercased, verb uppercased | OK |

---

### `src/irc/IrcRedaction.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 9-11 | INFO | SECURITY | `PASS` redacted | Good |
| 13-20 | INFO | SECURITY | `AUTHENTICATE` base64 payload redacted | Good |
| 22-29 | MED | SECURITY | Services regex misses `OPER`, `SET PASSWORD`, custom `/msg ns` | Expand patterns |
| 31 | LOW | SECURITY | Non-matching lines returned verbatim | Redact proxy auth if logged |

---

### `src/irc/CommandParser.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 35-44 | MED | CORRECTNESS | `channelTarget` prepends `#` to arbitrary tokens | Validate channel name charset |
| 75-99, 335-351 | INFO | DESIGN | Service aliases map to NickServ/ChanServ | Good |
| 103-150 | INFO | DESIGN | Broad command set present | Good |
| 238-239 | MED | SECURITY | `/msg` target not validated | Reject whitespace in targets |
| 429 | MED | SECURITY | `/mode` builds `MODE %1` with user-supplied `rawArgs` | Validate mode string |
| 453-458 | LOW | CORRECTNESS | `/op` `/deop` nicks with spaces break | Split/validate nicks |
| 581 | INFO | DESIGN | `/umite` alias for `/unmute` — intentional typo compat | OK |
| 665-673 | **HIGH** | SECURITY | `/raw` passes `rest` directly — intentional power user surface | Warn in UI; optional confirmation |
| 687-695 | **HIGH** | SECURITY | `/oper` sends password in `rest` — weakly redacted | Dedicated oper dialog; extend redaction |
| 706-709 | MED | SECURITY | `/kill` embeds reason in trailing | Validate nick |
| 717-721 | MED | CORRECTNESS | Unknown `/command` becomes raw IRC — typos sent to server | Return `Error` for unknown commands |

---

### `src/irc/IrcMessage.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 17-44 | LOW | CORRECTNESS | IRCv3 tag unescape handles `\:` `\s` `\\` `\r` `\n` | Good |
| 68-85 | MED | SECURITY | Tag string split on `;` with no limit — tag flood | Cap tags (e.g. 32) and length |
| 89-97 | LOW | CORRECTNESS | Prefix parsing handles edge cases | OK |
| 110-124 | LOW | CORRECTNESS | Trailing param preserves internal whitespace | Matches Python parity |

---

### `src/irc/IrcFormat.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 170-284 | LOW | SECURITY | `toHtml` uses `toHtmlEscaped()` — mitigates HTML injection | Good |
| 213-216 | LOW | SECURITY | Style values from internal color table, not user hex | Good |
| 133 | LOW | CORRECTNESS | `nickColor` hash modulo palette | OK |

---

### `src/irc/ReconnectPlanner.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 8 | INFO | DESIGN | `ServerRetryLimit = 3` before failover | OK |
| 15 | LOW | CORRECTNESS | `std::clamp` on `serverIndex` prevents OOB | Good |
| 19-40 | LOW | CORRECTNESS | Failover rotation logic consistent | OK |

---

### `src/core/SettingsStore.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 22-28 | LOW | DESIGN | Fallback config dir `~/.config/maxchat` — world-readable on Unix | Restrict dir permissions on create |
| 38 | **HIGH** | SECURITY | `password` inserted into network config — plaintext in JSON | Encrypt at rest or OS keychain |
| 112-138 | LOW | CORRECTNESS | JSON parse failure returns `{}` silently | Log parse errors; backup corrupt file |
| 150-181 | MED | CORRECTNESS | `saveRaw` read-merge-write without file lock — races with Python app | `QLockFile`; atomic replace |
| 161-167 | LOW | CORRECTNESS | Geometry keys preserved on partial saves | Good UX |
| 267-280 | INFO | DESIGN | Network merge preserves user-owned fields | Good |
| 300-307 | **HIGH** | SECURITY | Default settings include API keys/passwords — stored plaintext | Keychain integration |
| 435-439 | INFO | MAINTAINABILITY | `update_check` default false with release TODO | Release checklist item |

---

### `src/core/ConnectionPlan.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 44-59 | INFO | DESIGN | Endpoint deduplication | Good |
| 69-86 | LOW | CORRECTNESS | `parseAutojoinChannels` normalizes channels | Does not reject invalid chars |
| 98-100 | MED | SECURITY | `password` → `saslPassword` cleartext from QVariantMap | Schema validation; encryption |
| 109 | LOW | CORRECTNESS | `proxyPort` no upper bound 65535 here | Validate in plan builder |
| 117-124 | LOW | CORRECTNESS | Server list dedup by host:port:tls | Good |

---

### `src/core/FloodGuard.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 9-10 | LOW | CORRECTNESS | `maxMessages` clamped to min 2; window min 1s | OK |
| 24-36 | MED | CORRECTNESS | `recordMessage` triggers auto-ignore in MainWindow | Good; cap hash size |
| 30-35 | LOW | CORRECTNESS | Per-key timestamps pruned; keys never removed | Evict keys idle > 2× window |

---

### `src/core/ChatBufferStore.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 28 | LOW | SECURITY | `htmlText` stored without sanitization | Sanitize on insert or render via escaped pipeline |
| 58-64 | LOW | CORRECTNESS | `setMaxLinesPerBuffer` trims existing buffers | Good |
| 222-227 | LOW | CORRECTNESS | Unread only when buffer inactive and not `localEcho` | Good |
| 349-363 | LOW | CORRECTNESS | Keys case-folded; server target normalized | Good |

---

### `src/core/ChatLineFormatter.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 99-124 | MED | SECURITY | `linkifyUrls` wraps URLs in `<a href>` — schemes limited to http/ftp/www | Block `javascript:` if regex broadened |
| 114-116 | LOW | SECURITY | Skips match preceded by `"` — partial mitigation | Use DOM builder |
| 135-140 | LOW | SECURITY | `bodyColor` inserted into style without validation | Validate hex color |
| 213-214 | LOW | CORRECTNESS | `toHtmlEscaped()` on label/message paths | Good |

---

### `src/core/ChatLogStore.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 20-29 | INFO | SECURITY | `safePathPart` strips path chars, control chars, limits length 80 | Good defense |
| 67-83 | MED | SECURITY | `logMask` user-controlled — `..` components skipped | Audit custom mask UX |
| 74-76 | INFO | SECURITY | Explicit rejection of `.` and `..` path components | Good |
| 86-104 | LOW | CORRECTNESS | `appendLine` uses `QFile` append — torn writes on crash | Acceptable for logs |
| 101-103 | LOW | SECURITY | Log lines plaintext — secrets if redaction bypassed | Ensure redaction before logging |

---

### `src/core/CommandAlias.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 21 | LOW | SECURITY | `maxDepth = 8` limits alias expansion loops | Good |
| 57-99 | MED | SECURITY | Alias expansion can produce `/raw` chains | Cap output length; block expansion to `/raw` |
| 127-133 | LOW | SECURITY | `seen` set prevents alias cycles | Good |

---

### `src/core/NetworkImport.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 11-17 | LOW | SECURITY | `catalogNetworkFields` protects catalog metadata on merge | Good |
| 42-55 | MED | SECURITY | Import merge overwrites non-catalog fields including passwords | Validate schema; confirm in UI |
| 46-49 | LOW | CORRECTNESS | Catalog fields not overwritten by import | Good |

---

### `src/core/DefaultNetworks.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 21-1509 | LOW | MAINTAINABILITY | ~1500 lines static network data in one TU | External JSON/codegen |
| 1402 | INFO | DESIGN | Hardcoded IP failover may go stale | DNS preferred |
| 1520-1543 | LOW | CORRECTNESS | `parseServerSpec` — IPv6 literals with colons ambiguous | Use bracketed IPv6 |

---

### `src/core/UrlDetector.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 34-36 | LOW | SECURITY | Regex limits schemes to http/https/ftp/www | Good |
| 10-28 | LOW | CORRECTNESS | Trailing punctuation paren/bracket balancing | Good |
| 43 | LOW | CORRECTNESS | `www.` URLs lack scheme | Prepend `http://` in `href` |

---

### `src/scripting/LuaEngine.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 69-76 | INFO | SECURITY | `jailedPath` basename-only jail for read/append | Good |
| 145-182 | **HIGH** | SECURITY | `api.launch` runs arbitrary programs when `runPrograms` granted | Allowlist; user confirmation |
| 204-213 | MED | SECURITY | `api.read_file` uses `readAll()` with no size cap | Max read size 1–4 MiB |
| 227-229 | **HIGH** | SECURITY | `api.send_raw` no permission gate | Add `ircWrite` permission |
| 232-255 | **HIGH** | SECURITY | `api.mc_send` / `api.mc_reply` no permission gate | Add `mcData` permission |
| 257-346 | MED | SECURITY | All `api.terminal_*` APIs ungated | Add `terminal` permission |
| 372-377 | MED | SECURITY | `loadPrefs()` reads entire `prefs.json` without size limit | Cap file size |
| 430-432 | MED | SECURITY | `api.cancel_timer(id)` does not verify calling script owns `id` | Track owner; reject cross-script cancel |
| 437-447 | MED | BUG | `api.http_get` returns `nil` for both failure and empty body | Return `(body, ok)` tuple |
| 496-527 | **HIGH** | SECURITY | `fileAccessAllowed()` does not canonicalize symlinks | Use `canonicalFilePath()` |
| 552-556 | **HIGH** | SECURITY | With `runPrograms`, `os.execute`, `os.getenv`, etc. exposed | Strip `getenv` even when `runPrograms` on |
| 561-587 | MED | SECURITY | `loadModules` does not restrict `package.path` | Set `package.path` to script data dir only |
| 567-574 | INFO | SECURITY | `loadlib`/C searchers disabled when `loadModules` on | Good |
| 579-587 | INFO | SECURITY | Bytecode loading forced to text mode (`'t'`) | Good |
| 610-612 | **HIGH** | SECURITY | With file + `runPrograms`, `io.popen` available | Require separate `shell` flag |
| 785-789 | LOW | DESIGN | Timer callback errors reported but timer keeps firing | Stop after N consecutive errors |
| 492-494 | LOW | DESIGN | `setPermissions()` updates unused global `perms_` | Remove dead API or wire as defaults |
| 916-936 | LOW | SECURITY | `callHook` invokes global hooks — script can intercept app-wide | Namespace hooks per script |
| 974 | LOW | MAINTAINABILITY | `luaL_loadfile` assumes UTF-8 paths | Use platform-native path APIs |

---

### `src/scripting/ScriptPermissions.h`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 9-19 | INFO | SECURITY | Scripts start sandboxed by default | Good |
| 13-19 | INFO | DESIGN | No distinct permissions for IRC raw, MC DATA, terminals | Extend permission model |
| 30-48 | MED | DESIGN | `toMap()`/`fromMap()` omit `allowedDirs` | Include in serialization or document external storage |

---

### `src/services/LinkPreviewClassifier.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 50-82 | LOW | SECURITY | `isPublicIpv4Literal` omits some special-use ranges | Align with RFC 6890 |
| 180-196 | LOW | SECURITY | X/Twitter previews proxy through `fxtwitter.com` | Document privacy; allow opt-out |
| 219-242 | INFO | SECURITY | Scheme and userinfo guard | Good |
| 233-235 | MED | CORRECTNESS | Hosts containing `:` rejected — blocks all IPv6 URLs | Parse IPv6 literals |
| 247-280 | INFO | SECURITY | DNS public-only checks | Good |
| 268-281 | MED | SECURITY | `resolvesToPublicOnly()` uses blocking `QHostInfo::fromName` | Prefer async everywhere |
| 294-296 | **HIGH** | SECURITY | `allowPrivateNetwork=true` skips all SSRF checks | Audit all call sites |
| 300-331 | MED | SECURITY | Redirect SSRF mitigation async — TOCTOU race | Disable auto-redirect |

---

### `src/services/OpenGraphFetcher.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 16-18 | LOW | CORRECTNESS | Empty `Content-Type` treated as HTML | Require `text/html` or sniff |
| 60-128 | **HIGH** | SECURITY | No `downloadProgress` size cap during transfer | Mirror `ImageFetcher` streaming abort |
| 78-88 | MED | SECURITY | Redirect SSRF re-check async — TOCTOU race | Same fix as classifier |
| 111-112 | LOW | BUG | Reads `maxBytes+1` but does not abort early | Add progress handler |

---

### `src/services/ImageFetcher.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 66-76 | INFO | IMPROVEMENT | Good streaming size cap via `downloadProgress` | Extend to OpenGraphFetcher |
| 78-92 | MED | SECURITY | Redirect SSRF guard async — TOCTOU race | Disable auto-follow |
| 110-116 | LOW | SECURITY | Empty `Content-Type` accepted | Reject or magic-byte sniff |
| 124-128 | MED | SECURITY | `QImage::loadFromData` on untrusted bytes — decompression bombs | Pixel budget; format whitelist |

---

### `src/services/OpenGraphParser.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 51-56 | LOW | CORRECTNESS | Numeric HTML entities accept any codepoint | Validate ≤ `0x10FFFF`; reject surrogates |
| 73-79 | MED | SECURITY | `resolveUrlText` without scheme filtering | Restrict to `http`/`https` |
| 117-123 | LOW | MAINTAINABILITY | Regex HTML parsing fragile | Acceptable for previews |
| 177-181 | MED | SECURITY | `card.imageUrl`/`canonicalUrl` without scheme checks at parse | Filter here (defense in depth) |

---

### `src/services/LinkPreviewRenderer.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 6-8 | INFO | SECURITY | `htmlUrl` uses `FullyEncoded` + `toHtmlEscaped` | Good XSS mitigation |
| 12-17 | INFO | CORRECTNESS | Surrogate-pair-safe truncation | Good |
| 73-83 | INFO | SECURITY | `http`/`https`-only for click-through `canonicalUrl` | Extend to `card.imageUrl` |
| 132-137 | MED | SECURITY | `card.imageUrl` in `<img src>` without scheme whitelist | Require `http`/`https` |

---

### `src/upload/ImgbbUploader.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 18-20 | MED | SECURITY | API key stored as plain `QString` | Secure wipe; OS keychain |
| 44-47 | **HIGH** | SECURITY | API key in **query string** — leaks via logs/Referer | Prefer header or POST field |
| 49-78 | MED | MAINTAINABILITY | No timeout; unbounded JSON read | Add timeout + size cap |
| 69-77 | MED | SECURITY | Returned URL not validated | Scheme whitelist |

---

### `src/upload/ImgurUploader.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 17-19 | MED | SECURITY | Client-ID stored as plain `QString` | Secure wipe |
| 26-27 | INFO | SECURITY | CR/LF stripping on Client-ID prevents header injection | Good |
| 45-77 | MED | MAINTAINABILITY | No timeout; unbounded JSON read | Add timeout + size cap |
| 67-75 | MED | SECURITY | `data.link` not validated | Require `http`/`https` |

---

### `src/upload/PostimagesUploader.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 17-20 | MED | SECURITY | API token stored as plain `QString` | Secure wipe |
| 52-78 | MED | MAINTAINABILITY | No timeout or response size cap | Add both |
| 70-76 | MED | SECURITY | `direct_link`/`url` not validated | Scheme whitelist |

---

### `src/upload/ImgboxUploader.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 18-21 | **HIGH** | SECURITY | Username/password as plain `QString` members | Secure wipe on destroy |
| 38-48 | MED | SECURITY | Password in JSON body to API | Ensure QNetwork logging disabled |
| 47-74 | MED | MAINTAINABILITY | Token creation reply has no timeout | Add `QTimer` timeout |
| 57-58 | LOW | SECURITY | `readAll()` unbounded on token reply | Cap response size |
| 123-130 | MED | SECURITY | `original_url` not validated before emit | Allow only `http`/`https` |

---

### `src/upload/ImageUploaderFactory.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 19-38 | MED | SECURITY | Credentials read from QVariantMap with no validation | Trim keys; reject embedded newlines |
| 40 | INFO | DESIGN | Returns `nullptr` when disabled | Good safe default |

---

### `src/ui/MainWindow.cpp` (9,853 lines — sampled and targeted review)

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 1-9853 | **HIGH** | DESIGN | God-object: IRC, scripting, DCC, MC DATA, themes, comic, previews, settings | Extract subsystems |
| 706-715 | MED | BUG | `terminalScopedId` uses `/` delimiter; script names with `/` break routing | Use reserved delimiter |
| 921-925 | **HIGH** | BUG | DCC CTCP routed via `connection()` (active net), not origin network | Use `connectionForNetwork(net)` |
| 924-925 | MED | SECURITY | DCC negotiation tokens appear in chat buffer verbatim | Redact token in UI/log |
| 975-1017 | LOW | DESIGN | Terminal hooks pin network at open — good pattern | Extend to DCC/MC DATA |
| 2653-2657 | **HIGH** | SECURITY | `scriptSendRaw` no permission gate | Gate in LuaEngine |
| 2660-2677 | **HIGH** | SECURITY | `scriptMcData` no permission gate | Gate in LuaEngine |
| 2720-2730 | LOW | IMPROVEMENT | `scriptTerminalFrame` no cap on ops string length | Cap frame size (64 KiB) |
| 2813-2878 | **HIGH** | SECURITY/DESIGN | `scriptHttpGet` nested `QEventLoop::exec()` on GUI thread | Async HTTP |
| 4297-4305 | LOW | IMPROVEMENT | PM notification title: `stripFormatting TBD` | Apply `stripFormatting(sender)` |
| 4489-4496 | LOW | DESIGN | `dccRequest` uses per-network `signalNetwork` but outbound path does not | Pair inbound/outbound |
| 4503-4510 | MED | SECURITY | `mcDataReceived` dispatches to **all** scripts with no filter/rate limit | Route by service; rate cap |
| 6760-6777 | INFO | IMPROVEMENT | Chat preview image alignment fixes | Good |
| 7442-7496 | LOW | SECURITY | `handleChatAnchorClicked` default branch opens URL without scheme filter | Reuse `openableUrl()` |
| 7460-7491 | INFO | SECURITY | Direct media click uses async public-network check | Good |
| 8329-8333 | INFO | DESIGN | Link previews avoid comic panels | Good |
| 9050-9062 | INFO | SECURITY | Passwords flow through in-memory `ConnectConfig` from settings | Document; keychain later |
| 921-925 | MED | SECURITY | See DCC CTCP routing above | See above |
| 9240-9272 | MED | BUG | Theme-off leaves `chromeFontCss` in stylesheet | Fix or update test |
| 9257-9261 | LOW | SECURITY | Font family interpolated into QSS without escaping | Sanitize family names |
| 9265-9269 | MED | BUG | **Theme test failure** root cause | See P1 finding |

---

### `src/ui/DccManager.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 114-118 | **HIGH** | SECURITY | Passive token is 32 bits (8 hex chars) | Use ≥128 bits |
| 123-137 | INFO | IMPROVEMENT | `dccWritableChunk` refuses unbounded writes | Good |
| 211-212 | MED | BUG | `advertisedIp()` defaults to `127.0.0.1` | Auto-detect routable IPv4 |
| 215-223 | MED | SECURITY | Listeners bind `QHostAddress::Any` on all interfaces | Document; optional bind to advertised iface |
| 294-305 | INFO | SECURITY | Passive reply verifies sender nick + token | Good |
| 324-334 | **HIGH** | SECURITY | Active send accepts first TCP peer — no IP check | Verify `peerAddress()` |
| 460-476 | INFO | SECURITY | Passive send token replay protected by nick binding | Good |
| 470-475 | INFO | SECURITY | `sameNick(sender, t->peer)` check | Keep |
| 601-607 | INFO | IMPROVEMENT | Passive receive verifies peer IPv4 | Good |
| 764-847 | MED | BUG | `finishTransfer`/`cancelTransfer` never erase `runtimes_[id]` | `runtimes_.remove(id)` |
| 867-870 | **HIGH** | BUG | `offerChat` inserts without tearing down existing session | `teardownChat` first |
| 872-885 | INFO | SECURITY | Pending chat token expires after two minutes | Good |
| 894-904 | **HIGH** | SECURITY | Active chat accepts first connection without peer verify | Match passive chat check |
| 1002-1005 | INFO | CORRECTNESS | `acceptIncomingChat` tears down first | Good pattern |
| 1029-1035 | INFO | IMPROVEMENT | Passive chat verifies connecting peer IPv4 | Good |
| 1076-1092 | LOW | SECURITY | Chat enforces `ChatBufferCap` (65536) and `ChatLineCap` (8192) | Good anti-DoS |

---

### `src/ui/ScriptTerminalManager.cpp` / `ScriptTerminalDialog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| ScriptTerminalManager.cpp:71-76 | LOW | DESIGN | `closeTerminal` only hides; session lives until `killTerminal` | Document |
| ScriptTerminalManager.cpp:78-91 | LOW | BUG | `killTerminal` emits `terminalClosed` but dialog never emits it | Remove dead signal |
| ScriptTerminalDialog.cpp:64-79 | INFO | IMPROVEMENT | Paste guard (2048 bytes / 20 lines) | Good |
| ScriptTerminalDialog.cpp:138-145 | LOW | SECURITY | Hotspot action id passed to Lua without length cap | Cap action id length |
| ScriptTerminalDialog.cpp:214-281 | MED | SECURITY | `applyFrame` parses unbounded op streams | Cap ops bytes/count/grid |
| ScriptTerminalDialog.cpp:466-500 | LOW | SECURITY | Frame HTML uses `htmlEscapedChar`; `setOpenLinks(false)` | Good XSS posture |

---

### `src/ui/TerminalFrame.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 55-76 | LOW | CORRECTNESS | `readUtf8Text` rejects UTF-8 codepoint splits | Good |
| 81-166 | MED | SECURITY | Parser has no max ops length or op-count limit | `MaxOps = 4096`, `MaxOpsBytes = 65536` |
| 107 | LOW | MAINTAINABILITY | Duplicate ternary in color mapping (cppcheck) | Inspect; simplify |
| 129-134 | LOW | DESIGN | `W` opcode max 255; `X` allows 65535 — asymmetric | Align with MC DATA 350-byte wire cap |
| 147-158 | MED | SECURITY | `ExtendedWrite` allows up to 65535 bytes per op | Cap at application layer |

---

### `src/ui/AnsiRenderer.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 59-68, 117-164 | LOW | SECURITY | `toHtml` escapes via `toHtmlEscaped()`; hotspot href percent-encoded | Good pattern |

---

### `src/ui/ComicView.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 84-100 | LOW | CORRECTNESS | `gridFor()` assumes `count > 0` — cppcheck divide-by-zero | `Q_ASSERT(count > 0)` or guard |
| 116-123 | LOW | CORRECTNESS | Callers guard `panels_.isEmpty()` before call | Low risk today |

---

### `src/ui/ThemeCatalog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 77-89 | LOW | DESIGN | Asset directory resolution includes CWD | Prefer app-dir-relative first (already ordered) |
| 609-618 | LOW | SECURITY | Wallpaper QSS uses validated absolute file paths only | Good |

---

### `src/ui/UrlListDialog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 16-31 | INFO | SECURITY | `openableUrl()` permits only http/https/ftp | Reuse in ServerListDialog |

---

### `src/ui/ServerListDialog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 92-108 | MED | SECURITY | Password fields in UI; plaintext persistence | See SettingsStore |
| 371-375 | MED | SECURITY | `openHomepage()` opens URL without scheme guard | Reuse `openableUrl()` |

---

### `src/ui/PreferencesDialog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 736 | INFO | MAINTAINABILITY | Comment references `update_check` release TODO | Release checklist |
| 2167-2283 | MED | SECURITY | API keys/passwords saved plaintext | Keychain |
| 2415 lines | LOW | DESIGN | Second large dialog overlapping MainWindow settings | Split controllers |

---

### `src/ui/GeometryPersist.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 19-20 | LOW | BUG | Lambda captures `store` by reference — UB if dialog outlives store | Capture pointer or `QPointer` |

---

### `src/ui/ImageViewerDialog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 47-51 | INFO | SECURITY | SSRF gate via `isAllowedPreviewFetchUrl` + `ImageFetcher` | Good |

---

### `src/ui/MediaPlayerDialog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 82 | MED | SECURITY | No SSRF check inside dialog; relies on caller | Add gate in ctor |

---

### `src/comic/ComicArt.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 49-88 | INFO | SECURITY | Hostile `.avb/.bgb` guards: `MaxDibDim` 4096, `MaxInflateBytes` 32 MiB | Good |
| 416-434 | LOW | DESIGN | `scanArtDir` only lists one directory level | Document |

---

### `src/comic/ComicCharacter.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 201-213 | LOW | MAINTAINABILITY | Static `QHash` cache never evicted | LRU cap |

---

### `src/comic/ComicRenderer.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 27-32 | LOW | DESIGN | Module-global font family mutated per render | Pass as parameters |
| 266-283 | LOW | MAINTAINABILITY | Font-fit loop down to 6pt can be expensive | Cache layout if profiling shows jank |

---

### `src/spell/SpellcheckDictionaryCatalog.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 69-71 | INFO | DESIGN | Finnish listed but dictionary missing | Placeholder or bundle |
| 120-125 | INFO | DESIGN | Display label `Finnish [dictionary missing]` | Intentional placeholder |

---

### `src/spell/WindowsSpeller.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 36-75 | LOW | CORRECTNESS | Windows COM initialization left unbalanced intentionally | Verify on Windows builds |

---

### `src/main.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 21-52 | INFO | IMPROVEMENT | Translator install before main window — correct lifetime | OK |
| 57-102 | LOW | DESIGN | `--selftest` forces offscreen; constructs full `MainWindow` | Consider lighter selftest |

---

### `src/app/AppInfo.cpp`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 13-18 | INFO | MAINTAINABILITY | Version `0.1.0-dev`; experiment description | Generate from build system |

---

### `assets/scripts/bbs.lua` (1,568 lines)

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 26-27 | MED | SECURITY | Demo credentials `sir_iw` / `bbsiscool` hardcoded | Keep demo-only; label in UI |
| 1025-1043 | MED | SECURITY | Login sends plaintext password over MC DATA | No real accounts until `MC SEC` |
| 1275-1284 | INFO | DESIGN | Sessions keyed by network, nick, BBS id | Good |
| 1293-1297 | INFO | SECURITY | NOTICE-borne BBS traffic dropped — avoids reply loops | Good |
| 1301-1308 | INFO | SECURITY | HELLO replies throttled per peer | Good |
| (whole file) | MED | MAINTAINABILITY | Large stateful script; limited automated coverage | Add integration tests |

---

### `tools/mcb1_convert.py`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 125-132 | MED | BUG | `raw_bytes(bits)` assumes len divisible by 8 | Pad final byte with zero bits |
| 212-218 | MED | BUG | CLI allows custom sizes where `width*height` not divisible by 8 | Validate or pad |

---

### `tools/gen_sounds.py`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| (whole file) | INFO | DESIGN | Build-time sound generator | No security surface |

---

### `CMakeLists.txt`

| LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|-----|-----|---------|----------------|
| 3-6 | INFO | MAINTAINABILITY | `VERSION 0.1.0`, experiment description | Update before release |
| 13-16 | LOW | MAINTAINABILITY | Lua ON by default; comment at 55-57 says C compiler not required for default — stale | Update comment |
| 55-57 | LOW | MAINTAINABILITY | Stale comment contradicts `enable_language(C)` when Lua ON | Fix comment |
| 210-213 | LOW | MAINTAINABILITY | OS spell OFF by default in CMake; ON in `build.bat` | Document both paths |
| 338-341 | INFO | DESIGN | `WIN32_EXECUTABLE TRUE` — no console on Windows | Good |

---

### `tests/unit/` (53 targets)

| FILE | LINE | SEV | CAT | FINDING | RECOMMENDATION |
|------|------|-----|-----|---------|----------------|
| main_window_link_preview_test.cpp | 464-466 | MED | BUG | Expects empty stylesheet after Themes Off | Fix implementation or test |
| image_uploader_test.cpp | 173-199 | LOW | MAINTAINABILITY | Tests parse JSON directly, not full network path | Loopback server tests |
| lua_engine_test.cpp | 580-584 | INFO | DESIGN | Uses demo BBS credentials in tests | OK for demo |
| (suite overall) | — | INFO | DESIGN | 53 CTest targets; broad coverage | Add OpenGraph oversize abort test |

---

### Remaining `src/ui/` dialogs (brief — no direct security surface)

`AliasEditorDialog`, `AudioPlayerBar`, `BanListDialog`, `ChannelListDialog`, `ChannelModesDialog`, `ChatFindDialog`, `ColorPickerDialog`, `ComicSettingsDialog`, `ComicView`, `DccTransfersDialog`, `FriendsNotifyDialog`, `IgnoreListDialog`, `Notifier`, `QuickConnectDialog`, `RawLogDialog`, `ShortcutEditorDialog`, `SoundPlayer`, `SpellTextEdit`, `SpellcheckHighlighter`, `SystemInfo`, `TerminalProfile`, `ThemeEditorDialog`, `ToastWidget`, `AppIcon` — reviewed; no HIGH/CRITICAL findings. These are CRUD/settings UI with data passed through validated upstream paths.

---

## Subsystem Review

### Build System

**Good:**

- `WIN32_EXECUTABLE TRUE` at `CMakeLists.txt:338-341`
- `build.bat` validates Qt kit vs source tree
- `sync-to-win.sh` preserves CRLF for `build.bat`

**Needs cleanup:**

- Tracked `Testing/Temporary` files
- Stale CMake comment around Lua/default C compiler
- OS spell default differs between CMake and `build.bat` — document both paths

### IRC Connection And Session

**Good:**

- Outbound CRLF stripping (`IrcSession.cpp:145-150`)
- 512-byte wire cap + UTF-8-safe splitting (`IrcSession.cpp:153-196`)
- PING/PONG bypasses flood delay (`IrcSession.cpp:202-205`)
- SASL PLAIN chunked at 400 bytes (`IrcSession.cpp:375-386`)
- Plaintext auth blocked unless TLS/opt-in (`IrcSession.cpp:1014-1025`)
- Incoming flood caps (`IrcConnection.cpp:286-304, 414-416`)
- MC DATA 350-byte payload cap (`IrcSession.cpp:18, 297-306`)

**Risks:**

- IRC target parameter injection (HIGH)
- TLS cert bypass when `accept_invalid_cert` set
- `/raw`, `/oper`, `/kill` intentionally powerful — document

### Link Previews And External URLs

**Good:**

- Private network/metadata protections (`LinkPreviewClassifier.cpp`)
- Async resolver + redirect re-checks
- Direct media click SSRF gate (`MainWindow.cpp:7460-7491`)
- `UrlListDialog` scheme guard (`UrlListDialog.cpp:16-31`)

**Risks:**

- OpenGraph body cap too late
- Redirect TOCTOU race
- Server homepage / anchor click scheme gaps

### DCC

**Good:**

- File path sanitization, `.part` files, bounded writes
- Passive reply nick + token binding
- Passive receive/chat peer IP verification
- Chat buffer/line caps

**Risks:**

- Active send/chat first-connection-wins (P0)
- 32-bit passive tokens
- Chat re-offer session leak (P0)
- `runtimes_` accumulation

### Lua Scripting

**Good:**

- Sandbox defaults off (`ScriptPermissions.h:9-19`)
- `os.exit` always removed
- Native module loading disabled
- File access jailed; timers min 50 ms
- Bytecode loading text-only

**Risks:**

- Ungated IRC/MC/terminal APIs (P0)
- Symlink jail escape
- `package.path` not restricted with `loadModules`
- `runPrograms` exposes shell execution

### MC DATA And Retro-BBS

**Good:**

- Wire cap 350 bytes
- BBS HELLO throttled; NOTICE traffic dropped
- Session keyed by network/nick/BBS id
- Terminal ANSI/frame rendering tested

**Risks:**

- Demo plaintext login must stay demo-only
- MC DATA broadcast to all scripts without filter
- Terminal/frame parser lacks global size caps
- `bbs.lua` large and under-tested

### Theme And Wallpaper System

**Good:**

- JSON-based built-in and user themes
- Built-in deletion protection via `isUserThemeId()`

**Risks:**

- Theme-off test failure (`chromeFontCss`)
- CWD in asset resolution path

### Spellcheck

**Good:**

- Vendored Hunspell bundled
- Windows OS spell behind `MAXCHAT_OS_SPELL`
- Missing dictionaries get explicit label
- Tests pass

**Risks:**

- Finnish placeholder without bundled dictionary
- Windows COM init unbalanced — verify on Windows

### Tests

**Good:**

- 53 CTest targets
- Coverage: IRC, DCC, settings, link previews, spellcheck, UI dialogs, terminal, scripting, chat buffers
- 52/53 pass offscreen

**Needs improvement:**

- Fix or update theme-off test
- Document headless test command (`QT_QPA_PLATFORM=offscreen`)
- OpenGraph oversized body abort test
- Uploader end-to-end loopback tests
- `mcb1_convert.py` non-byte-aligned size test
- BBS/MC DATA integration tests

---

## Release Readiness Checklist

Before a public C++ build:

- [ ] Fix active DCC send/chat peer verification (`DccManager.cpp:324-334`, `894-904`)
- [ ] Fix DCC chat re-offer session leak (`DccManager.cpp:867-870`)
- [ ] Gate Lua `send_raw` / `mc_*` behind permissions (`LuaEngine.cpp:227-255`)
- [ ] Fix or document `Themes Off` behavior/test mismatch
- [ ] Decide local credential storage policy; update UI/docs
- [ ] Validate IRC targets before wire emission
- [ ] Make `scriptHttpGet` async (no GUI-thread blocking)
- [ ] Add early body cap to OpenGraph fetcher
- [ ] Fix SSRF redirect TOCTOU in fetchers
- [ ] Canonicalize symlinks in Lua file jail
- [ ] Add upload timeouts, response caps, URL validation
- [ ] Increase DCC passive tokens to 128+ bits
- [ ] Remove tracked CTest temp files; add `Testing/` to `.gitignore`
- [ ] Update product version/name/description from dev/experiment wording
- [ ] Decide whether `update_check` should default on
- [ ] Document headless test command
- [ ] Keep MC DATA/BBS marked demo/plaintext until secure mode exists
- [ ] Fix `mcb1_convert.py` non-byte-aligned crash

---

## Overall Assessment

The C++ port is a mature, feature-rich IRC client with unusually strong defensive coding for its stage: outbound IRC line injection is blocked at the CRLF layer, link previews have SSRF gates, script permissions default off for file/exec/network, DCC has bounded writes and filename sanitization on passive paths, and the test suite is broad (52/53 passing offscreen).

**No critical remote-code-execution or memory-corruption bugs** were found in project `src/` during this audit. The highest-impact risks are operational and trust-boundary gaps rather than missing functionality:

1. **Active DCC listeners accept any TCP peer** — highest immediate security gap.
2. **Lua IRC/MC APIs bypass the permission model** — largest scripting trust-boundary failure.
3. **Plaintext credential storage** in `settings.json`.
4. **IRC target parameter injection** — spaces in targets not validated before wire emission.
5. **`MainWindow.cpp` at ~9,853 lines** — god-object coupling drives regression risk (theme test failure is symptomatic).

The most important remaining work is **release hardening**, not basic functionality: make the test suite green, harden DCC active mode, gate script IRC APIs, protect or clearly label saved secrets, cap network fetch/upload bodies, and remove dev wording before packaging.

---

*Audit performed by XAI (Grok) on 2026-06-15. Branch `mc-data-terminal`, commit `cc88713`.*