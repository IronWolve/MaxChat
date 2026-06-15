# MaxChat C++/Qt Port Plan

> **HISTORIC DOCUMENT** (updated 2026-06-15). All milestones in this plan have
> been completed. The original plan was written when only 6 milestones existed;
> milestones 7–10 (DCC, Comic, Scripting, Packaging) were added as the port
> progressed. Today the C++ port exceeds the Python original in features, test
> coverage, and security. See `HANDOFF.md` for the current ground truth and
> `README.md` for build instructions.

Workspace: this C++/Qt port tree.

Reference app: the Python/PySide MaxChat tree is read-only reference material. The port should not
edit or build inside that project.

## Current Toolchain

Installed and usable:

- `g++` 15.2
- `cmake` 4.2.3
- `ninja` 1.13.2
- Qt 6.10.2: Core, Widgets, Network, Multimedia, Svg
- `ccache`
- `patchelf`
- Hunspell development headers and English dictionary

Qt helper tools exist, but Ubuntu keeps them outside the normal shell path:

- `/usr/lib/qt6/libexec/moc`
- `/usr/lib/qt6/libexec/uic`
- `/usr/lib/qt6/libexec/rcc`
- `/usr/lib/qt6/bin/lrelease`
- `/usr/lib/qt6/bin/lupdate`
- `/usr/lib/qt6/bin/designer`
- `/usr/lib/qt6/bin/linguist`

CMake's Qt integration should find `moc`, `uic`, and `rcc` automatically.

Recommended but not blocking:

```bash
sudo apt install -y clang-format clang-tidy gdb valgrind cppcheck hunspell qt6-wayland-dev
```

## Porting Principle

This is a behavior port, not a line-by-line translation.

The Python app already solved a lot of product and platform details. The C++ app should preserve the
user-facing behavior, file formats, defaults, menus, and layout decisions, while replacing the runtime
with native Qt/C++ modules.

Do not start by porting `main_window.py` as one giant C++ file. Split the app into small native
modules early, because the Python `main_window.py` grew into a central coordinator.

## Target Architecture

```text
src/
  app/              QApplication bootstrap, app metadata, paths
  core/             settings, models, shared helpers
  irc/              message parser, client, reconnect/failover, DCC
  ui/               main window, dialogs, widgets, theme helpers
  comic/            .avb/.bgb decoding, character model, renderer
  services/         OpenGraph/X/Mastodon cards, media helpers
  spell/            Hunspell adapter
  scripting/        deferred; decide whether to keep, replace, or drop Python scripting
assets/
  fonts/
  wallpapers/
  sounds/
  translations/
tests/
  unit/
  integration/
```

Core Qt modules:

- `Qt::Core`
- `Qt::Widgets`
- `Qt::Network`
- `Qt::Multimedia`
- `Qt::Svg`
- `Hunspell`
- OpenSSL through Qt TLS support

Target-link rule:

- Link each Qt module only into the target that actively uses it.
- The current app target links Core/Gui/Widgets/Network because live IRC and link
  cards are app-facing.
- Hunspell is linked into the app only when the native message-input spellcheck
  feature is available.
- Add Multimedia and Svg only when their feature slices land.

## Milestone 0: Repo Skeleton

Goal: build and run a native empty MaxChat window.

- Create `CMakeLists.txt`.
- Create `src/main.cpp`.
- Add app metadata/version.
- Add resource file for fonts/icons/wallpapers/sounds.
- Add `README.md` with build commands.
- Add `.clang-format`.
- Add first `ctest` target.

Acceptance:

- `cmake -S . -B build -G Ninja`
- `cmake --build build`
- `ctest --test-dir build`
- App opens a titled `MaxChat C++` window.

## Milestone 1: IRC Core

Goal: UI-free IRC engine with tests.

Port first:

- IRC line parser from `maxchat/irc/message.py`.
- Formatting/control-code parser from `maxchat/ui/irc_format.py`.
- Redaction rules from `maxchat/irc/client.py`.
- `IRCClient` using `QSslSocket`.
- CAP negotiation, SASL PLAIN, NickServ fallback.
- PING/PONG, JOIN/PART/QUIT/NICK, PRIVMSG/NOTICE, TOPIC, NAMES, WHOIS.
- Connection timeout and failover/retry behavior.

Acceptance:

- Parser tests pass.
- Client can connect to a test IRC server or real network.
- Failed first server advances to the next server.

Current status:

- Parser, formatting, redaction, reconnect planner, session core, default network
  model, TOPIC/NAMES handling, and local socket-backed connection tests are
  implemented.
