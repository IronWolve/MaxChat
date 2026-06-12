# MaxChat C++ Port Handoff

Date: 2026-06-09 (audit closeout 2026-06-10; UI polish 2026-06-11)

## Latest Completed Slice (comic chat polish, 2026-06-11 late)

- **Visual emotion picker**: Comic ▸ Emotion now shows your character's real
  faces (`Character::faceCell`) in an icon grid — Auto + 9 emotions,
  double-click to apply, geometry persisted (`geom_emotion_picker`). Plain
  list fallback when no art is loaded.
- **Per-panel actions**: ComicView right-click now offers "Copy this panel" /
  "Save this panel as PNG..." for the panel under the cursor (new
  `panelRects()` shared by painting + hit-testing) and "Save all panels..."
  (numbered PNGs into a chosen folder).
- Already-existing gems confirmed while reading: `(parenthesised text)` makes
  THOUGHT bubbles; poses vary per message via deterministic hash.

## Previous Slice (gap cleanup + comic emotion upgrade, 2026-06-11 late)

- **Stale stubs gone**: Comic ▸ Character Names now confirms + live-refreshes
  (was printing "planned" despite working); Scripts/Browse Characters fallbacks
  reworded; dead `showFeaturePlanned` helper removed.
- **Lua-only scripting**: the 4 legacy Python example scripts deleted from the
  user's deployed scripts dir (repo assets were already Lua-only).
- **Windows native spellcheck**: discovered ALREADY DONE — WindowsSpeller
  (ISpellChecker COM) compiles into the default Windows build
  (MAXCHAT_OS_SPELL=ON); select via Preferences ▸ Spelling ▸ "Operating
  system". Stale "not compile-tested" comment refreshed. macOS skipped by
  user decision. AUDIT #30 can be closed for Windows.
- **Translations clarified**: Qt-standard dialogs/buttons translate (qt_*.qm
  deployed + loaded per `interface_language`); app-specific strings have no
  maxchat_*.qm catalogs — plumbing would pick them up if ever generated.
- **Comic emotion guesser** extracted to `src/comic/ComicEmotion.{h,cpp}`
  (`maxchat::comic::guessEmotion`) and upgraded: angry + bored are now
  reachable from text (they NEVER fired before — 2 of 9 emotions dead in auto
  mode), emoji recognized (😂😡😭😱😉🙂🥱 etc.), `!!!` → shouting, precedence
  rules (angry `>:(` beats sad `:(`; laughing beats happy). New
  `comic_emotion_test` (20 cases) registered in CTest — built, not yet run.
- Save Comic default filename now includes the date (`comic-chan-2026-06-11.png`).

## Previous Slice (themes live-apply + previews + new themes, 2026-06-11 late)

- **Live apply**: theme/chat-theme/wallpaper combo changes restyle the whole
  app instantly via `PreferencesDialog::themePreviewRequested` →
  `MainWindow::setTheme/setChatTheme/setWallpaper(..., save=false)`; Cancel
  reverts to saved settings, OK saves normally.
- **Chat preview pane**: mini fake conversation (timestamps, bracketed colored
  nicks, system join line) in the actual theme colors; follows the app theme
  for "Follow".
- **Grouped combos**: user themes first + separator + built-ins.
- **"Open themes folder..."** button (new `userThemeDirectoryPath()`).
- **New built-ins**: app `Sakura` (soft pink light), `Deep Ocean` (blue-navy),
  `Ember` (warm dark orange), `High Contrast` (black/white/yellow,
  accessibility); chat `Midnight - blue hours` (navy bg, blue nick palette).

## Previous Slice (themes tab polish + nick color modes, 2026-06-11 late)

- Themes tab: "Active now: <name>" labels + live color-chip previews in both
  the app and chat boxes; **Delete** button for user themes only
  (`isUserThemeId` / `deleteUserAppTheme` / `deleteUserChatTheme` in
  ThemeCatalog). Built-ins protected.
- Nick colors: "Color nicknames" checkbox → 3-way combo (`nick_color_mode`:
  off / palette / irc). "Classic IRC colors" uses the traditional client
  palette and overrides theme mono-nick styling; chat view and member list
  share the exact same mode+palette so a nick is one color everywhere.
  Legacy `colored_nicks` bool still written/read as fallback.
