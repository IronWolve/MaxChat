"""IRC protocol layer: connection, message parsing, and channel/user state.

Kept independent of the UI so it can be unit-tested without a display. The
transport choice (Qt's ``QTcpSocket`` vs. a thread + sockets vs. ``asyncio``,
or an existing library such as the one already used in the user's IRC-bot
project) is an open planning question — see PLAN.md.
"""
