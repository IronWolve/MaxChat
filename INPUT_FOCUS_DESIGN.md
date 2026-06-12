# Input Focus Design

## Key Redirect (HexChat-style)

Typing anywhere in the main window while no modal/menu is active automatically
jumps to the message box. This mirrors HexChat behaviour and the Python original.

### Implementation

`MainWindow::buildLayout()` installs a **global app-level** event filter:

```cpp
qApp->installEventFilter(this);   // MainWindow.cpp, near other installEventFilter calls
```

`MainWindow::eventFilter` calls `redirectKeyToInput(e)` at the very top for
every `QEvent::KeyPress` event, before any per-widget handling.

`MainWindow::redirectKeyToInput(QKeyEvent* e)` — the core logic:

| Guard | Condition | Action |
|-------|-----------|--------|
| 1 | `QApplication::activePopupWidget() != nullptr` | skip (menu / combo popup) |
| 1 | `QApplication::activeModalWidget() != nullptr` | skip (modal dialog) |
| 2 | `QApplication::activeWindow() != this` | skip (non-modal popup focused) |
| Escape | Key_Escape and focus ≠ input | setFocus on input, return true |
| 3 | `isTextEntry(focusWidget)` → true | skip (writable text field) |
| 4 | focusWidget is QAbstractButton / QAbstractItemView / QAbstractSlider / QTabBar | skip (interactive nav widget) |
| 5 | event has Ctrl/Alt/Meta modifier | skip (let shortcuts through) |
| 6 | `e->text().isEmpty()` or not printable | skip (arrows, F-keys, Tab) |
| ✓ | otherwise | `m_input->setFocus(); m_input->insertPlainText(text)` |

`isTextEntry(w)` returns true for: QAbstractSpinBox, editable QComboBox,
QLineEdit, writable QTextEdit, writable QPlainTextEdit.
The **chat view** (QTextBrowser) is read-only → `isTextEntry` returns false →
keystrokes typed while the chat is clicked redirect to input. ✓

### Why the guards are all necessary

- **Guard 1 (popup/modal)**: Without this, typing in a right-click spell menu
  or a modal dialog would jump focus to the message box mid-interaction.
- **Guard 2 (activeWindow)**: Non-modal dialogs (Raw Log, URL List, etc.) have
  their own text fields. Without this, clicking in one and typing would
  redirect to the main window's input.
- **Guard 3 (isTextEntry)**: Prefs fields, search boxes, alias editors all live
  as non-modal children of this window. Without this, typing there would be
  stolen.
- **Guard 4 (interactive nav)**: Buttons respond to Space/Enter; lists use
  arrow keys; sliders and tab bars use arrows. Stealing those breaks keyboard
  navigation completely.
- **Guard 5 (modifiers)**: Ctrl+F (find), Ctrl+C (copy), Ctrl+Tab (buffer
  switch) must not be swallowed by the redirect. **AltGr exception
  (2026-06-12)**: on Windows AltGr arrives as Ctrl+Alt, and international
  layouts type printable characters with it (é € @ [ ]). A key that is BOTH
  Ctrl+Alt AND printable is AltGr, not a shortcut — it falls through to the
  redirect. Without this, international users typing after clicking chat got
  nothing.
- **Guard 6 (printable)**: Arrow keys, F-keys, Tab, Delete, Home/End must not
  be stolen from widgets that rely on them.

### Install location

`qApp->installEventFilter(this)` must be called AFTER `m_input` is
constructed. It lives near the per-widget installs in `buildLayout()`:

```cpp
m_input->installEventFilter(this);
m_input->viewport()->installEventFilter(this);
qApp->installEventFilter(this);   // ← global redirect
```

**Do NOT** move this to per-widget installs. The whole point is that the
filter catches events for every widget without needing to enumerate them.

### Redirect insertion point (2026-06-12)

The redirect moves the input cursor to END before inserting the typed
character. Inserting at the saved cursor garbled drafts: with "hello world"
and the cursor parked after "hello", clicking chat and typing "x" produced
"helloxworld" while the visible cursor sat at the end.

### Known gaps (documented, not yet fixed)

