# MaxChat C++ Port — Dev Notes

Internal notes for this port. Not shipped.

## ARCHITECTURE — chat view is a single shared, model-rebuilt widget

Unlike the Python app (one persistent QTextBrowser **per buffer**, in a
QStackedWidget), this port uses ONE shared `m_chatView` that is **cleared and
rebuilt from `ChatBufferStore` on every buffer switch** (`renderActiveBuffer`).
Consequence: anything painted directly onto the view that isn't in the stored
line-model gets wiped on the next switch. Anything that must persist has to live
in the model (or be re-applied inside `renderActiveBuffer`):
- Inline preview **images** are re-registered as document resources on each
  render (cached by URL) for exactly this reason — see `registerCachedImagesIn`.
- Log-**resume replay** (dimmed lines + "Chat ended" divider) is stored in
  `ChatBufferStore` via `seedReplayForBuffer` (called on first buffer open,
  guarded by `m_replayedBuffers`). Fixed in AUDIT backlog #29.
- The `──── new ────` marker is paint-only, BUT the *boundary* it renders from
  (`m_bufferMarkerCount`) is durable model state — so the marker is correctly
  reconstructed on every render. See CHAT_VIEW_DESIGN.md §5.
When adding anything that should survive a buffer switch, store it in the model.

## UNVERIFIED CODE (needs target-OS build)

