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

## 5. The four follow-ups (2026-11-17, later)

The brief after the rest: the Settings header, Stop on the player's face, Display and Visualizer apart, and the
support link.

| Commit | What |
|---|---|
| `7b454df` | Settings/About: the kit's header on the group list and every group page; the old tabs (and About as a tab) gone |
| `33f3bb7` | The player's row: Like · Playlist · List · Stop; SELECT keeps View; UP / DOWN do nothing |
| `73fe8c6` | A song's View: Display and Visualizer are two tabs and two choices; the style grid; the Top screen chip names the mode only |
| `2bf1926` | About's support line and static QR, shipped in the bundle; the dashboard's Home foot |

### 1. Settings header

`SettingsPage::draw` and `Ui::drawInfo` (About) fill a `Kit::Header` from `lay::playerLayout(0)`: the back key, the
title, a crumb ("Banana Player 0.7.27" on the tiles; "Settings · 3 of 10" in a group) and, in a group, the neighbours'
names either side of L2 / R2 in the right slot. `Kit::header` now takes the panel's right margin when there is no More
(it used the More key's left edge, which would have left the status marks 50 units short). The old tab strip is not
drawn on `Page::Settings` or `Page::About`, L / R do not switch menu tabs there, and `menuTabs` no longer offers About
(About is the last group; `TAB_ABOUT` is gone). B on About goes to the tiles and L2 / R2 step the groups, always: the
old `menuTab != TAB_ABOUT` cases are gone.

The tiles start at 100 and a group's rows at 100 (`SET_TILE_TOP`, `SET_ROW_TOP`); About shows nine rows at a time
(`ABOUT_Y`, `ABOUT_SHOWN`) and keeps its foot clear. The group pages' pressable neighbour boxes are gone (with
`GROUP_PREV` / `GROUP_NEXT` and their ids). One bug fixed on the way: `SettingsPage::key` clamped the settings list's
scroll after opening a group, so About opened scrolled to row 4; it now clamps only while a row group is on screen.

### 2. The player's row

The second row is Like · Playlist · List · Stop. Stop is the fourth key, at the right edge, in plain ink (Kit's normal
key, not Danger), and a tap sets `ui.stopPressed`, the same flag More's red Stop tile sets (that tile stays). SELECT
still opens View, and the hint bar says **SELECT View**. UP / DOWN return at once in `PlayerPage::key`; the Next line
has no focus (a tap still opens its list). `lay::PlayerFocus`, `playerFocusMove` and the area's `focus` are removed,
and `T_VIEW` became `T_STOP`.

### 3. Display and the Visualizer style

A song's `SongViewSheet` has five tabs: Display, Visualizer, Equalizer, Effects, Speed. Display is the four Show modes
as radio rows, with the spec's names (Album art only, Art + visualizer, Visualizer only, None) and its notes, then a
**Visualizer style** row (the style's glyph, its name, a chevron) that opens the Visualizer tab; while the mode shows no
visualizer it reads "<style> · not shown". The Visualizer tab is a 5 × 4 grid of the twenty offered styles
(`lay::gridCells`, a new pure function tested in `testKitLayout`); the style under the cursor is applied at once
(`p.setSettings`, the 500 ms graph debounce keeps it bearable), A keeps it, and a notice over the grid says "Not on the
top screen now · Top screen shows: Album art only. Your choice is kept." with **Show it** (Y) when the mode shows none.
The chip indices moved (Sound → Equalizer 2, Speed 4). The Top screen chip's value is `musicShowLabel(show)` only.
`SHOW_LABELS` is the spec's four; the offered Peaks Banana style is labelled "Peaks" and Popcorn machine "Popcorn"
(`VIS_LABELS`), and Settings' row is **Visualizer style**.

### 4. Support

About's foot (`Ui::drawAboutSupport`) draws a rule, the static QR (`packaging/support/kofi-qr.png`, committed), the
sentence "Banana Player is free. If you’d like to support it:" (U+2019) and `ko-fi.com/tonysong`. The image goes through
`decodePageFile` into a cached texture, first from the fonts' folder one up (the bundle's
`share/banana-player/kofi-qr.png`) and then from the source tree (`fontsDir/../../packaging/support/`); `build-bundle.sh`
copies it beside `icon.png`. Nothing on About is a key.

