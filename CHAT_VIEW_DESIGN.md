# Chat View Rendering — Design & Invariants

Internal design doc. Read this before touching anything that puts content into the
chat view (`m_chatView`). It exists because the same class of bug keeps coming
back: content that *looks* right when first shown but **vanishes on the next
buffer switch**.

> **THE ONE RULE**
> The chat view is a **single shared widget** that is **cleared and rebuilt from
> the buffer model on every render**. Therefore: **anything that must survive a
> buffer switch MUST live in the model (`ChatBufferStore`). Never make the view
> the only place a line exists.**

---

## 1. The architecture (why the rule exists)

Unlike the Python client (one persistent `QTextBrowser` per buffer), the C++ port
uses **one** `QTextBrowser* m_chatView` for all channels/queries/server tabs.

Switching buffers (and many incidental events — theme change, font change,
metadata refresh, `/clear`, marker updates) calls:

```
renderActiveBuffer()
  └─ m_chatView->clear();                              // <-- everything painted is GONE
     for (line : m_chatBuffers.snapshot(current).lines) // <-- the model is the source of truth
        render(line);
```

So the view is **derived state**. The buffer model (`ChatBufferStore`) is the
**source of truth**. If you append straight to the view and don't also store it,
the next `renderActiveBuffer()` wipes it. That is the root cause of every
"disappeared after switching channels" report.

---

## 2. The model: `ChatBufferLine`

`src/core/ChatBufferStore.h`. One stored line:

| Field        | Meaning |
|--------------|---------|
| `timestamp`  | Original time of the line. Replay lines carry their real time; live lines leave it null and take the current time at render. |
| `sourceText` | Raw IRC body (e.g. `<nick> hi`). Preferred render path — re-formatted live so theme/colour changes apply. |
| `htmlText`   | Pre-rendered HTML (link-preview cards, embedded images). Rendered as-is. |
| `plainText`  | Fallback plain text. |
| `systemLine` | **Styling flag only** — selects the system-tint palette for the whole line (joins, notices, dividers, and also ordinary PRIVMSGs which use the system tint by default). **This is NOT a semantic "server event" filter.** Regular `<nick> text` chat lines also carry `systemLine = true` because `appendSystemLineToTarget` defaults `systemStyling = true`. ACTION and NOTICE lines explicitly pass `false`. |
| `dimmed`     | Replayed history / "Chat ended" divider — rendered in the dim palette. |

`renderActiveBuffer()` chooses per line: `sourceText` → re-format; else `htmlText`
→ insert as HTML; else `plainText`.

### Timestamps gotcha
`renderActiveBuffer` does **not** trust `chatLineFormatOptions().timestamp` (that's
"now") for stored lines — it sets `options.timestamp` from `line.timestamp` when
valid. Dimmed lines with no timestamp **clear** the gutter (so the "Chat ended"
divider, whose date+time is in its *text*, doesn't pick up a misleading current
time).

### `systemLine` and comic chat
Because `systemLine` is a styling flag, not a semantic event filter, comic chat
(`refreshComic()`) cannot use it alone to exclude server events. It uses format
pattern matching instead:

- `<nick> text` — always a chat message (PRIVMSGs are always in this form).
- `* nick text` **with `systemLine = false`** — a user `/me` ACTION.
- `* text` **with `systemLine = true`** — a server event (join/part/quit/mode/nick
  change). Filtered by the `!line.systemLine` guard on the `* ` branch.
- Everything else — filtered by `else { continue; }`.

Never add a top-level `if (line.systemLine) continue;` in `refreshComic()` — it
would discard all `<nick>` speech (which also carry `systemLine = true`).

---

## 3. Painted vs. stored — know which path you're on

| Helper | Stores to model? | Use for |
|--------|------------------|---------|
| `appendSystemLineToNetworkTarget` / `appendChatLine*` | **Yes** (stores `sourceText`+`htmlText`, then paints if active) | normal live messages/system lines |
| `appendPreviewHtmlToNetworkTarget` | **Yes** (stores `htmlText`) | link-preview cards |
| `appendFormattedChatLine` | **No — paint only** | called *by* `renderActiveBuffer` per stored line; never the sole writer |
| `appendPreviewHtmlLine` / `appendPlainChatLine` | **No — paint only** | same |
| `appendUnreadMarkerLine` | **No — paint only** | the `──── new ────` marker, redrawn each render from the stored boundary (see §5) |

