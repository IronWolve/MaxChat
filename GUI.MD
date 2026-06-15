# MaxChat C++ GUI Reference

Last reviewed: 2026-06-15 (6 bugs fixed this session)

This document is the single reference for MaxChat C++ GUI surfaces: windows, dialogs, popups, menus, buttons, side panels, themes, fonts, shadows, and the tests that protect them. Update this file whenever a GUI surface changes so grouped windows and related menus do not drift apart.

## Goals

- Keep every user-facing GUI surface documented in one place.
- Make it obvious which code owns each menu, dialog, popup, button, and visual system.
- Record known GUI bugs and fragile areas before they become release regressions.
- Provide a checklist for future GUI work across Linux, Windows, WSLg, and headless tests.

## Main Source Map

- Main window, menu bar, toolbar, sidebars, chat area, context menus, popups, tray, theme/font application: `src/ui/MainWindow.cpp`, `src/ui/MainWindow.h`
- Preferences window: `src/ui/PreferencesDialog.cpp`, `src/ui/PreferencesDialog.h`
- Server list and network editor: `src/ui/ServerListDialog.cpp`, `src/ui/ServerListDialog.h`
- Theme catalog, theme JSON parsing, wallpapers, QSS: `src/ui/ThemeCatalog.cpp`, `src/ui/ThemeCatalog.h`
- Theme editor: `src/ui/ThemeEditorDialog.cpp`, `src/ui/ThemeEditorDialog.h`
- Terminal windows: `src/ui/ScriptTerminalDialog.cpp`, `src/ui/ScriptTerminalDialog.h`, `src/ui/TerminalFrame.*`, `src/ui/TerminalProfile.*`
- Comic strip view and comic settings: `src/ui/ComicView.*`, `src/ui/ComicSettingsDialog.*`
- Chat input spelling: `src/ui/SpellTextEdit.*`, `src/ui/SpellcheckHighlighter.*`, `src/spell/*`
- Link image/video/audio popups: `src/ui/ImageViewerDialog.*`, `src/ui/MediaPlayerDialog.*`, `src/ui/AudioPlayerBar.*`
- Notifications/toasts: `src/ui/ToastWidget.*`, `src/ui/Notifier.*`, `src/ui/SoundPlayer.*`
- Dialog geometry persistence helper: `src/ui/GeometryPersist.*`

## Asset Map

- App themes: `assets/themes/app-themes.json`
- Chat themes: `assets/themes/chat-themes.json`
- Wallpapers: `assets/wallpapers/`
- Bundled fonts: `assets/fonts/`
- Dictionaries: `assets/dictionaries/`
- Sounds: `assets/sounds/`
- Icons: `assets/icons/`
- Lua scripts: `assets/scripts/`

Bundled fonts:

- `JetBrainsMono-Regular.ttf`
- `JetBrainsMono-Bold.ttf`
- `ComicRelief-Regular.ttf`
- `ComicRelief-Bold.ttf`
- `SymbolsNerdFontMono-Regular.ttf`

Bundled wallpapers:

- `aurora.jpg`
- `circuit.png`
- `deep-waves.jpg`
- `dusk-peaks.jpg`
- `ember-bokeh.jpg`
- `hex-mesh.png`
- `matrix-rain.jpg`
- `nebula.jpg`
- `synthwave.png`
- `vaporwave-1.jpg`
- `vaporwave-2.jpg`

## Main Window

Owner: `src/ui/MainWindow.cpp`

Window title format:

- Disconnected/default: `MaxChat C++ 0.1.0-dev - MaxChat C++`
- Connected/current target: includes network and current target/topic as available.

Root layout:

- `centralRoot`: vertical root layout, 6 px margins, 4 px spacing.
- Topic bar: `topicLabel`, 30 px minimum height, single-line elided plain text.
- Optional buffer tabs: `bufferTabBar`, called `Buttons as Tabs` in the View menu.
- Main splitter: `mainSplitter`, horizontal, object name `mainSplitter`.
- Chat splitter: `chatSplitter`, vertical, object name `chatSplitter`.

Main splitter panes, left to right:

- Network/server tree: `networkTree`
- Chat column: `chatColumn`
- Member panel: `memberPanel`

Default splitter sizing:

- Network tree: about 180 px.
- Chat column: about 720 px.
- Member panel: about 190 px.
- Side panes are collapsible and synchronized with View menu actions.

Chat column contents:

- Comic strip view: `comicView`, visible only when comic mode is active for the current buffer.
- Chat transcript: `chatView`, a read-only `QTextBrowser`.
- Audio player bar: `audioPlayerBar`, hidden until audio playback starts.
- Input row: hidden `nickLabel` plus `messageInput`.
- Status bar: Qt status bar at bottom.

Chat view behavior:

- `chatView` does not open links directly. Anchor clicks are routed through `handleChatAnchorClicked`.
- Chat line alignment uses HTML/table-like formatting from the chat line formatter and must preserve time, nick column, separator column, and wrapped message text.
- The old pipe character separator should not be used as the real alignment mechanism. The movable chat separator controls the nick/message split.
- Server-scoped replies should route to the server buffer, not channel buffers.
- Noisy channel numerics should not duplicate channel lines.
- Replayed log lines must use the same alignment behavior as live lines.

Known stale content:

- Startup placeholder text in `MainWindow.cpp` still says `Native C++/Qt port skeleton is running.` This should be removed or replaced before public release polish.

## Top Menu Bar

Owner: `MainWindow::buildMenus()`

Top-level menus:

- `Server`
- `View`
- `Tools`
- `Settings`
- `Comic`
- `Help`

Menu font:

- Explicitly set in `applyTheme()` and `applyFontSettings()`.
- Popup menus inherit through stylesheet rules for `QMenuBar, QMenu, QToolBar, QToolButton`.

Menu shadows:

- Menus use native Qt/platform popup windows. There is no custom `QGraphicsDropShadowEffect` for menus.
- Linux/WSLg menu behavior is fragile because native popup/grab behavior differs by compositor.

## Server Menu

Owner: `MainWindow::buildMenus()`

Actions:

- `Server List...`, shortcut `Ctrl+S`, opens `ServerListDialog`.
- `Quick Connect...`, opens `QuickConnectDialog`.
- `Disconnect`, calls current-server disconnect.
- `Disconnect All`, currently calls current-server disconnect.
- `Reconnect All`, currently calls current-server reconnect.
- `Join...`, shortcut `Ctrl+J`, opens join-channel input.
- `Leave Channel`, parts the current channel.
- `Leave All Channels`, leaves all joined channels for the current context.
- `Channels...`, opens channel list.
- `Quit`, quits the application.

Known GUI/behavior issue:

- `Disconnect All` and `Reconnect All` are labeled as global actions but currently call current-server handlers. Either implement true all-network behavior or rename them.

Safety/polish note:

- There is a separator between `Leave All Channels` and `Channels...` to reduce accidental destructive clicks. Keep this separation.

## View Menu

Owner: `MainWindow::buildMenus()`

Actions:

- `Button Bar`, checkable, toggles `mainToolbar`.
- `Server List`, checkable, toggles `networkTree`.
- `Member List`, checkable, toggles `memberPanel`.
- `Buttons as Tabs`, checkable, toggles `bufferTabBar`.
- `Chat Separator`, checkable, toggles the movable nick/message separator.
- `Clear Current Chat`, clears the active buffer.
- `Mark All Read`, clears unread/highlight counters.

Terminology rule:

- Use `Buttons as Tabs`, not `Buffers as Tabs`.
- Internal code can use buffer terminology, but live GUI labels should not say `Buffers` for this feature.

Known GUI polish issue:

- There are two consecutive separators in the View menu definition. This is harmless but should be cleaned up.

## Tools Menu

Owner: `MainWindow::buildMenus()`

Actions:

- `Find in Chat...`, shortcut `Ctrl+F`, opens `ChatFindDialog`.
- `Open Log Folder`, opens the log directory.
- `URL List...`, opens `UrlListDialog`.
- `Raw Log...`, opens `RawLogDialog`.
- `Do Not Disturb`, checkable, suppresses toast/flash/tray/beep/sound behavior.

## Settings Menu

Owner: `MainWindow::buildMenus()`

Actions:

- `Preferences...`, shortcut `Ctrl+P`, opens `PreferencesDialog`.
- `Ignore List...`, opens `IgnoreListDialog`.
- `Aliases...`, opens `AliasEditorDialog`.
- `Themes`, submenu.
- `Keyboard Shortcuts...`, opens `ShortcutEditorDialog`.
- `Friends / Notify...`, opens `FriendsNotifyDialog`.
- `Scripts...`, opens script manager.
- `File Transfers...`, opens DCC transfers.
- `Import Settings...`
- `Export Settings...`
- `Reset Server List`

Themes submenu:

- `Theme`: app/chrome themes from `assets/themes/app-themes.json`.
- `Chat Theme`: chat transcript themes from `assets/themes/chat-themes.json`.
- `Wallpaper`: `Theme Default`, `Off`, bundled wallpapers, and `Load Image...`.
- `Saved Looks`: user-saved theme/chat/wallpaper combinations, with delete submenu.

## Comic Menu

Owner: `MainWindow::buildMenus()`

Actions:

- `Comic Mode`, checkable, shortcut `Ctrl+M`; disabled until a valid buffer context exists.
- `Emotion...`
- `Comic Settings...`
- `Browse Characters...`
- `Save Comic...`
- `Character Names`, checkable.

Comic mode is per-buffer opt-in. The global comic state follows whether any buffer is opted in.

## Help Menu

Owner: `MainWindow::buildMenus()`

Actions:

- `Commands & Shortcuts...`, shortcut `F1`, opens a message box.
- `Comic Chat Guide...`, opens a message box.
- `Check for Updates...`, message box flow.
- `About`, opens a message box.

## Main Button Bar

Owner: `MainWindow::buildToolbar()`

Object name: `mainToolbar`

Toolbar rules:

- Text-only buttons.
- Not movable.
- Visibility controlled by View > `Button Bar`.
- The toolbar is deliberately a top button row, not a landing-page style feature area.

Buttons:

- `Servers`
- `Members`
- `Channels`
- `Join`
- `Comic`
- `Emotion`
- `URLs`
- `Transfers`
- `Prefs`

Stub/feature rule:

- Planned feature buttons may exist before full functionality, but they should either open the best current placeholder dialog or be clearly harmless. They should not silently do nothing.

## Network Tree

Owner: `MainWindow::buildLayout()`, tree refresh helpers, context menu helpers.

Object name: `networkTree`

Behavior:

- Left-side server/channel/query/navigation tree.
- Header hidden.
- Root/server rows show network/server context.
- Channel rows show joined channels.
- Query rows show private-message buffers.
- Terminal rows are launchers, not chat buffers.
- Unread/highlight counts are reflected on inactive rows.
- Current selection is stored by network/target identity, not display label.

Default first-run behavior:

- Server list panel should be visible by default.
- Member list panel should be visible by default.

Context menu for terminal rows:

- `Open Terminal`
- `Kill Terminal`

Context menu for server/status/root rows:

- `Leave All Channels`
- `Disconnect`
- `Reconnect Now`
- `Close Server`
- `Server List...`
- `Quick Connect...`

Context menu for channel/query rows:

- `Select`
- `Copy Name`
- Channel-only: `Refresh Topic / Names / Modes`
- Channel-only: `Set Topic...`
- Channel-only: `Channel Modes...`
- Channel-only: `Ban List...`
- Channel-only: `Leave Channel`
- `Close`

## Member Panel

Owner: `MainWindow::buildLayout()`, member refresh helpers, member context menu.

Components:

- `channelModesButton`, text `Channel Modes...`
- `membersHeader`, normally shows count such as `25 users`
- `memberList`

Behavior:

- Right-side member list is visible by default.
- Double-clicking a member opens a query.
- Sorting can be configured by preferences.
- Member colors and status prefixes are rendered in the list.

Member context menu:

- `WhoIs <nick>`
- `Message <nick>`
- `Copy Nick`
- `Send File to <nick>...`
- `DCC Chat with <nick>`
- `Set Color...`
- `Reset Color` when an override exists.
- `Assign Comic Character...` when comic characters are loaded.
- `Ignore <nick>` or `Unignore <nick>`
- `Add Notify` or `Remove Notify`
- `Operator` submenu when current target is a channel.
- `Kick / Ban` submenu when current target is a channel.
- `CTCP` submenu.

Operator submenu:

- `Give Owner`
- `Take Owner`
- `Give Admin`
- `Take Admin`
- `Give Op`
- `Take Op`
- `Give Half-Op`
- `Take Half-Op`
- `Give Voice`
- `Take Voice`

Known GUI polish issue:

- The operator submenu construction adds separators inside the loop. Check for an unnecessary trailing separator after the final mode pair.

Kick/Ban submenu:

- `Kick`
- `Kick With Reason...`
- `Ban`
- `Kick + Ban`

CTCP submenu:

- `PING`
- `VERSION`
- `TIME`
- `CLIENTINFO`

## Buffer Tabs

Owner: `MainWindow::buildLayout()`

Object name: `bufferTabBar`

GUI label:

- View menu action is `Buttons as Tabs`.

Behavior:

- Hidden by default unless preference enables it.
- Scroll buttons enabled.
- Tabs are closable.
- Right-click context menu depends on target type.

Tab context menu for server/status target:

- `Disconnect`
- `Reconnect Now`
- `Server List...`
- `Quick Connect...`

Tab context menu for other targets:

- `Close`

## Message Input

