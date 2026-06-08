# Changelog

All notable changes to this project are documented here. The format follows
"Keep a Changelog"; semantic versioning applies once released.

## [Unreleased]

## [0.9.0] — 2026-06-07

First public release as **MaxChat**.

### Added
- **Comic mode** — renders the conversation as a comic strip: per-user characters, emotions, body
  poses, speech/thought balloons, per-channel backgrounds, 1–6 reflowing panels, Save/Copy as PNG.
  Reads classic `.avb`/`.bgb` art from your own install (none bundled).
- **Comic name labels** — show/hide, per-speaker or fixed color, size, and a random-background option.
- **IRC client** — multiple networks at once, TLS + cert validation, CAP/SASL PLAIN (+ NickServ
  fallback), separate server password, auto-connect/join/reconnect, failover + alt-nick, the full
  HexChat/mIRC command set, channel-modes + ban-list editors, ignore list, anti-flood/paste/invite.
- **DCC** — SEND/GET, RESUME, passive/reverse DCC, DCC CHAT, transfer window with rate/ETA, auto-accept.
- **Notifications** — custom themed corner toasts (corner/duration/color/sound) or OS-native, per-event
  PM/highlight toggles, Do Not Disturb, per-channel mute, taskbar flash, CTCP SOUND + a notify chime.
- **Customisation** — app + chat themes, theme editor, wallpapers, per-pane fonts, saved per-user nick
  colors, window/tray icon picker (bubble or emoji), rebindable shortcuts, zoom chat font.
- **Tools & commands** — Python scripting API (with a dice/8-ball example), URL grabber, raw log,
  `/lag` `/uptime` `/netinfo` `/sysinfo`, services helpers (`/ns` `/cs` `/ms` `/identify` `/ghost`),
  text macros, mark-all-read, disconnect/reconnect-all, unread count in the title/tray, update checker.
- **Media** — inline image/audio/video previews, X/Twitter cards, generic website cards with thumbnails,
  and SSRF-guarded fetching; preview types can be toggled in Preferences ▸ Services.
- **Spelling** — message-input spellcheck, bundled dictionaries for supported languages, and
  right-click replacement suggestions.

### Security
- Raw log redacts credentials (server PASS, SASL payload, NickServ identify passwords).
- The media SSRF guard re-checks every redirect hop.

### License
- Released under the Apache License 2.0.
