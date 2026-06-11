-- reminder.lua — /remind <seconds> <text>
-- Echoes a reminder back to you after N seconds. Shows api.timer + a one-shot
-- timer that cancels itself.

function on_command(api, command, args)
  if command ~= "remind" then
    return false
  end
  local secs, text = args:match("^(%d+)%s+(.+)$")
  if not secs then
    api.echo("usage: /remind <seconds> <text>")
    return true
  end
  local id
  id = api.timer(tonumber(secs) * 1000, function()
    api.echo("reminder: " .. text)
    api.cancel_timer(id) -- fire once, then stop
  end)
  api.echo("ok — I'll remind you in " .. secs .. "s")
  return true
end
