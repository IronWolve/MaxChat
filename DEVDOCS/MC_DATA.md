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
/bbsserve [name]
/bbsconfig
/bbsconfig name <name>
/bbsconfig sysop <name>
/bbsconfig welcome <text>
/bbsconfig profile <ibm-vga|c64|free>
/bbs <nick> [bbs_id]
/bbsbook
/bbsbook add <label> <nick> [bbs_id] [profile]
/bbsbook dial Retro-BBS
/bbsbook remove <label>
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

Stored with the Lua script preference API, not app settings and not loose files:

```text
api.get(...)
api.set(...)
```

Server config:

```text
server:name
server:sysop
server:profile
server:welcome
```

Address book:

```text
book:labels
book:<label> = nick|bbs_id|profile
```

## Terminal Rendering

- ANSI SGR colors/styles.
- Unicode box/block characters.
- Future CP437/PETSCII helpers TBD.

## Compact Terminal Frame Draft

This is the current working protocol for efficient BBS/script terminal updates.
It is a draft, not a frozen compatibility promise. The goal is to keep CTCP
payloads small, avoid flooding, support cursor-positioned screens, and leave
room for later CP437/C64 rendering.

Frame wrapper:

```text
MC DATA bbs T <ops>
```

For Lua/BBS shorthand, this may be described as:

```text
bbs T <ops>
```

Rules:

- No commas or text separators inside the op stream.
- Single-letter opcodes.
- Fixed-width hex arguments.
- Length-prefixed text writes.
- Parser walks left to right by opcode, consuming the exact argument width for
  each op.
- Normal text payloads should still stay under the current app payload guard.
- Full-screen redraws should be avoided when a smaller positioned update works.

Opcodes:

```text
C             clear screen
H             home cursor
Prrcc         position cursor; row and column are two-digit hex
Afb           set color attribute; foreground/background are one hex digit each
Wll<data>     write text; ll is two-digit hex byte length
N             newline
Xllll<data>   extended write; llll is four-digit hex byte length, TBD for v1
```

Example:

```text
CP0101A0FW08Retro-BBSP1801A07W06login>
```

Meaning:

```text
C             clear
P0101         cursor row 1, column 1
A0F           fg 0, bg 15
W08Retro-BBS  write "Retro-BBS"
P1801         cursor row 24, column 1
A07           fg 0, bg 7
W06login>     write "login>"
```

Screen-size notes:

- `ibm-vga`: default `80x25`.
- `c64`: default `40x25`.
- `free`: arbitrary rows/columns for script-controlled terminals.
- Scripts should ask the terminal size and generate output for that size.
- For slow links or strict flood limits, send diffs/regions instead of whole
  screens.

Encoding notes:

- `W` length is byte length, not character count.
- Plain ASCII is cheapest.
- Unicode box/block characters work but cost more bytes.
- Future CP437/PETSCII helpers may map compact byte values to display glyphs.
- Colors are logical terminal attributes; the renderer decides the final theme
  palette.

## Future Services

Possible Comic Chat service:

```text
service=comic
verbs=HELLO, CAPS, PROFILE, AVATAR, EMOTE, POSE, ROOMSTATE, BYE
```

Rules:

- Optional.
- Versioned capability negotiation.
- Normal IRC works without it.
- No binary blobs in v1.

Possible v1 verb meaning:

```text
HELLO       announce Comic service support and protocol version
CAPS        list optional capabilities
PROFILE     share display/avatar metadata references
AVATAR      share a small avatar/art reference, not a raw binary blob
EMOTE       announce current emotion/expression
POSE        announce current pose/stance
ROOMSTATE   share per-channel comic scene hints
BYE         clear transient comic state for the sender
```

Core API audit:

```text
MC DATA core: service + verb + payload only
Lua API: api.mc_send/api.mc_reply stay service-neutral
Terminal API: script-owned terminal windows only, no BBS assumptions
Retro-BBS: lives in assets/scripts/bbs.lua as one script/service
```

Do not add Comic Chat or BBS-specific branches to `IrcSession`, `IrcConnection`,
`ScriptTerminalDialog`, or `ScriptTerminalManager`. Service behavior belongs in
Lua scripts or future service-specific modules layered on top of MC DATA.

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
