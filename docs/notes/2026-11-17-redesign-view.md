# The redesign's View sheet (SELECT), and how it was built (2026-11-17)

Step B of `2026-11-16-redesign-kit.md`'s handover, the video's half: **SELECT · View** is a sheet over the
player, with **Subtitles**, **Audio**, **Picture** and **Speed** side by side. The design:
`docs/redesign/src/screens.js` "video-view", "video-speed" and "video-picture" (without their **Adjust** tab, which
is a new feature, out of this step).

## What it is

`Ui::ViewSheet` (`src/ui_player.cpp`) is one `Ui::Overlay` named "view", pushed by `Ui::PlayerPage::openView`.

- **Tabs** by `lay::tabs`, drawn by `Kit::tabs`: Subtitles, Audio, Picture, Speed; `L` / `R` step them, no wrap.
  The header's right says how many there are to choose ("6 tracks", "3 sizes"; nothing on Speed, as in the
  mock-up). The sheet opens on the tab last used (`PlayerPage::viewTab`).
- **Rows** are `Ui::rows(p)` with `ui.tracksTab` pointed at the tab's track type while it is asked (and put back
  afterwards: asking is not choosing), so the sheet and the old Tracks page list the same rows. `A` (or a tap) is
  `Ui::choose`'s own action: a subtitle or audio track by its **id** through `chooseTrack` (never "auto"), a
  picture size through `setAspect`, and **Subtitle style…** closes the sheet and opens Settings › Subtitles from
  its own row.
- **Radio rows**: a new `Kit::Row` field (`radio`, `radioOn`) draws `G_RADIO` / `G_RADIO_ON` in the row's icon
  slot. The track in use, `Off` among the subtitles, the picture size and the speed have it filled in yellow; a
  message row ("No audio in this file") has none and is dim.
- **Speed** is five rows from `format.h`'s `speedChoices()` (0.5x, 1x, 1.5x, 2x, 2.5x — the list the stepping chip
  already used, now shared), `1x` noted **Normal**. `A` calls `p.setSpeed` and says so by `p.osd`; the radio moves at
  once and the player's own speed follows from mpv.
- **Stays open** on a track, a size or a speed: several are set here. Seven rows show (`lay::LIST_ROWS`); LEFT /
  RIGHT page a longer list (`lay::listScroll` keeps the cursor shown, with `Ui::drawScrollBar`). UP / DOWN move,
  A chooses, SELECT / B close (Y closes too: it does not fall through to List), and a tap on the veil closes it.
  A tap on a tab or a row acts on the lift (slide off to cancel). Touch is ignored while the player's lock is on,
  as for the List and More sheets.
- **Where it opens**: SELECT on a video (the tab last used); the Subtitles / Audio / Picture chips open their tab;
  the Speed chip opens Speed. A song opens `SongViewSheet` instead (Display, Equalizer, Effects, Speed: see
  `2026-11-17-redesign-rest.md`); its Sound chip opens Equalizer, Top screen Display and Speed Speed.

## The pieces added for it

- `format.*`: `speedChoices()` returns the five speeds `stepSpeed` uses (one list, so a sixth is one line).
- `Kit::Row::radio` / `radioOn` and their drawing in `Kit::row` (the `G_RADIO` glyphs were already there).
- `Ui::PlayerPage::openView` / `viewTab`; `chipAct` and the SELECT key point at it. `FEATURES.md` updated.

## Checked

Desktop build (macOS): `cmake --build build -j` clean, `./build/core-test` ends `core: all passed` (the new
`speedChoices()` checks included), `cmake --build build --target manga-check` prints `manga: all passed`.

## Not verified

- ~~No scripted run and no screenshots.~~ Driven and photographed on 2026-11-17: Subtitles, Audio, Picture and
  Speed, the style row's way to Settings and B's way back (`redesign-shots/rest/vview-bottom.png`,
  `viewback-bottom.png`).
- The handheld's panel at one pixel a unit: the 12-unit header note and the 14-pixel radios are the things to look
  at first.
- A file with many tracks (the sheet's paging and scroll mark) and a file that can't play with the sheet open.
- ~~**Subtitle style…** goes to Settings as `Ui::choose` did, but B there still returns to the old Tracks page
  (`SettingsPage::styleFromSubs`), not to this sheet.~~ Fixed: `SettingsPage::menuBack` now returns to this sheet
  on **Subtitles** (`PlayerPage::openView(0, p)`), unless the file ended while the style was open.
