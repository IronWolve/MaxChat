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
-- Output is captured and echoed (first 300 characters) if the program
-- exits immediately; silent otherwise.

local function launch(prog)
    -- Detach: on Windows use 'start', on Unix append '&'.
    if package.config:sub(1, 1) == "\\" then
        os.execute('start "" ' .. prog)
        return nil
    else
        local handle = io.popen(prog .. " 2>&1 &", "r")
        if handle then
            handle:close()
        end
        return nil
    end
end

local function run_capture(prog)
    local handle = io.popen(prog .. " 2>&1", "r")
    if not handle then return nil end
    local out = handle:read("*a")
    handle:close()
    if out and out:match("^%s*$") then return "" end
    return out
end

function on_command(api, command, args)
    if command:lower() ~= "run" then return false end

    local prog = args:match("^%s*(.-)%s*$")
    if prog == "" then
        api.echo("Usage: !run <program>  e.g. !run explorer  !run notepad  !run calc")
        return true
    end

    -- Try to launch detached first.  If it fails, try capturing output.
    local ok = pcall(launch, prog)
    if ok then
        api.echo("Launched: " .. prog)
    else
        local out = run_capture(prog)
        if out then
            local trimmed = out:sub(1, 300):gsub("%s+$", "")
            api.echo(trimmed ~= "" and trimmed or ("Ran: " .. prog))
        else
            api.echo("run: could not start " .. prog)
        end
    end
    return true
end