- preferences_dialog_test updated for the combo (not run — ask-before-tests).

## Previous Slice (themes revamp, 2026-06-11 late)

- **Theme packs**: Preferences ▸ Themes gained a "Theme files" group —
  `Save Theme...` (current app theme + current fonts → named user theme),
  `Export...` / `Import...` (single JSON bundling app theme, chat theme, fonts,
  wallpaper; `kind: maxchat-theme-pack`). Bare single-theme JSONs import too.
- **Themes carry fonts**: `AppThemeDefinition.fonts` (whitelisted
  `*_font_family/size/bold` keys only — a shared theme file must not inject
  other settings). Applied on selection from the Preferences combo
  (`applyFontSelections`) and from Settings ▸ Themes (`setTheme` merges fonts
  into settings + `applyCurrentSettings`).
- **New built-in app themes**: `Normal` (system-like light), `Normal Dark`
  (neutral dark) — for "Themes Off is too light/bare" — and `Console`
  (black/cyan classic terminal-IRC look; pair with chat themes `irssi`/`bitchx`
  which already existed).
- New ThemeCatalog API: `ThemePack`, `exportThemePack`, `importThemePack`,
  `themeFontKeys`; `saveUserAppTheme` now persists bundled fonts.
- NOT yet done: no unit tests for pack import/export round-trip (offered, not
  run per ask-before-tests).

## Previous Slice (per-channel Comic Mode, 2026-06-11 late)

- Comic Mode is now **opt-in per channel**: toggling it (button/menu/Ctrl+M)
  affects only the active buffer. Previously the first enable turned panels on
  for *every* buffer and you could only hide per channel afterwards.
- Implementation: `m_comicHiddenBuffers` (hide-set, default visible) replaced
  with `m_comicEnabledBuffers` (opt-in set, default off). `m_comicMode` is now
  derived — backend runs iff any buffer opted in. `refreshComic` skips buffers
  not in the set; `activateBufferTarget` shows/hides the panel per buffer.
- The Comic Mode toggle is **disabled (greyed) on the server buffer** — set at
  action creation and re-synced on every buffer switch; the old confusing
  "toggle on server = global kill" behavior is gone.
- Docs: UI_SURFACES §9 scope note.

## Previous Slice (bundled-script seeding upgrade, 2026-06-11 late)

