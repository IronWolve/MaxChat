# Third-Party Notices

MaxChat application source retains its existing Apache-2.0 license (`LICENSE`).
Bundled libraries, fonts and dictionary data retain the grants below. Required
texts ship with every platform. See `licenses/RELINKING.txt` for source delivery,
replacement and rebuilding instructions; no additional restriction on debugging
modified libraries is imposed.

The BS MaxChat logo is copyright © 2026 IronWolve, all rights reserved, and is
included by permission. It retains its separate branding terms; see
`assets/branding/COPYRIGHT.txt`.

## Qt and multimedia

Qt 6.11.2 libraries and plugins are dynamically deployed under their LGPL-3.0
option. Actual modules come from qtbase, qtdeclarative (Linux Wayland transitives),
qtsvg, qtimageformats, qtmultimedia and qttranslations. These include Core, Gui,
Widgets, Network, Multimedia, MultimediaWidgets and platform-specific support.
The package file inventories identify the actual delivered modules/plugins.
Required Qt and embedded third-party grants are in `licenses/qt/`. The statically
linked Windows startup helper uses its BSD-3-Clause option, recorded separately.

The Qt FFmpeg runtime reports 7.1.5 and LGPL-2.1-or-later, with optional GPL
components disabled. Qt's generated metadata labels it 7.1.3; the binary version
and configuration take precedence. Its license is `licenses/ffmpeg/LGPL-2.1.txt`.
Qt/FFmpeg source and build information accompany the release. ICU 73.2, where
bundled, is covered by `licenses/icu/LICENSE.txt`.

Source: https://download.qt.io/official_releases/qt/6.11/6.11.2/submodules/
FFmpeg: https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz
ICU: https://github.com/unicode-org/icu/tree/release-73-2

## Platform runtimes

Windows uses MinGW GCC 13.1.0 runtime DLLs (`libgcc_s_seh-1.dll`,
`libstdc++-6.dll`, `libwinpthread-1.dll`). Their copyright, GPL texts and GCC
Runtime Library Exception are in `licenses/mingw/`; the exception applies to
normal eligible compilation of independent application code. License filenames
ending in `.LIB`/`.lib` have a `.txt` suffix, with unchanged contents. The optional
Mesa 11.2.2 software OpenGL renderer is excluded from this release.

Linux copied distribution libraries have exact package versions and copyright
texts under `licenses/system-runtime/`. Their corresponding source packages,
including distribution patches/build recipes, accompany the release. The
AppImage loader retains its embedded upstream license; packaging tools are
build-only and are not included as application runtime dependencies.

macOS uses Apple system frameworks and libc++ supplied by the operating system;
these system files are not copied into MaxChat.app. Its Qt/media frameworks are
replaceable; the supplied signing script permits ad-hoc re-signing after changes.

## Lua

The maxchat-script-worker helper statically links
the Lua 5.4.9 interpreter, whose source is vendored under `third_party/lua/`
(the standalone `lua.c`/`luac.c` mains are excluded). Lua is distributed under
the MIT License:

> Copyright © 1994–2026 Lua.org, PUC-Rio.
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

Vendored source: Lua 5.4.9, `lua-5.4.9.tar.gz`,
sha256 `2335b6c582a52654f94612bf10d2f4672805d05329aa6568b1d8cd9e5c6fb8e6`.

## Hunspell

Hunspell 1.7.4 is statically linked under its **MPL-1.1** license option, retaining
the MySpell BSD notices. Its unmodified covered source is in `third_party/hunspell/`
and the matching MaxChat source archive. Preserve `COPYING.MPL`, `license.hunspell`
and `license.myspell`; upstream alternative GPL/LGPL texts remain included.
MPL-covered files retain MPL terms when modified, while the larger MaxChat work
retains its own license. Full original source:
https://github.com/hunspell/hunspell/releases/download/v1.7.4/hunspell-1.7.4.tar.gz
SHA-256: `66ec82a577395fe9d471504267e6dd04615c76517c61af7c6b9c19e5e34e73c8`.

## Spelling dictionaries

The separately loaded, editable `.aff`/`.dic` data remains under each dictionary's
own grant. Upstream texts ship alongside the files. `licenses/DICTIONARIES.json`
records exact file hashes, collection package versions, modifications and the
pinned upstream revision. The corresponding-source companion also includes
original source distributions and generators; the collection's normalization
script records UTF-8/whitespace conversion. These data licenses do not replace
MaxChat's independent application license.

| Dictionary | Selected redistribution terms |
| --- | --- |
| en_US, en_GB | SCOWL composite MIT and BSD-style grants |
| nl_NL | BSD-3-Clause option |
| ru_RU | BSD-style grant; modified versions must be marked |
| tr_TR | MIT |
| es_ES, pt_PT, da_DK | MPL-1.1 option |
| fr_FR, pl_PL, pt_BR | MPL-2.0 option |
| de_DE, it_IT, uk_UA | GPL-3.0 option |
| nb_NO | GPL-2.0 |
| sv_SE | LGPL-3.0 |

The marked en_US affix change adds curly-apostrophe handling. Other files match
the pinned collection data. Dictionary replacement is supported by supplying
compatible `.aff`/`.dic` pairs; retain the corresponding licenses and sources.

## Fonts

- Comic Relief 1.200 regular/bold: OFL-1.1; copyright 2013 The Comic Relief
  Project Authors. See `licenses/fonts/ComicRelief-OFL.txt`.
- JetBrains Mono 2.305 regular/bold: OFL-1.1; copyright 2020 The JetBrains Mono
  Project Authors. See `licenses/fonts/JetBrainsMono-OFL.txt`.
- Symbols Nerd Font Mono 3.2.1: the symbols-only directory uses the upstream MIT
  grant, with component glyph terms and attribution retained in
  `licenses/fonts/SymbolsNerdFont-COMPONENTS.txt`. These include MIT, CC BY 4.0,
  Apache-2.0, OFL-1.1 and the Unlicense. The font is unchanged from that release.

Fonts may be bundled with the application; OFL fonts may not be sold alone.
Reserved names and original attribution remain intact. Font and icon trademarks
do not imply endorsement of MaxChat.

## zlib

The bounded comic-art decoder statically links the unmodified inflate subset of
zlib 1.3.2 under its zlib grant. The full notice is `third_party/zlib/LICENSE`.
Source: https://zlib.net/zlib-1.3.2.tar.gz
SHA-256: `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`.
Compression/gzip utilities are not included in this subset.

Release source companions, dependency manifests and file checksums identify the
actual distributions. Private profiles, toolchains and SDK installations are
excluded from the application packages.
