-- seen.lua — remember when each nick last spoke; /seen <nick> reports it.
-- Shows persistent api.set/api.get (stored per-script as JSON, survives reload).

function on_message(api, network, target, nick, text)
  api.set("seen:" .. nick:lower(), api.timestamp())
end

function on_command(api, command, args)
  if command ~= "seen" then
    return false
  end
  local who = args:match("^%s*(%S+)")
  if not who then
    api.echo("usage: /seen <nick>")
    return true
  end
  local when = api.get("seen:" .. who:lower())
  if when then
    api.echo(who .. " was last seen speaking at " .. when)
  else
    api.echo("no record of " .. who)
  end
  return true
end
