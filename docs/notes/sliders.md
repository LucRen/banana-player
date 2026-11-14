# Sliders: one slider for every bar a finger moves (2026-11-15)

## The problem

Eight bars on the bottom panel took a finger, each with its own code and its own rules: the player's progress bar and
the Manga reader's page slider moved *relative* to where they were (a drag from anywhere moved by the finger's travel,
a tap jumped as it lifted), the quick settings' bars and the readers' bars were *absolute*, the Brightness sheet kept a
drag to its row, the EQ's bands had their own. Hit areas ranged from the whole row to the 22 units around a 10-unit
track, only the progress bar paced what it sent, and none showed that it was held.

## The slider

`src/slider.h` (no window, no SDL; tested in `tests/core_test.cpp`, `testSlider`):

- `SliderRange {lo, hi, step}`; `sliderValueAt(at, from, len, range, upright)`: the value under a finger at `at` on a
  track from `from`, `len` long, clamped to the ends and snapped to the steps (upright: the top is `hi`).
- `sliderFollow(was, finger, slack)`: where the slider is, given the finger: it follows the finger less `slack`
  either way, so a fingertip's shift as it lands or lifts moves nothing.
- `sliderShare(value, range)`: 0..1 for drawing. `SliderPace`: one value per `everyMs` to a costly target.

`Ui` (`src/ui.h`, `src/ui_slider.cpp`) holds one `Slide slide`: a finger holds one slider at a time.

```cpp
struct SlideSpec { Rect track; SliderRange range; bool upright = false; Uint32 everyMs = 0; };
using SlideSet = std::function<void(double v, bool last, Player& p)>;
void slideGrab(int id, int sub, const SlideSpec& spec, int x, int y, Player& p, SlideSet set, const Overlay* owner = nullptr);
bool slideHeld(int id) const;  bool slideHeld(int id, int sub) const;	// for drawing and signature()
double slideShare(int id, int sub, double rest) const;	// the finger's share while held, else `rest`
void drawSlider(Rect track, double share, bool held = false, const std::string& tag = "");
void drawSlideKnob(Rect restingKnob);  void drawSlideTag(Rect track, double share, const std::string& text);
```

What it does, the same for all:

- **Grab.** The page's (or the overlay's) own `Pointer::Down` code hit-tests the slider's *hit area* (the row, not
  the thin track) and calls `slideGrab` with the slider's current value (`SlideSpec::now`). A touch alone changes
  nothing and shows nothing (`slideHeld` is false until the finger slides).
