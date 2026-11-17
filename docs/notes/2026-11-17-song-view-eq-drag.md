# A song's View: the equalizer's bands under a finger (2026-11-17)

Owner feedback: in a song's View sheet (`Ui::SongViewSheet`, `src/ui_player.cpp`), on the Equalizer tab the ten bands
would not move under a finger. The keys worked (LEFT / RIGHT choose a band, UP / DOWN change it); a drag and a tap
did nothing.

## Root cause

The sheet's `bandGrab` already called `Ui::slideGrab(SL_EQ, ...)` with the band's column as the hit area and the
Sound page's own `eqSetBand` as the setter — but it left `SlideSpec`'s `owner` at its default `nullptr`. `slideStale`
(`ui_slider.cpp`) lets go of a slider when `topOverlay() != slide.owner`, and inside a sheet `topOverlay()` is the
sheet, never null. So the first `Move` after the touch dropped the slide (`slideStale` → `slideDrop`) before the
setter was ever called, and the lift did nothing either: `slideFollow` had already returned. `docs/notes/sliders.md`
has the rule ("Inside an overlay: `ui.slideGrab(..., this)`"); this call predates the sheet.

## Fix

One argument: `bandGrab` passes `this` (the sheet) as the slider's owner. The rest is the app's existing slider, so a
band now behaves like the Sound page's: a press grabs it, a vertical drag moves its gain from where it was by the
finger's travel (the column under the finger becomes `ui.sound->eqBand`), a tap puts the gain where it touched, and
the lift keeps it. A second tap within 350 ms still returns the band to 0 dB (the Sound page's).

The sheet's drag-to-scroll cannot steal the finger: `Ui::pointer` hands every move and the lift to `slideFollow`
before the overlay sees them, and on the Equalizer tab the band establishes no `P_ROW` press anyway.

## Cost

Each 1 dB change goes through `Ui::Sound::eqSetBand` → `p.setSettings(s, false)` → `syncEq(false)`, the D-pad path.
`syncEq` is live once `@bpeq` is in `af`: an `af-command` per band that moved, no rebuild (`prop("af", ...)`), and
only the first change of a drag can rebuild by adding the EQ to a chain that lacked it. The slider's own step is 1 dB
over a ~134-unit track, so at most one change per ~8 units of travel; this is the existing `SL_EQ` slider's pacing
(each change), as `docs/notes/sliders.md`'s table says. The held band's value is in `Ui::signature` (`|sl…`, the
sheet's settings included), so the sheet redraws while dragged.

## Effects

The sheet's Effects tab is rows (`SOUNDFX_COUNT`, one choice each) with no bar; nothing to scrub there. The Sound
page's Effects tab is the same rows.

## Checked

Desktop build (`cmake --build build -j`), `./build/core-test` ends `core: all passed`, `cmake --build build --target
manga-check` prints `manga: all passed`.

Scripted runs (`--press`, `BP_SHOTS`, a generated sine MP3; `redesign-shots/eq/`, not committed):

- `Select,R,down:290:350,move:290:310,shot:eq-drag,up:290:310,shot:eq-after`: the 500 Hz band went 0 → +6 dB, EQ on
  by the edit, held knob drawn grown in the accent during the drag, resting look after; `settings.conf` ends
  `eq-gains = 0,0,0,0,6,0,0,0,0,0` and `eq = on`;
- `Select,R,tap:412:330,shot:eq-tap`: a tap put the 2k band at +4 dB (the value under the finger), then
  `down:229:350,move:265:382` moved the 250 Hz band to −5 dB while drifting 36 units sideways (only the vertical
  travel counts); `eq-gains = 0,0,0,-5,6,0,4,0,0,0`;
- the log shows the one `af` rebuild that adds the EQ to the chain and then no rebuilds, the later changes going as
  `af-command`s.

## Not verified

- The handheld's touch panel: the band is 40 units wide and 174 tall, the slack 6 units — chosen from the desktop
  panel's size, not tried under a real fingertip (as `docs/notes/sliders.md` says of all sliders).
- The sheet's drag-to-scroll with a finger that lands on a band and then moves far enough sideways to leave its
  column: `slideFollow` keeps it (that is the design), so only a drag started off the bands scrolls.
