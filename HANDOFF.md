# MaxChat C++ Port Handoff

Date: 2026-06-09

Status: simultaneous live multi-network plumbing is implemented and ready for
two-network smoke testing. The UI parity pass keeps the Python-style toolbar,
menus, button-tabs option, side panels, theme/chat-theme/wallpaper controls, and
planned feature stubs visible. A theme-parity pass against the Python app is in
progress (wallpaper rendering fixed; remaining gaps listed in `DEV_NOTES.md`).

## Latest Completed Slice (themes/palette/protection batch, git-tracked)

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
