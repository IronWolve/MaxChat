# Vendored Lua 5.4.9

Unmodified Lua interpreter source, used by MaxChat's scripting engine as a core build dependency.

- Upstream: https://www.lua.org/ftp/lua-5.4.9.tar.gz
- sha256: `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`
- License: MIT (see `../../THIRD_PARTY_NOTICES.md`)

Only `src/*.c` and `src/*.h` are vendored. The standalone `lua.c` (interpreter
main) and `luac.c` (compiler main) are intentionally **excluded** — we embed the
library, not the executables. Do not edit these files; to update, re-vendor a
new release and re-record the sha256.