Owner: `MainWindow::buildLayout()`, `MainWindow::eventFilter()`, `SpellTextEdit`

Object name: `messageInput`

Behavior:

- Plain text only.
- Dynamic height from 1 to 5 lines.
- No visual word wrap inside the input.
- `Enter` sends.
- `Shift+Enter` inserts newline.
- `Up` and `Down` navigate input history.
- `Tab` and `Backtab` complete nick/channel/command candidates.

Formatting shortcuts:

- `Ctrl+B`: bold control code.
- `Ctrl+I`: italic control code.
- `Ctrl+U`: underline control code.
- `Ctrl+R`: reverse/reset-related control code.
- `Ctrl+O`: reset formatting.
- `Ctrl+K`: opens `ColorPickerDialog`.

Input context menu:

- Starts with spell suggestions when the word under cursor is misspelled.
- Adds `Add to Dictionary`.
- Then includes the standard text edit context menu.

Popup safety:

- The input context menu is created with `WA_DeleteOnClose`.
- The key redirect guard must not steal keystrokes while a popup/menu/modal dialog is active.

## Global Key Redirect

Owner: `MainWindow::redirectKeyToInput()` and `MainWindow::eventFilter()`

Purpose:

- HexChat-style typing: when the chat view, nick list, topic bar, or other non-entry widget has focus, printable typing goes to `messageInput`.

Do not break these invariants:

- If a `QMenu`, combo popup, or modal dialog is active, do nothing.
- If focus is already in a writable text field, do nothing.
- If focus is an interactive button, slider, or tab bar, do nothing.
- Ctrl/Alt/Meta shortcuts pass through, except printable AltGr characters.
- Only printable text redirects.
- The event filter must be installed through `qApp->installEventFilter(this)`, not only per widget.

Menu focus restore:

- When a direct child dropdown of the main menu bar hides, focus returns to `messageInput` after Qt finishes popup teardown.
- This is intentionally not applied to context menus or submenus.

WSLg/Linux note:

- Native menu/popup grabbing can produce messages such as `This plugin supports grabbing the mouse only for popup windows`.
- The current code guards key redirect around active popups and restores focus after menu-bar dropdowns close. Avoid broad popup-closing changes; test both Linux/WSLg and Windows if this code changes.

## Tray Icon

Owner: `MainWindow::ensureTrayIcon()`

Behavior:

- Uses `QSystemTrayIcon` when available.
- Tray context menu is a `QMenu`.

Tray menu:

- `Show / Hide`
- `Do Not Disturb` when the main DND action exists.
- `Quit`

Tray activation:

- Trigger toggles window visibility.
- Notification click raises and activates the main window.

## Toast Notifications

Owner: `src/ui/ToastWidget.cpp`

Window flags:

- `Qt::FramelessWindowHint`
- `Qt::Tool`
- `Qt::WindowStaysOnTopHint`

Attributes:

- `WA_TranslucentBackground`
- `WA_ShowWithoutActivating`
- `WA_DeleteOnClose`

Layout:

- Fixed width: 340 px.
- Optional 28x28 icon.
- Bold title, slightly larger than body.
- Word-wrapped body.
- Click closes toast and runs the callback.
- Auto-closes after at least 1000 ms.

Shadow:

- Only explicit drop shadow currently found in the GUI.
- Applied to `toastCard` with `QGraphicsDropShadowEffect`.
- Blur radius: 26.
- Color: black alpha 170.
- Offset: `(0, 3)`.

Important:

- Dialogs and menus do not have custom shadows; they rely on platform/window-manager shadows.

## Preferences Dialog

Owner: `src/ui/PreferencesDialog.cpp`

Title: `Preferences`

Default size: `1000 x 680`

Layout:

- Left-side navigation list: `preferencesButtons`
- Stacked page area: `preferencesPages`
- Bottom `QDialogButtonBox`: OK/Cancel

Navigation is vertical on the left. Do not convert it back to top tabs.

Page order:

1. `Appearance`
2. `Layout`
3. `Messages`
4. `Notifications`
5. `Protection`
6. `Files (DCC)`
7. `Themes`
8. `Fonts`
9. `Spelling`
10. `Localization`
11. `Comic`
12. `Scripts`
13. `CTCP/Services`
14. `Image Hosting`
15. `Data`

Cancel behavior:

- Theme/chat/wallpaper live previews are restored on cancel.
- Accepted settings are merged back into the settings store.

### Preferences: Appearance

Controls:

- `showTimestamps`
- `timestampFormat`
- `nickColorMode`
- `alignNicks`
- `nickWidth`
- `separatorLine`
- `showFormatting`
- `wordWrap`
- `indentWrap`
- `stripColorCopy`
- `trayIcon`
- `buttonBarVisible`
- `inputHint`
- `markerLine`
- `sortByStatus`

### Preferences: Layout

Controls:

- `serverListVisible`
- `memberListVisible`
- `buttonsAsTabs`
- `connectOnStart`

### Preferences: Messages

Controls:

- `hideJoinPart`
- `showMode`
- `pmEcho`
- `loggingEnabled`
- `logMask`
- `replayLogEnabled`
- `replayLines`
- `autoReconnect`
- `autoRejoin`
- `confirmQuit`
- `scrollback`

### Preferences: Notifications

Controls:

- Do Not Disturb checkbox.
- `notifyPopup`
- `notifyPm`
- `notifyHighlight`
- `notifyFlash`
- `notifyCorner`
- `notifyDuration`
- `notifyTheme`
- `beepHighlight`
- `notifySound`
- `notifySoundFile`
- Sound directory/open buttons.
- `ctcpSound`
- `minimizeToTray`
- `updateCheck`
- `Test notification`

Known UX note:

- Test notification may do nothing if the main window is active or DND is enabled. If users report confusion, show a small inline explanation or allow explicit test override.

### Preferences: Protection

Controls:

- `floodProtect`
- `floodMessages`
- `floodSeconds`
- `pasteGuard`
- `pasteLines`
- `inviteProtect`
- `ignoreInvites`

### Preferences: Files (DCC)

Controls:

- `dccEnabled`
- `dccAccept`
- `dccTrusted`
- `dccPassive`
- `dccIp`
- `dccPortFirst`
- `dccPortLast`
- `dccDir`
- Browse button.

DCC UI is present, but DCC transfers remain a later implementation area unless explicitly marked complete elsewhere.

### Preferences: Themes

Controls:

- App theme combo: `theme`
- Active app theme label: `activeAppTheme`
- App theme preview: `appThemePreview`
- Buttons: `themeDefault`, `themeOff`, `customizeAppTheme`, `deleteAppTheme`
- Chat theme combo: `chatTheme`
- Active chat theme label: `activeChatTheme`
- Chat theme preview: `chatThemePreview`
- Buttons: `customizeChatTheme`, `deleteChatTheme`
- Chat opacity slider: `chatOpacity`
- Wallpaper combo: `wallpaper`
- File/action buttons: `saveTheme`, `importTheme`, `exportTheme`, `openThemesFolder`

