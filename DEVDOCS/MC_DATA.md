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
- Pop-out dialogs, each with a File/Settings menu bar.
  - File: Close (hide only), Kill Terminal (destroy session).
  - Settings: Font (curated list + chooser), Font Size, Terminal Size (80x25 / 80x40).
- Terminals also appear in the network tree as `Term N` launcher nodes under the
  network the opening script ran on (label shows the script name). Clicking a
  node pops/raises the window; right-click offers Open / Kill Terminal.
- Closing or minimizing a window only HIDES it; the session stays alive and is
  re-shown from its tree node. Only Kill Terminal (menu or tree) destroys it.
- Fixed-grid profiles (ibm-vga/c64) keep their column/row count; the font scales
  to fill the window on resize. Font Size sets the base/initial window zoom.
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

## Terminal Font Decision

Working choice:

```text
JetBrains Mono
Bundled files: assets/fonts/JetBrainsMono-Regular.ttf
               assets/fonts/JetBrainsMono-Bold.ttf
License file:  assets/fonts/JetBrainsMono-OFL.txt
```

Reason:

- We want a programmer/terminal font, not a strict VGA emulator.
- JetBrains Mono is already bundled with MaxChat and loaded by the app.
- Local font coverage check showed full Box Drawing coverage (`128/128`) and
  full Block Elements coverage (`32/32`) in both regular and bold weights.
- Common BBS/ANSI samples are present, including single/double line boxes,
  shade blocks, half blocks, filled blocks, and common geometric markers.

Known-good sample:

```text
┌┬┐ │ ├┼┤ └┴┘ ═ ║ ╔╗ ╚╝ ╬ ░▒▓ █ ▄▀▌▐ ■ ● ○ ◆ ◇ ▲ ▼ ◄ ►
```

- It avoids adding another font asset, license file, and packaging path.
- It keeps the BBS/script terminal visually consistent with the existing chat
  font defaults.

Display rules:

- Use JetBrains Mono for script/BBS terminal profiles by default.
- Keep the terminal renderer Unicode-based; do not emulate VGA hardware text
  mode just to use the font.
- Do not rely on Braille Patterns or Symbols for Legacy Computing in the first
  BBS screens; bundled JetBrains Mono does not cover those ranges.
- If a future feature needs PETSCII/C64-specific legacy glyphs, add a separate
  optional terminal font/profile after checking coverage and license terms.

Alternates considered:

```text
Spleen OTF
```

- Blockier old-terminal feel, BSD-2-Clause license, and CP437-capable in larger
  sizes.
- Keep as an optional future terminal style if JetBrains Mono feels too modern.

```text
PxPlus IBM VGA 8x16
```

- Closer to real DOS/VGA, but CC BY-SA 4.0 adds more attribution/share-alike
  concerns than we need for a bundled app font.
- Good fallback if we later want a stricter ANSI-art profile.

```text
Departure Mono
```

- Modern retro programmer font with SIL OFL licensing.
- Worth rechecking if we want a softer sci-fi terminal look.
- Needs box/block glyph coverage verified before bundling.

```text
Cozette
```

- MIT-licensed bitmap programming font.
- Good symbol coverage, but the default feel is smaller/cozier than the BBS
  terminal target.

```text
Cascadia Mono
```

- SIL OFL and broadly available.
- Safe modern fallback, but not blocky enough for the old BBS look.

## Lua Terminal API

```lua
api.terminal_open(id, title, profile_or_cols, rows)
api.terminal_close(id)
api.terminal_clear(id)
api.terminal_write(id, text)
api.terminal_frame(id, ops)
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

Script layout decision:

- Keep Retro-BBS in one `bbs.lua` script for the first protocol passes.
- Reason: one script lets one MaxChat instance act as server, client, or both,
  which is useful for local two-terminal testing.
- Split into `bbs_server.lua` and `bbs_client.lua` after static cache/manual BBS
  testing proves the protocol shape.
- When split, keep shared MC DATA framing helpers in a small common module only
  if the Lua sandbox/module policy makes that practical.

Commands:

```text
/bbsserve [name]
/bbsconfig
/bbsconfig name <name>
/bbsconfig sysop <name>
/bbsconfig welcome <text>
/bbsconfig profile <ibm-vga|c64|free>
/bbs <nick> [bbs_id]
/bbscache
/bbscache clear
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
Xllll<data>   extended write; llll is four-digit hex byte length
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
- `api.terminal_frame(id, ops)` parses and applies this op stream to a terminal
  grid.