- **Trackpad-style (the owner's call, 2026-11-15).** Past `DRAG_SLOP` of travel the value moves on from `now` by the
  finger's travel along the track (`sliderMoveBy`: the whole track is the whole range), so the knob needn't be under
  the finger. A tap, lifted without sliding, puts the value where the finger was. (The first version jumped to the
  finger on touch: on the handheld that read as "it can't be scrubbed".)
- **Kept until the lift.** `Ui::pointer` gives every move and the lift to `slideFollow` before anything else (before
  the overlays too), so the finger may drift off the row, over buttons, out of a sheet: only the slider follows, along
  its track's axis. Past an end of the track: that end's value.
- **The setter** hears `set(v, false, p)` on each change while held and `set(v, true, p)` once as the finger lifts.
  With `everyMs` (a seek, a page to decode) the held calls come one per `everyMs` at most, none for a plain tap, and
  `Ui::tick` sends the value a resting finger is on (`slideLive`; `animating()` keeps the loop awake for it). The lift's
  call always comes, with the exact last value. The progress bar is this with `SCRUB_MS`: `p.scrub` (keyframes) while
  held, `p.seekFraction` (exact) on the lift; `log.txt` gets `seek bar: let go at F after N scrubs`.
- **No wobble.** `SLIDE_SLACK` (6 units, under a millimetre): a tap stays where it touched, a drag ends where it was
  left. `slide.moved` (travel of `DRAG_SLOP` along the track) tells a tap from a drag for a setter that cares (the
  EQ's double-tap to 0 dB).
- **Let go without a lift** when what it was on is gone: a key pressed (`press()` calls `slideEnd`: the key may close
  the page; the last value is given), the page changed or an overlay came or went (`slideStale`: dropped, nothing
  given), a new touch while one is held (its lift was lost). The quick settings drop it when they open or close.
  The setter of an overlay's slider may capture the overlay: pass `this` as `owner`, and it is never called once
  that overlay isn't the top one.
- **signature()** has the held slider's id, sub and value (`|sl…`), so anything drawn from `slideHeld`/`slide.value`
  is redrawn as the finger moves. Nothing more to add per slider.

## How a held slider looks (every slider follows this)

At rest nothing changed (the screenshots are byte for byte the old ones). Under a finger:

1. **The knob grows** (`drawSlideKnob`: the resting knob's box, 3 more all round, a dark rim), so the finger knows it
   caught it. A bar with no knob at rest (the quick settings') gets one while held. The EQ's upright knob grows the
   same way, in the accent.
2. **The knob is where the finger has it** (`slideShare`), not where the target has got to: a paced target (pages)
   arrives a moment later.
3. **The value shows where the finger doesn't hide it.** If the row already says the value above or beside the bar,
   that text is lit in the accent and nothing is added (quick settings: the "75%" left of the bar; the Brightness
   sheet: the "100%" / "20% darker" over the bar; the EQ: the gain over the column). Otherwise a tag over the knob
   (`drawSlideTag`: an accent chip 28 units above the track, kept inside the panel): the progress bar's
   "0:47 / 1:00" (its chapter name stays under the bar, as before), the text reader's "76%", the pages' "Page 6 / 12"
   (the page lines of the Manga reader and the PDF header say the finger's page too while held, so the two agree).
4. **Lifted, the resting look again**, at once (the progress bar still rests where it was left until playback gets
   there: `seekHeld`, as before).

Colours and radii are `theme.h`'s (`KNOB`, `BG`, `ACCENT`, `ON_ACCENT`, `R_KNOB`).

**A slider in a list that scrolls** must not take a vertical scroll: there is none today (the quick settings and the
Brightness sheet don't scroll, Settings' rows step by arrows, the EQ page is fixed). One added later should not grab
on the touch: wait for the first move past `DRAG_SLOP`, along the track = `slideGrab` (from the landing point), across
it = the list's scroll, a lift before either = a tap that sets the value. Not written, since nothing needs it.

## Adding one

The rect it is drawn in, the area a finger takes it by, its range, a setter:

```cpp
// ui.h
enum { ..., SL_SPEED };	// with the other SL_* ids
Rect speedTrack() const { return {120, 300, 400, 10}; }
Rect speedHit() const { return {16, 276, 608, 56}; }	// the row

// the page's pointer code, on Pointer::Down
if (speedHit().hit(x, y))
{
	slideGrab(SL_SPEED, 0, {speedTrack(), {0.5, 2.0, 0.25}}, x, y, p, [this](double v, bool, Player& p) { p.setSpeed(v); });
	return false;
}

// its drawing
bool held = slideHeld(SL_SPEED);
drawSlider(speedTrack(), slideShare(SL_SPEED, 0, sliderShare(p.speed, {0.5, 2.0, 0.25})), held, held ? speedLabel(slide.value) : "");
```

Nothing in `pointer`'s dispatch, `press`, `tick` or `signature`. Upright: `{track, range, true}`. Costly: `{track,
range, false, SCRUB_MS}` and do the cheap thing for `last == false`, the exact one for `last == true`. Inside an
overlay: `ui.slideGrab(..., this)`.

## Which sliders use it

| Slider | Id | Hit area | Range, steps | Sent |
|---|---|---|---|---|
| Player's progress bar (`ui.cpp`, drawn in `ui_main.cpp`) | `SL_SEEK` | the bar, 20 above and below | 0..1 | scrub each 100 ms, exact seek on the lift |
| Quick settings: Volume, Brightness / Top / Bottom (`ui_menus.cpp`, `quickSlide`) | `SL_QUICK`, row | the row between − and + | 0..100 by 5; 10..100% by 10 | each change |
| Text reader's bar (`ui_docs.cpp`) | `SL_DOC` | from under the header to the drag area (44 tall, was 30) | 0..1 | each change (`TextReader::Jump`) |
| PDF / comic Vertical scroll's bar (`ui_docs.cpp`) | `SL_DOC` | the same | pages, by 1 | a page each 100 ms, the last on the lift |
| Manga reader's page slider, on the page and in fullscreen's sheet (`ui_manga.cpp`, `mangaSlide`) | `SL_MANGA` | the panel's width, 36 tall (was 560 x 22) | pages, by 1 | a page each 100 ms, the last on the lift |
| Brightness sheet's Screens / Top / Bottom / Extra dim (`ui_light.cpp`, an overlay) | `SL_LIGHT`, row | the whole row | 10..100 by 10; 0..80 by 10 | each change |
| EQ's ten bands (`ui_sound.cpp`), upright | `SL_EQ`, band | the band's column | −12..+12 dB by 1 | each change |

`drawSlider` draws the readers', the Manga reader's and the sheet's bars; the progress bar (chapter notches between
fill and knob), the quick settings' bar (no knob at rest) and the EQ's band keep their own drawing and use
`drawSlideKnob` / the same growth for the held look.

**What changed for the hand:** the progress bar and the Manga page slider were relative (a comment called that "as a
touch bar's slider"); they are absolute now, as the owner asked: the value jumps to the finger as it lands. The Manga
slider used to seek only as a tap lifted; it seeks at the touch.

## Left out

- Bars that only show progress: downloads, the Find job, the file job's card, the chapter row's line, History's rows,
  the level card. Not touched.
- The photo and PDF pan boxes, the lists' scrolling, the playlist's grip: drags in two dimensions or of rows, not
  sliders.
- The web dashboard's sliders (`web/`): the browser's own.

## Checked

Desktop build, the scripted harness (`--press`), which got three steps for this: `down:X:Y`, `move:X:Y`, `up:X:Y` (a
finger kept down across steps, so a `shot:` can catch the held look).

- `core-test`: `testSlider` (the ends, past the ends, the steps of each real slider, upright against the EQ's old
  `round((264 - y) / 8)`, the slack, the pace: a tap sends nothing early, a second of 8 ms moves sends at most 10).
- At rest, and after keys stepped each slider: bottom and top screenshots byte for byte those of the commit before,
  for the player, the quick settings, the text reader, its Brightness sheet, the PDF's Vertical scroll, the EQ, the
  Manga reader's page and its fullscreen sheet (a 12-page CBZ in a local `Manga` folder: the reader is reachable on a
  desktop build that way).
- Each slider driven: grabbed in its row off the thin track, the finger taken off the row (and out of the sheet),
  a shot while held, lifted a few units off: the value was the finger's (mpv's `time-pos`, the fake backlight's and
  volume's files, `settings.conf`'s `read-dim` and `eq-gains`, the shots). The progress bar over a 300 ms drag of ten
  moves: 2 scrubs, then the exact seek; a tap: no scrub, the seek.
- A key while a bar of the Brightness sheet is held (B): the sheet closes, the finger's later moves do nothing.

## Not verified

- The handheld's touch panel: the slack (6 units) and the tag's height over the knob (28) are chosen from the
  panel's size, not tried under a real fingertip. Both are constants (`SLIDE_SLACK`, `drawSlideTag`).
- The Manga reader with a source's chapter (pages fetched): only a local CBZ was driven. With the mini bar up
  (`miniUp`): the rects were checked by arithmetic only.
- The Brightness sheet opened from the PDF viewer and the Manga reader (the same `openLight()`); the Linux build
  (no Linux-only file was touched; `ui.h` gained one include of a header with no system dependency).
