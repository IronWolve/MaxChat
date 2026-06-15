# MainWindow Decomposition — Design Plan

Internal design doc. How to break the `MainWindow` God-object
(`src/ui/MainWindow.cpp` ~9,880 lines, `MainWindow.h` ~150 members) into
testable collaborators **without changing behavior**. Follow it phase by phase;
each phase is independently shippable and leaves the app working + tests green.

---

## 1. Why

`MainWindow` currently owns ~10 unrelated responsibilities. Consequences:
- Almost none of it is unit-testable — you can't instantiate a 9.9k-line widget
  in a test (`main_window_link_preview_test` resorts to friend-class white-box
  poking, and links nearly the whole app).
- A change to previews can break comic mode; merge conflicts are large.
- New contributors can't find anything.

**Goal:** `MainWindow` becomes a thin **view shell** — it owns the real widgets
(network tree, chat view, input bar, member list, menus, status bar) and the
top-level layout, and delegates all *logic* to focused controller objects it
owns. Target: MainWindow.cpp under ~2.5k lines, each controller 300–900 lines
with its own unit test.

**Non-goals:** no behavior change, no new features, no threading changes, no
settings-format changes. Pure structural refactor.

---

## 2. Guiding principles

1. **Strangler / extract-class, one module at a time.** Never a big-bang rewrite.
2. **Behavior-preserving.** After each phase the app is identical to the user.
   The full suite (`QT_QPA_PLATFORM=offscreen ctest`) stays green; rebuild the
   affected *test target* explicitly (stale test binaries hide regressions).
3. **Narrow seams.** A controller never reaches back into MainWindow's widgets
   directly. It talks through a small **Host interface** (a handful of methods:
   "append a line to buffer X", "current network", "insert into input") or via
   Qt signals. Dependencies point one way: MainWindow → controller, plus a
   thin callback/interface for the few things controllers need back.
4. **Move state with behavior.** Each member field migrates into the controller
   that owns it (the per-network `QHash`es, the caches, the timers).
5. **Test as you extract.** The payoff is testability — add a unit test for each
   controller's logic the moment it's isolated. That's the definition of done.
6. **Commit per phase** (`area: summary`, IronWolve identity, no AI trailer, no
   push). Keep forwarding shims within a phase if it makes the diff reviewable;
   delete them at the end of the phase.

---

## 3. Current responsibility clusters (grounded in real names)

| Cluster | Representative state | Representative methods |
|---|---|---|
| **IRC / connections** | `m_connections`, `m_connectionPlansByNetwork`, `m_registeredByNetwork`, `m_currentTargetByNetwork`, `m_openTargetsByNetwork`, `m_manualDisconnectByNetwork`, `m_initialConnectAttemptsByNetwork`, `m_reconnect*`, `m_pendingNamesByChannel`, `m_channelDrainTimer` | `connectToServer`, `connectConfigFor`, the giant per-connection signal wiring, `disconnectFromNetwork`, `reconnectNetwork`, `closeNetwork`, numerics → buffer routing |
| **Chat buffers + tree/tabs** | `m_chatBuffers` (ChatBufferStore), `m_chatView`, `m_networkTree`, `m_bufferTabBar`, `m_currentTarget` | `appendSystemLine*`, `appendChatLine`, `renderActiveBuffer`, `rebuildNetworkTree`, `activateBufferTarget`, `syncBufferTabs`, scrollback/separator |
| **Link previews + media** | `m_previewImageCache`, `m_pendingPreviewCandidates`, `m_audioBar`, `m_mediaPlayerDialog` | `requestPreviewImagesIn`, `handleChatAnchorClicked`, OG/image fetch orchestration, `startImageUpload` |
| **Scripting / MC DATA bridge** | `m_lua`, `m_scriptTerminals`, the `ScriptHost` vtable | all `scriptX()` overrides (`scriptSay`, `scriptMcData`, `scriptTerminal*`, `scriptHttpGet`...), `seedBundledScripts`, `buildScriptPermissionsFor`, `isBundledScript` |
| **Comic mode** | `m_comic*` (5), `m_comicEnabledBuffers`, `m_comicPanelCache` | `refreshComic`, `comicCharacterForNick`, comic toggle, ComicView wiring |
| **DCC** | `m_dccManager` (DccManager already its own class) | CTCP `dccRequest` routing, `/dcc` command handling, dialog wiring |
| **Notifications / tray** | `m_notifier`, `m_tray`, toast/DND/sound | `notify*`, tray `messageClicked`, CTCP SOUND |
| **Themes / appearance** | `m_currentTheme`, `m_currentChatTheme`, `m_currentWallpaper`, `m_chatOpacity` | `applyTheme`, `setTheme`, `setChatTheme`, `setWallpaper`, chrome font |
| **Friends / away** | `m_friendNicks`, `m_onlineFriendsByNetwork`, `m_friendPollTimer`, `m_away*`, `m_autoAwayTimer` | `pollFriends`, `triggerAutoAway`, `noteUserActivity` |
| **Settings / prefs glue** | `m_settings` | `openPreferences`, `applyCurrentSettings`, `exportSettings`, nav shortcuts |