- **Root-caused the "6-second `!run calc.exe`" report**: the launch backend was
  already fixed (`api.launch` → ShellExecuteW, commits 593a872 + 41b48e4), but
  the app loads scripts from `%LOCALAPPDATA%\maxchat\scripts\` and
  `seedBundledScripts` never overwrote an existing file — so the user's deployed
  run.lua was still the original blocking `os.execute('start "" …')` version
  (byte-identical to 797399f). `system()` blocks the GUI thread ~6 s on that
  machine.
- **Fix**: `seedBundledScripts` (MainWindow.cpp) now keeps `.bundled/` snapshots
  and upgrades any deployed script that still matches its snapshot (unmodified);
  user-edited scripts are never touched. Stale run.lua/memo.lua on the Windows
  install refreshed by hand (the new mechanism adopts them via the
  current==bundled branch).
- Docs: DEV_NOTES "THINGS I GOT WRONG" entry, SCRIPTING_DESIGN §7 + Step 10.
- **Verify on Windows**: restart MaxChat (or `/reload run`), `!run calc` should
  be instant.

## Previous Slice (UI polish batch, 2026-06-11)

### Upload / Image Hosting tab overhaul
- Tab renamed "Uploads" → "Image Hosting".
- `buildUploadsTab` fully rewritten: a `QComboBox` + `QStackedWidget` shows a
  dedicated panel per service (Disabled, imgbb, Imgur, Postimages, Imgbox).
- Each panel has a `QToolButton` that opens the service's signup page via
  `QDesktopServices`, a "TOS Accepted" `QCheckBox` that gates the credential
  fields (`QWidget creds` enabled/disabled by toggle), and hidden/show password
  control (Imgbox password row has a `QToolButton("🔍")` that toggles `EchoMode`).
- Four new `*_tos` boolean settings (`imgbb_tos`, `imgur_tos`, `postimages_tos`,
  `imgbox_tos`) with `false` defaults added to `SettingsStore`.

### Services tab — OG card display options
- Added a `QFrame::HLine` separator below the four link-preview-type checkboxes.
- Added "Card fields to display:" section with four `QCheckBox` widgets:
  Site name, Title, Description, Photo.
- Four new `og_show_*` boolean settings (`og_show_site_name`, `og_show_title`,
  `og_show_description`, `og_show_image`) with `true` defaults.
- `LinkPreviewRenderOptions` extended with matching fields; `applyCurrentSettings`
  populates `m_ogRenderOptions` from settings.
- `renderOpenGraphPreviewHtml` restructured: card `<div>` holds only text fields
  (site/title/description); image renders in a **separate** `<div>` below the
  card so the text card and photo don't visually overlap.

### OG image overflow fix
- `handlePreviewImageFetched`: large fetched images (> `maxImageWidth/Height`) are
  now scaled via `Qt::KeepAspectRatio + Qt::SmoothTransformation` **before** being
  stored in `m_previewImageCache`. `QTextDocument` registers the resource at the
  stored image's native pixel size (ignores CSS `max-width`), so images must be
  capped before insertion.

### Async OG card wrong-channel routing fix
- `LinkPreviewCandidate` gained `originNetwork`/`originTarget` fields.
- `queueLinkPreviewsFromLine` stamps these at queue time (the channel where the
  URL was posted). `handlePreviewCardFetched` uses the stamped fields (not
  `m_currentTarget` at callback time) to route the rendered card to the correct
  buffer. Fixes cards appearing in the wrong channel after the user switches tabs.

### OG card text alignment fix
- `appendPreviewHtmlLine`'s `QTextBlockFormat::setLeftMargin` only applied to the
  first block; each `<div>` in the card HTML created additional `QTextBlock`s with
  default (zero) left margin, causing all card text to render at the left edge.
  Fix: after `cursor.insertHtml(html)`, iterate over all inserted blocks
  (`insertStart`→`insertEnd`) and apply `mergeBlockFormat` to each. Card text now
  aligns with the rest of the chat message column. See DEV_NOTES "THINGS I GOT
  WRONG" for the full root-cause.

### Scripts tab + Scripts Manager improvements
- `PreferencesDialog` constructor gained optional `QStringList loadedScripts` and
  `QString scriptsDir` parameters (backward-compatible defaults).
- Scripts tab now opens with a "Currently loaded" group box showing which Lua
  scripts are active at the time the dialog opens (populated from
  `m_lua->loaded()` in `openPreferences`).
- "Open scripts folder..." button added at the bottom of the tab when `scriptsDir`
  is set.
- `openScriptsManager` (Scripts Manager dialog):
  - Full file path now stored in `Qt::UserRole + 1` on each list item.
  - **Edit** button: opens the selected `.lua` in the OS default text editor
    (`QDesktopServices::openUrl`).
  - **Settings** button: shows an info dialog with the script's name, path, load
    status, and any header comment block from the `.lua` file (first consecutive
    `--` lines read at click time).

## Port audit complete (2026-06-10) — see AUDIT.md

An 11-phase parity + security audit of the C++ port against the Python original is
**complete**. It fixed **4 real C++-only security vulnerabilities** (DCC size-0 unbounded
write, comic decoder OOM, two link-preview SSRF holes) plus assorted security hardening and
parity fixes; 7 of the original seed concerns turned out stale/wrong. Full record, the
26-item FIX BACKLOG, and the Python-side backports list are in **AUDIT.md** (and
`../maxchat/DEVDOCS/BACKPORTS.md`). Tests: 42/42 green. **Release note:** before any public
push, gitignore the internal docs (AUDIT.md, DEV_NOTES.md) per the audit's release-hygiene
item.


Status: simultaneous live multi-network plumbing is implemented and ready for
two-network smoke testing. The UI parity pass keeps the Python-style toolbar,
menus, button-tabs option, side panels, theme/chat-theme/wallpaper controls, and
planned feature stubs visible. A theme-parity pass against the Python app is in
progress (wallpaper rendering fixed; remaining gaps listed in `DEV_NOTES.md`).

## Latest Completed Slice (prefs parity + full DCC + full Comic Mode)

- Preferences now match the Python dialog page-for-page, control-for-control:
  Appearance/Messages/Notifications/Protection/Files(DCC)/Themes/Fonts/
  Localization/Comic/Services/Data; QFontComboBox per area incl. nick/status/
  topic; sort_status key fixed; all notify_*/dcc_* defaults added; nick label
  beside the input now exists and per-area fonts/colours apply.
- DCC is complete: passive/reverse mode, RESUME/ACCEPT, accept policy
  (ask/trusted/all), port range, advertised IP (incl. IrcConnection::
  localAddress), 32-bit acks, DCC CHAT (=peer buffers), member-menu Send File /
  DCC Chat, transfers dialog with progress/Open-folder.
- Comic Mode fully ported: src/comic/ (ComicArt .avb/.bgb decoder, ComicCharacter
  compose/mirror/trim, ComicRenderer balloon/tail/caption layout) + a MainWindow
  comic engine (assignment, emotion guess, filtering, panel spill) + ComicView
  panel grid + ComicSettingsDialog + character gallery + Save Comic. No art ships;
  users set comic_art_dir to their own Comic Chat install.
- Verified: clean-from-scratch debug build 0 errors, release builds, selftest OK.
  Tests still deferred per user. Translations (tr() of UI strings) intentionally
  left for later.

## Previous Slice (themes/palette/protection batch, git-tracked)

Now under git (local repo, no remote; author IronWolve, no AI trailer). Recent
commits, newest first: protection settings; notify highlight_words fix; last
planned stubs (strip-copy, sort-by-status); theme editor; app-wide QPalette;
OS-notify visible-tray fix. Done in this batch:
- App-wide QPalette + stylesheet (qApp), so dialogs are themed; "Themes Off"
  restores the platform palette.
- All 17 app + 8 chat Python themes verified present with full colour data.
- Theme editor: Customize buttons -> ThemeEditorDialog, saves user JSON, live
  registry reload (themeRegistry is now rebuildable).
- strip_color_copy, sort_users_by_status (tray-icon picker already existed).
- notify: highlight_words now honoured + no self-highlight.
- Protection: paste guard, auto-rejoin, ignore/invite-protect (new invited()
  signal), confirm-quit, scrollback cap, auto-away idle timer, hide/custom
  CTCP VERSION.
Comic Mode and DCC now have foundational passes too:
- Comic Mode (Ctrl+M): ComicView renders recent messages as panels with
  generated per-nick characters, name captions, speech bubbles. Emotion /
  Comic Settings / Browse / Save still stubs (need the bundled-art pipeline).
- DCC: DccManager does active-mode SEND + incoming-SEND receive with progress;
  DccTransfersDialog (Settings -> File Transfers); /dcc send|list|close; new
  dccRequest() signal. Passive mode + RESUME still TODO.
Tests deliberately not run this batch (user deferred); everything compiles
debug + release and the release selftest passes. With these, the major Python
feature set is ported; remaining work is depth (comic art, DCC passive/resume,
tr() localization strings) rather than missing features.

## Earlier Slice (member list / input / user themes / fonts)

- Member list: away dimming (away-notify), per-nick colour overrides
  (context menu + chat), role-grouped view when colours off, "N users"
  header.
- Input: Ctrl+B/I/U mIRC codes, Ctrl+K colour picker, hint-text pref.
- User themes + user chat themes load from the config dir (Python format).
- Fonts page: real list font + 7 per-area colour overrides (details and the
  one not-yet-applied key in DEV_NOTES.md).
- Verified: debug + release ctest 40/40, both selftests OK.

## Previous Slice (medium-tier parity batch)

One pass implemented: member-list nick colouring; /shrug-family commands;
pm_echo/show_mode/indent_wrap/marker_line/log_mask/replay_lines with prefs UI;
per-network autoconnect + perform; Saved Looks; shortcut editor + Alt+1..9 /
Alt+`; per-network SOCKS5/HTTP proxy; inline media (image viewer dialog,
audio bar under chat, video dialog — chat anchors now routed through the
link-preview classifier, externally opened otherwise); QTranslator loading.
Details + caveats (Qt Multimedia now linked, ffmpeg GPL note, tr() conversion
pending) in DEV_NOTES.md. Verified: debug + release ctest 40/40, both
selftests OK.

