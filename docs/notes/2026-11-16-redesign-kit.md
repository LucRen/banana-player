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
- The **header** is `Kit::header` (title, crumb, back, More, a right slot): the area fills it and its right slot is a
  lambda that draws Ui's status marks (`drawBattery` and the place in the list) and returns their left edge, so the
  crumb ends before them. Every redesigned page uses it.

## Not in the kit yet (components.md names them)

Section rail, the header's badges (the title, crumb and right slot are `Kit::header` now), the mini bar's pills,
filter chips used as filters, the settings
row's stepper, form rows, alert / confirm / busy sheets (today's `alert()` and dialogs still draw the centred box),
tiles with a summary, cover cells, cards, the empty state, the notice row, the top screen's cards. `Kit::bar`, `pill`,
`toggle`, `row`'s chevron and switch, `tabs` and `segment` are written and drawn by Play on's sheet or the List sheet:
checked by eye only where a screen uses them.

## The player screen as built

`docs/redesign/src/screens.js`: `video`, `music`, `video-chips`, `video-more`, `play-on`, `lock`, the play key of
`browser-play` / `browser-hold`.

- **Keys:** A play / pause (hold: take over from a browser); LEFT / RIGHT 5 s back / 10 s forward; L / R previous /
  next; L2 / R2 chapters; DOWN onto the chips, then the Next line, UP or B back; X More; Y the list; SELECT the view;
  L3 Like; START the menu; B back (it keeps playing).
