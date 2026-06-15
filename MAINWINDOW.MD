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

## 6. Phase order (lowest risk / highest value first)

Ordering rationale: start where a clean interface already exists (ScriptHost),
end with the riskiest, most cross-cutting piece (IRC routing).

### Phase 0 — Seam prep (no logic moves)
- Define `MainWindowHost` interface; make `MainWindow` implement it with thin
  methods that just call its existing private helpers.
- Verify nothing changed (compile + tests). One commit.

### Phase 1 — ScriptBridge  ⭐ best first extraction
- **Why first:** `ScriptHost` is already a clean abstract interface; the script
  surface is self-contained and currently forces `MainWindow` to carry ~25
  `scriptX()` overrides + `m_lua` + `m_scriptTerminals`.
- **Move:** all `scriptX()` overrides, `m_lua`, `m_scriptTerminals`,
  `seedBundledScripts`, `buildScriptPermissionsFor`, `isBundledScript`,
  `buildAllScriptPermsMap`, terminal-tree node building hooks.
- `ScriptBridge` *implements* `ScriptHost` and calls `MainWindowHost` for the
  few UI things (echo to buffer, insert input, "active network", open a
  terminal tree node).
- **Test:** the existing `lua_engine_test` already covers the engine; add a
  `script_bridge_test` for permission resolution (bundled vs user ircSend) and
  the MC-DATA network routing using a fake host — logic that's currently
  untestable inside MainWindow.

### Phase 2 — PreviewController
- **Move:** `m_previewImageCache`, the pending-preview maps, `requestPreviewImagesIn`,
  `handleChatAnchorClicked`, OG/image fetch orchestration, the audio/video/image
  player ownership (`m_audioBar`, `m_mediaPlayerDialog`, image viewer),
  `startImageUpload`.
- Calls back through host for "insert preview HTML into buffer line N".
- **Test:** anchor-scheme allow-list (P0-6 logic), SSRF gate calls, cache
  eviction — move/extend the relevant asserts out of `main_window_link_preview_test`.

### Phase 3 — AppearanceController
- **Move:** `applyTheme` / `setTheme` / `setChatTheme` / `setWallpaper` /
  chrome-font QSS / `m_currentTheme` / `m_currentChatTheme` / `m_currentWallpaper`
  / `m_chatOpacity`. Small, self-contained, low risk.
- **Test:** the themes-off / chrome-font-stays behavior (already in the link
  preview test) moves here as `appearance_controller_test`.

### Phase 4 — NotificationController
- **Move:** `m_notifier`, tray, toast, DND, CTCP-SOUND handling, `notify*`.
- Mostly wiring; Notifier already exists. Thin.

### Phase 5 — ComicController
- **Move:** `m_comic*`, `m_comicEnabledBuffers`, `m_comicPanelCache`,
  `refreshComic`, `comicCharacterForNick`, per-channel comic toggle, ComicView
  wiring.
- **Test:** comic-enabled-per-buffer resolution, panel cache keying.

### Phase 6 — DccController (thin)
- **Move:** the DccManager wiring, `/dcc` command handling, CTCP `dccRequest`
  routing to the correct network. DccManager already does the heavy lifting.

### Phase 7 — NetworkController / IrcRouter  ⚠️ riskiest, do last
- **Move:** per-network `IrcConnection` lifecycle (`connectToServer`,
  `connectConfigFor`, disconnect/reconnect/close), the per-network `QHash`es
  (`m_connectionPlansByNetwork`, `m_registeredByNetwork`,
  `m_currentTargetByNetwork`, `m_openTargetsByNetwork`,
  `m_manualDisconnectByNetwork`, `m_initialConnectAttemptsByNetwork`), the
  reconnect/idle-watchdog, and the **big signal-routing block** (messageReceived,
  userJoined/Parted/Quit, namesReceived, modeChanged, topicChanged, mcDataReceived,
  ctcpSound, dccRequest, numerics) translating IRC events → ChatBufferStore +
  tree updates via the host.
- **Why last:** it touches the most state and every other controller subscribes
  to its events. By now the others are extracted, so the router's surface is
  clearer. Consider exposing controller-facing signals
  (`messageForBuffer`, `membershipChanged`, `mcData`) the others connect to.
- **Test:** `network_controller_test` — feed raw IRC lines (reuse the
  `irc_session_test` harness pattern) and assert buffer/tree mutations on a fake
  host. High value: the routing logic is currently the least-tested, most-bug-
  prone area.

### Phase 8 — Residual cleanup
- Friends/away poller → `PresenceController` (or fold into NetworkController).
- Settings/prefs glue stays in MainWindow (it's view-adjacent) but
  `applyCurrentSettings` becomes a fan-out that calls each controller's
  `applySettings(map)`.
- Final pass: MainWindow.h should be mostly widget pointers + the controller
  unique_ptrs + the `MainWindowHost` overrides.

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

- [ ] Phase 0 — MainWindowHost seam
- [ ] Phase 1 — ScriptBridge
- [ ] Phase 2 — PreviewController
- [ ] Phase 3 — AppearanceController
- [ ] Phase 4 — NotificationController
- [ ] Phase 5 — ComicController
- [ ] Phase 6 — DccController
- [ ] Phase 7 — NetworkController / IrcRouter
- [ ] Phase 8 — residual cleanup + final shrink
