# MaxChat C++ Port — Dev Notes

Internal notes for this port. Not shipped.

## THINGS I GOT WRONG

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
