# One Display page, the song's themed top screen, the Theme preview over playback (2026-11-18)

Spec: docs/redesign (redesign-preview) ced345a, 5a3ba2b, f6e1555, 4c03fbf, d509fb2: components.md Song layout, Tile (the
choice tile), Tabs, Themes › The top screen; screens `music-view`, `music-viz-pick`, `music-viz`.

## What changed
- **Song info is always on.** `Settings::songInfo` and its `song-info` entry are gone; a saved `song-info=` line is
  ignored like any unknown key (parseSettings) and not written back. `songLayout(showArt, widest)`: art 240 (Album art
  only) / 224 (Art + visualizer), always with the lines; tested in `testSongLayout`.
- **View on a song: Display, Equalizer, Effects, Speed.** Display is one page (`lay::displayPage`, `displayMove`,
  `displayAt` in kitlayout.cpp; `testKitDisplayPage`): "Top screen shows", four mode tiles 147 x 56, the line
  "Visualizer · <style>" ("· not shown now: the top screen shows <mode>" under Album art only / None), twenty style
  tiles 117 x 56 in a 5 x 4 grid, sheet 452 tall. Drawn by `Kit::choiceTile` (ui_kit_variants.cpp). The cursor moves
  over both groups and changes nothing; A or a tap sets; SELECT steps the mode; L/R tabs; B and Y close.
- **The ground.** Album art only, Art + visualizer, None (and Peaks alone): the top screen is the theme's BG, the lines
  TEXT / DIM_ON / DIM, the chapter line ACCENT, the art's edge `edgeOf(SURFACE)`, mpv's art corners masked in BG.
  How, with no per-frame work:
  - mpv's `background-color` (`Player::setGround`, sent on a change only): the margins round mpv's own art (Album art
    only). Black for videos, photos and the other modes.
  - None, and a song with no art in Album art only: `blankGraph(ground)` fills its tiny 5 fps frame with BG.
  - Art + visualizer: `vizPlaceArt(graph, …, ground, band)` swaps the style's art input for one `color=` frame of BG
    (as the art used to be, one frame, held): every style draws over it as it drew over the art. `Player::vizColours()`
    is the hook for a themed band (ground today = BG; ink unused yet).
  - Peaks (`peaksThemed`, settings.cpp): bars ACCENT, caps TEXT, ground BG when alone (BT.601 limited, Y rounded down
    as its old numbers were: Yellow's graph is byte for byte as before; tested).
  - A theme change (`Player::themeChanged`) re-sends the lines and the ground at once and rebuilds a graph holding the
    colour 500 ms later (`displayDueAt`; one sound reopen, a second on the handheld).
- **The Theme row's preview shows over whatever plays** (topscreen.cpp): `paneNow` no longer asks that nothing plays;
  what plays is drained unseen under it (`drainUnder`, as under a reader); the level card is drawn over the pane
  (`paneCard`, a `CardOverlay` in the pane's renderer: `cardOnReader` routes it there while the pane is up).
- **Themes:** 16-bit Lavender (after 16-bit) and Sakura (after 32-bit): seventeen; the picker is 6 + 6 + 5 swatches of
  84 x 30, 9 apart, names in Inter 10.5.

## Mode switches (owner's handheld bugs)
- **Art + visualizer / Visualizer only / None → Album art only ended the song.** The graph was cleared ("") and the
  tracks chosen after it: for a moment nothing was selected, mpv ended the file (EOF) and the Session played the next.
  Now a song with art reaching Album art only from a graph gets `Player::artPassGraph()` (`[aid1] anull [ao]; [vid1]
  null [vo]`: the art one frame, as mpv gives it): graph to graph, never no track. A song's start still uses no graph.
  Log: `music: <file>: has art, N tracks; graph passing the art through`.
- **Album art only → Art + visualizer drew the visualizer small**: the art's margins stayed on after the graph changed
  (they were set when the mode changed, half a second before the graph). `applyAudioDisplay` now calls
  `updateOverlay` whenever it changes the graph, so the margins and the art box follow the picture the graph makes.
  Log: `music: top panel margins L R T B | none (the whole panel), panscan P` on each change.
- **The art fills its square** in Album art only: `panscan=1` with the margins (mpv crops to the box the margins
  leave), 0 otherwise. The overlay's decode already cut the art square from its middle.
- Checked on the Mac: all twelve switches among the four modes, a song with art and one without: no end, no skip.

- **The art off centre** (Album art only and Art + visualizer): the group (art + 28 + lines) was centred on the
  letters' estimate of the lines' width (`songTextWidth`), ~8% wide on Inter (Sunny Peel: 135 for 126), so the group
  sat left. The lines are now measured with SDL_ttf in the bundle's Inter (what libass draws with on the handheld;
  `\fs` taken as the font's ascent + descent) and cut by the same measure; the estimate stays where the fonts can't be
  read. Log: `… art S at X,Y (px P at x,y on WxH), text W wide at X (lines M, estimated E)`.
- **Fill lost on Next**: `load` sets the aspect mode's panscan (0) for every file, and the margins, unchanged, weren't
  sent again. `load` now clears `marginsSet`, so a song's next `updateOverlay` sends margins and panscan again (a
  video's: the aspect mode's own panscan). Checked: a 600x400 cover filled and centred, Next to a square one, filled.

## Not done / differs from the spec
- **Circle** keeps its own colours (cyan to pink, a pale blue ring) and black ground: the spec's "ACCENT bars, TEXT caps,
  Yellow unchanged" doesn't hold for Circle (Yellow's Circle isn't yellow), and its ring fades to black (a dark halo on
  a light ground). In Art + visualizer it holds the art in its ring and keeps its own picture, as before.
- The band is the styles' 200 rows (not 162): every style's strip height is its own.
- On the in-use tile the cursor is an ACCENT ring 2 clear of it (an ACCENT edge doesn't show on an ACCENT fill).

## Log lines
- `music: top panel ground #RRGGBB (the theme's BG under a song | black)`: each change of mpv's ground.
- `music: <file>: top panel <mode>, ground <#RRGGBB | the picture's>: <art box>, art S at X,Y, text W wide at X`.
- `timing: graph switch: first frame taken N ms after it was asked` (beside `timing: graph switch: sound back …`, which
  some outputs never send): a style or a theme's ground coming up.
- `theme: top panel overlays sent again: N`.
- `top: preview pane over what plays (<file>): it goes on under it`, `top: preview pane up|now …`,
  `top: preview pane dropped: the picture of what plays is back`.

## What may differ on the handheld
`background-color` under vo=libmpv on KMSDRM/GLES (bundled mpv 0.40; checked on the Mac's 0.42 only). The graph rebuild
on a theme change reopens the sound (debounced 500 ms). A pixel-art style's lut tables for the new Peaks colours are
made in the background the first time (the expressions meanwhile). Draining a 30-60 fps graph under the pane costs
what it cost before on screen, minus the drawing.

## Later: nothing playing (branch idle-theme, 2026-11-19)
`Player::showIdle(true)` sets the same ground (`setGround(true, "nothing plays")`; a video or a photo puts black back
at its overlay, a song its mode's), re-sent on a theme change (`themeChanged` shows the idle screen again). The name
is ACCENT, its line DIM, the fallback logo (src/logo.h, when app.png can't be read) BUTTON / ACCENT / TEXT; app.png's
own transparency sits on any ground. Log: `top: ground #RRGGBB (nothing plays | the theme's BG under a song | black:
a picture)`.
