-- bbs.lua - Retro-BBS demo over MC DATA CTCP.
--
-- This script uses the current IRC connection only. It does not open a new
-- socket or private-message chat; traffic rides over CTCP MC DATA frames.
--
-- Commands:
--   /bbsserve [name]                 start the local BBS server console
--   /bbsconfig                       show server config
--   /bbsconfig name <name>           set BBS name
--   /bbsconfig sysop <name>          set sysop display name
--   /bbsconfig welcome <text>        set welcome line
--   /bbsconfig profile <profile>     set ibm-vga, c64, or free
--   /bbs <nick> [bbs_id]             dial a BBS hosted by another user
--   /bbscache [clear]                show or clear local static frame cache
--   /bbsbook                         list address-book entries
--   /bbsbook add <label> <nick> [bbs_id] [profile]
--   /bbsbook dial <label>
--   /bbsbook remove <label>

local SERVICE = "bbs"
local DEFAULT_BBS_ID = "retro-bbs"
local DEFAULT_BBS_NAME = "Retro-BBS"
local DEFAULT_PROFILE = "ibm-vga"
local SERVER_TERM = "server"
local MAX_LINE = 220
local DEMO_USER = "sir_iw"
local DEMO_PASSWORD = "bbsiscool"
local CLIENT_CAPS = "T,S,B1"

local server_running = false
local server = {
  id = DEFAULT_BBS_ID,
  name = DEFAULT_BBS_NAME,
  sysop = "",
  welcome = "Welcome to Retro-BBS.",
  profile = DEFAULT_PROFILE,
  pages = 0,
  connects = 0,
  mirror_key = nil,
  static_sent = 0,
  cache_replays = 0,
  cache_misses = 0,
  fallback_frames = 0
}
local sessions = {}
local clients = {}
local static_cache = {}
local hello_cooldown = {}      -- network|nick -> true while a peer's HELLO is throttled
local MAX_SESSIONS = 16
local HELLO_COOLDOWN_MS = 3000
local board = {
  { from = "sysop", text = "Welcome. Leave a short note with P <message>." }
}

-- 1-bit pic gallery (B/I bitmap verbs, caps=B1). 80x50 px rendered as
-- half-blocks in 80x25 cells. Public-domain space-agency photos, reduced to
-- 1-bit: thresholded images compress well with rle1, dithered ones ship raw1.
local PICS = {
  { id = "earth", title = "Earth from space", w = 80, h = 50, enc = "rle1",
    data = {
      "103020311E321D341B3619070129020419030627030517030727030615030827020715030827020813040703011D060B13040620090B1204061B0E0B110605190903040C1008020202030112010304040201020B100B04210102020B0F0D0424010C0E0E0324010C0E0E03310D1102300D2B01170D2B02160D22010802160D430D0E01340D0D030702290E0D",
      "030306290E0C0303062A0F12042B0F13032B0F13041401160E13051202151012051103151012041302151011030E0206080E1120120C121F130C121F0C03040C131D08020206020B151C06030207030A151D0305020C02051731030419300205192F01061C331F302030202F222D2405042212"
    } },
  { id = "moon", title = "The Moon", w = 80, h = 50, enc = "rle1",
    data = {
      "1038180C012B1805082C16050A2C15050B2B15030F2A1402102A252C242C242D2409032024080602011C22090A1B0B0214090B1B0B0213090D1A1D01010A0E1A1E0A0E0F030720090E0E050522080E0E060423070E0D070413040B070F0E0507100D02081003020805080F17160705080B020214040301040D08030A0903020E0104050301040E1201020803",
      "020D0C06111001010904010D0E03120F02010904010D0403060411100D02010D04060306110F0D010209020104101102030A120707101A061207080F1A0604010D07081009020E0604010D08041407070A070301111D06070907040210200208080804020B02032A070905030803032B06090503090204040125050906080A040225040906090A0302320708",
      "0A04013108080602020302310906060307300A050505082E0C040506072E0C050506062D0E050505062C10050306062C10060206072A09"
    } },
  { id = "astro", title = "Moonwalker", w = 80, h = 50, enc = "raw1",
    data = {
      "12AAAADBBDFFFFFFFFFE24AAAB6CF6B7FFF6F7BB1555B5DBBBFDFFFFDEFD52A4D6EEFD57FDFDFFFE2D537DB2B52BFFDFFFEA05AADFFB76B5FFFFDBBD556DF6EDFA57FDFF6DF52AB6AFFF556AFFFFFFFEB55BFB7BFA7FFFFFFFFF6FFEBFFF5D5FEFFFFFFFBAD7EDFFF5BFFFFFEDFF6FFFBFFEBEFFFFDEF7FEBFBDFFFBFDBFFFFFD0BF7BFFFFFF1BFFFFFEFFEA",
      "AEEFF6B6AFFBFFFF2F7D7BBBFF5E9DFFFFFFF7F6AF6EDB052FFFFFFFFFFE769BF77557FFFFFFFFFF2B0DFEDAAFEFFFFFFFFF16FFFBBFFFFFFFFFFFFF45B77FFB7777FD7FFFFF22FFFFDDFFEFF757FFFF08DBEF6BBDF3596FFFFF25FEFBB6FFDDFABFFFFF5F57FFFBD6F7F5FFFFFF755AFFDFFFFABB7EFFFFADAADFFFD5A88ADFFFFF52D57FFFFAF557FFFF7D",
      "0977B7FDD15F82FFDBFF549D7EFFA96D4B77F5F52AD7DBFF513B2FFDDAAC2556FFD485D4BFF7F57756ABAAB80A43775F5FDD2AD56AC42A1DFDEBEBFF55255A20A00BAB5ABEFD554AAC8090ACA89777B629015A010176F401BDFF0AA4540201DB55246F6E2952400004BFED413BFF0448000016DEF6FDDEFF20511000092BDB56DBBF00A80000042BFFFFFFEF",
      "2058000017157AADD6BF28A000008956D7F7FFDF2AA00000222A501D5ABF09000800000000436FCF0400100100012000152F100000105000800000970000002A8242AB00000500000055401155600000"
    } },
}


local function trim(s)
  return (tostring(s or ""):gsub("^%s+", ""):gsub("%s+$", ""))
end