- The socket client currently connects to a local TCP test server and handles
  registration, welcome/nick sync, raw sends, redaction, and failed-connect
  disconnect signaling.
- Server List Connect now wires the main window to `IrcConnection` for a
  first-pass real IRC connection path.

## Milestone 2: Basic IRC UI

Goal: usable plain IRC client before extras.

Port:

- Main window layout: menu bar, topic bar, left server/channel tree, chat view, member list, input.
- Multi-network model.
- Server tab, channel views, query views.
- Send/receive chat messages.
- Input history and tab completion.
- Basic command dispatcher: `/join`, `/part`, `/msg`, `/me`, `/nick`, `/whois`,
  `/topic`, `/mode`, `/raw`, `/quit`.

Acceptance:

- User can connect, join, chat, receive PMs, switch channels/networks.
- No comic mode required yet.

Current status:

- Initial live IRC UI is implemented: Server List Connect starts live IRC,
  incoming PRIVMSG/NOTICE/action lines display readably, and the input box
  uses a tested command parser for `/join`, `/part`, `/msg`, `/me`, `/nick`,
  `/whois`, `/who`, `/whowas`, `/topic`, `/mode`, `/raw`, `/ctcp`, `/clear`,
  `/clearall`, `/lag`, `/uptime`, `/netinfo`, `/close`, `/reconnect`,
  `/disconnect`, `/quit`, and plain text to the active channel/query.
- Connection startup now has both socket-connect and IRC registration timeouts,
  so a dead primary server or a server that accepts TCP but never welcomes the
  client advances through the existing three-attempt-per-server failover planner.
- Incoming CTCP now stays out of normal chat text. ACTION still renders as an
  action, common VERSION/PING/TIME/CLIENTINFO requests get NOTICE replies, CTCP
  NOTICE replies display readably, and DCC-style requests are inert status lines
  until the DCC milestone.
- Topic updates, member-list population from chunked NAMES replies,
  JOIN/PART/QUIT/KICK/nick-change member updates, input history, and basic Tab
  completion are implemented.
- View > Server List and View > Members now toggle the side panels, persist to
  settings, restore correctly on a new window, and collapse hidden side columns.
  The inactive Buttons as Tabs action is withheld until the target-switching UI
  exists.
- View > Clear Current Chat is wired to the same current-buffer clear path as
  `/clear`.
- The unported Comic menu actions are disabled instead of live-looking no-ops,
  and Help > About opens a native about dialog.
- The first-pass server/channel tree now tracks open targets, remembers manual
  joins and private-message targets, and refreshes TOPIC/NAMES when a channel is
  selected.
- Tree selection and context-menu actions now use canonical targets stored on
  `QTreeWidgetItem` data instead of visible labels, so unread/highlight tree
  labels can change without changing the selected channel/query target.
- The tree now renders inactive buffer counters from `ChatBufferStore`: `[n]`
  for unread lines, `[!h/n]` for highlighted unread lines, an aggregate suffix
  on the network root for visible targets only, and no target suffix once the
  target is selected.
- View > Mark All Read clears unread/highlight counters for the current network
  without switching the active target or deleting retained history.
- A tested Core chat-buffer model now tracks per-network server/channel/query
  buffers, active-buffer selection, unread/highlight counts, bounded history,
  topics, joined state, members, and mark-read operations.
- MainWindow now uses that buffer model for one-connection target history:
  switching channel/query targets restores separate chat histories, topic text,
  and channel member lists, and incoming messages/common channel events route to
  their conversation buffer.
- The visible multi-network buffer state is now wired into MainWindow: the
  server/channel tree can show multiple network roots, remembers each network's
  current target and open channel/query list, preserves separate network
  histories, and restores the correct buffer when a target under another network
  is selected.
- Incoming private messages now open visible background query targets with
  unread counters instead of stealing focus from the active channel.
- Stored channel members now match IRC status-prefixed NAMES entries such as
  `@nick` and `+nick` against later plain nick events, keeping inactive channel
  metadata current across PART, QUIT, KICK, and nick changes.
- Quick Connect is implemented for one-off live connections.
- Simultaneous live multi-network socket sessions are now wired through
  per-network `IrcConnection` instances. Inactive network events update that
  network's buffers, counters, member/topic state, raw log, notify state, and
  failover/reconnect state without repainting the active chat.
- Basic IRC is now usable enough for two-network smoke testing: connect one
  network, join/chat, connect another, receive PMs, switch targets/networks,
  and keep per-network buffer state. Remaining command/UI edge cases should be
  tightened from live smoke-test findings.

## Milestone 3: Server List And Settings

