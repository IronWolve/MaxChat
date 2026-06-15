# Render-Pipeline Refactor — Design

Status: **DESIGN ONLY** (no code yet). Companion to [MAINWINDOW.MD](MAINWINDOW.MD).
Written 2026-06-15 after Phases 0/1/2a of the MainWindow decomposition landed.

This document explains **why** the remaining MainWindow decomposition phases
(Appearance, Notification, Comic, link-preview) stalled, and **designs the one
refactor that unblocks them**: extracting the chat-render pipeline into its own
component. It is the prerequisite the other controllers were silently waiting on.

---

## 1. Why this doc exists (the blocker)

The MainWindow decomposition (MAINWINDOW.MD) used a strangler/extract-class
approach. Two phases extracted cleanly:

- **ScriptBridge** (Phase 1) — scripting was a self-contained subsystem behind a
  narrow `ScriptHost` seam. Lifted out whole; testable in isolation.
- **MediaController** (Phase 2a) — inline media + image upload are event-driven
  (a click, a drop) and touch nothing during rendering. Lifted out cleanly.

Then the pattern broke. Phases 3–7 (Appearance, Notification, Comic, the
link-preview half of Phase 2, the IRC router) all turned out to be **welded to a
single shared mechanism: the chat-render pipeline** — the code that turns the
per-buffer line model into pixels in the chat `QTextBrowser`. They cannot become
clean controllers because the thing they share is not itself a component yet; it
is a tangle of ~20 `MainWindow` methods and ~15 members.

**Evidence (all in `src/ui/MainWindow.cpp`):**

| Would-be controller | What welds it to the render path |
|---|---|
| **Appearance** | `chatLineFormatOptions()` reads BOTH the chat theme and the app theme plus ~10 render config members; every theme change calls `renderActiveBuffer()` to repaint. `m_currentTheme`/`m_currentChatTheme` are read in ~8 render/paint sites. |
| **Preview** (Phase 2b) | `appendPreviewHtmlLine()` writes straight into the chat `QTextDocument` mid-render; `registerCachedImagesIn()`/`requestPreviewImagesIn()` run *during* `appendPreviewHtmlLine`. `handlePreviewImageFetched()` calls `renderActiveBuffer()` to swap a broken `<img>` for the decoded image while preserving scroll. |
| **Comic** | `renderActiveBuffer()` ends with `refreshComic()`; comic view is a per-buffer alternate rendering of the same line model. |
| **Notification** | `notify()` reads the app theme accent to colour the toast, and the toast/tray click handler reaches back into buffer activation. |

The render path is the **hub**; the controllers are spokes. Extract a spoke and
it drags the hub along as a wide host interface — you get a class that is ~half
callbacks into `MainWindow`, touching the most regression-prone code in the app
(theming has already produced the chrome-font-revert and themes-off bugs). Net:
more risk, little structural gain. That is the wrong trade, so those phases were
**deliberately deferred** (MAINWINDOW.MD §9, "Separability conclusion").

**The fix is to make the hub a component first.** Once the chat-render pipeline
is its own object with a clean interface, each deferred controller becomes a
genuine extraction again, because the thing they all touch is now a collaborator
they call, not a tangle they live inside.

---

## 2. Reasoning / principle

> **Extract by separability, not by feature grouping.** A subsystem is ready to
> be a controller when the rest of the window talks to it through a *narrow* seam.
> If extracting X forces a wide "do this to the chat view / re-render / read the
> theme" host interface, then the chat view + render + theme is the real unit,
> and it must be extracted first.

This is the same lesson Phase 1/2a taught, applied one level up: the natural next
unit is not "Appearance" or "Preview" — it is **the chat pane itself**.

Two further principles guide the design:

1. **The line model is already clean.** `core::ChatBufferStore` (the per-network/
   per-target line model) and `core::ChatLineFormatter` (`formatChatLine`) are
   already separate, headless, unit-tested core libraries. The mess is purely in
   the *widget-side glue* in `MainWindow`. So this is a UI-layer refactor; the
   model layer needs no changes.
2. **Behaviour-preserving.** This refactor moves code; it must not change a single
   pixel. Every render quirk (UTC→local timestamps, dim-replay palette, the
   `──── new ────` marker placement, scroll preservation on image landing,
   themes-off empty stylesheet) is load-bearing and tested. The success bar is
   "identical output, smaller MainWindow."

---

## 3. Target architecture

Introduce one widget component, **`ChatPane`**, that owns the chat `QTextBrowser`
and everything that renders the active buffer into it. `MainWindow` keeps the
buffer model (`m_chatBuffers`) as the source of truth and tells `ChatPane` what to
show; `ChatPane` tells `MainWindow` about user gestures (anchor clicked, separator
dragged) through a small delegate.

