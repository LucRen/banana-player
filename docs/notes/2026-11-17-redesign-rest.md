# The redesign's rest: the song's View, the header, and the smaller items (2026-11-17)

The last of the handover in `2026-11-16-redesign-kit.md`: the subtitle style's way back, a song's View sheet, and
step C (the header, the browser-output chips, the Next line's length, drag-to-scroll). Three commits:

| Commit | What |
|---|---|
| `15ab0bd` | B in Subtitle style returns to View on Subtitles (not the old Tracks page) |
| `61c4ba1` | A song's View: Display, Equalizer, Effects, Speed; the Display popup and the old Tracks / Chapters / Up next pages removed |
| `02706ca` | `Kit::header`, the "Handheld only" chips, the Next line's length, drag-to-scroll in List and View |

(`761474b` is the small header declaration and speed test `236637b` left out.)

## 1. Subtitle style back to View

`SettingsPage::menuBack` used to call `Ui::openTracks(TRACKS_SUBS)` (the old page). It now leaves the menu, makes the
player the page again and calls `PlayerPage::openView(0, p)`, which opens the View sheet on Subtitles. The style row
still closes View and opens Settings › Subtitles; B (or the menu's B) comes back. If the file ended while the style
was open there is no sheet to return to, so the menu closes as it would from any setting.

## 2. A song's View

`Ui::SongViewSheet` (`ui_player.cpp`, declared in `ui.h`), one `Ui::Overlay` named "songview":

- **Display**: rows, as the old popup had them — the four Show modes (Album art, Art + viz, Visualizer, None) and the
  offered visualizer styles (`VIS_SHOWN`, the popup's short names). A (or a tap) applies one at once; SELECT steps the
  Show mode, as SELECT did in the popup; B closes. The top screen follows (the player's own `setSettings`).
- **Equalizer**: the Sound page's ten bands in the sheet's body. The preset row has **L2 / R2** to step presets, the
  On/Off switch on **Y**, **A** Save, and **X** Options; LEFT / RIGHT choose a band, UP / DOWN change it by 1 dB. The
  touch screen drags a band (the same `SL_EQ` slider, double-tap back to 0 dB), taps the preset name for the list.
  Options, the preset list and Delete are the sheet's own rows, calling `Ui::Sound`'s existing `eqOptions`, `eqPick`,
  `eqReset`, `eqSave`, `eqNameOpen` and `p.deleteEqPreset`: what each choice does is unchanged. (The keys moved from
  the Sound page's Y/X and SELECT/START to L2/R2 and Y/X to make room for L / R as the tabs, as the mock-up has it.)
- **Effects**: rows from `SOUNDFX_COUNT`, `fxChoose` on A — the Sound page's own choice.
- **Speed**: the same five rows as a video's View, `p.setSpeed` + the osd.
- **Opening it**: SELECT on a song opens the tab last used (`PlayerPage::songViewTab`); **Sound** opens Equalizer,
  **Top screen** Display, **Speed** Speed. A song's Speed chip no longer steps: it opens the tab, like a video's.
- **Kept**: the Sound page itself (`Page::Sound`), reachable from Settings › Music › **Sound FX** and the settings'
  Sound row; its keys are unchanged. The web dashboard's `/api/eq`, `/api/fx` and the mini bar's `dialogOpen` still
  use it.

Removed with nothing reaching them any more: `Page::Tracks`, `Page::Chapters`, `Page::UpNext`, `openTracks`,
`stepTracksTab`, `tracksTabs`, `drawTracksHeader`, `openChapters`, `drawChapters`, `openUpNext`, `drawUpNext`,
`openMenu`; the Display popup (`displayOpen`, `displayRow`, `displayPending`, `displayKey`, `displayPointer`,
`drawDisplay`, its chips and constants) and `Ui::musicKey` (its SELECT branch was the popup; its X branch had no
caller). `Ui::rows`, `choose`, `STYLE_ROW` and `TracksTab` (Subtitles / Audio / Picture) stayed: the View sheets use
them. `Ui::modeLabel` stays (the dashboard). `upsNextList` / `playUpNext` / `chapterStep` stay (the List sheet).

## 3. Step C

- **`Kit::header`** (`ui_kit.h/.cpp`): takes the player layout and a `Header` (title, crumb, crumb mark, back and
  More with their pressed state, the title's state and mark, and a `right` callback). The callback draws the strip's
  right slot (the place in the list and Ui's status marks) from the right edge leftwards and returns its left edge,
  so the crumb ends before it. `PlayerPage::draw` fills it and draws nothing of its own for the strip or the title.
- **Browser output** (`ui.predicting(p)`): the Sound and Top screen chips of a song, and a video's Picture, say
  **Handheld only** and are drawn off (`look.off`); `chipAct` answers "Plays on the handheld only" by `p.osd`. The
  other chips and the keys are as they were.
- **The Next line**: `nextUp` asks `cachedDuration` for the next item and puts `formatTime(d)` in the note; with no
  cached length it keeps the place ("2 of 7", "· shuffled"). Since `nextUp`'s note is in the area's signature, a
  length that arrives (the lists' prober) redraws the line.
- **Drag-to-scroll**: the List and View sheets remember the finger's start row and scroll; a move of more than 12
  units turns the press into a drag, `scroll` follows the finger at one row per `lay::LIST_ROW`, `settle` leaves it
  alone until the lift, and the lift chooses nothing (the cursor goes to the row under the finger). The song's View
  rows have the same. A tap that does not move still chooses, on the lift.

## 4. The chips are touch only

Owner feedback: the D-pad could land on the chips row, and a stray press put a focus ring there. `PlayerPage`'s focus
lost its chip zone: **DOWN** now goes straight to the **Next** line, **UP** or **B** comes back to the playing keys,
and **LEFT / RIGHT** never leave it (with nothing lit they seek, as before). No chip draws a focus ring from a key; a
tap still does what it did. `lay::PlayerFocus` lost `chip` and `playerFocusMove` lost its chip moves (and the layout
tests with them). The hint bar says **DOWN Next line** rather than **Chips**. **SELECT** · View and **Y** · List are
unchanged, and a chip still opens the tab it did.

## Checked

Desktop build (macOS): `cmake --build build -j` clean apart from the existing settings enum warnings;
`./build/core-test` ends `core: all passed`; `cmake --build build --target manga-check` prints `manga: all passed`.

The chips are touch only: on a song and on a video, **DOWN** lights the Next line with no chip ring and the hint bar
reads **A List** / **B Back to playing** (`redesign-shots/chips/song-down-bottom.png`,
`video-down-bottom.png`, not committed; generated sine MP3 and testsrc MP4).

Scripted runs (`--press`, `BP_SHOTS`; generated media only: `ffmpeg` testsrc + quiet sine, an SRT beside it, a
20-chapter remux, a 9-audio-track MKV, and two sine MP3s with title/artist/album tags). No copyrighted media and none
of the owner's files.

- a song: SELECT on Display; the Equalizer tab (preset row, bands, On/Off, Save as); its Options and the preset list
  (with a Custom preset); Effects; Speed (cursor on the current speed); the Sound chip opens Equalizer, Top screen
  Display, Speed Speed; the player after closing;
- a video: the header (`player-header`), the List sheet's Chapters tab before and after a finger drag, View on
  Subtitles, the style row to Settings and B back to View; a 9-track Audio tab before and after a drag;
- a song's Display tab after a drag (the rows scroll, nothing chosen);
- the Next line after the next item's duration was known: `0:45` rather than "2 of 4".

Shots are under `redesign-shots/rest/` (not committed): `player-header-bottom.png`, `list-chapters-bottom.png`,
`list-chapters-drag-bottom.png`, `video-view-bottom.png`, `video-subtitle-style-bottom.png`,
`video-view-back-bottom.png`, `video-audio-bottom.png`, `video-audio-drag-bottom.png`,
`song-view-display-bottom.png`, `song-view-eq-bottom.png`, `song-view-presets-bottom.png`,
`song-view-effects-bottom.png`, `song-view-speed-bottom.png`, `song-player-bottom.png`,
`song-display-drag-bottom.png`, `song-chip-speed-bottom.png`, `nextlength-bottom.png`.

## Not verified

- **A browser page as the output on the handheld** (the "Handheld only" chips and `chipAct`'s osd): this session's
  shell can run the player but not the dashboard's client or `tools/web-smoke.sh`, so `ui.predicting(p)` was not
  driven. The code path is one `if` in `chips()` and one in `chipAct`.
- **The handheld's panel** at one pixel a unit: the equalizer's 12-unit values and frequency labels and the 9 bands
  beside them are the first thing to look at. (A finger on a band did not move it: the slider was grabbed with no
  overlay owner; fixed in `2026-11-17-song-view-eq-drag.md`, and a band was then dragged and tapped on the desktop
  panel. The sheet's own drag needs a real touch screen.)
- `SettingsPage::menuBack`'s return to View is checked with a video that has no subtitle tracks (the style row is the
  second row there); with several tracks the cursor is not put back on the style row (the sheet opens on Subtitles,
  as designed).
- Opening a **folder** directly (rather than a file) fell back to the browser in this build (`Failed to recognize
  file format`) during these runs; none of these commits touch file opening, and the shots use a file. Not chased.