## Previous Slice (chat-theme extras + prefs/server-list parity)

- irssi/BitchX chat themes render correctly again: `ts`/`bracket`/`system`/
  `nicks` restored in `assets/themes/chat-themes.json`, modeled in
  `ChatThemeDefinition`, plumbed through `chatLineFormatOptions()` into
  `ChatLineFormatter` (timestamp color, separate bracket tint, mono/palette
  nick modes, whole-line system tint). System-ness is an explicit
  `systemLine` flag on `ChatBufferLine`/append calls; incoming/outgoing
  ACTION and NOTICE pass `systemStyling=false`.
- Preferences: 12 pages in Python order (new: Notifications stub, Fonts with
  the real font controls, Comic stub), Appearance rebuilt with Python's
  grouping, nav width fits labels, dialog 860x640. Test page-count updated.
- Network editor: added NickServ account/password, server PASS,
  allow-plaintext-auth, accept-unsigned-cert, real name, username (backend
  already consumed all of these keys); planned stubs for autoconnect/perform/
  proxy. Unblocks authenticated two-network live testing.
- NOT yet verified by ctest run (user asks to be asked first) and visual
  check pending — user drives the GUI now; do NOT launch GUI apps or use
  computer use unless explicitly asked.

## Previous Slice (themes)

- Fixed wallpapers never rendering: `effectiveWallpaperPath()` (renamed from
  `effectiveWallpaperUrl()`) now returns a plain forward-slash path instead of a
  `file://` URL, quoted into `border-image: url("...")`. This also unblocks the
  Synthwave `bg_gradient` fallback when the wallpaper is off. Covered by the new
  `tests/unit/theme_catalog_test.cpp` (test 40).
