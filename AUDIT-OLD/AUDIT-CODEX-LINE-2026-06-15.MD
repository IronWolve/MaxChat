# MaxChat C++ Line Audit - 2026-06-15

## Objective

Perform a full line-by-line audit of the MaxChat C++ port in `/home/iw/work/claude/maxchat-c`, with attention to code correctness, security, stability, release readiness, test coverage, build hygiene, and implementation drift from the intended MaxChat behavior.

## Scope

Reviewed line-by-line:

- `CMakeLists.txt`
- `build.bat`
- `sync-to-win.sh`
- Core C++ source and headers under `src/`
- Unit tests under `tests/unit/`
- Lua scripts under `assets/scripts/`
- Helper tools under `tools/`
- Runtime resource manifest `resources/maxchat.qrc`
- Primary docs: `README.md`, `SCRIPTING.md`, `DEVDOCS/MC_DATA.md`, `DEVDOCS/MC_DATA_PHASES.md`

Inventoried but not manually byte-reviewed:

- Binary assets: fonts, icons, sounds, wallpapers
- Hunspell dictionary `.aff` / `.dic` wordlists
- Third-party vendored code
- Build directories and generated CMake output

Scale checked:

- Tracked files: 413
- Reviewed source/docs/tests/tools line count: about 48,916 lines
- Current repo state before this report: one existing untracked audit file, `AUDIT-CODEX-2026-06-15.MD`
- Current HEAD: `cc88713 docs: HANDOFF closeout for MC DATA BBS session`

## Verification

Commands run:

```text
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
cppcheck --enable=warning,style,performance,portability --std=c++20 --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction --suppress=unmatchedSuppression src tests
rg risk/TODO/security patterns across source, tests, docs, scripts, and build files
```

Results:

- Build passes.
- Plain `ctest` fails 5 tests in this shell because four GUI/media tests cannot initialize the `xcb` Qt platform plugin without a display. This is environmental.
- With `QT_QPA_PLATFORM=offscreen`, 52 of 53 tests pass.
- The one real test failure is `main_window_link_preview`, specifically `themeMenuChangesSavedThemeAndApplicationStyle`.
- `cppcheck` produced mostly style suggestions. One useful robustness warning appears in `ComicView::gridFor`, discussed below.

## Priority Findings

### P1 - Themes Off Test And Implementation Disagree

Evidence:

- `src/ui/MainWindow.cpp:9240-9272`
- `tests/unit/main_window_link_preview_test.cpp:464-466`

The test expects `qApp->styleSheet().isEmpty()` after selecting `Themes Off`. The implementation intentionally leaves `chromeFontCss` installed even for the system theme:

```text
qApp->setStyleSheet(chromeFontCss);
```

Impact:

- Current test suite is red: 52/53 passing offscreen.
- Product behavior needs a decision: either "Themes Off" truly means no application stylesheet, or it means no color/theme stylesheet while preserving the app chrome font.

Recommendation:

- If "Themes Off" should mean no stylesheet at all, move chrome font handling out of app-wide QSS and use widget/font application only.
- If current behavior is intentional, rename the behavior in tests and assert that theme CSS is absent while chrome font CSS remains.

### P1 - Local Secrets Are Stored In Plain Settings

Evidence:

- `src/core/SettingsStore.cpp:299-307`
- `src/core/ConnectionPlan.cpp:99-111`
- `src/ui/ServerListDialog.cpp:92-108`
- `src/ui/PreferencesDialog.cpp:2167-2171`, `2202-2206`, `2237-2241`, `2279-2283`

Stored sensitive values include:

- NickServ/SASL password
- Server PASS password
- Proxy password
- ImgBB API key
- Imgur Client-ID
- Postimages API token
- Imgbox username/password

The UI correctly uses password fields for some values and warns about plaintext network auth, but the local persistence model is still plain config storage.

Impact:

- Anyone with access to the config file can read saved secrets.
- This matters before a public release because people will assume saved passwords are protected.

Recommendation:

- Add a visible warning near saved credentials if this remains intentional.
- Prefer OS credential storage later: Windows Credential Manager, libsecret/KWallet on Linux, Keychain on macOS if supported.
- At minimum, separate "remember password/token" from temporary session-only credentials.

### P1 - MC DATA/BBS Demo Login Must Not Become Real Auth

Evidence:

- `assets/scripts/bbs.lua:26-27`
- `assets/scripts/bbs.lua:1025-1043`
- `DEVDOCS/MC_DATA.md:710-717`

