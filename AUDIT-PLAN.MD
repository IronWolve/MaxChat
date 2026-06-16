# maxchat-c — Consolidated Audit Fix Plan (peer audits, 2026-06-15)

> **STATUS: P0–P4 IMPLEMENTED (2026-06-15), 16 commits a2d724d..4e70c80, 53/53
> green.** Remaining = the Backlog section (God-object decomposition, secrets
> encryption, settings.json split, MC SEC, uploader base class) + release-
> packaging items. See HANDOFF "Latest Completed Slice" and DEV_NOTES.

## Context

Ten peer model-audits of the `mc-data-terminal` branch were collected
(`AUDIT-{CODEX,DEEPSEEK,MIMO,XAI,CLAUDE-LINE,NOUS}*`). They were de-duplicated
across models and the high-severity items were **verified against current HEAD
`a0664e3`**. Goal: one prioritized, verified plan to harden the port toward
release. Scope = everything (bugs, security, perf, design, style, hygiene, docs).

**Key correction:** every model reported `main_window_link_preview` FAILING
("Themes Off leaves a chrome-font stylesheet"). That is **already fixed at
HEAD** — `applyTheme()` only appends `chromeFontCss` on the non-system branch;
Themes Off sets an empty stylesheet, and the rebuilt test passes (53/53). The
auditors ran an earlier commit. So a verification pass was required and is
reflected below; do not re-fix already-resolved items.

### Already fixed / false at HEAD (do NOT action)
- Themes-off test failure — fixed (`MainWindow.cpp:9265-9275`).
- "Disconnect All / Reconnect All only current network" — those actions no
  longer exist (per-network Close/Disconnect/Reconnect only).
- WHO numeric 352 indexing — correct at HEAD (`IrcSession.cpp:880`).
- `AppInfo.h` missing include guard — has `#pragma once`.
- `ChatLineFormatter::colorSpan` "XSS" — colors come from fixed palettes, not
  user input; not exploitable.
- `ThemeCatalog` path traversal / `url()` CSS injection — `u-*` stems contain
  no separators; wallpaper path already sanitized.
- README/STATUS/HANDOFF/PORT_PLAN docs — current (PORT_PLAN marked HISTORIC).

### Decisions (from user)
- **Secrets at rest:** harden around plaintext now (parity with Python which
  shares `settings.json`); write a design note for encryption/keychain and
  **defer the build** (Backlog).
- **Lua IRC send:** **add a new permission** gating `send_raw/say/mc_send/
  mc_reply` (P0-5 below).
- **MainWindow God-object refactor:** **backlog** (do the concrete fixes now).

---

## P0 — Security (verified real; gate release)

