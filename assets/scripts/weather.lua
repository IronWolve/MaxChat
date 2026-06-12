-- weather.lua — fetch current conditions for any city, state, or ZIP code.
--
-- REQUIRES: Network access permission
--           Preferences → Scripts → Network access ✓
--
-- Usage:
--   !weather seattle
--   !weather 98022
--   !weather seattle, wa
--   !weather london, uk
--
-- Works both as a personal lookup (!weather echoes to you) and as a channel
-- command (anyone typing !weather in a channel gets a reply in that channel).
--
-- Data source: wttr.in (no API key needed).
-- Units: "u" = US customary (°F, mph) | "m" = metric (°C, km/h)

local UNITS = "u"

local BASE   = "https://wttr.in/"
local FORMAT = "?format=%l:+%C,+%t+(feels+%f),+Humidity+%h,+Wind+%w&"

-- percent-encode everything except unreserved chars; spaces → +
local function urlencode(s)
    return (s:gsub("([^%w%-%.%_%~])", function(c)
        if c == " " then return "+" end
        return string.format("%%%02X", string.byte(c))
    end))
end

-- send to current channel, or echo locally when on a server/status tab
local function send(api, text)
    local target = api.target()
    if target ~= "" and target ~= "(server)" then
        api.say(target, text)
    else
        api.echo(text)
    end
end

local function fetch_weather(api, location)
    local url = BASE .. urlencode(location) .. FORMAT .. UNITS
    local body = api.http_get(url)
    if not body or body:match("^%s*$") then
        return nil, "no response from wttr.in"
    end
    local line = body:match("^([^\r\n]+)")
    if not line then
        return nil, "empty response"
    end
    if line:lower():find("unknown location", 1, true) then
        return nil, "unknown location"
    end
    return line:gsub("%s+$", "")
end

function on_command(api, command, args)
    if command:lower() ~= "weather" then
        return false
    end

    local location = args:match("^%s*(.-)%s*$")
    if not location or location == "" then
        api.echo("Usage: !weather <city, state or ZIP>  —  e.g. !weather Seattle, WA")
        return true
    end

    local result, err = fetch_weather(api, location)
    if result then
        send(api, result)
    else
        api.echo("Weather: could not get conditions for \"" .. location .. "\" (" .. (err or "error") .. ")")
    end
    return true
end
