-- run.lua — launch an application from chat.
--
-- REQUIRES: Run programs permission
--           Preferences → Scripts → select run → enable Run programs
--
-- Usage:
--   !run explorer           open Windows Explorer
--   !run notepad            open Notepad
--   !run calc               open Calculator
--   !run xdg-open .         open current folder (Linux)
--
-- The program is launched detached so it does not block the client.

local is_windows = package and package.config:sub(1, 1) == "\\"

local function launch(prog)
    if is_windows then
        os.execute('start "" ' .. prog)
    else
        os.execute(prog .. " &")
    end
end

function on_command(api, command, args)
    if command:lower() ~= "run" then return false end

    if not os or not os.execute then
        api.echo("[run] needs 'Run programs' permission — Preferences → Scripts → run")
        return true
    end

    local prog = args:match("^%s*(.-)%s*$")
    if prog == "" then
        api.echo("Usage: !run <program>  e.g. !run explorer  !run notepad  !run calc")
        return true
    end

    local ok, err = pcall(launch, prog)
    if ok then
        api.echo("Launched: " .. prog)
    else
        api.echo("run: failed – " .. tostring(err))
    end
    return true
end
