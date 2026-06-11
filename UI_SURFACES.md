# MaxChat UI surfaces — the placement contract

**Internal design doc** (keep local; gitignore before any public push, like
AUDIT.md). Applies to BOTH clients — the C++ port (`maxchat-c`) and the Python
original (`maxchat`). Mirror changes into `maxchat/DEVDOCS/`.

## Why this exists

Several bugs came from one root cause: **one piece of information being written
to the wrong surface** — your sent message echoed into the bottom status bar,
"Not connected" shown in the topic bar, "network - channel" context dumped into
the topic bar, etc. Each surface must have a **single responsibility**. Before
adding any `setText`/`showMessage`/`setWindowTitle`, check this table: if the
content doesn't match the surface's responsibility, it goes somewhere else.

## The surfaces and what each may show

| Surface | Sole responsibility | MAY contain | MUST NOT contain | C++ writer | Python writer |
|---------|---------------------|-------------|------------------|------------|---------------|
| **Window title** | Identity + active context + unread | `(unread) MaxChat <ver> — <network> / <channel>` | chat text, transient status | `updateWindowTitle()` | `_refresh_title()` |
| **Topic bar** (top) | The **active channel's topic** | topic text, or empty | connection state, network/channel names, chat | `renderActiveBufferMetadata()` → `m_topicLabel` only | `_set_topic()` → `self.topic` only |
| **Status bar** (bottom) | **Transient connection lifecycle / one-shot status** | "Not connected", "Connecting…", "Connected to X as nick", "Disconnected", "Saved.", "Found …" | chat lines, the channel topic | `showConnectionStatus()` / `statusBar()->showMessage()` | `self.statusBar().showMessage()` |
| **Nick label** (input row) | **Your current nick** on the active network | the nick (or "—"/empty when none) | anything else | `m_nickLabel` (`updateNickLabel`) | `self.nick_label` |
| **Chat view** (center) | The conversation | messages, `*` events, `!` system lines, replays, previews | status that belongs in the bars | `append*ChatLine` / `appendSystemLine*` | `_append*` |
| **Server/network tree** (left) | Networks → channels/queries + per-buffer unread/highlight badges | network + buffer names, activity markers | topics, status | `rebuildNetworkTree()` / `updateNetworkTreeLabels()` | `_rebuild_tree()` |
| **Member list** (right) | Members of the active channel | nicks, role-group headers, "N users" header | non-members, status | `renderActiveBufferMetadata()` / `recolorMemberList()` | `_refresh_active_members()` |
| **Buffer tabs** | Open buffers for quick switching | buffer names + activity | — | `syncBufferTabs()` | tab bar |
| **Comic panels** | Comic-mode render of recent chat | comic panels (fixed count) | — | `refreshComic()` | `_render_strip()` |
| **Toasts / tray** (`Notifier`) | Out-of-focus alerts (PM/highlight) | title + text, network/target to focus on click | — | `notify()` | `notifier` / tray |

### Rules of thumb

1. **Topic bar = topic, full stop.** Empty on the server tab, in a PM, when not
   connected, or when the channel has no topic. Never connection state.
2. **Status bar = transient status only.** Never the topic, never chat. It is the
   place for "what just happened / what's the connection doing".
3. **Active context (which network/channel you're on) lives in the window title**
   and is implied by the tree selection — not in the topic or status bars.
4. **Your nick** goes in the nick label by the input box (and is part of the
   "Connected to X as nick" status message), nowhere else.
5. A toast/notification is **not** the status bar — alerts go through `notify()`.

## Audit — C++ vs Python (2026-06-11)

| Surface | Python | C++ now | Status |
|---------|--------|---------|--------|
| Topic bar | topic only (empty otherwise) | topic only (fixed: was a catch-all) | ✅ match |
| Status bar | lifecycle + one-shots | lifecycle + one-shots (fixed: was mirroring every chat line) | ✅ match |
| Nick label | current nick / "—" | `<nick>:` prefix, hidden when none | ✅ parity (cosmetic: prefix style) |
| Chat view | messages/events/system | same | ✅ match |
| Member list + header | members + "N users" | members + "N users" | ✅ match |
| Server tree | nets/buffers + activity | nets/buffers + activity | ✅ match |
| **Window title** | `(unread) MaxChat — net / chan` | **was static `MaxChat <ver>`** → now `MaxChat <ver> — net / chan` | ⚠→✅ **fixed here**; unread-count prefix still TODO |
| Comic panels | fixed N-panel grid | fixed N-panel grid (fixed earlier) | ✅ match |

### Known remaining gaps (tracked)

- **Unread count in the window title** — Python prefixes `(3)` / `(1⚑)`; C++
  title shows context but not yet the unread/highlight count. Low priority; the
  tree already shows per-buffer activity. (TODO)
- **Status-bar context vs title** — C++ still shows "network - channel" *context*
  in the status bar on buffer switch (harmless, informative). Python keeps the
  status bar purely lifecycle and puts context in the title. Acceptable
  divergence; revisit if it feels noisy.

## How to use this when adding UI

Before writing to any surface, ask: **"is this content the surface's sole
responsibility?"** If a string mixes concerns (e.g. "network - channel - topic"),
split it: topic → topic bar, network/channel → title, lifecycle → status bar.
When in doubt, match the Python writer named in the table above.