Important behavior:

- App theme, chat theme, opacity, and wallpaper preview live while the dialog is open.
- Cancel restores the prior live appearance.
- OK persists the changes.

Known active bug:

- GUI-focused tests currently fail `MainWindowLinkPreviewTest::themeMenuChangesSavedThemeAndApplicationStyle` because `Themes Off` leaves chrome font CSS in `qApp->styleSheet()`. Either update the test expectation to allow chrome font CSS, or change `applyTheme()` so Themes Off truly leaves no app stylesheet.

### Preferences: Fonts

Top buttons:

- `Set All to JetBrains Mono`
- `System Default`

Font groups:

- App font.
- Chat font.
- List font.
- Nick font.
- Status font.
- Topic font.
- Terminal font: `terminalFontFamily`, `terminalFontSize`, `terminalFontBold`, `terminalGrid`

Font expectations:

- JetBrains Mono and bold are intended defaults.
- System Default should not break chat line alignment.
- Chat alignment depends on correct font metrics and on the movable separator, not hard-coded pipe characters.

### Preferences: Spelling

Controls:

- `spellcheckEnabled`
- `spellBackend`
- `spellLanguage`
- `autocorrectEnabled`
- `autocorrectDistance`

Bundled dictionary languages:

- `da_DK`
- `de_DE`
- `en_GB`
- `en_US`
- `es_ES`
- `fr_FR`
- `it_IT`
- `nb_NO`
- `nl_NL`
- `pl_PL`
- `pt_BR`
- `pt_PT`
- `ru_RU`
- `sv_SE`
- `tr_TR`
- `uk_UA`

### Preferences: Localization

Controls:

- `interfaceLanguage`

Current state:

- Localization tab exists.
- Full UI translation is prepared structurally but not complete.
- Do not add fake finished translations. Placeholder languages should be clearly labeled if no translation exists.

### Preferences: Comic

Controls:

- `openComicSettings`
- `browseCharacters`
- Loaded character list.

### Preferences: Scripts

Controls:

- Script list.
- Buttons: `Load`, `Unload`, `Reload`, `Edit`, `Open Folder`
- Permission toggles: read files, write files, run programs, load modules, network.
- Allowed-folder buttons: `Add folder`, `Add drive/path`, `Remove`

### Preferences: CTCP/Services

Controls:

- `hideVersion`
- `ctcpVersion`
- `ctcpRespondPing`
- `ctcpRespondTime`
- `ctcpRespondClientInfo`
- Link preview toggles: `linkImages`, `linkMedia`, `linkXCards`, `linkWebCards`
- OpenGraph card toggles: `ogShowSiteName`, `ogShowTitle`, `ogShowDescription`, `ogShowImage`

### Preferences: Image Hosting

Controls:

- `uploadService`
- Provider terms/info boxes.
- `imgbbKey`
- `imgurClientId`
- `postimagesToken`
- `imgboxUsername`
- `imgboxPassword`

### Preferences: Data

Controls:

- Directory open buttons.
- `exportSettings`
- `importSettings`
- `resetServerList`
- `resetAllSettings`

## Server List Dialog

Owner: `src/ui/ServerListDialog.cpp`

Title: `Server List`

Main controls:

- Count label, for example `100 networks`.
- Network list, minimum size `680 x 420`.
- Buttons: `Add...`, `Edit...`, `Delete`, `Up`, `Down`, `Reset Defaults`
- Checkbox: `Auto-connect on startup`
- Bottom buttons: `Connect`, dynamic homepage button, OK, Cancel.

List label format:

- Network/group name.
- Primary server/IP after the network name.
- Homepage URL/domain after the server when available.

Selection behavior:

- Homepage button text changes with the selected network.
- Homepage button is disabled when no homepage is available.
- Edit/Delete/Up/Down/Connect enable according to selection and row position.
- Double-clicking a network connects.

Cancel behavior:

- Edits inside the dialog should not persist if the dialog is canceled.

Known GUI/security polish issue:

- `openHomepage()` passes the stored website directly to `QDesktopServices::openUrl(QUrl(website))`. The stored URL should include `http://` or `https://`; consider validating scheme before opening.

## Network Edit Dialog

Owner: internal dialog in `ServerListDialog.cpp`

Title:

- `Add Network`
- `Edit Network`

Default size: `620 x 720`

Fields:

- Network/group name.
- Servers list/edit field.
- Port.
- SSL/TLS.
- Accept unsigned certificate.
- Homepage.
- Startup/autoconnect controls.
- Nick/password/nickserv/channel join fields as defined by network config.

Rules:

- The primary server for a network must be first.
- Backup/failover servers stay in the same group.
- Network homepage belongs to the group, not individual server rows.

## Quick Connect Dialog

Owner: `src/ui/QuickConnectDialog.cpp`

Title: `Quick Connect`

Default size: `420 x 220`

Object names:

- `host`
- `port`
- `tls`
- `nick`
- `channels`

Expected controls:

- Host/server.
- Port.
- SSL/TLS toggle.
- Nick.
- Channels.
- OK/Cancel.

## Channel List Dialog

Owner: `src/ui/ChannelListDialog.cpp`

Title: `Channel List`

Default size: `820 x 560`

Controls:

- `get_list_button`
- `join_button`
- `copy_button`
- `close_button`
- `channel_filter`
- `min_users`
- `channel_table`

Behavior:

- `Get List` sends LIST.
- Filtering and minimum users should not destroy current sort state.
- Join button joins selected channel.

## Channel Modes Dialog

Owner: `src/ui/ChannelModesDialog.cpp`

Title: `Channel Modes - <channel>`

Controls:

- Mode checkboxes named `mode_*`.
- `key_enabled`
- `key_edit`
- Key set button.
- `limit_enabled`
- `limit_spin`
- Limit set button.
- Close.

## Ban List Dialog

Owner: `src/ui/BanListDialog.cpp`

Title: `Ban List - <channel>`

Controls:

- `ban_table`
- `ban_entry`
- `ban_add`
- `ban_remove`
- `ban_status`
- Close.

Behavior:

- Opens as a nonmodal singleton.
- Requests `MODE +b` when opened.

## Chat Find Dialog

Owner: `src/ui/ChatFindDialog.cpp`

Title: `Find in Chat`

Default size: `420 x 130`

Controls:

- `searchInput`
- `caseSensitive`
- `wrapSearch`
- Previous.
- Next.
- Close.

Behavior:

- Opens as a nonmodal singleton.
- Reopening raises/activates the existing dialog.

## Raw Log Dialog

Owner: `src/ui/RawLogDialog.cpp`

Title: `Raw Log`

Default size: `780 x 520`

Controls:

- `rawLogView`
- Clear.
- Copy.
- Close.

