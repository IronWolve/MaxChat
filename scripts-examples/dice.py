"""Dice + magic 8-ball — an example MaxChat script using the /command hook.

Adds two commands (sent to the current channel/query):
    /roll [NdM]   roll N dice of M sides (default 1d6)
    /8ball <q>    ask the magic 8-ball

This file is seeded into <config>/scripts/ on first run. Edit it and run /reload dice to tinker —
that's the whole point of the scripting system. See SCRIPTING.md.
"""

from __future__ import annotations

import random
import re

_8BALL = [
    "It is certain.", "Without a doubt.", "Yes, definitely.", "You may rely on it.",
    "Most likely.", "Outlook good.", "Signs point to yes.", "Reply hazy, try again.",
    "Ask again later.", "Cannot predict now.", "Don't count on it.", "My reply is no.",
    "Outlook not so good.", "Very doubtful.",
]


def _send(api, text):
    """Say it to the current channel/query; fall back to a local echo on the server tab."""
    target = api.target
    if target and target != "(server)":
        api.say(target, text)
    else:
        api.echo(text)


def on_command(api, command, args):
    cmd = command.lower()
    if cmd == "roll":
        spec = args.strip().lower() or "1d6"
        m = re.fullmatch(r"(\d*)d(\d+)", spec)
        if m:
            n, sides = int(m.group(1) or 1), int(m.group(2))
        elif spec.isdigit():
            n, sides = 1, int(spec)
        else:
            n = sides = 0
        if not (1 <= n <= 20 and 2 <= sides <= 1000):
            api.echo("Usage: /roll [NdM]  — e.g. /roll 2d6")
            return True
        rolls = [random.randint(1, sides) for _ in range(n)]
        detail = f" ({', '.join(map(str, rolls))})" if n > 1 else ""
        _send(api, f"\U0001F3B2 {api.me} rolls {n}d{sides}: {sum(rolls)}{detail}")
        return True
    if cmd in ("8ball", "eightball"):
        q = args.strip()
        if not q:
            api.echo("Usage: /8ball <question>")
            return True
        _send(api, f"\U0001F3B1 {q} — {random.choice(_8BALL)}")
        return True
    return False
