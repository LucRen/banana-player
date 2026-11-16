# The redesign kit, and how to build a screen with it (2026-11-16)

The redesign (`docs/redesign/`: `components.md`, `preview.html`, `src/screens.js`) draws 109 screens from 44
components. In the app each component is written once, in three layers, and a screen is its own area that asks them.
The player screen (a video, a song) is the first screen built this way; this note is for the next ones.

## The three layers

| Layer | Files | What it holds | Rule |
|---|---|---|---|
| Layout maths | `src/kitlayout.h`, `.cpp` (bpcore, namespace `lay`) | Where things go on 640x480 and how a cursor moves over them: `cell`, `transportCell`, `barKnob`, `barMarks`, `spread`, `sheet` / `Sheet::tile` / `Sheet::row` / `sheetMove`, `playerLayout`, `playerFocusMove`, `chipTrack` | No window, no SDL: pure functions of numbers, tested in `tests/core_test.cpp` (`testKitLayout`). `lay::Box` is `Ui::Rect` |
| Components | `src/ui_kit.h`, `.cpp` (`Ui::Kit`, owned as `ui.kit`) | Drawing: `caps`, `hints`, `key`, `transport`, `iconKey`, `chip`, `line`, `progress`, `bar`, `sheet`, `row`, `pill`, `toggle`, `lock`, `flat` | Stateless: a `Rect` and a small struct of inputs in, pixels out. No screen's state inside, no `Player` |
| The screen | its area (`src/ui_player.h`, `.cpp`: `Ui::PlayerPage`) and its overlays | The state (what is pressed, the focus, the lock), the keys, the touches, and `draw()`, which fills the components' inputs from the `Player` | As every area (docs/notes/ui-areas.md): redraw only on `signature()`, a sheet is a `Ui::Overlay` |

The one idea that makes touch and drawing agree: **a Rect is computed once, by a `lay::` function, and both `draw()`
and `pointer()` ask it.** `PlayerPage::rect(id, layout)` is the only place an id becomes a Rect.

## Type, colours, radii

