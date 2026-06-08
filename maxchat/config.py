"""Settings and on-disk paths.

Per-OS locations via Qt's ``QStandardPaths`` (no extra dependency). Settings persist as a
small JSON file written atomically (temp + ``os.replace``) so a crash mid-write can't corrupt
it. A missing/garbled file just yields defaults — never a crash.
"""

from __future__ import annotations

import json
import os

from PySide6.QtCore import QStandardPaths

from maxchat.content_services import DEFAULT_CONTENT_SERVICES
from maxchat.default_networks import DEFAULT_NETWORKS


_APP_DIR = "maxchat"  # one folder under the per-user config/cache root
_migrated = False


def config_dir() -> str:
    """Per-user config dir: ``<config>/maxchat`` (e.g. ``~/.config/maxchat``,
    ``%LOCALAPPDATA%\\maxchat``). Deliberately uses the *generic* config root + a single app
    folder, instead of Qt's ``AppConfigLocation`` which nests ``<org>/<AppName>`` (two levels deep)."""
    base = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericConfigLocation)
    d = os.path.join(base or os.path.expanduser("~/.config"), _APP_DIR)
    global _migrated
    if not _migrated:
        _migrated = True
        _migrate_renamed_app_dir(d)
        _migrate_legacy(d)
    return d


def cache_dir() -> str:
    """Per-user cache dir: ``<cache>/maxchat`` (e.g. ``~/.cache/maxchat``)."""
    base = QStandardPaths.writableLocation(QStandardPaths.StandardLocation.GenericCacheLocation)
    return os.path.join(base or os.path.expanduser("~/.cache"), _APP_DIR)


def _migrate_renamed_app_dir(new_dir: str) -> None:
    """Copy settings forward from the legacy app folder if the new folder is empty."""
    if os.path.exists(os.path.join(new_dir, "settings.json")):
        return
    legacy_dir_name = "comic" + "chat"
    old_dir = os.path.join(os.path.dirname(new_dir), legacy_dir_name)
    if not os.path.isfile(os.path.join(old_dir, "settings.json")):
        return
    import shutil
    os.makedirs(new_dir, exist_ok=True)
    for entry in os.listdir(old_dir):
        src, dst = os.path.join(old_dir, entry), os.path.join(new_dir, entry)
        if os.path.exists(dst):
            continue
        try:
            shutil.copytree(src, dst) if os.path.isdir(src) else shutil.copy2(src, dst)
        except OSError:
            pass


def _migrate_legacy(new_dir: str) -> None:
    """One-time: lift files out of the old Qt ``AppConfigLocation`` nesting
    (``<config>/maxchat/<AppName>/``) up into ``<config>/maxchat/``. No-op once migrated."""
    try:
        from maxchat import __app_name__
    except Exception:
        return
    old_dir = os.path.join(new_dir, __app_name__)  # e.g. …/maxchat/MaxChat
    if not os.path.isdir(old_dir) or os.path.exists(os.path.join(new_dir, "settings.json")):
        return
    import shutil
    os.makedirs(new_dir, exist_ok=True)
    for entry in os.listdir(old_dir):
        dst = os.path.join(new_dir, entry)
        if not os.path.exists(dst):
            try:
                shutil.move(os.path.join(old_dir, entry), dst)
            except OSError:
                pass
    try:
        os.rmdir(old_dir)  # drop the now-empty legacy folder
    except OSError:
        pass


def _settings_path() -> str:
    return os.path.join(config_dir(), "settings.json")


def load_settings() -> dict:
    try:
        with open(_settings_path(), encoding="utf-8") as f:
            data = json.load(f)
            return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_settings(data: dict) -> None:
    path = _settings_path()
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)
    os.replace(tmp, path)  # atomic on one filesystem


def get_setting(key: str, default=None):
    return load_settings().get(key, default)


def set_setting(key: str, value) -> None:
    data = load_settings()
    data[key] = value
    save_settings(data)


