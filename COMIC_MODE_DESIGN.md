# Comic Mode — design & invariants

Internal design doc (keep local; gitignore before any public push, like
AUDIT.md). Read this before touching anything under `src/comic/` or the comic
engine in MainWindow. The high-level "what the panel area does" lives in
UI_SURFACES.md §9; this is the *how and why*.

## 1. Architecture map

| Piece | File | Job |
|-------|------|-----|
| Art decoding | `src/comic/ComicArt.cpp` | .avb (characters) / .bgb (backgrounds): zlib-deflated cells; `scanArtDir` |
| Character | `src/comic/ComicCharacter.cpp` | body poses + emotion faces; `imageTrimmed(emotion, facing, pose)`, `faceCell` for picker previews |
| Emotion guess | `src/comic/ComicEmotion.cpp` | `guessEmotion(body)` — pure text heuristics, unit-tested (`comic_emotion_test`) |
| Panel renderer | `src/comic/ComicRenderer.cpp` | `renderComicPanel` — balloons, tails, captions, actors over a background |
| Panel display | `src/ui/ComicView.cpp` | grid layout + per-panel context menu + double-click zoom lightbox |
| Engine | `src/ui/MainWindow.cpp` `refreshComic()` | buffer lines → filter → group into panels → render (cached) |

## 2. Per-channel scope (NOT global)

Comic Mode is **opt-in per buffer**: `m_comicEnabledBuffers`
(key = `network\x1ftarget`); `m_comicMode` is *derived* — backend on iff the
set is non-empty. The toggle is `setEnabled(false)` on the server buffer.
History: the original design was "global flag + hide-set" with a secret
"toggle-off-on-server = global kill" — see DEV_NOTES for why that's banned.

## 3. Emotion pipeline

- 9 emotions: neutral + happy/sad/angry/laughing/coy/scared/shouting/bored.
- Auto mode: `maxchat::comic::guessEmotion` — emoticons, emoji, keywords,
  all-caps/`!!!` → shouting. **Every emotion must stay reachable from text**
  (angry/bored were unreachable for months and nobody noticed — guard with
  `comic_emotion_test`).
- Precedence: louder/more-specific first (laughing > happy, angry `>:(` >
  sad `:(`).
- Your own panels: `m_comicSelfEmotion` override via Comic ▸ Emotion — an icon
  grid of YOUR character's real `faceCell` faces (plain list w/o art).

## 4. Panel grouping & filtering (refreshComic)

- Source: the active buffer's stored lines (`sourceText` `<nick> body`,
  `* nick action`); links never become panels.
- Filters: global + per-channel ignore lists, bot-command prefixes
  (`comic_ignore_cmds` + `comic_bot_patterns`), `comic_exclude_regex`.
- `(parenthesised text)` ⇒ THOUGHT balloon (`think`); `/me` ⇒ tailless
  narration box (`action`).
- Grouping: greedy — extend the last panel while `panelMinFont(...) >= minFont`
  (i.e., text still fits at a readable size), else start a new panel; keep the
  last `comic_panels` (1–6) panels; pad with blank-background panels.
- Pose/character stability: per-nick character assignment (per-channel
  override > global map > deterministic auto-pick); pose =
  `comicHash(nick|text) % bodyCount` — deterministic, so re-renders are stable.

## 5. Panel cache (performance)

`m_comicPanelCache : QHash<QString, QPixmap>` in MainWindow. Key concatenates
**every visual input**: background `cacheKey()`, captions flag+scale, each
actor's nick/emotion/pose/caption-color, each line's actorIndex/kind/text,
panel size. Consequences:
- Only the panel that actually changed re-renders on a new message (the
  earlier panels' keys are unchanged).
- Settings/theme/art changes don't need explicit invalidation — they change
  the key. The cache IS explicitly cleared on art-dir reload (`ensureComicArt`)
  and crudely flushed past 96 entries (cheap flush > LRU bookkeeping here).
- The blank filler panel is cached under `blank|<bgKey>|<size>`.
**Rule: any new visual knob added to renderComicPanel MUST go into the key.**

## 6. Balloon & tail geometry (ComicRenderer)

Balloons hang above their speaker's column; the lowest balloon in a column
grows a tail; stacked balloons get short tails stopped above the balloon below
(`Row::tailStop`).

`drawTail` — ONE rule set for every tail (see the long comment in the code):
- base on the balloon's bottom edge at the point nearest the speaker's head,
  inset past the rounded corners;
- base width PANEL-relative: 2.2% of panel, clamped 5–12 px, never > bw/4
  (**never balloon-relative** — that's what produced needle tails);
- tip travels along the base→head direction (aimed at `charTop + 10%` — the
  face), length 70% of the distance, clamped to 5–16% of the panel, stopping
  short of the head line / balloon below / panel edge. Tails *point at* the
  speaker, they don't need to touch;
- lean clamped to ~50° from vertical;
- edges are **quadratic curves**: both control points offset horizontally by
  `0.35 × (baseMidX − tipX)` (bow back toward the balloon) — a vertical tail
  stays straight. `drawTri` is the single place this lives;
- think-puffs: shrinking ellipses riding the same quadratic bow
  (`offset = 2t(1−t)·bow`), radii panel-scaled.

## 7. ComicView interactions

- Layout: `panelRects()` is the single source for painting AND hit-testing.
- Right-click: copy/save the panel under the cursor; copy/save whole sheet;
  "Save all panels..." (numbered PNGs).
- Double-click: `PanelZoomDialog` lightbox — ~80% of screen, Left/Right flips
  through the strip, click/Esc closes.
- Save Comic default filename: `comic-<chan>-<yyyy-MM-dd>.png`.

## 8. Checklist — touching comic code

1. New visual setting? Add it to the panel-cache key (§5) or panels go stale.
2. New tail/balloon case? Route it through `drawTail`/`drawTri` — no
   special-cased geometry (that's how the old inconsistent tails happened).
3. New emotion trigger? Add a `comic_emotion_test` row; keep all 9 reachable.
4. Per-buffer comic state? Key by `network\x1ftarget`, mirror the rules in
   CHAT_VIEW_DESIGN §7.
5. Python parity: the Python client's comic (`maxchat/comic/`) does NOT have
   the new tail geometry / cache / zoom / emotion upgrades — tracked in
   `../maxchat/DEVDOCS/BACKPORTS.md`.

See also: UI_SURFACES.md §9, DEV_NOTES.md "THINGS I GOT WRONG" (comic toggle
semantics, tail geometry), CHAT_VIEW_DESIGN.md (buffer model the engine reads).
