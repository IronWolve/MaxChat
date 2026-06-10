# MaxChat C++ Port Status

## 2026-06-09

### Current Handoff Snapshot

- Latest completed slice: simultaneous live multi-network connection plumbing
  plus UI parity polish verification.
- MainWindow now creates and keeps one `IrcConnection` per network instead of
  reusing one socket for every Server List/Quick Connect request.
- Incoming events from inactive networks update that network's server/channel/
  query buffers, unread/highlight counts, members, topic, raw log, and notify
  state without repainting or stealing focus from the active chat.
- Disconnect/reconnect, server failover attempts, manual disconnect state,
  connection uptime, ISON notify polling, ignore masks, channel modes, pending
  NAMES, and context-menu actions are now network-scoped.
- The server tree can switch between live networks and restores each network's
  current target and visible buffer history.
- UI parity scan: visible labels use "Buttons as Tabs", the toolbar keeps the
  Python-style button layout (`Servers`, `Members`, `Channels`, `Join`,
  `Comic`, `Emotion`, `URLs`, `Transfers`, `Prefs`), theme/chat-theme/wallpaper
  menus are wired from JSON/assets, wallpaper transparency rules are present,
  and planned menu/dialog stubs remain visible for future features.
- Theme parity pass (later on 2026-06-09): fixed wallpapers never rendering
  (`file://` URL inside QSS `url()` is ignored by Qt; now a plain quoted path
  via the renamed `effectiveWallpaperPath()`), which also restores the
  Synthwave gradient fallback; added `theme_catalog` test; `build.bat` now
  fails when dist assets cannot be copied; `sync-to-win.sh` refreshes
  `dist-win/assets` on sync. Remaining theme gaps vs the Python app are listed
  in `DEV_NOTES.md`.
- Verification: Debug `ctest` 40/40 passed, Release `ctest` 40/40 passed,
  Debug selftest passed, Release app build passed, and Release selftest passed.
- Next focused step: live smoke-test two real networks at the same time, then
  tighten any remaining command/UI edge cases found in that session. Theme
  parity work continues alongside (chat-theme extras, user themes, QPalette,
  theme editor).

## 2026-06-08

### Historical Snapshot

- Current workspace: this C++/Qt port tree.
- Python MaxChat tree remains read-only reference material.
- `HANDOFF.md` was updated with the then-current resume guide.
- Latest completed slice: chat display parity cleanup. Raw IRC wire lines now
  stay in Raw Log instead of printing in chat, connection/server status routes
  to the Server buffer, event lines use IRC-style `* nick ...` formatting, and
  aligned chat rows show the nick/message separator line.
- Verification at the time: Debug `ctest` 38/38 passed, Release `ctest` 38/38
  passed, Release selftest passed, and earlier `cppcheck` was clean across 77
  files.
- Optimized app size at the time: `4,610,560` bytes.
- Dependency status: Hunspell is intentionally linked for spellcheck;
  `Qt6Multimedia` and `Qt6Svg` remain absent from the app dependency check.
- Next focused step at the time: live smoke-test the usable basic IRC path.

The native C++/Qt workspace now has a tested shell, IRC protocol core, default
network catalog, settings store, first Server List dialog, and socket-backed
connection layer wired into the main window.

Created:

- CMake/Ninja project with Debug and Release build trees.
- Native Qt Widgets app entry point.
- `MainWindow` shell with menu bar, server tree, chat view, members list, topic
  label, and input.
- View menu side-panel controls for Server List and Members. They save to the
  existing `server_list_visible` and `member_list_visible` settings and restore
  correctly on a new window.
- Comic menu entries are visible but disabled until Comic Mode is ported, and
  Help > About is wired to a real dialog.
- Qt resource file for bundled fonts, icon, wallpaper, and notification sound.
- App metadata module and `--selftest` startup check.
- IRC parser port from `maxchat/irc/message.py`.
- IRC formatting/control-code helper port from `maxchat/ui/irc_format.py`.
- Raw-log redaction rules for server PASS, SASL payloads, and services
  password commands.
