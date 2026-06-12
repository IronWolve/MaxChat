# MC DATA + Script Terminal Plan

## Objective

Build a reusable MaxChat script service layer where Lua scripts can open rich terminal windows and communicate over CTCP using `MC DATA`, over the IRC networks the user is already connected to.

## Goals

- Generic Lua terminal UI for any script.
- Generic `MC DATA` CTCP transport for any script/service.
- Retro-BBS as demo script, not a built-in special case.
- Existing IRC connections only; no new network/socket/daemon.
- Modular code for easy revert, fork, or replacement.
- Flood-safe small payloads, queueing, and throttling.
- Preserve existing CTCP/DCC behavior.
- Leave room for Comic Chat network features, security, encryption, and service versioning.

## Git Plan

Branch:

```text
mc-data-terminal
```

Commit chunks:

```text
1. Add MC DATA protocol docs and codec
2. Add Lua MC DATA API
3. Add script terminal UI module
4. Add Lua terminal API
5. Add terminal profiles and fonts
6. Add Retro-BBS demo script
7. Add MC DATA and terminal tests
```

## MC DATA Transport

Wire:

```text
PRIVMSG nick :\001MC DATA <service> <verb> <payload>\001
NOTICE  nick :\001MC DATA <service> <verb> <payload>\001
```

Lua API:

```lua
api.mc_send(target, service, verb, payload)
api.mc_reply(target, service, verb, payload)
```

Lua hook:

```lua
on_mc_data(api, network, target, nick, service, verb, payload, notice)
```

Rules:

- Uses current/existing IRC network only.
- `service` lowercase, `verb` uppercase.
- Payload target: `300-350 bytes`.
- Queue/throttle around `750-1000 ms`.
- No auto-reply to `NOTICE`.
- `DCC`, `ACTION`, `SOUND`, `PING`, `VERSION`, `TIME`, `CLIENTINFO` unchanged.

## Script Terminal

Keep code separated:

```text
ScriptTerminalDialog
ScriptTerminalManager
AnsiRenderer
TerminalProfile
```

Layout:

```text
DIAL: retro-bbs   CONNECT: Retro-BBS   USER: guest
[terminal display area]
bbs> input line
```

Behavior:

- Multiple script-owned terminal windows.
- Terminal IDs scoped by script name.
- Pop-out dialogs in v1.
- No menu bar in v1.
- Large paste guard over `2 KB` or `20 lines`.
- Paste inserts only, no auto-send.

## Terminal Profiles

```text
ibm-vga   fixed 80x25   ANSI/DOS BBS
c64       fixed 40x25   Commodore BBS
free      flexible      Any-size script terminal
```

- `ibm-vga`: default, fit scaling.
- `c64`: integer scaling.
- `free`: resizable rows/cols.

## Lua Terminal API

```lua
api.terminal_open(id, title, profile_or_cols, rows)
api.terminal_close(id)
api.terminal_clear(id)
api.terminal_write(id, text)
api.terminal_status(id, text)
api.terminal_prompt(id, text)
api.terminal_size(id)
api.terminal_profile(id, profile)
api.terminal_fit(id, mode)
api.terminal_hotspot(action_id, label)
```

Hooks:

```lua
on_terminal_input(api, id, text)
on_terminal_link(api, id, action_id)
on_terminal_closed(api, id)
```

## Retro-BBS Demo

Defaults:

```text
bbs_id=retro-bbs
bbs_name=Retro-BBS
```

Commands:

```text
/bbsserve
/bbs nick
/bbs nick retro-bbs
/bbsbook
/bbsbook dial Retro-BBS
```

`/bbs nick` uses the current IRC network.

Address book entries are shortcuts only:

```json
{
  "label": "Retro-BBS",
  "network": "synIRC",
  "nick": "iw_chat",
  "bbs_id": "retro-bbs",
  "profile": "ibm-vga"
}
```

If the saved network is not connected, show an error; do not auto-connect in v1.

## BBS Server

The server script listens through:

```lua
on_mc_data(...)
```

The server terminal is a sysop console, not the listener itself.

Modes:

```text
Stats mode
Mirror mode
```

Stats mode shows:

```text
sessions
connections
messages
pages
current board id/name
```

Mirror mode shows the last screen sent to one user.

If the mirrored user logs off, return to stats.

## Multiple BBS Instances

Each board has a stable identity:

```text
bbs_id=retro-bbs
bbs_name=Retro-BBS
```

Session routing uses:

```text
network + nick + bbs_id
```

That allows one IRC nick or script to host multiple boards later without confusing sessions.

## BBS Features v1

```text
Welcome / About
Main menu
Message board
Who online
Page sysop
Break-in/sysop chat
Hangman door
Logoff
```

Mailbox can be planned but is not required for the first demo unless persistence is added immediately.

## BBS Config

Stored in Lua script data, not app settings:

```text
scripts-data/bbs/server.json
scripts-data/bbs/address-book.json
```

Server config:

```json
{
  "bbs_id": "retro-bbs",
  "bbs_name": "Retro-BBS",
  "sysop": "IronWolve",
  "profile": "ibm-vga",
  "welcome": "Welcome to Retro-BBS"
}
```

## Terminal Rendering

- ANSI SGR colors/styles.
- Unicode box/block characters.
- Future CP437/PETSCII helpers TBD.

## Future Services

Possible Comic Chat service:

```text
service=comic
verbs=HELLO, PROFILE, AVATAR, EMOTE, POSE, ROOMSTATE
```

Rules:

- Optional.
- Versioned capability negotiation.
- Normal IRC works without it.
- No binary blobs in v1.

## Versioning / Security

Versions:

```text
MC DATA v1
Terminal API v1
Retro-BBS v1
Future Comic Service v1
```

Security:

- `MC DATA v1` is plaintext.
- No secrets, passwords, private tokens, or private files.
- Unsolicited data may notify/log only.
- Host queue/rate-limit required.
- DCC remains separate.
- Future `MC SEC` must use real reviewed crypto.

## Tests

- `MC DATA` parse request/reply.
- Existing CTCP/DCC behavior unchanged.
- Lua APIs call expected host methods.
- Multiple terminal windows work.
- Profiles apply size/font/palette.
- ANSI colors and Unicode render.
- Paste guard triggers.
- Server stats/mirror mode updates.
- `/bbs` uses current IRC network only.