If you call a paint-only helper as the *only* way a line appears, you have
re-introduced the bug.

---

## 4. Case study: chat history "replay" / resume

"Replay" = loading the tail of the on-disk log back into a buffer on first open
(a.k.a. chat history). Logs are `yyyy-MM-dd HH:mm:ss <body>` lines on disk.

**Correct implementation** (`seedReplayForBuffer`, `src/ui/MainWindow.cpp`):
- On a buffer's **first open**, parse the last N log lines and **store** each as a
  `ChatBufferLine{ timestamp, sourceText=body, dimmed=true }`.
- Append a `dimmed`+`systemLine` divider whose **text** is
  `--- Chat ended <ddd MMM d> <time> ---` (date+time in the text, not the gutter).
- Guard with `m_replayedBuffers` (seed once) and only seed an **empty** buffer
  (so history prepends cleanly, never injected between live messages).

Because it's stored, `renderActiveBuffer` reproduces it on every switch.

**Wrong (the original bug):** `replayCurrentLog()` painted the dimmed lines +
divider straight onto the view via `appendFormattedChatLine` and ran once at
connect for one buffer → wiped on the next render, missing from other channels.

---

## 5. Case study: the `──── new ────` unread marker

Marks where you left off reading, per channel (HexChat-style "marker line").