- Reconnect/failover planner with the three-attempt-per-server behavior.
- `IrcSession`, a socket-free protocol/session core for registration commands,
  CAP/SASL, NickServ fallback rules, nick collision handling, PING/PONG, common
  numeric replies, raw-line redaction, and outbound line-size checks.
- `DefaultNetworks` model generated from the current Python default server list:
  146 networks, primary server first, failovers parsed into host/port/TLS
  endpoints, network homepage kept on the group.
- `NetworkImport` merge helper preserving current bundled server/homepage catalog
  fields while restoring user-owned fields from backup imports.
- `SettingsStore` using the same `maxchat/settings.json` layout as the Python
  app. It supports atomic JSON saves, invalid/missing file fallback, launch
  defaults, default server-list conversion, reset server list, import
  preparation, and default-network merge versioning.
- `ServerListDialog`, the first native network-management UI slice. It is backed
  by `SettingsStore`, shows the network homepage/domain with the selected server,
  updates the homepage button as selection changes, supports add/edit/delete,
  reset defaults, startup toggle, and selected-network move up/down, and keeps
  Cancel as a rollback path.
- `MainWindow` now opens Server List from the Server menu and saves accepted
  changes back to `settings.json`. The Connect button starts the selected
  network through the first-pass live IRC path.
- `IrcConnection`, a `QSslSocket` wrapper in a separate `maxchat_network` target.
  It feeds incoming lines into `IrcSession`, mirrors protocol signals, and emits
  failover-friendly disconnect reasons for failed connect attempts.
- `ConnectionPlan`, a tested mapper from saved server-list entries into runtime
  connection state. It keeps primary servers first, parses failovers, normalizes
  autojoin channels, and preserves separate NickServ/SASL and server PASS fields.
- MainWindow live IRC handoff: Server List Connect now starts a real socket
  connection, retries up to three times per saved server before advancing to the
  next failover, registers through `IrcSession`, autojoins saved channels, formats
  incoming PRIVMSG/NOTICE/action messages, and supports daily-use commands and
  plain text to the active channel/query.
- Connection attempts now cover both socket-connect timeout and IRC registration
  timeout. If a server accepts TCP but never completes registration, the
  connection emits a terminal failure so MainWindow advances through the existing
  retry/failover planner instead of sitting on "Registering..." indefinitely.
- Topic and member-list handoff: IRC `TOPIC`, `331`, `332`, and chunked
  `353`/NAMES replies now update the topic label and member list in the
  first-pass UI.
- Channel membership events: IRC `JOIN`, `PART`, `QUIT`, `NICK`, and `KICK`
  now update the chat log and active member list. KICK only clears the active
  channel when the kicked nick is the current user.
- Open target tracking: the first-pass server/channel tree remembers manually
  joined channels and private-message targets, allows user selection, and
  refreshes channel TOPIC/NAMES when a channel is selected.
- Server/channel tree items now keep their raw target in `Qt::UserRole` while
  showing a separate display label. This keeps selection and context-menu
  actions stable while the visible tree text includes unread/highlight markers.
- The server/channel tree now uses `ChatBufferStore` counters in those display
  labels: plain unread lines show `[n]`, highlighted unread lines show
  `[!h/n]`, the network root aggregates child counters, and selecting a buffer
  clears that buffer's suffix. The root aggregate counts only targets currently
  visible in the tree, so closed hidden buffers do not keep the root marked
  unread.
- Incoming private messages now create visible background query targets with
  unread/highlight counters instead of switching away from the active channel.
  Selecting the query clears its unread counter and renders the stored PM
  history.
- View > Mark All Read clears all unread/highlight counters for the current
  network through `ChatBufferStore` and refreshes the visible tree labels without
  changing the active buffer.
- `/part #channel` now rebuilds the server/channel tree after forgetting the
  target even when the parted channel is not the active target.
- `ChatBufferStore`, a Core-only buffer model for the upcoming multi-network UI
  refactor. It tracks per-network server/channel/query buffers, active-buffer
  selection, unread/highlight counts, bounded line history, topics, joined state,
  and channel members while matching network/target names case-insensitively and
  preserving display casing.
