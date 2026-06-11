# MaxChat (C++) scripting — Lua

MaxChat's C++ port scripts in **Lua 5.4**. (The Python app used Python plugins;
embedding CPython in a C++/Qt app is heavy and a packaging headache, so the port
switched to Lua — tiny, MIT-licensed, purpose-built for embedding. The trade-off
is that Python plugins must be rewritten in Lua; the three bundled examples have
been converted, below.)

> **Status (2026-06-10):** the Lua **API contract + example scripts are defined**
> (this file + `assets/scripts/*.lua`). The C++ Lua **host** — embedding the
> interpreter, dispatching hooks, and the sandbox — is **still to build**
> (AUDIT.md FIX BACKLOG #6). This doc IS the contract that host implements.

## Scripts

A script is a `.lua` file in `<config>/scripts/`. A leading underscore
(`_hello.lua`) means it is **not** auto-loaded. Manage at runtime with
`/load <name>`, `/unload <name>`, `/reload <name>` (name = filename without
`.lua`). The examples in `assets/scripts/` are seeded into `<config>/scripts/`
on first run.

## Hooks

Define any of these as global functions. Each receives the shared `api` table
first (mirrors the Python `api` first-arg convention).

| Hook | Signature | Notes |
|------|-----------|-------|
| `on_load` | `(api)` | script loaded |
| `on_unload` | `(api)` | script unloaded |
| `on_message` | `(api, network, target, nick, text)` | a PRIVMSG arrived |
| `on_join` | `(api, network, channel, nick)` | someone joined |
| `on_command` | `(api, command, args)` → `bool` | a slash command was typed; **return `true` to consume it** |

`on_image_paste` is **not** ported yet (the C++ input ignores image paste; see
audit S13).

## The `api` table

| Call | Returns | Does |
|------|---------|------|
| `api.echo(text)` | — | print a line in the active chat (scoped to the hook's network) |
| `api.say(target, text)` | — | send a message to a channel/nick on the active network |
| `api.insert_input(text)` | — | insert text at the cursor in the message box |
| `api.notify(title, text)` | — | fire a desktop notification |
| `api.me()` | string | your current nick (`""` if unknown) |
| `api.target()` | string | the active chat target (channel/nick); `"(server)"` on the server tab |
| `api.data_dir()` | string | path to this script's writable data folder |
| `api.append_file(name, text)` | — | append to a file **inside** the data dir (basename only — no path traversal). Use this instead of raw `io`. |
| `api.timestamp([fmt])` | string | current local time, default `YYYY-MM-DD HH:MM:SS` |

Difference from Python: `me`/`target` were **properties** there; here they are
**function calls** (`api.me()`), so they always reflect live state.

## Sandbox (host must enforce)

- Available: `math` (with `math.random` pre-seeded by the host), `string`,
  `table`, `tostring`/`tonumber`/`pairs`/`ipairs`/`select`, etc.
- Removed/restricted: `io`, `os` (except a safe time source behind
  `api.timestamp`), `package`/`require`, `dofile`/`loadfile`/`load`,
  `debug`. File writes go only through `api.append_file` (data-dir-scoped).
- Lua patterns, not full regex (e.g. `https?://%S+`, `^(%d*)d(%d+)$`).

## Bundled examples (`assets/scripts/`)

| File | Hook(s) | Shows |
|------|---------|-------|
| `_hello.lua` | on_load, on_command, on_join | the basics; `/hello` |
| `dice.lua` | on_command | `/roll [NdM]`, `/8ball` — math.random, patterns, say-vs-echo |
| `url_logger.lua` | on_load, on_message | sandbox-safe file writes via `api.append_file` |

## Python → Lua mapping (for porting more)

| Python | Lua |
|--------|-----|
| `def on_command(api, command, args):` | `function on_command(api, command, args)` |
| `return True` (consume) | `return true` |
| `api.me` / `api.target` (property) | `api.me()` / `api.target()` (call) |
| `open(os.path.join(api.data_dir(), name), "a")` + write | `api.append_file(name, text)` |
| `datetime.now()` | `api.timestamp()` |
| `random.randint(1, n)` | `math.random(1, n)` |
| `re.fullmatch(r"(\d*)d(\d+)", s)` | `s:match("^(%d*)d(%d+)$")` |
| `re.findall(r"https?://\S+", t)` | `for u in t:gmatch("https?://%S+") do` |
