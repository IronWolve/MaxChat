-- URL logger — records every URL seen in chat to `urls.log` in the script's
-- data folder. A working example of the on_message hook.
--
-- The host sandbox blocks raw file I/O, so writes go through api.append_file,
-- which is scoped to this script's own data dir (no path traversal). Open the
-- log via Settings ▸ Scripts… ▸ Open folder. Turn it off with
-- /unload url_logger or by deleting this file. See SCRIPTING.md.

function on_load(api)
  api.echo("[url_logger] active — logging URLs to " .. api.data_dir() .. "/urls.log")
end

function on_message(api, network, target, nick, text)
  for url in text:gmatch("https?://%S+") do
    api.append_file(
      "urls.log",
      api.timestamp() .. "\t" .. network .. "/" .. target ..
        "\t<" .. nick .. ">\t" .. url .. "\n")
  end
end