- `MainWindow` now integrates `ChatBufferStore` for visible per-target history:
  the active buffer is rendered into the chat view, inactive channel/query
  buffers retain their own lines and unread/highlight counts, target switching
  restores the selected buffer, and common incoming message/channel events route
  to their conversation buffer. Topic/member metadata now comes from the same
  store, including inactive channel `TOPIC`, NAMES, JOIN, PART, QUIT, NICK, and
  KICK updates.
- `MainWindow` now keeps visible multi-network buffer state instead of flattening
  everything through one active network label. The server/channel tree can show
  multiple network roots, keeps per-network current targets and open
  channel/query lists, preserves each network's buffer history, and restores the
  selected network's chat buffer when a target under that network is chosen.
- Raw IRC wire lines now update Raw Log only instead of being printed into the
  chat buffer. Connection/server status is routed to the Server buffer, common
  join/part/quit/nick/kick/mode events use `* nick ...` IRC event formatting,
  and aligned chat rows include a visible nick/message separator.
- `ChatBufferStore` member matching now treats IRC status-prefixed NAMES entries
  such as `@nick` and `+nick` as the same user as plain `nick`, so later PART,
  QUIT, KICK, and nick-change events update stored channel member lists
  correctly.
- `ChatBufferStore` now exposes focused mark-read helpers for one buffer or the
  whole current network, giving the UI a tested way to clear notification state
  independently from target selection.
- `RawLogDialog`, a modeless Tools menu dialog that shows retained raw IRC
  traffic, updates live while open, and supports copy/clear actions without
  adding any new Qt feature modules.
- `UrlDetector` and `UrlListDialog`, a Core-only URL extraction helper plus a
  modeless Tools menu URL list. It detects common pasted links from chat lines,
  de-duplicates them, updates live while open, and supports open/copy/clear
  actions.
- `OpenGraphParser`, a services slice for generic link cards. It
  extracts OpenGraph/Twitter title, description, image, canonical URL, site name,
  and type metadata, handles common HTML entities, resolves relative URLs against
  the page URL, and is covered by focused unit tests.
- `OpenGraphFetcher`, an async services slice that builds link-preview HTTP
  requests, parses fetched HTML into OpenGraph cards, rejects non-HTML responses,
  times out slow replies, and blocks local/private preview targets by default.
  The positive fetch path is covered by a local-server unit test that explicitly
  opts into private-network fetches.
- `LinkPreviewClassifier`, a Qt Core-only services slice for direct raster
  images, direct audio/video URLs, generic website cards, X/Twitter status URLs,
  and Mastodon-style status URLs. It also rejects unsupported schemes, local
  network targets, credential-bearing URLs, and SVG direct-image previews so the
  later fetcher has a safer policy boundary.
- `LinkPreviewPolicy`, a services helper that maps the saved `content_services`
  toggles to classified preview types so future UI wiring does not duplicate the
  rules for images, media, X/Twitter cards, and generic website cards.
- `LinkPreviewRenderer`, a services helper that renders escaped compact HTML for
  OpenGraph cards, direct image previews, and direct audio/video preview rows,
  including canonical click targets, primary-domain display, image links, and
  bounded image dimensions.
- The main chat view now uses the services stack for link previews: direct image
  URLs render as clickable thumbnails, direct audio/video URLs render compact
  clickable preview rows, OpenGraph/X/Mastodon-style pages fetch async cards,
  Services tab toggles gate each preview type, replay/raw-log lines do not
  trigger preview fetches, and unsafe card image URLs are stripped.
- `SpellcheckDictionaryCatalog`, a Qt Core-only spell/localization support slice
  that lists launch languages, checks for paired `.aff`/`.dic` dictionaries in
  packaged and system dictionary paths, and labels missing dictionaries in the
  Preferences drop-down.
- `HunspellSpellchecker`, an optional spell engine target that loads paired
  Hunspell `.aff`/`.dic` dictionaries, checks words, and returns suggestions. It
  is covered by a focused unit test when the Hunspell development package is
  present.
