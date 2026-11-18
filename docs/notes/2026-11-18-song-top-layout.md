# The song layout on the top panel (2026-11-18)

The redesign's Song layout (docs/redesign/components.md, Top screen › Song layout): a smaller art box, the title
lines beside it, Art + visualizer as art above a band, a placeholder for a song with no art.

## Where it lives

- `src/songlayout.*` (bpcore): the maths, in 640x480 units. `songLayout(showArt, widest)` gives the art box and
  the text column (art 240 on Album art only, 224 above the band; the lines always shown since 2026-11-18-song-display.md; the group centred on
  its real width in 40-unit margins). `songTextWidth` / `songFitText`: libass measures nothing for us, so a line's
  width is estimated from its letters (Inter: 0.45 x fs, 0.48 bold, measured on its rendering and a little over; a
  wide CJK letter 1 x fs). `vizPlaceArt`: swaps a style's art input in its art + visualizer graph. Tested in
  `testSongLayout`.
- `Player::updateOverlay` (player.cpp): places the art and writes every line as ASS (osd-overlay 1, 960x720 canvas);
  `Player::songGraph`: the graph for a Show mode.

## How the art is placed, and why

The art's x depends on the title's width (the group is centred). A graph change reopens the sound output (a second
on the handheld), and the title is only known after the file opens, so the art can't be placed by the graph without a
gap at every song. So the art is placed outside the graph wherever it can be:

- **Album art only:** mpv's own video (no graph, as before), put in the art's box by `video-margin-ratio-*` (the VO's
  scaling: no filter, no per-frame work of ours). Corners: four black ASS shapes over the art's corners (the background
  is black), each reaching under the edge as mpv's scaled picture bleeds a pixel. The edge: an ASS ring (two contours,
  the inner drawn the other way round: a hole by either fill rule). Margins are reset for anything that isn't a song.
- **Art + visualizer, a local song:** the graph is the style's art + visualizer graph with its `[vid1] scale=640:480…`
  input swapped for one black frame (`color=black:s=640x480:r=1:d=1`), so the band is drawn as before over black and
  the top is black. The art is an `overlay-add` image (id 4; the idle icon is 0, the level card 1): decoded once per
  song on a thread of its own (`probeCover`: the picture in the file or `cover.jpg` beside it; `decodePage`, cut square
  from its middle, corners rounded with a smoothed alpha, premultiplied BGRA) at the panel's pixels, then laid over at
  the layout's spot; it moves with the title at no cost. Pixels live till mpv answers the command (as the level card's).
  The same graph serves a song with art and one without, so an album that mixes them no longer reopens the sound
  between songs (before, a song without art switched Art + visualizer to the visualizer alone).
- **Art + visualizer, a network song:** the decode reads local files only, so mpv's art comes through the graph,
  scaled and cropped once to the box and padded to 640x480 (one frame, as before). The graph can't follow the title,
  so the art sits where a long title puts it (left, at the margin).
- **Circle** keeps the art inside its ring (the style's own use of it); the info then sits at the top left, as over a
  visualizer alone.
- **No art:** the graph as for art (Album art only: the blank graph, as for None), and the placeholder in ASS: 72
  wedges of the colour wheel clipped (`\clip` with a drawing) to the rounded box, a dark square at 38%, and G_NOTE's
  cells at 22%.

Band: the styles' strips are 200 rows (their art + visualizer drawings, unchanged), so the art's group sits in the
280 rows above, not the drawing's 16..304 over a 148-unit band. Changing every style's strip height was out of scope.

## Costs (Mac; the handheld not measured)

- The art + visualizer graphs: the art's one-frame scale + pad becomes one black frame; everything per frame is
  unchanged (the strip still lut2s against its "art" slice, now black). The per-frame figures of
  2026-11-09-visualizer-performance.md stand; a run of `tools/viz-bench` on the handheld would confirm.
- The art's decode: 7-13 ms a song on the Mac (480 px; a 600x600 PNG), off the main thread, once per song and size.
  `music: <file>: art for the overlay decoded in N ms (P px)` in the log.
- ASS: rendered by libass when the overlay changes (a song, its tags, a chapter, a setting), not per frame. The
  placeholder is the heaviest event (72 clipped wedges).
- Graph switches: none between songs in Art + visualizer for local files (`timing: graph switch` absent in a three-song
  list: art, no art, long title).

## Log lines

`music: <file>: top panel <mode>, info on|off: <what the art box holds>, art S at X,Y, text W wide at X` once per
change (the art box: mpv's art in the margins, the graph's art, the decoded art, the placeholder, nothing yet).

## What may differ on the handheld

KMSDRM and GLES: `video-margin-ratio` and the overlay's position are in the render target's pixels (topW x topH);
the art is decoded at `S x topH / 480`. glibc: the decoded art is ~0.25 MB (240 units on 480 rows), freed at the next
song. The libass rendering of the placeholder on the A55 (once per song without art). Software decoding: the decode
runs at nice 10.

Later (2026-11-18-song-display.md): the lines are always on, and the ground is the theme's BG (mpv's background-color,
the blank graph's and the art + visualizer graph's one `color=` frame) instead of black.
