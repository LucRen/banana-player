# Six simple visualizers in the theme (2026-11-19)

Branch `viz-simple`. Circle, Music box, Bubble tea, Jellyfish, Sound garden and Long cat made way for six plain styles,
each in the replaced one's slot of `VIS_SHOWN`, all in `src/viz_simple.cpp`.

## The styles (theme roles)
| Style | Replaces | Draws |
|---|---|---|
| Mirror bars | Circle | 40 spectrum bars up and down from a centre line: bars ACCENT, the line DIM |
| Wave | Music box | the waveform as one thick soft line ACCENT; alone a DIM centre line under it |
| Dots | Bubble tea | 32 x 22 (band: 9) round-cornered dots: lit ACCENT, unlit BUTTON |
| Levels | Jellyfish | two stereo meters (RMS, -30..0 dB): level ACCENT on a BUTTON track, peak tick TEXT |
| Area | Sound garden | 48 levels stretched bilinear into one filled shape: edge ACCENT, fill 6-62% ACCENT fading to BG downwards |
| Radial | Long cat | 96 bars round a ring, four mirrored quarters (low notes top/bottom): bars ACCENT, tips TEXT, ring DIM |

All on BG alone (the song's lines over them are the theme's: `visualizerOnGround`), over the band's ground in Art +
visualizer (the art's planes kept where the code is 0, as Peaks). A theme change rebuilds them (Player::themeChanged).

## The recipe (one for all six)
Peaks': a half-size (320x240; band 320x100) gray picture of small codes made by `lut2` against a still (one frame:
`color=…:r=1:d=1,geq`, or Radial's two 16-bit tables by `movie=`), then a lut a plane (codes as classes, or a ramp
0..250 from the ground to ACCENT, 255 a second ink), only the luma scaled x2 (bilinear for Wave, Area, Radial alone;
over the art bilinear only for ramp codes: Wave, Area). YUV straight to mpv; nothing converted per frame; rate=60,
drawn at the visualizer-fps setting by `vizAtFps`. The spectrum row (`spectrumRow`) is Pocket console's measure and
contrast, the bottom 40 of 252 steps cut off (the spectrum's bed), eased as the LED meter's.

Levels is the exception for its input: `showvolume` (RMS a frame, log scale, two 252-pixel bars averaged to a pixel
each: the share lit is the level). It wants stereo, so its head is `asplit=2` and `vizShapeInput` leaves it unshaped.

## Pitfall found
`format=gray,scale=…,bplut=…,format=gray16le`: the scaler's output is free, so FFmpeg negotiated gray16 into the lut
(the conversion moved before it) and the cached 8-bit lookup saw 16-bit values: the level was always full scale, then
0 after the exp/log easing. A `format=gray` right after a `scale` that feeds a bplut pins it. (Peaks and Pocket are safe:
`format=gray` sits before their first bplut, and a lut's input and output share a format.)

## Expected cost (reasoned, not measured: the owner tests on the handheld)
Per frame each does about what Peaks does: one tiny showfreqs (or showvolume) measure, a stretch of one row
(pad + fillborders: row copies), one or two lut2 and three luts at 320x240, a nearest/bilinear luma scale to 640x480.
Radial adds a remap of a 120x120 quarter (one lookup a pixel, ~14k) and the flips/stacks (row copies): a little
above Peaks. Wave's showwaves at 320x240 in gray plus an area scale down and a bilinear scale up: about Peaks.
Unlike Peaks (fixed 30 fps) they follow visualizer-fps (default 45), so per second ~1.5x Peaks' at the default:
still far under Circle (a 400x400 three-plane remap, 8.4 ms a frame on the A55s, docs/notes/2026-11-09-visualizer-performance.md).
`tools/viz-bench/` would confirm on the handheld if wanted.

## Log lines
`visualizer: <Style> in <theme>: <role> #RRGGBB, …; graph built in N ms` (Radial adds `tables ready|NOT written`),
at each build (also during the lut warm-up, twice a style). `visualizer: N stills of the retired styles removed from
<dir>` once, when an older build's pictures are found.

## Kept / removed
Retired ids stay readable (`visualizerReplacement`): a saved `circle`, `music-box`, `bubble-tea`, `jellyfish`,
`sound-garden`, `long-cat` reads as its replacement and is written back as the new id. The five scene files and
Circle's code (settings.cpp, its tests) are removed; their stills are unlinked from the state folder by
`writeVisualizerMaps`. Glyphs `G_VIS_MIRROR … G_VIS_RADIAL`; web/sprite.svg regenerated (the dashboard lists no styles).

## Checked
Build, core-test (`testVizSimple`: rules, theme change, Radial's tables, retired stills removed; replacements and
slots in testSettings). Each graph also ran through the Mac's ffmpeg once (alone and band) to see the pictures.

## What may differ on the handheld
`showvolume` in the bundled FFmpeg 7.1.1 (checked on the Mac's); remap with gray8 and `movie=` 16-bit PGMs as Circle
used; the cost above.

## Later the same day: Area became Horizon, Radial became Steps (branch viz-horizon)
The owner's call (approved proposals). `area` reads as `horizon`, `radial` (and `long-cat`) as `steps`
(`visualizerReplacement`), each in the slot it replaces.
- **Horizon** (`horizonGraph`): the 20 levels' row hflipped and hstacked (bass in the middle), stretched to the sky
  (160 rows alone, 64 in the band), told apart by lut2 against a still of each row's step and a low sun's disc;
  the reflection is the sky's codes vflipped, squashed (nearest) and masked by a still of stripes and the horizon
  line; vstack, then the palette. Skyline ACCENT, sun BUTTON, line and reflection DIM. The proposal's showfreqs bars
  at half width are the measured row here (mirrored as a row: 40 values, not a picture).
- **Steps** (`stepsGraph`): 20 bands of 16 columns; the outline (the step's top, and a band's rise or fall at its
  first two columns, known from a second row of the levels shifted one band) by two lut2 against a still of each
  pixel's step; a third with the held peaks (lagfun 0.95, ~3.4 s) gives the DIM peak line. Outline ACCENT, fill
  BUTTON. The proposal's mask minus its shifted copy (blend=difference) is done by these lookups instead: no
  full-size shifted copy or blend a frame.
- Radial's remap, its tables and code are gone; its tables (radial1-3) are unlinked from the state folder once, with
  the other retired stills. No simple style reads stills any more.
- Expected cost (reasoned): Horizon about Peaks plus a flip and a nearest squash of 320x80 and a vstack (row
  copies); Steps three lut2 at 320x240 and three row stretches, a little over Peaks, cheaper than Radial's remap.
- Checked: build, core-test; one render of both through the Mac's ffmpeg. The switch bug (Area → Mirror bars in Art +
  visualizer stopping the song) was not investigated (owner's call): Area is gone.

## The final list (branch viz-final)
The owner's fifteen, in this order (VIS_SHOWN; the Display page's 5 x 3 grid; Settings' row steps the same): Peaks,
Mirror bars, Pocket console, Night city, Horizon, Aurora, Dots, Steps, Barcode, Rings, Banana dancer, Pixel sea,
Jelly wobble, Washing machine, Tube man.
- **Barcode** (`barcodeGraph`): 96 levels stretched (nearest) along one 320-wide row, a still keeps the first column
  of each band (a 1-pixel line here, 2 on the panel, ~6.7 apart), the row stretched down (row copies); each line's
  strength painted from BUTTON (quiet) to ACCENT (loud) (`between`).
- **Rings** (`ringsGraph`): 10 levels and a column for the dot stretched down 8 rows, told apart by a still of each
  row's distance out (row 0 the ring's middle line: BUTTON when the band is under its bed); remap from two still
  quarter tables (`rings1-*.pgm`, 120x120 alone, 50x50 in the band), hflip/vflip for the other quarters. Rings
  ACCENT, guides BUTTON, centre dot TEXT. Cost reasoned: a quarter remap (14k lookups) plus row copies, about Peaks.
- Dropped, code deleted: Wave, Levels (viz_simple.cpp), Campfire, Bunny hop, Fishbowl, Vibing cat, Popcorn
  (viz_*.cpp); their stills unlinked once by writeVisualizerMaps. A saved one, and Music box / Jellyfish (which had
  become Wave / Levels), reads as Peaks (`visualizerReplacement`). Tube man is kept.
- Checked: build, core-test (the order round, the 15-tile Display page, the replacements, Rings' tables, the dropped
  stills removed); one render of Barcode and Rings through the Mac's ffmpeg.