The BBS demo has the requested account:

```text
sir_iw / bbsiscool
```

The MC DATA spec correctly says v1 is plaintext and must not carry secrets. The BBS script sends typed login data over MC DATA.

Impact:

- Fine for a demo account.
- Not acceptable for real accounts, private mail, moderation powers, or anything user-sensitive.

Recommendation:

- Keep this labeled as demo-only.
- Before real BBS accounts, add `MC SEC` or another reviewed authenticated/encrypted layer.
- Until then, do not store or transmit real passwords over MC DATA.

### P2 - OpenGraph Fetcher Caps The Body Too Late

Evidence:

- `src/services/OpenGraphFetcher.cpp:60-128`
- `src/services/ImageFetcher.cpp:66-76`

`ImageFetcher` aborts as soon as `downloadProgress` exceeds `maxBytes`. `OpenGraphFetcher` only reads `maxBytes + 1` in `finished`.

Impact:

- A large HTML response can be buffered by Qt before the application truncates it.
- This is not as dangerous as fetching private network targets because SSRF checks are solid, but it is still avoidable memory pressure.

Recommendation:

- Copy the `downloadProgress` early-abort pattern from `ImageFetcher` into `OpenGraphFetcher`.
- Add a test that serves an oversized HTML body and verifies the fetch aborts before completion.

### P2 - Uploaders Need Timeouts, Response Caps, And Stronger Tests

Evidence:

- `src/upload/ImgbbUploader.cpp:49-79`
- `src/upload/ImgurUploader.cpp:45-77`
- `src/upload/PostimagesUploader.cpp:52-78`
- `src/upload/ImgboxUploader.cpp:44-74`, `105-131`
- `tests/unit/image_uploader_test.cpp:173-199`

The uploaders encode PNGs and post to fixed HTTPS endpoints, which is good. But they do not set request timeouts or response body caps. Several tests only parse JSON directly rather than exercising the uploader network path.

Impact:

- A slow or stalled upload service can leave an operation hanging.
- A bad response can be read with `readAll()` without a cap.
- Real behavior coverage is thinner than it looks.

Recommendation:

- Add per-request timeout handling.
- Cap response body size before JSON parsing.
- Allow test injection of endpoint URLs or network replies so all uploaders can be tested end-to-end against a loopback server.

### P2 - DCC Passive Tokens Are Only 32 Bits

Evidence:

- `src/ui/DccManager.cpp:114-118`
- `src/ui/DccManager.cpp:294-305`
- `src/ui/DccManager.cpp:460-476`
- `src/ui/DccManager.cpp:872-885`

The code uses `QRandomGenerator::system()` and verifies the sender nick before accepting a passive reply. That is good. The token itself is one 32-bit value formatted as 8 hex chars.

Impact:

- 32 bits is probably okay for casual DCC handshakes with short TTLs, but it is thin for a token described as the barrier against forged passive replies.

Recommendation:

- Use at least 128 bits of random token material.
- Keep the sender-nick binding and TTL expiration.

### P2 - Server Homepage Button Opens Unvalidated URLs

Evidence:

- `src/ui/ServerListDialog.cpp:371-375`
- `src/ui/UrlListDialog.cpp:16-31`

`UrlListDialog` has a good `openableUrl()` guard that permits only `http`, `https`, and `ftp`. `ServerListDialog::openHomepage()` directly hands the stored website string to `QDesktopServices::openUrl`.

Impact:

- Default server homepages are trusted and tests assert they are `http`/`https`.
- User-edited imported network data could still contain a non-web scheme that launches an external handler.

Recommendation:

- Reuse the `UrlListDialog` scheme guard or add a small shared helper for external URL opening.
- Restrict server homepages to `http` and `https`.

### P2 - Generated CTest Files Are Tracked

Evidence:

- `git ls-files Testing/Temporary`
- `.gitignore:1-14`

Tracked generated files:

```text
Testing/Temporary/CTestCostData.txt
Testing/Temporary/LastTest.log
```

`.gitignore` ignores build directories but not tracked CTest output.

Impact:

- Test runs can dirty the repo or cause confusing diffs.

Recommendation:

- Add `Testing/` to `.gitignore`.
- Remove the tracked files from Git with `git rm --cached Testing/Temporary/CTestCostData.txt Testing/Temporary/LastTest.log`.

### P2 - Qt GUI Tests Need A Stable Headless Test Mode

Evidence:

- Plain `ctest` failed GUI/media tests due missing display/xcb.
- `env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure` reduced failures to the one real theme test failure.

Impact:

- CI or headless development shells will show noisy failures unless the platform is set.

Recommendation:

- Add a documented test command using `QT_QPA_PLATFORM=offscreen`.
- Consider setting `ENVIRONMENT QT_QPA_PLATFORM=offscreen` for relevant tests in CMake, or create a `test-headless` script.

### P2 - `mcb1_convert.py` Crashes On Some Valid Custom Sizes

Evidence:

- `tools/mcb1_convert.py:125-132`
- `tools/mcb1_convert.py:212-218`

`raw_bytes(bits)` assumes the number of bits is divisible by 8:

```text
bits[i + j]
```

The CLI allows sizes where `width * height` is not divisible by 8.

Impact:

- Default sizes are safe.
- A user-provided custom size like `81x25` can crash during raw encoding.

Recommendation:

- Pad the final byte with zero bits.
- Add a unit/smoke test for non-byte-aligned dimensions.

## Medium Priority Observations

### P3 - Version And Product Name Still Say Dev/Experiment

Evidence:

- `CMakeLists.txt:3-6`
- `src/app/AppInfo.cpp:13-18`

The project metadata says:

```text
VERSION 0.1.0
DESCRIPTION "Native C++/Qt port experiment for MaxChat"
```

The app info still reports `0.1.0-dev` and `MaxChat C++`.

Impact:

- Fine for development.
- Not release-ready if this is meant to look like a public MaxChat build.

Recommendation:

- Decide whether the C++ port is its own product build or the next MaxChat line.
- Update version/display strings before packaging.

### P3 - Build Option Defaults Are Slightly Confusing

Evidence:

- `CMakeLists.txt:13-16`
- `CMakeLists.txt:55-57`
- `CMakeLists.txt:210-213`
- `build.bat:82-102`

Lua is now ON by default, so the comment saying a C compiler is not required for the default build is stale. OS spell is OFF by default in CMake but ON by default in `build.bat`.

Impact:

- Developers can misunderstand which build path is the release-like path.

Recommendation:

- Update comments to say:
  - CMake default: Lua ON, OS spell OFF.
  - Windows `build.bat` default: Lua ON, OS spell ON.

### P3 - Update Check Is Still Release-Gated

Evidence:

- `src/core/SettingsStore.cpp:435-439`
- `src/ui/PreferencesDialog.cpp:736`

The default update check remains false with a TODO to flip at release.

Impact:

- Fine before launch.
- Easy to forget during packaging.

Recommendation:

- Put this in the release checklist.

### P3 - Notification Formatting TODO

Evidence:

- `src/ui/MainWindow.cpp:4297-4305`

Private message notification title uses:

```text
const QString stripped = sender;  // stripFormatting TBD
```

Impact:

- Low risk unless sender strings can contain IRC formatting or unexpected control characters in the notification title.

Recommendation:

- Apply `maxchat::irc::stripFormatting(sender)` and trim control characters.

### P3 - Finnish Is Listed But Dictionary Is Missing

Evidence:

- `src/spell/SpellcheckDictionaryCatalog.cpp:69-71`
- `src/spell/SpellcheckDictionaryCatalog.cpp:120-125`

Finnish appears in the language list but no bundled `fi_FI.aff`/`.dic` exists. The display label intentionally becomes:

```text
Finnish [dictionary missing]
```

Impact:

- This matches the placeholder plan.
- It may look unfinished to users if too many missing languages ship.

Recommendation:

- Either keep the placeholder intentionally or bundle Finnish if licensing is acceptable.

### P3 - `ComicView::gridFor` Should Guard Its Precondition

Evidence:

- `src/ui/ComicView.cpp:84-100`
- `src/ui/ComicView.cpp:116-123`

`gridFor()` assumes `count > 0`. Callers currently guard `panels_.isEmpty()` before calling it, so this is likely safe. `cppcheck` still reports a possible divide by zero.

Impact:

- Low risk today, but easy to harden.

Recommendation:

- Add `if (count <= 0) { rows = cols = edge = 0; return; }` or `Q_ASSERT(count > 0)`.

## Subsystem Review

### Build System

Good:

- `WIN32_EXECUTABLE TRUE` is set at `CMakeLists.txt:338-341`, so the Windows app should not spawn a console window.
- Build script validates Qt kit vs source tree at `build.bat:29-38`.
- Build script copies runtime assets and license notices at `build.bat:178-190`, `277-317`.
- `sync-to-win.sh:55-60` preserves CRLF for `build.bat`, which matters for `cmd.exe` labels.

