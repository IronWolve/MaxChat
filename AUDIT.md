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
  - **Port-is-better items:** when a finding is something the C++ port does
    *better* than Python (or a latent bug in BOTH), add it to the **Backports to
    Python** section below **AND** to the mirror copy at
    `../maxchat/DEVDOCS/BACKPORTS.md`. Keep the two in sync — never leave the
    info in only one repo. (Don't run git in the Python repo; the user drives
    that. Just write the file.)
- **Status flow:** `OPEN` → `FIXED` (code changed, tests green) → `VERIFIED`
  (user confirmed on Windows build). Decisions to NOT port something: `WONTPORT`
  with one-line rationale.
- **Severity:** `SEC` (security), `BUG` (wrong behavior), `MISS` (missing feature),
  `DIFF` (intentional/cosmetic difference — confirm intentional).
- When a phase is done: tick its checkbox in the index below, update the
  "Last session" line, and note anything learned in DEV_NOTES (wrong-ports go in
  "THINGS I GOT WRONG").
- After ALL phases: Phase 11 closes out (test-coverage map, backlog triage, doc sweep).

### Model recommendation

Default for the whole audit: **Opus 4.8, medium effort.** Reading the source dominates
cost (identical at any effort), so medium is the best cost-per-correct-finding. Each
phase below has a **Model:** line; the security/analysis-heavy phases (1, 5, 9, 10) are
flagged "medium+ — push past the checklist for extra attack angles."

**At the start of every phase, the session must STOP and ask the user whether to keep
the recommended model/effort or change it — before reading any files.** Do not begin the
phase work until the user confirms.

**Last session:** 2026-06-11 — Upload service overhaul: removed 0x0.st (content policy conflict), added Postimages (API token) and Imgbox (username+password 2-step); all services now require account login. 4 upload backends, 16/16 upload tests + 7/7 prefs tests green. Dead ZeroXUploader files deleted. 45/48 green (3 pre-existing headless failures).

