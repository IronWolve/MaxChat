# MaxChat scripting — design & implementation plan

**Internal doc** (keep local; gitignore before any public push, like AUDIT.md /
DEV_NOTES.md). The *user-facing* contract is `SCRIPTING.md`. This file is the
architecture + step-by-step build plan for the scripting engine, covering BOTH
the C++ port (`maxchat-c`) and the Python original (`maxchat`).

Status: IN PROGRESS (2026-06-10). Tracked as AUDIT.md FIX BACKLOG #6.

### Progress log
- **Step 1 DONE (commit b895ecd):** Lua 5.4.7 vendored under `third_party/lua/`
  behind `option(MAXCHAT_LUA OFF)`; `maxchat_lua` static lib + gated
  `lua_smoke_test`. Linux-verified: `build-lua` (ON) compiles + passes smoke;
  default `build/` byte-unchanged (43/43, no Lua). **⚠ WINDOWS GATE:** before
  relying on scripting, run `build.bat` once with `-DMAXCHAT_LUA=ON` to confirm
  Lua compiles under the Windows toolchain (MinGW). Steps 2+ stay gated, so the
  default Windows build is unaffected until then.
- **Step 2 DONE (commit 486a3f8):** `LuaEngine` + `ScriptHost` (gated
  `maxchat_scripting` lib). One sandboxed `lua_State` per script; load + on_load/
  on_unload; sandbox opens only base/table/string/math/utf8 and strips
  load/dofile/loadfile/loadstring (no io/os/package/debug). Errors caught, never
  crash. `lua_engine_test`.
- **Step 3 DONE (commit 27c4c8e):** api core — say/insert_input/notify/me/target/
  network/timestamp + basename-jailed data_dir/append_file/read_file. 9 engine
  tests incl. `../..` traversal jailing.
- **Step 4 DONE (commit b0161e3):** LuaEngine.dispatch + typed-arg callHook;
  MainWindow implements ScriptHost and owns the engine. on_message/on_notice/
  on_join/on_part/on_quit/on_nick fire from the IRC signals. Scripting lib now
  always built (stub when OFF) and linked into app + MainWindow test. Verified
  default 43/43 and -DMAXCHAT_LUA=ON 45/45.
- **Step 5 DONE (commit 5ee599a):** on_command (scripts get first crack at any
  /command after alias expansion; true consumes) + real /scripts /load /unload
  /reload.