Needs cleanup:

- Tracked `Testing/Temporary` files.
- Stale CMake comment around Lua/default C compiler.
- OS spell default differs between raw CMake and `build.bat`, which is okay if documented.

### Application Metadata

Good:

- Version is centralized enough to test through `app_info_test`.

Needs cleanup:

- Dev/experiment wording should be changed before release packaging.

### Core Settings And Network Defaults

Good:

- Defaults set Synthwave theme, JetBrains Mono, bold fonts, spellcheck enabled, server/member list visible.
- Network merge preserves user-owned fields while adding new defaults in `SettingsStore.cpp:267-280`.
- Connection planning deduplicates endpoints in `ConnectionPlan.cpp:44-59`.

Risks:

- Secrets are local plaintext.
- Update check default is intentionally not launch-ready.

### IRC Connection And Session

Good:

- Outgoing IRC line injection is guarded by CR/LF stripping at `IrcSession.cpp:145-150`.
- Overlong `PRIVMSG`/`NOTICE` text is split at UTF-8-safe boundaries at `IrcSession.cpp:152-196`.
- PING/PONG bypasses flood queue delay at `IrcSession.cpp:202-205`.
- Send flood control uses an ircII-style penalty window at `IrcSession.cpp:223-237`.
- SASL PLAIN is chunked at 400 bytes at `IrcSession.cpp:375-386`.
- Password auth is blocked on plaintext IRC unless explicitly allowed at `IrcSession.cpp:1014-1025`.
- Connect timeout, registration timeout, idle probe, and backlog caps are implemented in `IrcConnection.cpp:81-87`, `173-175`, `279-324`, `371-390`.
- Incoming server floods are capped by line, pending bytes, per-tick line count, and total backlog in `IrcConnection.cpp:15-29`, `286-304`, `393-435`.

Risks:

- Accepting invalid TLS certs is implemented at `IrcConnection.cpp:358-368`; that is okay only if the UI makes it explicit per network.
- `/raw`, `/oper`, `/kill`, and unknown command passthrough are intentionally powerful. This matches IRC client expectations but should stay documented.

### Command Parser

Good:

- Broad command set is present in `CommandParser.cpp:103-150`.
- `/msg`, `/query`, `/notice`, `/me`, `/away`, `/back`, `/connect`, and `/server` are implemented and covered in tests.
- Service aliases map to NickServ/ChanServ/etc. at `CommandParser.cpp:75-99`, `335-351`.

Notes:

- `/umite` is accepted as an alias for `/unmute` at `CommandParser.cpp:581`; this appears intentional from prior typo compatibility.
- Unknown slash commands fall through to raw IRC at `CommandParser.cpp:717-721`, which is normal for IRC clients but can surprise novice users.

### Link Previews And External URLs

Good:

- Private network/metadata protections are strong:
  - Scheme and userinfo guard at `LinkPreviewClassifier.cpp:219-242`
  - DNS public-only checks at `LinkPreviewClassifier.cpp:247-280`
  - Async resolver at `LinkPreviewClassifier.cpp:300-331`
  - Redirect re-checks in both fetchers
- Direct media click handling reuses the async public-network check before playback at `MainWindow.cpp:7460-7491`.
- `UrlListDialog` refuses non-web schemes at `UrlListDialog.cpp:16-31`.

Risks:

- OpenGraph body cap happens too late.
- Server homepage button should use the same URL scheme guard.

### Image Uploads

Good:

- Upload service choice is factory-driven and disabled when not configured.
- Imgur Client-ID strips CR/LF before inserting into a header at `ImgurUploader.cpp:26-47`.
- Uploads use HTTPS endpoints.

Risks:

- No timeout handling.
- No response size caps before `readAll()`.
- Tests are partial and do not exercise all real uploader request/response paths.

### DCC

Good:

- File paths are sanitized, reserved Windows device names handled, and receive writes use `.part` files.
- Offered sizes are clamped and writes are bounded by the offered size.
- Passive reply tokens bind to the intended nick.
- Pending chat token expires after two minutes.
- DCC tests pass.

Risks:

- Passive token should be longer than 32 bits.
- DCC remains inherently peer-to-peer and exposes IP/port metadata by design; keep this clear in UI/docs.

### Lua Scripting

Good:

