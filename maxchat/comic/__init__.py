"""Comic rendering: turn a stream of chat messages into comic panels.

This is the part that makes the client distinctive: characters with selectable
emotions, speech/thought balloons with tails, panel layout, and backdrops. The
rendering approach (``QGraphicsScene`` vs. a custom ``QPainter`` widget) is an
open planning question — see PLAN.md.
"""
