# Channel List Design

## Current implementation (C++ / Qt6)

### Layout

```
[ Get List ]  [ Search channels or topics  ×]  Min users: [ 0 ]
┌─────────────────────────────────────────────────────────┐
│ Channel ▲  │ Users ▼ │ Topic                            │
├───────────────────────────────────────────────────────── │
│ #linux      │   842   │ Linux help and support           │
│ #games      │   210   │ Gaming discussion                │
│ ...                                                      │
└─────────────────────────────────────────────────────────┘
342 of 1205 channels shown, loaded
[ Join ]  [ Copy ]  [ Close ]
```

### Features

- **Get List button** — triggers `/LIST` (emits `listRequested` signal → MainWindow sends `LIST` raw). Button disabled while loading.
- **Search box** — real-time filter on channel name + topic, case-insensitive. Has a clear (×) button.
- **Min users spinbox** — hides channels below the threshold. Default 0 = show all. Labeled "any" at 0.
- **Sortable table** — click column header to sort. Defaults to users descending. Users column sorts numerically.
- **Status line** — shows "Loading…", "N channels loaded", or "N of M channels shown, loaded".
- **Join** — joins selected channel (also double-click). Disabled when nothing selected.
- **Copy** — copies selected rows to clipboard (tab-separated); falls back to all visible rows if none selected.

### Signals

| Signal | Direction | Description |
|--------|-----------|-------------|
| `joinRequested(channel)` | dialog → MainWindow | User asked to join |
| `listRequested()` | dialog → MainWindow | User clicked Get List |

### MainWindow wiring

- `openChannelList(reset=true)` — opens/raises dialog, clears if `reset`.
- Called from Server > Channels menu, toolbar button, and `/list` command.
- `listRequested` handler in MainWindow clears the table then sends `LIST`.
- IRC 322 (RPL_LIST) → `addChannel()` per entry.
- IRC 323 (RPL_LISTEND) → `setComplete(true)`.

## Python backport notes

The equivalent Python path must implement:
- `listRequested` signal → call `connection.send_raw("LIST")` / `send_list_command()`
- `setFetching(True)` before sending, `setComplete(True)` on 323
- Min users spinbox wired to the same `apply_filter` path
- `clearChannels()` on new list fetch

Key difference: Python version may need to buffer 322 responses if the server sends them faster than the UI event loop processes. Consider batching `addChannel` calls every 100ms via a timer.
