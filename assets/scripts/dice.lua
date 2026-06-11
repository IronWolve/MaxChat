-- Dice + magic 8-ball — an example MaxChat script using the /command hook.
--
-- Adds two commands (sent to the current channel/query):
--     /roll [NdM]   roll N dice of M sides (default 1d6)
--     /8ball <q>    ask the magic 8-ball
--
-- This file is seeded into <config>/scripts/ on first run. Edit it and run
-- /reload dice to tinker — that's the whole point of the scripting system.
-- math.random is pre-seeded by the host. See SCRIPTING.md.

local EIGHTBALL = {
  "It is certain.", "Without a doubt.", "Yes, definitely.", "You may rely on it.",
  "Most likely.", "Outlook good.", "Signs point to yes.", "Reply hazy, try again.",
  "Ask again later.", "Cannot predict now.", "Don't count on it.", "My reply is no.",
  "Outlook not so good.", "Very doubtful.",
}

-- Say it to the current channel/query; fall back to a local echo on the server tab.
local function send(api, text)
  local target = api.target()
  if target ~= "" and target ~= "(server)" then
    api.say(target, text)
  else
    api.echo(text)
  end
end

function on_command(api, command, args)
  local cmd = command:lower()

  if cmd == "roll" then
    local spec = (args:gsub("%s+", "")):lower()
    if spec == "" then spec = "1d6" end
    local n, sides
    local nn, ss = spec:match("^(%d*)d(%d+)$")
    if ss then
      n, sides = tonumber(nn) or 1, tonumber(ss)
    elseif spec:match("^%d+$") then
      n, sides = 1, tonumber(spec)
    else
      n, sides = 0, 0
    end
    if not (n >= 1 and n <= 20 and sides >= 2 and sides <= 1000) then
      api.echo("Usage: /roll [NdM]  — e.g. /roll 2d6")
      return true
    end
    local total, parts = 0, {}
    for _ = 1, n do
      local r = math.random(1, sides)
      total = total + r
      parts[#parts + 1] = tostring(r)
    end
    local detail = (n > 1) and (" (" .. table.concat(parts, ", ") .. ")") or ""
    send(api, "\u{1F3B2} " .. api.me() .. " rolls " .. n .. "d" .. sides .. ": " .. total .. detail)
    return true
  end

  if cmd == "8ball" or cmd == "eightball" then
    local q = args:gsub("^%s+", ""):gsub("%s+$", "")
    if q == "" then
      api.echo("Usage: /8ball <question>")
      return true
    end
    send(api, "\u{1F3B1} " .. q .. " — " .. EIGHTBALL[math.random(1, #EIGHTBALL)])
    return true
  end

  return false
end