**Audit history (Phase 11 closeout):** All 11 phases done.
Test-coverage map vs Python below; 42 C++ test targets (broader than Python's 23) with known
gaps in comic-logic / DCC-loopback / input-bar (backlogged #13/#19). FINAL SUMMARY added
below the index. HANDOFF.md updated. Translations stay deferred (S14). Remaining work =
the 26 FIX BACKLOG items + 10 Python backports; nothing blocking.

Phase 10 done (Opus 4.8 medium+, security sweep). Redaction
matches Python exactly; chat-view HTML escaping solid (all via formatChatLine/insertText);
QSS colours safe (numeric rgb from QColor); settings robust; log/DCC paths safe; cppcheck
clean. 2 fixes: CR/LF stripped in sendRaw (line-injection defense-in-depth) + topic QLabel
forced to PlainText (rich-text injection). Passwords-plaintext = parity note. 43 green.
Next: Phase 11 (closeout).

Phase 9 done (Opus 4.8 medium+, hard SSRF push).
**Found + fixed two SSRF holes** the port had vs Python: (1) redirect targets weren't
re-validated (public host → 30x → 169.254.169.254/LAN was followed), (2) only the hostname
string was checked, not the resolved IP (DNS-to-private). Both now match Python (per-hop
re-check + resolve-and-check). The IP block list itself is thorough (metadata/CGNAT/0.0.0.0/
IPv6/decimal-IP all blocked — locked with new classifier tests). 42 green. Next: Phase 10.

Phase 8 done (Opus 4.8 low-med). **Log-path security is
solid** (safePathPart neutralizes separators/reserved/control chars/`..` per component — no
traversal via channel names). Flood guard exempts friends + self. Auto-replay on open
exists. 1 cheap fix: replay_lines=0 now means 50 (was 100000 → dumped the whole log every
open). Minor parity notes (strftime token subset, no replay dimming, logs keep mIRC codes)
backlogged/noted. 42 green. Next: Phase 9 (link previews / SSRF).

Phase 7 done (Opus 4.8 medium, audit + cheap wins).
Big surprise: S2/S3/S4/S5 were **largely stale** — tray icon, tray menu, minimize-to-tray,
taskbar flash, beep, OS notifications, DND, notify_pm/highlight, highlight_words, and Saved
Looks are all implemented. Genuine remaining gaps: real .wav playback (notify_sound only
beeps; CTCP SOUND receive not plumbed) and the update checker — both backlogged as builds.
1 cheap fix: highlight matching now strips mIRC codes (Python parity). 42 tests green. Next: Phase 8.

Phase 6 done (Opus 4.8 medium+, decoder-safety push).
**Found + fixed another real vuln**: the .avb/.bgb decoder trusted a file-supplied inflate
length → ~4GB OOM from a tiny hostile file (C++-only; Python immune). Also capped
dimensions + fixed abs(INT_MIN) UB. New comic_art_test (hostile inputs). The S10 comic
parity gaps (per-channel overrides, emotion wheel, assign dialog, random-bg logic, panel
menu) are all confirmed missing → backlogged as feature builds. 42 tests green. Next: Phase 7.

Phase 5 done (Opus 4.8 medium+, hard security pass).
**Found + fixed a real vuln**: DCC size-0/negative offers bypassed the receive byte cap →
unbounded disk write (C++ inverted Python's guard). Also hardened DCC tokens to a CSPRNG.
New dcc_manager_test (pure helper). 41 tests green. 4 lower items to backlog/backports.
Next: Phase 6 (comic).

Phase 4 (2026-06-10, Opus 4.8 medium). S8 DEBUNKED — IrcFormat.cpp
is a faithful full port (0–98 palette + 0x04 hex + all control codes, reverse-video and
parse edge cases match Python). ChatLineFormatter solid. 1 fix (Ctrl+O/R format keys),
1 backlog (Tab-completion cycling). All 40 green. Next: Phase 5 (DCC).

Phase 3 (2026-06-10, Opus 4.8 low-med). Settings layer is
solid: Python has 127 default keys (not 175 — inventory over-claimed), C++ 100; the
gaps are renames + comic keys (inline fallbacks, Phase 6) + migration flags (N/A for a
fresh port) + deferred features. 1 cleanliness fix (shortcuts/looks defaults). S5a
debunked (both default theme = synthwave), S11 resolved (shortcuts DO persist), S7
quantified. Authoritative key map below. All 40 tests green. Next: Phase 4.

Phase 2 (2026-06-10, bumped): $me/$chan aliases, slap/fish defaults, /sound; scripting
DEFERRED; S9 debunked; 4 backports. Phase 1 (Opus 4.8 medium): 5 protocol fixes, S6
resolved (no SCRAM).

### FINAL SUMMARY (audit complete 2026-06-10)

11/11 phases done. Headline: the port was in **better** shape than the inventory implied
(7 seed findings were stale/wrong — S4, S5, S5a, S6, S8, S9, S11), but careful reading +
adversarial security passes found **4 genuine, C++-only security vulnerabilities** plus a
batch of parity fixes.

**Real vulnerabilities fixed (all C++-only regressions; Python was safe):**
1. DCC `size=0` offer bypassed the receive cap → unbounded disk write (P5-1).
2. Comic `.avb/.bgb` decoder trusted a file-supplied inflate length → ~4GB OOM (P6-1).
3. Link-preview SSRF via redirect — targets weren't re-validated (P9-1).
4. Link-preview SSRF via DNS — only the hostname string was checked, not resolved IPs (P9-2).

**Other security hardening:** CTCP reply rate-limit (P1-5), DCC token CSPRNG (P5-2), comic
dim/UB caps (P6-2/3), CR/LF line-injection strip (P10-1), topic rich-text → PlainText (P10-2).

**Parity/correctness fixes:** IRCv3 line caps (P1-10), numeric fallthrough + 005 (P1-2/3),
CTCP VERSION string (P1-4), `$me`/`$chan` aliases (P2-2), slap/fish defaults (P2-6), `/sound`
(P2-1), shortcuts/looks defaults (P3-1), Ctrl+O/R (P4-1), highlight formatting-strip (P7-1),
replay_lines default (P8-1).

**Verified clean (no fix needed):** redaction, chat-view HTML escaping, QSS color injection,
log-path traversal, settings robustness, cppcheck.

**Outstanding (non-blocking):** 26 FIX BACKLOG items (feature builds: sounds, update checker,
comic extras, scripting; + low-pri hardening/tests), 10 Python backports (`../maxchat/DEVDOCS/
BACKPORTS.md`), translations deferred (S14). Test suite 42/42 green throughout.

### Phase index

- [x] Phase 1 — IRC protocol parity ✅ 2026-06-10
- [x] Phase 2 — Slash commands & aliases ✅ 2026-06-10
- [x] Phase 3 — Settings keys & preferences ✅ 2026-06-10
- [x] Phase 4 — Chat rendering & input ✅ 2026-06-10
- [x] Phase 5 — DCC (parity + security) ✅ 2026-06-10
- [x] Phase 6 — Comic mode (parity + decoder robustness) ✅ 2026-06-10
- [x] Phase 7 — Themes, fonts, notifications, tray, sounds ✅ 2026-06-10
- [x] Phase 8 — Logging, replay, buffers ✅ 2026-06-10
- [x] Phase 9 — Link previews & SSRF ✅ 2026-06-10
- [x] Phase 10 — Cross-cutting security sweep ✅ 2026-06-10
- [x] Phase 11 — Tests & docs closeout ✅ 2026-06-10  → **AUDIT COMPLETE**

---

## Seed findings (from 2026-06-10 exploration — verify in the named phase, don't re-discover)

| # | Sev | Area | Issue | Phase | Status |
|---|-----|------|-------|-------|--------|
| S1 | MISS | Scripting | Python plugin API not ported. **DECISION 2026-06-10: use embedded Lua 5.4** (was deferred). API contract + 3 example scripts converted (SCRIPTING.md, assets/scripts/*.lua); the C++ Lua host (embed+hooks+sandbox) is backlog #6. | 2 | IN PROGRESS |
| S2 | MISS | Update checker | Confirmed absent (Phase 7). Python has Help ▸ Check for Updates + quiet startup check (GitHub releases). → Backlog #20 (confirm repo URL at build time; HTTPS-only, no auto-download). | 7 | Backlog #20 |
| S3 | MISS | Sounds | Partially valid (Phase 7): notify_sound only **beeps** (no notify.wav playback), and **CTCP SOUND receive isn't plumbed** (IrcSession emits no sound signal). `/sound` send was added in Phase 2. → Backlog #21 (notify.wav player) + #22 (CTCP SOUND receive+play, own-file-only). | 7 | Backlog #21-22 |
| S4 | MISS | Notifications | **MOSTLY STALE (debunked Phase 7):** taskbar flash (QApplication::alert), tray icon + context menu (Show/Hide, DND, Quit), minimize_to_tray, OS notifications (showMessage), DND, notify_pm/notify_highlight, beep_highlight, and highlight_words are ALL implemented. Only fix: highlight matching now strips mIRC codes (P7-1). | 7 | VERIFIED |
| S5 | MISS | Themes | DEBUNKED (Phase 7): "Saved Looks" IS implemented (View ▸ Saved Looks, Save Current Look, rebuildLooksMenu, apply). S5a default-theme mismatch already closed Phase 3 (both = synthwave). | 7 | VERIFIED |
| S6 | BUG? | SASL | ~~Python does PLAIN only; C++ claims PLAIN + SCRAM-SHA-256.~~ RESOLVED 2026-06-10: no SCRAM exists in src/ — the inventory over-claimed. C++ does PLAIN only, matching Python. No action. | 1 | VERIFIED |
| S7 | MISS | Settings | ~~175 vs ~70~~ QUANTIFIED Phase 3: Python has **127** default keys, C++ **100**. Gap = renames (2) + comic_* (16, inline fallbacks → Phase 6) + migration flags (3, N/A for fresh port) + deferred features (update_check/seeded_scripts/nick_width_autoset) + legacy notify_method. No data-loss bug. Map in Phase 3 section. | 3 | VERIFIED |
| S8 | BUG? | Rendering | ~~colors 16–98 + hex likely missing~~ DEBUNKED 2026-06-10: IrcFormat.cpp has the full 0–98 palette (identical RRGGBB), the 0x04 hex code, and all control codes (bold/italic/underline/strike/reverse/mono/reset). Reverse-video swap + color-parse edge cases match Python. No bug. | 4 | VERIFIED |
| S9 | MISS | Commands | ~~~25 Python commands likely missing~~~ MOSTLY DEBUNKED 2026-06-10: the inventory was wrong — /raw /quote /oper /kill /wallops /ns /cs /ms /identify /ghost /alias /unalias and unknown→raw passthrough all already exist in CommandParser. Only **/sound** was genuinely missing (now FIXED). C++ is in fact a superset (adds /help /close /disconnect /reconnect /connect /server /notify etc.). | 2 | VERIFIED |
| S10 | MISS | Comic | Confirmed Phase 6, all still missing → backlog #14-18: per-channel overrides (comic_channels absent), emotion wheel (openEmotionPicker is a stub), assign-character dialog from member menu (only global text list), comic_random_bg (dead checkbox — stored, not consumed), panel right-click menu (ComicView has none). | 6 | Backlog #14-18 |
| S11 | BUG | Shortcuts | ~~Rebinding not persisted~~ RESOLVED in code (Phase 3): `shortcuts` is saved via saveRaw (MainWindow.cpp:5278), re-applied on startup (applyNavShortcutOverrides @ 6653) and after edit (5282). Was stale. A round-trip regression test would lock it in (Backlog #7). | 3 | VERIFIED |
| S12 | MISS | DCC UI | Transfers dialog lacks Rate/ETA columns (Python has live rate + ETA). Confirmed Phase 5; cosmetic. | 5 | Backlog #12 |
| S13 | MISS | Input | Image-paste hook (Python emits imagePasted for a script to upload+link). C++ input is a QTextEdit with acceptRichText(false) → pasted images are ignored (no crash). Nothing to wire until scripting exists. | 4 | DEFERRED (with S1) |
| S14 | MISS | Localization | Translations deferred by user decision: tr() wrapping incomplete, no .qm files. Loader + Localization page exist. NOT an audit failure — track only. | 11 | WONTPORT (deferred) |

---

## Phase 1 — IRC protocol parity

**Model:** Opus 4.8 medium+ (state-machine tracing rewards deeper reasoning).

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P1-10 | BUG (med-high) | IrcConnection.cpp queueIncomingLines/onReadyRead | Incoming-line cap reused the 512-byte **send** cap (IrcMaxWireBytes) and pending buffer was 4096. IRCv3 tagged lines (server-time/account-tag) routinely exceed 512 → legit messages dropped, and a long in-progress line >4096 aborted the connection. Python uses 8192 line / 65536 buffer. | **FIXED** — added MaxIncomingLineBytes=8192, MaxPendingBytes=65536. Covered by irc_connection_test (green). |
| P1-3 | BUG (med) | IrcSession.cpp handleLine | No generic numeric fallthrough — every numeric without an explicit handler was silently dropped (LUSERS 251-255, 250, 265/266, 396, server errors 462/465/491/484, etc.). Python surfaces all unknown numerics as status text. | **FIXED** — added catch-all emitting systemText. Test: unhandledNumericsAreSurfacedAsStatusText. |
| P1-2 | DIFF (low) | IrcSession.cpp 005 handler | 005/ISUPPORT returned early; Python stores tokens AND falls through to display the line. | **FIXED** — 005 now falls through to the numeric catch-all. Test: isupportLineIsAlsoShownAsStatusText. |
| P1-5 | SEC (med-low) | IrcSession.cpp CTCP block | No CTCP auto-reply rate-limit — answered every VERSION/PING/TIME 1:1, usable as a CTCP flood reflector. Python throttles to 1/sec. | **FIXED** — added ctcpReplyTimer_ 1s throttle (ACTION/DCC/NOTICE excluded). Test: ctcpAutoRepliesAreRateLimited. |
| P1-4 | DIFF (low) | IrcSession.cpp VERSION reply | Hardcoded "MaxChat C++" with no version; Python uses app name + version. | **FIXED** — uses app::displayName()+version(). Existing test updated to build expected from AppInfo. |
| P1-1 | DIFF (low) | IrcMessage.cpp parseMessage | QString::trimmed() on the whole line discards trailing whitespace in the trailing param; Python only strips \r\n (keeps trailing spaces verbatim). Rarely matters. | BACKLOG (#1) |
| P1-6 | DIFF (low) | IrcSession.cpp CTCP NOTICE reply | Doesn't compute CTCP PING reply round-trip time; Python shows "%.3fs". | BACKLOG (#2) |
| P1-8 | DIFF (low) | IrcConnection.cpp connectTo (proxy) | No proxy port-range validation (casts to quint16); Python errors on invalid port. | BACKLOG (#3) |
| P1-9 | DIFF (low) | IrcConnection.cpp connectTo (proxy) | Unknown proxy type silently ignored (connects directly); Python raises "Unsupported proxy type". | BACKLOG (#3) |
| P1-11 | DIFF (low-med) | IrcConnection.cpp queueIncomingLines | Drains all buffered lines synchronously per readyRead; Python throttles to 100 lines/tick with deferred draining to keep UI responsive under floods/netsplits. | BACKLOG (#4) |
| P1-12 | DIFF (low) | IrcSession.cpp AWAY handler | Emits a readable "[away] X is away/back" replyText for **every** away-notify in addition to awayChanged; Python emits only the signal. With away-notify on a busy channel this could spam the active buffer. Verify UI side. | BACKLOG (#5) |
| P1-7 | DIFF (none) | IrcConnection.cpp | Connect+registration timeouts 15s each vs Python 20s watchdog. Acceptable (arguably better split design). No action. | NOTED |

Checklist status: message parsing ✓ (one whitespace diff P1-1); CAP/SASL ✓ (S6 resolved
— PLAIN only); registration/nick-collision/alt-nick ✓ (exact match); watchdog/failover ✓
(IrcConnection split timeouts + ReconnectPlanner ServerRetryLimit=3, P1-7 noted); CTCP
replies ✓ (P1-4/P1-5 fixed; SOUND not handled → seed S3, Phase 7); numerics ✓✓ (P1-3 the
big fix; C++ also richer on 301/305/306/307/313/328/331/333/671); ISUPPORT ✓ (P1-2);
away-notify/ISON ✓ (P1-12 noted); proxy ✓ (P1-8/P1-9 minor); TLS cert exception ✓.

---

## Phase 2 — Slash commands & aliases

**Model:** Opus 4.8 low–medium (mechanical diff against a clear checklist).

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P2-2 | MISS (med) | CommandAlias.cpp applyTemplate / MainWindow call site | Alias templates supported only `$*` `$N` `$N-`; **`$me` and `$chan` were missing** (and `expandCommandAliases` had no nick/channel context). Python supports both. | **FIXED** — added `$me`/`$chan`, threaded selfNick+currentChannel through. Test: substitutesMeAndChanPlaceholders. |
| P2-6 | MISS (med) | CommandAlias.cpp defaultCommandAliases | C++ shipped j/p/w; Python ships **slap** + **fish** (classic fun aliases using `$1-`). Fresh C++ installs lacked them. | **FIXED** — added slap+fish (kept j/p/w as extras). Test: shipsClassicFunAliases. |
| P2-1 | BUG/MISS (med) | CommandParser.cpp + MainWindow dispatch | `/sound` was unrecognized → fell through to raw passthrough, sending `SOUND …` to the server (now visible as a 421 after the Phase-1 numeric fix). Python sends a CTCP SOUND to the room + plays locally. | **FIXED** — parse `/sound <file> [text]`, send CTCP SOUND to the current buffer. Local playback (self + received) deferred to Phase 7 (S3). Test: soundCommandTargetsCurrentBufferAndNeedsFile. |
| P2-3 | DIFF (low) | CommandAlias.cpp applyTemplate | When a template has no placeholder, C++ appends the args (mIRC-style); Python drops them. Intentional + tested (appendsArgumentsWhenTemplateHasNoPlaceholder). | NOTED — backport candidate BP-7. |
| P2-4 | MISS | CommandParser/MainWindow | Scripting commands (/scripts /load /unload /reload) are placeholders — the Python plugin API isn't ported. | DEFERRED (S1, user decision). Backlog #6. |
| P2-5 | DIFF | CommandParser.cpp | C++ is a **superset** of Python's commands: adds /help /? /close /disconnect /reconnect /connect /server /notify /unnotify /list (dialog) /q /m and os/hs service shortcuts. | NOTED — backport candidates BP-4/5/6. |
| P2-8 | DIFF (none) | CommandParser.cpp | `umite` ships as a typo-tolerant alias for `unmute`. Harmless; intentional. | NOTED. |

Checklist status: full command diff done — Python ~50 dispatch arms vs C++ ~60+ parser
arms; C++ is a superset, only `/sound` was missing (fixed). Unknown→raw passthrough ✓
(both send the slash-stripped line). Alias substitution ✓✓ ($me/$chan was the gap, fixed;
$*/$N/$N- already worked; recursion guard via `seen` set + maxDepth=8 ✓). Default aliases ✓
(slap/fish added). Per-command arg/usage behavior spot-checked (msg/nick/whois/kick/ban/
mode/topic) — matches Python's "needs rest" guards. Services passwords redacted in the
ServiceMessage path ✓ (full check is Phase 10).

---

## Phase 3 — Settings keys & preferences

**Model:** Opus 4.8 low–medium (key-by-key mapping; lots of output, little reasoning).

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

### Python→C++ settings key map (authoritative, 2026-06-10)

Python DEFAULTS = **127** keys; C++ defaultSettings() = **100** (102 after this phase's
shortcuts/looks add). **~95 keys are present in both with matching names AND default
values** (theme, all fonts, timestamps, scrollback=2000, flood 10/4, paste_lines=4,
notify_* corner=br/duration=6, dcc_*, replay_lines=0, nick_width=16, the 7 per-area
colors, content_services all-true, the boolean toggles, etc.) — those are not listed
individually. Only the non-trivial keys are reconciled below.

| Python key | C++ key | Status | Note |
|---|---|---|---|
| `aliases` | `command_aliases` | RENAMED | Same semantics (slap/fish now in both — Phase 2). |
| `muted_buffers` | `muted_channels` | RENAMED | Same "net/#chan" key format. Verify format in Phase 8. |
| `comic_*` (16: art_dir, bg, panels, per_panel, min_font, chars, ignore, ignore_cmds, bot_patterns, exclude_regex, captions, caption_mode, caption_color, caption_scale, random_bg, self_char) | (none in defaults) | INLINE FALLBACK | Read via `settings.value(key, fallback)` scattered in code, not centralized. Works, but defaults aren't in one place. Reconcile values in **Phase 6**. |
| `comic_channels` | (absent) | MISSING | Per-channel overrides not implemented → S10, Phase 6. |
| `comic_patterns_migrated` | (absent) | N/A | One-time Python migration flag; a fresh C++ port needs no migration. |
| `font_defaults_version` | (absent) | N/A | Python migration flag (re-apply bundled font profile once). N/A. |
| `logging_for_replay_migrated` | (absent) | N/A | Python migration flag. N/A. |
| `nick_width_autoset` | (absent) | MISSING (low) | The "auto-fit nick column once" feature isn't ported. Minor — Backlog #8. |
| `update_check` | (absent) | MISSING | Update checker not built → S2, Phase 7. |
| `seeded_scripts` | (absent) | DEFERRED | Scripting deferred (S1). |
| `notify_method` | (absent) | DROPPED | Legacy/superseded key Python keeps for back-compat; intentional to drop. |
| `shortcuts` | `shortcuts` | OK (fixed) | Was read/written but absent from defaults; **added this phase**. |
| `looks` | `looks` | OK (fixed) | Same — **added this phase**. |
| — | `networks_merge_version` | C++-ONLY | Internal network-merge versioning. Fine. |

### Findings — Phase 3

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P3-1 | DIFF (low) | SettingsStore.cpp | `shortcuts` and `looks` were read/written but had no central default entry (relied on empty-map fallback). | **FIXED** — added both as `{}` defaults. settings_store_test green. |
| P3-2 | DIFF (low) | comic_* settings | 16 comic defaults live as scattered inline fallbacks, not in defaultSettings(). Works; reconcile in Phase 6. | NOTED → Phase 6 |
| P3-3 | MISS (low) | nick_width_autoset | One-time nick-column auto-fit not ported. | Backlog #8 |
| (S5a) | — | theme default | Debunked: Python default theme is "synthwave", same as C++. | CLOSED |
| (S11) | — | shortcuts | Debunked: shortcuts persist + re-apply on startup. | CLOSED |

Checklist status: full 127-key map produced ✓; default VALUES verified for the shared
keys (all match) ✓; malformed settings.json → pure defaults (loadRaw returns {} on parse
error) ✓; atomic write via QSaveFile ✓; import/export merge preserves user network fields
(NetworkImport overlays imported onto catalog base, keeps catalog fields fresh) ✓;
shortcuts round-trip ✓ (S11); no settings-layer data-loss bugs. Prefs-dialog
page-behavior spot-check deferred to where each feature is audited (Fonts→Phase 7,
DCC→Phase 5, etc.).

---

## Phase 4 — Chat rendering & input

**Model:** Opus 4.8 medium.

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P4-1 | MISS (low) | MainWindow.cpp input shortcuts | C++ wired Ctrl+B/I/U/K but not **Ctrl+O (reset 0x0F)** and **Ctrl+R (reverse 0x16)** that Python's FORMAT_KEYS has. | **FIXED** — added both. |
| P4-2 | DIFF (med) | MainWindow.cpp completeInput | Tab completion always picks the **first** matching candidate; Python's `_complete` **cycles** through all matches on repeated Tab/Backtab. | Backlog #9 |
| P4-3 | DIFF (low) | MainWindow input widget | C++ input is a multi-line QTextEdit; Python is a single-line QLineEdit. Paste guard therefore triggers on **send** (m_pasteGuard @ submit) rather than on **paste** (Python intercepts in insertFromMimeData). Functionally equivalent, different trigger point. | NOTED |
| P4-4 | DIFF (none) | MainWindow.cpp addInputHistory | History capped at 200 entries; Python's grows unbounded. Acceptable/better. | NOTED |
| (S8) | — | IrcFormat.cpp | Debunked — faithful full color/code port. | CLOSED |
| (S13) | — | input image paste | Deferred with scripting (S1); QTextEdit ignores image paste, no crash. | DEFERRED |

Checklist status: color codes ✓✓ (S8 — full 0–98 + hex + all control codes, values match);
bg colors / reverse-video / reset / mono ✓ (exact parity); strip-on-copy ✓ (ChatLineFormatter
plainText path + strip_color_copy); nick column right-align + width clamp + hang-indent ✓;
timestamps ✓ (token format handled at render); nick coloring + override precedence + bracket
tint ✓; tab completion present (P4-2 cycling gap); history ✓; Ctrl+B/I/U/K ✓ + O/R fixed;
paste guard ✓ (P4-3 trigger-point diff); spellcheck highlighter present. Emoji-fallback font
is a Phase 7 (fonts) concern. HTML-escaping of nick/text verified inline — full sweep Phase 10.

---

## Phase 5 — DCC (parity + security)

**Model:** Opus 4.8 medium+ (security review — invent attacks beyond the checklist).

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P5-1 | **SEC (med)** | DccManager.cpp beginReceive | **Unbounded disk write.** Byte cap was guarded `if (tr->size > 0)`, so a peer offering size 0 (or negative) bypassed it entirely — every byte streamed was written to disk, completion never fired, disconnect reported success. Python drops data when `remaining <= 0`. Remote-triggerable with accept policy `all`/trusted (no click). | **FIXED** — `dccWritableChunk()` helper caps to remaining and returns 0 for zero/negative/complete; offered size clamped >=0. Test: dcc_manager_test (3 cases). DEV_NOTES logged. |
| P5-2 | SEC (low) | DccManager.cpp newToken | DCC passive/chat tokens used `QRandomGenerator::global()` (non-crypto PRNG); the token is the only barrier against a third party injecting a fake passive reply (→ file misdelivery). Python uses `secrets`. | **FIXED** — switched to `QRandomGenerator::system()` (CSPRNG). |
| P5-7 | OK | DccManager.cpp inSend/destPath | Path-traversal: filenames are reduced via `QFileInfo::fileName()` on receive (strips `/` on Linux, `/`+`\` on Windows) and dest is `QDir::filePath(base)`. Adequate + parity with Python's `os.path.basename`. Residual: a literal `\`-containing name on Linux or control chars yield an ugly-but-contained filename in the download dir (no traversal). | VERIFIED (no fix) |
| P5-4 | DIFF (low) | inResume/inAccept | RESUME/ACCEPT `pos` is trusted without validating against file size (sender seeks past EOF → sends nothing; receiver could get a sparse/corrupt file if the sender lies). Python has the same non-validation. Corruption, not a security hole. | NOTED — backport BP-8 (BOTH) |
| P5-5 | SEC (low) | inSend / inChat | No cap on the number of pending incoming offers — a peer can spam DCC SEND/CHAT to grow transfer records/sockets (UI/memory DoS). Python is also uncapped. | Backlog #10; backport BP-9 (BOTH) |
| P5-3 | DIFF (low) | offerChat / inChat | Passive CHAT tokens never expire; Python expires `_chat_awaiting` after 120s + drops on close. | Backlog #11 |
| P5-6 | DIFF (none) | setAcceptPolicy | "trusted" matches by lowercased nick only (spoofable on IRC). Same as Python's design. | NOTED |
| S12 | MISS (low) | DccTransfersDialog | No Rate/ETA columns (Python shows live rate + ETA via an EMA tick). | Backlog #12 |
| — | TEST | tests/ | Only the `dccWritableChunk` helper is unit-tested; there's no loopback test of the SEND/GET/RESUME/CHAT handshakes (Python has test_dcc.py). | Backlog #13 |

Checklist status: all handshakes traced (active/passive SEND+GET, RESUME offset, token echo,
ack wraparound `& 0xFFFFFFFF`, destPath collision/resume rules, accept policy, CHAT 8192/65536
caps) — all match Python. Security pass found one real vuln (P5-1, fixed) + token hardening
(P5-2, fixed); traversal adequate (P5-7); pos-validation, flood-cap, chat-token-expiry are
parity gaps shared with Python (backlog/backport). Resume-overwrite: auto-resume keys on
name+size, can resume into a coincidentally-named partial — parity with Python, low.

---

## Phase 6 — Comic mode (parity + decoder robustness)

**Model:** Opus 4.8 medium.

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P6-1 | **SEC (med)** | ComicArt.cpp inflateDib | **OOM from a hostile art file.** `origLen` (attacker-controlled DIB field) is prefixed for `qUncompress`, which allocates it up front → a tiny file claiming `origLen=4GB` OOMs. Python uses `zlib.decompress` (ignores the length field) and is immune. | **FIXED** — cap origLen ≤ 32MB. Test: comic_art_test::hugeOrigLenDoesNotAllocate. |
| P6-2 | SEC (low-med) | ComicArt.cpp inflateDib | `w`/`h` unbounded → `stride = w*2` int overflow + huge `QImage` alloc. | **FIXED** — cap both dims ≤ 4096. Test: absurdDimensionsRejected. |
| P6-3 | BUG (low) | ComicArt.cpp inflateDib | `std::abs(static_cast<int>(h))` is UB when h = INT_MIN. | **FIXED** — height magnitude computed in qint64. |
| P6-4 | MISS | MainWindow / ComicSettingsDialog | `comic_random_bg` is a **dead checkbox** — saved to settings but `comicBackground()` never consults it (no stable per-channel random selection). | Backlog #17 |
| S10a | MISS | comic engine | Per-channel overrides (`comic_channels`: bg/chars/ignore) entirely absent. | Backlog #14 |
| S10b | MISS | openEmotionPicker | Emotion wheel is a "planned" stub; auto-emotion-from-text only. | Backlog #15 |
| S10c | MISS | member context menu | No assign-character dialog from a nick; only the global nick=stem text box in Comic Settings. | Backlog #16 |
| S10e | MISS | ComicView | No panel right-click menu (Copy/Save panel). | Backlog #18 |
| — | TEST | tests/ | Before this phase there were **no comic tests**; added comic_art_test (decoder robustness). Renderer/emotion-guess/bot-filter logic still untested (ported under tasks #25/#29). | Backlog #19 |

Checklist status: decoder robustness ✓✓ (the phase focus — every offset bounds-checked;
the chunk-table loop always progresses or breaks, no infinite loop; per-cell offset/dim
guards present; the OOM + overflow + UB holes fixed and tested). Parity: the renderer,
emotion-guessing table, and bot-pattern/ignore/exclude filtering were ported (tasks
#25/#29) and read as faithful; the five S10 feature gaps are confirmed missing and
backlogged (they're UI/plumbing builds, not in-scope for an audit pass). Save-comic PNG
works.

---

## Phase 7 — Themes, fonts, notifications, tray, sounds

**Model:** Opus 4.8 medium (real implementation work: sounds, tray, update checker).

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P7-1 | BUG (low) | MainWindow.cpp textHighlightsMe | Highlight matching ran against raw text incl. mIRC codes; Python strips formatting first, so a colour/bold code by your nick could hide a highlight. | **FIXED** — strip via stripFormatting() before matching (parity). |
| S3a | MISS | notification path | `notify_sound` only calls `QApplication::beep()`; Python plays `<config>/sounds/notify.wav` (user's, else bundled) via QSoundEffect. Qt6::Multimedia is already linked. | Backlog #21 |
| S3b | MISS | IrcSession / MainWindow | CTCP SOUND **receive** isn't plumbed — IrcSession emits no sound signal (SOUND falls through as a generic CTCP, Phase 1), so sounds others send never play. Python's sounds.resolve() is own-file-only (basename, .wav, must exist locally) — port that safety. | Backlog #22 |
| S2 | MISS | — | Update checker absent (Help menu + optional quiet startup, pref `update_check`). | Backlog #20 |
| S4 | OK | MainWindow.cpp | Tray icon + menu, minimize_to_tray, taskbar flash, beep, OS notifications, DND, notify_pm/highlight, highlight_words all implemented. | VERIFIED |
| S5 | OK | MainWindow.cpp | Saved Looks implemented (menu + Save Current Look + apply). | VERIFIED |

Checklist status: themes/chat-themes/wallpaper/fonts/per-area colours were verified in earlier
phases + tasks #17-21 and read as complete; default theme = synthwave both sides (S5a closed).
Notifications subsystem is far more complete than the seed implied — flash/beep/tray/OS-toast/
DND/per-event gating/highlight all present; the only fix was the highlight formatting-strip
(P7-1). Genuine remaining builds: real .wav playback (notify_sound + CTCP SOUND receive) and
the update checker — backlogged. cheap-wins directive satisfied (P7-1); the rest are true
feature builds, not cheap.

---

## Phase 8 — Logging, replay, buffers

**Model:** Opus 4.8 low–medium.

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P8-1 | DIFF (low-med) | MainWindow.cpp replayCurrentLog | `replay_lines == 0` replayed up to 100000 lines (effectively the whole log) on every buffer open; Python treats 0 as 50. | **FIXED** — 0 → 50 (DefaultReplayLines). |
| P8-2 | DIFF (low) | ChatLogStore.cpp logFilePath | Only `%Y %m %d` date tokens are substituted; Python runs the mask through `strftime`, so any other code (`%H %b %A %j` …) works there but is left literal here. | Backlog #23 |
| P8-3 | DIFF (low) | MainWindow.cpp replay | Replayed lines aren't dimmed (Python dims past messages); C++ uses `--- Log replay ---` header/footer instead of an "Ended <date>" divider. Cosmetic. | Backlog #24 |
| P8-4 | DIFF (low) | logging | C++ logs raw text **with** mIRC codes (so replay re-renders colour); Python logs stripped plain text. Trade-off: colourful replay vs clean log files (control chars in logs). | NOTED |
| SEC | OK | ChatLogStore.cpp safePathPart/logFilePath | **No log-path traversal**: separators, Windows-reserved chars, control chars (`\x00-\x1f`, incl. CR/LF/NUL) and leading dots are stripped, and each mask component skips `.`/`..`. A channel named `#../../x` can't escape the log dir. | VERIFIED |

Checklist status: log mask %network/%channel + subfolders ✓ (date-token subset, P8-2);
log files daily-per-target ✓; replay auto-on-open (gated on replay_log) + manual Tools action
✓; replay_lines default fixed (P8-1); scrollback cap + keep-on-part in ChatBufferStore
(ported, tasks done); muted buffers suppress notify but still log ✓; FloodGuard thresholds +
window + friends/self exemption ✓; CR/LF log injection not reachable (IRC lines pre-split on
\r\n; appendLine trims) ✓; **path traversal defended** ✓ (the security highlight — done right,
unlike the DCC/comic length-trust bugs).

---

## Phase 9 — Link previews & SSRF

**Model:** Opus 4.8 medium+ (highest-risk phase — the redirect-revalidation item).

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P9-1 | **SEC (med-high)** | OpenGraphFetcher.cpp | **SSRF via redirect.** `NoLessSafeRedirectPolicy` only blocks https→http; the final `reply->url()` was never re-checked, so a public host could 302→`http://169.254.169.254/` (cloud metadata) or a LAN IP and we'd fetch it. Python re-validates every hop. | **FIXED** — connect `redirected`, abort on a disallowed target ("blocked redirect"). |
| P9-2 | **SEC (med)** | OpenGraphFetcher.cpp | **SSRF via DNS.** The guard checked the hostname *string* only — a public-looking domain whose A record resolves to a private/loopback IP passed. Python resolves (getaddrinfo) and rejects any private IP. | **FIXED** — `resolvesToPublicOnly()` (QHostInfo) re-checks every resolved address against the IP block list; can't-resolve → blocked. |
| P9-3 | SEC (low) | MainWindow.cpp preview / QTextBrowser | DirectImage previews are SSRF-checked at **classification** (string check blocks private-IP literals), but the image the chat view loads isn't resolve-checked or redirect-guarded. Lower risk: display-only, private literals already blocked, body not read into logic. | Note → Phase 10 |
| P9-4 | PERF (low) | OpenGraphFetcher.cpp | `resolvesToPublicOnly` does synchronous DNS on the GUI thread (parity with Python's blocking getaddrinfo); slow DNS could stall the UI. | Backlog #25 |
| — | OK | LinkPreviewClassifier.cpp | Block list thorough: 0/8, 10/8, 100.64/10 CGNAT, 127/8, 169.254 metadata, 172.16/12, 192.168, RFC-test nets, ≥224 multicast; all IPv6 blocked (colon); dotless-decimal IP rejected; credentials rejected; data:/ftp:/file: + SVG rejected. Locked by classifier test `blocksSsrfSensitiveIpForms`. | VERIFIED |
| — | OK | OpenGraphFetcher / LinkPreviewRenderer | Size cap (256KB), timeout (10s), content-type HTML check; OG metadata (title/desc/domain/url) all `toHtmlEscaped`; og:image re-fetch goes back through classify→fetch (SSRF-checked); content_services gates each type. | VERIFIED |

Checklist status: redirect re-validation ✓ (P9-1 fixed); resolve-and-check ✓ (P9-2 fixed);
IP block-list completeness ✓✓ (incl. metadata/CGNAT/IPv6/decimal — tested); credentials,
scheme, SVG rejection ✓; size/timeout caps ✓; metadata escaping ✓; per-type gating ✓.
Both SSRF holes were C++-only regressions (Python had both protections) — NOT backports.
Residual: image-path resolve/redirect (P9-3) + sync-DNS UI stall (P9-4) noted/backlogged;
fetch-storm cap (many URLs in one line) shared with Python, low — note only.

---

## Phase 10 — Cross-cutting security sweep

**Model:** Opus 4.8 medium+ (cheap phase to read, but extra thinking finds the most here).

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

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| P10-1 | SEC (low-med) | IrcSession.cpp sendRaw | No CR/LF stripping → a lone `\r` mid-message (CR-only paste survives the per-line split + trim) could split a second IRC command onto the wire. Input-path splitting mostly prevented it, but defense-in-depth belongs at the send choke point. | **FIXED** — strip `\r`/`\n` in sendRaw (+redact the cleaned line). Test: sendRawStripsEmbeddedCrlfToPreventInjection. Backport BP-10 (BOTH). |
| P10-2 | SEC (low-med) | MainWindow.cpp topic label | Topic bar QLabel used default AutoText, so a channel TOPIC with `<img src=…>`/markup rendered as rich text. | **FIXED** — `setTextFormat(Qt::PlainText)`. |
| — | OK | IrcRedaction.cpp | Redaction matches Python exactly: PASS, AUTHENTICATE (except +/PLAIN/EXTERNAL), and services IDENTIFY/REGISTER/GHOST/RECOVER/RELEASE/SIDENTIFY/LOGIN. Applied to both sent + received raw-log lines. | VERIFIED |
| — | OK | chat view paths | No HTML injection: every untrusted string reaches the QTextBrowser via formatChatLine (toHtmlEscaped / stripFormatting+escape) or insertText; preview HTML escapes OG fields (Phase 9); about dialog escapes; tooltips are app-generated static. | VERIFIED |
| — | OK | ThemeCatalog.cpp | No QSS injection: theme colours are emitted as numeric `rgb()/rgba()` from QColor (cssRgb/cssRgba/borderColor), so a malicious JSON colour string can't break out of the stylesheet. | VERIFIED |
| — | OK | SettingsStore.cpp | Malformed settings.json → loadRaw returns {} → pure defaults; atomic write via QSaveFile (Phase 3). | VERIFIED |
| P10-3 | DIFF (note) | settings | Passwords stored plaintext in settings.json — parity with Python (only over TLS by default). Not a regression. Optional future: OS keychain. | NOTED |
| P10-4 | SEC (low) | ThemeCatalog.cpp effectiveWallpaperPath | The user-chosen wallpaper path is interpolated into QSS `url("…")` unquoted-escaped; a path containing `"` could inject QSS. User-controlled (own file), not remote → very low. | Backlog #26 |
| — | OK | — | cppcheck (warning,portability) clean on the security-relevant files; build warning-clean. The "trust attacker-supplied length/target" pattern (the root of the DCC/comic/SSRF bugs) swept: only the comic qUncompress site uses an external size, and it's capped (Phase 6). | VERIFIED |

Checklist status: redaction ✓✓; HTML-injection ✓ (chat view, previews, tooltips, about);
QSS-injection ✓; settings robustness ✓; CR/LF outbound ✓ (P10-1 fixed); topic rich-text ✓
(P10-2 fixed); untrusted file paths ✓ (DCC Phase 5, log Phase 8, sounds own-file-only spec
in backlog #22); password-plaintext = parity note; integer/length-trust pattern swept (DCC/
comic/IRC caps all closed in earlier phases); cppcheck clean. P9-3 (image-path resolve/
redirect) re-noted here — lower risk, display-only.

---

## Phase 11 — Tests & docs closeout

**Model:** Opus 4.8 low (mostly bookkeeping).

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
- [ ] Reconcile the **Backports to Python** section here against the canonical
      `../maxchat/DEVDOCS/BACKPORTS.md` — confirm both list the same entries.
      (Note: `BACKPORTS.md` lives in the Python repo's gitignored `/DEVDOCS/` —
      intentionally local-only, "never pushed". It's durable on disk; that's the
      to-do log for the Python build.)
- [ ] **Release hygiene (before any public push of maxchat-c):** `AUDIT.md` and
      `DEV_NOTES.md` are currently tracked. Per the internal-docs-stay-local rule,
      add them to `.gitignore` and `git rm --cached` them (and confirm no AI
      attribution ships) before the repo goes public.

### Python→C++ test-coverage map

C++ has **42** test targets vs Python's **23** — broader (dedicated dialog tests Python
lacks). Mapping Python → C++ and the genuine gaps:

| Python test | C++ coverage | Status |
|---|---|---|
| test_irc_parse | irc_message_test | ✓ |
| test_irc_format | irc_format_test | ✓ |
| test_client | irc_session_test + irc_connection_test | ✓ |
| test_redact | irc_redaction_test | ✓ |
| test_reconnect_failover | reconnect_planner_test | ✓ |
| test_network_import | network_import_test | ✓ |
| test_chat_view | chat_line_formatter_test | ✓ |
| test_logmask | chat_log_store_test | ✓ |
| test_media | open_graph_fetcher_test + link_preview_classifier_test (incl. SSRF) | ✓ |
| test_theme_platform | theme_catalog_test | ✓ |
| test_font_defaults | settings_store_test | ✓ |
| test_main_window_routing / _startup / _smoke | main_window_link_preview_test + command_parser_test | ✓ (partial) |
| test_dcc | dcc_manager_test (pure helper only) | GAP — loopback handshakes (backlog #13) |
| test_comic_filter / test_comic_speech / test_character_cells | comic_art_test (decoder safety only) | GAP — filter/emotion/cell-layout (backlog #19) |
| test_input_bar | (input lives in MainWindow; spellcheck_highlighter_test only) | GAP — history/completion (tie to backlog #9) |
| test_proxy | (proxy inline in IrcConnection) | GAP — add with backlog #3 |
| test_autoconnect | connection_plan_test (partial) | minor gap |
| test_ctcp_sound | — | N/A (CTCP SOUND not implemented; backlog #22) |
| test_scripting | — | N/A (scripting deferred, S1) |

### Findings — Phase 11

| ID | Sev | Where | Issue | Status |
|----|-----|-------|-------|--------|
| — | OK | tests/ | 42/42 green (Debug). Every security/logic fix has a regression test (irc_session, command_alias, command_parser, dcc_manager, comic_art, link_preview_classifier). UI-only/constant fixes (Ctrl+O/R, topic PlainText, replay_lines, highlight-strip) are untested by design — consistent with existing UI coverage; noted. | VERIFIED |
| — | OK | DEV_NOTES.md | "THINGS I GOT WRONG" has all 4 wrong-port entries (IRC caps, DCC size-0, comic OOM, SSRF, CR/LF+topic). DECISIONS has the scripting-defer. | VERIFIED |
| — | DONE | HANDOFF.md | Updated with an audit-complete summary + pointer to AUDIT.md/backlog. | VERIFIED |
| S14 | DEFER | i18n | Translations remain deferred (user decision); loader + Localization page exist, no .qm/tr() yet. | DEFERRED |

Closeout status: phase index all ☑; test map produced (gaps backlogged); fixed-findings
have tests where feasible; DEV_NOTES + HANDOFF updated; backlog (26) + backports (10) triaged;
release-hygiene reminder recorded (gitignore internal docs before public push).

---

## Backports to Python

Items where the C++ port ended up **better than / usefully different from** the
Python original — candidates to port *back* into Python MaxChat. **Mirror of**
`../maxchat/DEVDOCS/BACKPORTS.md` (canonical copy lives there, where the work
happens). Keep both in sync; see the sync rule in "How to use this doc".

Priority: `P1` clear win · `P2` nice to have · `P3` minor/architectural.

> **Lesson learned (the backports idea).** Auditing a port is bidirectional. The
> port is not just "behind" the original — it also accrues genuine improvements
> the original lacks, and a port-vs-original audit is the one moment you have
> both in view to spot them. Capture those as backport candidates *while you're
> looking*, in **both** repos, or the knowledge dies with the audit session. Keep
> the parity direction (port catching up) and the backport direction (original
> catching up) as two separate ledgers so neither swamps the other.

| ID | Phase | Pri | Item | Detail | Status |
|----|-------|-----|------|--------|--------|
| BP-1 | 1 | P1 | Readable numeric formatting | C++ emits friendly lines for numerics Python dumps raw: 301/305/306/307/313/328/331/333/671 (e.g. `[topic] #chan set by alice at <ts>`). Add explicit branches in `client.py` `_handle` before the digit catch-all. | OPEN |
| BP-2 | 1 | P3 | Split connect vs registration watchdog | Port has independent connect + registration timeouts (clearer failure reason, correct failover when a server accepts TCP but never registers); Python has one ~20s watchdog. | OPEN |
| BP-3 | 1 | P3 | ~~Tolerant PONG token matching~~ RETRACTED (phantom: parser sets `trailing == params[-1]`, so trailing already covers the last param — no real difference in either client). | — | WONTFIX |
| BP-4 | 2 | P2 | Connection-management commands | Port has `/server` `/connect` `/disconnect` `/reconnect`; Python has none (menu only). Useful power-user parity. | OPEN |
| BP-5 | 2 | P3 | In-client `/help` `/?` | Port prints a command reference in chat; Python only has the Help menu/dialog. | OPEN |
| BP-6 | 2 | P3 | Misc command extras | Port adds `/close`, `/notify` `/unnotify`, `/list` (opens dialog), `/q` `/m` short aliases, and os/hs service shortcuts. | OPEN |
| BP-7 | 2 | P3 | Alias appends unused args | When an alias template has no placeholder, the port appends the args (mIRC-style); Python drops them. | OPEN |
| BP-8 | 5 | P3 | Validate RESUME/ACCEPT pos (BOTH) | Both clients trust the peer's resume offset without checking it against the file size → a lying peer causes a sparse/corrupt download. Add `0 <= pos <= size` validation on both the RESUME (sender) and ACCEPT (receiver) sides. | OPEN |
| BP-9 | 5 | P2 | Cap pending DCC offers (BOTH) | Neither client limits incoming DCC offers → offer-spam DoS. Add a per-peer + global cap. | OPEN |
| BP-10 | 10 | P3 | Strip CR/LF in send_raw (BOTH) | Neither client strips embedded CR/LF before appending the terminator. The C++ multi-line input made it reachable (now fixed there); Python's QLineEdit limits exposure but the defense-in-depth still applies — strip `\r`/`\n` in `IRCClient.send_raw`. | OPEN |

### Feature backports (C++ → Python) — bidirectional audit 2026-06-11

Whole features added/expanded in the C++ port that Python lacks. Mirror of the
canonical table in `maxchat/DEVDOCS/BACKPORTS.md`.

| ID | Pri | Feature | What Python needs | Status |
|----|-----|---------|-------------------|--------|
| FB-1 | P2 | Scripting overhaul | Expand `scripting.py` to the C++ api/hook surface (on_notice/part/quit/nick; send_raw/network/channels/nicks/strip/read_file/get/set/timer); add permission model + Preferences ▸ Scripts page + Scripts manager. Python keeps Python lang (D5); CPython sandbox = decision needed. | OPEN |
| FB-2 | P3 | Preferences "Layout" page | Add a dedicated Layout page (list visibility, button bar, buffer tabs, separator, nick-col width, splitter) — Python has none. | OPEN |
| FB-3 | P3 | Rich `/sysinfo` | Port OS/uptime/CPU/RAM/GPU/res/drives line + `/sysinfo send [#chan\|nick]`; Python has no rich sysinfo. | OPEN |
| FB-4 | P3 | Cleaner OpenGraph cards (Discord-style) | Match C++ `LinkPreviewRenderer` (2026-06-11): site name top → title → image; drop the repeated URL footer; drop the description when an image is present EXCEPT X/Mastodon (post text must stay). Python `_web_card` is the noisy version. UX choice — confirm. | OPEN |
| BP-11 | media | Async SSRF DNS (BOTH) | Python `is_safe_fetch_url` (`socket.getaddrinfo`) resolves on the GUI thread → UI stall per link/redirect. C++ fixed via `resolvePreviewUrlPublicAsync` (QHostInfo::lookupHost off-thread). Python: thread the getaddrinfo, continue in callback; keep literal/scheme checks sync. | OPEN |

> Not backports (C++ catching up): comic per-channel editor, sounds, update
> checker, DCC rate/ETA, dimmed replay, window-title/nick — all Python-first.

---

## FIX BACKLOG

Big items deferred from phases. Each entry must be actionable cold: what, where, why, sketch.

| # | From phase | Sev | Item | Notes | Status |
|---|-----------|-----|------|-------|--------|
| 1 | 1 (P1-1) | DIFF low | parseMessage preserves trailing whitespace in the trailing param | In `IrcMessage.cpp parseMessage`, the early `s = s.trimmed()` (line ~75/88) strips trailing spaces from the whole line, losing them in the trailing param. Python only strips `\r\n`. Fix: rstrip only `\r\n` at entry; use lstrip (not trimmed) at the prefix/command boundaries. Add a parser test with a trailing-space message. Low value, parser-risk — verify all parse tests still pass. | **DONE** (aa6ac20) |
| 2 | 1 (P1-6) | DIFF low | CTCP PING reply round-trip time | In `IrcSession.cpp` CTCP-NOTICE-reply path (ctcpSummary for NOTICE), when ctcp.command=="PING" parse args as a float timestamp and show "%.3fs" elapsed, matching Python `_handle_ctcp_reply`. | **DONE** (537c329) |
| 3 | 1 (P1-8/P1-9) | DIFF low | Proxy config validation | Validate type/host/port; error+bail on bad config. | **DONE** (16e9bdc) — unsupported type / missing host / port out of 1-65535 → errorOccurred + disconnected. |
| 4 | 1 (P1-11) | DIFF low-med | Throttled line draining | `IrcConnection::queueIncomingLines` drains all complete lines synchronously per readyRead. Python caps at 100 lines/tick and defers the rest via singleShot(0) to keep the UI responsive during floods/netsplits. Port the deferred-drain queue. | **DONE** (537c329) — 100 lines/tick + deferred continuation; overflow abort gated on incomplete line. |
| 5 | 1 (P1-12) | DIFF low | away-notify chat spam | Dropped the per-change replyText; awayChanged signal only (Python parity). | **DONE** (17b3a5f) |
| 6 | 2 (S1) | MISS | Lua scripting host | **Engine choice: Lua 5.4** (2026-06-10). API contract + example scripts DONE (SCRIPTING.md, assets/scripts/_hello.lua, dice.lua, url_logger.lua). **Remaining: build the host** — vendor Lua sources (single static lib, also into Windows build.bat), embed the interpreter, implement the `api` table (echo/say/insert_input/notify/me/target/data_dir/append_file/timestamp), dispatch hooks (on_load/unload/message/join/command), enforce the sandbox (no io/os/require/load; math.random pre-seeded; append_file scoped to data dir), wire /load /unload /reload + the scripts dialog, seed assets/scripts on first run. on_image_paste deferred (S13). | **DONE** — full engine + sandbox + permissions tab + Scripts dialog + examples |
| 7 | 3 (S11) | TEST | Shortcut persistence regression test | Shortcuts persist correctly but there's no test. Add a round-trip: set an override → saveRaw → reload → applyNavShortcutOverrides binds the new key. Needs a MainWindow harness (see main_window_link_preview_test). | DEFERRED (low-value: UI-bound / core already covered) |
| 8 | 3 (P3-3) | MISS low | Port `nick_width_autoset` | First connect widens the nick column to fit your nick (never shrinks; once). | **DONE** (537c329+) — autoset on `registered`, default added. |
| 9 | 4 (P4-2) | DIFF med | Tab-completion cycling | `MainWindow::completeInput` picks the first match only. Make repeated Tab/Backtab cycle through all candidates: remember (tokenStart, prefix, candidate list, index, last-inserted range); on each Tab advance the index and replace the last completion; reset the state on any other keypress/edit. Mirror Python `input_bar._complete(back=...)`. Needs the main_window test harness for a round-trip test. | **DONE** — CompletionCycle state in MainWindow; Tab/Backtab cycle, resets on edit; test added |
| 10 | 5 (P5-5) | SEC low | Cap pending DCC offers | Ignore new SEND/CHAT offers past 32 pending, with a status line. | **DONE** (16e9bdc) — still a Python backport (BP-9). |
| 11 | 5 (P5-3) | DIFF low | Expire passive CHAT tokens | 120s QTimer clears an unanswered pending token. | **DONE** (5884a74) |
| 12 | 5 (S12) | MISS low | DCC Rate/ETA columns | Add live transfer rate + ETA to DccTransfersDialog (EMA over recent throughput on a timer tick), like Python's dcc_dialog. | **DONE** (next commit) |
| 13 | 5 | TEST | DCC loopback tests | Port test_dcc.py: drive a real QTcpServer/QTcpSocket loopback through SEND/GET/RESUME/CHAT and assert the received file matches (sha/byte-compare), plus the size-0 guard end-to-end. Heavier harness than the pure-helper test already added. | DEFERRED (low-value: UI-bound / core already covered) |
| 14 | 6 (S10a) | MISS | Comic per-channel overrides | Implement `comic_channels` `{"<net>/<#chan>": {bg, chars{nick:stem}, ignore[]}}`: a Channels page in ComicSettingsDialog + lookups in refreshComic/comicBackground/comicCharacterForNick that prefer per-channel over global. Mirror Python comic_settings.py. | **DONE** (389d0d4 engine + editor UI (next commit)) |
| 15 | 6 (S10b) | MISS | Comic emotion wheel | Replace the openEmotionPicker stub with a real picker (Python emotion_picker.py — 9 emotions) that sets the emotion for your next message; refreshComic already supports explicit emotions. | **DONE** (df9acf5) |
| 16 | 6 (S10c) | MISS | Assign-character dialog | Add a member context-menu "Assign comic character…" → dialog listing available .avb stems, writing comic_chars[nick] (Python assign_character.py). | **DONE** (8c0f268 global write; Python writes per-channel) |
| 17 | 6 (P6-4) | MISS | Wire comic_random_bg | The checkbox was saved but unused. | **DONE** (537c329) — comicBackground() picks a stable per-channel bg (seeded by target) when none set + toggle on. |
| 18 | 6 (S10e) | MISS | Comic panel context menu | Add a right-click menu to ComicView panels: Copy panel, Save panel as PNG. | **DONE** (a175412) |
| 19 | 6 | TEST | Comic renderer/filter tests | Port test_comic_speech.py / test_comic_filter.py / test_character_cells.py: emotion guessing, bot-pattern/ignore filtering, cell layout. | OPEN |
| 20 | 7 (S2) | MISS | Update checker | Help ▸ Check for Updates + optional quiet startup check (pref `update_check`). HTTPS GitHub releases API (IronWolve/MaxChat); manual + startup (pref, default off pre-release). | **TBD (deferred)** |
| 21 | 7 (S3a) | MISS | notify.wav playback | Replace the `QApplication::beep()` for `notify_sound` with a QSoundEffect player: play `<config>/sounds/notify.wav` (user's, else a bundled default if one ships) at low latency; fall back to beep if Multimedia/file absent. Port Python sounds.py SoundPlayer/notify_path. | **DONE** (bf37046) |
| 22 | 7 (S3b) | MISS | CTCP SOUND receive + play | Plumb a sound signal: IrcSession detect incoming CTCP `SOUND <file> [text]` → emit → IrcConnection → MainWindow; gate on the `ctcp_sound` pref; play only via an own-file-only resolver (basename, force `.wav`, must already exist in `<config>/sounds/` — never fetch). Mirror Python sounds.resolve(). | **DONE** (bf37046) |
| 23 | 8 (P8-2) | DIFF low | Full strftime tokens in log mask | ChatLogStore.logFilePath only maps %Y/%m/%d. Mapped the common strftime codes to QDateTime formats. | **DONE** |
| 24 | 8 (P8-3) | DIFF low | Dimmed replay + "Ended" divider | Render replayed log lines in a dimmed style and use an "Ended <date> <time>" divider (Python look) instead of the plain `--- Log replay ---` header/footer. | **DONE** (next commit) |
| 25 | 9 (P9-4) | PERF low | Async SSRF resolve | `resolvesToPublicOnly` blocks the GUI thread on DNS. Move the resolve off-thread (QHostInfo::lookupHost async, or a worker) and complete the fetch in the callback, so a slow/black-hole DNS can't stall the UI. Also add a live-redirect integration test (302→private aborts) + a DNS-to-private test once resolution is mockable. | **DONE** — `resolvePreviewUrlPublicAsync` (QHostInfo::lookupHost) off-thread; both fetchers + redirect hops gate via it; literal/allow-private still sync. |
| 26 | 10 (P10-4) | SEC low | Escape wallpaper path in QSS | Drop the wallpaper if its path contains a quote/newline (can't break out of url()). | **DONE** (5884a74) |
| 27 | media | MISS | Image drop / paste → upload → inline link | **Goal:** drop OR paste an image into the input/chat → upload to a user-chosen service → insert the returned direct URL (the inline-image loader then previews it). User picks the service in Preferences (a new **Uploads** section: service dropdown + per-service API key/Client-ID field). Accounts/login = future TBD. **Architecture:** pluggable `ImageUploader` (one backend per service) emitting `uploaded(url)` / `uploadFailed(reason)`; reuse `QNetworkAccessManager` + `QHttpMultiPart`; trigger from BOTH `dragEnter/drop` (image files) and clipboard paste (`QGuiApplication::clipboard()->image()` / image mime) on the input. Parse each service's JSON for the direct link. **Service reality (don't build hollow backends):** clean public APIs = **ImgBB** (free key, `api.imgbb.com/1/upload`, `data.url`) and **Imgur** (`api.imgur.com/3/image`, `Authorization: Client-ID`, `data.link`). Keyless default = **0x0.st** (or uguu.se, temporary). **Postimages** has an API-token endpoint (messier). **Image2URL / Imgbox / ImageShack** lack clean public APIs (web-form / multi-step / auth) → defer. Test backends' response parsing via loopback server (like image_fetcher_test). | **DONE** (2026-06-11) — `src/upload/` lib (ZeroXUploader, ImgbbUploader, ImgurUploader, factory); Preferences → Uploads page (service picker + key fields, visibility-gated); `SpellTextEdit::insertFromMimeData` intercepts image paste → `imageReceived` signal; `MainWindow::dragEnterEvent/dropEvent` handles file drops; `startImageUpload` uploads + inserts URL at cursor; disabled by default (user picks service); `image_uploader_test` 12/12 green. |
| 28 | media | ENH | User-selectable OG card fields | Let users choose which OpenGraph card elements show (site name / title / description / image) via Preferences — e.g. per-field checkboxes, or a "description: never / text-only cards / always" mode. Current behaviour: site name + title + image always; description shown only on text-only (no-image) cards (`renderOpenGraphPreviewHtml`, LinkPreviewRenderer.cpp). Imgur confirmed its `og:description` is generic boilerplate, so the default stays "drop description when an image is present"; this would make that overridable. Also consider a `twitter:image`-vs-`og:image` preference (imgur's og:image is a 600x315 crop; twitter:image is full aspect). | OPEN (TBD) |
| 29 | resume | DIFF med-high | Log-resume replay + "new" marker are painted, not modeled | **Symptom:** chat-log resume (dimmed history + "Chat ended" divider) and the `──── new ────` unread marker don't behave right — replay disappears after switching buffers, doesn't appear for channels joined after connect, and the marker can't sit correctly relative to "Chat ended". **Root cause (architecture):** C++ uses ONE shared `m_chatView`, cleared + rebuilt from the stored line-model on every buffer switch (`renderActiveBuffer` → `m_chatView->clear()`). But `replayCurrentLog` *paints* the dimmed history + divider straight onto the view via `appendFormattedChatLine` (which does NOT store to `ChatBufferStore`), so the rebuild wipes it. Replay is also only triggered once at connect for the active buffer (MainWindow.cpp ~3484) + the manual Tools menu item — not per-buffer on first open. The unread marker is positioned from the stored unread count (`renderActiveBuffer` markerIndex), independent of the painted replay. Python avoids all this by giving each buffer its own persistent QTextBrowser (replay drawn once on `_ensure_buffer`). **Fix approach (when we tweak this):** make replay part of the buffer model — on a buffer's first open, seed `ChatBufferStore` with the dimmed history lines + a "Chat ended" divider line (store as htmlText or add a `dimmed`/`divider` flag to `ChatBufferLine`), then drop the paint-time replay. `renderActiveBuffer` then reproduces resume on every switch and the `new` marker lands naturally after it. Per-buffer "already seeded" guard + replay-on-first-open hook (not just at connect). | **DONE** (2026-06-11) — added `dimmed` flag to `ChatBufferLine`; `seedReplayForBuffer()` stores dimmed history + "Chat ended" divider into `ChatBufferStore` on each buffer's first open (guarded by `m_replayedBuffers`, empty-buffer only); `renderActiveBuffer` formats dimmed lines with the dim palette + their original timestamp; dropped the connect-time paint. Regression test `replayHistorySurvivesBufferSwitch`. |
| 30 | spell | MISS | Native OS speller backend | **Stage 1 done (2026-06-11):** red WaveUnderline, right-click suggestions, and the Localization `Spell Engine` selector `[Internal (Hunspell) \| Operating system]` persisted as `spellcheck_backend` (default `internal`). **Stage 2 (this item):** implement the actual OS speller so `spellcheck_backend == "os"` uses the native engine. **Why it matters:** all current spellcheck is behind `MAXCHAT_WITH_HUNSPELL` (Linux pkg-config only), so the **Windows build has no spellcheck at all** — the OS engine is how Windows gets it. **Plan:** (1) extract a small `Speller` interface (`isLoaded`, `isCorrect(word)`, `suggestions(word)`, `loadLanguage(code)`); make `HunspellSpellchecker` implement it (internal). (2) Windows backend `OsSpeller` via COM `ISpellChecker` (`<spellcheck.h>`, `SpellCheckerFactory`→`CreateSpellChecker(L"en-US")`→`Check`/`Suggest`; `CoInitializeEx` + link `ole32`) behind `#if defined(Q_OS_WIN)`. macOS via `NSSpellChecker` behind `#if defined(Q_OS_MAC)`. (3) `configureSpellcheck` picks the backend from `spellcheck_backend`; if "os" but no native engine is compiled/available, fall back to internal and post a one-time status note (don't silently behave like internal). (4) gate the Windows COM TU behind a `build.bat osspell` opt-in flag (default OFF) like Lua was, so the default Windows build can't be broken by unverified COM; flip the default ON once verified on a real Windows build. **Constraint:** the COM/Cocoa code can't be compile-tested from WSL — must be built/verified on the target OS. macOS NSSpellChecker: deferred until a macOS build is available. | **TBD (deferred)** — Windows `ISpellChecker` code is written (`WindowsSpeller.cpp`, `build.bat osspell`) but unverified on a real Windows build. macOS `NSSpellChecker` not yet implemented. Both blocked on target-OS builds. |
| -- | 9 (P9-4) note | -- | (Async SSRF #25 now also covers the image fetcher: both go through `services::canFetchPreviewUrl`, whose `resolvesToPublicOnly` moved into LinkPreviewClassifier.cpp. Fixing #25 there fixes both fetchers.) | — | NOTE |