| # | Item | Location | Fix |
|---|------|----------|-----|
| 0-1 | DCC active-send accepts first inbound TCP peer with no IP check | `DccManager.cpp:324-334` | Verify connecting peer IP against the offer's expected peer (mirror the passive-mode check already added). |
| 0-2 | DCC `offerChat` re-offer leaks/orphans existing `ChatRuntime` | `DccManager.cpp:852-909` | Tear down any existing chat for that nick before `chats_.insert`. |
| 0-3 | DCC passive token only 32-bit (8 hex) | `DccManager.cpp:114-119` | Widen `newToken()` to ≥128-bit (match Python `token_hex(16)`). |
| 0-4 | `QFile::write()` return ignored on receive — disk-full → truncated file + wrong ACK | `DccManager.cpp:665` | Check write return; abort transfer + surface error on short write. |
| 0-5 | **New Lua IRC-send permission** | `ScriptPermissions.{h,cpp}`, `LuaEngine.cpp:227,653,845-858` | Add `allowIrcSend` (default ON for bundled `.bundled/` scripts, OFF for user scripts); gate `l_send_raw/l_say/l_mc_send/l_mc_reply`; surface in Preferences ▸ Scripts. |
| 0-6 | `handleChatAnchorClicked` default opens URL with no scheme allow-list | `MainWindow.cpp:7497` | Restrict `QDesktopServices::openUrl` to `{http,https,ftp,mailto}` (reuse the guard already used by `UrlListDialog`). |
| 0-7 | Uploaders: no timeout, unbounded `readAll()`, returned URL unvalidated, ImgBB key in query string | `src/upload/*.cpp` | Add per-request timeout + response size cap; validate returned URL scheme is https; move ImgBB key to a form field/header if the API allows. |
| 0-8 | OpenGraph fetcher caps body only at finish (no in-flight abort) | `OpenGraphFetcher.cpp:60-128` | Abort on `downloadProgress` over cap, mirroring `ImageFetcher`. |
| 0-9 | SSRF: redirect re-validation async (TOCTOU) + IPv6 previews fully blocked | `LinkPreviewClassifier.cpp:233-235,300-332`, `ImageFetcher.cpp:78-92`, `OpenGraphFetcher.cpp:78-88` | Re-check each redirect hop before following; decide IPv6 policy (parse/validate `[..]` literals instead of rejecting all `:` hosts). Note residual DNS-rebind window in a comment. |
| 0-10 | `ComicArt` integer truncation: `quint32 cmprLen`→`int` bypasses bounds check; `u32(data,rec)+bias` can wrap | `ComicArt.cpp:74,85,339` | Keep comparisons in 64-bit/unsigned; reject sizes > sane cap before casting. |
| 0-11 | IRC target/param injection: targets/channels with spaces emit extra wire params (CRLF already stripped) | `IrcSession.cpp:266,309,327`, `CommandParser.cpp:232-258,429` | Reject/validate target tokens (no spaces) before building `PRIVMSG/JOIN/MODE`. |
| 0-12 | `TerminalFrame::parse` / terminal output unbounded (CPU/mem DoS via `api.terminal_frame`) | `TerminalFrame.cpp:81-166`, `ScriptTerminalDialog.cpp:191-201` | Cap op-count/total length per frame; cap terminal scrollback bytes. |
| 0-13 | `ScriptPermissions.allowedDirs` not serialized — per-script dir perms silently lost on restart | `ScriptPermissions.h:30-40` | Add `allowedDirs` to `toMap()`/`fromMap()`; add a round-trip test. |
| 0-14 | Secrets-at-rest hardening (parity, no format change) | `SettingsStore.cpp:300-307`, `IrcRedaction.cpp:7-31`, prefs/server dialogs | Confirm every password field uses `QLineEdit::Password`; extend `redactLine` to `OPER` + missing NickServ variants; document the at-rest model. (Encryption = Backlog.) |
| 0-15 | bbs.lua password compare case-sensitive while nick is case-insensitive; demo creds plaintext over MC DATA | `assets/scripts/bbs.lua:26-27,1026,1036` | Make compare consistent; comment that this is demo-only (no real auth until MC SEC). |

---

## P1 — Correctness bugs (verified / high-confidence)

