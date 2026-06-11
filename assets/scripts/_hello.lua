-- Hello — a tiny demo script (NOT auto-loaded).
--
-- The leading underscore means MaxChat won't load this on startup. To try it,
-- either `/load _hello` once, or rename the file to `hello.lua` (drop the
-- underscore) so it loads automatically.
--
-- Shows the on_command / on_join hook shapes. on_command returns true to tell
-- the client it handled the command. on_join is a quiet no-op by default
-- (announcing every join is noisy) — uncomment its body to try it.
-- Safe to delete. See SCRIPTING.md for the full API.

function on_load(api)
  api.echo("[hello] loaded — try typing /hello")
end

function on_command(api, command, args)
  if command == "hello" then
    local who = api.me() ~= "" and api.me() or "there"
    api.echo("Hello, " .. who .. "!  (from hello.lua)   args=" .. string.format("%q", args))
    return true  -- we handled this command — the client won't try to run it
  end
  return false
end

function on_join(api, network, channel, nick)
  -- Quiet by default — the client already shows joins. api.echo prints into the
  -- hook's OWN network tab. Uncomment to greet joiners if you like:
  -- if nick ~= api.me() then
  --   api.echo("[hello] " .. nick .. " joined " .. channel .. " on " .. network)
  -- end
end