Behavior:

- Opens as a nonmodal singleton.
- Reopening raises/activates the existing dialog.

## URL List Dialog

Owner: `src/ui/UrlListDialog.cpp`

Title: `URL List`

Default size: `700 x 460`

Behavior:

- Opens as a nonmodal singleton.
- Reopening raises/activates the existing dialog.

## Alias Editor Dialog

Owner: `src/ui/AliasEditorDialog.cpp`

Title: `Command Aliases`

Default size: `620 x 420`

Object names:

- `alias_table`
- `alias_name`
- `alias_template`
- `alias_add_update`
- `alias_remove`
- `alias_restore_defaults`
- `alias_buttons`

## Ignore List Dialog

Owner: `src/ui/IgnoreListDialog.cpp`

Title: `Ignore List`

Expected role:

- Manage ignore masks/rules.
- Opens from Settings menu and member context menu integration.

## Friends / Notify Dialog

Owner: `src/ui/FriendsNotifyDialog.cpp`

Title: `Friends / Notify`

Expected role:

- Manage notify/friend nicks.
- Integrates with member context menu `Add Notify` / `Remove Notify`.

## Shortcut Editor Dialog

Owner: `src/ui/ShortcutEditorDialog.cpp`

Title: `Keyboard Shortcuts`

Default size: `420 x 460`

Expected role:

- Shows editable or planned shortcut mappings.
- Opens from Settings menu.

## Theme Editor Dialog

Owner: `src/ui/ThemeEditorDialog.cpp`

Titles:

- `Customize App Theme`
- `Customize Chat Theme`

Buttons:

- Save.
- Cancel.

Expected role:

- Edit and save theme JSON-compatible colors.
- Should never make theme JSON invalid.

## DCC Transfers Dialog

Owner: `src/ui/DccTransfersDialog.cpp`

Title: `File Transfers (DCC)`

Default size: `560 x 320`

Current state:

- GUI entry exists.
- Full transfers are a later implementation area unless separately completed.

## Image Viewer Dialog

Owner: `src/ui/ImageViewerDialog.cpp`

Title:

- URL filename when available, otherwise full URL.

Default size:

- Starts at `640 x 480`.
- Resizes to loaded image, capped around 85% of available screen.

Controls:

- Scroll area.
- Image label, initially `Loading image...`.
- Escape closes.

Security/behavior:

- Uses `ImageFetcher`, not raw network fetch.
- Enforces allowed preview URL checks.
- Enforces max image bytes: 25 MB.
- Enforces timeout: 15 seconds.
- Allows full viewer dimensions up to 8192 x 8192.

## Media Player Dialog

Owner: `src/ui/MediaPlayerDialog.cpp`

Title:

- URL filename when available, otherwise full URL.
- Changes to `Playback error: <reason>` on playback error.

Default size: `720 x 460`

Controls:

- `QVideoWidget`
- `videoPlayPause`
- Seek slider.
- Escape closes.

Behavior:

- Starts playing immediately.
- Play/Pause button stays synchronized with actual playback state.

## Audio Player Bar

Owner: `src/ui/AudioPlayerBar.cpp`

Object names:

- `audioPlayerBar`
- `audioPlayPause`
- `audioName`
- `audioSeek`
- `audioTime`
- `audioClose`

Layout:

- Horizontal bar under chat transcript.
- Play/Pause.
- Plain-text filename/URL label.
- Seek slider.
- Time label.
- Close button, text `X`.

Security note:

- Filename label uses `Qt::PlainText` because filenames are remote/URL-derived text.

## Script Terminal Window

Owner: `src/ui/ScriptTerminalDialog.cpp`

Components:

- Menu bar.
- `terminalStatus`
- `scriptTerminalDisplay`
- `terminalPrompt`
- `terminalInput`

File menu:

- `Close`, hides the terminal window.
- `Kill Terminal`, emits kill request.

Settings menu:

- `Font`
- `Font Size`
- `Terminal Size`

Font menu:

- `JetBrains Mono`
- `Cascadia Mono`
- `Consolas`
- `Courier New`
- `DejaVu Sans Mono`
- `Choose...`, opens `QFontDialog` titled `Terminal Font`.

Font Size menu:

- `8 pt`
- `10 pt`
- `12 pt`
- `14 pt`
- `16 pt`
- `18 pt`
- `20 pt`
- `24 pt`

Terminal Size menu:

- `80 x 25`
- `80 x 40`

Terminal notes:

- Terminal fonts default through settings and should prefer JetBrains Mono unless user overrides.
- The terminal supports CP437/box-drawing-style Unicode characters through normal text rendering:
  `┌┬┐ │ ├┼┤ └┴┘ ═ ║ ╔╗ ╚╝ ╬ ░▒▓ █ ▄▀▌▐ ■ ● ○ ◆ ◇ ▲ ▼ ◄ ►`

## Comic View Context Menu

Owner: `src/ui/ComicView.cpp`

Shown only when there are comic panels.

Panel-specific actions when right-clicking a panel:

- `Copy this panel`
- `Save this panel as PNG...`

General actions:

- `Copy comic`
- `Save comic as PNG...`
- `Save all panels...` when multiple panels exist.

## Comic Settings Dialog

Owner: `src/ui/ComicSettingsDialog.cpp`

Title: `Comic Settings`

Default size: `640 x 560`

Expected role:

- Configure comic mode, characters, character names, and rendering behavior.
- Opens from Comic menu and Preferences > Comic.

## Color Picker Dialog

Owner: `src/ui/ColorPickerDialog.cpp`

Title: `Insert Color`

Behavior:

- Opens from `Ctrl+K` in the message input.
- Provides mIRC color swatches/buttons.
- Cancel closes without insertion.

## Themes

Owner: `src/ui/ThemeCatalog.cpp`

App theme file:

- `assets/themes/app-themes.json`

Chat theme file:

- `assets/themes/chat-themes.json`

Wallpaper directory:

- `assets/wallpapers/`

App theme JSON fields:

- `id`
- `label`
- `system`
- `dark`
- `bg`
- `panel`
- `panel2`
- `text`
- `on`
- `on_text`
- `accent`
- `groove`
- `scroll`
- `scroll_hi`
- `chat_bg`
- `chat_fg`
- `wallpaper`
- `bg_gradient`
- `fonts`

Chat theme JSON fields:

- `id`
- `label`
- `bg`
- `fg`
- `fixed`
- `ts`
- `bracket`
- `system`
- `nicks`

### How To Make Themes

There are three theme formats to know:

- Bundled app themes: stored in `assets/themes/app-themes.json`.
- Bundled chat themes: stored in `assets/themes/chat-themes.json`.
- User themes: stored in the user's config directory and loaded in addition to bundled themes.

Use bundled themes for shipped defaults. Use user themes for custom local themes and imported/exported themes.

User theme locations:

