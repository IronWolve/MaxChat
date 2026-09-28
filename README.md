# MaxChat

A native C++/Qt IRC client for **Windows, Linux and macOS**, with optional comic-strip conversations.

Connect to multiple IRC networks, customize the interface, and use Lua scripts
for automation. Comic mode turns messages into panels with characters, emotions
and speech balloons using your own `.avb`/`.bgb` artwork. No character art is bundled.

## Download

[Get MaxChat 1.0.2](https://github.com/IronWolve/MaxChat/releases/tag/1.0.2).
Qt, fonts, dictionaries, themes and the required helpers are included.

| Platform | Download | Requirements |
| --- | --- | --- |
| Windows | [Windows x64 ZIP](https://github.com/IronWolve/MaxChat/releases/download/1.0.2/MaxChat-1.0.2-windows-x64.zip) | Tested on Windows 11 x64. Extract the whole ZIP and run `maxchat.exe`. |
| Linux | [Linux x86-64 AppImage](https://github.com/IronWolve/MaxChat/releases/download/1.0.2/MaxChat-1.0.2-x86_64.AppImage) | Requires glibc 2.43 or newer; tested on Ubuntu 26.04. |
| macOS | [Apple Silicon DMG](https://github.com/IronWolve/MaxChat/releases/download/1.0.2/MaxChat-1.0.2-macos-arm64.dmg) · [ZIP](https://github.com/IronWolve/MaxChat/releases/download/1.0.2/MaxChat-1.0.2-macos-arm64.zip) | Apple Silicon, macOS 13+. Tested on macOS 26.6.2. Intel Macs are not included. |

**macOS is new in 1.0.2.** Open the DMG and copy `MaxChat.app` to Applications, or
extract the ZIP. The app is ad-hoc signed, not Developer ID signed or notarized;
macOS may require approval in System Settings → Privacy & Security on first launch
(see [Apple's instructions](https://support.apple.com/en-us/102445)).
Windows and Linux packages are unsigned. Release assets include SHA-256 checksums.

On Linux:

```sh
chmod +x MaxChat-1.0.2-x86_64.AppImage
./MaxChat-1.0.2-x86_64.AppImage
```

Open **Server → Server List** or **Quick Connect** to choose a network. Keep the
application, helpers and bundled resources together when moving an installation.

## Features

- Multiple IRC networks, channels and private conversations; TLS, SASL, reconnect,
  server tools, logging and scrollback.
- DCC transfers, flood protection, nick completion, notifications and a friends list.
- Optional link previews and local media playback, with bounded HTTP requests and
  address/TLS validation. Automatic previews are off for new profiles.
- Optional comic mode with characters, expressions, backgrounds and PNG export.
- App/chat themes, 59 theme packs, a theme builder and customizable fonts.
- English plus 16 translated UI languages, and offline spelling dictionaries.
- Lua hooks, timers, script terminals and bundled examples in [assets/scripts](assets/scripts/).

## Scripts and private data

Grant script permissions in **Preferences → Scripts**. Lua runs in separate workers
with execution, allocation and output limits: a runaway script is unloaded while
the application remains responsive. Each script has a 32 MiB Lua heap and a
two-second execution budget per callback, with separate bounded host waits.
Granting native program execution lets a script run programs with your privileges;
the worker is not an OS sandbox for that permission.

Saved credentials use Windows Credential Manager, macOS Keychain or an existing
unlocked Secret Service on Linux. Settings exports omit passwords and keychain
references. Back up your profile before upgrading and re-enter credentials when
moving to another computer.

`--profile <directory>` selects a portable profile. Relative paths resolve beside
the executable, or beside the `.app` on macOS. For an AppImage, select a writable
directory outside the mounted image. Profile data is excluded from release packages.

## Build from source

Requires a C++20 compiler, CMake 3.24+, Ninja and a Qt 6.11.2 or newer SDK with
Core, Gui, Widgets, Network, Multimedia, MultimediaWidgets and LinguistTools.
Qt Test is needed when building tests. The published 1.0.2 packages use Qt 6.11.2.
Review the licenses of any replacement dependencies before distributing a build.

Use an enclosing project directory so source, output and private state stay separate:

```sh
mkdir maxchat
cd maxchat
git clone https://github.com/IronWolve/MaxChat.git repo
```

Run the following from that project directory:

| Platform | Build command | Output |
| --- | --- | --- |
| Linux | `./repo/tools/build.sh` | `run/app/` and `run/launchers/` |
| Linux AppImage | `./repo/package-linux.sh` | `run/packages/` |
| Windows | `repo\build.bat tests` | `run/windows/` and a ZIP in `run/packages/` |
| macOS | `./repo/package-macos.sh` | `run/macos/MaxChat.app`, ZIP and DMG in `run/packages/` |

Select an installed Qt SDK with `MAXCHAT_QT_ROOT` on Linux/macOS or `QT_DIR` on
Windows; select a MinGW toolchain with `MINGW_DIR` when needed. Relative settings
resolve from the enclosing project directory. The build scripts do not install
Qt or start the application. AppImage packaging downloads checksum-pinned tools
when its project-local cache is empty.

On Linux, use `run/launchers/start.sh` and `stop.sh` for the deployed app; on macOS,
use `run/launchers/start-macos.sh` and `stop-macos.sh`. These launchers keep the
profile in `.config/maxchat`, with logs, cache and temporary state inside the
project directory. Do not build inside `repo/` or copy a development checkout into
a runtime installation. Preserve the profile when updating the built app.

## License and release sources

MaxChat retains its [Apache-2.0 license](LICENSE). Libraries and assets retain their
own grants; see [third-party notices](THIRD_PARTY_NOTICES.md) and
[library replacement instructions](licenses/RELINKING.txt).

Each release includes matching application source, corresponding third-party
sources, component/file inventories and checksums. Keep those source companions
and required notices available when redistributing the binaries.