```
                 owns model + windowing
            ┌───────────────────────────────┐
            │           MainWindow           │
            │  m_chatBuffers (ChatBufferStore)│
            │  network tree / tabs / members  │
            └───────────────┬────────────────┘
                            │ setActiveBuffer(snapshot, opts)
                            │ applyChatTheme(ChatRenderTheme)
                            │ appendLiveLine(...) / appendPreviewHtml(...)
                            ▼
            ┌───────────────────────────────┐
            │            ChatPane            │  ← NEW widget component
            │  owns: QTextBrowser (chat view)│
            │        ChatRenderTheme (colors,│
            │           fonts, toggles)      │
            │        preview image cache     │
            │        comic view swap         │
            │  does: buffer snapshot → view  │
            │        live line append        │
            │        preview placeholder swap│
            │        scroll preservation     │
            │        unread / replay markers │
            └───────────────┬────────────────┘
                            │ ChatPaneDelegate (callbacks up)
                            │  anchorClicked(url)  → MediaController
                            │  separatorMoved(x)   → save setting
                            │  previewImageNeeded(url) → fetch (preview ctrl)
                            ▼
                    MainWindow / sibling controllers
```

### 3.1 What `ChatPane` owns (moves out of MainWindow)

- The chat `QTextBrowser` widget (`m_chatView`) and the custom `ChatTextView`
  subclass (separator guide, strip-colours-on-copy).
- `renderActiveBuffer()`, `renderActiveBufferMetadata()` (topic/elide split out).
- The line-append helpers: `appendFormattedChatLine`, `appendPlainChatLine`,
  `appendPreviewHtmlLine`, `appendCenteredDivider`, `appendUnreadMarkerLine`.
- `chatLineFormatOptions()` → fed a `ChatRenderTheme` instead of reading the live
  theme members directly.
- The preview image cache + register/request (`m_previewImageCache`,
  `registerCachedImagesIn`, `requestPreviewImagesIn`, `handlePreviewImageFetched/
  Failed`, `activeBufferReferencesImage`) — the render-bound half of Phase 2b.
- The comic swap (`refreshComic` + the `ComicView` instance), as a `ChatPane`
  sub-mode (chat vs comic is a rendering choice for the same line model).
- Scroll-preservation logic (currently inline in `handlePreviewImageFetched`).

### 3.2 What stays in MainWindow

- `m_chatBuffers` (the model) and all buffer routing/unread bookkeeping.
- Network tree, buffer tabs, member list, input box, menus, status/topic bars.
- Theme *decisions* (which theme is selected, persisting it) — MainWindow still
  owns the selection; it hands `ChatPane` a resolved `ChatRenderTheme`.

### 3.3 The two seams

**Down (MainWindow → ChatPane)** — a concrete API, not a virtual host:

```cpp
class ChatPane : public QWidget {
  public:
    void setRenderTheme(const ChatRenderTheme&);   // colors+fonts+toggles, resolved
    void showBuffer(const core::ChatBufferSnapshot&, const BufferRenderOptions&);
    void appendLive(const core::ChatBufferLine&);   // one new line, no full re-render
    void appendPreviewHtml(const QString& html);    // OG card / inline media
    void onPreviewImageReady(const QUrl&, const QImage&); // swap placeholder, keep scroll
    void setMode(RenderMode);                        // Chat | Comic
};
```

**Up (ChatPane → owner)** — a narrow delegate (1 interface, ~4 methods):

```cpp
class ChatPaneDelegate {
  public:
    virtual void chatAnchorClicked(const QUrl&) = 0;       // → MediaController
    virtual void chatSeparatorMoved(int pixelX) = 0;       // → persist setting
    virtual void previewImageNeeded(const QUrl&) = 0;      // → preview fetcher
    virtual void chatScrollSettled() = 0;                   // → mark-read, optional
};
```

`ChatRenderTheme` is a plain value struct (resolved colours, fonts, the
`m_show*`/`m_align*`/`m_coloredNicks`/nick-palette toggles) — it is what
`chatLineFormatOptions()` builds today, lifted into a named type. MainWindow
builds it from the selected app+chat theme and passes it down. **This is the
hinge that frees Appearance:** the controller computes a `ChatRenderTheme` and
hands it to `ChatPane.setRenderTheme()`; it no longer needs to reach into render
internals.

---

## 4. How the deferred phases collapse out of this

Once `ChatPane` exists, the previously-welded phases become small:

- **Phase 2b — Preview fetch/cache.** The cache + fetch orchestration moves into
  `ChatPane` (it already owns the document the images land in). The OG/candidate
  *fetching* (`m_openGraphFetcher`, `m_pendingPreviewCandidates`,
  `queueLinkPreviewsFromLine`) becomes a small `PreviewFetcher` collaborator that
  emits "here's the card HTML for network/target" → `appendPreviewHtml`. Clean.