- User app themes: `<config>/maxchat/themes/*.json`
- User chat themes: `<config>/maxchat/chat_themes.json`
- On Linux this is normally under `~/.config/maxchat/`.
- On Windows this is normally under the user's Qt generic config location.

The application creates a copyable `_example.json` starter file in the user app theme directory. Files beginning with `_` are ignored, so users can keep templates there safely.

#### App Theme JSON

App themes control the main chrome: window background, menus, toolbar, buttons, panels, scrollbars, list backgrounds, topic bar, splitters, and default chat colors.

Required app theme fields:

- `id`: unique lowercase ID for bundled themes. User theme file stems become IDs if omitted.
- `label`: display name. User themes may also use `name`.
- `dark`: boolean used for theme metadata and contrast expectations.
- `bg`: window background color.
- `panel`: main panel/input/list background color.
- `panel2`: menu bar, toolbar, status bar, and raised panel color.
- `text`: normal text color.
- `on`: selection/highlight color.
- `accent`: accent color.
- `groove`: darker structural/border color.
- `scroll`: scrollbar track/thumb base color.
- `scroll_hi`: scrollbar hover color.

Optional app theme fields:

- `on_text`: text color shown on top of `on`; usually `white` or `black`.
- `chat_bg`: default chat background when the chat theme follows the app theme.
- `chat_fg`: default chat foreground when the chat theme follows the app theme.
- `wallpaper`: bundled wallpaper filename or wallpaper value.
- `bg_gradient`: list of gradient stops.
- `fonts`: whitelisted bundled font preferences.
- `system`: true only for the special `Themes Off` entry.

Color format:

- Use RGB arrays: `[red, green, blue]`
- Values are `0` to `255`.
- Alpha is not stored in theme JSON; runtime transparency comes from wallpaper/opacity settings.

Minimal bundled app theme:

```json
{
  "id": "my-theme",
  "label": "My Theme",
  "dark": true,
  "bg": [18, 20, 24],
  "panel": [28, 31, 38],
  "panel2": [38, 42, 52],
  "text": [232, 235, 240],
  "on": [40, 120, 80],
  "on_text": "white",
  "accent": [80, 230, 150],
  "groove": [12, 14, 18],
  "scroll": [58, 64, 76],
  "scroll_hi": [86, 96, 112],
  "chat_bg": [12, 14, 18],
  "chat_fg": [220, 224, 230]
}
```

User app theme file example:

```json
{
  "name": "My Local Theme",
  "dark": true,
  "on_text": "white",
  "bg": [40, 42, 54],
  "panel": [52, 55, 70],
  "panel2": [68, 71, 90],
  "text": [248, 248, 242],
  "on": [98, 114, 164],
  "accent": [80, 250, 123],
  "groove": [33, 34, 44],
  "scroll": [68, 71, 90],
  "scroll_hi": [98, 114, 164]
}
```

App theme font bundle example:

```json
{
  "id": "mono-launch",
  "label": "Mono Launch",
  "dark": true,
  "bg": [20, 20, 22],
  "panel": [30, 30, 34],
  "panel2": [42, 42, 48],
  "text": [235, 235, 238],
  "on": [30, 110, 70],
  "on_text": "white",
  "accent": [80, 230, 150],
  "groove": [14, 14, 18],
  "scroll": [62, 62, 70],
  "scroll_hi": [90, 90, 104],
  "fonts": {
    "app_font_family": "JetBrains Mono",
    "app_font_size": 10,
    "app_font_bold": true,
    "chat_font_family": "JetBrains Mono",
    "chat_font_size": 10,
    "chat_font_bold": true
  }
}
```

Allowed `fonts` keys:

- `app_font_family`, `app_font_size`, `app_font_bold`
- `chat_font_family`, `chat_font_size`, `chat_font_bold`
- `list_font_family`, `list_font_size`, `list_font_bold`
- `nick_font_family`, `nick_font_size`, `nick_font_bold`
- `status_font_family`, `status_font_size`, `status_font_bold`
- `topic_font_family`, `topic_font_size`, `topic_font_bold`

Font keys are whitelisted intentionally. Imported themes cannot inject arbitrary settings through the `fonts` object.

#### Chat Theme JSON

Chat themes control transcript colors and IRC line formatting accents. They do not style the whole application.

Required chat theme fields:

- `id`: unique lowercase ID for bundled themes.
- `label`: display name.
- `bg`: chat background color.
- `fg`: chat foreground color.

Optional chat theme fields:

- `fixed`: true if the chat theme expects a monospace/fixed font.
- `ts`: timestamp color.
- `bracket`: nick bracket/separator color.
- `system`: system/server line color.
- `nicks`: either `"mono"` or an array of RGB colors used as a nick palette.

Special chat theme:

- `id: "follow"` means the chat area follows the active app theme.

Minimal chat theme:

```json
{
  "id": "my-chat",
  "label": "My Chat",
  "bg": [8, 10, 12],
  "fg": [220, 224, 230],
  "fixed": true,
  "ts": [120, 132, 150],
  "bracket": [90, 100, 120],
  "system": [120, 160, 220],
  "nicks": [
    [80, 180, 255],
    [120, 220, 120],
    [255, 200, 90],
    [255, 120, 160]
  ]
}
```

Mono-nick chat theme:

```json
{
  "id": "plain-mono",
  "label": "Plain Mono",
  "bg": [0, 0, 0],
  "fg": [192, 192, 192],
  "fixed": true,
  "ts": [95, 95, 110],
  "bracket": [120, 120, 120],
  "system": [108, 132, 168],
  "nicks": "mono"
}
```

#### Theme Pack JSON

Theme packs are shareable files exported from Preferences. A pack may contain an app theme, a chat theme, font preferences inside the app theme, and a wallpaper choice.

Theme pack shape:

```json
{
  "kind": "maxchat-theme-pack",
  "version": 1,
  "name": "My Complete Look",
  "app": {
    "name": "My Complete Look",
    "dark": true,
    "on_text": "white",
    "bg": [18, 20, 24],
    "panel": [28, 31, 38],
    "panel2": [38, 42, 52],
    "text": [232, 235, 240],
    "on": [40, 120, 80],
    "accent": [80, 230, 150],
    "groove": [12, 14, 18],
    "scroll": [58, 64, 76],
    "scroll_hi": [86, 96, 112]
  },
  "chat": {
    "label": "My Complete Look",
    "bg": [8, 10, 12],
    "fg": [220, 224, 230],
    "fixed": true
  },
  "wallpaper": "synthwave.png"
}
```

Import behavior:

- Theme packs are identified by `kind: "maxchat-theme-pack"`.
- Bare single-theme JSON files are also accepted.
- If a bare file contains `panel` or `kind: "app"`, import tries it as an app theme first.
- Otherwise import tries chat theme first, then app theme.
- Imported app and chat themes are saved as user themes with IDs like `u-my-complete-look`.