- **Step 6 DONE (commits f14b3c6, b79dd0c):** full api — send_raw, channels()/
  nicks(), strip(), persistent get/set (JSON in the script's data dir), and
  timers (api.timer/cancel_timer, QTimer-backed, 50ms floor, cancelled on
  unload/reload). lua_engine_test now 17 cases incl. timer lifecycle.
- **Step 7 DONE (commit 07b3257):** Settings ▸ Scripts… manager dialog
  (list + Load/Unload/Reload/Open-folder) + first-run seeding of the bundled
  examples; removed the placeholder.
- **Step 8 DONE (commit f6dddcb):** SCRIPTING.md updated to the implemented
  surface; new examples reminder.lua (timer) + seen.lua (get/set); test loads
  every bundled example. **C++ scripting is FEATURE-COMPLETE.**
- **Remaining:** Python parity (steps 9-11, additive to the existing
  scripting.py) — and the standing **Windows-build gate** (build.bat with
  -DMAXCHAT_LUA=ON) before shipping enabled.

---

## 1. Goal

Let users extend MaxChat with small scripts that react to chat and add commands,
**without** being able to harm the machine. One documented API; each client runs
it in its own native language (C++ → Lua, Python → Python). A script is
mechanically portable between the two (same hooks, same `api` calls), not
byte-identical.

## 2. Decisions (with rationale)

| # | Decision | Why | Alternative rejected |
|---|----------|-----|----------------------|
| D1 | **Lua 5.4** for the C++ client | tiny, MIT, built to embed; HexChat already ships a Lua IRC interface — proven path | LuaJIT (harder Windows/ARM build, speed unneeded) |
| D2 | **Vendor** Lua source, compile as a static lib | no system dependency; Windows `build.bat` just compiles ~35 C files | `find_package(Lua)` — breaks on machines without Lua |
| D3 | **Raw Lua C API**, no binding lib | zero extra deps; our surface is small enough to bind by hand | sol2 (heavy templates, slows compile, another vendored dep) |
| D4 | **Optional CMake flag `MAXCHAT_LUA`, default OFF** | the normal build stays untouched; flip ON only when we can verify the Windows compile (can't from WSL) | always-on (risks an untestable build break) |
| D5 | **Python client stays Python** (not Lua-via-lupa) | it already has a working in-process plugin system; native Python is its whole appeal; adds no dependency | unify on Lua via `lupa` (throws away working code, adds a dep, dubious gain) |
| D6 | **Safe-by-default sandbox** | our differentiator vs mIRC/KVIrc (sockets/DLL/file = malware vector) and even WeeChat/irssi/HexChat (inherit full host-language access) | trust-only model |
| D7 | Lua runs on the **GUI thread** | IRC volume is trivial; avoids all cross-thread/data-race complexity | worker thread (needless for this load) |

## 3. The agreed API surface

This is the contract both clients implement. (★ = NEW vs the current
`SCRIPTING.md`; everything else already exists in the Python client and is
spec'd for C++.)

### Hooks (define as global functions; first arg is always `api`)

| Hook | Signature | Notes |
|------|-----------|-------|
| `on_load` | `(api)` | script loaded |
| `on_unload` | `(api)` | script unloaded |
| `on_message` | `(api, network, target, nick, text)` | a PRIVMSG arrived |
| `on_join` | `(api, network, channel, nick)` | someone joined |
| `on_command` | `(api, command, args)` → bool | slash command typed; return true to consume |
| ★ `on_part` | `(api, network, channel, nick)` | someone left |
| ★ `on_quit` | `(api, network, nick)` | someone quit |
| ★ `on_nick` | `(api, network, old, new)` | nick change |
| ★ `on_notice` | `(api, network, target, nick, text)` | a NOTICE arrived |
| ★ `on_raw` | `(api, network, line)` | every raw inbound IRC line (power users) |

`on_image_paste` stays **deferred** (C++ input ignores image paste — audit S13).

### `api` table

| Call | Returns | Does |
|------|---------|------|
| `api.echo(text)` | — | print a line in the active (hook-scoped) buffer |
| `api.say(target, text)` | — | send a message on the active network |
| `api.insert_input(text)` | — | insert at the cursor in the message box |
| `api.notify(title, text)` | — | desktop notification |
| `api.me()` | string | your nick (`""` if unknown) |
| `api.target()` | string | active target; `"(server)"` on the server tab |
| `api.data_dir()` | string | this script's writable data folder |
| `api.append_file(name, text)` | — | append to a file **inside** the data dir (basename only) |
| `api.timestamp([fmt])` | string | local time, default `YYYY-MM-DD HH:MM:SS` |
| ★ `api.send_raw(line)` | — | send a raw IRC line on the active network (CR/LF stripped) |
| ★ `api.timer(ms, fn)` | id | call `fn()` every `ms`; returns a cancel id |
| ★ `api.cancel_timer(id)` | — | stop a timer |
| ★ `api.network()` | string | active network name |
| ★ `api.channels()` | list | open channels on the active network |
| ★ `api.nicks([target])` | list | members of `target` (default: active) |
| ★ `api.get(key)` / `api.set(key, val)` | val / — | per-script persistent prefs (strings/numbers) |
| ★ `api.read_file(name)` | string | read a file inside the data dir (basename only) |
| ★ `api.strip(text)` | string | remove IRC colour/format codes |

**Deliberately excluded** (the danger surface mIRC/KVIrc expose): raw sockets,
HTTP, arbitrary file paths, `os.execute`/process spawn, native/DLL loading,
arbitrary GUI. See §6.

## 4. C++ architecture

```
            IRC events                     user types /cmd
                │                                │
   IrcConnection signals              CommandParser → UserCommandType::Scripts
                │                                │  (and unknown /cmd → on_command)
                ▼                                ▼
        MainWindow slots  ───────────────►  LuaEngine
        (messageReceived,                  ├─ QHash<name, lua_State-script>  (or one state, per-script env)
         userJoined, …)                    ├─ dispatch(hook, args…) → bool
                                           ├─ registers the `api` table (C closures)
                                           └─ owns timers (QTimer per api.timer)
                                                     │  api.say/echo/notify/…
                                                     ▼
                                           MainWindow action methods
                                           (sendMessage, appendSystemLine, notify, …)
```

- **`src/scripting/LuaEngine.{h,cpp}`** — new. Owns the Lua VM(s), loads scripts,
  builds the sandbox + `api` table, dispatches hooks. Knows nothing about Qt
  widgets directly: it talks to MainWindow through a **narrow callback interface**
  (`ScriptHost`) so the engine stays unit-testable headless.
- **`ScriptHost` interface** (pure virtual) — the methods the `api` needs:
  `say`, `echo`, `insertInput`, `notify`, `me`, `target`, `network`, `channels`,
  `nicks`, `sendRaw`. MainWindow implements it. Tests implement a fake.
- **One `lua_State` per script** (clean isolation; unload = close state) OR a
  shared state with per-script environments. Decision deferred to build time;
  per-script states are simpler to reason about and to unload — lean that way.
- **Hook scoping**: when MainWindow calls `engine.dispatch("on_message", net, …)`
  it sets the engine's "current network" so `api.echo` lands in the right buffer
  (mirrors the Python `ScriptManager.ctx_net`).
- **`api.say` / `send_raw`** route to the same code path as the input box, so
  flood-guard, CR/LF stripping, and logging all apply — scripts get no privileged
  bypass.

## 5. Sandbox

Built once per script state, before any user code runs:

- **Keep**: `math` (with `math.random` seeded by the host), `string`, `table`,
  `tostring`/`tonumber`/`type`/`pairs`/`ipairs`/`next`/`select`, `pcall`/`error`,
  `os.time`/`os.date`/`os.clock` only.
- **Remove/replace**: `io`, the rest of `os` (`execute`, `remove`, `getenv`,
  `exit`, `tmpname`…), `package`/`require`, `dofile`/`loadfile`/`load`/`loadstring`,
  `debug`, `collectgarbage` (or restrict).
- Set up by building a fresh environment table with only the allowed globals,
  then loading the script chunk with that environment (`lua_setupvalue` of `_ENV`
  on 5.4). No `require` means no escaping to the C modules.
- File access **only** through `api.append_file`/`api.read_file`, which:
  `QFileInfo(name).fileName()` to force basename, reject empty, resolve under the
  script's data dir, and never follow `..`. (Same rule already proven in
  `SoundPlayer::resolveSoundPath` and the DCC path checks.)

## 6. Threat model

A script may be hostile or buggy. Guarantees:

1. **No filesystem reach** beyond its own data dir (no read of `~/.ssh`, no write
   to startup folders).
2. **No network of its own** — it can only send IRC on networks you're already on
   (so the worst case is "annoying in a channel", not "exfiltrate files / join a
   botnet" — the classic mIRC attack).
3. **No process / native code execution.**
4. **No crashing the client** — every hook call is wrapped in `lua_pcall`; an
   error prints `[scripts] <name>.<hook>: <msg>` and disables nothing else.
5. **Resource sanity** — optional instruction-count hook (`lua_sethook`) to break
   runaway loops; timers capped to a sane minimum interval. (Stretch goal.)

Documented limit: this is a *script* sandbox, not a hostile-multi-tenant jail.
We block the obvious foot-guns and accidental damage; we still tell users to
install scripts they trust.

## 7. Script lifecycle

- Scripts live in `<config>/scripts/*.lua`. Leading `_` = not auto-loaded.
- On first run, seed the bundled examples from `assets/scripts/` into the config
  dir (don't overwrite user edits).
- Startup: auto-load every non-`_` script.
- Runtime commands (wire the existing `UserCommandType::Scripts`):
  `/scripts` (list), `/load <name>`, `/unload <name>`, `/reload <name>`.
- **Scripts dialog** (replace the menu placeholder): list with load/unload/reload
  buttons, "open scripts folder", and per-script error display.

## 8. Testing (C++)

The engine is headless-testable via the `ScriptHost` fake — no GUI needed:

- `lua_engine_test`: load a script that calls each `api.*`; assert the fake host
  recorded the right calls.
- Sandbox tests: a script doing `os.execute("…")`, `io.open("/etc/passwd")`,
  `require("os")`, `load("…")`, `api.append_file("../../x", …)` — assert each is
  blocked/no-op and the host filesystem is untouched.
- Dispatch tests: `on_command` returning true consumes; an erroring hook is
  caught and doesn't abort the run.
- Reuse the `QTemporaryDir` pattern from `sound_player_test`.

## 9. Build / vendoring

- `third_party/lua/` — the unmodified Lua 5.4 `.c`/`.h` (exclude `lua.c`,
  `luac.c` — we don't want the standalone interpreter/compiler `main`s).
- `add_library(maxchat_lua STATIC …)` gated on `option(MAXCHAT_LUA "" OFF)`.
- When ON: define `MAXCHAT_LUA`, compile `LuaEngine.cpp`, link `maxchat_lua`.
- When OFF: `LuaEngine` compiles to a stub whose `/load` etc. say
  "built without scripting" — so the menus/commands still exist, just inert.
- Add Lua's copyright to `THIRD_PARTY_NOTICES.md`.

## 10. Python parity (`maxchat`)

Python already has the engine (`scripting.py`: `ScriptManager` + `ScriptAPI`).
Parity work is **additive** — bring it up to §3:

- Add the ★ hooks to `_HOOKS` and dispatch them from the matching client signals.
- Add the ★ `api` methods (`send_raw`, `timer`/`cancel_timer`, `network`,
  `channels`, `nicks`, `get`/`set`, `read_file`, `strip`).
- `me`/`target` stay **properties** in Python (already are); they are **calls**
  in Lua — already noted in the SCRIPTING.md mapping table.
- Python remains "full Python, trust your scripts" (it can't truly sandbox
  CPython) — document that the *Lua* client is the sandboxed one. This asymmetry
  is intentional and must be stated in both docs.
- Track every change in `maxchat/DEVDOCS/BACKPORTS.md` (user drives git there).

## 11. Documentation plan

- **`SCRIPTING.md`** (both repos, shipped): update to the §3 surface; keep the
  Python↔Lua mapping table; add examples for timers + state queries.
- **Example scripts**: add one using a timer (e.g. `away_reminder`) and one using
  state (`api.nicks`) — in both `assets/scripts/*.lua` (C++) and the Python
  examples, so the docs have real, runnable references on each side.
- **This file**: keep the design current; log wrong-turns in DEV_NOTES.
- Mirror this design doc into `maxchat/DEVDOCS/` so it isn't trapped in one repo.

## 12. Phased steps (each ≈ one commit-sized chunk)

**C++ (maxchat-c):**

1. **Vendor Lua + CMake flag.** Add `third_party/lua`, `maxchat_lua` lib,
   `MAXCHAT_LUA` option (OFF). Confirm a trivial `lua_State` opens/closes in a
   smoke test. *(Verify Windows build here before going further.)*
2. **`ScriptHost` interface + `LuaEngine` skeleton.** Open a state, sandbox it,
   load a `.lua` from a dir, run `on_load`. Headless test with a fake host.
3. **`api` core**: echo/say/insert_input/notify/me/target/timestamp +
   data-dir file calls (append/read). Sandbox tests.
4. **Hook dispatch**: wire `on_message`/`on_join`/`on_part`/`on_quit`/`on_nick`/
   `on_notice` from MainWindow signals (network-scoped echo).
5. **`on_command` + the `/load /unload /reload /scripts` commands** (replace the
   `UserCommandType::Scripts` placeholder). Unknown `/cmd` → `on_command`.
6. **New api**: send_raw, network/channels/nicks (via ChatBufferStore), get/set
   prefs, strip, timers (QTimer-backed).
7. **Scripts dialog** (replace the menu placeholder) + first-run seeding.
8. **Examples + SCRIPTING.md update + THIRD_PARTY_NOTICES** (Lua license).

**Python (maxchat) — after the C++ surface settles:**

9. Add ★ hooks to `scripting.py` + dispatch from client signals.
10. Add ★ `api` methods.
11. Update Python example scripts + SCRIPTING.md; log in BACKPORTS.md.

## 13. Open questions

- One `lua_State` per script vs shared-state-with-envs (lean: per-script).
- Timer ownership across unload/reload (cancel a script's timers on unload).
- Instruction-count watchdog now or later (lean: later, stretch).
- Do we expose `api.send_raw` to *all* scripts or gate it behind a per-script
  "trusted" flag? (lean: expose; it's IRC-only and you already trust the script.)
