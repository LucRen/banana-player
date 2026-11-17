# Settings as the spec's groups (2026-11-17)

The Settings page rebuilt on the redesign kit: the group list, each group's rows, **L2 / R2** between
groups, and the focused row's help. The spec is `docs/redesign/src/screens.js` (`settings`,
`settings-group`, `set-playback`, `set-music`, `set-photos`, `set-reading`, `set-files`, `set-screen`,
`set-updates`, `set-about`) and `components.md` ("Settings row (stepper)", "Settings tile"). The page frame
is still today's (the menu's tabs, header and the bottom hint bar): the redesign's global header, section
rail and now-playing bar around every page are the next step, not this one.

## The groups

Ten tiles, in the spec's order, two to a row (the mock-up's 58-tall tile: a mark, the name, a live
summary):

**Playback** (Remember position, Remember audio track, Remember subtitles, Max volume, Button hotkeys) ·
**Subtitles** (the six style rows, the live preview kept under them) · **Music & sound** (Music display,
Visualizer, Visualizer frame rate, Song info on top, Sound) · **Photos** · **Reading** (the text and PDF
viewers' choices, which had no page before) · **Files & history** · **Power & screens** · **Web
dashboard** (its own page, as before) · **Updates** · **About** (the existing About page).

The rows the spec moves moved in the settings table only (`settingDefs()`): Max volume and Button
hotkeys from Controls & screen to Playback; the document viewers' choices from no page to Reading; the
Power & screens rows re-ordered (`same-brightness` 30, `screen-off` 40, `screen-off-video` 50,
`lid-close` 70, the two new lid rows 80 and 90, `battery-percent` 120, `clock` 130). `SettingsGroup`
and `Ui::SetGroup` have the same order, checked by a `static_assert`.

Two rows are the spec's new features from the other two items of this work, and are in the table now:
**Shoulder keys, lid shut** (`lid-shoulder`: Ignored / Hold / As usual; at 45% with "The lid pauses" while
When the lid closes is Pause) and **Lid or sleep in incognito** (`lid-incognito`: Nothing / Close all /
Quit, default Close all). What they do is items 2 and 3.

## The kit

- `kitlayout.*`: `lay::settingsTile(area, n, cols, i)` (the ten tiles), `lay::settingsRows()`
  (six 46-tall rows at 92, the help's two lines at 400), with tests in `testKitLayout` (inside the panel,
  no row over the next, clear of the help and the hint bar).
- `ui_kit.*`: `Kit::settingRow` (label; the value in a `< value >` stepper, or an action's note and
  chevron; focused lit with its label and value yellow; `off` for the 45% look) and `Kit::settingTile`.
- `ui_settings.*`: the tiles and rows draw through those, and their touches test the same Rects
  (`groupRect`, `rowRect`); L2 / R2 step `stepGroup` (About and Web dashboard, being pages of their own,
  are where a step can land); Y / X no longer step. `settingsDown`, the drag and the tap use the layout's
  measures. Manga's own settings page keeps the old `drawSettingRow`.

## Not in this item

Left out because the spec marks them new features (rows, not built): **Picture look** (Playback), **Top
screen off (music)**, **Dim before turning off** and **Stay awake for copies and downloads** (Power &
screens), **Dim the controls while reading** and **PDF page fit** (Reading). The Power & screens list
scrolls six at a time rather than the mock-up's four, and the subtitles' preview and help stay on the
bottom panel (the spec puts the explanation and preview on the top screen).

**Extensions** and **Manga** are not tiles here: the spec's group list has them, but their screens
(`extensions`, `manga-settings`) are the Manga app's own pages in this build, and item 1's screens are the
ones listed in the brief.

## Checked

Desktop build (macOS): `cmake --build build -j` clean; `./build/core-test` ends `core: all passed`
(`testSettingsTable` now checks the group order, the Reading rows and the two lid rows' value texts;
`testKitLayout` the settings layout); `cmake --build build --target manga-check` prints `manga: all
passed`.

Scripted runs with generated media (testsrc MP4 and a quiet sine MP3): the group list, each group, the
Power & screens list scrolled to the two lid rows, and L2 / R2 from Playback to About and on. Shots under
`redesign-shots/settings/` (not committed): `settings-groups-*`, `set-playback-*`, `set-subtitles-*`,
`set-music-*`, `set-photos-*`, `set-reading-*`, `set-files-*`, `set-power-*`, `set-updates-*`,
`set-about-*`.

## Not verified

- The handheld's 640x480 panel at one pixel a unit: the tiles' 12.5-unit summaries and the rows' 16-unit
  labels are the first thing to look at; a finger on a row (left of the value steps back, the rest on) and
  on a tile needs the real touch screen.
- The top-screen help and subtitles preview the spec draws there: not built in this item (no shell).
