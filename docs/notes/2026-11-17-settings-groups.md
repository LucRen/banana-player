# Settings: the redesign's groups (2026-11-17)

Item 1 of the redesign's settings, lid and incognito brief: the Settings page as `docs/redesign/src/screens.js` groups it
(`settings`, `settings-group`, `set-playback`, `set-music`, `set-photos`, `set-reading`, `set-files`, `set-screen`,
`set-updates`, `set-about`), drawn with the kit. The app shell (the global header, the section rail, the now-playing bar)
is **not** built: the page keeps today's frame (the menu's tabs, the title line, the hint bar) and today's top panel.

## The groups

Settings opens on ten tiles, two a row, in the spec's order: **Playback, Subtitles, Music & sound, Photos, Reading,
Files & history, Power & screens, Web dashboard, Updates, About**. Each tile is the mock-up's Settings tile: a glyph, the
name, and a live line saying what it is set to now ("Resume on · tracks remembered", "Screen off 30 seconds · the lid
pauses", "Off in incognito"). `A` or a tap opens one; `B` goes back to the tiles.

In a group: `UP`/`DOWN` choose a row, `LEFT`/`RIGHT` change it in place, a tap on the left of its value steps back and
elsewhere on the row on, `L2`/`R2` go to the group before or after (the neighbours either side of the title are those
keys too). The focused row is lit, and its help is the line above the hint bar (today's place; the mock-up puts it on the
top screen, which this item does not change). The subtitle style's preview keeps its place under the six style rows.

The rows are the settings table's (`settingDefs()`, docs/notes/settings-table.md) — no setting is held anywhere else. What
moved, by the spec's notes:

| Group | Rows |
|---|---|
| Playback | Remember position, Remember audio track, Remember subtitles, Max volume, Button hotkeys (the old Remember group, plus Max volume and Button hotkeys from Controls & screen). The spec's **Picture look** is a feature that does not exist yet: no row |
| Subtitles | the six style rows and Text encoding, as before |
| Music & sound | Music display, Visualizer, Visualizer frame rate, Song info on top, **Sound** (the Sound page's row, named as the mock-up does) |
| Photos | Keep zoom between photos, Pan area, Drag moves |
| Reading | **new group**: Font, Text size, Line spacing, Margins, Colours, Text opens as, PDF opens as — the readers' defaults, which had no row anywhere before (`text-*`, `pdf-view`) |
| Files & history | Remember last folder, Show hidden folders, Keep play history, Network files in Recent, Clear play history |
| Power & screens | Brightness (or Top and Bottom), Same for both screens, Screen off (music), Screen off (video), When the lid closes, Shoulder keys lid shut, Lid or sleep in incognito, Battery percentage, Clock |
| Web dashboard | the page it always was (its own header, QR, address, PIN), opened by its tile; its tile's line is its state |
| Updates | Check automatically, Check for updates, Update to … |
| About | today's About page (version, licence, warranty, manga-sources note, libraries), opened by its tile and `L2`/`R2`-stepped like a group; not reskinned |

`Shoulder keys, lid shut` and `Lid or sleep in incognito` are in the table from this item (defaults Ignored and Close all);
their behaviour is items 2 and 3. `Shoulder keys` is drawn at 45% with "The lid pauses" in the value's place while
When the lid closes is Pause, and changes nothing then.

Two rows the spec draws have no feature behind them yet, so they are not listed: **Top screen off (music)**, **Dim before
turning off** and **Stay awake for copies and downloads** (Power & screens), and **Dim the controls while reading**
(Reading). The spec's **Extensions** and **Manga** tiles are not built either: the manga reader's own settings page is
still Manga's (`Page::MangaSettings`), reached from its app, and this item's scope is the ten screens the brief names.

## Code

- `src/kitlayout.*` (bpcore): `lay::settingsTile(area, n, cols, i)` (the 58-tall tiles, `cell()`'s spare units to the
  last one), `lay::SettingsRows` / `lay::settingsRows(shown)` (the rows 46 apart from `SET_ROW_TOP`, the help line) and the
  `SET_TILE_*` / `SET_ROW_*` measures. Tested in `testKitLayout`.
- `src/ui_kit.*`: `Kit::settingTile` (a glyph, a name, a live line) and `Kit::settingRow` (a label, then the stepper
  `< value >` at the arrow the page's tap already used, or an action's note and a chevron; a row `off` is the one 45%
  look with its reason in the value's place).
- `src/ui_settings.*`: the groups' names (`GROUP_NAMES`), the group summaries, the tiled group list and the rows' drawing
  with the kit; `stepGroup` is the one way to the neighbour (the key, the title's tap, About's and the Web page's `L2`/`R2`
  too), and it comes back to the tiles with the cursor on the group it leaves. The Manga settings page keeps the old
  `drawSettingRow`.
- `src/settings.*`: `SG_PLAYBACK` … `SG_UPDATES` in the page's order (About has no table rows), the Reading rows added to
  the table, Max volume / Button hotkeys / Battery percentage / Clock moved to their groups, and the two lid rows
  (`lid-shoulder`, `lid-incognito`).
- `src/ui.cpp`: the group tiles' drag no longer scrolls (they all fit); `L2`/`R2` reach the Web page, About and the group
  pages; B in About (opened from Settings) goes back to the tiles, not out of the menu.

## Checked

Desktop build (macOS), `./build/core-test` ends `core: all passed` (with the settings-table test's groups updated and the
two lid rows' texts), `cmake --build build --target manga-check` prints `manga: all passed`. Scripted runs (`--press`,
`BP_SHOTS=redesign-shots/settings`, a scratch state with every setting written, no media): the group list, each of the ten
groups (`settings`, `playback`, `subtitles`, `music`, `photos`, `reading`, `files`, `screen`, `web`, `updates`, `about`),
the Power & screens scroll bar (eight rows, six shown), tapping a tile, a row's left and right halves, a neighbour key,
`L2` from About to Updates, `R2` back to About, and B from About to the tiles.

## Not verified

- The handheld's panel at one pixel a unit: the 12-unit tile summaries and the 16-unit rows' values are the first thing
  to look at; every size is the mock-up's, tried on a Mac at twice the panel's pixels.
- The Power & screens rows on the handheld (its brightness rows come from the backlights; this Mac has none, so the group
  starts at Screen off (music)).
- A finger on a tile or a row's arrow: the tap rects are the layout's, but a real touch screen's aim is not.
- The Updates group with an update offered (this build's updater is Off, so only Check for updates shows).