- Retro-BBS uses `MC DATA bbs T <ops>` for compact display updates.
- Plain ASCII is cheapest.
- Unicode box/block characters work but cost more bytes.
- Future CP437/PETSCII helpers may map compact byte values to display glyphs.
- Colors are logical terminal attributes; the renderer decides the final theme
  palette.

## Static Frame Cache Draft

Static frame caching is the next planned optimization after compact `T` frames.
The idea is that menus, login art, help pages, and board chrome often do not
change between visits. The server can send a static frame once, and later tell
the client to replay it locally with a tiny command.

Capability negotiation:

```text
HELLO payload includes caps=T,S
```

Rules:

- If the peer does not advertise `S`, send normal `T` frames.
- Cache keys include `service + bbs_id + page_id + hash`.
- `page_id` names the static page, such as `login`, `main`, `about`, or
  `hangman`.
- `hash` changes when the static frame content changes.
- The hash prevents stale cached menus after script updates.
- Static frames must never contain secrets or user-specific private data.
- Dynamic overlays carry the user-specific parts after a cached page is shown.

Verbs:

```text
T <ops>                 apply transient frame immediately
S <id> <hash> <ops>     store static frame and apply it
R <id> <hash>           replay cached static frame if present
Q <id> <hash>           request static frame resend because cache is missing
D <ops>                 apply dynamic overlay after static/replayed frame
```

Example first visit:

```text
MC DATA bbs S main A94F2C CP0101W09Retro-BBS...
MC DATA bbs D P1801W06login>
```

Example later visit:

```text
MC DATA bbs R main A94F2C
MC DATA bbs D P1801W06login>
```

Missing cache:

```text
MC DATA bbs Q main A94F2C
```

Server response:

```text
MC DATA bbs S main A94F2C <ops>
```

Implementation notes:

- Start with in-memory cache per app session.
- `/bbscache` shows local cache size and server-side static-frame counters.
- `/bbscache clear` clears the local static-frame cache for manual testing.
- The server console shows static frames sent, cache replays, cache misses, and
  fallback `T` frame sends.
- Later, persistent cache may live under the script data directory.
- Keep the resend path simple: if the server cannot find the requested static
  frame, send a normal `T` frame instead.
- Static frame cache should be implemented before bitmap assets because bitmap
  transfer also needs asset identity, hash validation, and replay semantics.

## Bitmap Cell Art Draft

Bitmap cell art is a future extension for old-school BBS graphics over MC DATA.
It should ride on the same cache idea as static frames, because bitmap assets
are usually reused rather than sent every time.

Target:

```text
80x25 terminal cells
```

Useful sizes:

```text
80x25 cell pixels = 2000 bits = 250 raw bytes = 500 hex chars
80x50 half-block pixels = 4000 bits = 500 raw bytes = 1000 hex chars
```

Encoding:

- `raw1`: packed 1-bit pixels, hexadecimal text.
- `rle1`: simple 1-bit run-length encoding, hexadecimal text.
- Use `raw1` when RLE would grow the payload.
- Use `rle1` for sparse logos, borders, and large flat areas.
- Avoid color bitmap transfer in v1; use terminal attributes and overlays for
  color first.

Verbs:

```text
B <id> <w> <h> <hash> <enc> <chunk>/<total> <data>
I <id> <hash> Prrcc
```

Meaning:

- `B` stores one bitmap asset chunk.
- `I` inserts/renders a cached bitmap asset at a terminal position.
- `id` names the bitmap asset, such as `logo`.
- `hash` validates that the cached image is the expected version.
- `enc` is `raw1` or `rle1`.

Example:

```text
MC DATA bbs B logo 80 25 A94F raw1 1/2 FFEEDD...
MC DATA bbs B logo 80 25 A94F raw1 2/2 A0B1C2...
MC DATA bbs I logo A94F P0101
```

Rendering options:

- One terminal cell per pixel: map `0` to space and `1` to `█`.
- Half-block mode: map two vertical pixels into ` `, `▀`, `▄`, or `█`.
- Verify font coverage before using quadrant/legacy block characters.

Capability negotiation:

```text
HELLO payload includes caps=T,S,B1
```

Rules:

- If the peer does not advertise `B1`, do not send bitmap assets.
- Bitmap transfers should respect the same flood limits as terminal frames.
- Missing bitmap cache should use a request/resend flow like static frames.
- Bitmap support is optional; text/ANSI BBS screens must still work without it.

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