- `SpellcheckHighlighter`, a tested document highlighter for the message input.
  MainWindow uses it with Hunspell when a dictionary is available, honors the
  saved spellcheck settings, ignores URLs/channels/command syntax, and falls
  back cleanly when spellcheck is disabled or no dictionary exists.
- `ChatFindDialog`, a modeless Tools menu chat-search dialog with Ctrl+F,
  next/previous, case-sensitive search, wrap search, and status feedback from
  the active chat view.
- `ChatLogStore`, a Core-only daily log writer/reader. MainWindow honors the
  existing `logging` and `replay_log` settings, writes per-network/per-target
  logs under the app config directory, replays the first active target on connect
  when enabled, and exposes Tools > Replay Current Log.
- `CommandAlias`, a Core-only command alias expander. MainWindow loads the
  `command_aliases` settings map, expands aliases before parsing input, includes
  alias names in slash-command completion, and ships conservative defaults for
  `/j`, `/p`, and `/w`.
- `AliasEditorDialog`, a native Settings > Aliases editor for the saved
  `command_aliases` map. It supports add/update/remove, restores the conservative
  defaults, keeps Cancel as rollback, and updates only the alias value in the
  loaded settings snapshot.
- `CommandParser`, a tested Qt Core-only parser for plain text, raw IRC commands,
  `/join`, `/part`, `/leave`, `/cycle`, `/hop`, `/msg`, `/privmsg`, `/query`,
  `/notice`, `/me`, `/amsg`, `/ame`, `/onotice`,
  `/nickserv`, `/ns`, `/chanserv`, `/cs`, `/memoserv`, `/ms`, `/operserv`,
  `/os`, `/hostserv`, `/hs`, `/nick`, `/whois`, `/who`, `/whowas`, `/names`,
  `/topic`, `/mode`, `/list`, `/invite`, `/kick`, `/op`, `/deop`, `/voice`,
  `/devoice`, `/halfop`, `/dehalfop`, `/ban`, `/kickban`, `/kb`, `/away`,
  `/back`, `/lag`, `/uptime`, `/netinfo`, and local control commands.
- First-class `/topic` handling sends IRC `TOPIC` for the active channel or an
  explicit target channel, covering both topic queries and topic updates.
- `/clear`, `/close`, `/reconnect`, and `/disconnect` are implemented as local
  commands and slash-completion targets. `/ctcp` sends CTCP requests through the
  same connection path as the member-list CTCP menu. `/clear` clears the active
  view plus stored current-buffer lines without requiring a live IRC connection.
- Incoming CTCP handling now keeps CTCP traffic out of normal chat lines:
  ACTION remains an action message, VERSION/PING/TIME/CLIENTINFO requests get
  standard NOTICE replies, CTCP NOTICE replies display readably, and DCC-style
  requests are displayed as inert status lines until DCC work lands.
- `/cycle` and `/hop` send `PART` followed by `JOIN` for the active or explicit
  channel, keeping that target visible and active while the reconnect happens.
- NickServ-style service commands send regular private messages to their service
  nick and redact password-like command echoes before displaying or logging them.
  `/identify`, `/id`, and `/ghost` map to the matching NickServ payloads.
- `/amsg` and `/ame` use `ChatBufferStore` joined-channel state to message or
  action every joined channel on the current network, and `/onotice` sends
  `NOTICE @#channel`.
- `/op`, `/deop`, `/voice`, `/devoice`, `/halfop`, and `/dehalfop` build native
  `MODE` lines for the active channel. `/ban` sends `MODE +b`, while
  `/kickban` and `/kb` send the ban and kick lines together.
- `/leave` is an alias of `/part`, `/part reason text` now leaves the current
  channel with that reason, `/back` clears away state, and `/clearall` clears
  all current-network buffers.
- `/lag` sends a timed PING and prints the matching PONG round-trip in
  milliseconds. `/uptime` reports app uptime and current connection uptime.
  `/netinfo` displays stored server ISUPPORT data from numeric `005`.
- `/list [query]` sends IRC `LIST`, and `322/323` replies are parsed into
  channel/user/topic rows for the UI and later Channel List browser work.
- `/who target` and `/whowas nick` are first-class parsed commands that send
  native IRC lookup requests through the live connection path.
