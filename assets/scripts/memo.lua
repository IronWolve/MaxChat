-- memo.lua — leave offline messages for other users on the same network.
--
-- REQUIRES: Read files + Write files permissions
--           Preferences → Scripts → select memo → enable Read files + Write files
--
-- Usage:
--   !memo <nick> <message>   leave a memo for nick
--   !memo                    check your own pending memos
--
-- Memos are stored in this script's data folder, one file per network.
-- They are delivered the next time the recipient speaks in any channel.

local function memo_file(api)
    return api.data_dir() .. "/" .. (api.network():gsub("[^%w%-_]", "_")) .. "_memos.txt"
end

local function load_memos(path)
    local t = {}
    local f = io.open(path, "r")
    if not f then return t end
    for line in f:lines() do
        local nick, from, ts, msg = line:match("^([^\t]+)\t([^\t]+)\t([^\t]+)\t(.+)$")
        if nick then
            t[#t + 1] = {nick = nick:lower(), from = from, ts = ts, msg = msg}
        end
    end
    f:close()
    return t
end

local function save_memos(path, memos)
    local f = io.open(path, "w")
    if not f then return end
    for _, m in ipairs(memos) do
        f:write(m.nick .. "\t" .. m.from .. "\t" .. m.ts .. "\t" .. m.msg .. "\n")
    end
    f:close()
end

local function timestamp()
    return os.date and os.date("%Y-%m-%d %H:%M") or "?"
end

function on_command(api, command, args)
    if command:lower() ~= "memo" then return false end

    if not io or not io.open then
        api.echo("[memo] needs Read files + Write files permissions — Preferences → Scripts → memo")
        return true
    end

    local path = memo_file(api)
    local memos = load_memos(path)

    local target, msg = args:match("^%s*(%S+)%s+(.+)%s*$")
    if not target then
        -- List caller's pending memos.
        local me = api.me():lower()
        local mine = {}
        for _, m in ipairs(memos) do
            if m.nick == me then mine[#mine + 1] = m end
        end
        if #mine == 0 then
            api.echo("No pending memos.")
        else
            for _, m in ipairs(mine) do
                api.echo("[memo] from " .. m.from .. " @ " .. m.ts .. ": " .. m.msg)
            end
        end
        return true
    end

    -- Store the new memo.
    memos[#memos + 1] = {
        nick = target:lower(),
        from = api.me(),
        ts   = timestamp(),
        msg  = msg,
    }
    save_memos(path, memos)
    api.echo("Memo saved for " .. target .. ".")
    return true
end

function on_message(api, network, target, nick, text)
    local path = memo_file(api)
    local memos = load_memos(path)
    local key = nick:lower()
    local pending, rest = {}, {}
    for _, m in ipairs(memos) do
        if m.nick == key then
            pending[#pending + 1] = m
        else
            rest[#rest + 1] = m
        end
    end
    if #pending == 0 then return false end
    save_memos(path, rest)
    for _, m in ipairs(pending) do
        api.say(target, nick .. ": memo from " .. m.from .. " @ " .. m.ts .. ": " .. m.msg)
    end
    return false  -- don't consume; other scripts still see the message
end