| # | Item | Location | Fix |
|---|------|----------|-----|
| 1-1 | `registrationTimeoutMs` set from `connectTimeoutMs` (copy-paste) | `MainWindow.cpp:9058` | Use the registration-timeout setting. |
| 1-2 | `parseServerSpec` breaks bare IPv6 (`lastIndexOf(':')`) and forces `tls=false` when a port is present | `DefaultNetworks.cpp:1526-1534` | Bracket-aware IPv6 parse; keep `defaultTls` unless `+` says otherwise; validate port 1-65535. |
| 1-3 | AnsiRenderer dead ternary `code>=100 ? code-10 : code-10`; bright-bg SGR mis-mapped | `AnsiRenderer.cpp:107,83-113` | Fix bg mapping; optionally handle `38;5;n` / `38;2;r;g;b`. |
| 1-4 | Speller fail-open vs fail-closed mismatch when no dict | `HunspellSpellchecker.cpp:72-81`, `WindowsSpeller.cpp:79-87` | Pick one (recommend fail-open) and apply to both. |
| 1-5 | `readUtf8Text` re-encodes per-QChar → desync on surrogates/non-BMP | `TerminalFrame.cpp:55-77` | Encode the whole run once; handle surrogate pairs. |
| 1-6 | ChannelModes: `rejected`→`accept()`; clearing key doesn't send `-k` | `ChannelModesDialog.cpp:171,191` | Wire `rejected`→`reject()`; send `-k` when key cleared. |
| 1-7 | Seek-slider `qint64`→`int` truncation (>24-day media wraps) | `MediaPlayerDialog.cpp:48-52`, `AudioPlayerBar.cpp:58-64` | Scale to a fixed range or keep 64-bit. |
| 1-8 | Unknown `/command` silently sent raw to server (typos transmitted) | `CommandParser.cpp:717-721` | Warn/confirm on unknown commands (keep `/raw`/`/quote` explicit). |
| 1-9 | CTCP auto-reply throttle global, throttled requests invisible | `IrcSession.cpp:561-575` | Per-sender throttle; still surface the request. |
| 1-10 | `linkifyUrls` can nest `<a>` tags (substring re-match) | `ChatLineFormatter.cpp:113-122` | Match on token boundaries / skip inside anchors. |
| 1-11 | `renameMember` drops op/voice prefix on collision | `ChatBufferStore.cpp:291-298` | Preserve prefix across remove+add. |
| 1-12 | `saveRaw` with partial map wipes unrelated keys; cache staleness within same second | `SettingsStore.cpp:116-118,161-181` | Always merge onto `loadRaw()`; strengthen cache check (hash / ns mtime). |
| 1-13 | Uploader status not updated on all fail paths (progress hangs) | `src/upload/*.cpp` | Resolve UI state on every error/timeout branch. |
| 1-14 | `ReconnectPlanner` single-server retries forever; clamped index not written back | `ReconnectPlanner.cpp:11-37` | Cap retries; persist clamped index. |
| 1-15 | `scriptHttpGet` blocks GUI thread via nested `QEventLoop` (10 s) | `MainWindow.cpp:2820-2870` | Move to async/worker, or document the bound (has a timeout; lower priority). |
| 1-16 | `mcb1_convert.py raw_bytes` assumes bit count % 8 == 0 (custom sizes crash) | `tools/mcb1_convert.py:125-132,212` | Pad to byte boundary or reject non-multiple sizes. |

---

## P2 — Performance (hot paths)

- `applyCurrentSettings` parses JSON ~5×; `refreshComic` re-reads settings per
  nick; `loadWithDefaults()` ~61× — pass a cached settings map.
  (`MainWindow.cpp:9377-9683,8215-8499`)
- `ChatBufferStore`: sort-per-join and `removeFirst()` trim loops O(n²) — `QSet`
  dedup + range erase. (`:36-44,61-63,260-268`)
- `ChatLogStore::recentLines` reads whole file + `removeFirst` loop. (`:118-126`)
- Per-call `QRegularExpression` → `static const` (`CommandAlias`, `ChatLogStore`,
  `CommandParser:21,158`, `IrcSession:569`).
- `mircColors()` `QHash`→`std::array<QString,99>`. (`IrcFormat.cpp:9-63`)
- `m_previewImageCache` unbounded — add LRU eviction. (`MainWindow.h:434`)
- `IrcConnection` `buffer_.contains("\r\n")` O(n) per `readyRead`. (`:299`)
- `ScriptTerminalDialog::writeText` O(n²) append; `QFontMetrics` per binary-search
  iter. (`:191-201,387-413`)
- `supportedCommandNames()`/`languageSpecs()`/`emojiFamilies()` → `static const`.
- `defaultSettings()`/`defaultNetworks()` big value copies each call → static ref.

## P3 — Design / robustness

- Mutable render globals `g_comicFontFamily`/`g_actionIncludeName` (re-entrancy)
  → render context. (`ComicRenderer.cpp:27,32`)
- `ComicCharacter` static cache never evicted; `ThemeCatalog` mutable static
  singletons + `fileKind.contains(kind)` substring match (`ThemeCatalog.cpp:331`).
- Lifetime: `writer_` lambda raw `this` (`IrcConnection.cpp:149`); GeometryPersist
  lambda captures `&store` by reference (`GeometryPersist.cpp:19`).
