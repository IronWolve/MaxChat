# maxchat-c Port Audit — Phased Work Plan

Internal doc. Full audit of the C++/Qt6 port against the Python original
(`/home/iw/work/claude/maxchat`, READ-ONLY reference) covering: port parity
(missing / changed / wrong), functional correctness, and security.

Created 2026-06-10. Source sizes at creation: C++ ~22.3k LOC src + ~5.2k tests
(40/40 green); Python ~14.9k LOC across 54 modules.

---

## How to use this doc

- **One phase per session.** Say "do audit phase N". Each phase is self-contained:
  it lists exactly which files to read on both sides, what to check, and where to
  log results. Do NOT read files outside the phase scope (that's how we run out
  of context).
- **Per phase:** read the listed files, work the checklist, then:
  - **Small fixes** (≤ ~30 min each): fix immediately, add/adjust a test, run the
    full test suite, commit (`area: summary`, IronWolve identity, no AI trailer).
  - **Big items:** log in **FIX BACKLOG** at the bottom with enough detail to act cold.
  - **Findings:** record in the phase's Findings table as you go.
- **Status flow:** `OPEN` → `FIXED` (code changed, tests green) → `VERIFIED`
  (user confirmed on Windows build). Decisions to NOT port something: `WONTPORT`
  with one-line rationale.
- **Severity:** `SEC` (security), `BUG` (wrong behavior), `MISS` (missing feature),
  `DIFF` (intentional/cosmetic difference — confirm intentional).
- When a phase is done: tick its checkbox in the index below, update the
  "Last session" line, and note anything learned in DEV_NOTES (wrong-ports go in
  "THINGS I GOT WRONG").
- After ALL phases: Phase 11 closes out (test-coverage map, backlog triage, doc sweep).

**Last session:** none yet — start with Phase 1.

### Phase index

- [ ] Phase 1 — IRC protocol parity
- [ ] Phase 2 — Slash commands & aliases
- [ ] Phase 3 — Settings keys & preferences
- [ ] Phase 4 — Chat rendering & input
- [ ] Phase 5 — DCC (parity + security)
- [ ] Phase 6 — Comic mode (parity + decoder robustness)
- [ ] Phase 7 — Themes, fonts, notifications, tray, sounds
- [ ] Phase 8 — Logging, replay, buffers
- [ ] Phase 9 — Link previews & SSRF
- [ ] Phase 10 — Cross-cutting security sweep
- [ ] Phase 11 — Tests & docs closeout

---

## Seed findings (from 2026-06-10 exploration — verify in the named phase, don't re-discover)

