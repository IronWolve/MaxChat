"""A small **Python scripting / plugin system**.

Scripts are plain ``.py`` files in ``<config>/scripts/``. A script may define any of these top-level
**hook functions** — each gets the shared ``api`` object as its first argument:

    on_load(api)                                   # when the script loads
    on_unload(api)                                 # when it unloads
    on_message(api, network, target, nick, text)   # a channel/PM message arrived
    on_join(api, network, channel, nick)           # someone joined a channel
    on_command(api, command, args)                 # a /command was typed — return True to consume it
    on_image_paste(api, image)                      # a QImage was pasted into the message box —
                                                    #   return True to consume it (e.g. upload + link)

The ``api`` object lets a script interact with the client:

    api.echo(text)            # print a line in the active chat
    api.say(target, text)     # send a message to a channel/nick on the active network
    api.insert_input(text)    # insert text at the cursor in the message box (e.g. an uploaded link)
    api.notify(title, text)   # fire a desktop notification
    api.data_dir()            # a writable folder for the script's own files
    api.me                    # your current nick
    api.target                # the active chat target (channel/nick) — e.g. api.say(api.target, …)

Scripts run **in-process with full Python** — only install scripts you trust. Manage them with
``/load`` ``/unload`` ``/reload`` ``/scripts`` or **Settings ▸ Scripts…**. See ``SCRIPTING.md``.
"""

from __future__ import annotations

import importlib.util
import os
import shutil
import sys
import time
from pathlib import Path

from PySide6.QtCore import QObject, Signal

from maxchat import config

_HOOKS = ("on_load", "on_unload", "on_message", "on_join", "on_command", "on_image_paste")


class ScriptAPI(QObject):
    """The bridge handed to every script hook."""

    echoRequested = Signal(str)
    sayRequested = Signal(str, str)
    insertInputRequested = Signal(str)
    notifyRequested = Signal(str, str)

    def __init__(self, window) -> None:
        super().__init__(window if isinstance(window, QObject) else None)
        self._w = window
        self.echoRequested.connect(window._script_echo)
        self.sayRequested.connect(window._script_say)
        self.insertInputRequested.connect(window._script_insert_input)
        self.notifyRequested.connect(window._notify)

    def echo(self, text) -> None:
        self.echoRequested.emit(str(text))

    def say(self, target, text) -> None:
        self.sayRequested.emit(str(target), str(text))

    def insert_input(self, text) -> None:
        self.insertInputRequested.emit(str(text))

    def notify(self, title, text="") -> None:
        self.notifyRequested.emit(str(title), str(text))

    def data_dir(self) -> str:
        d = os.path.join(config.config_dir(), "scripts", "data")
        os.makedirs(d, exist_ok=True)
        return d

    @property
    def me(self) -> str:
        net = self._w._scripts.ctx_net or self._w._active_net
        return net.client.nick if net is not None else ""

    @property
    def target(self) -> str:
        """The active chat target (channel/nick) — handy for ``api.say(api.target, …)`` from /commands."""
        return self._w._active_buf or ""


class ScriptManager:
    def __init__(self, window) -> None:
        self._w = window
        self.api = ScriptAPI(window)
        self.scripts: dict[str, object] = {}  # name → loaded module
        self.ctx_net = None  # the network a hook is firing for → scopes api.echo to that network

    def dir(self) -> str:
        d = os.path.join(config.config_dir(), "scripts")
        os.makedirs(d, exist_ok=True)
        return d

    def _seed_examples(self) -> None:
        """Copy each bundled example into <config>/scripts EXACTLY ONCE (tracked in ``seeded_scripts``),
        so new examples arrive on upgrade but ones the user deleted don't keep coming back."""
        examples = Path(__file__).resolve().parents[1] / "scripts-examples"
        if not examples.is_dir():
            return
        d = self.dir()
        existing = set(os.listdir(d))
        if "hello.py" in existing and "_hello.py" not in existing:  # demo is now opt-in (don't auto-load)
            try:
                os.rename(os.path.join(d, "hello.py"), os.path.join(d, "_hello.py"))
                existing.discard("hello.py")
                existing.add("_hello.py")
            except OSError:
                pass
        seeded = set(config.pref("seeded_scripts") or [])
        for f in examples.glob("*.py"):
            if f.name not in seeded:
                if f.name not in existing:
                    try:
                        shutil.copy(f, d)
                    except OSError:
                        pass
                seeded.add(f.name)
        if seeded != set(config.pref("seeded_scripts") or []):
            config.set_pref("seeded_scripts", sorted(seeded))

    def load_all(self) -> None:
        self._seed_examples()
        for fn in sorted(os.listdir(self.dir())):
            if fn.endswith(".py") and not fn.startswith("_"):
                self.load(os.path.join(self.dir(), fn))

    def load(self, path: str) -> bool:
        name = os.path.splitext(os.path.basename(path))[0]
        try:
            spec = importlib.util.spec_from_file_location(f"maxchat_script_{name}", path)
            mod = importlib.util.module_from_spec(spec)
            sys.modules[spec.name] = mod
            spec.loader.exec_module(mod)
        except Exception as e:  # noqa: BLE001 — a bad script must not crash the app
            self.api.echo(f"[scripts] failed to load {name}: {e}")
            return False
        self.scripts[name] = mod
        self._call(mod, "on_load")
        return True

    def unload(self, name: str) -> bool:
        mod = self.scripts.pop(name, None)
        if mod is None:
            return False
        self._call(mod, "on_unload")
        sys.modules.pop(f"maxchat_script_{name}", None)
        return True

    def reload(self, name: str) -> bool:
        self.unload(name)
        path = os.path.join(self.dir(), name + ".py")
        return self.load(path) if os.path.exists(path) else False

    def dispatch(self, hook: str, *args, net=None) -> bool:
        """Call ``hook`` on every script; returns True if any handler consumed it (on_command). ``net``
        scopes ``api.echo`` from inside the hook to THAT network's buffer (so a script reacting to a join
        on one network doesn't print into whatever tab happens to be in front)."""
        self.ctx_net = net
        try:
            consumed = False
            for mod in list(self.scripts.values()):
                if self._call(mod, hook, *args) is True:
                    consumed = True
            return consumed
        finally:
            self.ctx_net = None

    def _call(self, mod, hook: str, *args):
        fn = getattr(mod, hook, None)
        if not callable(fn):
            return None
        try:
            start = time.monotonic()
            result = fn(self.api, *args)
            elapsed = time.monotonic() - start
            if elapsed >= 0.25:
                self.api.echo(
                    f"[scripts] {mod.__name__.replace('maxchat_script_', '')}.{hook} "
                    f"took {elapsed:.2f}s"
                )
            return result
        except Exception as e:  # noqa: BLE001 — isolate script errors
            self.api.echo(f"[scripts] {mod.__name__.replace('maxchat_script_', '')}.{hook}: {e}")
            return None
