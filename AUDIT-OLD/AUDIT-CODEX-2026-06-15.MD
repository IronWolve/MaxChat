# MaxChat C++ Full Audit - 2026-06-15

## Scope

Audited repository: `/home/iw/work/claude/maxchat-c`

Branch at audit time: `mc-data-terminal`

Tracked scope observed:

- 413 tracked files.
- 155 C++ source/header files under `src/`.
- 53 unit/integration-style test targets in the current Linux build after rebuild.
- Major areas reviewed: IRC connection/session handling, command parsing, settings import/export, link previews, image fetch/upload, DCC, Lua scripting, MC DATA/BBS terminal layer, themes, packaging scripts, repo hygiene, and tests.

I did not install extra packages. Existing local tools were enough: `cmake`, `ctest`, `cppcheck`, `rg`, `git`, and the configured Qt toolchain.

## Executive Summary

The C++ port is in a strong basic-client state. The important security posture is better than a quick grep would suggest: script permissions default off, link previews have SSRF protections, IRC password auth is blocked on plaintext unless explicitly allowed, outbound IRC lines strip CR/LF, DCC filenames are sanitized, and the test suite is broad.

There is one current test blocker: both rebuilt normal and release Linux builds fail `main_window_link_preview_test` because "Themes Off" still leaves an application stylesheet active for the chrome font rule. That may be intended product behavior, but the test currently says it is not.

The highest-priority non-test issues are stored secrets in plaintext settings, Lua permissions that intentionally become high-risk when enabled, a short 32-bit DCC passive token, MC DATA/BBS being plaintext and trust-light by design, and tracked CTest temporary files.

## Verification Results

### Build/Test

Rebuilt normal build:

- Command: `cmake --build build -j2`
- Result: success.
- Link warning: vendored Lua references `tmpnam` from `loslib.c`.

Normal tests after rebuild:

- Command: `env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -j2`
- Result: 52/53 passed.
- Failure: `main_window_link_preview`.
- Failing assertion: `tests/unit/main_window_link_preview_test.cpp:466`
- Reason: test expects `qApp->styleSheet().isEmpty()` after Themes Off, but `MainWindow::applyTheme()` still sets a chrome-font stylesheet for the system theme at `src/ui/MainWindow.cpp:9265-9270`.

Rebuilt release build:

- Command: `cmake --build build-release -j2`
- Result: success.
- Same Lua `tmpnam` link warning.

Release tests after rebuild:

- Command: `env QT_QPA_PLATFORM=offscreen ctest --test-dir build-release --output-on-failure -j2`
- Result: 52/53 passed.
- Same failure: `main_window_link_preview`, same assertion at `tests/unit/main_window_link_preview_test.cpp:466`.

Focused DCC test:

- Command: `env QT_QPA_PLATFORM=offscreen ./build/dcc_manager_test`
- Result: 9/9 passed.

### Static Analysis

Focused command:

```bash
cppcheck --enable=warning,style,performance,portability --std=c++20 \
  --suppress=missingIncludeSystem --inline-suppr -I src --quiet src tests
```

Useful result: no high-confidence memory safety issue in project source from this focused run.

Noise observed:

- Many `shadowFunction` false positives around Qt signals and `emit`.
- Style-only suggestions like "use STL algorithm" in comic rendering.
- A few "can be static" suggestions.

The earlier compile-database cppcheck run included generated build files and vendored Hunspell, which produced a lot of third-party/generated noise. The project should have a committed static-analysis command or script that excludes `build*`, generated MOC files, and vendored code.

## Findings

### P1 - Test Blocker: "Themes Off" Does Not Match Current Implementation

Files:

- `tests/unit/main_window_link_preview_test.cpp:464-466`
- `src/ui/MainWindow.cpp:9240-9272`

Current behavior:

- Themes Off triggers the system/default theme.
- `applyTheme()` resets the palette, but still calls `qApp->setStyleSheet(chromeFontCss)` so menu/toolbar fonts remain JetBrains/default app font.

Current test expectation:

- `qApp->styleSheet().isEmpty()` must be true after Themes Off.

Risk:

- Release tests are red.
- Product semantics are unclear: "themes off" could mean "no theme colors/wallpaper" or "absolutely no QSS at all."

Recommendation:

- Decide the product rule.
- If Themes Off means no visual theme but keep chosen app font, update the test to assert no theme-specific selectors/colors/wallpaper, not empty stylesheet.
- If Themes Off means no QSS at all, move chrome font application out of app-wide stylesheet for system theme.

### P1 - Secrets Are Stored in Plaintext JSON Settings

Files:

- `src/core/SettingsStore.cpp:299-307`
- `src/ui/ServerListDialog.cpp:92-108`
- `src/ui/PreferencesDialog.cpp:2237-2241`
- `src/ui/PreferencesDialog.cpp:2279-2283`

Stored sensitive fields include:

- IRC/NickServ/server/proxy passwords.
- Upload service tokens and passwords: `postimages_token`, `imgbox_password`, likely API keys/client IDs.

