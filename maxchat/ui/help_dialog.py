"""Help ▸ Commands & Shortcuts — a quick reference of the /commands, keyboard shortcuts, and the
mIRC formatting codes. Keyboard rows show your CURRENT bindings (rebindable in Settings ▸ Keyboard
Shortcuts)."""

from __future__ import annotations

from html import escape

from PySide6.QtWidgets import QTextBrowser, QVBoxLayout

from maxchat.ui.shadow_dialog import ShadowDialog

# (command, description) — grouped. Channel commands act on the current channel unless a #chan is given.
COMMANDS = [
    ("Channels", [
        ("/join #chan [key]", "join a channel"),
        ("/part [#chan] [reason]", "leave a channel"),
        ("/cycle", "part then rejoin the current channel"),
        ("/topic [text]", "show or set the channel topic"),
        ("/names [#chan]", "list who's in the channel"),
        ("/invite <nick> [#chan]", "invite someone"),
        ("/list [filter]", "browse the network's channel list"),
        ("/hop", "leave then rejoin (alias of /cycle)"),
        ("/mute /unmute", "stop / resume highlights from this channel"),
    ]),
    ("Messaging", [
        ("/msg <target> <text>", "send a private message / to a channel"),
        ("/query <nick> [text]", "open a PM tab"),
        ("/me <action>", "send an action (/me waves)"),
        ("/notice <target> <text>", "send a notice"),
        ("/amsg <text>", "message every channel you're in"),
        ("/ame <action>", "action to every channel you're in"),
        ("/onotice <text>", "notice the current channel's ops (@#chan)"),
        ("/ctcp <target> <type>", "send a CTCP (PING/VERSION/TIME…)"),
        ("/sound <file> [text]", "play a CTCP sound (both sides need the .wav)"),
        ("/shrug /tableflip /lenny", "insert an emoticon (+ /unflip /disapprove)"),
    ]),
    ("Channel ops", [
        ("/op /deop <nick>", "give / take operator"),
        ("/voice /devoice <nick>", "give / take voice"),
        ("/halfop /dehalfop <nick>", "give / take half-op"),
        ("/kick <nick> [reason]", "kick someone"),
        ("/ban <nick|mask>", "ban; /kickban (/kb) bans + kicks"),
        ("/mode [#chan] <modes>", "set channel/user modes"),
        ("/wallops <message>", "message everyone with +w (opers)"),
        ("/oper <user> <pass>", "become an IRC operator · /kill <nick> disconnects"),
    ]),
    ("You", [
        ("/nick <newnick>", "change your nickname"),
        ("/away [message]", "mark yourself away"),
        ("/back", "clear your away status"),
        ("/whois <nick>", "look someone up · /whowas for offline"),
        ("/ignore [mask]", "ignore a nick/host · /unignore to clear"),
        ("/ns /cs /ms <text>", "message NickServ / ChanServ / MemoServ"),
        ("/identify <pass>", "identify to services · /ghost <nick> <pass> reclaims a nick"),
    ]),
    ("Files & tools", [
        ("/dcc send|chat|close", "DCC file transfer / direct chat"),
        ("/alias <name> <text>", "make a command alias · /unalias"),
        ("/load /unload /reload", "manage Python scripts · /scripts lists them"),
        ("/clear", "clear the current chat · /clearall for every chat"),
        ("/lag", "measure server round-trip time"),
        ("/uptime", "client + connection uptime"),
        ("/netinfo", "the server's ISUPPORT summary"),
        ("/sysinfo [send]", "your system info — local, or 'send' to share it"),
        ("/raw <line>", "send a raw IRC line (advanced)"),
        ("/quit", "disconnect this network"),
    ]),
]

# mIRC formatting control keys (in the message box).
FORMAT_KEYS = [
    ("Ctrl+B", "bold"), ("Ctrl+I", "italic"), ("Ctrl+U", "underline"),
    ("Ctrl+R", "reverse"), ("Ctrl+O", "reset (plain)"),
]


class HelpDialog(ShadowDialog):
    def __init__(self, parent=None, shortcuts: list[tuple[str, str]] | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Commands & Shortcuts")
        self.set_autosize(70, 34)
        v = QVBoxLayout(self.body)
        browser = QTextBrowser()
        browser.setOpenExternalLinks(False)
        browser.setHtml(self._html(shortcuts or []))
        v.addWidget(browser)

    @staticmethod
    def _html(shortcuts: list[tuple[str, str]]) -> str:
        def row(a: str, b: str) -> str:
            return (f'<tr><td style="padding:2px 16px 2px 0;white-space:nowrap;"><b>{escape(a)}</b></td>'
                    f'<td style="padding:2px 0;">{escape(b)}</td></tr>')

        parts = ["<h2>Commands</h2>"]
        for title, cmds in COMMANDS:
            parts.append(f"<h3>{escape(title)}</h3><table>")
            parts += [row(c, d) for c, d in cmds]
            parts.append("</table>")
        parts.append("<h2>Keyboard shortcuts</h2><table>")
        parts += [row(keys, label) for label, keys in shortcuts]
        parts.append("</table>")
        parts.append("<h2>Text formatting (in the message box)</h2><table>")
        parts += [row(k, d) for k, d in FORMAT_KEYS]
        parts.append("</table>")
        parts.append("<h2>Finding servers</h2>")
        parts.append('<p>More IRC networks and server lists can be found at '
                     '<a href="https://www.irchelp.org">https://www.irchelp.org</a> and '
                     '<a href="https://netsplit.de/">https://netsplit.de/</a>.</p>')
        parts.append('<p style="color:#888;">Channel commands use the current channel unless you name a '
                     "#channel. Shortcuts are rebindable in Settings ▸ Keyboard Shortcuts.</p>")
        return "".join(parts)
