# MaxChat C++/Qt Port

This is the native C++/Qt port workspace for MaxChat. It is separate from the
Python/PySide release project.

## Build

Debug:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Release:

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-release
ctest --test-dir build-release --output-on-failure
```

Run:

```bash
./build/maxchat-c
./build/maxchat-c --selftest
./build-release/maxchat-c --selftest
```

## Current Scope

Implemented so far:

- Native Qt Widgets shell.
- View menu side-panel toggles for Server List and Members, with persistent
  settings and no inactive Buttons as Tabs checkbox until that UI is implemented.
- Disabled Comic menu entries until Comic Mode is actually ported, plus a wired
  Help > About dialog.
- App metadata and selftest.
- IRC line parser.
- IRC formatting/control-code helper.
- Raw-log redaction.
- Reconnect/failover planner.
- Socket-free IRC session core for registration, CAP/SASL, NickServ fallback,
  nick collision handling, PING/PONG, common numerics, readable
  PRIVMSG/NOTICE/action signals, incoming CTCP request/reply handling, and safe
  outbound writes.
- Default network catalog with primary-first server ordering, failovers, and
  network homepages.
- Backup import merge helper that keeps the current server/homepage catalog and
  restores user-owned fields such as nick, passwords, channels, perform
  commands, and proxy settings.
- Settings store for `maxchat/settings.json`, including launch defaults, atomic
  saves, reset server list, import preparation, and default-network merge
  versioning.
- First native Server List dialog backed by the settings store and bundled
  defaults. It supports add/edit/delete, reset defaults, homepage button text,
  selected-network move up/down, startup toggle, and save-on-OK/cancel rollback.
- Socket-backed IRC connection layer wired into the main window. Server List
  Connect now starts a real connection, retries/fails over through saved servers,
  times out both socket-connect and IRC registration stalls, registers, autojoins
  channels, and displays raw lines plus readable messages.
- Topic replies/topic changes and chunked NAMES replies now update the topic
  label and member list in the first-pass chat UI.
- JOIN/PART/QUIT/KICK and nick-change events now update the chat log and visible
  member list for the active conversation.
- The first-pass server/channel tree tracks open targets, remembers manually
  joined channels/queries, and refreshes TOPIC/NAMES when a channel is selected.
- Tree items keep canonical channel/query targets separate from their display
  text, so unread/highlight labels do not change which target selection and
  context-menu actions operate on. Inactive targets show compact `[n]` and
  `[!h/n]` suffixes from `ChatBufferStore`, the network root aggregates those
  counters for visible targets only, and selecting a target clears that target's
  suffix. View > Mark All Read clears the current network's counters without
  switching buffers.
- A tested Core `ChatBufferStore` now models per-network server/channel/query
  buffers, active-buffer selection, unread/highlight counts, bounded line
  history, topics, joined state, channel members, and mark-read operations.
- `MainWindow` now writes chat lines, topics, and channel member state into
  `ChatBufferStore` and restores separate channel/query history plus metadata
  when the selected target changes. Incoming messages and common channel events
  are routed to their conversation buffer, with focused offscreen coverage.
- The visible multi-network buffer state is wired into MainWindow: the
  server/channel tree can show multiple network roots, remembers each network's
  current target and open channel/query list, preserves separate network
  histories, and restores the correct buffer when a target under another network
  is selected.
- Incoming private messages open a visible background query target and increment
  unread/highlight counters instead of stealing focus from the active channel.
- Stored channel members match IRC status-prefixed NAMES entries such as `@nick`
  and `+nick` against later plain nick events, so inactive channel member lists
  stay coherent across PART, QUIT, KICK, and nick changes.
- Tools > Raw Log opens a live, retained raw IRC log with copy and clear
  controls.
- Tools > URL List collects detected `http`, `https`, `ftp`, and `www.` links
  from chat lines and provides open/copy/clear controls.
- A separate services target now has tested OpenGraph/Twitter metadata parsing
  and fetching plus link-preview classification for direct images, audio/video
  URLs, generic website cards, X/Twitter posts, and Mastodon posts. Direct image
  previews, compact clickable audio/video rows, and fetched website/social cards
  are now wired into the rich chat view through the Services tab toggles; inline
  media playback remains deferred.
- Tools > Find in Chat opens a modeless search dialog with next/previous,
  case-sensitive, and wrap-search controls.
- Persistent chat logging writes per-network/per-target daily logs, and Tools >
  Replay Current Log loads recent saved lines for the active target.
- The chat view now uses `QTextBrowser` rich rendering with a tested Core
  formatter for mIRC control codes, colored nick labels, timestamps,
  right-aligned nick columns, and routine join/part/quit hiding.
- Command aliases expand from the `command_aliases` settings map before normal
  command parsing, with conservative defaults for `/j`, `/p`, and `/w`.
- Settings > Aliases opens a native command-alias editor for the saved
  `command_aliases` map, with add/update/remove controls, default restore, and
  OK/Cancel behavior.
- Tested user command parser for plain text, raw commands, `/join`, `/part`,
  `/leave`, `/cycle`, `/hop`, `/msg`, `/privmsg`, `/query`, `/notice`, `/me`,
  `/amsg`, `/ame`, `/onotice`, `/nick`, `/whois`, `/who`, `/whowas`, `/names`,
  `/list`, `/topic`, `/mode`, `/invite`, `/kick`, channel-op helpers,
  `/away`, `/back`, `/ctcp`, NickServ-style service commands, `/clear`,
  `/clearall`, `/lag`, `/uptime`, `/netinfo`, `/close`, `/reconnect`,
  `/disconnect`, and `/quit`; MainWindow input now uses that parser and
  supports Up/Down history plus Tab completion.
- `/clear` works as a local command even while disconnected and clears retained
  lines for the current chat buffer. View > Clear Current Chat uses the same
  buffer-safe clear path.
- `/close` closes the visible current target without sending `PART`, preserving
  stored history for the target.
- `/cycle` and `/hop` part and immediately rejoin the active or explicit
  channel without closing the visible target.
- `/leave` aliases `/part`, and `/part reason text` leaves the current channel
  with that reason unless an explicit `#channel` is named.