- `build.bat` `:copy_assets` now fails the build when copying `assets\themes` /
  `assets\wallpapers` into `dist-win` fails (was silently swallowed; the user's
  dist-win had no assets at all).
- `sync-to-win.sh` now also refreshes `dist-win/assets/{themes,wallpapers}` on
  an existing packaged build, so asset edits show up without rerunning
  `build.bat`. NOTE: the Windows exe still needs one `build.bat` run to pick up
  the wallpaper code fix.
- Theme gap scan vs the Python app recorded in `DEV_NOTES.md` (chat-theme
  extras `ts`/`bracket`/`system`/`nicks` lost in JSON conversion, user themes,
  QPalette, theme editor stubs, Fonts tab with per-area colors, default theme
  mismatch).

## Workspace

- Active C++ port: this tree.
- Reference Python app: sibling MaxChat Python tree.
- Keep the Python app read-only as reference material for this port.
- Do not copy builds into production or release folders until explicitly asked.

## Runnable Builds

- Debug app: `build/maxchat-c`
- Release app: `build-release/maxchat-c`

## Latest Completed Slice

Multi-network connection management is implemented in `src/ui/MainWindow.cpp`
and `src/ui/MainWindow.h`, with focused coverage in
`tests/unit/main_window_link_preview_test.cpp`.

New behavior now covered:

- Starting a connection for a second network creates a separate `IrcConnection`
  and no longer tears down the first network's socket/session state.
- Inactive-network events update the correct network buffer, tree counters,
  raw log, member/topic state, and notify state without repainting or stealing
  focus from the active chat.
- Selecting a target under another network switches active network context and
  restores that network's current target and stored history.
- Disconnect/reconnect, failover attempts, manual disconnect flags, registered
  state, uptime, pending NAMES, channel mode cache, ignore masks, ISON notify
  polling, and server-tree context actions are scoped to the intended network.

Recent chat display parity cleanup is still covered in `src/ui/MainWindow.cpp`
and `src/core/ChatLineFormatter.cpp`, with focused coverage in
`tests/unit/main_window_link_preview_test.cpp` and
`tests/unit/chat_line_formatter_test.cpp`.

