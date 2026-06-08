"""Parse a raw IRC line into its parts (RFC 1459/2812 + IRCv3 message tags).

Pure and UI-free so it can be unit-tested without a display or a socket. Grammar:

    [ "@" tags SPACE ] [ ":" prefix SPACE ] command *( SPACE param ) [ SPACE ":" trailing ]
"""

from __future__ import annotations

from dataclasses import dataclass, field

_TAG_UNESCAPE = {"\\:": ";", "\\s": " ", "\\\\": "\\", "\\r": "\r", "\\n": "\n"}


def _unescape_tag_value(value: str) -> str:
    out: list[str] = []
    i = 0
    while i < len(value):
        if value[i] == "\\" and i + 1 < len(value):
            out.append(_TAG_UNESCAPE.get(value[i : i + 2], value[i + 1]))
            i += 2
        else:
            out.append(value[i])
            i += 1
    return "".join(out)


@dataclass
class IRCMessage:
    raw: str
    command: str
    params: list[str] = field(default_factory=list)
    prefix: str = ""
    tags: dict[str, str] = field(default_factory=dict)

    @property
    def nick(self) -> str:
        """The nick portion of the prefix (before ``!``), if any."""
        return self.prefix.split("!", 1)[0] if self.prefix else ""

    @property
    def trailing(self) -> str:
        """The last parameter (usually the human-readable message text)."""
        return self.params[-1] if self.params else ""


def parse(line: str) -> IRCMessage:
    s = line.rstrip("\r\n")
    raw = s
    tags: dict[str, str] = {}
    prefix = ""

    if s.startswith("@"):
        tagstr, _, s = s[1:].partition(" ")
        for item in tagstr.split(";"):
            if not item:
                continue
            key, sep, val = item.partition("=")
            tags[key] = _unescape_tag_value(val) if sep else ""
    s = s.lstrip(" ")

    if s.startswith(":"):
        prefix, _, s = s[1:].partition(" ")
    s = s.lstrip(" ")

    command, _, rest = s.partition(" ")
    params: list[str] = []
    rest = rest.lstrip(" ")
    while rest:
        if rest.startswith(":"):
            params.append(rest[1:])  # trailing: keep internal spaces/colons verbatim
            break
        token, _, rest = rest.partition(" ")
        params.append(token)
        rest = rest.lstrip(" ")

    return IRCMessage(raw=raw, command=command.upper(), params=params, prefix=prefix, tags=tags)
