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

function on_command(api, command, args)
    if command:lower() ~= "run" then return false end

    if not api.launch then
        api.echo("[run] needs 'Run programs' permission — Preferences → Scripts → run")
        return true
    end

    local prog = args:match("^%s*(.-)%s*$")
    if prog == "" then
        api.echo("Usage: !run <program>  e.g. !run explorer  !run notepad  !run calc")
        return true
    end

    local ok = api.launch(prog)
    if ok then
        api.echo("Launched: " .. prog)
    else
        api.echo("run: could not start " .. prog)
    end
    return true
end
