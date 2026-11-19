# Title marquee and Repeated start (2026-11-19, issue #14)

Spec: docs/redesign/components.md › Title marquee (redesign-preview branch), screen `long-title`.

- **Pure parts (bpcore, core_test `testTitleMarquee`):** `lay::marquee(ms, textW, boxW, fontPx, passes)` is the whole
  timing (rest 1.5 s, 24 steps a second at 2 units a step from 24 px type, 1 under; rest 1.5 s at the end; fade out and
  in 150 + 150 ms; 3 s from the fade's end to the next move; 2 passes, 1 in a row) and gives the offset, the text's
  alpha, which edges fade, and a `step` that changes only when the picture does. `shownTitle` + `titleStarts` +
  `sharedStart` (songlayout) are the Repeated start rule.
- **Bottom panel:** `Ui::drawMarquee` (ui_draw.cpp): the text clipped to its box, moved, its alpha; the fades are four
  4-unit strips of the ground at 20..80%. The first title drawn in a frame that isn't done claims the panel (rows draw
  before the mini bar, so a focused row's pass comes first and the mini bar waits); each title's start time is kept by
  its key (size, box, passes, text) and the map is cleared on a page change, while a sheet/menu/keyboard/picker is up,
  and while the panel is off. The step goes in `signature()` only while one moves (a full redraw 24 times a second for
  the few seconds it moves: no partial-redraw path in the renderer today), and `animating()` keeps the loop at 16 ms.
  Used by the player's header title (`Header::marquee`, `titleFor` gives the Repeated start for the room left), the
  mini bar (`Ui::drawMiniBar`) and `Kit::row`'s focused row (one pass, ground BUTTON).
- **Top panel:** when the title doesn't fit (any mode) it leaves the lines' overlay (id 1) and is its own ASS overlay
  (id 2): `\anN\pos(x - offset)\clip(box)\alpha` plus the fades as BG drawings, sent again only when the step changes
  (`Player::tickMarquee`, each loop pass; the loop wakes at 16 ms while `marqueeMoving()`). None's block is split: the
  title alone at the box's left, the rest `\an8` under it (heights at 1.2 the size a line). The native styles keep off
  the lines' box as before (alone: the box is the whole 596 when the title moves).
- Not wired: the lid and the screen-off warning don't stop the top panel's marquee (it ends after two passes anyway);
  the bottom's restarts on wake via the cleared map. Fonts measured with the bundle's Inter on both (SDL_ttf for the
  bottom's Pixelify; `lineW` for the top).

Checked on the Mac: a song titled "Yellow Album - 03 - Sunny Peel (Live …)" (album Yellow Album) shows "03 - Sunny
Peel (Live …" moving on both panels in Album art only and Visualizer only; a video "Big Buck Bunny - S01E03 - …" in
the folder Big Buck Bunny moves on the bottom panel. Untimed on the handheld: the bottom's full redraw at 24 a second
while it moves, and mpv's frame per step in Album art only.