The dashboard's state JSON gains `version` (`BP_VERSION`, in `snapshotJson`), and `web/js/home.js` puts one dim
`.hm-foot` line at the foot of Home: `Banana Player <version> · Source · Support Banana Player`; Source and Support are
`<a target="_blank" rel="noopener">` to the repository and `https://ko-fi.com/tonysong`. `web/style.css` has the rule
and the links' dim colour.

### Checked

Desktop build (macOS), `./build/core-test` ends `core: all passed` (the grid cells' test and the "Peaks" label are
new), `cmake --build build --target manga-check` prints `manga: all passed`. Scripted runs with generated media only
(testsrc video + quiet sine; a 75 s sine MP3 with a generated cover and tags):

- Settings: the group list (kit header, no tabs), Playback, Music & sound ("Music display", "Visualizer style": Peaks);
  About with the QR, the rule, the sentence and the address;
- a video: the row Like · Playlist · List · Stop, the hint bar SELECT View; Down / Up change nothing; SELECT opens View;
- a song: the row; the Top screen chip "Album art only"; View › Display (four radio rows, "Peaks · not shown", the
  style row); **R** to Visualizer (the notice, Show it (Y), the 20-style grid, Peaks outlined); Y shows the visualizer
  (the notice goes, the grid grows); L back to Display ("Peaks", no "not shown");
- the dashboard's `/api/state` answered `version: 0.7.27` after a login; `node --check web/js/home.js` passes.

Shots under `redesign-shots/q4/` (not committed): `settings-bottom.png`, `music-group-bottom.png`, `about-bottom.png`,
`video-bottom.png`, `song-bottom.png`, `song-display-bottom.png`, `song-viz-bottom.png`, plus
`song-display-shown-bottom.png` and `song-viz-shown-bottom.png` for the after-Y states.

The arm64 bundle: `packaging/build-bundle.sh --quick` (Docker) was run once at the end; it compiles the arm64 build.

### Not verified

- **The dashboard's Home foot in a browser**: the API's version was read and the module is syntactically valid, but
  headless Chrome against the running server (with `#pin=`) did not finish while the page's SSE stream was open, so no
  screenshot of the line was taken. The CSS is two rules in `web/style.css`.
- The handheld's panel at one pixel a unit: the 84-unit QR, the 12-unit support sentence, the 20 grid labels at 12.5
  and the settings header's crumb are the first things to look at.
- Scanning the QR with a phone (the image is the address; it was not regenerated here).
- A real finger on the Visualizer grid (the taps and the keys were driven; the grid's cells are the layout's).

## 6. The vizlite merge, START's places, the lid key rows, About's bar (2026-11-18)

The brief after the four follow-ups. Four commits:

| Commit | What |
|---|---|
| `9272273` | Merge `vizlite` into `redesign` |
| `fc4b17f` | START: the Menu of places, with no tabs |
| `7afd10b` | Settings: Power key, lid shut and Volume keys, lid shut |
| `de6552b` | About: the kit hint bar; its menu tile reads its lines |

### 1. Lite in the redesign