**Correct implementation:**
- `m_bufferMarkerCount[network\x1ftarget]` = line index of the **first message that
  arrived while the buffer was inactive** (the unread boundary). Maintained by
  `noteUnreadBoundary()`, called from the two live-append sites:
  - message arrives **inactive** and no boundary set yet → set boundary at the
    current line count (the new line is the first unread);
  - message arrives **active** (you're reading it live) → clear the boundary.
- It is **not** touched on activate or leave — so it **persists across buffer
  switches** (the user's requirement) and only resets when you actually read new
  activity live.
- `renderActiveBuffer` draws the marker at the replay→live boundary when replay
  exists, or at `markerIndex` when there is no replay. See the `seenDimmed` /
  `replayMarkerInserted` logic below.
- Rendered **centered** via `appendCenteredDivider()`, same as the "Chat ended"
  resume divider.

**Wrong (original bug #1):** marker position came from a transient parameter
computed only at switch time and was painted → vanished on any re-render and only
showed in the just-activated channel.

**Wrong (original bug #2):** an early fix updated the boundary "on leave" (to the
bottom), so tabbing away and back with no new activity wiped the marker. The
boundary must be tied to *unread arrival*, not to focus changes.

**Wrong (original bug #3 — "marker missing on 2nd channel"):** when a buffer had
both replay content (dimmed lines) *and* a new-message boundary, the `──── new ────`
marker was absent. A guard `if (markerIndex < 0)` was on the structural
replay→live boundary check, preventing it from firing when an explicit count-based
marker also existed. The two mechanisms were treated as mutually exclusive when
they should be prioritized: the structural boundary wins whenever replay is present.

**Wrong (original bug #4 — "marker too far down"):** after bug #3's fix (the
`markerIndex < 0` guard was removed), the marker appeared below the live connection
messages (Connecting…, joined, etc.) instead of right after "Chat ended". The
explicit count-based marker (`lineIndex == markerIndex`) fired before the structural
check, at the line index where the background message arrived — which was already
after several live system lines had been added, placing the marker too late.

**The fix (both bugs #3 and #4):**

```cpp
bool seenDimmed = false;
bool replayMarkerInserted = false;
for (const maxchat::core::ChatBufferLine& line : snapshot.lines) {
    // Explicit unread marker — only when no replay content has been seen yet
    // (if replay exists, the structural check below will handle placement instead)
    if (lineIndex++ == markerIndex && !seenDimmed) {
        appendUnreadMarkerLine();
        replayMarkerInserted = true;
    }
    // Structural replay→live boundary: fires at the first live line after dimmed
    if (m_markerLine && !line.dimmed && seenDimmed && !replayMarkerInserted) {
        appendUnreadMarkerLine();
        replayMarkerInserted = true;
    }
    if (line.dimmed) seenDimmed = true;
    // ... render line
```

Key invariant: replay lines (dimmed) are always seeded BEFORE any live lines arrive
(guarded by `m_replayedBuffers` + empty-buffer check in `seedReplayForBuffer`), so
`seenDimmed` is guaranteed true at `markerIndex` whenever replay exists → the
`!seenDimmed` guard on the explicit marker is safe (it will always be false when
replay is present, letting the structural check take over).

---

## 6. Link-preview specific traps

### 6a. Card text alignment — `appendPreviewHtmlLine` block-format pitfall

`appendPreviewHtmlLine` computes a `leftMargin` (pixels wide enough for the chat
prefix) and applies it via `QTextBlockFormat` before calling `cursor.insertHtml`.
**The trap:** `insertHtml` with block-level HTML (`<div>`, `<p>`) creates multiple
`QTextBlock`s. Only the first block inherits the cursor's block format; every
subsequent block gets default formatting (leftMargin = 0), so card text lands at
the left edge of the window instead of aligning with chat text.

**Fix** (2026-06-11, commit `8061fdc`): after `cursor.insertHtml(html)`, iterate
over every block from `insertStart` to `insertEnd` and call `mergeBlockFormat` on
each:

```cpp
const int insertStart = cursor.position();
cursor.insertHtml(html);
const int insertEnd = cursor.position();
if (blockFormat.leftMargin() > 0.0) {
    QTextBlock block = doc->findBlock(insertStart);
    while (block.isValid() && block.position() <= insertEnd) {
        QTextCursor bc(block);
        bc.mergeBlockFormat(blockFormat);
        block = block.next();
    }
}
```

**Rule:** Never assume a block format set before `insertHtml` propagates into
block elements inside the HTML. Apply it to all inserted blocks post-insert.

---

### 6b. Async card routing — which channel does the OG card belong to?

OG card fetches are async. If the user switches channels between the URL being
posted and the fetch completing, `m_currentTarget` at callback time is the *new*
channel — the card appears in the wrong buffer.

**Fix** (2026-06-11, commit `8061fdc`): `LinkPreviewCandidate` carries
`originNetwork` / `originTarget` (the channel where the URL was typed).
`queueLinkPreviewsFromLine` stamps them at queue time; `handlePreviewCardFetched`
routes to those fields instead of `m_currentTarget`.

**Rule:** Any async work that appends to a specific buffer must stamp the target
buffer at *dispatch time*, not at *completion time*.

---

## 7. Checklist — adding ANY new chat-view element

1. Does it need to survive a buffer switch? If yes → **store it in
   `ChatBufferStore`** (add a `ChatBufferLine`, or a per-buffer field +
   reproduce it in `renderActiveBuffer`). Painting alone is not enough.
2. Is it per-buffer state (a boundary, a flag)? Key it by
   `network + QChar(0x1f) + target`, not by raw target (networks collide).
3. Does it show a time? Decide per-line vs. current; set `options.timestamp`
   explicitly in `renderActiveBuffer` — don't let it default to "now".
4. Add a regression test in `tests/unit/main_window_link_preview_test.cpp` that
   switches away and back and asserts the element is still there. Set
   `window.m_replayLogEnabled = false` unless you're testing replay (otherwise
   on-disk logs make the test environment-dependent).

---

## 8. Related trap (not the view model, but same area)

`m_input` is a `QTextEdit` — a `QAbstractScrollArea`. **Mouse/context-menu events
go to its `viewport()`, not the widget.** Keyboard events go to the widget.
Install event filters on **both** if you need mouse events (this is why
right-click spell suggestions were silently broken). See DEV_NOTES.md.

---

See also: `UI_SURFACES.md` (what each window surface is responsible for),
`DEV_NOTES.md` ("THINGS I GOT WRONG").
