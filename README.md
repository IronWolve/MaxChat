# MaxChat

> A comic-strip-style graphical IRC client.

MaxChat is the native C++/Qt rewrite of the original Python/Qt MaxChat prototype: a real
desktop **IRC client** with an optional **comic mode**. Flip comic mode on and the conversation
is drawn as a comic strip — each message becomes a panel with an expressive character,
a chosen emotion, a speech (or thought) balloon, and a scene backdrop. Flip it off and it's a clean,
modern IRC client.

> **Unofficial** and not affiliated with, authorized by, or endorsed by any other chat program or its
> rights holders. It ships **no third-party character art** — you point the app at your own classic
> external comic art install (see [Comic art](#comic-art-optional)). Built with **C++ and Qt 6**;
> targets **Windows and Linux**.

## Screenshots

| No-theme chat view | Comic mode with theme turned on |
| --- | --- |
| ![MaxChat no-theme chat view](docs/maxchat-no-theme.jpg) | ![MaxChat with comic mode enabled and theme turned on](docs/maxchat-theme.jpg) |

---

## Features

**IRC client**
- Bundled server directory with many popular IRC networks, failover servers, network homepages,
  type-to-jump search, and a **Reset server list** option
- TLS with certificate validation by default; `Accept unsigned` is an explicit per-network exception
- **CAP + SASL** (PLAIN, with NickServ fallback); passwords are sent only over TLS unless a saved
  network explicitly enables plaintext auth
- Auto-connect/join, **auto-reconnect**
- **Multiple networks at once** — a clickable network tree with channels, queries, and a server tab each
- Channels + private queries · notices · `/me` actions · **CTCP** auto-reply + request/reply in chat
- **WHOIS to the active chat** · **PMs echoed** to the server tab & current chat (so you never miss one)
- Full classic **IRC command set** (current-channel defaults): `/topic /kick /op /ban /mode /msg …`
- **Channel Modes** popup (t/n/s/i/p/m + key/limit) · **right-click** op/kick/ban/CTCP/ignore menus
- **/list** channel browser (sortable, min-users, join-on-click, CSV/copy)
- **Ignore list** (`/ignore`, glob over `nick!user@host`) · tab-completion · input history · format keys
- **Logging** + **replay** on open (with an "Ended" divider) · per-chat scrollback
- **Anti-flood protection** (auto-ignore flooders, large-paste guard, invite-spam guard) · **system-tray** + taskbar-flash notifications · **friends / notify list**
- **Link previews** — inline images/audio/video, X/Twitter cards, and generic OpenGraph website cards
  with thumbnails, controlled from **Preferences ▸ CTCP/Services** (SSRF-safe fetcher: per-redirect-hop
  re-validation, private-address blocking, response-size caps)
- **DCC file transfer** (right-click ▸ Send File; transfers window) with **passive / reverse DCC** for NAT/firewalls
- **Lua scripting** — drop `.lua` scripts in your scripts folder (message/join/command hooks) behind a
  per-script permission prompt; bundled examples include a URL logger, dice, weather, seen, reminders,
  a memo, and a small BBS (see `assets/scripts/`)
- **Raw log, URL list, and server tools** are built in for the practical day-to-day IRC chores

**Comic mode**
- Decodes classic comic `.avb`/`.bgb` art **from your own install** (you point the app at it)
- Panels with **multiple characters** (facing each other), **varied body poses**, **emotions**, and
  speech/thought balloons in a bundled **comic font** (Comic Relief, OFL)
- **Emotion picker + self-view** — choose your own expression (or let it guess from your text)
- **Per-channel** backgrounds + **per-user character** assignments
- 1–6 **reflowing panels**, remembered per channel · **Save Comic…** exports the strip as a PNG
- Comic rendering stays local: no special server support is required, and normal IRC users still just see normal text

**Appearance**
- App + chat themes (including faithful retro / classic-terminal chat looks) + a live theme customizer
  and user JSON themes
- A bundled **theme-pack gallery** (59 ready-made looks with previews + wallpapers) you import from
  **Preferences ▸ Themes ▸ Import**, plus a standalone **Theme Builder** (Help ▸ Theme Builder) for
  making your own
- **Default** theme plus a **Turn themes off** option for users who want native Qt/platform colors
- Per-pane fonts, one-click **JetBrains Mono** / **System Default** font presets, 12/24-hour clock,
  colored nicks / IRCCloud-style role groups, framed menus & popups
- A left-nav **Preferences** dialog with wrapped help text, link-preview toggles,
  spellcheck/language settings, and translation support

**Languages & spelling**
- **16 translated UI languages** (German, Spanish, French, Italian, Portuguese, Dutch, Polish, Turkish,
  Russian, Ukrainian, Japanese, Korean, Chinese Simplified/Traditional, Arabic, Hindi) plus English; set
  under **Preferences ▸ Localization**
- Offline spellcheck via a bundled engine with dictionaries for many languages; on Windows an optional
  native speller backend is available. Misspelled words mark the input; right-click for suggestions

---

## Install

### Download (recommended)

Grab the latest build from the [Releases](https://github.com/IronWolve/MaxChat/releases) page:

- **Windows** — download `MaxChat-<version>-windows-x64.zip`, unzip it, and run `maxchat.exe`.
  Everything it needs (Qt, themes, wallpapers, dictionaries, fonts) is in the zip.
- **Linux** — download `MaxChat-<version>-x86_64.AppImage`, then:
  ```bash
  chmod +x MaxChat-*-x86_64.AppImage
  ./MaxChat-*-x86_64.AppImage
  ```
  It's a single self-contained file with Qt bundled — no system Qt required.

Then **Server ▸ Server List…** to choose a saved network, or **Server ▸ Quick Connect…** for a
one-off connection. Comic mode is optional — see [Comic art](#comic-art-optional) to switch it on.

### Build from source

Requires **Qt 6** (Widgets, Network, Multimedia, LinguistTools) and **CMake** + a C++20 compiler.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/maxchat
```

On **Windows** (MinGW or MSVC), `build.bat` configures, builds, runs `windeployqt`, and assembles a
runnable `dist-win/` folder. See [Building from source](#building-from-source) for options.

## Themes

MaxChat ships built-in app/chat themes, and you can fully customise or create your own:

- **Customise live** — Preferences ▸ Themes: pick an app theme, chat theme, fonts, and wallpaper, and
  save the result as a new theme.
- **Theme gallery** — the `themes/` folder (next to the app) bundles **59 ready-made theme packs** with
  preview images and wallpapers. Import the ones you like via **Preferences ▸ Themes ▸ Import**; they're
  not auto-loaded, so the picker stays uncluttered.
- **Make your own** — open the **Theme Builder** (Help ▸ Theme Builder, or `themes/theme-builder.html`
  in any browser): adjust app/chat colours, fonts, and wallpaper, preview, and save a theme-pack JSON.
  Themes are plain JSON, so they're easy to share.

## Server list

Open **Server ▸ Server List…** to choose or edit networks. A network can have multiple server lines;
the first is the primary server and the rest are used as backups/failover. The list also stores each
network homepage, shown in the server-list popup with a homepage button.

- Press a letter while the server list is focused to jump through network names.
- Use the up/down buttons beside **Server(s)** to reorder a network's servers.
- Use **Preferences ▸ Data ▸ Reset server list** to restore the bundled defaults.
- More IRC network information: <https://www.irchelp.org> and <https://netsplit.de/>.

## Link previews and cards

MaxChat can preview links directly in chat. Image links show thumbnails, audio/video links get inline
play controls, X/Twitter status links get a compact status card, and normal website links use public
OpenGraph/Twitter-card metadata to show a small summary card.

Preview fetching is optional per service under **Preferences ▸ CTCP/Services**, and clicked links can
open in-app (image viewer / audio bar / video player) or in your browser via the **Open links in
browser** master toggle. Privacy note: fetching a preview contacts the linked host from your computer;
turning a service off keeps the plain clickable link and skips the automatic fetch.

## Comic art (optional)

MaxChat ships **no character art**. Comic mode reads `.avb` (characters) and `.bgb` (backgrounds)
art from a **classic late-1990s chat-art program** that you install separately and point MaxChat at:

1. Download the classic program — English, ~1.7 MB:
   <https://phoenix-online-nexus.com/mschat/cchat/mschat25.exe>
   (other languages: <https://phoenix-online-nexus.com/mschat/mschat.htm>)
2. Install it (or just extract the `.avb`/`.bgb` files from its folder somewhere).
3. In MaxChat, open **Comic ▸ Comic Settings**, set the **art folder** to that install directory,
   then click **Comic** on the toolbar.

Without art, MaxChat runs as a normal IRC client — comic mode simply has nothing to draw.

## Scripting

MaxChat embeds **Lua 5.4** for optional scripting. Drop `.lua` files in your scripts folder; each
script runs in a sandbox and must be granted permissions (network access, sending to IRC, disk) before
it can use them — bundled scripts default to no permissions until you allow them in
**Preferences ▸ Scripts**. The bundled examples (`assets/scripts/`) cover a URL logger, dice roller,
weather, last-seen tracker, reminders, a memo pad, and a small interactive BBS. See
[SCRIPTING.md](SCRIPTING.md) for the API.

## Building from source

```bash
# Debug
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure   # 57-target test suite
```

Build options:

| Option | Default | Effect |
| --- | --- | --- |
| `MAXCHAT_TERMINAL` | ON | Script terminal / BBS UI. OFF → a lean "vanilla IRC client". |
| `MAXCHAT_OS_SPELL` | OFF via direct CMake; ON by default in `build.bat` | (Windows) native speller backend in addition to the bundled engine. |
| `BUILD_TESTING` | ON | Build the unit tests. OFF → app only, no Qt Test module required. |

Lua scripting is a core dependency and is always built. UI strings marked with `tr()` are translatable;
sources live in `translations/*.ts` and compile to `.qm` embedded in the binary. Add a language with
`cmake --build build --target update_translations`, translate the `.ts`, add it to `TS_FILES` in
`CMakeLists.txt`, and rebuild.

**Windows:**

```cmd
build.bat            ::  MinGW default → dist-win\ + MaxChat-<ver>-windows-x64.zip
build.bat msvc       ::  MSVC
build.bat noterm     ::  without the terminal / BBS UI
build.bat tests      ::  build, then run the test suite
```

### Release packaging

- **Windows:** `build.bat` assembles `dist-win\` (exe + Qt via `windeployqt` + assets +
  licenses) and zips it to `MaxChat-<version>-windows-x64.zip`.
- **Linux:** `./package-linux.sh` builds Release and produces a self-contained
  `dist-linux/MaxChat-<version>-x86_64.AppImage` with Qt bundled (fetches the
  `linuxdeploy` tooling automatically). Upload either artifact straight to a GitHub Release.

## License

Licensed under the **Apache License 2.0** — see [LICENSE](LICENSE). The bundled font (Comic Relief)
is under the SIL Open Font License 1.1, and vendored components are listed in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md); these are unaffected by the project license.