- **IME composition** (CJK, dead keys) arrives as `QEvent::InputMethod`, not
  KeyPress — the redirect never sees it, so composition typed over the chat
  view is dropped. Fix would be an InputMethod-event handoff to `m_input`.
- **Esc is handled before Guard 3** (text-entry): a future writable widget in
  the main window (e.g. a find bar) would have its Esc stolen. Reorder when
  that widget arrives.
- **Keyboard-triggered context menus** (Menu key) hand `pos()` in widget
  coordinates to `cursorForPosition`, which expects viewport coordinates —
  spell suggestions can pick a neighbouring word. Mouse-triggered menus are
  correct.

### First-show focus

`MainWindow::showEvent` sets focus to `m_input` on the first show:

```cpp
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (!m_focusedOnce && m_input != nullptr) {
        m_focusedOnce = true;
        m_input->setFocus();
    }
}
```

---

## Window Geometry Persistence

All resizable dialogs save/restore their size and position via
`attachGeometryPersist(dialog, m_settings, "geom_<key>")` defined in
`src/ui/GeometryPersist.cpp`.

`attachGeometryPersist` connects to `QDialog::finished` to save and restores
the geometry before the first show. The saved value is a base64-encoded blob
under the `geom_<key>` key in the settings JSON.

### The saveRaw overwrite problem (fixed 2026-06-11)

Dialogs that save settings call `m_settings.saveRaw(dialog.settings())`.
`saveRaw` previously **replaced the entire settings file**. Since
`QDialog::finished` fires before `exec()` returns (geometry saved),
and `saveRaw` is called after `exec()` returns, the geometry key was
immediately overwritten.

**Fix**: `SettingsStore::saveRaw` now reads the existing file first and
preserves any `geom_*` keys not present in the incoming map. Since geometry is
application state managed independently of any dialog's settings, this is
always the correct behaviour.

### Complete dialog inventory

Grouped by menu / access point:

#### Server menu
| Dialog | geom key |
|--------|----------|
| Server List | `geom_server_list` |
| Quick Connect | `geom_quick_connect` |
| Channel List | `geom_channel_list` |

#### Settings menu
| Dialog | geom key |
|--------|----------|
| Preferences | `geom_preferences` |
| Aliases | `geom_aliases` |
| Ignore List | `geom_ignore_list` |
| Friends / Notify | `geom_friends_notify` |
| Shortcut Editor | `geom_shortcut_editor` |
| Scripts Manager | `geom_scripts_manager` |

#### Channel menu
| Dialog | geom key |
|--------|----------|
| Channel Modes | `geom_channel_modes` |
| Ban List | `geom_ban_list` |

#### Comic menu
| Dialog | geom key |
|--------|----------|
| Comic Settings | `geom_comic_settings` |
| Browse Characters | `geom_char_gallery` |

#### Tools / View (non-modal, persistent)
| Dialog | geom key |
|--------|----------|
| Find in Chat | `geom_chat_find` |
| Raw Log | `geom_raw_log` |
| URL List | `geom_url_list` |

#### Files (DCC)
| Dialog | geom key |
|--------|----------|
| DCC Transfers | `geom_dcc_transfers` |

### Dialogs intentionally without geometry persistence
- **Join Channel**: `QInputDialog::getText` — one-liner, no resize needed
- **Emotion Picker**: `QInputDialog::getItem` — small dropdown
- **About**: `QMessageBox::about` — fixed size
- **Color Picker** (Ctrl+K): small inline picker, not user-resizable
- **Image Viewer / Media Player**: ephemeral pop-ups per link
- **Script Info** (Scripts → Settings): tiny info card, child of Scripts Manager

### Adding geometry to a new dialog

```cpp
MyDialog dialog(/* ... */, this);
attachGeometryPersist(&dialog, m_settings, QStringLiteral("geom_my_dialog"));
dialog.exec();  // or dialog.show() for non-modal
```

For `WA_DeleteOnClose` non-modal dialogs (heap-allocated):
```cpp
auto* dialog = new MyDialog(this);
dialog->setAttribute(Qt::WA_DeleteOnClose);
attachGeometryPersist(dialog, m_settings, QStringLiteral("geom_my_dialog"));
dialog->show();
```

No manual merge is needed before `saveRaw` — `SettingsStore::saveRaw`
handles it automatically.