- Scripts start sandboxed by default in `ScriptPermissions.h:9-19`.
- Base Lua state excludes dangerous APIs unless permissions are granted.
- `os.exit` is always removed.
- Native module loading is disabled even when Lua module loading is allowed.
- File access is jailed through guarded paths.
- Timers have a minimum 50 ms interval to avoid runaway 0 ms loops.
- Permission UI clearly labels high-risk process execution.

Risks:

- If `runPrograms` is granted, Lua's `tmpname`, `execute`, `popen`, `remove`, `rename`, and environment access become available. This is correctly high-risk but should remain opt-in and visible.
- Vendored Lua links `tmpnam`, and the linker warns it is dangerous. The sandbox removes `os.tmpname` unless `runPrograms` is enabled, so this is controlled but worth tracking.

### MC DATA And Retro-BBS

Good:

- MC DATA payload cap is 350 bytes at `IrcSession.cpp:18`, `297-306`.
- BBS HELLO replies are throttled per peer at `bbs.lua:1301-1308`.
- BBS sessions are keyed by network, nick, and BBS id at `bbs.lua:1275-1284`.
- NOTICE-borne BBS traffic is dropped to avoid automatic reply loops at `bbs.lua:1293-1297`.
- Static frame/bitmap cache is implemented and documented.

Risks:

- Demo login is plaintext and must stay demo-only.
- `bbs.lua` is large and stateful; it needs more automated coverage before being treated as a real service platform.

### Terminal, ANSI, And Comic UI

Good:

- Terminal/profile code has focused tests.
- ANSI renderer is tested.
- Chat preview image alignment fixes were added around `MainWindow.cpp:6760-6777`.
- Link previews avoid comic panels by design at `MainWindow.cpp:8329-8333`.

Risks:

- `cppcheck` noted a duplicate ternary expression in `src/ui/AnsiRenderer.cpp:107`. It may be harmless, but it should be inspected because color code mapping is visual behavior.
- `ComicView::gridFor` should guard `count <= 0`.

### Theme And Wallpaper System

Good:

- Built-in and user themes are JSON-based.
- User theme folder gets a starter `_example.json`.
- Theme and wallpaper assets are loaded from deployable disk directories.
- Deleting user themes protects built-ins via `isUserThemeId()`.

Risks:

- Theme-off behavior is the current red test.
- Asset directory resolution includes current working directory at `ThemeCatalog.cpp:77-89`. This is convenient for dev but means runtime asset lookup can be affected by process cwd. For release builds, prefer app-dir-relative assets first, which the current order already does.

### Spellcheck

Good:

- Internal Hunspell is vendored and bundled.
- Windows OS spell is isolated behind `MAXCHAT_OS_SPELL`.
- Missing dictionaries get an explicit `[dictionary missing]` label.
- Spellcheck tests pass.

Risks:

- Finnish is intentionally listed without a bundled dictionary.
- Windows COM initialization is left unbalanced intentionally in `WindowsSpeller.cpp:36-75`. This is probably fine for app lifetime use, but it should be verified on Windows builds.

### Tests

Good:

- 53 CTest targets exist.
- Coverage includes IRC parser/session/connection, DCC, settings, network defaults/import, link previews, spellcheck, UI dialogs, terminal, scripting, and chat buffer behavior.
- Offscreen run has only one real failure.

Needs improvement:

- Add a stable headless test command or CMake test environment.
- Fix or update the theme-off test.
- Add tests for OpenGraph oversized body abort.
- Add uploader end-to-end tests against injected/loopback endpoints.
- Add `mcb1_convert.py` non-byte-aligned size test.

## Release Readiness Checklist

Before a public C++ build:

- Fix the `Themes Off` behavior/test mismatch.
- Decide local credential storage policy and update UI/docs.
- Add early body cap to OpenGraph fetcher.
- Add upload timeouts and response caps.
- Increase DCC passive tokens to 128 bits or more.
- Remove tracked CTest temp files.
- Update product version/name/description from dev/experiment wording.
- Decide whether `update_check` should default on.
- Document headless test command.
- Keep MC DATA/BBS marked demo/plaintext until secure mode exists.

## Overall Assessment

The C++ port is no longer just a skeleton. The IRC core, connection retry behavior, command parser, chat buffers, spellcheck, themes, scripting sandbox, DCC, and link preview systems all have real implementation and meaningful tests.

The most important remaining work is release hardening, not basic functionality: make the test suite green, clean generated files from Git, harden network fetch/upload caps, protect or clearly label saved secrets, and remove dev wording before packaging.