#### Gradients And Wallpapers

Wallpaper values:

- `Theme Default`: use the active app theme's default wallpaper.
- `Off`: disable wallpaper.
- Bundled wallpaper filename: for example `synthwave.png`.
- User-loaded absolute image path.

Wallpaper safety:

- Wallpaper paths containing quotes or newlines are ignored before QSS is built.
- `Themes Off` always disables wallpaper.

Gradient example:

```json
{
  "id": "gradient-test",
  "label": "Gradient Test",
  "dark": true,
  "bg": [20, 20, 28],
  "panel": [30, 30, 42],
  "panel2": [42, 42, 58],
  "text": [235, 235, 245],
  "on": [90, 120, 220],
  "on_text": "white",
  "accent": [120, 220, 255],
  "groove": [16, 16, 24],
  "scroll": [60, 60, 80],
  "scroll_hi": [90, 90, 120],
  "bg_gradient": [
    {"stop": 0.0, "color": [20, 20, 32]},
    {"stop": 1.0, "color": [48, 24, 70]}
  ]
}
```

#### Theme Validation Rules

An app theme is skipped if any required core color is missing or invalid:

- `bg`
- `panel`
- `panel2`
- `text`
- `on`
- `accent`
- `groove`
- `scroll`
- `scroll_hi`

A chat theme is skipped if either required chat color is missing or invalid:

- `bg`
- `fg`

Bad user theme files are skipped, never fatal. Startup should not fail because of a broken custom theme.

#### Theme Creation Workflow

Recommended workflow for a new shipped theme:

1. Add or copy an entry in `assets/themes/app-themes.json`.
2. Add a matching chat theme in `assets/themes/chat-themes.json` only if the transcript needs custom colors.
3. Put any new wallpaper in `assets/wallpapers/`.
4. Keep the wallpaper filename simple: no quotes, newlines, or odd path characters.
5. Build and run the theme catalog tests.
6. Open Preferences > Themes and verify the preview.
7. Verify the main menu, toolbar, sidebars, member list, chat input, status bar, splitters, scrollbars, dialogs, and message boxes.
8. Check with and without wallpaper.
9. Check chat opacity at 100%, around 80%, and around 50%.
10. Check both `Follow app theme` and a fixed chat theme.
11. Check Windows and Linux/WSLg because popup/menu rendering differs.
12. Update this document and screenshots if the theme is user-facing.

Recommended workflow for a user/custom theme:

1. Open Preferences > Themes > `Open Themes Folder`.
2. Copy `_example.json` to a new filename without a leading underscore.
3. Edit the colors.
4. Restart the app or reload themes through the theme UI after saving/importing.
5. Select the new theme from Preferences > Themes.

Theme test command:

```bash
env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R "(theme_catalog|preferences_dialog|main_window_link_preview)"
```

Bundled app theme labels:

- `Default`
- `Themes Off`
- `Light`
- `Normal`
- `Normal Dark`
- `Console`
- `Dracula`
- `Nord`
- `Solarized Dark`
- `Solarized Light`
- `Gruvbox Dark`
- `Cyberpunk`
- `Monokai Pro`
- `Tokyo Night`
- `Ubuntu`
- `Gruvbox Material`
- `Synthwave`
- `Vaporwave`
- `Classic (terminal)`
- `Classic Green`
- `Amber`
- `Sakura`
- `Deep Ocean`
- `Ember`
- `High Contrast`
- `Matrix`
- `Catppuccin Mocha`
- `One Dark`
- `Pure Black`
- `Rose Pine`
- `Everforest`
- `Windows 95`
- `Windows XP`
- `Windows 11`
- `Windows 11 Dark`
- `Aqua`

Bundled chat theme labels:

- `Follow app theme`
- `Terminal - grey on black`
- `Terminal - green on black`
- `Terminal - amber on black`
- `irssi - clean (mono nicks)`
- `BitchX - bright & loud`
- `White - proportional`
- `Paper - sepia`
- `Midnight - blue hours`
- `Matrix - green rain`
- `Dracula`
- `Nord`
- `Gruvbox`
- `One Dark`
- `Solarized - dark`
- `Pure Black - high contrast`
- `Old School - black on white`
- `Cyber - neon on void`
- `Rose Pine`
- `Windows 95 - grey & navy`
- `Windows XP - Luna`
- `Windows 11 - light`
- `Windows 11 - dark`
- `Aqua - glossy`
- `Latte - warm light`
- `Forest - mossy dark`
- `Abyss - deep teal`
- `Sunset - coral on plum`
- `Slate - cool grey`
- `Espresso - coffee & cream`

Theme fallback behavior:

- Missing/invalid app themes fall back to `Default` and `Themes Off`.
- Missing/invalid chat themes fall back to `Follow app theme` and `Terminal - grey on black`.
- Theme IDs are normalized before lookup.

Wallpaper behavior:

- Choices include `Theme Default`, `Off`, bundled wallpaper files, and user-loaded images.
- `Themes Off` disables wallpaper.
- `Off` disables wallpaper.
- Wallpaper paths are rejected if they contain quote/newline characters before being interpolated into QSS.

Transparency behavior:

- If a wallpaper is active, panel colors use alpha around 0.58.
- If a wallpaper is active and chat follows the app theme, chat background uses alpha around 0.80.
- User-selected chat opacity wins over theme defaults and clamps from 20% to 100%.

QSS surfaces styled by app themes:

- `QMainWindow`
- Base `QWidget`
- `centralRoot`, `chatColumn`, `memberPanel`
- `QSplitter`
- `QMenuBar`
- `QMenu`
- `QStatusBar`
- `QToolBar#mainToolbar`
- `QToolButton`
- `QPushButton`
- `QTabBar#bufferTabBar`
- `QTreeWidget`
- `QListWidget`
- `QTextBrowser`
- `QTextEdit`
- `QLineEdit`
- `QPlainTextEdit`
- `QTableWidget`
- `QComboBox`
- `QSpinBox`
- `QTextBrowser#chatView`
- `QTextEdit#messageInput`
- `QTreeWidget#networkTree`
- `QListWidget#memberList`
- `QListWidget#preferencesButtons`
- `QLabel#topicLabel`
- `QGroupBox`
- `QHeaderView::section`
- `QScrollBar`
- `QDialogButtonBox QPushButton`

Spinbox warning:

- Styling `QSpinBox` with borders switches it into stylesheet style. The up/down buttons must have explicit geometry or click areas break. The current QSS deliberately styles button geometry but does not override arrow glyphs.

## Fonts

Owner: `MainWindow::loadBundledFonts()`, `MainWindow::applyFontSettings()`, `MainWindow::applyTheme()`

Bundled font loading:

- JetBrains Mono Regular/Bold.
- Comic Relief Regular/Bold.
- Symbols Nerd Font Mono.

Font application:

- Application font applies globally.
- Chat font applies to transcript and input.
- List font applies to network tree and member list.
- Status font applies to status bar.
- Topic font applies to topic label.
- Menu bar font is explicitly set because palette/style repolish can reset it.
- Popup menu and toolbar font are reinforced through QSS.

Alignment warning:

- Changing fonts can affect chat line alignment. Tests should cover replayed logs, wrapped messages, live messages, MOTD/server lines, actions, notices, private messages, and resizing.

## Shadows

Explicit custom shadows:

- Toast notifications only: `ToastWidget` uses `QGraphicsDropShadowEffect`.

Platform/native shadows:

- Menus.
- Dialogs.
- Message boxes.
- Combo popups.
- Tray menus.

Testing note:

- About/help/menu shadow behavior can differ between Windows, Linux, WSLg, Wayland, X11, and offscreen Qt. Do not assume native shadows will match screenshots.

## Modal vs Nonmodal Window Policy

Modal or dialog-owned flows:

- Server list.
- Quick connect.
- Preferences.
- Alias editor.
- Ignore list.
- Friends / Notify.
- Channel modes.
- Message boxes.
- Network editor.
- Color picker.
- Theme editor.

Nonmodal singleton flows:

- Ban list.
- Channel list.
- Chat find.
- Raw log.
- URL list.

Delete-on-close popups/windows:

- Image viewer.
- Media player.
- Toasts.
- Input context menu.

Rule:

- If a nonmodal singleton already exists, raise/activate it rather than creating duplicates.

## Geometry Persistence

Owner: `GeometryPersist.*` and each opener in `MainWindow.cpp`

Known keys:

- `geom_server_list`
- `geom_quick_connect`
- `geom_preferences`
- `geom_aliases`
- `geom_ignore_list`
- `geom_friends_notify`
- `geom_channel_modes`
- `geom_ban_list`
- `geom_channel_list`
- `geom_chat_find`
- `geom_raw_log`
- `geom_url_list`

Rule:

- Any large or frequently reused dialog should get a stable geometry key.

## GUI Tests

Run focused GUI tests headlessly:

```bash
env QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure -R "(main_window_link_preview|preferences_dialog|server_list_dialog|theme_catalog|quick_connect_dialog|channel_list_dialog|channel_modes_dialog|ban_list_dialog|chat_find_dialog|raw_log_dialog|url_list_dialog|alias_editor_dialog|ignore_list_dialog|friends_notify_dialog|terminal_frame|terminal_profile|script_terminal_manager)"
```

Current focused GUI test result from 2026-06-15:

- 16 of 17 focused GUI tests passed.
- Failing test: `main_window_link_preview`
- Failure: `MainWindowLinkPreviewTest::themeMenuChangesSavedThemeAndApplicationStyle`
- Cause: test expects `qApp->styleSheet().isEmpty()` after selecting `Themes Off`, but `applyTheme()` still leaves chrome font CSS installed.

GUI-related test files:

- `tests/unit/main_window_link_preview_test.cpp`
- `tests/unit/preferences_dialog_test.cpp`
- `tests/unit/server_list_dialog_test.cpp`
- `tests/unit/quick_connect_dialog_test.cpp`
- `tests/unit/channel_list_dialog_test.cpp`
- `tests/unit/channel_modes_dialog_test.cpp`
- `tests/unit/ban_list_dialog_test.cpp`
- `tests/unit/chat_find_dialog_test.cpp`
- `tests/unit/raw_log_dialog_test.cpp`
- `tests/unit/url_list_dialog_test.cpp`
- `tests/unit/alias_editor_dialog_test.cpp`
- `tests/unit/ignore_list_dialog_test.cpp`
- `tests/unit/friends_notify_dialog_test.cpp`
- `tests/unit/theme_catalog_test.cpp`
- `tests/unit/script_terminal_manager_test.cpp`
- `tests/unit/terminal_frame_test.cpp`
- `tests/unit/terminal_profile_test.cpp`
- `tests/unit/spellcheck_highlighter_test.cpp`
- `tests/unit/spellcheck_dictionary_catalog_test.cpp`
- `tests/unit/sound_player_test.cpp`

## Resolved GUI Issues

1. `Themes Off` test mismatch. **FIXED 2026-06-15.**
   - File: `src/ui/MainWindow.cpp`
   - `applyTheme()` for Themes Off now sets an empty stylesheet and relies on
     `qApp->setFont()` instead of chrome font CSS — no re-polish conflict.
   - Test `themeMenuChangesSavedThemeAndApplicationStyle` now passes.

2. Server menu global labels did not match handlers. **FIXED 2026-06-15.**
   - File: `src/ui/MainWindow.cpp`
   - `Disconnect All` → `Disconnect`, `Reconnect All` → `Reconnect`.

3. Startup placeholder was stale. **FIXED 2026-06-15.**
   - File: `src/ui/MainWindow.cpp`
   - Replaced skeleton text with a concise connect hint.

4. View menu had duplicate separators. **FIXED 2026-06-15.**
   - File: `src/ui/MainWindow.cpp`
   - Removed one `addSeparator()` call.

5. Server list homepage opening did not validate scheme. **FIXED 2026-06-15.**
   - File: `src/ui/ServerListDialog.cpp`
   - `openHomepage()` now prepends `https://` for schemeless URLs and rejects
     non-http/https schemes.

6. Operator submenu had a trailing separator. **FIXED 2026-06-15.**
   - File: `src/ui/MainWindow.cpp`
   - Separators are now added between mode pairs (before, gated on first) instead
     of after every pair.

## Known GUI Issues

1. Test notification can appear broken.
   - File: `src/ui/MainWindow.cpp`
   - If main window is active or DND is on, the test path may not show a toast.

2. Native shadows are not consistent across platforms.
   - Only toasts use explicit drop shadows.
   - Menus/dialogs depend on OS/window manager.

## Update Checklist For Future GUI Work

When adding or changing a window, popup, button, theme, or menu item:

- Update this `GUI.MD`.
- Add or update object names for important controls.
- Add or update a focused GUI test.
- Verify both Windows and Linux/WSLg for menus and popups.
- Verify `QT_QPA_PLATFORM=offscreen` tests still pass or document the known failure.
- Check that menu labels match actual handler behavior.
- Check that Preferences page order still matches this document.
- Check that toolbar buttons and menu actions expose the same major feature paths.
- Check that nonmodal singleton dialogs raise existing windows instead of duplicating.
- Check that Cancel restores live-preview changes.
- Check that theme/wallpaper transparency still works.
- Check that `Themes Off` does what the tests and UI promise.
- Check that long text wraps or elides intentionally.
- Check that all user-controlled labels use plain text unless rich text is explicitly required.
- Check chat alignment after changing fonts, themes, or transcript rendering.
- Check that no user-facing label says `Buffers as Tabs`; it should say `Buttons as Tabs`.