Goal: preserve first-run experience and network management.

Port:

- Config paths using `QStandardPaths`.
- JSON settings compatible with the Python app where practical.
- Default network list, including multiple servers and homepages.
- Backup import merge behavior that keeps the current bundled server catalog and
  restores user-owned fields from older backups.
- Server List dialog: add/edit/delete, reset server list, move servers up/down.
- Quick Connect dialog.
- Startup/autoconnect settings.

Acceptance:

- Fresh config has the expected default network list.
- Reset server list restores backup/failover servers.
- User-edited network secrets/settings survive restart.

Current status:

- Default network model, import merge helper, and `SettingsStore` are implemented
  with tests.
- First native Server List UI is implemented with tests. It supports
  add/edit/delete, reset defaults, startup toggle, homepage button updates, and
  move up/down for selected networks.
- MainWindow opens the dialog and persists accepted edits through
  `SettingsStore`. The Connect button starts a real IRC connection from the
  selected network.
- Settings import/export/reset server list actions are implemented in the
  Settings menu. Import keeps bundled server catalog updates by running through
  `SettingsStore::prepareImportedSettings()`.

## Milestone 4: Chat View, Commands, Logging

Goal: make the daily IRC workflow feel like current MaxChat.

Port:

- Rich chat rendering with mIRC/HexChat control codes.
- Hang indent, nick column alignment, marker line, timestamps.
- URL detection and URL list.
- Raw log.
- Logging and log replay.
- Find in chat.
- Ignore list and flood protection.
- Friends/notify list.
- Channel modes, ban list, user context menus.
- Command aliases.

Acceptance:

- Existing day-to-day MaxChat chat behavior works without comic mode.

Current status:

- Tools > Raw Log is implemented as a modeless dialog with retained raw IRC
  traffic, live updates while open, copy, clear, and a focused unit test.
- URL detection and Tools > URL List are implemented with a Core-only extractor,
  retained/de-duplicated links, live dialog updates, open/copy/clear controls,
  and focused unit tests.
- Tools > Find in Chat is implemented with Ctrl+F, next/previous,
  case-sensitive, wrap-search controls, and a focused unit test.
- Persistent logging and basic replay are implemented with a Core-only log
  store, per-network/per-target daily log files, existing `logging`/`replay_log`
  settings, automatic first-target replay on connect, Tools > Replay Current
  Log, and focused unit tests.
- Command aliases are implemented with a Core-only expander, the
  `command_aliases` settings map, conservative defaults, slash-command
  completion, and focused unit tests.
- Settings > Aliases now provides a native editor for the saved
  `command_aliases` map, including add/update/remove, default restore, and
  OK/Cancel behavior under focused offscreen tests.
- First-class command parsing/dispatch now includes `/query`, `/notice`,
  `/privmsg`, `/who`, `/whowas`, `/names`, `/list`, `/invite`, `/kick`, `/away`,
  `/back`, `/ctcp`, `/cycle`, `/hop`, `/leave`, `/amsg`, `/ame`, `/onotice`,
  channel-op helpers, ban/kickban helpers, local `/clear`, `/clearall`,
  `/lag`, `/uptime`, `/netinfo`, `/close`, `/reconnect`, `/disconnect`,
  NickServ-style service commands, and operator raw helpers in addition to the
  earlier daily-use command set.
- IRC `322/323` LIST replies now emit typed session/connection signals and print
  compact rows in the current chat. The full sortable Channel List browser is
  still pending.
- `/who` and `/whowas` now send native IRC lookup requests through the live
  connection path.
- `/whois` now renders common WHOIS numerics as readable status lines instead
  of leaving them only in raw log output.
- WHOWAS now renders its main reply, missing-history errors, and completion
  numerics as readable status lines instead of relying on raw log output.
- MOTD and common IRC error numerics now render as readable status lines for
  first-run connection output and routine nick/channel/join/operator failures.
- Away/invite acknowledgements and common channel-user/mode/operator command
  failures now render as readable status lines.
- Incoming `away-notify` `AWAY` events now render as readable status lines when
  another user goes away or comes back.
- Topic numerics now render no-topic, topic text, and channel URL replies as
  readable status lines while still updating the topic bar.
- Miscellaneous live IRC events now render as readable status lines, including
  server `ERROR`, `WALLOPS`, incoming `INVITE`, topic setter metadata, and
  channel creation time.
- `/part reason text` now leaves the current channel with that reason, matching
  the Python app's current-channel default unless an explicit channel is named.
- `/amsg` and `/ame` broadcast through joined channels from `ChatBufferStore`,
  and `/onotice` sends `NOTICE @#channel`.