- **Phase 3 — Appearance.** `AppearanceController` owns theme/chat-theme/wallpaper/
  opacity selection + persistence + the menu action-group sync, and produces a
  `ChatRenderTheme` + the app-wide QSS. It calls `ChatPane.setRenderTheme()` and
  `qApp->setStyleSheet()`. No render-internal reach. The themes-off and
  chrome-font rules live in one place and stay testable.
- **Phase 5 — Comic.** Becomes `ChatPane`'s `RenderMode::Comic` plus a
  `ComicController` for the art pipeline/dialogs. The per-buffer enable lookup is
  a delegate query.
- **Phase 4 — Notification.** Mostly independent once it stops borrowing render
  internals for the toast accent: it takes an accent colour in its `notify()`
  arguments (caller already knows the theme), and the click-to-activate path goes
  through the existing buffer-activation API. Extractable without `ChatPane`, but
  cleaner alongside it.
- **Phase 7 — IRC router.** Still the largest, separate effort; not part of this
  refactor, but it benefits because message-arrival no longer reaches into render
  internals — it appends to the model and calls `ChatPane.appendLive()`.

---

## 5. Migration plan (phased, each step green)

Behaviour-preserving, one reviewable commit per step, full `ctest` after each.

1. **R0 — Extract `ChatRenderTheme` value type. ✅ DONE.** `src/ui/ChatRenderTheme.{h,cpp}`
   holds the resolved colour struct + a pure `resolveChatRenderTheme(...)`; the
   theme→colour logic (dark-chat luminance, `"follow"` resolution, fg fallback,
   event-colour override) moved out of `chatLineFormatOptions()` verbatim. New
   headless `chat_render_theme_test` (10 cases). Behaviour-preserving; 56/56 green.
2. **R1 — Create `ChatPane` owning the chat view + append helpers.** Move
   `m_chatView`, `ChatTextView`, and the `append*ChatLine` family into `ChatPane`.
   MainWindow delegates through the new API. Anchor-click + separator signals route
   through `ChatPaneDelegate` (MainWindow implements it, forwards to
   `MediaController`/settings as today).
3. **R2 — Move `renderActiveBuffer()` into `ChatPane.showBuffer()`.** MainWindow
   passes the snapshot + options; the unread/replay/dim logic moves verbatim.
4. **R3 — Move the preview image cache + placeholder swap into `ChatPane`.** The
   fetch orchestration stays out (becomes `PreviewFetcher` in a later step);
   `ChatPane` exposes `onPreviewImageReady`/`appendPreviewHtml`.
5. **R4 — Move the comic swap in as `RenderMode`.** `refreshComic` + `ComicView`
   become `ChatPane` internals.
6. **R5 — Add `chat_pane_test`.** Drive `ChatPane` headless: render a known
   snapshot and assert the document (timestamps local-from-UTC, dim replay, marker
   placement, themes-off empty QSS path). This is the regression net the deferred
   theming work needs.
7. **Then** resume MAINWINDOW.MD: Appearance, Preview-fetch, Comic, Notification —
   now each a small, clean extraction against `ChatPane`'s API.

---

## 6. Risks & test strategy

- **Render parity is the whole game.** The chat pane has many load-bearing quirks
  (see §2). Mitigations: (a) move code verbatim per step, never "improve" while
  moving; (b) land `chat_pane_test` (R5) capturing the document output for a fixed
  snapshot before the dependent phases touch theming; (c) keep the existing
  `main_window_link_preview_test` passing at every step — it exercises the
  themes-off stylesheet rule that already regressed once.
- **Scroll preservation** is currently inline and subtle (at-bottom detection,
  clamp to max). Move it as one unit into `ChatPane` and assert it in R5.
- **Comic mode** shares the line model but a different widget; keep the swap logic
  intact and verify per-buffer comic enable still toggles.
- **No model changes.** `ChatBufferStore`/`ChatLineFormatter` stay untouched, so
  their existing tests remain valid throughout.

---

## 7. Sequencing vs release

This is a **post-v1 refactor**, not a release blocker. The app ships fine as-is;
MainWindow being large is a maintainability cost, not a user-facing defect. The
`MainWindowHost` seam from Phase 0 is already in place and does not need to change
for this work. Recommended order: ship v1 → do R0–R5 → finish the dependent
controller phases. Doing it before release would mean reworking the most
regression-prone code right before shipping, for no user benefit.

---

## 8. Summary

The remaining decomposition phases are blocked on a shared dependency — the
chat-render pipeline — that is not yet a component. Extracting controllers around
it produces glue, not structure. The unlock is to make the chat pane itself a
component (`ChatPane`) with a `ChatRenderTheme` value-in / `ChatPaneDelegate`
events-out seam, then let Appearance/Preview/Comic/Notification fall out as the
small extractions they were always meant to be. Plan: R0–R5 above, behaviour-
preserving, each step green, `chat_pane_test` as the safety net, after v1.
