# MaxChat UI design — what each window/area is for

**Internal design doc** (keep local; gitignore before any public push, like
AUDIT.md). Applies to BOTH clients — `maxchat-c` (C++) and `maxchat` (Python).
Mirror changes into `maxchat/DEVDOCS/`.

> This doc covers *what goes in each surface*. For *how the chat view itself is
> rendered* — the single-shared-view model and the rule that anything which must
> survive a buffer switch has to live in `ChatBufferStore` (replay history,
> unread marker) — see **`CHAT_VIEW_DESIGN.md`**. Read that before adding any new
> chat-view element.

## Purpose of this doc

Each area of the window has **one job**. Most of our UI bugs were one piece of
information shown in the wrong place (your message in the status bar, "Not
connected" in the topic bar, the network/channel name in the topic bar). This
doc is the source of truth for **what belongs in each area**. Before putting any
text in an area, find it below and check the text matches that area's job. If it
mixes jobs (e.g. "network – channel – topic"), split it across the right areas.

The implementation mapping (which widget/function) is in the appendix; the design
is the plain-language lists below.

---

## 1. Window title (the OS title bar)
**Job:** tell you, at a glance, which app + where you are + if anything's waiting.
**Shows:**
- App name + version.
- Active network, and the active channel/PM when one is selected.
- (TODO) an unread/highlight count prefix.

**Never shows:** chat text, the channel topic, connection status.
**Example:** `MaxChat — synIRC / #iw-test-stuff`

## 2. Topic bar (the strip across the top)
**Job:** show the **topic of the channel you're currently looking at** — nothing else.
**Shows:**
- The active channel's topic text.
- Nothing (empty) when: not connected, on the server tab, in a private message, or the channel has no topic.

**Never shows:** connection status, network/channel names, your nick, chat.
**Does:** double-click → set the channel topic.

## 3. Status bar (the strip across the bottom)
**Job:** report **what just happened / what the connection is doing** — transient, one line.
**Shows:**
- Connection lifecycle: "Not connected — Server ▸ Server List…", "Connecting…", "Connected to <net> as <nick>", "Disconnected".
- One-shot results of an action: "Saved.", "Found 'x'.", "Marked 3 chats read.", "Do Not Disturb on".

**Never shows:** chat lines, the channel topic.
**Note:** it's transient status, **not** a place for alerts — see Notifications (#10).

## 4. Nick label (next to the message box)
**Job:** show **your current nick** on the active network.
**Shows:** your nick (or "—"/empty when not connected).
**Never shows:** anything else.

## 5. Chat view (the big center area)
**Job:** the conversation itself.
**Shows:**
- Messages (`<nick> text`), actions (`* nick …`).
- Events: joins/parts/quits/nick changes/mode changes (subject to hide-join/part).
- System/notice lines (`! …`, `-nick- …`), link previews, and dimmed log replay with a "Chat ended" divider.

**Never shows:** status that belongs in the bars.
**Does:** click links; right-click for context actions; scrollback.

## 6. Server / network tree (left panel)
**Job:** navigate — networks and their channels/PMs — and signal activity.
**Shows:**
- Each network, expandable to its channels and private-message buffers.
- Per-buffer activity markers (unread / highlight).

**Never shows:** topics, status, chat.
**Does:** click → switch buffer; right-click → join/part/close/refresh topic·names·modes, etc.

## 7. Member list (right panel)
**Job:** who's in the **active channel**.
**Shows:**
- The channel's members, grouped by role (ops/voiced/…), away users dimmed.
- A header: "N users" (or "Members").

**Never shows:** members of other channels, status.
**Does:** double-click → open query / whois; right-click → op/kick/ban/ignore/etc.

## 8. Buffer tabs (optional bar)
**Job:** quick-switch between open buffers.
**Shows:** open buffer names + activity. **Does:** click → switch; Alt+1..9 too.

## 9. Comic panels (when Comic Mode is on)
**Job:** render recent chat as comic panels.
**Shows:** a fixed grid of `comic_panels` panels (blanks until filled).
**Does:** right-click → copy / save comic.

## 10. Notifications — toasts & tray (not a window area)
**Job:** alert you when a PM/highlight arrives **while MaxChat isn't focused**.
**Shows:** title + text; clicking focuses that network/buffer.
**Never:** use the status bar for alerts — alerts go here.

---

## Decision rule (use this every time)

> Take the thing you want to show. Which **one** area's job (above) does it match?
> Put it only there. If it's two things glued together, split it:
> **topic → topic bar · network/channel → title · status → status bar ·
> your nick → nick label · conversation → chat view.**

---

## Appendix — implementation map (for devs)

| Area | C++ (maxchat-c) | Python (maxchat) |
|------|-----------------|------------------|
| Window title | `updateWindowTitle()` → `setWindowTitle` | `_refresh_title()` |
| Topic bar | `renderActiveBufferMetadata()` → `m_topicLabel` | `_set_topic()` → `self.topic` |
| Status bar | `showConnectionStatus()` / `statusBar()->showMessage()` | `self.statusBar().showMessage()` |
| Nick label | `updateNickLabel()` — called on register, nick change, and buffer/network switch | `self.nick_label` |
| Chat view | `append*ChatLine` / `appendSystemLine*` | `_append*` |
| Server tree | `rebuildNetworkTree()` / `updateNetworkTreeLabels()` | `_rebuild_tree()` |
| Member list | `renderActiveBufferMetadata()` / `recolorMemberList()` | `_refresh_active_members()` |
| Buffer tabs | `syncBufferTabs()` | tab bar |
| Comic panels | `refreshComic()` | `_render_strip()` |
| Toasts / tray | `notify()` / `Notifier` | `notifier` / tray |

## Audit status (2026-06-11)

All areas match Python after the recent fixes (topic bar made topic-only, status
bar stopped mirroring chat, window title now carries network/channel context).
Remaining gap: unread-count prefix in the window title (TODO, cosmetic — the tree
already shows per-buffer activity).