- **SELECT · View**, one sheet for either: a video's Subtitles, Audio, Picture, Speed; a song's Display (the Show mode
  and the visualizer's style), Equalizer, Effects, Speed. The track chips open their tab, Sound opens Equalizer, Top
  screen Display, Speed Speed. Y opens the List sheet (or a playlist's own page); START the menu with its Playing tab.
  The old Tracks page and Display popup are gone, and so are the Chapters and Up next pages the List sheet replaced.
  Adjust and the picture looks are new features, not built; a video's Repeat chip opens the List sheet.
- **The header** is `Kit::header` (back, crumb, place, status marks, More, title and state): every next page says a
  title and a crumb through it.
- **The photo viewer** is `Page::Main` too and was not redesigned in this step: `PlayerPage` hands its keys, touches
  and drawing to Ui's `photoKey`, `photoDown`, `drawPhoto` (ui_main.cpp).

## Checked

Desktop build (macOS), `core-test` and `manga-test` pass. Scripted runs (`--press`, `BP_SHOTS`, generated media: a
90 s test card with four chapters, a second clip, two 75 s tones with a cover, two photos, a file of noise), before
(b4b1371's player) and after:

- a video: playing, paused, a chapter jumped (L2 / R2), the bar dragged, Next, the list, Info, Screen off (A pauses
  blind, X wakes it), Stop from More; the chips by touch and by DOWN / LEFT / RIGHT / A; the Next line by DOWN and A;
- a song: the same, Like by L3 and from More; the Display popup and the Sound page from their chips and from View
  (since `61c4ba1`: View's Display, Equalizer, Effects and Speed tabs, `2026-11-17-redesign-rest.md`);
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

## Handover (2026-11-16): where it stopped, and the steps left

**All of steps A, B and C below are done** (2026-11-17). Everything is committed on `redesign`, builds, and
`core-test` / `manga-test` pass. What is left is in `2026-11-17-redesign-rest.md` ("Left for later"): the photo screen,
Adjust and the picture looks (new features), and what no scripted run could reach.

### Done (commits on `redesign`, oldest first)

1. `5a04cc4` the layout maths (`src/kitlayout.*`, bpcore) and `nextPlace()` (`src/session.*`), with tests.
2. `956e522` the kit (`src/ui_kit.*`), the More sheet (`src/ui_more.cpp`), the player area (`src/ui_player.*`), the
   theme tokens, fonts and glyphs, the old `drawMain` and its ids removed, FEATURES.md.
3. `363fd99` this note, ui-areas.md, architecture.md, CLAUDE.md.
4. `ecb11b6` a dimmed Next still answers a tap.
5. `26c36b9` Step A: Y · List as a sheet (the tabs, the Shuffle switch and Repeat choice, Chapters beside Up next),
   with `2026-11-16-redesign-list.md`.
6. `236637b` + `761474b` Step B's video part: SELECT · View as a sheet over the player — Subtitles / Audio / Picture /
   Speed, radio rows, Speed set at once; the track chips open its tabs and a video's Speed chip opens Speed. See
   `2026-11-17-redesign-view.md`. (`761474b` is the header declaration and the speed test the first commit left out.)
7. `15ab0bd` Step B's follow-up: B in the subtitle style returns to View on Subtitles (`SettingsPage::menuBack`).
8. `61c4ba1` Step B for songs: View's Display, Equalizer, Effects and Speed tabs; the Display popup, the old Tracks
   page and the unreachable Chapters / Up next pages removed. Sound chip → Equalizer, Top screen → Display, Speed →
   Speed. See `2026-11-17-redesign-rest.md`.
9. `02706ca` Step C: `Kit::header` in `PlayerPage::draw`; the "Handheld only" chips while a page plays it; the Next
   line's length; drag-to-scroll in the List and View sheets.

Of the brief's scope: the kit and the player screen (X › More) are whole; so are the List sheet, both View sheets,
the header, and the smaller step C items. Adjust and picture looks were never in this step.

### Build and check

```sh
cd <the worktree>   # .claude/worktrees/redesign, branch redesign; never the main checkout, never push
cmake -S . -B build -DCMAKE_OSX_SYSROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX26.5.sdk \
  -DPDFIUM_DIR=<main checkout>/.cache/mac/pdfium
cmake --build build -j
./build/core-test                       # ends "core: all passed" (it prints swscaler noise first)
cmake --build build --target manga-check   # "manga: all passed"
```

A scripted run (generated media only; make the tones quiet, `-af volume=0.01`: the Mac plays them aloud):

```sh
# run.sh BIN OUTDIR FILE STEPS [settings lines...]
rm -rf "$OUT"; mkdir -p "$OUT/state" "$OUT/shots" "$OUT/home"; : > "$OUT/state/settings.conf"
for l in "$@"; do echo "$l" >> "$OUT/state/settings.conf"; done
cd "$OUT" && BP_SHOTS="$OUT/shots" BP_BROWSE="$LIB" HOME="$OUT/home" sandbox-exec -p \
 '(version 1)(allow default)(deny network-outbound)(allow network-outbound (remote ip "localhost:*"))(allow network-outbound (remote unix-socket))' \
 "$BIN" "$FILE" --state-dir "$OUT/state" --press "$STEPS" > "$OUT/log.txt" 2>&1
```

Steps that cover the player: `shot:a,A,shot:paused,R2,X,shot:more,A,shot:locked,X,A,Down,Right*3,A,shot:speed,Down,
shot:nextline,B` (a video with chapters: `ffmpeg -f lavfi -i testsrc=... -i ch.txt -map_metadata 2`, `ch.txt` an
`;FFMETADATA1` file of `[CHAPTER]` blocks). Shots are 1280x960 on a Mac (twice the panel): the layout's units are half
the pixels. Taps are in units: Play is `tap:320:225`, More `tap:602:27`, back `tap:30:27`, the chips' row y 364, the
Next line `tap:300:423`, the More sheet's tiles y 308 (x 95, 250, 404, 560). The scratch copies of the helper
(`run.sh`, `contact.sh`) and the media are in the session's scratchpad under `redesign-work/`; they may be gone.
The dashboard: start the player with `BP_WEB_FORCE=1 BP_WEB_PIN=ACDE BP_WEB_PORT=8093 BP_BROWSE=<lib>` and
`--press` of a few hundred `info` steps, then `tools/web-smoke.sh http://127.0.0.1:8093` (202 passed with the reader
off; the songs must be longer than 30 s).

### Behaviour that must stay (each was checked unless "not driven" says otherwise)

- **Every play / pause / step goes through the Session** (`SessionHost::play`, `step`, `playLive`, `seekLive`): the
  bodies in `PlayerPage::key` are the old lines of `Ui::press`, moved. Don't call `p.togglePause()` alone.
- **Hold A to take over from a browser:** `ui.playHoldAt` is set in `PlayerPage::key` (A) or `pointer` (the play
  key, with `playHoldTouch`), `Ui::release` and the lift toggle the browser, `Ui::tick` takes over at
  `PLAY_HOLD_MS` and calls `playerPage->dropTouch()`. The mini bar's play mark shares this state: it stays `Ui`'s.
  Not driven here (needs a dashboard page as the output).
- **Previous's 5-second rule** is the Session's `Step`; **L2 / R2** are handled in `Ui::press` before any area.
- **The dark panel:** `Ui::press` lets A, LEFT, RIGHT, L, R, L2, R2, L3 through blind over the player; anything else
  wakes it. `PlayerPage::key` clears the focus while the panel is off, so LEFT / RIGHT seek. UP / DOWN left that list
  on purpose (they move a focus now).
- **Lock:** the state is `PlayerPage::touchLocked`; `Ui::pointer` sends every touch to `lockPointer` while
  `locked(p)` on `Page::Main`; `Ui::draw` calls `lockTimer()` before the signature; leaving the player unlocks
  (`showBrowser`, `backFromPlayer`). The sheets ignore touch while locked (`ui.touchIsLocked()`).
- **The photo viewer** is `Page::Main` with `p.playingPhoto()`: `PlayerPage` returns into `ui.photoKey`,
  `ui.photoDown`, `ui.drawPhoto` first thing. Its page is byte for byte the old one; keep that until the photo
  screen is redesigned. The mini bar shows over it and takes its touches before the area.
- **Loop / Shuffle:** two lasting settings; `ui.musicKey(Button::X, p)` is the one round (In order, Loop all, Loop
  one, Shuffle with Loop all) and the Repeat chip calls it. `modeLabel` is used by the dashboard's routes.
- **List (Y)** on a playlist's song leaves the player for the playlist's page (`showSourceAfterLeave`, return true);
  else Up next. The List key's glow: `listPulseArm` is set by `Ui::showPlayer`.
- **`--press` names** are untouched (main.cpp); `touchIsLocked()` still feeds its state line.

### Traps found

- `ui.kit` is a member: the layout namespace is `lay`, not `kit`, because `kit::` inside `Ui` would name the member.
- `Ui::repeats` knows nothing of overlays: don't make it return false under one (the Brightness sheet and quick
  settings rely on held LEFT / RIGHT).
- `Ui::press` asks the overlay before the dark-panel rule, so a sheet open on a dark panel would take keys blind:
  the screen-off timer in `Ui::tick` now waits while any overlay is open (`!topOverlay()`), and the Screen off tile
  closes the sheet first.
- An overlay has no `alive()` hook. The More sheet closes itself in `draw()` when its list comes back empty
  (`moreSpec` returns none when nothing plays or the page isn't `Main`); a new sheet over the player needs the same
  guard (`PlayOnSheet::draw` has it).
- An area's `pointer` sees moves and lifts only when nothing of `Ui`'s holds the finger (`pressedBtn`, a slider, a
  pan). So the area keeps its own `pressed`, and anything in `Ui` that swallows a touch must also call
  `playerPage->dropTouch()` (`Ui::dropTouch`, the take-over in `tick`).
- `cell()` gives spare units to the last cells; a hand-written `x + i * (w + gap)` will be one unit off there.
- The 45% "off" look is `Kit::off` over black. On SURFACE (a sheet) it is slightly darker than the design's
  opacity; nobody has complained, but a row off inside a sheet is the place to look.
- `FONT_COUNT` (ui.h) must equal `F_COUNT` (ui_internal.h): a `static_assert` says so.
- Adding a glyph: `GLYPH(G_NAME, ...)` in `ui_glyphs.h`, then run `tools/make-sprite.py` (it rewrites
  `web/sprite.svg`).
- Don't write generated media into a shared scratch `lib/`: use a folder of your own (this run overwrote the two
  `Music/Yellow Album` tones in the session's shared scratch library with 75 s ones that carry a cover).

### The steps left, each small and testable

**A. Y · List as a sheet** (`docs/redesign/src/screens.js`: `video-list`, `video-chapters-long`). **Done: see
`2026-11-16-redesign-list.md`.** The steps below are what it took.
1. `kitlayout`: `lay::tabs(area, widths, gap)` (44 tall, 6 apart, caps at both ends) and a `lay::listSheet(rows
   shown, header)`; a pure `lay::listScroll(cursor, scroll, shown, n)`. Tests in `testKitLayout`.
2. `ui_kit`: `Kit::tabs(Rect, labels, on, keys)`, `Kit::segment(Rect, options, on)` (Repeat Off / All / One), and use
   `Kit::toggle` for Shuffle.
3. `ui_player.cpp`: `struct Ui::ListSheet : Ui::Overlay` with two tabs: Up next (rows from `ui.upNextList(p, cur)`,
   A = `ui.playUpNext(i, p)`) and Chapters (rows from `p.chapters`, A = the seek `Ui::choose` does on
   `Page::Chapters`: read it in `ui_menus.cpp`). Header right: the Shuffle switch (`SessionHost::setShuffle` plus the
   setting) and Repeat (`p.setSettings` with `loop`): read `Ui::musicKey` and the dashboard's
   `api_player.cpp` (around `modeLabel`) for how each is set alone. L / R step the tabs, L2 / R2 pass through to the
   chapter step (call the block in `Ui::press`, or move it into a `Ui::chapterStep(dir, p)`), Y / B close.
4. Point `PlayerPage::listAct`, `T_LINE`, the Next line and the Repeat chip at it; keep "a playlist's song opens the
   playlist's page" (`listAct`'s first branch). Then the Repeat chip reads only Repeat, and a song's fourth chip is
   Shuffle (the mock-up).
5. Shots: both tabs, 42 chapters (a generated ffmetadata), a long name, Shuffle on, each Repeat.

**B. SELECT · View as a sheet** (`video-view`, `video-speed`, `video-picture`, `music-view`, `music-eq`). **Done.**
1. **Video: `2026-11-17-redesign-view.md`.** `struct Ui::ViewSheet : Ui::Overlay`, tabs Subtitles / Audio / Picture /
   Speed (rows from `ui.rows(p)` per `tracksTab`: `Ui::choose` has each row's action; a track is chosen by id,
   `chooseTrack`), radio rows (`Kit::Row::radio`, `G_RADIO` / `G_RADIO_ON`). Speed: five rows from `format.h`'s
   `speedChoices()`, `p.setSpeed` + the osd.
2. **Video**: SELECT opens it on the tab last used; the track chips open their tab and the Speed chip opens Speed.
3. **Song: `2026-11-17-redesign-rest.md`.** `SongViewSheet` with Display, Equalizer, Effects and Speed: the Display
   popup's Show modes and styles, the Sound page's bands and effects (its own actions), and the same five speeds.
   The old Tracks page and Display popup are removed; the Sound page stays for Settings.
4. "Adjust" and picture looks in the mock-ups are new features, not in this scope.

**C. Smaller, independent.** **Done.**
- `Kit::header(title, crumb, back, more, right slot)` and use it in `PlayerPage::draw` (it was drawn by hand there).
- Browser output on the panel, per `browser-play`: dim the Sound / Top screen (and a video's Picture) chips with
  "Handheld only" as the value while `ui.predicting(p)`: `chips()` sets `look.off` and the value; `chipAct` then says
  "Plays on the handheld only" by `p.osd`. (Not driven: no browser page is reachable with this session's shell.)
- The Next line's note is the item's length when `DurationCache` has it (probed from the browser or an earlier play),
  else its place ("2 of 7").
- Move the photo viewer's state and code from `Ui` (ui_main.cpp) into the area or a struct of its own when the photo
  screen is redesigned (`photo` in screens.js), not before.
- The owner's open question: Stop is one press further (X, then the red tile). If a Stop key should be back on the
  main screen, the place is the second row (`lay::playerLayout`'s `keys`: five of them instead of four, or Stop in
  place of Playlist, which More also has): `T_STOP`, `usable`, `act` (`ui.stopPressed = true`), one `Kit::Key` with
  `Tone::Danger`.
