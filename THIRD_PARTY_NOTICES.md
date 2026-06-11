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

## Lua

When built with scripting enabled (`-DMAXCHAT_LUA=ON`), MaxChat statically links
the Lua 5.4.7 interpreter, whose source is vendored under `third_party/lua/`
(the standalone `lua.c`/`luac.c` mains are excluded). Lua is distributed under
the MIT License:

> Copyright © 1994–2024 Lua.org, PUC-Rio.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in
> all copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
> THE SOFTWARE.

Vendored source: Lua 5.4.7, `lua-5.4.7.tar.gz`,
sha256 `9fbf5e28ef86c69858f6d3d34eccc32e911c1a28b4120ff3e84aaa70cfbf1e30`.

## Hunspell

MaxChat statically links the Hunspell 1.7.2 spell-checking engine, whose source
is vendored under `third_party/hunspell/`. Hunspell is tri-licensed under
GPL-2.0 / LGPL-2.1 / MPL-1.1; MaxChat uses it under the **LGPL-2.1** (and/or
MPL-1.1). Full license texts are vendored alongside the source as
`COPYING` (GPL), `COPYING.LESSER` (LGPL), and `COPYING.MPL` (MPL), plus the
upstream `license.hunspell` / `license.myspell`.

## Spelling dictionaries

MaxChat bundles Hunspell `.aff`/`.dic` dictionaries under `assets/dictionaries/`.
These are **data files**, not linked into the binary, and each remains under its
own upstream license (mere aggregation). The per-dictionary upstream license
text ships alongside each pair (`assets/dictionaries/README_en_US.txt` for
`en_US`; `assets/dictionaries/LICENSE_<code>.txt` for the rest). Most were
sourced from the curated Hunspell dictionary collection at
<https://github.com/wooorm/dictionaries>, which preserves each project's
original license.

| Code   | Language               | License (upstream)              |
|--------|------------------------|---------------------------------|
| en_US  | English (US)           | SCOWL, BSD-style (permissive)   |
| en_GB  | English (UK)           | SCOWL, BSD-style (permissive)   |
| nl_NL  | Dutch                  | BSD-style (permissive)          |
| ru_RU  | Russian                | MIT (permissive)                |
| uk_UA  | Ukrainian              | MIT (permissive)                |
| tr_TR  | Turkish                | MIT (permissive)                |
| es_ES  | Spanish                | MPL-2.0 / GPL / LGPL            |
| fr_FR  | French                 | MPL-2.0                         |
| de_DE  | German                 | MPL / GPL / LGPL                |
| it_IT  | Italian                | GPL-3.0                         |
| pl_PL  | Polish                 | GPL / LGPL / MPL / CC-BY-SA     |
| pt_BR  | Portuguese (Brazil)    | LGPL                            |
| pt_PT  | Portuguese (Portugal)  | GPL-2.0                         |
| sv_SE  | Swedish                | LGPL                            |
| nb_NO  | Norwegian Bokmål       | GPL-2.0                         |
| da_DK  | Danish                 | GPL / LGPL / CC-BY-SA           |

The GPL/LGPL/MPL dictionaries are aggregated with — not derived from or linked
into — MaxChat; the MaxChat application itself stays under the Apache License
2.0. Finnish is intentionally not bundled (Finnish spell-checking requires the
Voikko engine rather than a plain Hunspell dictionary). Additional or updated
language dictionaries can be added at runtime by dropping the `.aff`/`.dic` into
the `dictionaries` folder.

## Fonts

Bundled fonts:

- Comic Relief: SIL Open Font License 1.1
- JetBrains Mono: SIL Open Font License 1.1
- Symbols Nerd Font Mono: MIT License

Font license files are included under `licenses/fonts/`.