The merge kept both sides: the redesign's display modes and settings groups, and Lite (the player-drawn visualizer,
`src/vizlite.h`, `TopScreen::drawLiteFrame`, the `@bplite` af tap). Lite is the first style in `VIS_SHOWN` and in
Settings' **Visualizer style** row, and the Visualizer tab's grid now has 21 cells (a fifth row of one, its cells still
46 tall). It works with **Art + visualizer** (mpv's art behind it) and **Visualizer only** (`vid=no`, black). The
conflicted files were `FEATURES.md`, `src/main.cpp`, `src/player.cpp`, `src/settings.*`, `src/ui.h`, `src/ui_internal.h`
and `src/ui_sound.cpp`; the old display popup (`Ui::displaySet`, `drawDisplay`, `musicKey`) stays deleted, as the
redesign already removed it.

Checked: a song with generated art, Visualizer only, Lite chosen in the grid; the top panel draws the glow and the
twelve bars (`redesign-shots/q5/lite-top.png`). Not fixed here: Lite's af tap ends a **mono** file's playback (an
`af`/`join` failure); the checks used a stereo MP3. The vizlite branch's own note is `2026-11-17-viz-lite.md`.

### 2. START, the Menu of places

The tabs (Playing, Settings) are gone, and with them `Page::Playing`, `Page::Exit`, `drawTabs`, the Playing page's
rows and the old Play on picker. START opens `Page::Menu`: the kit header (**Menu**, the Playing / Paused state, the
battery and clock), a **Now playing** line when something plays and the menu was opened off the player, seven tiles
(**Home, Files, Playlists, Manga, Network, Settings, About**; four a row, the kit's settings tiles with live lines),
**Quick settings** (F) and **Quit Banana Player**, two note lines and the kit's hint bar. What plays keeps playing.

Settings and About keep the menu under them: B in a group goes to the tiles, B at the tiles to the Menu, B on the Menu
closes it (to the player if it was opened there). Manga opens the reader when it is on and the Extensions page when it
is not; while a video plays its tile says "stops the video". The playing actions all live on the player's X · More
(Like, Add to playlist…, Show in folder, Play on…, Info, Stop) and the mini bar.

Checked (generated stereo MP3): the Menu over the player (no Now playing line), over the browser (the line, SELECT,
the hint bar), Down/Right moving the tile focus, a tap on the Settings tile, Settings then B back to the Menu then B to
the browser, and the About tile. Shots: `redesign-shots/q5/menu-bottom.png`, `menu-now-bottom.png` (not committed).

### 3. Power key, lid shut and Volume keys, lid shut

Settings › Power & screens has two rows under **When the lid closes**, before the shoulder row and incognito:
**Power key, lid shut** (Play / pause, default, or Sleep) and **Volume keys, lid shut** (Volume and skip, default, or
Volume only). Both are `settingDefs()` rows (`lid-power`, `lid-volume`), saved and stepped as the others. With **When
the lid closes: Pause** the three key rows are at 45% with **The lid pauses** and change nothing. main hands the power
and volume devices to the player only for Play / pause and Volume and skip; Sleep and Volume only never take them, so
BananaOS keeps the keys. The first-start lid question's second line is the spec's: "Keep playing: the sound goes on
with the lid shut. Then the power key pauses and plays, and holding a volume key skips."

Checked on a Mac (no lid or keys): the two rows' values in both lid states, the three dimmed rows with The lid pauses,
and the settings table's test (the row order); the devices themselves are code paths only.

### 4. About's hint bar

`drawInfo`'s About page draws `kit->hints` (arrow caps, **L2/R2 Group**, **B Back**) like the other group pages. The
START Menu's About tile now calls `openAbout` first: before, the tile set the page only and About was blank. Shot:
`redesign-shots/q5/about-bottom.png`.

### Checked

Desktop build (macOS); `./build/core-test` ends `core: all passed`; `cmake --build build --target manga-check` prints
`manga: all passed` before each commit. Shots under `redesign-shots/q5/` (not committed): `lite-top.png`,
`menu-bottom.png`, `menu-now-bottom.png`, `power-pause-bottom.png`, `power-keep-bottom.png`, `about-bottom.png`.
Generated media only: a stereo 220/330 Hz sine MP3 with a generated cover. The arm64 bundle:
`packaging/build-bundle.sh --quick` was run once at the end.

### Not verified

- Lite with a mono file (it ends the file, above) and the handheld's panel at one pixel a unit for the fifth grid row.
- The Menu's Manga tile stopping a playing video (the spec's guard sheet is not built).
- The power and volume devices on the handheld: no lid switch, power key or `gpio-keys-volume` on a Mac.



