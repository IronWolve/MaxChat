# MaxChat 1.0.1 - The Old Internet, Rewired

MaxChat 1.0.1 is the native C++/Qt public release of MaxChat, rewritten from the original Python/Qt prototype into a faster desktop IRC client with comic mode, themes, scripting, localization, and modern link previews.

## Highlights

- Native **C++/Qt 6** desktop build for Windows and Linux.
- Full IRC client workflow: multiple networks, channels, private queries, notices, `/me`, CTCP, WHOIS, `/list`, ignore rules, reconnect, logging, and scrollback replay.
- Built-in server directory with network homepages, failover servers, search, reorder controls, and reset-to-default support.
- Optional **Comic Mode** using your own classic comic chat art files, with multi-character panels, emotions, backgrounds, and PNG export.
- App and chat themes, including a bundled theme gallery, wallpapers, theme previews, and Theme Builder.
- Link previews for images, audio/video, X/Twitter cards, and generic OpenGraph website cards with SSRF-safe fetch limits.
- Lua scripting with sandboxed permissions and bundled examples, including URL logging, dice, reminders, seen, memo, weather, and a small BBS.
- Localization support with 16 translated UI languages plus English.
- Offline spellcheck support, plus optional native Windows spellcheck backend.
- DCC file transfers, including passive/reverse DCC for NAT/firewall setups.

## Downloads

- **Windows:** `MaxChat-1.0.1-windows-x64.zip`
- **Linux:** `MaxChat-1.0.1-x86_64.AppImage`

## Linux Install

```bash
chmod +x MaxChat-1.0.1-x86_64.AppImage
./MaxChat-1.0.1-x86_64.AppImage
```

The Linux AppImage bundles Qt and the app assets, themes, wallpapers, dictionaries, fonts, and scripts.

## Windows Install

Download `MaxChat-1.0.1-windows-x64.zip`, unzip it, and run `maxchat.exe`.

## Notes

Comic mode does not ship third-party character art. To use comic mode, point MaxChat at your own classic comic chat art install from **Comic -> Comic Settings**.

If you prefer a plain IRC client look, use **Preferences -> Themes -> Turn themes off**.

## Checksums

```text
7fa77975c988ee18249ce519128ca4d0a1d4d9cb13445ffa527da42ec224874f  MaxChat-1.0.1-x86_64.AppImage
```
