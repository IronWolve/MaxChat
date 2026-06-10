# Third-Party Notices

This distribution contains MaxChat plus third-party runtime libraries and
assets. MaxChat source code is licensed under the Apache License 2.0; see
`LICENSE`.

## Qt

The Windows build deploys Qt runtime libraries, plugins, and translations with
`windeployqt`. These files remain under their own Qt open-source licenses.

Relevant deployed files include:

- `Qt6Core.dll`
- `Qt6Gui.dll`
- `Qt6Network.dll`
- `Qt6Widgets.dll`
- Qt plugins under `platforms/`, `imageformats/`, `iconengines/`, `styles/`,
  `networkinformation/`, `generic/`, and `tls/`
- Qt translation files under `translations/`

Qt license files are included under `licenses/qt/` when `build.bat` prepares
the Windows deployment folder.

## MinGW Runtime

The MinGW Windows build deploys runtime DLLs needed by the compiler toolchain:

- `libgcc_s_seh-1.dll`
- `libstdc++-6.dll`
- `libwinpthread-1.dll`

Relevant MinGW/GCC runtime license files are included under `licenses/mingw/`
when `build.bat` prepares the Windows deployment folder.

## Fonts

Bundled fonts:

- Comic Relief: SIL Open Font License 1.1
- JetBrains Mono: SIL Open Font License 1.1
- Symbols Nerd Font Mono: MIT License

Font license files are included under `licenses/fonts/`.