- `/op`, `/deop`, `/voice`, `/devoice`, `/halfop`, `/dehalfop`, `/ban`,
  `/kickban`, and `/kb` build native IRC mode/kick lines for the active channel.
- `/identify`, `/id`, and `/ghost` map to NickServ payloads and keep using the
  redacted service-message echo path.
- `IrcSession` now stores server ISUPPORT tokens from numeric `005`; `/netinfo`
  displays them, `/lag` measures timed PING/PONG round trip, and `/uptime`
  reports app plus current connection uptime.
- Incoming CTCP handling is implemented for readable replies and standard
  VERSION/PING/TIME/CLIENTINFO responses while leaving DCC inactive for the DCC
  milestone.
- Topic editing is implemented through first-class `/topic` parsing and
  MainWindow IRC `TOPIC` dispatch for active or explicit channels.
- Basic channel mode support is implemented: `MODE`/`324` protocol signals,
  readable mode lines, `/mode`, cached channel mode replies, and a native Channel
  Modes dialog for common flags, key, and user limit.
- First-pass tree/member context menus are implemented for daily IRC actions:
  refresh, set topic, channel modes, leave/close, WhoIs, message, copy nick,
  operator mode changes, kick/ban, and CTCP shortcuts.
- Ignore masks are implemented in the IRC session and MainWindow, with
  `/ignore`, `/unignore`, saved settings, and member-list context menu actions.
- Basic friends/notify support is implemented with saved `friends`, `/notify`,
  `/unnotify`, `303`/ISON parsing, and periodic ISON polling for online/offline
  status lines.
- Native Settings menu editors for Ignore List and Friends / Notify are
  implemented with add/remove/OK/Cancel behavior and focused offscreen tests.
- First-pass Ban List editor is implemented as a modeless channel action using
  `367/368` replies and `MODE +b/-b` add/remove controls.
- Flood protection is implemented through a tested Core `FloodGuard`; when the
  existing setting is enabled, MainWindow auto-ignores bursty non-friend senders.
- Auto-reconnect is exposed in Preferences > Protection and wired into the live
  settings apply path.
- Reconnect Now is available from the Server menu and server-tree context menu,
  using the existing failover planner.
- Rich chat formatting now uses a tested Core formatter plus `QTextBrowser` to
  render mIRC control codes, colored nicks, timestamps, and aligned nick columns
  without adding new Qt modules.
- The services target now extracts OpenGraph/Twitter metadata and is linked into
  the app for direct image thumbnails and async website/social cards.
- A Qt Core-only link preview classifier now identifies direct raster images,
  direct audio/video URLs, generic website cards, X/Twitter status URLs, and
  Mastodon-style status URLs while rejecting unsafe local/private fetch targets.
- An async OpenGraph fetcher now prepares card HTTP requests,
  rejects non-HTML responses, enforces timeouts, and blocks local/private fetch
  targets by default.
- A services-only link preview policy helper now maps `content_services` toggles
  to direct image, audio/video, X/Twitter, Mastodon-style, and generic website
  card candidates.
- A services-only link preview renderer now generates escaped compact HTML for
  OpenGraph cards, direct image previews, and compact direct audio/video preview
  rows used by the chat-view preview path.
- The main chat view now wires in direct image thumbnails and async
  OpenGraph/X/Mastodon-style cards through the existing Services tab toggles.
  Direct audio/video URLs now render as clickable preview rows without linking
  Qt Multimedia.
- A Qt Core-only spellcheck dictionary catalog now feeds the Localization
  Preferences drop-down with available dictionaries and missing-dictionary
  placeholders without requiring Hunspell by itself.
- An optional Hunspell spellchecker target now loads dictionaries, checks words,
  and returns suggestions under test coverage. MainWindow wires it into a tested
  message-input highlighter when Hunspell and a dictionary are available, while
  cleanly disabling spellcheck when they are not.
- Routine JOIN/PART/QUIT visibility now follows `hide_joinpart` from settings and
  Preferences.
- Deeper context-menu parity is still pending.

## Milestone 5: Appearance And Preferences

Goal: preserve MaxChat's look and sane defaults.

Port:

- Theme system and no-theme/default options.
- Synthwave default.
- JetBrains Mono bold defaults.
- Font tab with Set All to JetBrains Mono and System Default.
- Preferences tabs: Appearance, Messages, Protection, Files, Services, Localization, Data.
- Menu/popup handling with the Linux/WSLg workaround documented in the Python app.
- Localization plumbing with Qt `.qm` files.

Acceptance:

- Clean first run visually matches current MaxChat closely.
- Menus/popups behave correctly on Linux and Windows.

Current status:

- First native Preferences dialog is implemented with Appearance, Messages,
  Protection, Files, Services, Localization, and Data tabs. It persists real
  settings, including the flood-protection and logging/replay controls,
  immediately applies theme/font/word-wrap basics in the main window, and reuses
  the existing import/export/reset server-list handlers for Data actions.
- Spell language choices are populated from the native dictionary catalog, so
  installed dictionaries show as available and missing launch languages are
  visibly marked.
- The message input now uses a document highlighter for spellcheck underlines
  while preserving Return submit, Up/Down history, and Tab completion behavior.
- Services preferences preserve the Python app's `content_services` map shape for
  future link-card/media work, including separate toggles for images,
  audio/video previews, X/Twitter cards, and generic website cards.
- Full preferences parity is still pending.

## Milestone 6: Services, Spellcheck, Media

Goal: preserve modern link and input features.

Port:

- Hunspell spellcheck adapter.
- Language dropdown with available/placeholder dictionaries.
- Direct image thumbnails.
- Generic OpenGraph cards.
- X/Twitter and Mastodon card handling.

Acceptance:

- Spellcheck works by default.
- Link cards/images can be toggled under Services.

Current status:

- Spellcheck is wired into the native message input when Hunspell and a matching
  dictionary are available, and it is on by default through saved settings.
- Direct image thumbnails, generic OpenGraph cards, X/Twitter cards, Mastodon
  cards, and compact direct audio/video preview rows are implemented and gated by
  Services tab toggles.
- Full inline WAV/MP3/video playback with `QMediaPlayer` is still pending; the
  current audio/video preview rows are clickable links and intentionally do not
  pull in Qt Multimedia.

TBD / later:

- WAV/MP3/video playback.
- Notification sound playback.

## Milestone 7: DCC

Goal: native DCC parity.

Port:

- DCC SEND/GET.
- Passive/reverse DCC.
- Resume support.
- DCC CHAT.
- Transfers dialog with progress, rate, ETA.
- Auto-accept policy and DCC port/IP settings.

Acceptance:

- File send/receive works on local and NAT-friendly paths.

## Milestone 8: Comic Mode

Goal: restore the signature feature.

Port:

- `.bgb` background decoder.
- `.avb` character decoder.
- Character model, emotions, poses, facing.
- Comic renderer with `QPainter`.
- Comic settings.
- Character assignment and emotion picker.
- Panel strip, reflow, save comic PNG.

Acceptance:

- Existing external comic art can be selected.
- Incoming messages render as panels matching current behavior.
- Plain IRC mode still works cleanly.

## Milestone 9: Scripting Decision

Goal: choose a maintainable replacement for Python scripting.

Options:

- Defer scripting for the first C++ release.
- Embed Python for compatibility with current scripts.
- Replace with a simpler JavaScript/Lua plugin API.
- Expose only command aliases and user scripts later.

Recommendation:

Defer scripting until the native IRC client and comic mode are stable. Embedding Python would bring
back much of the packaging weight the port is trying to avoid.

## Milestone 10: Packaging

Goal: produce testable Linux and Windows builds.

Linux:

- CMake/Ninja build.
- Bundle assets.
- Check Wayland/X11 behavior.
- Consider AppImage or zipped folder first.

Windows:

- Decide MSVC vs MinGW.
- Install Qt Windows dev stack separately.
- Build with CMake.
- Package with Qt deployment tooling.

Acceptance:

- Clean Linux test build.
- Clean Windows test build.
- No dependency on Python runtime.

## Test Strategy

Start with unit tests before UI tests:

- IRC parse tests.
- IRC formatting tests.
- Redaction tests.
- Server list import/reset tests.
- Reconnect/failover tests.
- Config migration tests.
- Comic asset decode tests.
- Spellcheck adapter tests.

Use Qt Test for C++ unit tests. Add manual smoke scripts for real GUI checks.

## Risks

- Rebuilding the UI is the largest effort.
- Python scripting parity is expensive and should be deferred.
- Comic asset decoding needs careful byte-for-byte tests.
- Link preview fetching needs the same SSRF/privacy protections.
- Windows packaging will need a separate native Qt toolchain.
- Exact config compatibility may need migration code instead of direct reuse.

## First Concrete Task

Build Milestone 0:

- Minimal CMake project.
- Native Qt main window.
- Resource loading.
- App metadata.
- One passing Qt Test.

After Milestone 0 builds, start Milestone 1 with the IRC parser and tests.
