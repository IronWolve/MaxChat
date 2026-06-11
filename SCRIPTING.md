# MaxChat (C++) scripting — Lua

MaxChat's C++ port scripts in **Lua 5.4**. (The Python app used Python plugins;
embedding CPython in a C++/Qt app is heavy and a packaging headache, so the port
switched to Lua — tiny, MIT-licensed, purpose-built for embedding. The trade-off
is that Python plugins must be rewritten in Lua; the three bundled examples have
been converted, below.)

> **Status:** **implemented and on by default.** The interpreter (Lua 5.4,
> vendored) is built into normal builds — `build.bat` includes it; CMake users
> get it unless they pass `-DMAXCHAT_LUA=OFF` (or `build.bat nolua`) for a lean
> build. The engine, sandbox, all hooks, and the full `api` table below are in
> place and unit-tested.

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
| `on_notice` | `(api, network, target, nick, text)` | a NOTICE arrived |
| `on_join` | `(api, network, channel, nick)` | someone joined |
| `on_part` | `(api, network, channel, nick)` | someone left |
| `on_quit` | `(api, network, nick)` | someone quit |
| `on_nick` | `(api, network, old, new)` | a nick change |
| `on_command` | `(api, command, args)` → `bool` | a slash command was typed; **return `true` to consume it** |

`on_image_paste` is **not** ported (the C++ input ignores image paste; audit S13).

## The `api` table

| Call | Returns | Does |
|------|---------|------|
| `api.echo(text)` | — | print a line in the active chat (scoped to the hook's network) |
| `api.say(target, text)` | — | send a message to a channel/nick on the active network |
| `api.send_raw(line)` | — | send a raw IRC line on the active network (CR/LF stripped) |
| `api.insert_input(text)` | — | insert text at the cursor in the message box |
| `api.notify(title, text)` | — | fire a desktop notification |
| `api.me()` | string | your current nick (`""` if unknown) |
| `api.target()` | string | the active chat target (channel/nick); `"(server)"` on the server tab |
| `api.network()` | string | the active network name |
| `api.channels()` | table | array of open channels on the active network |
| `api.nicks([target])` | table | array of members of `target` (default: the active target) |
| `api.strip(text)` | string | remove IRC colour/formatting codes |
| `api.timestamp([fmt])` | string | current local time, default `YYYY-MM-DD HH:MM:SS` (`fmt` is a Qt date format) |
| `api.data_dir()` | string | path to this script's writable data folder |
| `api.append_file(name, text)` | — | append to a file **inside** the data dir (basename only — no traversal) |
| `api.read_file(name)` | string\|nil | read a file inside the data dir (nil if missing) |
| `api.get(key)` | value\|nil | read a persisted pref (string/number/bool) for this script |
| `api.set(key, value)` | — | persist a pref (string/number/bool) for this script |
| `api.timer(ms, fn)` | id | call `fn()` every `ms` (floor 50ms); returns a cancel id |
| `api.cancel_timer(id)` | — | stop a timer (also auto-cancelled when the script unloads) |

Difference from Python: `me`/`target` were **properties** there; here they are
**function calls** (`api.me()`), so they always reflect live state.

## Sandbox (enforced by the engine)

Scripts start **fully sandboxed**. The always-available core:

- `math` (with `math.random` pre-seeded), `string`, `table`, `utf8`,
  `tostring`/`tonumber`/`pairs`/`ipairs`/`select`, `pcall`, and a read-only `os`
  subset (`os.time`/`date`/`clock`/`difftime`).
- File access through `api.append_file`/`api.read_file` (basename-only, jailed to
  the script's own data dir) and persisted prefs via `api.get`/`api.set`.
- Lua patterns, not full regex (e.g. `https?://%S+`, `^(%d*)d(%d+)$`).
- Script/hook errors are caught: a broken hook prints `[scripts] name.hook: …`
  and never crashes the client.

By default there is **no** `io`, no process spawning, no `require`, no network,
and `os.exit` is never available.

### Permissions (Preferences ▸ Scripts)

Extra abilities are opt-in per capability — grant them only for scripts you
trust. Changing any of these reloads your scripts under the new rules.

| Permission | Unlocks |
|------------|---------|
| **Read files** | `io.open(path, "r")` within the allowed folders |
| **Write files** | `io.open(path, "w"/"a")` within the allowed folders |
| **Run programs** | `os.execute`, `io.popen`, `os.remove`/`rename`/`getenv` |
| **Load modules** | `require`/`package` + `load`/`loadfile`/`dofile` |
| **Network access** | `api.http_get(url)` → page body (string) or nil |

`os.exit` is removed even with "Run programs" — a script can never quit the
client. File reads/writes are confined to the **allowed folders** list (the
script's own data dir is always allowed); add folders or a drive/path there.

## Bundled examples (`assets/scripts/`)

| File | Hook(s) | Shows |
|------|---------|-------|
| `_hello.lua` | on_load, on_command, on_join | the basics; `/hello` |
| `dice.lua` | on_command | `/roll [NdM]`, `/8ball` — math.random, patterns, say-vs-echo |
| `url_logger.lua` | on_load, on_message | sandbox-safe file writes via `api.append_file` |
| `reminder.lua` | on_command | `/remind <secs> <text>` — one-shot `api.timer` + `api.cancel_timer` |
| `seen.lua` | on_message, on_command | `/seen <nick>` — persistent `api.get`/`api.set` |

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
