"""Hello — a tiny demo script (NOT auto-loaded).

The leading underscore means MaxChat won't load this on startup. To try it, either `/load _hello`
once, or rename the file to ``hello.py`` (drop the underscore) so it loads automatically.

Adds a ``/hello`` command and shows the ``on_command`` / ``on_join`` hook signatures. Safe to delete.
``on_command`` returns True to tell the client it handled the command. The ``on_join`` hook is a quiet
no-op by default (announcing every join is noisy) — uncomment its body to try it.
"""


def on_load(api):
    api.echo("[hello] loaded — try typing /hello")


def on_command(api, command, args):
    if command == "hello":
        api.echo(f"Hello, {api.me or 'there'}!  (from hello.py)   args={args!r}")
        return True  # we handled this command — the client won't try to run it
    return False


def on_join(api, network, channel, nick):
    # Quiet by default — the client already shows joins. api.echo now prints into the hook's OWN network
    # tab (not whatever tab is in front), so uncomment to greet joiners if you like:
    # if nick != api.me:
    #     api.echo(f"[hello] {nick} joined {channel} on {network}")
    pass