# ---- typed preferences (one place for defaults) -----------------------------
DEFAULTS = {
    "theme": "synthwave",             # app theme; "system" = Qt/system default with MaxChat QSS off
    "interface_language": "system",  # system | en | translated language code; applied on restart
    "spellcheck_enabled": True,      # message-box spellcheck once dictionaries are bundled
    "spell_language": "en",          # preferred bundled spellcheck dictionary
    "wallpaper": "",           # window wallpaper override: "" = the theme's own · "none" = off · <path> = your image
    "show_timestamps": True,
    "timestamp_format": "%I:%M %p",  # 12-hour clock by default (strftime; 24-hour = %H:%M)
    "colored_nicks": True,
    "show_formatting": True,   # render mIRC colours/formatting (off = strip to plain)
    "hide_joinpart": False,    # hide join/part/quit chatter
    "show_mode": True,         # show channel/user MODE changes
    "chat_font_family": "JetBrains Mono",  # bundled default chat font
    "chat_font_size": 14,
    "chat_font_bold": True,    # bold chat font
    "chat_text_color": "",     # chat message text colour ("" = the chat theme's own foreground)
    "app_font_family": "JetBrains Mono",  # bundled default UI/chrome font
    "app_font_size": 14,
    "app_font_bold": True,     # bold UI/chrome font
    "list_font_size": 14,      # font size for the channel tree + member list
    "list_font_bold": True,    # bold channel tree + user list
    "font_defaults_version": 3,  # current bundled JetBrains Mono bold default profile
    "comic_panels": 4,         # comic view: 1–6 framed panels (reflow to fit the strip)
    "comic_art_dir": "",       # folder with the user's OWN Comic Chat install (.avb/.bgb) — never bundled
    "comic_self_char": "",     # your character (.avb stem)
    "comic_bg": "",            # default background (.bgb filename)
    "comic_per_panel": 4,      # max speech bubbles per panel (hard cap; it also breaks earlier on fit)
    "comic_min_font": 9,       # readable text floor (pt @315px): break to a new panel before shrinking past it
    "comic_chars": {},         # GLOBAL nick → character-stem assignments (Comic Settings ▸ Global)
    "comic_ignore": [],        # nicks whose messages never become comic panels (bots / URL-info) — chat only
    "comic_ignore_cmds": True, # honour comic_bot_patterns below (master toggle for command filtering)
    "comic_bot_patterns": ["!", ".", "~", "@", "?", "s/", ".8ball", "#8ball"],  # a message starting with
    #                          one of these is a bot command/correction (!mlb, .np, s/typo/fix/, .8ball,
    #                          #8ball) → kept out of the comic (#8ball needs the literal: '#' alone would
    #                          also hide channel mentions like #linux)
    "comic_exclude_regex": "", # advanced: a custom regex; messages matching it never become comic panels
    "comic_patterns_migrated": False,  # one-time: merged newer default bot-patterns (s/ …) into old configs
    "comic_channels": {},      # per-channel comic overrides: {"<net>/<#chan>": {"bg": str, "chars": {nick: stem}}}
    "scrollback": 2000,        # max lines kept per buffer
    "pm_echo": True,           # echo incoming private msgs to the server tab + current chat (query still opens)
    "notify_method": "flash",  # (legacy) highlight/PM alert: off | flash | tray | both — superseded below
    # --- notifications (window is inactive only) ---
    "notify_popup": "custom",  # toast style: off | custom (in-app, themed) | system (OS-native via tray)
    "notify_flash": True,      # also flash the taskbar / window
    "notify_pm": True,         # notify on private messages
    "notify_highlight": True,  # notify on highlights (your nick / highlight words)
    "notify_corner": "br",     # custom toast corner: tl | tr | bl | br
    "notify_duration": 6,      # custom toast seconds on screen
    "notify_theme": "follow",  # custom toast colours: follow | dark | light | solar-dark | solar-light
    "notify_sound": False,     # also play <config>/sounds/notify.wav when a notification fires
    "dnd": False,              # Do Not Disturb — suppress ALL notifications (toast/flash/tray/beep/sound)
    "auto_rejoin": False,      # rejoin a channel automatically after being kicked
    "rejoin_delay": 2,         # seconds to wait before an auto-rejoin
    "update_check": True,      # check GitHub Releases for a newer MaxChat on startup
    "nick_colors": {},         # per-user nick colour overrides {lower-nick: "#rrggbb"} — saved like avatars
    "auto_away_mins": 0,       # auto-away after N minutes idle (0 = off); auto-back on activity
    "muted_buffers": [],       # "network/#channel" keys whose messages never highlight / notify
    "comic_captions": True,    # draw the nick caption under each comic character
    "comic_caption_mode": "nick",   # name-box colour: "nick" (per-speaker, like the user list) | "fixed"
    "comic_caption_color": "#363636",  # the fixed name-box colour when mode == "fixed"
    "comic_caption_scale": 1.0,     # name-label size multiplier (0.8 small · 1.0 normal · 1.3 large)
    "comic_random_bg": False,       # channels with no set background get a random (stable) one
    "seeded_scripts": [],      # example scripts already copied into <config>/scripts (seed each once)
    "hide_version": False,     # don't answer CTCP VERSION requests
    "ctcp_version": "",        # custom CTCP VERSION reply ("" = "MaxChat <version>")
    "minimize_to_tray": False, # the minimise button hides to the system tray (when a tray is available)
    "tray_icon": "bubble",     # window/tray icon: "bubble" (theme-tinted speech bubble) or an emoji glyph
    "ctcp_sound": False,       # play CTCP SOUND (.wav from <config>/sounds/) others send — OFF by default
    "friends": [],             # notify-list nicks — alert when they come online / go offline
    "aliases": {               # /command aliases: $1 $2 positional, $1- rest, $me your nick, $chan channel
        "slap": "me slaps $1- around a bit with a large trout",
        "fish": "me slaps $1- around the face with a wet fish",
    },
    "ignores": [],             # ignore masks (glob over nick!user@host); their messages are hidden
    "flood_protect": False,    # auto-ignore flooders — OFF by default (it silently mutes chatty BOTS,
                               #   and the ignore is global so it hides them on EVERY network)
    "flood_msgs": 10,          # this many messages within flood_secs counts as a flood (when enabled)
    "flood_secs": 4,           # the flood detection window (seconds)
    "paste_guard": True,       # confirm before sending a large multi-line paste (and throttle it)
    "paste_lines": 4,          # pasting this many lines (or more) triggers the guard
    "ignore_invites": False,   # silently ignore all channel invites
    "invite_protect": True,    # auto-ignore users who spam invites
    "dcc_passive": True,       # use passive/reverse DCC when sending (works behind NAT/firewalls)
    "dcc_ip": "",              # your public IPv4 to advertise for DCC ("" = the connection's local IP)
    "dcc_port_first": 0,       # DCC listen-port range start (0 = any free port; set for NAT forwarding)
    "dcc_port_last": 0,        # DCC listen-port range end
    "dcc_dir": "",             # where received files are saved ("" = <config>/downloads)
    "dcc_accept": "ask",       # incoming DCC policy: ask | trusted (auto-accept trusted) | all
    "dcc_trusted": [],         # nicks auto-accepted when dcc_accept = "trusted"
    "logging": True,           # write conversations to <config>/logs/ (on by default → resume/replay works)
    "logging_for_replay_migrated": False,  # one-time: turned logging on for existing replay-enabled configs
    "log_mask": "%network-%channel",  # log filename: %network/%channel tokens + strftime (%Y…); / = subdir
    "replay_log": True,        # on opening a buffer, replay the tail of its log (with an "Ended" divider)
    "replay_lines": 0,         # how many log lines to replay (0 → a sensible default)
    "highlight_words": "",     # extra words (beyond your nick) that trigger a highlight
    "beep_highlight": True,    # beep when highlighted / on a private message
    "confirm_quit": True,      # ask before quitting while still connected to a network
    "auto_reconnect": True,    # reconnect automatically after an unexpected disconnect
    "indent_wrap": True,       # hang-indent wrapped lines under the message (HexChat look)
    "marker_line": True,       # show an "unread" marker line in inactive chats
    "align_nicks": True,       # right-align nicks in a fixed column (HexChat aligned look)
    "nick_width": 16,          # nick column width (chars) when align_nicks is on (fits most nicks)
    "nick_width_autoset": False,  # have we auto-fit the column to your nick once? (then you drag it)
    "word_wrap": True,         # wrap long lines (off = horizontal scroll)
    "content_services": DEFAULT_CONTENT_SERVICES,  # per-service on/off for link previews (Prefs ▸ Services)
    "strip_color_copy": True,  # copying chat text puts plain text on the clipboard (no colour markup)
    "separator_line": True,    # the "chat line" — vertical rule between the nick column and the message
    "show_input_hint": True,   # show the grey hint inside the message box ("Tab completes · history · …")
    "nick_label_color": "",    # colour of YOUR nick beside the input bar ("" = the theme's text colour)
    "status_text_color": "",   # colour of the status-bar text ("" = the theme's text colour)
    "topic_color": "",         # colour of the channel topic bar text ("" = the theme's text colour)
    "tree_color": "",          # colour of the server/channel tree text ("" = the theme's text colour)
    "userlist_color": "",      # colour of the user-list text ("" = theme; colour-nicks still wins)
    "event_color": "",         # colour of join/part/quit/mode/nick lines ("" = the system-line colour)
    "list_font_family": "JetBrains Mono",  # font family for the channel tree + user list
    "nick_font_family": "JetBrains Mono",  # font for YOUR nick beside the input
    "nick_font_size": 14,
    "nick_font_bold": True,
    "status_font_family": "JetBrains Mono",  # font for the status-bar text
    "status_font_size": 14,
    "status_font_bold": True,
    "topic_font_family": "JetBrains Mono",  # font for the channel topic bar
    "topic_font_size": 14,
    "topic_font_bold": True,
    "sort_status": True,       # sort the user list by op/voice status, then name
    "buffer_tabs": False,      # show open chats as buttons/tabs across the top instead of the left tree
    "server_list_visible": True,  # show the left server/channel pane on startup
    "member_list_visible": True,  # show the right channel-member pane on startup
    "show_button_bar": False,  # top button bar (Server List, Channel List) — off by default; View ▸ Button Bar
    "shortcuts": {},           # rebound keyboard shortcuts {action_id: "Ctrl+…"} (Keyboard Shortcuts editor)
    "looks": {},               # saved "looks": {name: {appearance prefs}} — theme+wallpaper+fonts+colours
    "chat_theme": "follow",    # chat-area theme, independent of the app theme (see theme.CHAT_THEMES)
    "networks": DEFAULT_NETWORKS,  # server address book — HexChat's network list (Server ▸ Server List…)
    "connect_on_start": False,  # auto-connect to the top (last-connected) network when the app launches
}


def pref(key: str):
    return get_setting(key, DEFAULTS.get(key))


def set_pref(key: str, value) -> None:
    set_setting(key, value)