DccManager, Notifier, LuaEngine, ScriptTerminalManager, ChatBufferStore,
ThemeCatalog, the `services/*` fetchers, CommandParser already exist as separate
classes — so several clusters are mostly **wiring** that can move into a thin
controller cheaply.

---

## 4. Target architecture

```
MainWindow (QMainWindow)         ── view shell: widgets, layout, menus, status
  owns ──> MainWindowHost (interface it implements; passed to controllers)
  owns ──> ScriptBridge          ── implements ScriptHost; owns LuaEngine + ScriptTerminalManager
  owns ──> PreviewController     ── preview fetch/cache + anchor clicks + media players
  owns ──> NetworkController     ── per-network IrcConnection lifecycle + signal routing
  owns ──> ComicController       ── comic state + render orchestration
  owns ──> NotificationController── notifier/tray/toast/DND/sound
  owns ──> AppearanceController  ── theme/chat-theme/wallpaper/chrome font
  owns ──> DccController (thin)  ── wraps DccManager wiring + /dcc + CTCP routing
  keeps ── ChatBufferStore, network tree, chat view, input, member list (the view)
```

**`MainWindowHost`** — a small abstract interface (≈8–12 methods) that
controllers call back into, e.g.:
```cpp
struct MainWindowHost {
  virtual QString activeNetwork() const = 0;
  virtual QString currentTarget() const = 0;
  virtual void appendSystemLine(const QString& network, const QString& target,
                                const QString& text) = 0;
  virtual void appendChatLine(/* ...ChatLine... */) = 0;
  virtual void insertInput(const QString& text) = 0;
  virtual maxchat::core::SettingsStore& settings() = 0;
  virtual QWidget* widget() = 0;            // parent for dialogs
  // ...the minimum each controller needs, nothing more
};
```
MainWindow implements it. Controllers take `MainWindowHost&` (or narrower
sub-interfaces) in their constructor. This is the seam that makes them testable
with a fake host.

---

## 5. Extraction recipe (apply to every module)

1. **Create** `src/ui/<Name>Controller.{h,cpp}`, add to `CMakeLists.txt`
   (`maxchat-c` target + any test target that needs it).
2. **Move state**: cut the member fields for this cluster from `MainWindow.h`
   into the controller; MainWindow gets a single `std::unique_ptr<XController>`.
3. **Move methods**: move the cluster's methods into the controller. Where a
   method still needs a widget/buffer, route it through `MainWindowHost`.
4. **Wire**: construct the controller in MainWindow's ctor (after the host bits
   it depends on exist); connect any Qt signals it exposes.
5. **Forwarding shims** (optional, within-phase): keep `MainWindow::foo()`
   calling `m_xController->foo()` so call sites compile unchanged; delete shims
   once call sites are updated.
6. **Test**: add `tests/unit/<name>_controller_test.cpp` driving the controller
   against a fake `MainWindowHost`. Register in CMake + `add_test`.
7. **Build the app + the new test target explicitly**, run full offscreen ctest,
   commit.

---

## 6. Phases (detailed, executable)

Ordering rationale: start where a clean interface already exists (ScriptHost),
end with the riskiest, most cross-cutting piece (IRC routing). Each phase below
is a self-contained mini-plan: do it in one sitting, keep tests green, commit.
Method/member names are the real ones in the current tree.

---

### Phase 0 — `MainWindowHost` seam (no logic moves)

**Goal:** introduce the interface controllers will depend on, with zero behavior
change, so later phases just *move* code behind it.

