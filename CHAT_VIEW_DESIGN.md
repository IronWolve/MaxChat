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
| `systemLine` | Whole-line system tint (joins, notices, dividers). |
| `dimmed`     | Replayed history / "Chat ended" divider — rendered in the dim palette. |

`renderActiveBuffer()` chooses per line: `sourceText` → re-format; else `htmlText`
→ insert as HTML; else `plainText`.

### Timestamps gotcha
`renderActiveBuffer` does **not** trust `chatLineFormatOptions().timestamp` (that's
"now") for stored lines — it sets `options.timestamp` from `line.timestamp` when
valid. Dimmed lines with no timestamp **clear** the gutter (so the "Chat ended"
divider, whose date+time is in its *text*, doesn't pick up a misleading current
time).

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
- `renderActiveBuffer` draws the marker **before line index `markerCount`** when
  `0 < markerCount < lines.size()` — computed every render, so it survives
  re-renders and shows independently in **every** channel with unread activity.
- Rendered **centered** via `appendCenteredDivider()`, same as the "Chat ended"
  resume rule (both dividers are centered dim blocks, not left chat lines).

**Wrong (original bug #1):** marker position came from a transient parameter
computed only at switch time and was painted → vanished on any re-render and only
showed in the just-activated channel.

**Wrong (original bug #2):** an early fix updated the boundary "on leave" (to the
bottom), so tabbing away and back with no new activity wiped the marker. The
boundary must be tied to *unread arrival*, not to focus changes.

---

## 6. Checklist — adding ANY new chat-view element

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

## 7. Related trap (not the view model, but same area)

`m_input` is a `QTextEdit` — a `QAbstractScrollArea`. **Mouse/context-menu events
go to its `viewport()`, not the widget.** Keyboard events go to the widget.
Install event filters on **both** if you need mouse events (this is why
right-click spell suggestions were silently broken). See DEV_NOTES.md.

---

See also: `UI_SURFACES.md` (what each window surface is responsible for),
`DEV_NOTES.md` ("THINGS I GOT WRONG"), `AUDIT.md` #29.