- `/whois nick` now has readable protocol output for common server replies:
  user/host, server, channels, account, away, operator, idle/signon, secure
  connection, and end-of-WHOIS.
- WHOWAS replies now render the main historical identity line, missing-history
  errors, and end-of-WHOWAS completion as readable `[whowas]` status lines.
- Incoming `away-notify` `AWAY` events now render as readable `[away]` status
  lines when a user goes away or comes back.
- Topic-related numerics now render readable status lines for no-topic replies,
  topic text replies, and channel homepage/URL replies while still updating the
  topic bar.
- Away/back confirmations, invite success, too-many-channel errors, bad nick
  errors, user-not-in-channel/already-in-channel errors, missing-parameter
  errors, unknown mode errors, and operator/mode permission failures now render
  as readable status lines.
- MOTD and common error numerics now emit readable status lines for connection
  greeting output and common failures such as no such nick/channel, cannot send,
  unknown command, bad password, full/invite-only/banned/keyed channels,
  registered-only channels, and not-channel-operator errors.
- Miscellaneous live IRC events now emit readable lines for server `ERROR`,
  `WALLOPS`, incoming `INVITE`, topic setter metadata, and channel creation
  time.
- IRC `MODE` events and `324` channel mode replies now have protocol signals,
  readable chat output, and cached per-channel mode lines for the UI.
- `ChannelModesDialog`, a native no-extra-module dialog for common channel flags,
  key, and user-limit modes, with focused unit coverage.
- MainWindow has right-click menus for the server/channel tree and member list.
  They cover channel refresh, set topic, channel modes, leave/close, WhoIs,
  private message, copy nick, operator modes, kick/ban, and CTCP shortcuts.
- Ignore masks are now saved in settings, compiled in `IrcSession`, and applied
  before PRIVMSG/NOTICE signals reach the UI. `/ignore`, `/unignore`, and the
  member-list context menu update the list.
- Basic friends/notify support is implemented with saved `friends`, `/notify`,
  `/unnotify`, member-list add/remove actions, `303`/ISON parsing, and a
  one-connection poll timer that reports online/offline status lines.
- `IgnoreListDialog` and `FriendsNotifyDialog` are native Settings menu editors
  for the saved ignore masks and notify nicks, with add/remove/OK/Cancel
  behavior covered by focused offscreen tests.
- `BanListDialog` is implemented as a modeless channel action. It requests
  `MODE #channel +b`, consumes existing `367/368` replies, displays mask/setter
  rows, and sends `MODE +b/-b` for add/remove.
- `FloodGuard`, a Core-only flood protection helper, is implemented with unit
  tests. MainWindow wires the existing `flood_protect`, `flood_msgs`, and
  `flood_secs` settings into auto-ignore behavior for non-friend senders.
- `auto_reconnect` is exposed in Preferences > Protection and is loaded by
  MainWindow during settings application so live preference changes are honored.
- Server menu and server-tree context menu now include `Reconnect Now`, which
  reuses the existing reconnect/failover planner and avoids double-starting a
  socket during intentional reconnects.
- `ChatLineFormatter`, a Core-only rich line formatter, is implemented with unit
  tests. The main chat view now uses `QTextBrowser` and renders mIRC control
  codes, colored nick labels, timestamps, and aligned nick columns while keeping
  plain log text available. Python-style timestamp tokens such as `%I:%M %p` are
  translated to Qt date/time format tokens at display time.
- The existing `hide_joinpart` setting is now honored for routine JOIN/PART/QUIT
  event lines, while own join/part actions still print feedback.
- MainWindow input history and tab completion. Up/Down walk recent submitted
  input; Tab completes slash commands, the current target, saved autojoin
  channels, and member-list entries.
- `QuickConnectDialog`, a small one-off connection dialog for host/port/TLS/nick
  and startup channels.
- Settings menu actions for import settings, export settings, and reset server
  list. Import runs through `SettingsStore::prepareImportedSettings()` so bundled
  server catalog updates are not replaced by stale backup server lists.
