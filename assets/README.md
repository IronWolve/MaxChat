# Assets

Art and fonts used by the comic renderer.

- `characters/` — per-character expression art (one image per emotion), plus a manifest
  describing each character. Format TBD in planning.
- `backgrounds/` — panel backdrops.
- `fonts/` — lettering fonts for balloons (check licensing before bundling).

Nothing is committed yet. Keep only assets we have the right to ship; prefer original
or clearly-licensed art, and keep third-party brands out.

- `cc-art/` — **TEST ONLY, DO NOT SHIP.** Copies of a Microsoft Comic Chat install's `.avb`/`.bgb`
  art (Jim Woodring / Microsoft IP), here so the comic renderer works while developing. Gitignored
  (`/assets/cc-art/`, `*.avb`, `*.bgb`). The app loads it via `assets.bundled_art_dir()` only when no
  `comic_art_dir` is configured. **Delete this folder before any public release** — at release the art
  comes only from the user's own Comic Chat install.