local function words(s)
  local out = {}
  for w in tostring(s or ""):gmatch("%S+") do out[#out + 1] = w end
  return out
end

local function starts_with(s, prefix)
  return tostring(s or ""):sub(1, #prefix) == prefix
end

-- Cut a string to at most `limit` BYTES without splitting a UTF-8 sequence
-- (the C++ frame parser rejects a W length that splits a character, which
-- would make the whole frame fail to render).
local function cut_utf8(s, limit)
  if #s <= limit then return s end
  local i = limit
  while i > 0 do
    local b = s:byte(i)
    if b < 0x80 then break end          -- ASCII: safe boundary after it
    if b >= 0xC0 then i = i - 1; break end -- lead byte: drop the partial char
    i = i - 1                            -- continuation byte: keep walking back
  end
  return s:sub(1, math.max(0, i))
end

local function clean_line(s)
  s = trim(s):gsub("[\r\n\t]", " ")
  if #s > MAX_LINE then s = cut_utf8(s, MAX_LINE - 3) .. "..." end
  return s
end

-- Like clean_line but KEEPS leading spaces, so pre-aligned/centered art rows
-- survive (only control chars and trailing spaces are dropped). Truncation is
-- by byte length to stay inside the MC DATA payload guard.
local function clean_frame_line(s)
  s = tostring(s or ""):gsub("[\r\n\t]", " "):gsub("%s+$", "")
  if #s > MAX_LINE then s = cut_utf8(s, MAX_LINE - 3) .. "..." end
  return s
end

local MAX_BOARD = 50

local function save_config(api)
  api.set("server:name", server.name)
  api.set("server:sysop", server.sysop)
  api.set("server:welcome", server.welcome)
  api.set("server:profile", server.profile)
end

local function load_config(api)
  server.name = tostring(api.get("server:name") or DEFAULT_BBS_NAME)
  server.sysop = tostring(api.get("server:sysop") or api.me() or "")
  server.welcome = tostring(api.get("server:welcome") or "Welcome to Retro-BBS.")
  server.profile = tostring(api.get("server:profile") or DEFAULT_PROFILE)
end

local function parse_kv(payload)
  local t = {}
  for key, value in tostring(payload or ""):gmatch("([%w_]+)=([^;]*)") do
    t[key] = value
  end
  return t
end

local function has_cap(caps, wanted)
  for cap in tostring(caps or ""):gmatch("[^,]+") do
    if trim(cap):upper() == wanted then return true end
  end
  return false
end

local function session_key(network, nick, bbs_id)
  return tostring(network or "") .. "|" .. tostring(nick or ""):lower() .. "|" .. tostring(bbs_id or DEFAULT_BBS_ID):lower()
end

local function client_key(network, nick, bbs_id)
  return session_key(network, nick, bbs_id)
end

local function client_term(nick, bbs_id)
  return "client:" .. tostring(nick or "unknown"):gsub("[^%w%-_]", "_") .. ":" .. tostring(bbs_id or DEFAULT_BBS_ID):gsub("[^%w%-_]", "_")
end

-- network is optional: empty/nil uses the dispatch-context network. Session
-- and client sends always pass their stored network so traffic stays on the
-- connection it started on (sysop console acts across networks).
local function send(api, nick, verb, payload, network)
  api.mc_send(nick, SERVICE, verb, clean_line(payload or ""), network or "")
end

local function send_raw(api, nick, verb, payload, network)
  api.mc_send(nick, SERVICE, verb, tostring(payload or ""), network or "")
end

local function hex2(n)
  return string.format("%02X", math.max(0, math.min(255, tonumber(n) or 0)))
end

local function frame_write(text)
  text = clean_frame_line(text or "")
  if #text <= 255 then
    return "W" .. hex2(#text) .. text
  end
  return "X" .. string.format("%04X", #text) .. text
end

local function frame_pos(row, col)
  return "P" .. hex2(row) .. hex2(col)
end

local function hex1(n)
  return string.format("%X", math.max(0, math.min(15, tonumber(n) or 7)))
end

-- Screen rows are plain strings (fg 7 on bg 0) or { text=..., fg=..., bg=... }
-- tables using the 16-color VGA palette indices.
local function row_parts(row)
  if type(row) == "table" then
    return tostring(row.text or ""), tonumber(row.fg) or 7, tonumber(row.bg) or 0
  end
  return tostring(row or ""), 7, 0
end

local function static_cache_key(bbs_id, page_id, hash)
  return tostring(bbs_id or DEFAULT_BBS_ID):lower() .. "|" .. tostring(page_id or "") .. "|" .. tostring(hash or "")
end

local function static_cache_count()
  local count = 0
  for _ in pairs(static_cache) do count = count + 1 end
  return count
end

local function frame_hash(text)
  local h = 0
  text = tostring(text or "")
  for i = 1, #text do
    h = (h * 33 + string.byte(text, i)) % 4294967296
  end
  return string.format("%08X", h)
end

local function terminal_frame_chunks(lines)
  local chunks = {}
  local current = "C"
  for row, value in ipairs(lines or {}) do
    local text, fg, bg = row_parts(value)
    -- The A op is emitted per row (not only on change) so chunks stay
    -- self-contained — static cache parts replay independently.
    local op = frame_pos(row, 1) .. "A" .. hex1(fg) .. hex1(bg) .. frame_write(text)
    -- 300, not the 350 transport cap: static sends prepend "<page#N> <hash> "
    -- (~20 bytes) and the whole payload must stay under the MC DATA limit.
    if #current + #op > 300 and #current > 0 then
      chunks[#chunks + 1] = current
      current = ""
    end
    current = current .. op
  end
  if #current > 0 then chunks[#chunks + 1] = current end
  return chunks
end

local function send_terminal_frames(api, s, lines)
  local chunks = terminal_frame_chunks(lines)
  server.fallback_frames = server.fallback_frames + #chunks
  for _, chunk in ipairs(chunks) do
    send_raw(api, s.nick, "T", chunk, s.network)
  end
end

-- Multi-chunk pages are cached per chunk: part ids "main#1", "main#2", ...
-- reuse the existing S/R/Q verbs unchanged (each part is its own cached page).
local function send_static_or_terminal_frame(api, s, page_id, lines)
  local chunks = terminal_frame_chunks(lines)
  if s.supports_static then
    s.static_defs = s.static_defs or {}
    s.static_sent = s.static_sent or {}
    for i, ops in ipairs(chunks) do
      local part_id = (#chunks == 1) and page_id or (page_id .. "#" .. i)
      local hash = frame_hash(ops)
      local sent_key = part_id .. "|" .. hash
      s.static_defs[sent_key] = ops
      if s.static_sent[sent_key] then
        server.cache_replays = server.cache_replays + 1
        send_raw(api, s.nick, "R", part_id .. " " .. hash, s.network)
      else
        server.static_sent = server.static_sent + 1
        send_raw(api, s.nick, "S", part_id .. " " .. hash .. " " .. ops, s.network)
        s.static_sent[sent_key] = true
      end
    end
  else
    server.fallback_frames = server.fallback_frames + #chunks
    for _, chunk in ipairs(chunks) do
      send_raw(api, s.nick, "T", chunk, s.network)
    end
  end
end

local function frame_encode(s)
  s = clean_line(s or "")
  s = s:gsub("%%", "%%25")
  s = s:gsub("|", "%%7C")
  if s == "" then return "%20" end
  return s
end

local function frame_decode(s)
  if s == "%20" then return "" end
  s = tostring(s or "")
  s = s:gsub("%%7[Cc]", "|")
  s = s:gsub("%%25", "%%")
  return s
end

local function frame_parts(payload)
  local parts = {}
  for part in (tostring(payload or "") .. "|"):gmatch("([^|]*)|") do
    parts[#parts + 1] = frame_decode(part)
  end
  return parts
end

-- ===== bitmap art client (B/I verbs) =====
local bitmap_cache = {}   -- bbs_id|id|hash -> {w,h,enc,data}

local function bitmap_key(bbs_id, id, hash)
  return tostring(bbs_id):lower() .. "|" .. tostring(id) .. "|" .. tostring(hash)
end

local function decode_bits(enc, data, total)
  local bits = {}
  if enc == "raw1" then
    for i = 1, #data do
      local v = tonumber(data:sub(i, i), 16)
      if not v then return nil end
      bits[#bits + 1] = (v >= 8) and 1 or 0; v = v % 8
      bits[#bits + 1] = (v >= 4) and 1 or 0; v = v % 4
      bits[#bits + 1] = (v >= 2) and 1 or 0; v = v % 2
      bits[#bits + 1] = v
    end
  elseif enc == "rle1" then
    -- alternating runs starting with bit 0; one hex byte per run; a 00 run
    -- flips color without pixels (encodes runs longer than 255)
    local cur = 0
    for i = 1, #data - 1, 2 do
      local n = tonumber(data:sub(i, i + 1), 16)
      if not n then return nil end
      for _ = 1, n do bits[#bits + 1] = cur end
      cur = 1 - cur
    end
  else
    return nil
  end
  if #bits < total then return nil end
  return bits
end

-- Local-render variant of frame_write: no MAX_LINE cap. These ops feed
-- api.terminal_frame directly and never cross MC DATA.
local function frame_write_full(text)
  if #text <= 255 then return "W" .. hex2(#text) .. text end
  return "X" .. string.format("%04X", #text) .. text
end

local function render_bitmap(api, term, bm)
  local bits = decode_bits(bm.enc, bm.data, bm.w * bm.h)
  if not bits then return false end
  local ops = "C"
  for r = 1, math.floor(bm.h / 2) do
    local row = {}
    for c = 1, bm.w do
      local top = bits[(2 * r - 2) * bm.w + c] == 1
      local bot = bits[(2 * r - 1) * bm.w + c] == 1
      row[c] = (top and bot) and "\u{2588}" or (top and "\u{2580}") or (bot and "\u{2584}") or " "
    end
    ops = ops .. frame_pos(r, 1) .. "AF0" .. frame_write_full(table.concat(row))
  end
  return api.terminal_frame(term, ops)
end

-- Message board persists via the script preference store: "from|text" rows
-- (| and % escaped with the frame codec), capped at MAX_BOARD entries.
local function save_board(api)
  api.set("board:count", tostring(#board))
  for i, msg in ipairs(board) do
    api.set("board:" .. i, frame_encode(msg.from) .. "|" .. frame_encode(msg.text))
  end
end

local function load_board(api)
  local count = tonumber(api.get("board:count") or 0) or 0
  if count <= 0 then return end
  local loaded = {}
  for i = 1, math.min(count, MAX_BOARD) do
    local raw = tostring(api.get("board:" .. i) or "")
    local from, text = raw:match("^([^|]*)|(.*)$")
    if from and text ~= "" then
      loaded[#loaded + 1] = { from = frame_decode(from), text = frame_decode(text) }
    end
  end
  if #loaded > 0 then board = loaded end
end

local function send_input(api, client, text)
  -- bbs_id rides as the first token so one nick can use two boards on the
  -- same host; servers fall back to treating the whole payload as text.
  api.mc_send(client.nick, SERVICE, "INPUT",
              client.bbs_id .. " " .. clean_line(text or ""), client.network or "")
end

local function remember_screen(s, verb, payload)
  s.last = s.last or { lines = {} }
  if verb == "CLEAR" then
    s.last.lines = {}
  elseif verb == "LINE" then
    s.last.lines[#s.last.lines + 1] = payload
    while #s.last.lines > 28 do table.remove(s.last.lines, 1) end
  elseif verb == "STATUS" then
    s.last.status = payload
  elseif verb == "PROMPT" then
    s.last.prompt = payload
  end
end

local function server_status(api, text)
  if not server_running then return end
  api.terminal_status(SERVER_TERM, text)
end

local function server_write(api, text)
  if not server_running then return end
  api.terminal_write(SERVER_TERM, clean_line(text) .. "\n")
end

local function update_mirror(api, s)
  if not server_running or not s or server.mirror_key ~= s.key then return end
  api.terminal_clear(SERVER_TERM)
  api.terminal_status(SERVER_TERM, "USER " .. s.nick .. " CONNECTED - MIRROR MODE")
  for _, value in ipairs((s.last and s.last.lines) or {}) do
    local text = row_parts(value)
    api.terminal_write(SERVER_TERM, text .. "\n")
  end
  api.terminal_prompt(SERVER_TERM, "mirror> ")
end

local function out(api, s, verb, payload)
  payload = clean_line(payload or "")
  remember_screen(s, verb, payload)
  send(api, s.nick, verb, payload, s.network)
  update_mirror(api, s)
end

local function clear(api, s) out(api, s, "CLEAR", "") end
local function line(api, s, text) out(api, s, "LINE", text or "") end
local function prompt(api, s, text) out(api, s, "PROMPT", text or "bbs> ") end
local function status(api, s, text) out(api, s, "STATUS", text or "") end

local function screen(api, s, status_text, prompt_text, lines, page_id)
  lines = lines or {}
  s.last = { status = clean_line(status_text or ""), prompt = clean_line(prompt_text or ""), lines = {} }
  for _, value in ipairs(lines) do
    local text, fg, bg = row_parts(value)
    text = clean_frame_line(text)
    if fg == 7 and bg == 0 then
      s.last.lines[#s.last.lines + 1] = text
    else
      s.last.lines[#s.last.lines + 1] = { text = text, fg = fg, bg = bg }
    end
  end
  if s.sent_status ~= s.last.status then
    send(api, s.nick, "STATUS", s.last.status, s.network)
    s.sent_status = s.last.status
  end
  if page_id then
    send_static_or_terminal_frame(api, s, page_id, s.last.lines)
  else
    send_terminal_frames(api, s, s.last.lines)
  end
  if s.sent_prompt ~= s.last.prompt then
    send(api, s.nick, "PROMPT", s.last.prompt, s.network)
    s.sent_prompt = s.last.prompt
  end
  update_mirror(api, s)
end

-- ===== ASCII-art welcome screen (ANSI Shadow style block letters) =====
-- Block letters are 6 rows tall; each glyph is padded to a fixed width so rows
-- concatenate into a column-aligned banner.
local GLYPH = {
  R = {"██████╗ ", "██╔══██╗", "██████╔╝", "██╔══██╗", "██║  ██║", "╚═╝  ╚═╝"},
  E = {"███████╗", "██╔════╝", "█████╗  ", "██╔══╝  ", "███████╗", "╚══════╝"},
  T = {"████████╗", "╚══██╔══╝", "   ██║   ", "   ██║   ", "   ██║   ", "   ╚═╝   "},
  O = {" ██████╗ ", "██╔═══██╗", "██║   ██║", "██║   ██║", "╚██████╔╝", " ╚═════╝ "},
  B = {"██████╗ ", "██╔══██╗", "██████╔╝", "██╔══██╗", "██████╔╝", "╚═════╝ "},
  S = {"███████╗", "██╔════╝", "███████╗", "╚════██║", "███████║", "╚══════╝"},
  [" "] = {"  ", "  ", "  ", "  ", "  ", "  "},
}

-- Display width = codepoint count (every glyph here is a single monospace cell).
local function dwidth(s)
  local n = 0
  for _ in tostring(s or ""):gmatch("[^\128-\191]") do n = n + 1 end
  return n
end

local function spaces(n) return n > 0 and string.rep(" ", n) or "" end

local function fig(word)
  local rows = {"", "", "", "", "", ""}
  for i = 1, #word do
    local g = GLYPH[word:sub(i, i):upper()] or GLYPH[" "]
    for r = 1, 6 do rows[r] = rows[r] .. g[r] .. " " end
  end
  return rows
end

local BOX_W = 70                              -- total box width in cells
local BOX_IN = BOX_W - 2                       -- inner width
local BOX_MARGIN = math.floor((80 - BOX_W) / 2)

local function center_in(s, w)
  local pad = w - dwidth(s)
  if pad <= 0 then return s end
  return spaces(math.floor(pad / 2)) .. s
end

local function box_top() return spaces(BOX_MARGIN) .. "╔" .. string.rep("═", BOX_IN) .. "╗" end
local function box_bot() return spaces(BOX_MARGIN) .. "╚" .. string.rep("═", BOX_IN) .. "╝" end

local function box_row(inner)
  inner = inner or ""
  return spaces(BOX_MARGIN) .. "║" .. inner .. spaces(BOX_IN - dwidth(inner)) .. "║"
end

local function box_centered(inner) return box_row(center_in(inner or "", BOX_IN)) end

-- One color per row (VGA palette index); borders adopt the row color —
-- classic ANSI gradient style. RETRO fades cyan→white→cyan, BBS magenta.
local function colored(text, fg, bg)
  return { text = text, fg = fg, bg = bg or 0 }
end

local function welcome_lines(api, s)
  local RETRO_FADE = { 11, 11, 15, 15, 11, 11 }
  local BBS_FADE = { 13, 13, 15, 15, 13, 13 }
  local L = {}
  L[#L + 1] = ""
  L[#L + 1] = colored(box_top(), 13)
  L[#L + 1] = colored(box_row(""), 13)
  for i, r in ipairs(fig("RETRO")) do L[#L + 1] = colored(box_centered(r), RETRO_FADE[i]) end
  L[#L + 1] = colored(box_row(""), 13)
  for i, r in ipairs(fig("BBS")) do L[#L + 1] = colored(box_centered(r), BBS_FADE[i]) end
  L[#L + 1] = colored(box_row(""), 13)
  L[#L + 1] = colored(box_centered("M A X C H A T   ·   M C - D A T A   B O A R D"), 15)
  L[#L + 1] = colored(box_row(""), 13)
  L[#L + 1] = colored(box_row("  SYSOP . . . . " ..
                              (server.sysop ~= "" and server.sysop or "AVAILABLE")), 7)
  L[#L + 1] = colored(box_row("  LAST CALL . . " .. s.nick), 7)
  L[#L + 1] = colored(box_bot(), 13)
  L[#L + 1] = ""
  L[#L + 1] = "  Enter your handle to log in (demo user: " .. DEMO_USER .. ")."
  return L
end

local function box_div() return spaces(BOX_MARGIN) .. "╠" .. string.rep("═", BOX_IN) .. "╣" end

-- Truncate by display width (codepoint count), adding an ellipsis if cut.
local function trunc(s, w)
  if dwidth(s) <= w then return s end
  local out, n = {}, 0
  for c in tostring(s):gmatch("[^\128-\191][\128-\191]*") do
    if n >= w - 1 then break end
    out[#out + 1] = c
    n = n + 1
  end
  return table.concat(out) .. "…"
end

-- A standard framed page: centered title bar + divider + left-aligned body.
-- Cyan chrome, yellow title (override title_fg for alerts), grey body.
local function framed(title, body, title_fg)
  local L = {""}
  L[#L + 1] = colored(box_top(), 3)
  L[#L + 1] = colored(box_centered(title or ""), title_fg or 14)
  L[#L + 1] = colored(box_div(), 3)
  for _, t in ipairs(body or {}) do
    L[#L + 1] = box_row("  " .. trunc(t or "", BOX_IN - 3))
  end
  L[#L + 1] = colored(box_bot(), 3)
  return L
end

local function show_stats(api)
  if not server_running then return end
  local active = 0
  for _ in pairs(sessions) do active = active + 1 end
  api.terminal_clear(SERVER_TERM)
  api.terminal_status(SERVER_TERM, "RETRO-BBS SERVER MODE: STATS")
  api.terminal_write(SERVER_TERM, server.name .. " [" .. server.id .. "]\n")
  api.terminal_write(SERVER_TERM, "Sysop: " .. (server.sysop ~= "" and server.sysop or api.me()) .. "\n")
  api.terminal_write(SERVER_TERM, "Profile: " .. server.profile .. "\n")
  api.terminal_write(SERVER_TERM, "Connections: " .. server.connects .. "  Active: " .. active .. "  Pages: " .. server.pages .. "  Messages: " .. #board .. "\n")
  api.terminal_write(SERVER_TERM, "Cache: static sent=" .. server.static_sent .. "  replays=" .. server.cache_replays .. "  misses=" .. server.cache_misses .. "  fallback T=" .. server.fallback_frames .. "\n")
  api.terminal_write(SERVER_TERM, "\nActive sessions:\n")
  if active == 0 then
    api.terminal_write(SERVER_TERM, "  none\n")
  else
    for _, s in pairs(sessions) do
      api.terminal_write(SERVER_TERM, "  " .. s.nick .. "  mode=" .. s.mode .. "  size=" .. s.cols .. "x" .. s.rows .. "\n")
    end
  end
  api.terminal_write(SERVER_TERM, "\nConsole: stats | mirror <nick> | mirror off\n")
  api.terminal_write(SERVER_TERM, "         chat <nick> <text> | logoff <nick> | help\n")
  api.terminal_prompt(SERVER_TERM, "sysop> ")
end

local function login_screen(api, s)
  s.mode = "login"
  screen(api, s, "CONNECT 57600  " .. server.name, "login> ", welcome_lines(api, s), "login")
end

local function password_screen(api, s)
  s.mode = "password"
  screen(api, s, "CONNECT 57600  " .. server.name, "password> ", framed("LOGIN  ·  " .. server.name, {
    "Username accepted: " .. s.login_user,
    "",
    "Enter your password to continue.",
  }))
end

local function login_failed(api, s)
  s.login_user = nil
  s.mode = "login"
  screen(api, s, "LOGIN FAILED  " .. server.name, "login> ", framed("!!  ACCESS DENIED  !!", {
    "That login was not accepted.",
    "",
    "Enter your handle to try again.",
  }, 12), "login-failed")
end

local function menu(api, s)
  s.mode = "menu"
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "bbs> ", framed(server.name, {
    server.welcome,
    "",
    "[1]  About this BBS",
    "[2]  Message board",
    "[3]  Who is online",
    "[4]  Page the sysop",
    "[5]  Hangman door",
    "[6]  Pic gallery",
    "[7]  Log off",
  }), "main")
end

local function about(api, s)
  s.mode = "about"
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "about> ", framed("About " .. server.name, {
    server.name .. " is a small MC DATA demo.",
    "Traffic uses CTCP MC DATA on your current IRC network.",
    "No secrets, passwords, or private data should be sent here yet.",
    "",
    "Press B to return to the main menu.",
  }), "about")
end

local function show_board(api, s)
  s.mode = "board"
  local body = {}
  if #board == 0 then
    body[#body + 1] = "No messages yet. Be the first to post."
  else
    for i, msg in ipairs(board) do
      body[#body + 1] = string.format("%2d. %s: %s", i, msg.from, msg.text)
    end
  end
  body[#body + 1] = ""
  body[#body + 1] = "P <message> to post  ·  B to return"
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "board> ",
    framed("Message Board", body))
end

local function show_who(api, s)
  s.mode = "who"
  local body = {}
  local count = 0
  for _, other in pairs(sessions) do
    count = count + 1
    body[#body + 1] = string.format("%-20s (%s)", other.nick, other.mode)
  end
  if count == 0 then body[#body + 1] = "Nobody online. That should not happen, but here we are." end
  body[#body + 1] = ""
  body[#body + 1] = "Press B to return."
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "who> ",
    framed("Who Is Online (" .. count .. ")", body))
end

local function page_sysop(api, s)
  s.mode = "page"
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "page> ", framed("Page The Sysop", {
    "Type a short message for the sysop.",
    "",
    "Press B to return without paging.",
  }), "page-sysop")
end

local function pics_menu(api, s)
  s.mode = "pics"
  local body = {}
  if not s.supports_bitmap then
    body[#body + 1] = "Your client does not support bitmap art (caps B1)."
  else
    for i, pic in ipairs(PICS) do
      body[#body + 1] = string.format("[%d]  %-18s %s  %dx%d  1-bit", i, pic.title, pic.enc, pic.w, pic.h)
    end
  end
  body[#body + 1] = ""
  body[#body + 1] = "Pick a number  ·  B to return"
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "pics> ",
    framed("Pic Gallery  ·  1-bit photos over MC DATA", body), "pics")
end

local function show_pic(api, s, pic)
  if not s.supports_bitmap then pics_menu(api, s); return end
  s.mode = "picview"
  local data = table.concat(pic.data)
  local hash = frame_hash(data)
  s.sent_pics = s.sent_pics or {}
  local sent_key = pic.id .. "|" .. hash
  if not s.sent_pics[sent_key] then
    local total = #pic.data
    for i, chunk in ipairs(pic.data) do
      send_raw(api, s.nick, "B", pic.id .. " " .. pic.w .. " " .. pic.h .. " " .. hash ..
               " " .. pic.enc .. " " .. i .. "/" .. total .. " " .. chunk, s.network)
    end
    s.sent_pics[sent_key] = true
  end
  send_raw(api, s.nick, "I", pic.id .. " " .. hash .. " P0101", s.network)
  send(api, s.nick, "STATUS", "PIC: " .. pic.title .. "  (" .. pic.enc .. " " ..
       pic.w .. "x" .. pic.h .. ")  any key returns", s.network)
  send(api, s.nick, "PROMPT", "pic> ", s.network)
  -- force STATUS/PROMPT resend on the next screen() after the manual sends
  s.sent_status = nil
  s.sent_prompt = nil
end

local function hangman_word()
  local list = {"MODEM", "PACKET", "TERMINAL", "SYSOP", "RETRO", "ANSI", "COMMODORE", "HANGMAN", "SCRIPT", "MAXCHAT"}
  return list[math.random(#list)]
end

local function hangman_text(h)
  local shown = {}
  for i = 1, #h.word do
    local ch = h.word:sub(i, i)
    shown[#shown + 1] = h.guessed[ch] and ch or "_"
  end
  return table.concat(shown, " ")
end

local function render_hangman(api, s, note)
  s.mode = "hangman"
  local lines = {"Hangman Door", "------------"}
  if note and note ~= "" then lines[#lines + 1] = note end
  lines[#lines + 1] = "Word:  " .. hangman_text(s.hangman)
  lines[#lines + 1] = "Wrong: " .. table.concat(s.hangman.wrong, " ")
  lines[#lines + 1] = "Lives: " .. s.hangman.lives
  lines[#lines + 1] = "Guess a letter, NEW for a new word, or B to return."
  screen(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user, "hangman> ", lines)
end

local function new_hangman(api, s)
  s.hangman = { word = hangman_word(), guessed = {}, wrong = {}, lives = 6 }
  render_hangman(api, s, "")
end

local function hangman_done(h)
  for i = 1, #h.word do
    if not h.guessed[h.word:sub(i, i)] then return false end
  end
  return true
end

local function handle_hangman(api, s, input)
  local upper = trim(input):upper()
  if upper == "B" or upper == "BACK" then menu(api, s); return end
  if upper == "NEW" or not s.hangman then new_hangman(api, s); return end
  local ch = upper:match("^[A-Z]$")
  if not ch then render_hangman(api, s, "Type one letter."); return end
  if s.hangman.guessed[ch] then render_hangman(api, s, "Already guessed."); return end
  s.hangman.guessed[ch] = true
  if not s.hangman.word:find(ch, 1, true) then
    s.hangman.wrong[#s.hangman.wrong + 1] = ch
    s.hangman.lives = s.hangman.lives - 1
  end
  if hangman_done(s.hangman) then
    local word = s.hangman.word
    s.hangman = nil
    line(api, s, "You solved it: " .. word)
    new_hangman(api, s)
    return
  end
  if s.hangman.lives <= 0 then
    local word = s.hangman.word
    s.hangman = nil
    line(api, s, "Out of lives. Word was " .. word)
    new_hangman(api, s)
    return
  end
  render_hangman(api, s, "")
end

local function logoff(api, s)
  screen(api, s, "DISCONNECTED", "", {
    "Thanks for calling " .. server.name .. ".",
    "Carrier dropped."
  }, "logoff")
  sessions[s.key] = nil
  if server.mirror_key == s.key then
    server.mirror_key = nil
    show_stats(api)
  else
    show_stats(api)
  end
end

-- Console nick matching: exact nick first, substring only as a fallback, so
-- "logoff a" can't grab the first session that happens to contain an "a".
local function console_find(want)
  want = trim(want):lower()
  if want == "" then return nil end
  for _, s in pairs(sessions) do
    if s.nick:lower() == want then return s end
  end
  for _, s in pairs(sessions) do
    if s.nick:lower():find(want, 1, true) then return s end
  end
  return nil
end

local function handle_server_input(api, text)
  text = trim(text)
  local cmd, rest = text:match("^(%S+)%s*(.*)$")
  cmd = (cmd or ""):lower()
  if cmd == "" or cmd == "stats" then
    server.mirror_key = nil
    show_stats(api)
    return
  end
  if cmd == "help" then
    server_write(api, "stats | mirror <nick> | mirror off | chat <nick> <text> | logoff <nick>")
    return
  end
  if cmd == "mirror" then
    if trim(rest):lower() == "off" then
      server.mirror_key = nil
      show_stats(api)
      return
    end
    local s = console_find(rest)
    if s then
      server.mirror_key = s.key
      update_mirror(api, s)
      return
    end
    server_write(api, "No matching session for mirror.")
    return
  end
  if cmd == "chat" then
    local nick, msg = rest:match("^(%S+)%s+(.+)$")
    if not nick then server_write(api, "Usage: chat <nick> <text>"); return end
    local s = console_find(nick)
    if s then
      line(api, s, "SYSOP: " .. clean_line(msg))
      prompt(api, s, s.mode .. "> ")
      server_write(api, "Sent to " .. s.nick .. ".")
      return
    end
    server_write(api, "No matching session.")
    return
  end
  if cmd == "logoff" then
    if trim(rest) == "" then server_write(api, "Usage: logoff <nick>"); return end
    local s = console_find(rest)
    if s then
      server_write(api, "Logging off " .. s.nick .. ".")
      logoff(api, s)
      return
    end
    server_write(api, "No matching session.")
    return
  end
  server_write(api, "Unknown console command. Type help.")
end

local function handle_session_input(api, s, input)
  local text = trim(input)
  local low = text:lower()

  if s.mode == "login" then
    if text:lower() == DEMO_USER then
      s.login_user = DEMO_USER
      password_screen(api, s)
    else
      login_failed(api, s)
    end
    return
  end

  if s.mode == "password" then
    if text == DEMO_PASSWORD then
      s.user = s.login_user or DEMO_USER
      s.login_user = nil
      menu(api, s)
    else
      login_failed(api, s)
    end
    return
  end

  -- Any key (including b/q) leaves a picture and returns to the gallery.
  if s.mode == "picview" then
    pics_menu(api, s)
    return
  end

  if low == "b" or low == "back" then menu(api, s); return end
  if low == "q" or low == "quit" or low == "logoff" then logoff(api, s); return end

  if s.mode == "menu" then
    if text == "1" or low == "about" then about(api, s)
    elseif text == "2" or low == "board" then show_board(api, s)
    elseif text == "3" or low == "who" then show_who(api, s)
    elseif text == "4" or low == "page" then page_sysop(api, s)
    elseif text == "5" or low == "hangman" then new_hangman(api, s)
    elseif text == "6" or low == "pics" then pics_menu(api, s)
    elseif text == "7" then logoff(api, s)
    else line(api, s, "Choose 1-7."); prompt(api, s, "bbs> ") end
    return
  end

  if s.mode == "pics" then
    local pic = PICS[tonumber(text) or 0]
    if pic then show_pic(api, s, pic) else pics_menu(api, s) end
    return
  end

  if s.mode == "about" or s.mode == "who" then
    menu(api, s)
    return
  end

  if s.mode == "board" then
    local msg = text:match("^[Pp]%s+(.+)$")
    if msg then
      board[#board + 1] = { from = s.nick, text = clean_line(msg) }
      while #board > MAX_BOARD do table.remove(board, 1) end
      save_board(api)
      show_board(api, s)
    else
      line(api, s, "Use P <message> or B.")
      prompt(api, s, "board> ")
    end
    return
  end

  if s.mode == "page" then
    server.pages = server.pages + 1
    server_write(api, "PAGE from " .. s.nick .. ": " .. text)
    line(api, s, "The sysop has been paged.")
    line(api, s, "If they are watching, they can type: chat " .. s.nick .. " <text>")
    prompt(api, s, "page> ")
    return
  end

  if s.mode == "hangman" then
    handle_hangman(api, s, text)
    return
  end

  menu(api, s)
end

local function connect_client(api, nick, bbs_id, profile)
  bbs_id = bbs_id ~= "" and bbs_id or DEFAULT_BBS_ID
  profile = profile ~= "" and profile or DEFAULT_PROFILE
  local id = client_term(nick, bbs_id)
  api.terminal_open(id, "Retro-BBS - " .. nick, profile, profile == "c64" and 40 or 80, 25)
  api.terminal_status(id, "DIAL: " .. bbs_id .. "  CONNECTING: " .. nick .. "  User: guest")
  api.terminal_clear(id)
  api.terminal_write(id, "Dialing IRC nick " .. nick .. " / " .. bbs_id .. "...\n")
  api.terminal_write(id, "That nick must be connected here and running /bbsserve.\n")
  api.terminal_prompt(id, "")
  local cols, rows = api.terminal_size(id)
  local key = client_key(api.network(), nick, bbs_id)
  local client = { network = api.network(), nick = nick, bbs_id = bbs_id, term = id,
                   profile = profile, connected = false }
  clients[key] = client
  client.timer = api.timer(12000, function()
    local current = clients[key]
    if current and not current.connected then
      api.terminal_status(id, "NO ANSWER: " .. nick .. " / " .. bbs_id)
      api.terminal_write(id, "\nNo BBS answer received.\n")
      api.terminal_write(id, "Check the nick spelling and make sure the other client ran /bbsserve.\n")
      api.terminal_prompt(id, "")
    end
  end)
  local ok = api.mc_send(nick, SERVICE, "HELLO", "bbs_id=" .. bbs_id .. ";cols=" .. tostring(cols) .. ";rows=" .. tostring(rows) .. ";profile=" .. profile .. ";caps=" .. CLIENT_CAPS)
  if not ok then
    api.terminal_status(id, "SEND FAILED: " .. nick)
    api.terminal_write(id, "\nCould not send MC DATA HELLO. Are you connected to IRC?\n")
  end
end

local function book_labels(api)
  local raw = tostring(api.get("book:labels") or "")
  local labels = {}
  for label in raw:gmatch("[^,]+") do labels[#labels + 1] = label end
  return labels
end

local function save_book_labels(api, labels)
  api.set("book:labels", table.concat(labels, ","))
end

local function book_get(api, label)
  local value = api.get("book:" .. label)
  if not value then return nil end
  local nick, bbs_id, profile = tostring(value):match("^([^|]+)|([^|]+)|([^|]+)$")
  if not nick then return nil end
  return { nick = nick, bbs_id = bbs_id, profile = profile }
end

local function book_set(api, label, nick, bbs_id, profile)
  local labels = book_labels(api)
  local exists = false
  for _, item in ipairs(labels) do if item == label then exists = true end end
  if not exists then labels[#labels + 1] = label end
  save_book_labels(api, labels)
  api.set("book:" .. label, nick .. "|" .. (bbs_id or DEFAULT_BBS_ID) .. "|" .. (profile or DEFAULT_PROFILE))
end

local function book_remove(api, label)
  local labels = book_labels(api)
  local kept = {}
  for _, item in ipairs(labels) do if item ~= label then kept[#kept + 1] = item end end
  save_book_labels(api, kept)
  api.set("book:" .. label, "")
end

function on_load(api)
  math.randomseed(os.time and os.time() or 1)
  load_config(api)
  load_board(api)
  api.echo("[bbs] loaded - /bbsserve starts Retro-BBS, /bbs <nick> dials.")
end

function on_command(api, command, args)
  command = tostring(command or ""):lower()
  args = trim(args)

  if command == "bbsserve" then
    load_config(api)
    if args ~= "" then server.name = clean_line(args); save_config(api) end
    server_running = true
    server.id = DEFAULT_BBS_ID
    api.terminal_open(SERVER_TERM, "Retro-BBS Server", "free", 80, 25)
    show_stats(api)
    return true
  end

  if command == "bbsconfig" then
    load_config(api)
    local field, value = args:match("^(%S+)%s*(.*)$")
    field = (field or ""):lower()
    value = clean_line(value or "")
    if field == "name" and value ~= "" then server.name = value; save_config(api)
    elseif field == "sysop" and value ~= "" then server.sysop = value; save_config(api)
    elseif field == "welcome" and value ~= "" then server.welcome = value; save_config(api)
    elseif field == "profile" and value ~= "" then server.profile = value; save_config(api)
    elseif field ~= "" then api.echo("[bbs] usage: /bbsconfig name|sysop|welcome|profile <value>"); return true end
    api.echo("[bbs] name=" .. server.name .. " id=" .. server.id .. " sysop=" .. server.sysop .. " profile=" .. server.profile)
    api.echo("[bbs] welcome=" .. server.welcome)
    return true
  end

  if command == "bbs" then
    local parts = words(args)
    if not parts[1] then
      api.echo("[bbs] usage: /bbs <nick> [bbs_id]")
      return true
    end
    connect_client(api, parts[1], parts[2] or DEFAULT_BBS_ID, DEFAULT_PROFILE)
    return true
  end

  if command == "bbscache" then
    local sub = (words(args)[1] or "stats"):lower()
    if sub == "clear" then
      static_cache = {}
      api.echo("[bbscache] cleared local static frame cache")
    else
      api.echo("[bbscache] local static frames=" .. tostring(static_cache_count()))
      api.echo("[bbscache] server static sent=" .. server.static_sent .. " replays=" .. server.cache_replays .. " misses=" .. server.cache_misses .. " fallback T=" .. server.fallback_frames)
    end
    return true
  end

  if command == "bbsbook" then
    local parts = words(args)
    local sub = (parts[1] or "list"):lower()
    if sub == "list" then
      local labels = book_labels(api)
      if #labels == 0 then api.echo("[bbsbook] empty") end
      for _, label in ipairs(labels) do
        local item = book_get(api, label)
        if item then api.echo("[bbsbook] " .. label .. " -> " .. item.nick .. " " .. item.bbs_id .. " " .. item.profile) end
      end
      return true
    end
    if sub == "add" then
      if not parts[2] or not parts[3] then
        api.echo("[bbsbook] usage: /bbsbook add <label> <nick> [bbs_id] [profile]")
        return true
      end
      book_set(api, parts[2], parts[3], parts[4] or DEFAULT_BBS_ID, parts[5] or DEFAULT_PROFILE)
      api.echo("[bbsbook] saved " .. parts[2])
      return true
    end
    if sub == "dial" then
      local label = parts[2] or "Retro-BBS"
      local item = book_get(api, label)
      if not item then api.echo("[bbsbook] not found: " .. label); return true end
      connect_client(api, item.nick, item.bbs_id, item.profile)
      return true
    end
    if sub == "remove" then
      if not parts[2] then api.echo("[bbsbook] usage: /bbsbook remove <label>"); return true end
      book_remove(api, parts[2])
      api.echo("[bbsbook] removed " .. parts[2])
      return true
    end
    api.echo("[bbsbook] list | add | dial | remove")
    return true
  end

  return false
end

-- Find this nick's session on THIS network (and board, when bbs_id is known).
-- Nick-only matching would cross-route input between networks/boards.
local function find_session(network, nick, bbs_id)
  if bbs_id and bbs_id ~= "" then
    return sessions[session_key(network, nick, bbs_id)]
  end
  for _, s in pairs(sessions) do
    if s.network == network and s.nick == nick then return s end
  end
  return nil
end

local function session_count()
  local n = 0
  for _ in pairs(sessions) do n = n + 1 end
  return n
end

function on_mc_data(api, network, target, nick, service, verb, payload, notice)
  if tostring(service or ""):lower() ~= SERVICE then return false end
  -- Spec: never auto-reply to NOTICE. Everything below can send, so drop
  -- NOTICE-borne bbs traffic entirely (log-only would also be acceptable).
  if notice then return true end
  verb = tostring(verb or ""):upper()
  payload = tostring(payload or "")

  if verb == "HELLO" then
    -- Per-peer cooldown: every HELLO costs ~10 outgoing frames (login screen),
    -- so an unthrottled peer could make us flood our own send queue.
    local cd_key = tostring(network) .. "|" .. tostring(nick):lower()
    if hello_cooldown[cd_key] then return true end
    hello_cooldown[cd_key] = true
    api.timer(HELLO_COOLDOWN_MS, function() hello_cooldown[cd_key] = nil end)

    load_config(api)
    local kv = parse_kv(payload)
    local bbs_id = kv.bbs_id or DEFAULT_BBS_ID
    local key = session_key(network, nick, bbs_id)
    local s = {
      key = key,
      network = network,
      nick = nick,
      user = nick,
      bbs_id = bbs_id,
      cols = tonumber(kv.cols) or 80,
      rows = tonumber(kv.rows) or 25,
      profile = kv.profile or DEFAULT_PROFILE,
      supports_static = has_cap(kv.caps, "S"),
      supports_bitmap = has_cap(kv.caps, "B1"),
      mode = "menu",
      last = { lines = {} }
    }
    if not server_running then
      -- Reply without storing: no ghost sessions while the board is offline.
      screen(api, s, "OFFLINE", "", {
        "Retro-BBS is not running here.",
        "Ask the other user to run /bbsserve."
      })
      return true
    end
    if not sessions[key] and session_count() >= MAX_SESSIONS then
      send(api, nick, "LINE", "Board is full. Try again later.")
      return true
    end
    sessions[key] = s
    server.connects = server.connects + 1
    send(api, nick, "WELCOME", "caps=" .. CLIENT_CAPS, network)
    server_write(api, "CONNECT " .. nick .. " " .. s.cols .. "x" .. s.rows .. " " .. s.profile)
    login_screen(api, s)
    show_stats(api)
    return true
  end

  if verb == "INPUT" then
    -- New format: "<bbs_id> <text>". Old/bare payloads still route by nick.
    local first, rest = payload:match("^(%S+)%s?(.*)$")
    local found = first and sessions[session_key(network, nick, first)] or nil
    if found then
      handle_session_input(api, found, rest or "")
    else
      found = find_session(network, nick)
      if not found then return true end
      handle_session_input(api, found, payload)
    end
    return true
  end

  if verb == "Q" then
    local page_id, hash = payload:match("^(%S+)%s+(%S+)$")
    if page_id and hash then
      server.cache_misses = server.cache_misses + 1
      local s = find_session(network, nick)
      if s then
        local sent_key = page_id .. "|" .. hash
        local ops = s.static_defs and s.static_defs[sent_key]
        if ops then
          send_raw(api, s.nick, "S", page_id .. " " .. hash .. " " .. ops, s.network)
        elseif s.last and s.last.lines then
          send_terminal_frames(api, s, s.last.lines)
        end
      end
    end
    return true
  end

  if verb == "LOGOFF" then
    -- Payload carries the bbs_id; only drop that board's session on this
    -- network (a bare LOGOFF still only affects this network's sessions).
    for key, s in pairs(sessions) do
      if s.network == network and s.nick == nick and
         (payload == "" or s.bbs_id == payload) then
        sessions[key] = nil
      end
    end
    show_stats(api)
    return true
  end

  local client = nil
  for _, item in pairs(clients) do
    if item.nick == nick and (item.network == nil or item.network == network) then
      client = item
      break
    end
  end
  if not client then return true end
  if not client.connected then
    client.connected = true
    if client.timer then
      api.cancel_timer(client.timer)
      client.timer = nil
    end
  end
  if verb == "B" then
    local id, w, h, hash, enc, idx, total, data =
      payload:match("^(%S+) (%d+) (%d+) (%S+) (%S+) (%d+)/(%d+) (%S+)$")
    if id then
      client.bm_parts = client.bm_parts or {}
      local pkey = id .. "|" .. hash
      local entry = client.bm_parts[pkey] or { parts = {}, total = tonumber(total) }
      entry.parts[tonumber(idx)] = data
      entry.w, entry.h, entry.enc = tonumber(w), tonumber(h), enc
      client.bm_parts[pkey] = entry
      local have = 0
      for _ in pairs(entry.parts) do have = have + 1 end
      if have >= entry.total then
        local full = {}
        for i = 1, entry.total do full[i] = entry.parts[i] or "" end
        bitmap_cache[bitmap_key(client.bbs_id, id, hash)] =
          { w = entry.w, h = entry.h, enc = entry.enc, data = table.concat(full) }
        client.bm_parts[pkey] = nil
      end
    end
    return true
  end
  if verb == "I" then
    local id, hash = payload:match("^(%S+) (%S+)")
    local bm = id and bitmap_cache[bitmap_key(client.bbs_id, id, hash)]
    if bm then
      if not render_bitmap(api, client.term, bm) then
        api.terminal_write(client.term, "[bbs] bad bitmap data\n")
      end
    else
      api.terminal_write(client.term, "[bbs] missing pic data\n")
    end
    return true
  end
  if verb == "WELCOME" then
    client.server_caps = parse_kv(payload).caps or ""
    return true
  end
  if verb == "BYE" then
    client.connected = false
    api.terminal_status(client.term, "DISCONNECTED: " .. (payload ~= "" and payload or client.bbs_id))
    api.terminal_write(client.term, "\nThe BBS shut down. Carrier dropped.\n")
    api.terminal_prompt(client.term, "")
    return true
  end
  if verb == "FRAME" then
    local parts = frame_parts(payload)
    api.terminal_clear(client.term)
    api.terminal_status(client.term, parts[1] or "")
    for i = 3, #parts do
      api.terminal_write(client.term, (parts[i] or "") .. "\n")
    end
    api.terminal_prompt(client.term, parts[2] or "")
  elseif verb == "T" then
    if not api.terminal_frame(client.term, payload) then
      api.terminal_write(client.term, "[bbs] bad terminal frame\n")
    end
  elseif verb == "S" then
    local page_id, hash, ops = payload:match("^(%S+)%s+(%S+)%s+(.+)$")
    if page_id and hash and ops then
      if static_cache_count() >= 128 then static_cache = {} end -- crude cap
      static_cache[static_cache_key(client.bbs_id, page_id, hash)] = ops
      if not api.terminal_frame(client.term, ops) then
        api.terminal_write(client.term, "[bbs] bad static frame\n")
      end
    else
      api.terminal_write(client.term, "[bbs] bad static frame header\n")
    end
  elseif verb == "R" then
    local page_id, hash = payload:match("^(%S+)%s+(%S+)$")
    local ops = page_id and hash and static_cache[static_cache_key(client.bbs_id, page_id, hash)]
    if ops then
      if not api.terminal_frame(client.term, ops) then
        api.terminal_write(client.term, "[bbs] cached frame rejected\n")
      end
    elseif page_id and hash then
      server.cache_misses = server.cache_misses + 1
      send_raw(api, client.nick, "Q", page_id .. " " .. hash, client.network)
    else
      api.terminal_write(client.term, "[bbs] bad cache replay header\n")
    end
  elseif verb == "D" then
    if not api.terminal_frame(client.term, payload) then
      api.terminal_write(client.term, "[bbs] bad dynamic frame\n")
    end
  elseif verb == "LINES" then
    for _, text in ipairs(frame_parts(payload)) do
      api.terminal_write(client.term, text .. "\n")
    end
  elseif verb == "CLEAR" then api.terminal_clear(client.term)
  elseif verb == "LINE" then api.terminal_write(client.term, payload .. "\n")
  elseif verb == "STATUS" then api.terminal_status(client.term, payload)
  elseif verb == "PROMPT" then api.terminal_prompt(client.term, payload)
  else api.terminal_write(client.term, "[bbs] " .. verb .. " " .. payload .. "\n") end
  return true
end

function on_terminal_input(api, id, text)
  if id == SERVER_TERM then
    handle_server_input(api, text)
    return
  end
  if starts_with(id, "client:") then
    for _, client in pairs(clients) do
      if client.term == id then
        send_input(api, client, text)
        return
      end
    end
    api.terminal_write(id, "No active BBS connection for this terminal.\n")
  end
end

function on_terminal_closed(api, id)
  if id == SERVER_TERM then
    for _, s in pairs(sessions) do
      send(api, s.nick, "BYE", s.bbs_id, s.network)
    end
    server_running = false
    sessions = {}
    server.mirror_key = nil
    return
  end
  for key, client in pairs(clients) do
    if client.term == id then
      api.mc_send(client.nick, SERVICE, "LOGOFF", client.bbs_id, client.network or "")
      clients[key] = nil
      return
    end
  end
end

function on_terminal_link(api, id, action_id)
  -- Hotspot clicks act like typed input on dial-in terminals.
  if starts_with(id, "client:") then
    for _, client in pairs(clients) do
      if client.term == id then
        send_input(api, client, action_id)
        return
      end
    end
  end
end