- ~~WindowsSpeller~~ **RESOLVED 2026-06-11**: `src/spell/WindowsSpeller.cpp`
  compiles clean under MinGW and ships in the default Windows build
  (`MAXCHAT_OS_SPELL=ON` is now build.bat's default; "noosspell" skips).
  Select via Preferences ▸ Spelling ▸ "Operating system". Runtime smoke test
  on Windows still pending (falls back to internal engine with a notice on
  failure). macOS backend intentionally SKIPPED per user decision 2026-06-11.

## THINGS I GOT WRONG

- **2026-06-12 — Spinbox up/down arrows did nothing (Comic Settings, and every
  spinbox app-wide).** The theme QSS put a `border` on `QSpinBox`, which flips
  the widget to Qt's stylesheet style (`QStyleSheetStyle`). Once that happens,
  the up/down sub-buttons need EXPLICIT geometry in the QSS or their click
  rects collapse — the arrows still paint but clicking does nothing. Fix: add
  `QSpinBox::up-button`/`down-button` rules (subcontrol-position + width +
  border) and CSS-triangle arrows. Verified via `subControlRect(SC_SpinBoxUp)`
  → non-empty (19x15) after the fix. **Lesson: styling ANY complex Qt widget
  (QSpinBox/QComboBox/QScrollBar) via QSS means you must also style its
  sub-controls, or interaction silently breaks.**


- **2026-06-12 — "Yell garbled the avatar": a non-face cell misclassified as a
  face.** MS Comic Chat .avb files carry a large square self/preview cell
  (e.g. 262x332) alongside the real head cells (~166x190). The art decoder
  classifies cells by aspect ratio (h > w*1.4 → body), so the near-square big
  cell slipped through as a FACE. The emotion→face stretch maps "shouting"
  (the last of 9 emotions) onto the LAST face id — which was that junk cell —
  compositing a whole body where the head goes. Only shouting hit it; emotions
  0–7 mapped to real faces, which is why it looked emotion-specific.
  Fix: `dropOutlierFaces` removes face cells whose area > 1.8× the median face
  (a head is much smaller than a body/self cell). **Lesson: classifying art
  cells by a single heuristic (aspect) isn't enough — validate against the
  cohort (a face is head-sized relative to its siblings), and when a mapping
  reaches the extreme index, the extreme cell is the most likely to be junk.**


- **2026-06-12 — DCC review: every trust decision must name WHO it trusts.**
  The recurring hole across 6 of the 15 findings: an identifier (token, port
  number, filename, TCP connection) was treated as authorization without
  checking the principal behind it. The passive token authorized ANY sender;
  RESUME authorized any nick that knew the port; listening sockets trusted
  whoever connected first; an existing same-name file authorized appending a
  stranger's bytes ("resume"). And CHAT offers skipped authorization entirely
  (auto-connect = IP disclosure to any CTCP). **Rule: every DCC message and
  connection must be checked against the OFFER's nick/host/state, and every
  numeric field range-checked — toUInt() without an ok-flag turned "65536"
  into port 0 and misclassified the offer.** Also: notifications repeated two
  old classes — QLabel AutoText rendering remote HTML (toasts, after fixing
  the same in the audio bar days earlier: grep ALL QLabels fed remote text),
  and substring highlight matching ("art" fires on "start").


- **2026-06-12 — Media/input/startup sweep: 20+ findings, the recurring themes:**
  1. **Every fetch path must use the shared hardened fetcher.** ImageViewerDialog
     rolled its own QNAM fetch — no DNS SSRF gate, no redirect re-vetting, no
     timeout, and the 25 MB "cap" only truncated AFTER QNAM buffered the whole
     body. Now consumes services::ImageFetcher (which also gained an in-flight
     downloadProgress abort). Audio/video URLs likewise gated through
     resolvePreviewUrlPublicAsync before reaching QMediaPlayer. Same lesson as
     api.http_get on 06-11: new fetch path = same treatment, no exceptions.
  2. **Insert-over-selection destroys text.** Ctrl+B/I/U/K replaced the
     selection with the control code (selected word deleted). Wrap, don't
     insert. Likewise Down-arrow cleared an in-progress draft.
  3. **AltGr arrives as Ctrl+Alt** — the redirect filter's modifier guard ate
     international characters typed after clicking chat. Printable + Ctrl+Alt
     = AltGr, must fall through.
  4. **One-line input + multiline submit = invisible messages.** Pastes/Shift+
     Enter produced lines the user could never see. The box now grows to 5
     lines (blockCountChanged → resizeMessageInput).
  5. **Startup ordering**: static window icon overwrote the themed one; saved
     splitter sizes applied before geometry restore (drift every run); a
     synchronous 3 s D-Bus probe could stall first paint; scripts ran their
     top-level before the window painted. And settings.json was parsed 11
     times before first paint — SettingsStore now has a stat-validated cache
     (still detects the Python app's external writes via mtime+size).
  Full findings list in the 2026-06-12 HANDOFF slice.


- **2026-06-12 — Deep chat-text review found 8 silent rendering bugs + the real
  cause of the "lost" nick-color setting.** Highlights (commit-day fixes):
  1. **Timestamps shifted to UTC on every re-render** — stored UTC, formatted
     without toLocalTime(); live lines showed local, any theme change shifted
     the whole gutter by the UTC offset. *Format stored UTC times in local.*
  2. **"Dim replay" never dimmed the body** — defaultForeground only feeds
     reverse-video; the message text rendered full-bright. New bodyColor option.
  3. **mIRC codes inside `<nick>` rendered as garbage AND broke the colour
     hash** (chat hashed "\x0304nick", member list hashed "nick" → different
     colours — one source of the recurring mismatch reports). Strip before use.
  4. **Reverse-video used hardcoded dark-theme colours** on every theme.
  5. **No contrast guard** — yellow/cyan nicks invisible on light themes.
  6. **Copied text contained U+00A0** padding (breaks paste into terminals).
  7. **Separator guide assumed monospace** (space-width × columns) — now
     measured in pixels from the real prefix.
  8. **Preferences OK clobbered concurrent settings writes** — the dialog
     saved its open-time snapshot wholesale, reverting window geometry (saved
     on `finished`, which fires BEFORE exec() returns!), Comic Settings changes
     made from inside Preferences, and IRC-driven keys. Now only CHANGED keys
     merge over a fresh load (`PreferencesDialog::changedSettings`).
  **Root cause of the user's lost "Classic IRC colors" choice: the C++ port
  and the Python MaxChat share `<config>/maxchat/settings.json`; Python
  doesn't know `nick_color_mode`, and saves from a stale in-memory dict drop
  it — then every C++ defaults-expanded save rematerialises `palette`.
  Mitigated by diff-saving; the real fix is a port-specific settings file
  (decision pending with user).** Meta-lesson: a settings file shared between
  two writers needs last-writer-wins discipline NOBODY has — never save a
  defaults-expanded snapshot, only diffs.


- **2026-06-11 — Replayed log lines lose their metadata flags, and every
  consumer of the buffer must cope.** Live join/part/quit lines carry
  `systemLine = true`, so the comic collector's `!line.systemLine` check
  excluded them — but log REPLAY re-stores those lines as bare text, flag
  gone, and they re-entered the comic as fake `/me` actions ("* iw_chat joined
  #trump" panels). Fix: the comic collector also filters action lines by event
  verb (joined/left/quit/kicked/known as/mode/topic). **Lesson: any flag that
  exists only in memory is LOST on the disk round-trip — code that filters by
  such flags needs a content-level fallback for replayed lines.**

- **2026-06-11 — Centered dividers ignored the aligned-nick layout.**
  `appendCenteredDivider` centered "Chat ended …" / "──── new ────" across the
  FULL viewport width, so with align-nicks on the divider crossed the
  nick/message separator bar. Fix: when `m_alignNicks` is set, give the divider
  the same leftMargin as the message column so it centres within the text
  area. **Lesson: "centered" elements must centre within the content column
  the user reads, not the raw widget width.**


- **2026-06-11 — Comic balloon tails: per-case ad-hoc geometry instead of one
  rule set.** The original `drawTail` had three separate geometries (tail-to-
  head, stacked-balloon stub, side-exit), each with its own arbitrary numbers:
  base width scaled with the BALLOON (narrow balloon → needle tail, wide →
  slab), tips used magic lean fractions (0.55 / 0.82) instead of aiming at the
  speaker, and lengths differed wildly between cases. User-visible result:
  "no real logic to length, angle, size… some tails thin, some short."
  Fix (commits b4a84fa + 247736a): ONE geometry — panel-relative base width,
  tip along the actual base→head vector with clamped length (5–16% of panel)
  and lean (~50° from vertical), quadratic-curved edges in the single
  `drawTri` helper. **Lesson: when the same visual element is drawn from
  several code paths, centralise the geometry and derive everything from a
  few panel-relative constants — per-case magic numbers WILL drift apart and
  look inconsistent. Full rules in COMIC_MODE_DESIGN.md §6.**

- **2026-06-11 — Two of nine comic emotions were unreachable and nobody
  noticed.** `comicEmotionFor` never returned "angry" or "bored" — the art,
  the picker, and the renderer all supported them, but auto mode could not
  produce them, for months. **Lesson: when a classifier feeds a fixed enum,
  test that EVERY enum value is producible** (now guarded by
  `comic_emotion_test`).

- **2026-06-11 — Quick audit caught 6 issues in code that "looked done" (all
  fixed same day). The recurring patterns, so we don't get caught again:**
  1. **A permission name must mean what it says.** "Load modules" opened the
     full Lua `package` library — `package.loadlib` + require's C searchers =
     load any DLL = arbitrary native code, silently bypassing the separate
     "Run programs" permission. Granting capability X must not smuggle in
     capability Y. Fix: strip `loadlib` + C searchers (slots 3/4), force
     text-only `load`/`loadfile`/`dofile` (bytecode is unverified).
  2. **Every NEW network fetch path needs the SSRF/size treatment, not just
     the first one.** The OG-card fetcher had the private-IP gate + redirect
     re-check + byte cap; `api.http_get` (added later) had none of it — scripts
     could hit localhost/169.254.169.254 and buffer unbounded bodies. When
     adding a fetch path, reuse `resolvePreviewUrlPublicAsync` + a read cap;
     check redirects too (`NoLessSafeRedirectPolicy` only stops downgrades).
  3. **File ops that feed decisions must check their own success.**
     `seedBundledScripts` ignored `QFile::copy` failures (locked/read-only dest
     → record updated but dest not → file misclassified as "user edit" forever,
     the upgrade mechanism bricks itself), and its `readAll` returned `""` for
     both "empty" and "unreadable" (two locked files compare equal → a real
     user edit could be clobbered as "unmodified"). Never compare or record
     based on a read/copy you didn't verify.
  4. **Round-tripping args through split-then-join loses quoting.**
     `QProcess::splitCommand` strips quotes; re-joining with spaces broke any
     `api.launch` argument containing a space on the ShellExecuteW path.
     If you split, re-quote when you rebuild.
  5. **"A replaces B" rules must be enforced at BOTH toggles.** Buttons-as-tabs
     hides the server tree, but View ▸ Server List could force the tree back on
     while tabs were active (a later setVisible fix made the hidden state
     overridable). When two controls share state, every entry point must check
     the invariant, not just the one that established it.
  Process lesson: run a quick adversarial review pass after each feature batch
  — all six were found by a 3-minute audit, not by usage.

- **2026-06-11 — Comic Mode toggle semantics: a "global on + per-buffer hide-set"
  design is the wrong model for a per-channel feature.** The original toggle
  turned comic on for EVERY buffer on first enable, then later toggles maintained
  a `m_comicHiddenBuffers` hide-set — so the user had it "on everywhere or off
  everywhere" and had to hide channel-by-channel. Worse, toggling it off while on
  the **server buffer** was overloaded as the secret "global kill" — invisible,
  undiscoverable behavior. Redesign (commit e142ebb): invert the set —
  `m_comicEnabledBuffers` is **opt-in per buffer** (key = network\x1ftarget),
  `m_comicMode` is *derived* (backend on iff the set is non-empty), and the
  action is `setEnabled(false)` on the server buffer instead of overloading it.
  The tricky parts to not re-break:
  - The QAction is shared between the menu and the toolbar button. Its
    **checked** state must be re-synced (under `QSignalBlocker`, or `toggled`
    re-fires `setComicMode`) on every buffer switch to show the ACTIVE buffer's
    state, and its **enabled** state must be re-synced in the same place
    (`activateBufferTarget`) — one without the other leaves a stale button.
  - The action must start **disabled at creation**: the startup buffer is the
    server tab, and the buffer-switch sync that would disable it may sit behind
    a `m_comicView != nullptr` guard or simply not have run yet. (Hidden default
    breaks Ctrl+M too — keep a server-buffer guard inside `setComicMode` itself.)
  - `refreshComic` must check the opt-in set, not just the backend flag —
    it renders the *active* buffer, which may not have opted in.
  General rule: per-channel state = per-channel **opt-in keyed by
  network+target**, never "global flag + exception set"; and a control that's
  meaningless in a context gets **disabled** there, never repurposed.

- **2026-06-11 — Shipped a script fix that never reached the user's machine
  (the "6-second `!run calc.exe`" report).** `run.lua` was rewritten to use the
  non-blocking `api.launch` (ShellExecuteW backend, commits 593a872 + 41b48e4),
  the C++ was rebuilt and synced — and `!run` was still slow, because the app
  loads scripts from the user's config dir
  (`%LOCALAPPDATA%\maxchat\scripts\`), and `seedBundledScripts` only copied a
  bundled script **if the file didn't already exist** ("never overwrite user
  edits"). The deployed `run.lua` was still the original `os.execute('start "" …')`
  version: `system()` blocks the Qt GUI thread for the whole cmd.exe spawn chain
  (~6 s on that machine). Diagnosis trick: the deployed file was byte-identical
  to commit 797399f's version, proving it was an unmodified stale seed, not a
  user edit. Fix: `seedBundledScripts` now keeps a `.bundled/` snapshot of each
  script as last seeded; on startup, a deployed script that still matches its
  snapshot (user never edited it) is upgraded when the bundled version changes.
  Scripts that differ from both bundled and snapshot are never touched.
  **Lesson: a fix to a *seeded* asset isn't shipped until the seeding mechanism
  can deliver updates — verify the fix on the deployed copy, not the repo copy.**

- **2026-06-11 — Input focus grab never ported from Python.** The Python original
  installs `app.installEventFilter(self)` (a GLOBAL filter) so keystrokes typed
  anywhere jump to the message box. The C++ port only had per-widget filters on
  `m_input` and its viewport — clicking the chat view and typing did nothing.
  Fix: `qApp->installEventFilter(this)` + `redirectKeyToInput` in eventFilter.
  Six guards are required (menu/modal, activeWindow, text-entry widget,
  interactive-nav widget, Ctrl/Alt/Meta, non-printable). Removing ANY one breaks
  something. Full design in `INPUT_FOCUS_DESIGN.md`.

- **2026-06-11 — `refreshScriptList` inside `scriptPermissionChanged` destroyed
  checkbox state.** After changing a permission checkbox, the handler called
  `dialog.refreshScriptList()` to update the loaded/unloaded status column. That
  clears and rebuilds the QListWidget → `currentItemChanged(nullptr)` fires →
  all permission checkboxes are disabled. Result: clicking a second checkbox
  had no effect. Fix: don't call `refreshScriptList` from the permission handler
  (permission changes don't affect loaded status; only reload the script).

- **2026-06-11 — `SettingsStore::saveRaw` silently stomped window geometry.**
  `attachGeometryPersist` saves via `store.setValue(key, geom)` when
  `QDialog::finished` fires (before `exec()` returns). Any caller that then did
  `m_settings.saveRaw(dialog.settings())` replaced the entire file, losing the
  `geom_*` key just written. Fix: `saveRaw` now reads the existing file first and
  preserves any `geom_*` keys not present in the incoming map. See
  `INPUT_FOCUS_DESIGN.md` for the complete dialog geometry inventory.

- **2026-06-11 — `api.data_dir()` returned a path but didn't create the
  directory.** `memo.lua` called `api.data_dir()` to construct a file path then
  passed it to `io.open(path, "w")`. If the directory didn't exist, `io.open`
  returned `nil` silently; `save_memos` bailed on the nil check; the user saw
  "Memo saved for nick." but nothing was written. Fix: `l_data_dir` in
  `LuaEngine.cpp` now calls `QDir().mkpath(dir)` before returning the path.

- **2026-06-11 — `os.execute` blocks Qt's main thread.** `run.lua` used
  `os.execute('start "" prog')` which calls the C `system()` function → blocks
  the event loop until cmd.exe exits (≈200–500ms on Windows → visible UI freeze).
  Fix: added `api.launch(cmdline)` backed by `QProcess::startDetached` (non-
  blocking, returns immediately after spawning). `os.execute` stays in the sandbox
  for scripts that genuinely need it; `api.launch` is the right tool for "open
  something detached."

- **2026-06-11 — OG card text rendered at the left edge instead of aligned with
  chat text.** `appendPreviewHtmlLine` computed a `leftMargin` from the chat
  prefix width and applied it via `QTextBlockFormat` before calling
  `cursor.insertHtml(html)`. The format set on the initial block was correct, but
  `insertHtml` with block-level HTML elements (`<div>`) creates additional
  `QTextBlock`s; those new blocks get default formatting (leftMargin=0) regardless

- **2026-06-11 — `QMenu::Hide` focus restore fired for every context menu, not
  just menu-bar dropdowns.** The `eventFilter` handler hooked `QEvent::Hide` on
  any `QMenu` and deferred `m_input->setFocus()`. Right-clicking the network tree,
  member list, or tab bar → close menu → focus stolen from the tree. Fix: guard
  with `qobject_cast<QMenuBar*>(menu->parentWidget()) != nullptr` (menu-bar
  menus have the QMenuBar as their direct parent; context menus don't). Also added
  `QApplication::activePopupWidget() != nullptr` guard in the deferred lambda so a
  sub-menu closing while the parent menu is still open doesn't trigger the restore.

- **2026-06-11 — `setComicMode` could never turn off the global comic backend.**
  First-enable set `m_comicMode = true`; the per-channel branch toggled only the
  view (show/hide for this channel). Nothing ever set `m_comicMode = false`. Once
  on, the backend ran for the whole session. Fix: pressing Ctrl+M on the server
  buffer (where per-channel toggle makes no sense) now acts as the global off —
  `m_comicMode = false`, hidden-set cleared, view hidden. Channel-buffer Ctrl+M
  still means per-channel toggle. This gives a natural "global on → per-channel
  overrides → return to server buffer to globally off" flow.

- **2026-06-11 — Tab "Close" context menu closed the wrong tab after IRC events.**
  The lambda captured `int index` at menu-open time. `QMenu::exec()` spins a
  nested event loop, so a JOIN or PART during the open triggered `syncBufferTabs`,
  which rebuilds the entire tab bar and shifts all indices. The captured index now
  pointed at the wrong tab. Pattern: lambdas passed to `QMenu::exec()` must never
  capture positional indices — capture item identity (network + target strings)
  and re-derive the index at invocation time.
  of the cursor's block format. Result: only the first (empty/initial) block had
  the margin; every card `<div>` appeared at column 0. Fix: after `insertHtml`,
  iterate over every block from `insertStart` to `insertEnd` and call
  `mergeBlockFormat` on each. Lesson: `QTextBlockFormat` applied before
  `insertHtml` only affects the pre-existing block at the cursor — any block
  element in the HTML creates a fresh block with default formatting; you must
  apply the format to all inserted blocks post-insert.

- **2026-06-11 — right-click spelling suggestions never appeared.** The input is
  a `QTextEdit` (a `QAbstractScrollArea`). The event filter was installed only on
  `m_input`, so keyboard events (which go to the widget) worked, but mouse-driven
  `QEvent::ContextMenu` is delivered to the **viewport**, not the widget — the
  filter never saw it and Qt's built-in menu (no suggestions) showed instead.
  Fix: also `installEventFilter` on `m_input->viewport()` and accept either object
  in the ContextMenu branch; viewport coords are also what `cursorForPosition`
  wants. Lesson: for any QAbstractScrollArea, mouse/context-menu events target the
  viewport — filter the viewport, not just the widget.

- **2026-06-11 — divider rules ("--- Chat ended ---", "──── new ────") rendered
  ragged, not aligned.** Two compounding bugs: (1) `ChatLineFormatter::parseLineShape`
  treated a leading `---` as a nick label (nick = `"-"`), splitting the divider into
  a bogus nick column + body; (2) both divider sites in `MainWindow.cpp` set
  `showTimestamp=false`, dropping the timestamp gutter so the body started 9 cols left
  of every other line. Fix: parser now rejects degenerate/empty nicks (real `-NickServ-`
  notices still parse — sender is non-empty); both dividers now carry their time in the
  gutter so the body lands in the message column. Regression tests added
  (`dividerRulesAreNotMisparsedAsNickLabel`, `realNoticeNicksStillParse`). NOTE: this is
  C++-port-only — Python renders the divider via a dedicated centered `view.insert_rule`
  (`main_window.py:2657`), so it has neither bug; **no backport**. Design confirmed with
  user: align-to-message-column (not Python's centered rule). Lesson: routing dividers
  through the shared chat-line text formatter inherits the nick-parsing heuristics — a
  dedicated rule render (like Python) sidesteps that class of bug entirely.

- **2026-06-11 — `m_spellchecker` + `spell/Speller.h` left inside `#ifdef
  MAXCHAT_WITH_HUNSPELL` in MainWindow.h, but the OS-speller refactor uses them
  unconditionally.** Linux (always has Hunspell → macro defined) compiled fine;
  the Windows/MinGW build (no Hunspell) failed with `'m_spellchecker' was not
  declared in this scope` (configureSpellcheck + showInputContextMenu). Fixed by
  moving the include and the member OUT of the guard — the whole point of the OS
  backend is to work *without* Hunspell. **This is the SAME hunspell-`#ifdef`
  trap already logged below (2026-06-10).** Lesson, again: the dev box's Hunspell
  hides these; verify with `-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON` (or just
  keep spell members/includes unconditional unless they truly need Hunspell).
  Also: build.bat now writes `build-win-mingw/build-output.log` so Windows
  failures are inspectable from WSL.

- **2026-06-11 — "complete" features that were hollow (inline images, X/Twitter
  cards).** Both compiled and had all the scaffolding (enum kinds, classifier,
  prefs toggles, renderers) so they *looked* done, but end-to-end they did
  nothing: inline `<img src=remote>` never renders in a QTextBrowser (it doesn't
  fetch over the network → broken-image glyph), and X posts were fetched from
  raw x.com, which serves a JS shell with no OpenGraph tags. **Lesson:** "builds
  + has the code path" ≠ "works." Verify a feature produces its visible result,
  not just that the plumbing exists. Fix: services/ImageFetcher downloads +
  embeds images as document resources; X posts fetch via fxtwitter.
- **2026-06-11 — added xPostFetchUrl() but forgot to call it from the classifier
  dispatch.** The helper existed; the dispatch still used the raw URL, so the
  fix was inert until a unit test caught it. **Lesson:** when adding a transform,
  assert the *observable* output (fetchUrl), not just that the helper compiles.
- **2026-06-11 — raw-string regex delimiter clash.** `R"(...([^"]+)"...)"` was
  terminated early by the `)"` inside the pattern. Use a custom delimiter
  (`R"RX(...)RX"`) whenever the pattern can contain `)"`.

- **2026-06-11 — Qt auto-sort corrupts row indices mid-addChannel.** When `QTableWidget::setSortingEnabled(true)` is active, calling `setItem(row, sortColumn, ...)` triggers an internal sort immediately — the row moves to its sorted position but the local `row` variable still holds the old index. Subsequent `setItem(row, otherColumn, ...)` calls write to the wrong row. Fix: call `setSortingEnabled(false)` before any `setItem` sequence when rows are being mutated; call `sortItems()` manually afterward (works regardless of enabled state). Do NOT re-enable with `setSortingEnabled(true)` after manual `sortItems()` — that fires Qt's internal sort a second time. Only call `setSortingEnabled(true)` *before* `sortItems()` (e.g., in `setComplete`) so the auto-sort and the manual sort are in the same pass without hidden-row state to corrupt.

- **2026-06-11 — DCC enable flag stale after Preferences close.** `DccManager::enabled_` was only refreshed inside `configureDcc()`, but `openPreferences()` calls `applyCurrentSettings()` (which updates `m_dccEnabled`) and does NOT call `configureDcc()`. Result: after the user unchecked "Enable File Transfers" and closed Preferences, `enabled_` stayed `true` until the next DCC-related action. Fix: call `m_dccManager->setEnabled(m_dccEnabled)` directly inside `applyCurrentSettings()`. Lesson: whenever two state caches must track the same setting (a MainWindow flag + a subsystem flag), sync both in `applyCurrentSettings` — never rely on a lazy-refresh call site to pick it up.

- **2026-06-10 — Scripting/SoundPlayer includes landed inside `#ifdef
  MAXCHAT_WITH_HUNSPELL` in MainWindow.h.** Successive include edits anchored on
  the spell include, which was already inside the hunspell guard, so
  `scripting/LuaEngine.h`, `ScriptHost.h`, and `ui/SoundPlayer.h` only got
  included when hunspell was found. The Linux dev box always has hunspell, so it
  compiled there; the Windows/MinGW build (no hunspell) failed with
  `'maxchat::scripting' has not been declared`. Fixed by moving those includes
  out of the guard. **Lesson:** the dev box's hunspell hides `#ifdef
  MAXCHAT_WITH_HUNSPELL` regressions — verify with
  `-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON` to mirror the no-hunspell (Windows)
  config before declaring a build green.
- **2026-06-10 — The Edit tool rewrote `build.bat` with LF endings.** cmd.exe
  seeks batch labels by byte offset, so LF-only files make a later `call :label`
  (here `copy_assets`, near EOF) fail with "cannot find the batch label
  specified" while earlier labels still work. Fixed by restoring CRLF and having
  `sync-to-win.sh` force CRLF on the synced `build.bat`. **Lesson:** never let a
  `.bat` go out with LF; the sync now guarantees CRLF regardless of the repo
  file's endings.

- **2026-06-10 — Two injection gaps found in the cross-cutting sweep (audit
  phase 10).** (1) `sendRaw` appended `\r\n` but didn't strip embedded CR/LF, so
  a lone `\r` mid-message (CR-only paste survives `trimmed()`) could split a
  second command onto the wire — fixed by stripping CR/LF at the single send
  choke point (defense-in-depth regardless of input path; Python doesn't do this
  either → backport BP-10). (2) the topic bar `QLabel` used the default AutoText
  format, so a channel `TOPIC` containing `<img src=…>`/markup would render as
  rich text — fixed with `setTextFormat(Qt::PlainText)`. Lesson: any Qt widget
  that shows remote/attacker text (QLabel, tooltips, QTextBrowser) defaults to
  rich-text detection — force PlainText unless you are deliberately rendering
  escaped HTML.

- **2026-06-10 — Link-preview SSRF guard only checked the first URL (audit phase
  9, SECURITY).** Two gaps vs the Python original: (1) `buildRequest` set
  `NoLessSafeRedirectPolicy` (which only blocks https→http downgrades) and the
  `finished` handler never re-checked `reply->url()`, so a public host could
  302-redirect to `http://169.254.169.254/` (cloud metadata) or a LAN IP and we
  followed it. (2) the guard checked the hostname *string* only — a public-looking
  domain whose DNS A record points at a private IP sailed through. Python does
  both: it re-runs the guard on every redirect (`reply.redirected`) and resolves
  the host (getaddrinfo) rejecting any private/loopback/link-local/reserved IP.
  Fixed: re-validate each redirect hop (abort on a disallowed target) and add a
  synchronous resolve-and-check that reuses the existing IP block list on every
  resolved address. Lesson: an SSRF allowlist must be applied to the *connection
  target* (post-DNS, post-redirect), not just the URL the user typed.

- **2026-06-10 — Comic art decoder trusted a file-supplied length → OOM
  (audit phase 6, SECURITY).** `ComicArt.cpp inflateDib` prepends the DIB's
  `origLen` field to the zlib stream for `qUncompress`, which allocates that
  length up front. `origLen` is attacker-controlled (a `.avb`/`.bgb` is
  user-supplied art), so a tiny file claiming `origLen = 0xFFFFFFFF` forced a
  ~4GB allocation → OOM. The Python original is immune: it uses
  `zlib.decompress(stream)` and never reads the length header for sizing. Also
  `w`/`h` were unbounded (→ `w*2` stride overflow + huge `QImage`) and
  `std::abs` on a possibly-`INT_MIN` height was UB. Fixed: cap `origLen` at
  32 MB and dimensions at 4096, and compute the height magnitude in qint64.
  Tested with crafted headers in comic_art_test. Lesson: the `qUncompress`
  big-endian-length-prefix trick is convenient but it makes a file-supplied
  number an allocation size — always clamp it.

- **2026-06-10 — DCC receive cap skipped for size-0 offers → unbounded disk write
  (audit phase 5, SECURITY).** `DccManager::beginReceive` guarded the byte cap
  with `if (tr->size > 0) { truncate to remaining }`. The Python original does
  the opposite — `remaining = size - transferred; if remaining <= 0: drop` —
  which means a peer offering size 0 gets *nothing* written. The C++ inversion
  meant a malicious `DCC SEND "x" ip port 0` (or a negative size) bypassed the
  cap entirely: every byte the peer sent was written to disk with no limit, and
  the disconnect handler reported success. Remote-triggerable with accept policy
  `all` or a trusted nick (no user click). Fixed with a pure, unit-tested helper
  `dccWritableChunk(size, transferred, available)` that returns 0 for
  zero/negative/complete and otherwise caps to remaining; offered size is also
  clamped to >=0 at parse. Lesson: when porting a bounds check, port the
  *predicate exactly* — `if size > 0 then cap` is NOT equivalent to `if
  remaining <= 0 then drop` at the size==0 boundary, and that boundary is
  attacker-reachable.

- **2026-06-10 — Incoming IRC lines capped at the 512-byte *send* limit (audit
  phase 1).** `IrcConnection::queueIncomingLines` dropped any incoming line
  larger than `IrcMaxWireBytes` (512) and aborted on a pending buffer >4096.
  512 is the RFC limit for the *message* you SEND — but an incoming IRCv3 line is
  that 512 plus up to ~8KB of message tags (server-time, account-tag), which the
  client requests during CAP. So once tags were negotiated, ordinary messages
  near the limit were silently dropped and long in-progress lines killed the
  connection. The Python client always used separate constants (8192 line /
  65536 buffer); the port collapsed them into one. Fixed with
  `MaxIncomingLineBytes`/`MaxPendingBytes`. Lesson: send caps and receive caps
  are different numbers in IRC — never reuse the wire-send limit for parsing.

- **2026-06-09 — Wallpapers never rendered (every theme, every platform).**
  `effectiveWallpaperUrl()` returned `QUrl::fromLocalFile(...)` (`file:///...`)
  and pasted it into the stylesheet as `border-image: url(file:///...)`. Qt
  style sheets do not resolve `file://` URLs inside `url()` — they want a plain
  filesystem path (the Python app passes `Path.as_posix()`). Result: Synthwave
  and Vaporwave silently lost their wallpaper, and because the wallpaper still
  counted as "present", the Synthwave `bg_gradient` fallback was skipped too —
  flat dark window instead of either look. Fixed by returning a plain
  forward-slash path (`effectiveWallpaperPath()`) and quoting it in `url("...")`.
  Regression-tested in `tests/unit/theme_catalog_test.cpp`. Lesson: when porting
  Python/Qt string-built QSS, keep the *exact* value shapes the original feeds
  into the stylesheet; "more correct looking" types (QUrl) can be silently
  wrong.

- **2026-06-09 — `build.bat` swallowed packaging failures.** `:copy_assets`
  ignored `xcopy` errors and `exit /b 0`'d unconditionally, so a dist without
  `assets\` looked like a successful build (app silently falls back to the
  built-in Dark theme only). Now each copy is checked and fails the build.

## DECISIONS

- **2026-06-10 — Scripting/plugins: use embedded Lua (supersedes the earlier
  defer).** Python ships a Python plugin API; embedding CPython in a C++/Qt app
  is heavy + a Windows packaging headache, so the port adopts **Lua 5.4**
  (tiny, MIT, built for embedding). User accepted dropping Python-plugin parity.
  The Lua **API contract + the three bundled example scripts are done**
  (`SCRIPTING.md`, `assets/scripts/{_hello,dice,url_logger}.lua`); converting the
  scripts pinned the API and surfaced the sandbox needs (e.g. `api.append_file`
  instead of raw `io` for url_logger). **Still to build:** the C++ Lua host —
  vendor the Lua sources, embed the interpreter, dispatch the hooks, enforce the
  sandbox, wire `/load /unload /reload` — AUDIT FIX BACKLOG #6. Note: this adds
  Lua sources to the Windows `build.bat`, which can't be tested from WSL.

## Conversion debt spotted while comparing themes (2026-06-09)

- ~~Bundled chat-themes.json lost the irssi/BitchX extras~~ DONE later that
  day: `ts`/`bracket`/`system`/`nicks` restored in the JSON, modeled in
  `ChatThemeDefinition`, rendered by `ChatLineFormatter` (timestamp color,
  bracket tint, mono/palette nicks, system-line tint via a `systemLine` flag
  stored on `ChatBufferLine`; ACTION/NOTICE call sites pass systemStyling
  false).
- No user themes: Python loads `<config>/themes/*.json` + `chat_themes.json`
  and writes an `_example.json` template; C++ reads bundled assets only and the
  registry is a one-shot static (blocks runtime registration / theme editor).
- No `QPalette` application; stylesheet applied to MainWindow, not app-wide.
- Preferences "Customize..." theme-editor buttons are disabled stubs.
- Per-area fonts/colors (`chat_text_color`, `event_color`, `tree_color`,
  `userlist_color`, `nick_label_color`, `status_text_color`, `topic_color`)
  still missing — the new Fonts page holds only app/chat fonts plus a planned
  note.
- Default theme is `synthwave` in C++ vs `dark` ("Default") in Python.

## PARITY GAP DEEP DIVE vs Python app (2026-06-09 scan)

Full two-sided feature inventory diff. Member-list nick colouring was fixed in
this session (recolorMemberList follows chat nick rules incl. mono/palette).

Member list still missing vs Python: away-user dimming (grey when away —
needs away tracking from away-notify/WHO), per-nick colour override ("Set
color..." context item + `nick_colors` pref, override wins even in mono),
role-grouped view with header boxes when colored-nicks is OFF (C++ always
flat), "N users" header, Send File / DCC Chat / comic context items.

Whole subsystems absent (not just stubbed):
- Notifications: toasts, beep, taskbar flash, highlight_words, notify_* prefs,
  minimize_to_tray, unread/highlight in window title; per-buffer mute exists.
- Tray icon (incl. theme-tinted bubble icon, DND in tray menu).
- Sounds: CTCP SOUND, notify .wav (Qt6Multimedia intentionally unlinked).
- Scripts/plugins: Python loader + hooks (on_message etc.); /load /unload
  /reload /scripts parse but stub.
- DCC: send/get/resume/chat, passive mode, dcc_* prefs (deferred).
- Comic Mode: renderer, settings, gallery, emotion wheel, assign-character
  (deferred).
- Inline media: audio transport bar, video player, image full-size viewer
  (link previews/OG cards exist).
- Update checker; translations/localization plumbing; theme editor + user
  themes (themes/*.json, chat_themes.json); Saved Looks (menu placeholder);
  shortcut editor + rebindable shortcuts + Alt+1..9 buffer nav; mIRC color
  picker (Ctrl+K) + Ctrl+B/I/U input formatting; proxy (SOCKS5/HTTP);
  QPalette + app-wide stylesheet.

Settings keys Python has, C++ lacks entirely: scrollback, confirm_quit,
auto_rejoin + rejoin_delay, paste_guard + paste_lines, ignore_invites,
invite_protect, auto_away_mins, hide_version, ctcp_version, log_mask,
replay_lines, pm_echo, show_mode, indent_wrap, marker_line, strip_color_copy,
show_input_hint, tray_icon, nick_colors, looks, shortcuts, update_check,
highlight_words, beep_highlight, notify_*, dcc_*, comic_* (most).

Commands missing: /sound, /dcc *, emoticons (/shrug /tableflip /flip /unflip
/lenny /disapprove). Everything else ported.

Suggested order: (1) notifications+tray+sounds block, (2) member-list
remainder + nick_colors, (3) paste guard/auto-rejoin/invite+CTCP protection
block + confirm_quit + scrollback, (4) input formatting (Ctrl+K/B/I/U), (5)
user themes + theme editor + QPalette, (6) per-area fonts/colors, (7) scripts,
(8) update checker + saved looks + shortcut editor, (9) media/audio, (10)
proxy/perform/autoconnect, (11) comic, (12) DCC.

## Member list / input / user themes / fonts batch DONE (2026-06-09, same session)

- Member list: away nicks tracked per network from away-notify AWAY (new
  awayChanged signal Session→Connection→MainWindow) and dimmed #808080;
  per-nick colour overrides ("nick_colors" setting, member context menu
  "Set Color..."/"Reset Color", win even in mono/colours-off — also plumbed
  into ChatLineFormatter via options.nickColorOverrides); grouped-by-role
  sections with bold header rows when colored-nicks is OFF (headers carry
  Qt::UserRole+1=true and are skipped by recolor/double-click/context menu);
  "N users" header label above the list. Incremental member updates route
  through memberListChanged() (grouped mode rebuilds via metadata render).
- Input: Ctrl+B/I/U insert 0x02/0x1D/0x1F (widget-scoped QShortcuts),
  Ctrl+K opens ColorPickerDialog (classic 16 mIRC codes, inserts \x03NN);
  show_input_hint pref drives the placeholder text.
- User themes: ThemeCatalog now also loads <config>/maxchat/themes/*.json
  (Python format: id = file stem, "name" = label, "_"-files skipped, bad
  files skipped) and chat_themes.json (id->object map); writes
  _example.json template; builtin ids win. NOTE: registry is still a
  startup-time static — a future in-app theme editor needs it reloadable.
- Fonts page: real List font controls (list_font_* no longer mirrored from
  the app font) + 7 colour overrides (chat_text_color, event_color,
  tree_color, userlist_color, nick_label_color, status_text_color,
  topic_color) with Pick/Default. Applied as widget-level stylesheets over
  the theme QSS; event_color overrides the chat theme's system-line colour.
  nick_label_color is stored but NOT yet applied (the port has no nick label
  widget beside the input yet).

## Medium-tier batch DONE (2026-06-09, same session)

Implemented in one pass (all compiled debug+release, selftest OK):
- Member-list nick colouring (recolorMemberList, follows chat nick rules).
- Fun commands: /shrug /tableflip /flip /unflip /lenny /disapprove (parser →
  Text with decorated message).
- Message options with real prefs UI: pm_echo, show_mode, indent_wrap,
  marker_line (<hr> at the unread boundary on buffer switch — unread count is
  captured BEFORE setActiveBuffer zeroes it), log_mask (ChatLogStore mask with
  %network/%channel/%Y/%m/%d; default mask reproduces the historical
  Network/target/date.log layout), replay_lines (0 = default 200).
- Per-network autoconnect (staggered 1.5s apart at startup, falls back to
  first-connectable) + perform-on-connect (lines run via sendCommandOrMessage
  or sendRaw after 001).
- Saved Looks: "looks" settings map; View ▸ Saved Looks (save/apply/delete);
  apply = merge keys + applyCurrentSettings.
- Shortcut editor (ShortcutEditorDialog, "shortcuts" overrides map) +
  Alt+1..9 jump-to-Nth-tree-row + Alt+` jump-to-unread.
- Per-network proxy (SOCKS5/HTTP CONNECT + auth) via QNetworkProxy on the IRC
  socket; fields in the network editor.
- Inline media: chat anchors now route through classifyLinkPreview —
  DirectImage → ImageViewerDialog, DirectAudio → AudioPlayerBar under the
  chat, DirectVideo → MediaPlayerDialog; other links open externally.
  NOTE: Qt6::Multimedia + MultimediaWidgets are NOW LINKED (release-deps
  expectations change; windeployqt ships the extra DLLs; the Qt ffmpeg
  backend is GPL — revisit THIRD_PARTY_NOTICES before the public release).
- Translations plumbing: QTranslator loader in main.cpp (interface_language
  setting, "system" = OS locale; loads qtbase_*.qm + translations/maxchat_*.qm
  next to the binary). UI strings still need tr() conversion incrementally —
  loader is inert until .qm files exist.

## Preferences / Server List parity pass (2026-06-09, same session)

- Preferences was too short and nav labels truncated → dialog now 860x640 and
  the nav width is computed from the longest label (Python `nav_fit_width`
  style). Pages reordered to Python order; added Notifications (stub), Fonts
  (real app/chat font controls moved off Appearance), Comic (stub); Appearance
  rebuilt with the Python grouping (Timestamps/Nicknames/Text/Window) from the
  old Messages controls; Messages keeps hide-join/part + planned stubs;
  "Files" renamed "Files (DCC)". Disabled "(planned)" stubs mark unbuilt
  options.
- Network editor was missing everything auth: NickServ account/password
  (SASL + IDENTIFY fallback), server PASS, allow-plaintext-auth, accept
  unsigned cert, real name, username — ALL of these were already consumed by
  `connectionPlanFromNetwork()`; only the dialog never exposed them. Added
  with Python's section layout. Still planned (not consumed by the backend):
  per-network autoconnect, perform-on-connect, proxy.

## Terminal: tree integration + menu bar + font preference (2026-06-12)

- Added a Terminal group to the Fonts prefs tab (family/size/bold + default
  grid 80x25/80x40). Default family JetBrains Mono. Font size 0 = "use the
  terminal profile's own size" (ibm-vga 11, c64 13); non-zero overrides every
  profile. Keys: `terminal_font_family/size/bold`, `terminal_rows`.
  ScriptTerminalManager stores the pref and pushes it to all current + future
  terminals; MainWindow::applyCurrentSettings feeds it from settings.
- ScriptTerminalDialog now has a File/Settings menu bar. File: Close (hide),
  Kill Terminal (emit killRequested). Settings: Font, Font Size, Terminal Size.
  Menu changes emit fontPreferenceChanged / gridSizeChanged → MainWindow saves
  them globally (load/insert/saveRaw, NOT a full defaults write, to avoid
  clobbering the settings.json shared with the Python app).
- Close semantics CHANGED: closing/minimizing a terminal now only HIDES it
  (removed WA_DeleteOnClose; closeEvent hides + ignores). The session lives
  until an explicit Kill (File menu, or tree right-click). Lua `terminal_close`
  maps to killTerminal (a script closing its own terminal ends the session).
- Terminals appear in the network tree as `Term N - <script>` launcher nodes
  under the network the script ran on (active network at open time). New tree
  role `TreeTerminalRole` (UserRole+2) holds the scoped id and carries NO target
  role so it's never treated as a chat buffer. currentItemChanged + itemClicked
  pop/raise the window (itemClicked too, so re-clicking an already-selected node
  re-pops a hidden terminal). Manager emits terminalsChanged → rebuildNetworkTree.
- Fixed-grid terminals keep 80x25/80x40 and SCALE THE FONT to fill on resize
  (binary-search the largest point size that fits cols*rows into the display
  viewport; recomputed in resizeEvent). Font Size menu sets the base size and
  re-zooms the window via resizeWindowForFont; dragging then re-fits. "free"
  profiles (fitMode none, e.g. the BBS sysop console) keep the configured size.

## BBS/MC DATA audit fixes (2026-06-12)

- B1 network routing: terminal hooks (on_terminal_input/link/closed) now run in
  the network context the terminal was OPENED on (manager stores it; closed
  signal carries it). api.mc_send/mc_reply gained an optional 5th arg `network`;
  bbs.lua session/client sends always pass their stored network so sysop
  console actions reach sessions on other networks.
- B2: bbs.lua drops ALL NOTICE-borne MC DATA (spec: no auto-reply to NOTICE).
- B3: session lookups (INPUT/Q/LOGOFF, client frames) match network+nick, and
  LOGOFF honors its bbs_id payload — nick-only matching cross-routed input.
- B4: per-peer HELLO cooldown (3 s, timer-cleared), MAX_SESSIONS=16 cap, and
  offline HELLOs no longer store ghost sessions.
- B5: cut_utf8() byte-safe truncation in clean_line/clean_frame_line — a cut
  mid-codepoint made the C++ parser reject the WHOLE frame.
- B11: sysop console nick matching is exact-first, substring fallback.
- REGRESSION caught by lua_engine test: framed screens exceed one chunk, and
  static caching only handled single-chunk pages — every framed page silently
  fell back to T frames. Static cache now works per chunk ("main#1", "main#2",
  same S/R/Q verbs); chunk budget lowered 330→300 so "S <part> <hash> " fits
  the 350-byte MC DATA cap. **Lesson: re-run lua_engine (bundled-script test)
  after ANY bbs.lua change — terminal tests alone don't cover the protocol.**

## BBS speed + Phase 11-14 batch (2026-06-12)

- SLOWNESS ROOT CAUSE: every IRC line pays a 2 s flood penalty (ircII model);
  a BBS screen is ~8-15 MC DATA frames, so after the 24 s burst window each
  screen crawled at 1 line/2 s. MC DATA frames now carry a 750 ms penalty
  (spec throttle target) via PendingLine.penaltyMs / enqueueLine; chat lines
  keep 2 s. PING/PONG still free.
- Colored frames: rows can be {text,fg,bg} (VGA indices); chunker emits a
  per-row A op (always, so static-cache parts stay self-contained). Welcome
  logo: magenta box, RETRO cyan->white fade, BBS magenta fade; framed pages:
  cyan chrome, yellow title (red for ACCESS DENIED).
- WELCOME caps echo on HELLO; BYE broadcast to sessions when the sysop kills
  the server console (client shows "Carrier dropped"); INPUT payload is now
  "<bbs_id> <text>" with bare-payload fallback; frame hash widened to 32-bit;
  board persists via api.set (cap 50); client static cache capped at 128;
  on_terminal_link forwards hotspot clicks as INPUT (frame-mode hotspot
  emission still unsupported in the grid renderer); login screen is now a
  cacheable static page; server console help line split (was wrapping 80 cols).