- First native `PreferencesDialog` with Appearance, Messages, Protection, Files,
  Services, Localization, and Data tabs. It edits real `SettingsStore` values,
  including theme, app/chat fonts, word wrap, timestamp/nick alignment toggles,
  IRC formatting/color toggles, flood-protection threshold/window settings,
  logging/replay toggles, content service toggles (`images`, `xcards`,
  `webcards`), interface language, and spellcheck settings. The Data tab reuses
  MainWindow's import/export/reset server-list handlers and closes before
  import/reset to avoid saving a stale settings snapshot. MainWindow applies
  font/theme/word-wrap basics immediately after Preferences are saved or settings
  are imported.
- Services preferences now preserve toggles for image previews, audio/video
  previews, X/Twitter cards, and generic website cards in the existing
  `content_services` map.
- `.clang-format`, `.gitignore`, and build README.

Optimization notes:

- The app target links `Qt6::Core`, `Qt6::Gui`, `Qt6::Widgets`, and
  `Qt6::Network`. Network is now intentional because the main window starts live
  IRC connections.
- Hunspell is now intentionally linked into the app when the development package
  is present because the message input spellcheck UI is wired. If Hunspell is not
  found, the app still builds with spellcheck disabled.
- `Qt6::Multimedia` and `Qt6::Svg` remain out of the app target. Direct
  audio/video preview rows are rendered as clickable HTML links for now, so they
  do not require Multimedia yet.
- WAV/MP3/video playback and notification sound playback are explicitly TBD for
  a later slice; the current port keeps those out of the active dependency set.
- Debug app size: `22,022,472` bytes.
- Release app size: `4,610,560` bytes.

Verified:

- Debug configure:
  `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
- Debug build: `cmake --build build`
- Debug tests: `ctest --test-dir build --output-on-failure`
- Debug selftest: `build/maxchat-c --selftest`
- Static check:
  `cppcheck --enable=warning,performance,portability --std=c++20 --suppress=unknownMacro --error-exitcode=1 src tests`
- Release configure:
  `cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`
- Release build: `cmake --build build-release`
- Release tests: `ctest --test-dir build-release --output-on-failure`
- Release selftest: `build-release/maxchat-c --selftest`
- Link check: `ldd build-release/maxchat-c` shows Hunspell as expected for
  spellcheck. It does not show `Qt6Multimedia` or `Qt6Svg`.

Result from that verification pass:

- `ctest` Debug: 38/38 passed.
- `ctest` Release: 38/38 passed.
- selftest: `MaxChat C++ 0.1.0-dev selftest OK`.
- source-only `cppcheck`: clean across 77 files.

Current test targets:

- `app_info`
- `irc_message`
- `irc_format`
- `irc_redaction`
- `reconnect_planner`
- `command_parser`
- `command_alias`
- `flood_guard`
- `chat_line_formatter`
- `open_graph_parser`
- `open_graph_fetcher`
- `link_preview_classifier`
- `link_preview_policy`
- `link_preview_renderer`
- `spellcheck_dictionary_catalog`
- `spellcheck_highlighter`
- `hunspell_spellchecker`
- `theme_catalog`
- `main_window_link_preview`
- `connection_plan`
- `irc_session`
- `default_networks`
- `network_import`
- `settings_store`
- `url_detector`
- `chat_log_store`
- `chat_buffer_store`
- `chat_find_dialog`
- `alias_editor_dialog`
- `channel_modes_dialog`
- `ban_list_dialog`
- `ignore_list_dialog`
- `friends_notify_dialog`
- `server_list_dialog`
- `preferences_dialog`
- `quick_connect_dialog`
- `raw_log_dialog`
- `url_list_dialog`
- `irc_connection`

Next milestone:

- Live-test two real networks at the same time and fix any connection,
  failover, tree-switching, or unread/highlight issues found there.
- Close the remaining readable IRC reply gaps and command edge cases found
  during live testing.
- Continue Preferences, theme, toolbar, and Quick Connect parity against the
  Python app.
- Keep the target-link rule: only link Network, Multimedia, Svg, or Hunspell into
  app-facing targets once the feature using that module is wired into the app.