- **New files:** `src/ui/MainWindowHost.h` (pure abstract, no .cpp).
- **Define** the minimum surface (grow it per phase, don't front-load):
  ```cpp
  class MainWindowHost {
  public:
    virtual ~MainWindowHost() = default;
    virtual QString activeNetwork() const = 0;
    virtual QString currentTarget() const = 0;
    virtual void appendSystemLine(const QString& network, const QString& target,
                                  const QString& text) = 0;
    virtual void insertInput(const QString& text) = 0;
    virtual maxchat::core::SettingsStore& settings() = 0;
    virtual QWidget* dialogParent() = 0;
  };
  ```
- **Steps:**
  1. Add the header; make `MainWindow` inherit `public MainWindowHost`.
  2. Implement each method as a one-liner over an existing private helper
     (`activeNetwork()` → `activeNetworkName()`, etc.).
  3. Build + full `ctest`. No test changes.
- **Done:** compiles, suite green, no diff in behavior.
- **Commit:** `ui: add MainWindowHost interface (decomp phase 0)`

---

### Phase 1 — `ScriptBridge`  ⭐ best first extraction

**Goal:** lift the entire script surface out of `MainWindow`.

- **New files:** `src/ui/ScriptBridge.{h,cpp}` + `tests/unit/script_bridge_test.cpp`.
- **Move — state:** `m_lua`, `m_scriptTerminals`, the `m_comicEnabledBuffers`?
  (no — that's comic), the scripts dir bits.
- **Move — methods:** the 23 `ScriptHost` overrides (`scriptEcho`, `scriptSay`,
  `scriptSendRaw`, `scriptMcData`, `scriptMe`, `scriptTarget`, `scriptNetwork`,
  `scriptChannels`, `scriptNicks`, `scriptNotify`, `scriptInsertInput`,
  `scriptHttpGet`, and all `scriptTerminal*`), plus `seedBundledScripts`,
  `scriptsDirectory`, `isBundledScript`, `buildScriptPermissionsFor`,
  `buildAllScriptPermsMap`, and the terminal→tree-node wiring + the
  `inputSubmitted/linkActivated/terminalClosed/terminalsChanged/
  fontPreferenceChanged/gridSizeChanged` connections.
- **Design:** `ScriptBridge` *implements* `ScriptHost` (LuaEngine talks to it
  directly). It takes a `MainWindowHost&` for echo/insert/active-network and a
  couple of script-specific callbacks the host still owns (e.g. "the terminal
  list changed → rebuild tree", "send MC DATA / raw on network N" → goes through
  the NetworkController later; for now via host).
- **Host additions:** `scriptEcho`-style buffer append, `terminalsChanged()`
  notification hook, and access to the IRC send path (temporary host method
  `sendRawOn(network, line)` / `sendMcData(...)` until Phase 7 owns it).
- **Steps:**
  1. Create `ScriptBridge` implementing `ScriptHost`; give it `m_lua` +
     `m_scriptTerminals` ownership.
  2. Move the methods; replace MainWindow widget access with `host_` calls.
  3. In `MainWindow` ctor, construct `ScriptBridge` and pass it where `this`
     (as ScriptHost) was passed to `LuaEngine`.
  4. Move the terminal-tree node building (`rebuildNetworkTree`'s terminal
     section reads `m_scriptTerminals->terminals()`) behind a host query.
  5. Delete the moved members/methods from `MainWindow`.
- **Test (`script_bridge_test`):** drive `ScriptBridge` with a fake host +
  fake LuaEngine perms: bundled-vs-user `ircSend` default resolution, MC-DATA
  routing to the dispatch network — logic untestable while embedded in MainWindow.
- **Done:** `MainWindow` no longer names `LuaEngine`/`ScriptTerminalManager`;
  suite green incl. `lua_engine` + new `script_bridge`.
- **Commit:** `ui: extract ScriptBridge from MainWindow (decomp phase 1)`

---

### Phase 2 — `PreviewController`

**Goal:** own link-preview fetch/cache + media playback + image upload.

- **New files:** `src/ui/PreviewController.{h,cpp}` + `tests/unit/preview_controller_test.cpp`.
- **Move — state:** `m_previewImageCache`, `m_pendingPreviewCandidates`,
  `m_audioBar`, `m_mediaPlayerDialog` (+ image viewer ownership), the image
  uploader handle.
- **Move — methods:** `requestPreviewImagesIn`, `handleChatAnchorClicked`,
  `startImageUpload`, `configureImageUploader`, and the OG/image fetch
  orchestration + SSRF-gated callbacks.
- **Host additions:** "replace preview placeholder in buffer line" + "append
  system line"; reuse the existing SSRF helper (`resolvePreviewUrlPublicAsync`)
  directly (it's free-standing in `services/`).
- **Steps:** standard recipe; the anchor-scheme allow-list (P0-6) and SSRF
  re-checks move verbatim.
- **Test:** scheme allow-list refuses `file:`/`javascript:`; cache eviction at
  64; SSRF gate is consulted before playback. Migrate the relevant asserts out
  of `main_window_link_preview_test`.
- **Done:** suite green; preview asserts now live in `preview_controller_test`.
- **Commit:** `ui: extract PreviewController (decomp phase 2)`

---

### Phase 3 — `AppearanceController`

**Goal:** own theme / chat-theme / wallpaper / chrome-font / opacity.

- **New files:** `src/ui/AppearanceController.{h,cpp}` +
  `tests/unit/appearance_controller_test.cpp`.
- **Move — state:** `m_currentTheme`, `m_currentChatTheme`, `m_currentWallpaper`,
  `m_chatOpacity`.
- **Move — methods:** `applyTheme`, `setTheme`, `setChatTheme`, `setWallpaper`,
  `syncThemeActions`, `syncChatThemeActions`, `syncWallpaperActions`, and the
  chrome-font QSS assembly.
- **Host additions:** `qApp` is global (no host needed); needs "re-render active
  buffer" + "the menu action groups" (pass the `QActionGroup*`s in, or expose a
  `syncMenus()` callback).
- **Test:** Themes-Off leaves an empty stylesheet **and** keeps the chrome-font
  rule on themed paths (the exact behavior we just fixed); wallpaper/opacity in
  the QSS. This is the cleanest controller to test.
- **Done:** suite green; the themes asserts move here from the link-preview test.
- **Commit:** `ui: extract AppearanceController (decomp phase 3)`

---

### Phase 4 — `NotificationController`

**Goal:** own notifications / tray / toast / DND / CTCP SOUND.

- **New files:** `src/ui/NotificationController.{h,cpp}` (+ test if logic warrants).
- **Move — state:** `m_notifier`, `m_tray`, DND/last-notify bookkeeping.
- **Move — methods:** `notify`, `updateTrayIcon`, `handleCtcpSound`, tray
  `messageClicked` wiring, DND window checks.
- **Host additions:** "raise/activate the relevant buffer" on tray click.
- **Test:** DND suppression window; "don't notify for self/active buffer" rules.
- **Commit:** `ui: extract NotificationController (decomp phase 4)`

---

### Phase 5 — `ComicController`

**Goal:** own comic state + render orchestration.

- **New files:** `src/ui/ComicController.{h,cpp}` + `tests/unit/comic_controller_test.cpp`.
- **Move — state:** the `m_comic*` members, `m_comicEnabledBuffers`,
  `m_comicPanelCache`.
- **Move — methods:** `refreshComic`, `comicCharacterForNick`, `setComicMode`,
  `saveComic`, `openComicSettings`, `openComicHelp`, ComicView wiring.
- **Host additions:** access to the active buffer's lines + the ComicView widget
  (passed in), and settings.
- **Test:** per-buffer comic-enabled resolution; panel-cache key includes all
  visual inputs (font, tint, size) — guards the cache-staleness class of bug.
- **Commit:** `ui: extract ComicController (decomp phase 5)`

---

### Phase 6 — `DccController` (thin)

**Goal:** own the DccManager wiring (DccManager already does the work).

- **New files:** `src/ui/DccController.{h,cpp}`.
- **Move — state:** `m_dccManager` + the transfers/chat dialogs it owns.
- **Move — methods:** the `dccRequest` CTCP routing to the right network, the
  `/dcc` command handling, accept-policy prompts, dialog wiring.
- **Host additions:** "which network did this CTCP arrive on" (the routing fix
  from the audit — preserve it), append-system-line, dialog parent.
- **Test:** thin; rely on the existing `dcc_manager_test`. Optionally assert the
  CTCP→network routing on a fake host.
- **Commit:** `ui: extract DccController (decomp phase 6)`

---

### Phase 7 — `NetworkController` / IrcRouter  ⚠️ riskiest, do last

**Goal:** own per-network `IrcConnection` lifecycle + the IRC-event→buffer/tree
routing. This is the heart of the app and the least-tested code; doing it last
means every consumer is already a clean controller.

- **New files:** `src/ui/NetworkController.{h,cpp}` + `tests/unit/network_controller_test.cpp`.
- **Move — state:** `m_connections`, `m_connectionPlansByNetwork`,
  `m_registeredByNetwork`, `m_currentTargetByNetwork`, `m_openTargetsByNetwork`,
  `m_manualDisconnectByNetwork`, `m_initialConnectAttemptsByNetwork`,
  `m_pendingNamesByChannel`, `m_channelDrainTimer`, reconnect/watchdog state.
- **Move — methods:** `connectToServer`/`connectConfigFor`,
  `connectionForNetwork`, `activeNetworkName`, `disconnectNetwork`,
  `reconnectNetwork`, `closeNetwork`, `setupConnectionSignals`, and the entire
  per-connection signal→buffer routing (`messageReceived`, `userJoined/Parted/
  Quit`, `namesReceived`/`namesEnd`, `modeChanged`, `topicChanged`,
  `mcDataReceived`, `ctcpSound`, `dccRequest`, numerics).
- **Design:** expose controller-facing **signals** the already-extracted
  controllers connect to instead of MainWindow reaching in:
  `messageForBuffer(buffer, line)`, `membershipChanged(buffer)`,
  `mcData(network, …)`, `ctcpSound(...)`, `dccRequest(network, …)`,
  `registeredChanged(network)`. ScriptBridge/Dcc/Notification subscribe.
- **Host additions:** buffer append/ensure, tree rebuild, member-list refresh —
  whatever the routing currently calls; keep buffer-key formats identical.
- **Steps:** move lifecycle first (connect/disconnect/reconnect), verify; then
  move the signal-routing block in one piece; then convert MainWindow's direct
  calls into signal subscriptions. Preserve the per-network `signalNetwork`
  captures **exactly** — that's where the multi-network routing fixes live.
- **Test (`network_controller_test`):** feed raw IRC lines (reuse the
  `irc_session_test` line-injection harness) through a connection into the
  controller and assert ChatBufferStore/tree mutations on a fake host — the
  highest-value new coverage in the whole effort.
- **Done:** MainWindow no longer wires IRC signals directly; suite green.
- **Commit:** `ui: extract NetworkController (decomp phase 7)`

---

### Phase 8 — Residual cleanup + final shrink

- Friends/away poller (`pollFriends`, `triggerAutoAway`, `noteUserActivity`,
  `m_friend*`/`m_away*`/timers) → `PresenceController`, or fold into
  NetworkController if small.
- Turn `applyCurrentSettings` into a fan-out: build the settings map **once**
  (also fixes the P2 repeated-parse note) and call each controller's
  `applySettings(const QVariantMap&)`.
- Shrink the `main_window_link_preview_test` friend surface as asserts have moved
  to controller tests; ideally drop the friendship entirely.
- **Final state check:** `MainWindow.h` is mostly widget pointers + controller
  `unique_ptr`s + the `MainWindowHost` overrides; `MainWindow.cpp` ≤ ~2.5k lines.
- **Commit:** `ui: residual MainWindow cleanup + settings fan-out (decomp phase 8)`

---

## 7. Risks & gotchas

- **Qt object ownership / lifetime.** Controllers that own `QObject`s (timers,
  dialogs, the LuaEngine) must parent them correctly or hold them in the
  controller and destroy in its dtor. Order matters: construct controllers after
  the widgets/host they depend on; destroy before the widgets (declare the
  `unique_ptr`s after the widgets in `MainWindow.h` so they destruct first).
- **Signal wiring across the seam.** Prefer connecting controller signals →
  MainWindow slots over passing raw `this`. The existing `runInContext` /
  per-network signal-network captures in the connection wiring must move intact —
  that's where multi-network correctness lives (we just fixed routing bugs there;
  don't regress).
- **The ScriptHost vtable** is the one interface that must not change shape
  (LuaEngine depends on it). ScriptBridge implements it verbatim.
- **`applyCurrentSettings` is a fan-out** touched by many clusters — split it
  carefully so each controller reads only its keys; watch the JSON-parse-count
  (a P2 item) — pass one cached settings map to all `applySettings()` calls.
- **White-box test** (`main_window_link_preview_test`) reaches private members
  via friendship; as logic moves out, migrate those asserts to the new
  controller tests and shrink the friend surface.
- **No behavior drift:** keep the same buffer-key formats, the same
  `activeNetworkName()` semantics, the same per-network map keys.

---

## 8. Definition of done (whole effort)

- `MainWindow.cpp` ≈ ≤2.5k lines (view + thin wiring only).
- Each controller: own `.h/.cpp`, owns its state, ≤~900 lines, has a unit test
  driving it through a fake `MainWindowHost`.
- Full offscreen `ctest` green at every phase boundary; net-new tests for the
  previously-untestable routing/preview/script logic.
- HANDOFF + DEV_NOTES updated; this file's phases checked off.

## 9. Progress

- [x] Phase 0 — MainWindowHost seam  *(commit ed38832)*
- [x] Phase 1 — ScriptBridge  *(LuaEngine + ScriptTerminalManager + 23 ScriptHost
      overrides + perms/seeding moved out; MainWindow forwards via `m_scripts`.
      `script_bridge_test` covers bundled-vs-user ircSend + MC-DATA routing.
      Residual: terminal manager takes the window as its QWidget parent — full
      relocation waits on Phase 2+, see DEV_NOTES.)*
- [~] Phase 2 — split on separability:
  - [x] **2a — MediaController** *(inline media playback + image upload: clicked
        image/audio/video links with the scheme allow-list + SSRF gate, and the
        drag/drop/paste image uploader). New `media_controller_test` covers the
        scheme allow-list + no-uploader path.)*
  - [ ] **2b — link-preview fetch/cache (OG + inline images)** — DEFERRED: it's
        interleaved with the chat-render pipeline (`appendPreviewHtmlLine` writes
        straight to the chat document during render; `registerCachedImagesIn` /
        `requestPreviewImagesIn` run mid-render). Migrates when the render
        pipeline is addressed, not as a standalone controller. See DEV_NOTES.
- [—] Phase 3 — AppearanceController — **deferred (render/window-welded).**
      `applyTheme` drives `qApp` palette/stylesheet + tray + window icon + menu
      font, and `m_currentTheme`/`m_currentChatTheme` are read in ~8 render/paint
      sites. A controller is ~half host-callbacks and touches the
      regression-prone theming path. Fold into the render-pipeline pass.
- [—] Phase 4 — NotificationController — **deferred (window/theme-welded).**
      `notify()` depends on window active-state, taskbar flash, click-to-focus
      + activate-buffer, the theme accent, and 6+ `m_notify*` config members; the
      tray also drives minimize-to-tray window behaviour. The only fully clean
      slice (CTCP-SOUND playback) is too small to be its own controller.
- [—] Phase 5 — ComicController — **deferred (render-welded).** Comic view
      swaps in per-buffer inside the chat-render path.
- [x] Phase 6 — DccController — **already done:** DCC logic lives in `DccManager`
      (its own class); MainWindow only wires signals + dispatches `/dcc`. No
      god-object code to extract.
- [—] Phase 7 — NetworkController / IrcRouter — **deferred (core-welded).** The
      IRC event router is the heart of MainWindow; splitting it is the largest,
      riskiest change and belongs with the render-pipeline pass.
- [—] Phase 8 — residual cleanup — folds into the render-pipeline pass.

### Separability conclusion (2026-06-15)

After landing Phases 0/1/2a, an assessment of every remaining phase shows the
clean, high-value extractions are **done**: scripting (`ScriptBridge`) and inline
media + upload (`MediaController`) were genuinely self-contained; DCC was already
a class. Everything left — theme/appearance, notifications, comic, link-preview,
the IRC router — is **welded to the chat-render pipeline and/or window/qApp/theme
state**. Extracting those as standalone controllers would produce glue-heavy
classes that are mostly host-callbacks while repeatedly touching the
regression-prone render/theming code, for marginal structural gain.

**Recommendation:** treat the remaining items as a single future **render-pipeline
refactor** (extract a chat-render component first, then the preview/appearance/
comic/notification state can move with it), rather than as separate controller
phases. The `MainWindowHost` seam is in place to support that when it happens.

→ Full design for that refactor: **[RENDER_PIPELINE_DESIGN.md](RENDER_PIPELINE_DESIGN.md)**
(the `ChatPane` component + `ChatRenderTheme`/`ChatPaneDelegate` seams, R0–R5
behaviour-preserving migration plan, and how each deferred phase falls out of it).