Existing mitigations:

- Password fields use `QLineEdit::Password` in the UI.
- IRC auth refuses plaintext IRC unless TLS is active or the user opts in: `src/irc/IrcSession.cpp:1014-1024`.
- Server list clearly warns for plaintext auth: `src/ui/ServerListDialog.cpp:102-108`.

Risk:

- Anyone with access to the user's config file can read credentials.
- Export/import may carry secrets unless explicitly documented or filtered.

Recommendation:

- Short term: document this clearly in README/help and export/import UI.
- Medium term: add "export without passwords/tokens" and "clear saved secrets".
- Long term: use platform keychain APIs for secrets, keeping only references in `settings.json`.

### P1 - Lua Scripting Is Sensibly Sandboxed by Default, but High-Risk Permissions Need Guardrails

Files:

- `src/scripting/ScriptPermissions.h:11-19`
- `src/scripting/LuaEngine.cpp:547-615`
- `src/ui/PreferencesDialog.cpp:918-932`

Good:

- Script permissions default off.
- `os.execute`, `io.popen`, `os.remove`, `os.rename`, `os.tmpname`, `require`, `load`, `loadfile`, `dofile`, and `api.http_get` are gated or removed by default.
- Native Lua module loading is blocked even when module loading is enabled by removing `package.loadlib` and C searchers.
- `io.open` is replaced with a guarded implementation.

Risk:

- Granting "Run programs" intentionally enables process/file OS operations.
- Granting file access relies on user-selected shared allowed directories.
- `api.http_get` is synchronous and can block script execution for up to the timeout.

Recommendation:

- Keep all script permissions off by default.
- Add a per-script "last loaded/last run/last error" audit pane.
- Add a one-time high-risk confirmation when enabling Run Programs.
- Consider separate toggles for `os.remove`/`os.rename` versus launching programs. Those are different risk classes.
- Add tests around allowed-dir symlink behavior, because `QFileInfo(path).absoluteFilePath()` plus string-prefix checks may not resolve symlinks the way users expect.

### P2 - DCC Passive Token Is CSPRNG but Only 32 Bits

File:

- `src/ui/DccManager.cpp:114-118`

Current:

- Token comes from `QRandomGenerator::system()->generate()`.
- Formatting creates an 8-hex-character token, which is 32 bits.

Good:

- It uses the system CSPRNG.
- Passive replies are tied to the expected peer nick before accepting.

Risk:

- 32 bits is small for a network-exposed authorization token, especially if tokens sit around for transfer negotiation.

Recommendation:

- Increase to at least 128 bits, e.g. four `generate()` calls or `fillRange`, formatted as 32 hex characters.
- Add a unit test checking token format/length and uniqueness shape if `newToken()` is made testable.

### P2 - MC DATA/BBS Is Plaintext and Should Stay Marked Experimental

Files:

- `src/irc/McDataCodec.cpp:39-62`
- `assets/scripts/bbs.lua`
- `DEVDOCS/MC_DATA.md`
- `DEVDOCS/MC_DATA_PHASES.md`

Good:

- `MC DATA` service/verb tokens validate allowed characters.
- Payload is preserved byte-exact after the verb, which is necessary for terminal frame data.
- Current docs already say no secrets/passwords over MC DATA v1.

Risk:

- Traffic is visible to the IRC server and potentially to channel/network observers depending on target/notice path.
- There is not yet authentication, encryption, replay protection, or peer identity pinning.
- The demo BBS account (`sir_iw` / `bbsiscool`) is fine as a demo, but it must never be presented as real security.

Recommendation:

- Keep MC DATA as experimental in docs/UI until MC SEC exists.
- Add sequence/session IDs and replay handling before any comic-avatar exchange or richer app protocol.
- Add a protocol-level rate limiter and per-peer cap to avoid CTCP flood disconnects.
- Keep the static-frame cache design, but include cache invalidation/version rules in the protocol docs.

### P2 - Link Preview Security Is Strong, but Keep It Covered

Files:

- `src/services/LinkPreviewClassifier.cpp`
- `src/services/OpenGraphFetcher.cpp:44-88`
- `src/services/ImageFetcher.cpp:53-92`
- `tests/unit/link_preview_classifier_test.cpp`
- `tests/unit/open_graph_fetcher_test.cpp`

Good:

- Rejects private/local/metadata IPs, unsupported schemes, userinfo URLs, dotless hosts, and private-looking names.
- Re-checks redirect targets for OpenGraph and image fetches.
- Caps image size and aborts oversized downloads during progress.
- Tests cover SSRF-sensitive forms like `127.0.0.1`, `169.254.169.254`, CGNAT, IPv6 local, and decimal loopback.

Risk:

- DNS rebinding is hard to fully eliminate with Qt's high-level network stack because DNS can be resolved separately from the actual connection.
- OpenGraph fetch reads only a capped amount at finish, but does not abort early on `downloadProgress` like `ImageFetcher` does.

Recommendation:

- Add early download cap abort to `OpenGraphFetcher`, mirroring `ImageFetcher`.
- Add tests for redirect-to-private on OpenGraph, not just classifier-level URL rejection.
- If the app ever supports previews on corporate/private networks, keep it opt-in and per-network.

### P2 - Vendored Lua Emits `tmpnam` Link Warning

File:

- `third_party/lua/loslib.c` via linked Lua OS library.

Observed warning:

```text
warning: the use of `tmpnam' is dangerous, better use `mkstemp'
```

Good:

- `LuaEngine` removes `os.tmpname` unless Run Programs is granted: `src/scripting/LuaEngine.cpp:552-556`.

Risk:

- The symbol is still present in the binary and becomes reachable if Run Programs is granted.

Recommendation:

- Patch vendored Lua to disable `os.tmpname`, or replace it with a safer Qt-backed temp-file API if scripts truly need temporary files.
- Treat this as hardening, not a release blocker.

### P2 - Tracked CTest Temporary Files Should Be Removed From Git

Files currently tracked:

- `Testing/Temporary/CTestCostData.txt`
- `Testing/Temporary/LastTest.log`

Good:

- `.gitignore` already includes build/test-output patterns.

Risk:

- Test runs can dirty the tree or churn logs.

Recommendation:

- `git rm --cached Testing/Temporary/CTestCostData.txt Testing/Temporary/LastTest.log`
- Keep `Testing/` ignored.

### P3 - Static Analysis Setup Needs a Clean Project Command

Current:

- Running cppcheck from the compile database pulls in generated and third-party code.
- Focused source-only cppcheck is much cleaner but misses some include/config context.

Recommendation:

- Add `tools/audit_static.sh` or a CMake target that excludes:
  - `build*`
  - `third_party`
  - MOC/generated files
  - assets/dictionaries
- Store suppressions for Qt signal false positives.
- Later add `clang-tidy` checks incrementally on touched modules only.

### P3 - Settings Import/Merge Policy Looks Good, but Needs User-Facing Export Choices

File:

- `src/core/SettingsStore.cpp:199-270`

Good:

- Imported networks merge into newer defaults instead of blindly replacing the whole default server list.
- Default failover servers and websites get added when missing.

Risk:

- Importing user settings can still bring stale saved server data and secrets.

Recommendation:

- Add import choices:
  - "Import preferences only"
  - "Import identities/passwords"
  - "Import custom networks"
  - "Import all"
- Add a preview summary before import.

### P3 - The Update Check Default Is Still Gated Off

File:

- `src/core/SettingsStore.cpp:436-438`

Current:

- `update_check` defaults false with a TODO to flip for live release.

Recommendation:

- Decide before public release:
  - Keep startup update checks off by default for privacy/no surprise network.
  - Or ask on first run.
  - Manual Help > Check for Updates can stay.

### P3 - Open TODO/Deferred Items Are Reasonable but Should Be Pruned Before Public Docs

Observed from existing docs/backlog:

- Native OS spell backend still has target-OS verification constraints.
- DCC/media/sounds and BBS/comic-network ideas are staged/TBD.
- Internal docs are useful, but release-facing docs should not expose old stale planning notes as current behavior.

Recommendation:

- Keep `DEVDOCS/` for internal plans.
- README should describe only working features plus clearly labeled "planned".
- Before public release, scan docs for stale terms and internal-only notes.

## Strengths Worth Keeping

- IRC send path strips CR/LF before wire output, protecting against line injection.
- Password auth is TLS-first and requires explicit plaintext opt-in.
- Redaction covers PASS, SASL AUTHENTICATE payloads, and common services password commands.
- Link previews have serious SSRF thought behind them.
- DCC has path traversal prevention, Windows reserved-name handling, size caps, stale offer cleanup, peer checks, and pending-offer caps.
- Lua scripts start sandboxed and permissions are visible in Preferences.
- Tests cover a broad spread: IRC parsing/session, command parser, DCC, link preview, chat formatting, settings, server list, spellcheck, themes, terminal, and Lua.
- `sync-to-win.sh` is careful about CRLF for `build.bat` and refreshes runtime theme/wallpaper assets.

## Recommended Next Actions

1. Fix the current red test by deciding Themes Off semantics.
2. Remove tracked `Testing/Temporary/*` logs from git.
3. Increase DCC passive token size to 128 bits.
4. Add early body-size abort to `OpenGraphFetcher`.
5. Add export/import options that avoid passwords and API tokens by default.
6. Add a clearly named static-analysis script that filters generated/vendor files.
7. Add release docs that say saved secrets are stored locally in settings until keychain support exists.
8. Keep MC DATA/BBS marked experimental and no-secrets until the security layer is designed.

## Release Readiness Call

For a dev/test build: good enough, with the known red test documented.

For a public release: fix or formally rebaseline the theme-off test first, remove tracked test logs, and document plaintext local credential storage. The rest can be staged as hardening/backlog unless you are positioning DCC, Lua scripts, or MC DATA as security-sensitive launch features.