- **Type** is a size index (`ui_internal.h`, `F_*`): `F_SMALL`, `F_NORMAL`, `F_LARGE` are every page's (Pixelify
  Sans 18, 24, 32); the kit's are the mock-ups' sizes: `F_P12` (a key cap), `F_P13` (a chip's value), `F_P14`
  (a transport key's and a tile's label), `F_P16` (a key's label, a line key's text), `F_P20` (a sheet's title),
  `F_P28` (the player's title), and Inter for what is read: `F_I11B` (small capitals, pills), `F_I12` (12.5: notes,
  hints, crumbs), `F_I14` (a sentence). Every text call takes one (`ui.drawTextMid(text, F_P16, ...)`). The kit's
  sizes are opened the first time one is drawn (`Ui::font`), with the same fallback fonts as the others. The design's
  17 and 25 are drawn with `F_SMALL` (18) and `F_NORMAL` (24). A new size is one line in `FONT_DEFS` and
  `Ui::FONT_COUNT`.
- **Colours** are `theme.h`'s by role, as before. The mock-up's variables map: `--bg` BG, `--sf` SURFACE, `--bt`
  BUTTON, `--pr` PRESSED, `--ac` ACCENT, `--tx` TEXT, `--dm` DIM, `--do` DIM_ON, `--dg` DANGER, `--tr` TRACK, `--ru`
  RULE; `--ed` and `--eds` are `edgeOf(BUTTON)` and `edgeOf(SURFACE)` (`Kit::edgeOf`).
- **New tokens** (`theme.h`, mirrored by hand in `web/style.css`): `R_CAP` 5, `R_TRACK` 7, `R_GRIP` 3, `R_SHEET` 18,
  `R_LOCK` 30; strengths in percent `VEIL_SHEET` 68, `VEIL_LOCK` 80, `OFF` 45, `HOLD_FILL` 28, `TINT` 20; `mix()`.
- **Unavailable** is one rule: pass `off = true` and the component draws itself at `OFF` percent (`Kit::off`). Don't
  pick a dim colour by hand.
- **A box with a lip** is a button (`ui.drawBox`); **a box without** is a surface (`Kit::flat`: chips, line keys,
  a focused row).

## Building a screen: the steps

1. **Layout.** Add a `lay::fooLayout(...)` returning a struct of `Box`es (and a `lay::fooMove` if a cursor moves in
   two dimensions). Rows of equal keys are `lay::cell(area, n, gap, i)`: it gives the spare units to the last cells,
   so "five of 115 + 116" needs no special case. Test it: every Box inside the panel, none over another, the row
   ending on the margin (`testKitLayout` does this for the player).
2. **The area.** `struct Ui::Foo : Ui::Area` in `ui_foo.h` (docs/notes/ui-areas.md has the steps). Its state is the
   least it needs: an `int pressed` (the touch id under the finger), the focus, whatever is the screen's own.
3. **draw().** Ask the layout, fill each component's struct, call the kit:

   ```cpp
   lay::PlayerLayout l = lay::playerLayout((int)chipList.size());
   Kit& k = *ui.kit;
   Kit::Key like;
   like.glyph = liked ? &G_HEART : &G_HEART_O;
   like.label = liked ? "Liked" : "Like";
   like.key = hotkeys ? "L3" : "";	// the cap only while the buttons work here (Button hotkeys)
   like.lit = liked;
   like.off = !usable(T_LIKE, p);
   like.pressed = pressed == T_LIKE;
   k.key(l.keys[0], like);
   ...
   k.hints({{"LEFT/RIGHT", "Seek"}, {"X", "More"}, {"B", "Back"}});
   ```

   Key names in caps: one name or several apart by `/` (`"L2/R2"`, `"X/B"`); `LEFT`, `RIGHT`, `UP`, `DOWN` are drawn
   as arrows. The hint bar is the last thing drawn (30 units at the foot); an overlay draws its own.
4. **pointer().** On a down: `pressed = id` of the Rect hit, if `usable(id)`. On the lift: act if the finger is
   still on it (slide off to cancel). A bar the finger moves is a slider (`ui.slideGrab`, docs/notes/sliders.md): pass
   the same track Rect `Kit::progress` drew. Reset the D-pad's focus on a touch (touch never shows focus).
5. **key().** Move the focus with the `lay::` function; `A` on the focused piece calls the same `act` the tap does.
6. **signature().** Everything `draw()` shows that `Ui::signature` doesn't already hold (it has all of `Settings`,
   the position by the second, the title, the tracks' ids): `pressed`, the focus, and anything read from elsewhere
   (the player's: what plays next, the song's tags). Something that animates adds a clock step.
7. **Sheets** are overlays (docs/notes/overlay-stack.md). For a menu, don't write one: call
   `ui.openMore(build)` with a function that returns a `MoreSpec` (tiles, rows, one or two columns; each item a
   glyph, a label, a note, a cap, and `act`). It is asked for each time the sheet is drawn or pressed, so a row says
   what it is now, and returning an empty list closes the sheet (what it was about has gone). For another kind of
   sheet, an overlay of your own: `lay::sheet({tiles, rows, cols, body})` gives the box, `Kit::sheet` draws the veil,
   the box, the grab mark and the header, `Kit::row` the rows, `Kit::hints` its hint bar (`PlayOnSheet` in
   `ui_player.cpp` is 100 lines).

## What the player needed that the kit did not have, and where it went

- The **play key's hold** (taking playback over from a browser) is `Kit::Transport::hold` (0..1) and `third` (the
  line under the label). The timing stayed `Ui`'s (`playHoldAt`: the mini bar's play mark holds the same way).
- The **lock** is the area's flag and `Kit::lock`'s drawing, not an overlay: an overlay takes the keys, and locked
  the buttons must go on working.
