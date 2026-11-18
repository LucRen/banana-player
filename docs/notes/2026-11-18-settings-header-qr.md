# Settings' title beside the back key; About's support QR, large on the top panel (2026-11-18)

The brief after §7 of `2026-11-17-redesign-rest.md`: the Settings header as the mock-up draws it, and the support QR
readable. Two commits:

| Commit | What |
|---|---|
| (item 1) | Settings and every group page: the title beside the back key, its crumb under it; the page starts at 64 |
| (item 2) | About's support QR, large on the top panel (Y or a tap), and its address; the hint bar says Y QR |

## 1. The title in the header's own row

`Kit::header` drew the player's header: the back and More keys and the crumb in a 54-tall strip, then the title in
`lay::playerLayout`'s title row at 58. That row is right on the player, where the song's title under the header row is
the design; on Settings it was the wrong drawing: the mock-up (`docs/redesign/src/kit.js`'s `head`, `.hd` in
`docs/redesign/src/style.css`) draws the back key and then one block, the 25px title with the 12px crumb under it, both
inside the 54-tall strip.

`Kit::Header` gained **`inlineTitle`**: the title's middle at `cy - 8` and the crumb's at `cy + 12`, from the same x
(the back key's right, plus 10), ending before the strip's right slot (the battery and clock on the tiles, the L2 / R2
neighbours' names in a group). It returns before the player's title row is drawn, so nothing is drawn twice.
`SettingsPage::draw` and `Ui::drawInfo` (About, the last group) set it; the player's header (`PlayerPage::draw`) does
not, and neither does the Web dashboard's page (it has its own header, a title in the strip already).

With no title row, the page starts higher. `lay::SET_TILE_TOP` and `lay::SET_ROW_TOP` went from 100 to **64** (the
strip ends at 54), and a group shows **seven** rows (`lay::SET_ROWS`, and `Ui::SET_SHOWN` now reads it) instead of six:
`SET_ROW_TOP + 7 * SET_ROW_H` is 386, clear of the help at 400 and the hint bar at 450. About's lines start at 64 too
and it shows ten (`ABOUT_Y`, `ABOUT_SHOWN`), the foot's rule still at 340. The tiles (five rows of 58) end at 386.

The subtitle style's preview follows the rows as before (`min(rows, SET_SHOWN)`); with the Subtitles group's six rows it
is where it was, at 344.

## Checked

Desktop build (macOS), `./build/core-test` ends `core: all passed` (with `testKitLayout`'s settings checks: the tiles and
rows start under the strip, the tiles end above the hint bar, and the seven rows are still clear of the help), and
`cmake --build build --target manga-check` prints `manga: all passed`.

Scripted runs (`--press`, `BP_SHOTS=redesign-shots/q7`, a scratch state, no media): the Settings tiles with the inline
**Settings** and **Banana Player 0.7.27**, the Subtitles group (**Subtitles**, **Settings · 2 of 10** and **L2 Playback ·
Music R2** in the same strip, six rows and the preview), and Power & screens with seven rows shown and its bar.
Shots: `settings-bottom.png`, `group-bottom.png`, `power-bottom.png` (not committed).

## Not verified

- The handheld's panel at one pixel a unit: the title in F_P28 with a 12-unit crumb under it in a 54-unit strip, beside
  a 56-wide back key, and the longest group name ("Power & screens") next to the neighbours' names.
- A finger: the rows' and tiles' boxes moved up by 36 units with the layout (`lay::settingsRows`, `settingsTile`), so
  the taps are where they are drawn; no real touch screen was used.