Behavior now covered:

- Raw IRC wire lines update Raw Log but do not print into the chat view.
- Connection/server status routes to the Server buffer instead of active
  channel/query buffers.
- Aligned chat rows show a visible `|` separator between the nick column and
  message text.
- Join/part/quit/nick/kick/mode events use IRC-style `* nick ...` formatting.
- The server/channel tree can show multiple network roots.
- Each network keeps its own current target and open channel/query list.
- Selecting a target under another network switches the active network and
  restores that network's stored chat buffer.

Recent topic numeric slice still covered:

- Topic numerics display no-topic, topic text, and channel URL replies as
  readable status lines while still updating the topic bar.

Recent away-notify slice still covered:

- Incoming `away-notify` `AWAY` events display when another user goes away or
  comes back.

Recent command-failure slice still covered:

- Away/back confirmations, invite success, too-many-channel errors, bad nick
  errors, user-not-in-channel/already-in-channel errors, missing-parameter
  errors, unknown mode errors, and operator/mode permission failures display as
  readable status lines.

Recent WHOWAS slice still covered:

- `/whowas nick` displays the historical identity reply, missing-history errors,
  and end-of-WHOWAS completion as readable `[whowas]` status lines.

Recent miscellaneous event slice still covered:

- Server `ERROR`, `WALLOPS`, incoming `INVITE`, topic setter metadata (`333`),
  and channel creation time (`329`) display as readable status lines.

Recent MOTD/error slice still covered:

- MOTD start/body/end/no-MOTD numerics display as readable `[motd]` lines.
- Common errors display as `[error]` lines for no such nick/channel, cannot
  send, unknown command, bad password, full/invite-only/banned/keyed channels,
  registered-only channels, and not-channel-operator failures.

Recent WHOIS slice still covered:

- `/whois nick` displays common server replies as readable `[whois]` status
  lines, including user/host, server, channels, account, away, operator,
  idle/signon, secure connection, and end-of-WHOIS.

Recent PM/query slice still covered:

- Incoming private messages open visible background query targets with unread
  counters instead of stealing focus from the active channel.

Recent timeout slice still covered:

- Socket-connect timeout, IRC registration timeout, TLS readiness guarding, and
  failover-friendly disconnect signaling through the existing retry planner.

Recent CTCP slice still covered:

- Incoming CTCP ACTION, VERSION, PING, TIME, CLIENTINFO, CTCP NOTICE replies,
  and inert DCC-style request display.

Docs updated:

- `README.md`
- `STATUS.md`
- `PORT_PLAN.md`
- `HANDOFF.md`

## Verification Snapshot

- Debug build: passed.
- Release app build: passed.
- Debug `ctest`: 40/40 passed.
- Release `ctest`: 40/40 passed.
- Debug selftest: `MaxChat C++ 0.1.0-dev selftest OK`.
- Release selftest: `MaxChat C++ 0.1.0-dev selftest OK`.
- Earlier `cppcheck`: clean across 77 source/test files.
- Release dependency check: Hunspell is expected; `Qt6Multimedia` and `Qt6Svg`
  are not linked.
- Debug binary size: `22,022,472` bytes.
- Release binary size: `4,610,560` bytes.

## Next Resume Step

Live-test two real networks at the same time: connect one, join/chat, connect a
second, confirm both stay alive, switch targets in the server tree, disconnect
one network, and confirm the other remains connected.

Suggested starting points:

- `src/irc/IrcSession.cpp`
- `src/ui/MainWindow.cpp`
- `src/irc/CommandParser.cpp`
- `tests/unit/main_window_link_preview_test.cpp`
- `tests/unit/irc_session_test.cpp`
- `tests/unit/command_parser_test.cpp`

## Still Deferred

- Comic Mode.
- DCC.
- WAV/MP3/video playback.
- Notification sound playback.
- Full release packaging for this C++ port.

## Quick Commands

```bash
# Run from the C++ port root.
cmake --build build
cmake --build build-release
ctest --test-dir build --output-on-failure
ctest --test-dir build-release --output-on-failure
build-release/maxchat-c --selftest
```