- The **header** is drawn by the area from `Kit::iconKey` and Ui's own status marks (`drawBattery`, asked to end
  left of the More key). A `Kit::header` (title, crumb, back, More, a right slot) is the first thing the next screen
  should add: every redesigned page has one.

## Not in the kit yet (components.md names them)

Section rail, the header with a title and badges, the mini bar's pills, tabs and filter chips, the settings row's
stepper, form rows, alert / confirm / busy sheets (today's `alert()` and dialogs still draw the centred box), tiles
with a summary, cover cells, cards, the empty state, the notice row, the top screen's cards. `Kit::bar`, `pill`,
`toggle` and `row`'s chevron and switch are written and drawn by Play on's sheet or nothing yet: checked by eye only
where a screen uses them.

## The player screen as built

`docs/redesign/src/screens.js`: `video`, `music`, `video-chips`, `video-more`, `play-on`, `lock`, the play key of
`browser-play` / `browser-hold`.

- **Keys:** A play / pause (hold: take over from a browser); LEFT / RIGHT 5 s back / 10 s forward; L / R previous /
  next; L2 / R2 chapters; DOWN onto the chips, then the Next line, UP or B back; X More; Y the list; SELECT the view;
  L3 Like; START the menu; B back (it keeps playing).
- **Still today's pages**, until their sheets are built: Y opens Up next (or a playlist's own page), SELECT opens
  Tracks (a video) or the Display popup (a song), the Subtitles / Audio / Picture chips open their tab of Tracks,
  Sound opens the Sound page, START opens the menu with its Playing tab.
- **Interim, until `video-list` and `video-speed` exist:** the Speed chip steps the speed round its five choices; the
  Repeat chip steps the old X round (In order, Loop all, Loop one, Shuffle) and reads Repeat Off / All / One or
  Shuffle On / One / Once. When List has its Shuffle switch and Repeat choice, both chips open their sheet instead.
- **The photo viewer** is `Page::Main` too and was not redesigned in this step: `PlayerPage` hands its keys, touches
  and drawing to Ui's `photoKey`, `photoDown`, `drawPhoto` (ui_main.cpp).

## Checked

Desktop build (macOS), `core-test` and `manga-test` pass. Scripted runs (`--press`, `BP_SHOTS`, generated media: a
90 s test card with four chapters, a second clip, two 75 s tones with a cover, two photos, a file of noise), before
(b4b1371's player) and after:

- a video: playing, paused, a chapter jumped (L2 / R2), the bar dragged, Next, the list, Info, Screen off (A pauses
  blind, X wakes it), Stop from More; the chips by touch and by DOWN / LEFT / RIGHT / A; the Next line by DOWN and A;
- a song: the same, Like by L3 and from More, the Sound page and the Display popup from their chips and from View;
- the lock: from More, a tap on a dimmed key (nothing), the hold on the lock (its bar filling), unlocked; More
  opened by X while locked (touch ignored), A on Unlock touch;
- a file that can't play: the keys that still work, the rest at 45%;
- Button hotkeys off: no caps, the keys by touch;
- a photo: its page byte for byte as before (the header's clock aside).
- `tools/web-smoke.sh` against the build.

## Not verified

- The handheld: every size is the mock-up's, tried on a Mac at twice the panel's pixels. Pixelify Sans at 12 and 13
  units on the real 640x480 panel (one pixel a unit) is the thing to look at first; the caps' arrows are 5 x 9 pixels
  there.
- A browser as the output seen on the handheld's own panel (the state "In Chrome on Mac", the play key's third line
  and its fill, Play on's sheet with a page in it): the code paths are the old ones behind new drawing, and the web
  smoke test exercises the Session's side, but no scripted run drew them.
- The List key's glow (a playlist's first song), a network error's Retry key, a song with chapters.
- The Linux build (no Linux-only file changed; `ui.h` gained `#include "kitlayout.h"`).