- `/amsg` and `/ame` send to every joined channel on the current network, and
  `/onotice` sends a notice to the active channel's ops.
- `/op`, `/deop`, `/voice`, `/devoice`, `/halfop`, `/dehalfop`, `/ban`,
  `/kickban`, and `/kb` send native channel mode/kick lines for the active
  channel.
- `/lag` measures server round-trip with a timed PING, `/uptime` reports app and
  connection uptime, and `/netinfo` displays stored ISUPPORT data from the
  server's `005` reply.
- `/reconnect` and `/disconnect` reuse the existing menu reconnect/disconnect
  paths.
- `/ctcp nick command [args]` reuses the existing CTCP send path from the
  member-list context menu.
- Incoming CTCP is handled before normal chat display: ACTION remains an action,
  VERSION/PING/TIME/CLIENTINFO requests get standard NOTICE replies, CTCP NOTICE
  replies display readably, and DCC-style requests stay inert until DCC support
  is ported.
- `/nickserv`, `/chanserv`, `/memoserv`, `/operserv`, `/hostserv`, and short
  forms such as `/ns` and `/cs` message the correct service nick, with
  password-like local echoes redacted. `/identify`, `/id`, and `/ghost` map to
  NickServ payloads.
- `/list [query]` sends IRC `LIST`; server `322/323` replies print compact
  channel/user/topic rows in the current chat.
- `/who target` and `/whowas nick` send native IRC lookup requests.
- `/whois nick` sends native IRC lookup requests and common WHOIS replies render
  as readable `[whois]` status lines.
- `/whowas nick` replies now render the historical identity line, missing-history
  errors, and end-of-WHOWAS completion as readable `[whowas]` status lines.
- MOTD and common IRC error numerics render as readable status lines, including
  typical nick/channel/join/operator failures.
- Away/back confirmations, invite success, bad nick/channel/user errors,
  missing-parameter errors, unknown modes, and operator/mode permission failures
  render as readable status lines.
- Incoming `away-notify` `AWAY` events render when another user goes away or
  comes back.
- Topic numerics render no-topic, topic text, and channel URL replies as
  readable status lines while still updating the topic bar.
- Server `ERROR`, `WALLOPS`, incoming `INVITE`, topic setter metadata, and
  channel creation time also render as readable status lines.
- `/topic` can query the active channel topic, set the active channel topic, or
  target an explicit channel with `/topic #channel new topic`.
- `/mode` is a first-class parser command, and the UI now consumes server
  `MODE`/`324` replies for readable mode updates.
- Channel Modes dialog supports common channel flags, key, and user-limit mode
  changes without adding new Qt module dependencies.
- Right-click context menus are wired for the server/channel tree and member
  list, including channel refresh, set topic, leave/close, WhoIs, private
  message, copy nick, operator mode changes, kick/ban, and CTCP shortcuts.
- Ignore masks are loaded from settings and applied in the IRC session before
  messages reach the UI; `/ignore` and `/unignore` update the saved list.
- Basic notify/friends support is wired with `/notify`, `/unnotify`, member-list
  menu actions, and periodic ISON polling for online/offline status lines.
- Settings > Ignore List and Settings > Friends / Notify provide native list
  editors for those saved masks and nicks.
- Channel tree Ban List opens a native modeless editor backed by IRC `367/368`
  replies, with add/remove controls that send `MODE +b/-b`.
- Flood protection settings now drive a tested Core guard; when enabled, bursty
  non-friend senders are auto-added to the ignore list.
- Auto-reconnect is wired to saved settings and exposed under Protection.
- Server menu and server-tree context menu include Reconnect Now, using the
  existing failover planner without adding dependencies.
- Simultaneous live multi-network sessions use separate IRC connections per
  network while keeping inactive network traffic in that network's buffers,
  unread/highlight counters, member/topic state, raw log, notify polling, and
  reconnect state.
- Quick Connect dialog for one-off live connections.
- Settings import/export/reset server list actions under the Settings menu.
- First native Preferences dialog for theme/font basics, message options,
  flood-protection controls, Files logging/replay toggles, Services link-preview
  toggles for images, audio/video, X/Twitter cards, and website cards,
  localization, spellcheck settings, IRC formatting/color toggles, and Data
  import/export/reset actions.
- Spellcheck language choices now come from a small dictionary catalog. Installed
  `.aff`/`.dic` dictionaries show as available, while missing languages are
  clearly marked in the drop-down.
- When the Hunspell development package is present, the app links the native
  spell engine and underlines misspelled words in the message input. The
  highlighter follows the saved Localization spellcheck settings, ignores URLs,
  channels, and command syntax, and falls back cleanly when a dictionary is
  missing.

The app target intentionally links only the Qt modules it currently uses:
`Qt6::Core`, `Qt6::Gui`, `Qt6::Widgets`, and now `Qt6::Network` because live IRC
is wired into the main window. Hunspell is linked only when the native input
spellcheck feature is available. Multimedia and SVG remain deferred until a
feature actually needs those modules.

Not ported yet:

- Full preferences/settings UI parity beyond the first native preferences slice.
- Full chat command dispatcher beyond the expanded daily/channel-op command set.
- Comic mode.
- DCC.

TBD / later:

- WAV/MP3/video playback.
- Notification sound playback.
