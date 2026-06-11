# Vendored Lua 5.4.7

Unmodified Lua interpreter source, used by MaxChat's scripting engine when built
with `-DMAXCHAT_LUA=ON`.

- Upstream: https://www.lua.org/ftp/lua-5.4.7.tar.gz
- sha256: `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`
- License: MIT (see `../../THIRD_PARTY_NOTICES.md`)

Only `src/*.c` and `src/*.h` are vendored. The standalone `lua.c` (interpreter
main) and `luac.c` (compiler main) are intentionally **excluded** — we embed the
library, not the executables. Do not edit these files; to update, re-vendor a
new release and re-record the sha256.
