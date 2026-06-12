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

local server_running = false
local server = {
  id = DEFAULT_BBS_ID,
  name = DEFAULT_BBS_NAME,
  sysop = "",
  welcome = "Welcome to Retro-BBS.",
  profile = DEFAULT_PROFILE,
  pages = 0,
  connects = 0,
  mirror_key = nil
}
local sessions = {}
local clients = {}
local board = {
  { from = "sysop", text = "Welcome. Leave a short note with P <message>." }
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

local function clean_line(s)
  s = trim(s):gsub("[\r\n\t]", " ")
  if #s > MAX_LINE then s = s:sub(1, MAX_LINE - 3) .. "..." end
  return s
end

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

local function session_key(network, nick, bbs_id)
  return tostring(network or "") .. "|" .. tostring(nick or ""):lower() .. "|" .. tostring(bbs_id or DEFAULT_BBS_ID):lower()
end

local function client_key(network, nick, bbs_id)
  return session_key(network, nick, bbs_id)
end

local function client_term(nick, bbs_id)
  return "client:" .. tostring(nick or "unknown"):gsub("[^%w%-_]", "_") .. ":" .. tostring(bbs_id or DEFAULT_BBS_ID):gsub("[^%w%-_]", "_")
end

local function send(api, nick, verb, payload)
  api.mc_reply(nick, SERVICE, verb, clean_line(payload or ""))
end

local function send_input(api, nick, text)
  api.mc_send(nick, SERVICE, "INPUT", clean_line(text or ""))
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
  for _, line in ipairs((s.last and s.last.lines) or {}) do
    api.terminal_write(SERVER_TERM, line .. "\n")
  end
  api.terminal_prompt(SERVER_TERM, "mirror> ")
end

local function out(api, s, verb, payload)
  payload = clean_line(payload or "")
  remember_screen(s, verb, payload)
  send(api, s.nick, verb, payload)
  update_mirror(api, s)
end

local function clear(api, s) out(api, s, "CLEAR", "") end
local function line(api, s, text) out(api, s, "LINE", text or "") end
local function prompt(api, s, text) out(api, s, "PROMPT", text or "bbs> ") end
local function status(api, s, text) out(api, s, "STATUS", text or "") end

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
  api.terminal_write(SERVER_TERM, "\nActive sessions:\n")
  if active == 0 then
    api.terminal_write(SERVER_TERM, "  none\n")
  else
    for _, s in pairs(sessions) do
      api.terminal_write(SERVER_TERM, "  " .. s.nick .. "  mode=" .. s.mode .. "  size=" .. s.cols .. "x" .. s.rows .. "\n")
    end
  end
  api.terminal_write(SERVER_TERM, "\nConsole: stats | mirror <nick> | mirror off | chat <nick> <text> | help\n")
  api.terminal_prompt(SERVER_TERM, "sysop> ")
end

local function menu(api, s)
  s.mode = "menu"
  clear(api, s)
  status(api, s, "CONNECT: " .. server.name .. "  User: " .. s.user)
  line(api, s, "========================================")
  line(api, s, " " .. server.name)
  line(api, s, " " .. server.welcome)
  line(api, s, "========================================")
  line(api, s, " 1  About this BBS")
  line(api, s, " 2  Message board")
  line(api, s, " 3  Who is online")
  line(api, s, " 4  Page the sysop")
  line(api, s, " 5  Hangman door")
  line(api, s, " 6  Log off")
  prompt(api, s, "bbs> ")
end

local function about(api, s)
  s.mode = "about"
  clear(api, s)
  line(api, s, server.name .. " is a small MC DATA demo.")
  line(api, s, "Traffic uses CTCP MC DATA on the IRC network you are already connected to.")
  line(api, s, "No secrets, passwords, or private data should be sent here yet.")
  line(api, s, "Type B to return.")
  prompt(api, s, "about> ")
end

local function show_board(api, s)
  s.mode = "board"
  clear(api, s)
  line(api, s, "Message Board")
  line(api, s, "-------------")
  for i, msg in ipairs(board) do
    line(api, s, i .. ". " .. msg.from .. ": " .. msg.text)
  end
  line(api, s, "")
  line(api, s, "P <message> posts. B returns to menu.")
  prompt(api, s, "board> ")
end

local function show_who(api, s)
  s.mode = "who"
  clear(api, s)
  line(api, s, "Who is online")
  line(api, s, "-------------")
  local count = 0
  for _, other in pairs(sessions) do
    count = count + 1
    line(api, s, other.nick .. "  (" .. other.mode .. ")")
  end
  if count == 0 then line(api, s, "Nobody. That should not happen, but here we are.") end
  line(api, s, "")
  line(api, s, "Type B to return.")
  prompt(api, s, "who> ")
end

local function page_sysop(api, s)
  s.mode = "page"
  clear(api, s)
  line(api, s, "Page Sysop")
  line(api, s, "----------")
  line(api, s, "Type a short message for the sysop, or B to return.")
  prompt(api, s, "page> ")
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
  clear(api, s)
  line(api, s, "Hangman Door")
  line(api, s, "------------")
  if note and note ~= "" then line(api, s, note) end
  line(api, s, "Word:  " .. hangman_text(s.hangman))
  line(api, s, "Wrong: " .. table.concat(s.hangman.wrong, " "))
  line(api, s, "Lives: " .. s.hangman.lives)
  line(api, s, "Guess a letter, NEW for a new word, or B to return.")
  prompt(api, s, "hangman> ")
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
  clear(api, s)
  line(api, s, "Thanks for calling " .. server.name .. ".")
  line(api, s, "Carrier dropped.")
  status(api, s, "DISCONNECTED")
  prompt(api, s, "")
  sessions[s.key] = nil
  if server.mirror_key == s.key then
    server.mirror_key = nil
    show_stats(api)
  else
    show_stats(api)
  end
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
    server_write(api, "stats | mirror <nick> | mirror off | chat <nick> <text>")
    return
  end
  if cmd == "mirror" then
    if trim(rest):lower() == "off" then
      server.mirror_key = nil
      show_stats(api)
      return
    end
    local want = trim(rest):lower()
    for _, s in pairs(sessions) do
      if s.nick:lower():find(want, 1, true) then
        server.mirror_key = s.key
        update_mirror(api, s)
        return
      end
    end
    server_write(api, "No matching session for mirror.")
    return
  end
  if cmd == "chat" then
    local nick, msg = rest:match("^(%S+)%s+(.+)$")
    if not nick then server_write(api, "Usage: chat <nick> <text>"); return end
    for _, s in pairs(sessions) do
      if s.nick:lower():find(nick:lower(), 1, true) then
        line(api, s, "SYSOP: " .. clean_line(msg))
        prompt(api, s, s.mode .. "> ")
        server_write(api, "Sent to " .. s.nick .. ".")
        return
      end
    end
    server_write(api, "No matching session.")
    return
  end
  server_write(api, "Unknown console command. Type help.")
end

local function handle_session_input(api, s, input)
  local text = trim(input)
  local low = text:lower()
  if low == "b" or low == "back" then menu(api, s); return end
  if low == "q" or low == "quit" or low == "logoff" then logoff(api, s); return end

  if s.mode == "menu" then
    if text == "1" or low == "about" then about(api, s)
    elseif text == "2" or low == "board" then show_board(api, s)
    elseif text == "3" or low == "who" then show_who(api, s)
    elseif text == "4" or low == "page" then page_sysop(api, s)
    elseif text == "5" or low == "hangman" then new_hangman(api, s)
    elseif text == "6" then logoff(api, s)
    else line(api, s, "Choose 1-6."); prompt(api, s, "bbs> ") end
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
  api.terminal_write(id, "Dialing " .. nick .. " / " .. bbs_id .. "...\n")
  api.terminal_prompt(id, "")
  local cols, rows = api.terminal_size(id)
  clients[client_key(api.network(), nick, bbs_id)] = { nick = nick, bbs_id = bbs_id, term = id, profile = profile }
  api.mc_send(nick, SERVICE, "HELLO", "bbs_id=" .. bbs_id .. ";cols=" .. tostring(cols) .. ";rows=" .. tostring(rows) .. ";profile=" .. profile)
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
    api.terminal_open(SERVER_TERM, "Retro-BBS Server", "free", 100, 30)
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

function on_mc_data(api, network, target, nick, service, verb, payload, notice)
  if tostring(service or ""):lower() ~= SERVICE then return false end
  verb = tostring(verb or ""):upper()
  payload = tostring(payload or "")

  if verb == "HELLO" then
    load_config(api)
    local kv = parse_kv(payload)
    local bbs_id = kv.bbs_id or DEFAULT_BBS_ID
    local key = session_key(network, nick, bbs_id)
    local s = {
      key = key,
      nick = nick,
      user = nick,
      bbs_id = bbs_id,
      cols = tonumber(kv.cols) or 80,
      rows = tonumber(kv.rows) or 25,
      profile = kv.profile or DEFAULT_PROFILE,
      mode = "menu",
      last = { lines = {} }
    }
    sessions[key] = s
    if not server_running then
      status(api, s, "OFFLINE")
      clear(api, s)
      line(api, s, "Retro-BBS is not running here.")
      line(api, s, "Ask the other user to run /bbsserve.")
      prompt(api, s, "")
      return true
    end
    server.connects = server.connects + 1
    server_write(api, "CONNECT " .. nick .. " " .. s.cols .. "x" .. s.rows .. " " .. s.profile)
    menu(api, s)
    show_stats(api)
    return true
  end

  if verb == "INPUT" then
    local found = nil
    for _, s in pairs(sessions) do
      if s.nick == nick then found = s; break end
    end
    if not found then return true end
    handle_session_input(api, found, payload)
    return true
  end

  if verb == "LOGOFF" then
    for key, s in pairs(sessions) do
      if s.nick == nick then sessions[key] = nil end
    end
    show_stats(api)
    return true
  end

  local client = nil
  for _, item in pairs(clients) do
    if item.nick == nick then client = item; break end
  end
  if not client then return true end
  if verb == "CLEAR" then api.terminal_clear(client.term)
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
        send_input(api, client.nick, text)
        return
      end
    end
    api.terminal_write(id, "No active BBS connection for this terminal.\n")
  end
end

function on_terminal_closed(api, id)
  if id == SERVER_TERM then
    server_running = false
    sessions = {}
    server.mirror_key = nil
    return
  end
  for key, client in pairs(clients) do
    if client.term == id then
      api.mc_send(client.nick, SERVICE, "LOGOFF", client.bbs_id)
      clients[key] = nil
      return
    end
  end
end

function on_terminal_link(api, id, action_id)
  api.terminal_write(id, "Links are reserved for a future BBS menu build. Type the menu choice for now.\n")
end