- `DccManager.runtimes_` never pruned after completion (leak); `chats_` raw ptrs.
- Lua: per-script timers uncapped; `createTimer` O(n) owner scan; `loadPrefs`
  re-reads disk per `api.get`; `l_set` rewrites prefs.json non-atomically (use
  `QSaveFile`). (`LuaEngine.cpp:383,398-416,755-759,765-776`)
- `FloodGuard` records before comparing (trips one early) + empty-key map leak.

## P4 — Style / hygiene / docs / build

- **Repo hygiene:** untrack `Testing/Temporary/*`; `.gitignore` add `Testing/`,
  `dist-win/`, build artifacts. Per release-and-privacy rules, also gitignore
  internal docs (`AUDIT*.MD`, `DEV_NOTES.md`, `HANDOFF.md`, `DEVDOCS/`,
  `PORT_PLAN.md`) — confirm before untracking.
- **Test smell:** `#define private public` → `friend class`
  (`main_window_link_preview_test.cpp:33`); set `QT_QPA_PLATFORM=offscreen` in the
  CMake test env; `image_uploader_test` loopback server started but unused.
- Duplicate includes in `MainWindow.cpp` (`QMenu`, `QMimeData`, `QSet`).
- Version: `AppInfo.cpp` `0.1.0-dev` vs CMake `0.1.0` — inject via `configure_file`;
  set release version + flip `update_check` default at packaging.
- `tools/mcb1_convert.py` guard (see 1-16); `SCRIPTING.md` timer doc periodic vs
  one-shot, examples count 5 vs 9.
- `build.bat` `RUN_TESTS` never set; hardcoded Qt version list. CMake: `Qt6::Test`
  /`MultimediaWidgets` unconditional (headless CI); `ScriptPermissions.h` not in
  `maxchat_scripting`; redundant `AUTOMOC` on `maxchat_upload`.
- Long NIT tail (duplicate `comicfan` default nick, magic constants, `toLower`
  vs `toCaseFolded`, deprecated `globalPos()`, etc.) — sweep opportunistically
  as the files above are touched.

## Backlog (large / deferred by decision)

- **MainWindow God-object decomposition** (~9,900 LOC → IRC router / preview
  manager / DCC facade / comic controller / script bridge).
- **Secrets-at-rest encryption / OS keychain** — write a design note; defer build.
- ~~**settings.json split** from the Python app~~ — RESOLVED 2026-06-15: keep
  shared (no split); both apps load-merge-save, so no clobber. See DEV_NOTES DECISIONS.
- **MC DATA / BBS plaintext → future `MC SEC`** (reviewed crypto).
- ~~Uploader boilerplate → shared base class~~ — DONE (commit fd617f2).

---

## Execution & verification

Work top-down (P0 → P1 → P2 → P3 → P4), batching by file/subsystem so each commit
is coherent (`area: summary`, IronWolve identity, no AI trailer, no push). Reuse
existing helpers: `IrcSession::enqueueLine` (CRLF strip), `IrcRedaction::
redactLine`, the SSRF gate (`resolvePreviewUrlPublicAsync`/`isPublicIpv4Literal`),
`ScriptPermissions`, `GeometryPersist`, `UrlListDialog`'s scheme guard.

Per batch:
1. Add/adjust a unit test that fails before and passes after (esp. 0-13 perms
   round-trip, 1-1 timeout, 1-2 IPv6, 1-6 modes, 0-4 short-write).
2. **Rebuild the affected test target explicitly** (the `main_window_link_preview`
   false-pass proved stale test binaries hide regressions), then
   `QT_QPA_PLATFORM=offscreen ctest -j4` — expect all green.
3. After security batches, focused manual smoke on Windows (DCC peer reject,
   uploader timeout, Lua send-permission prompt).
4. Update HANDOFF/DEV_NOTES; record any "fix Python too" items in
   `../maxchat/DEVDOCS/BACKPORTS.md`.

**Done for this pass:** P0 + P1 implemented & verified; P2-P4 done or explicitly
logged alongside the Backlog in HANDOFF.