| # | Sev | Area | Issue | Phase | Status |
|---|-----|------|-------|-------|--------|
| S1 | MISS | Scripting | Entire Python plugin API (`scripting.py`: on_message/on_join/on_command/on_image_paste hooks, /load /unload /reload, scripts dialog) not ported. `/scripts` etc. are stubs. Decide: port, defer, or WONTPORT. | 2 | OPEN |
| S2 | MISS | Update checker | Python has Help ▸ Check for Updates + quiet startup check (GitHub releases). C++ has none. When built: HTTPS-only. | 7 | OPEN |
| S3 | MISS | Sounds | notify.wav playback, beep, CTCP SOUND (`/sound`, plays from `<config>/sounds/`) unwired. Qt6Multimedia is already linked for the media player. | 7 | OPEN |
| S4 | MISS | Notifications | Taskbar flash (notify_flash), tray icon not visually present, highlight_words matching incomplete, DND coverage unverified. | 7 | OPEN |
| S5 | MISS | Themes | "Looks" (saved theme+font combos) placeholder only. Default theme mismatch: C++ "synthwave" vs Python "dark". | 7 | OPEN |
| S6 | BUG? | SASL | Python does PLAIN only; C++ claims PLAIN + SCRAM-SHA-256. Verify SCRAM actually works (or remove the claim) — parity-plus must still be correct. | 1 | OPEN |
| S7 | MISS | Settings | Python DEFAULTS has ~175 keys; C++ SettingsStore ~70. ~105 keys unaccounted (some intentionally dropped, some missing). Phase 3 produces the authoritative map. | 3 | OPEN |
| S8 | BUG? | Rendering | mIRC colors 16–98 + hex color code (\x04 / \x0C#RRGGBB) likely missing in C++ IrcFormat (0–15 only). Strike/reverse/mono coverage unverified. | 4 | OPEN |
| S9 | MISS | Commands | ~25 Python commands likely missing: /sound, /raw, /quote, /oper, /kill, /wallops, /ns /cs /ms /identify /ghost /recover /sidentify /login, /alias /unalias, /query?, unknown-command raw passthrough. | 2 | OPEN |
| S10 | MISS | Comic | Per-channel overrides (comic_channels: bg/chars/ignore), real emotion wheel (stub), assign-character dialog from member menu, stable per-channel random bg, panel right-click menus. | 6 | OPEN |
| S11 | BUG | Shortcuts | Shortcut rebinding exists but reportedly not persisted across sessions (DEV_NOTES). | 3 | OPEN |
| S12 | MISS | DCC UI | Transfers dialog lacks Rate/ETA columns (Python has live rate + ETA). | 5 | OPEN |
| S13 | MISS | Input | Image-paste hook (Python emits imagePasted for script upload) — depends on S1 decision. | 4 | OPEN |
| S14 | MISS | Localization | Translations deferred by user decision: tr() wrapping incomplete, no .qm files. Loader + Localization page exist. NOT an audit failure — track only. | 11 | WONTPORT (deferred) |

---

## Phase 1 — IRC protocol parity

**C++ scope:** `src/irc/IrcMessage.{h,cpp}` (140), `src/irc/IrcSession.{h,cpp}` (~1000),
`src/irc/IrcConnection.{h,cpp}` (~470), `src/irc/ReconnectPlanner.{h,cpp}` (70),
`src/core/ConnectionPlan.{h,cpp}` (170). Tests: `tests/unit/irc_session_test.cpp`.
**Python reference:** `maxchat/irc/client.py`, `maxchat/irc/message.py`,
`maxchat/default_networks.py` (failover semantics only).

Checklist:
- [ ] Message parsing: IRCv3 tags (escaping per spec), prefix, trailing, max line
      length cap (Python: 8192) — byte-for-byte parity on edge cases from
      `test_irc_parse.py`.
- [ ] CAP negotiation: caps requested (multi-prefix, server-time, away-notify, sasl),
      cap-3.2 LS 302 handling, behavior when server offers none / errors mid-CAP.
- [ ] SASL: PLAIN payload format (authzid\0authcid\0passwd b64); **S6** SCRAM-SHA-256 —
      trace the full exchange; failure → graceful CAP END + NickServ IDENTIFY fallback.
- [ ] Registration: NICK/USER order, server PASS (separate from NickServ pass),
      nick collision 433 → Python scheme `base_`, `base__`, …
- [ ] Connect watchdog: Python = 20s to register else next server. C++ equivalent?
- [ ] Failover: Python SERVER_RETRY_LIMIT=3 per server then next; C++ ReconnectPlanner
      semantics match? Auto-reconnect pref honored?
- [ ] CTCP auto-replies: VERSION (hide_version + custom ctcp_version), PING round-trip,
      TIME format, CLIENTINFO list — exact reply text parity.
- [ ] Numerics: diff the handled set. Python handles at minimum 001-005, 251-255, 265/266,
      301/303/305/306, 311-319, 324/329/331-333, 352/353/366, 367/368, 372-376,
      401-465 error display, 433. Any numeric C++ drops silently that Python displayed?
- [ ] ISUPPORT (005): what does each side actually use (PREFIX, CHANTYPES, NETWORK)?
- [ ] away-notify + ISON friends polling: interval, scoping per network.
- [ ] Proxy: SOCKS5/HTTP CONNECT parity with Python proxy config (incl. auth fields).
- [ ] TLS: cert validation default-on, per-network invalid-cert exception plumbed
      identically (server list flag → socket).

### Findings — Phase 1

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 2 — Slash commands & aliases

**C++ scope:** `src/irc/CommandParser.{h,cpp}` (~790), command dispatch portion of
`src/ui/MainWindow.cpp` (search `handleCommand`/dispatch — do NOT read the whole file),
`src/core/CommandAlias.{h,cpp}` (153). Tests: `command_parser_test.cpp`.
**Python reference:** command dispatch in `maxchat/ui/main_window.py` (the
slash-command table — search `def _handle_command` / command map), `maxchat/ui/help_dialog.py`
(authoritative user-facing command list).

Checklist:
- [ ] Build the full diff table: Python's ~71 commands vs C++'s 46. For each missing one:
      implement, or mark WONTPORT with reason. Known suspects (**S9**): /raw, /quote,
      /oper, /kill, /wallops, /sound, services helpers (/ns /cs /ms /identify /ghost
      /recover /sidentify /login — these also need redaction, see Phase 10), /alias,
      /unalias, /load /unload /reload /scripts (**S1** decision), /away with no args
      toggling, /amsg + /ame edge cases.
- [ ] Unknown `/command` → Python passes through as raw IRC. C++ behavior?
- [ ] Alias substitution: $1, $1-, $me, $chan — Python semantics; C++ CommandAlias
      supports? Recursion guard?
- [ ] Default aliases parity (/j /p /w …) — same set both sides?
- [ ] Argument edge cases per command (missing args → usage line vs silent drop) —
      spot-check 10 commands against Python behavior.
- [ ] Fun commands (/shrug etc.) — C++ has extras; fine, mark DIFF.
- [ ] **S1 decision point:** scripting. If WONTPORT, the script-related commands should
      print a helpful "not supported" line, not silently no-op; record decision in
      DECISIONS/DEV_NOTES.

### Findings — Phase 2

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 3 — Settings keys & preferences

**C++ scope:** `src/core/SettingsStore.{h,cpp}` (413), `src/ui/PreferencesDialog.{h,cpp}`
(~1420), `src/core/NetworkImport.{h,cpp}` (84), `src/ui/ShortcutEditorDialog.{h,cpp}` (114).
Tests: `settings_store_test.cpp`, `preferences_dialog_test.cpp`.
**Python reference:** `maxchat/config.py` (the DEFAULTS dict — 175 keys),
`maxchat/ui/prefs_dialog.py` (page-by-page), `maxchat/network_import.py`.

Checklist:
- [ ] **S7 — the key map.** Produce a 3-column table (append below): Python key →
      C++ key (note renames like sort_users_by_status→sort_status) → status
      (OK / MISSING / WONTPORT / C++-only). Every one of the 175 keys gets a row.
      This table is the deliverable of this phase.
- [ ] For each MISSING key that has UI in Python prefs: does the C++ prefs page
      silently lack the control, or is the whole feature absent (cross-ref other phases)?
- [ ] Settings persistence: atomic write? Malformed settings.json → graceful defaults
      (also in Phase 10)? Migration from older key names?
- [ ] Import/export: NetworkImport merge preserves user-owned fields (nick, passwords,
      autojoin) like Python network_import.py? Bundled-catalog merge versioning?
- [ ] **S11:** shortcuts — verify save/load round-trip across restart; fix if broken.
- [ ] Prefs dialog behavioral parity spot-check: each of the 13 pages writes the keys
      it claims (settings() round-trip), defaults match Python DEFAULTS values
      (not just key presence — values: e.g. scrollback 2000, paste_lines 4,
      notify_corner "br", replay_lines semantics 0=default).
- [ ] Keys present in C++ but absent in Python: list as DIFF (fine, but document).

### Python→C++ settings key map (fill in during phase)

| Python key | C++ key | Status | Note |
|---|---|---|---|
| | | | |

### Findings — Phase 3

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 4 — Chat rendering & input

**C++ scope:** `src/irc/IrcFormat.{h,cpp}` (301), `src/ui/ChatLineFormatter` (find it —
may live in MainWindow or its own file; tests: `chat_line_formatter_test.cpp`),
chat-view + input portions of `src/ui/MainWindow.cpp` (search: input history, tab
completion, paste guard, marker line, copy handling — targeted reads only),
`src/core/UrlDetector.{h,cpp}` (67), `src/spell/*` (~420).
**Python reference:** `maxchat/ui/irc_format.py`, `maxchat/ui/chat_view.py`,
`maxchat/ui/input_bar.py`, `maxchat/ui/color_picker.py`.

Checklist:
- [ ] **S8 — color codes:** Python renders mIRC 0–98 + hex `\x04RRGGBB`; verify/port
      16–98 extended palette and hex. Also: strike (\x1E), reverse (\x16),
      monospace (\x11), italic (\x1D), reset (\x0F) — full control-code table diff.
- [ ] Background colors (\x03fg,bg) including bg-only changes and "99" default.
- [ ] Strip-on-copy: copying chat yields plain text (no markup) — strip_color_copy pref.
- [ ] Nick column: right-align, nick_width, separator line draggable reflow (Python has
      drag-handle reflow — does C++?), hang indent (indent_wrap) on wrapped lines.
- [ ] Marker line (unread boundary) placement + clearing rules parity.
- [ ] Timestamps: token conversion correctness (%H %M %S %I %p %y %Y %m %d), 12/24h.
- [ ] Nick coloring: hash function parity not required, but stability + per-nick
      override precedence (override > hash; mono chat-theme behavior) must match.
- [ ] Input: history (↑/↓ with draft preservation?), tab completion cycling (nicks by
      recency? commands? mid-word?), Ctrl+B/I/U/K/O/R insertion, paste guard
      (>paste_lines prompt + throttle send), Ctrl+F search (wrap, next/prev),
      Ctrl+L clear.
- [ ] **S13:** image paste — Python emits hook for scripts. Pending S1 decision;
      minimum: pasting an image must not crash/insert garbage.
- [ ] Spellcheck: ignores URLs/#channels//commands; suggestions on right-click;
      builds and degrades cleanly without Hunspell.
- [ ] Emoji fallback font for glyphs the chat font lacks (Python configures fallback).
- [ ] HTML escaping of nick/text/topic at every render path — overlap with Phase 10;
      here just flag, Phase 10 verifies exhaustively.

### Findings — Phase 4

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 5 — DCC (parity + security)

**C++ scope:** `src/ui/DccManager.{h,cpp}` (896), `src/ui/DccTransfersDialog.{h,cpp}` (168),
DCC wiring in MainWindow (configureDcc, /dcc routing, =peer buffers — targeted reads).
Tests: any dcc test files.
**Python reference:** `maxchat/irc/dcc.py`, `maxchat/ui/dcc_dialog.py`.

Checklist (parity — trace each handshake side-by-side):
- [ ] Active SEND: listen, CTCP format, ack pump, completion on full-size ack
      (32-bit wraparound for >4GB files — both sides mask the same way?).
- [ ] Passive SEND (default): token offer, peer's echo reply matching, connect-out.
- [ ] GET active + passive, RESUME: offset request `RESUME "name" port pos`,
      ACCEPT wait, file open mode (append vs seek), partial-file detection threshold.
- [ ] destPath collision rules: Python naming vs C++ `file.N.ext` — same? (DIFF ok if doc'd.)
- [ ] Accept policy ask/trusted/all + trusted list matching (case-insensitive? hostmask or nick?).
- [ ] DCC CHAT: both handshake directions, line caps (8192 line / 65536 buffer) parity,
      `=nick` buffer lifecycle (close on /dcc close, on disconnect).
- [ ] **S12:** add Rate/ETA columns (EMA over recent throughput) to transfers dialog.
- [ ] Port range binding + advertised IP precedence (dcc_ip pref → local socket addr → fallback).

Checklist (security — attacker = malicious peer):
- [ ] Filename: traversal (`../../x`, `C:\x`, leading `~`, NUL, CR/LF in name), Unicode
      direction tricks, overlong names. Confirm QFileInfo::fileName() strips ALL of these
      on Windows too (backslashes!) — add explicit sanitization if not.
- [ ] RESUME as attack: peer sending RESUME for a file WE offered — position validated
      ≤ file size? Can a peer make us read outside the file or send from offset 0 of a
      different transfer (port-keyed map collisions)?
- [ ] ACCEPT spoofing: resuming_ keyed "peer:file:port" — can another nick hijack?
- [ ] Size lies: offered size ≠ actual bytes — do we stop at offered size (truncate) and
      handle short transfers (hang? timeout?). No unbounded disk write.
- [ ] Token reuse/forgery on passive transfers; awaitingTokens_ cleanup on cancel/timeout.
- [ ] Flood: many simultaneous offers → unbounded dialogs/sockets? Cap or queue.
- [ ] Resume-overwrite: auto-resume must never resume into a file the user didn't start
      (partial-file check is name+size only — note risk, decide).

### Findings — Phase 5

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 6 — Comic mode (parity + decoder robustness)

**C++ scope:** `src/comic/ComicArt.{h,cpp}` (426), `src/comic/ComicCharacter.{h,cpp}` (264),
`src/comic/ComicRenderer.{h,cpp}` (496), `src/ui/ComicView.{h,cpp}` (128),
`src/ui/ComicSettingsDialog.{h,cpp}` (274), comic engine in MainWindow (refreshComic,
comicCharacterForNick, comicBackground, ensureComicArt — targeted reads).
**Python reference:** `maxchat/comic/renderer.py`, `characters.py`, `assets.py`,
`maxchat/ui/comic_settings.py`, `character_gallery.py`, `assign_character.py`,
`emotion_picker.py`, `DEVDOCS/COMIC.md` (rendering rules spec).
**Test art:** `/mnt/c/apps/Mschat25` (user's Comic Chat install; never ship art).

Checklist (parity):
- [ ] **S10a:** per-channel overrides — comic_channels {bg, chars, ignore} honored in
      refreshComic + ComicSettingsDialog Channels page (Python has it; C++ global only).
- [ ] **S10b:** emotion wheel — real picker for own messages (Python emotion_picker.py);
      C++ openEmotionPicker is an auto-only stub.
- [ ] **S10c:** assign-character dialog from member context menu (Python assign_character.py).
- [ ] **S10d:** comic_random_bg — stable seeded-per-channel selection (Python seeds by
      channel; C++ behavior?).
- [ ] **S10e:** panel right-click context menu (Copy panel, Save panel).
- [ ] Emotion guessing table parity (lol→laughing, CAPS→shouting, smileys, etc.) —
      diff against characters.py.
- [ ] Bot filtering: prefixes (! . ~ @ ? s/), comic_ignore nicks, exclude regex,
      ignore_cmds toggle — same rule set incl. the single-char-alnum exception.
- [ ] Layout: panel spill threshold (min font), per-panel bubble cap, think bubbles
      (parenthesized), /me narration, captions + caption color modes — already ported;
      spot-check 3 rules against COMIC.md.
- [ ] Save Comic PNG output parity (sheet layout).

Checklist (decoder robustness — input = hostile .avb/.bgb):
- [ ] Every offset/length read from the file bounds-checked before use (chunk TLV walk,
      cell-table pointers + 0x0107 bias, palette chunk).
- [ ] qUncompress size header: we synthesize the big-endian length prefix — cap it
      (e.g. ≤ 16 MB per cell) so a forged orig_len can't OOM.
- [ ] Width/height sanity caps before allocating QImage (negative h = bottom-up; |h|, w
      bounded, stride math can't overflow int).
- [ ] Truncated file / zero-cell character / empty background → null return, no crash.
      Quick manual fuzz: feed 20 random-mutated copies of a real .avb through
      loadCharacterCells in a scratch test; no crash/hang/OOM.

### Findings — Phase 6

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 7 — Themes, fonts, notifications, tray, sounds

**C++ scope:** `src/ui/ThemeCatalog.{h,cpp}` (1047), `src/ui/ThemeEditorDialog.{h,cpp}` (188),
`src/ui/Notifier.{h,cpp}` (160), `src/ui/ToastWidget.{h,cpp}` (129),
`src/ui/AppIcon.{h,cpp}` (95), theme/font/notify application in MainWindow
(applyTheme, applyCurrentSettings, notification triggers — targeted reads).
**Python reference:** `maxchat/ui/theme.py`, `fonts.py`, `notifier.py`, `sounds.py`,
`app_icon.py`, `maxchat/ui/audio_bar.py` (placeholder — ignore).

Checklist:
- [ ] **S5a:** default theme — Python defaults "dark"; C++ "synthwave". Align or DIFF
      with user sign-off.
- [ ] **S5b:** Looks (saved theme+fonts+colors combos) — implement or WONTPORT.
- [ ] Theme set diff: C++ 17 app + 8 chat vs Python 10 app + 5 chat — extras fine (DIFF);
      verify the shared ones have matching palettes (spot-check 3).
- [ ] User JSON themes: location, schema, live reload, theme editor save path parity.
- [ ] Wallpaper modes: none / theme default / custom path; per-window override?
- [ ] Chat themes: follow mode, mono vs bright, user-saved chat_themes.json.
- [ ] Font re-application after stylesheet (known fixed 14cc0d5) — add regression test
      if none exists.
- [ ] **S3 — sounds:** wire notify.wav playback (notify_sound), beep_highlight (QApplication::beep
      or wav), CTCP SOUND receive (play from `<config>/sounds/`, NEVER from arbitrary
      paths in the CTCP — filename only, must exist locally) + `/sound` send.
- [ ] **S4 — notifications:** taskbar flash (QApplication::alert), highlight_words
      matching (word-boundary? case?) parity with Python, notify_pm/notify_highlight
      per-event toggles, DND suppresses ALL (toast/flash/beep/sound/tray), toast
      corner/duration/theme prefs honored, notify only when window inactive.
- [ ] Tray icon: make it real — show icon (tray_icon pref glyphs), minimize_to_tray,
      left-click restore, context menu (restore/quit), unread badge if Python has it.
- [ ] **S2 — update checker:** implement (Help menu + optional quiet startup, pref
      update_check): HTTPS GitHub releases API, IronWolve/MaxChat? — confirm correct
      repo for the C++ port with user before wiring URL. No auto-download — link only.

### Findings — Phase 7

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 8 — Logging, replay, buffers

**C++ scope:** `src/core/ChatBufferStore.{h,cpp}` (474), `src/core/ChatLogStore.{h,cpp}` (153),
replay + scrollback wiring in MainWindow (targeted reads), `src/core/FloodGuard.{h,cpp}` (63).
**Python reference:** logging/replay code in `maxchat/ui/main_window.py` (search
log_mask/replay), `maxchat/config.py` log defaults, `test_logmask.py` cases.

Checklist:
- [ ] log_mask tokens: %network, %channel, strftime passthrough (subfolders via %Y/%m),
      illegal-filename character handling (Windows: : * ? " < > |) — parity with
      Python test_logmask.py cases.
- [ ] Logs are stripped text (no mIRC codes), one file per day? (C++ does daily;
      Python mask-driven — confirm same semantics or DIFF.)
- [ ] Replay: dimmed history styling, "─── Ended <date> ───" divider, replay_lines
      (0 = default count — Python "sensible default" vs C++ 0→100000 cap: reconcile),
      replay only on first open.
- [ ] Scrollback: cap enforcement (default 2000), keep-on-part (don't clear buffer when
      leaving channel), unread/highlight counters reset rules.
- [ ] Muted buffers (muted_channels) suppress notifications but still log.
- [ ] PM echo (pm_echo), query auto-open on incoming PM, `=dcc` buffers excluded from
      logging? (decide + document).
- [ ] FloodGuard: thresholds (flood_msgs/flood_secs), friends exempt, auto-ignore
      behavior parity; invite_protect + ignore_invites.
- [ ] Log writes: no markup injection (CR/LF in incoming text can't forge log lines).

### Findings — Phase 8

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 9 — Link previews & SSRF

**C++ scope:** `src/services/OpenGraphFetcher.{h,cpp}` (148), `OpenGraphParser.{h,cpp}` (199),
`LinkPreviewClassifier.{h,cpp}` (307), `LinkPreviewPolicy.{h,cpp}` (69),
`LinkPreviewRenderer.{h,cpp}` (168), `src/ui/ImageViewerDialog/AudioPlayerBar/MediaPlayerDialog`,
preview wiring in MainWindow (targeted). Tests: the services tests.
**Python reference:** `maxchat/ui/media.py` (is_safe_fetch_url + redirect re-validation),
`test_media.py`.

Checklist:
- [ ] SSRF block list parity AND completeness: 127/8, 10/8, 172.16/12, 192.168/16 —
      plus gaps Python may share: 169.254/16 (link-local/cloud metadata), 0.0.0.0,
      100.64/10 (CGNAT), ::1, fc00::/7, fe80::/10, IPv4-mapped IPv6, decimal/octal
      IP forms (http://2130706433/). Fix in C++ even if Python lacks them.
- [ ] Redirect handling: Python re-validates each redirect. C++ uses
      NoLessSafeRedirectPolicy — that does NOT block redirect-to-private-IP. Verify a
      redirected request re-checks the target (hook redirected() signal and validate,
      or resolve+pin). This is the highest-risk item in this phase.
- [ ] DNS rebinding: hostname checked then fetched separately? Note risk; pragmatic
      mitigation = resolve once, connect to the checked IP (or accept + document).
- [ ] Credentials-in-URL block, data:/file:/ftp: scheme rejection (classifier),
      SVG rejection for direct images.
- [ ] Size caps (256 KB HTML; image/audio/video fetch caps?), timeout (10s), max
      concurrent fetches (flood of URLs in chat → request storm?).
- [ ] Fetched metadata (og:title/description/site) HTML-escaped before rendering;
      image URLs from OG re-validated through the same SSRF check before fetch.
- [ ] content_services toggles actually gate each preview type (images/media/xcards/webcards).
- [ ] Previews only triggered from displayed messages (not raw log / ignored users).

### Findings — Phase 9

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 10 — Cross-cutting security sweep

**C++ scope:** `src/irc/IrcRedaction.{h,cpp}` (43), `src/ui/RawLogDialog.{h,cpp}` (100),
grep-driven targeted reads across src/ (this phase is grep + spot-read, not full reads).
**Python reference:** `_redact` in `maxchat/irc/client.py`, `test_redact.py`,
`maxchat/DEVDOCS/AUDIT_FINDINGS.md` (Python's own audit — re-check every finding
fixed there is also fixed here).

Checklist:
- [ ] **Redaction completeness:** PASS, AUTHENTICATE, NickServ-style (IDENTIFY,
      REGISTER, GHOST, RECOVER, RELEASE, SIDENTIFY, LOGIN, /ns /cs /ms forms,
      `PRIVMSG NickServ :identify …`) — both directions (sent AND received echoes),
      raw log AND chat display AND disk logs. Diff against Python test_redact.py cases.
- [ ] **HTML injection:** grep every path that builds HTML for QTextBrowser
      (chat lines, topic, nick list tooltips, WHOIS output, CTCP display, DCC status
      lines, channel list topics, OG card fields, toast text). Each interpolated
      untrusted string must pass through escaping. Write one adversarial test:
      message `<img src=x onerror=...>` + topic + nick with `<b>` render inert.
- [ ] **QSS injection:** theme JSON values and wallpaper path are interpolated into
      stylesheets — can a crafted value (quote/brace/url()) escape? Validate colors
      (#hex only) and escape/normalize paths.
- [ ] **Settings robustness:** truncated/malformed settings.json, wrong types
      (string where int expected), huge values → defaults + no crash. Atomic write
      (temp+rename) so a crash can't zero the file.
- [ ] **Untrusted-name file paths:** beyond DCC (Phase 5) — log_mask channel names
      (`#../../x` as channel name → path traversal in log path!), sound filenames from
      CTCP SOUND, comic art dir contents. Sanitize channel/network components for
      filesystem use.
- [ ] **Command injection via nick/channel into IRC protocol:** user-supplied targets
      containing spaces/CR/LF must not split into extra IRC commands (e.g. `/msg "a\r\nQUIT"`).
      Verify outbound line builder strips/rejects CR/LF everywhere.
- [ ] **Password storage:** plaintext in settings.json — parity with Python (DIFF, not
      regression). Note in doc; optional future: OS keychain.
- [ ] **Integer/buffer review:** the three binary parsers (IrcMessage byte handling,
      ComicArt — covered Phase 6, DCC acks — covered Phase 5): confirm Phase 5/6
      findings closed; here just verify status.
- [ ] cppcheck + compiler warnings sweep at max useful level; fix new findings.

### Findings — Phase 10

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## Phase 11 — Tests & docs closeout

**Scope:** `tests/` both repos (lists only, then targeted), HANDOFF.md, DEV_NOTES.md,
this file.

Checklist:
- [ ] Map Python's 23 test files → C++ equivalents; list untested-in-C++ areas
      (known candidates: dcc handshakes, comic filtering/emotions, redaction breadth,
      logmask, reconnect/failover, autoconnect sequencing, proxy).
- [ ] Every FIXED finding in this doc has a regression test where feasible.
- [ ] Full suite green Debug + Release; Windows build clean (user runs build.bat).
- [ ] Backlog triage: every FIX BACKLOG item is either scheduled, done, or WONTPORT
      with user sign-off.
- [ ] DEV_NOTES "THINGS I GOT WRONG" updated with any wrong-port findings from the audit.
- [ ] HANDOFF.md updated; this doc's phase index all ☑; final summary section appended
      (counts by severity/status).
- [ ] Revisit S14 (translations) with user — schedule or keep deferred.

### Findings — Phase 11

| Sev | Where | Issue | Status |
|-----|-------|-------|--------|
| | | | |

---

## FIX BACKLOG

Big items deferred from phases. Each entry must be actionable cold: what, where, why, sketch.

| # | From phase | Sev | Item | Notes | Status |
|---|-----------|-----|------|-------|--------|
| | | | | | |
