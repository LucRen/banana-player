# Settings' title beside the back key; About's support QR, large on the top panel (2026-11-18)

The brief after §7 of `2026-11-17-redesign-rest.md`: the Settings header as the mock-up draws it, and the support QR
readable. Two commits:

| Commit | What |
|---|---|
| `972590b` | Settings and every group page: the title beside the back key, its crumb under it; the page starts at 64 |
| `8a9eafe` | About's support QR, large on the top panel (Y or a tap), and its address; the hint bar says Y QR |

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

## 2. The support QR, large on the top panel

About's foot drew the static code at 84 units (its own real size, and too small to scan from a phone held over the
handheld). It stays exactly as it was; what was added is a view the top panel can show, as it shows a reader's pages.

- **Where it lives:** `TopScreen` gained its own `PageRenderer` and `drawQr`: the picture is composed at the panel's
  **drawable pixels** (`src/supportqr.h`: `composeSupportQr`), black, a white box with the code inside it, the address
  under it, and drawn 1:1 through the same renderer the readers use (no new thread, no mpv overlay: the top screen is
  the same picture path a chapter's pages take). `onTop` counts it, so a song's visualizer rests while it is up and
  what plays is still taken under it (`drainUnder`); `capture` draws it too, so a screenshot's top half is the code.
- **As big as fits, nearest:** `lay::qrPanel(w, h)` (bpcore, tested in `testKitLayout`) centres a square quiet zone
  across the panel, 20 units of margin at 480 (proportional to the height), the address's line under it; the code is
  76% of the box, so the white ring is about four modules of the 29-module code a phone needs. The file is decoded at
  its own 492 px and each code pixel is one source pixel (`(2x+1)*src/(2*dst)`), so no edge is smeared; at 640×480 the
  code is 310 units, at the Mac's 1280×960 it is 620. The address is Inter, sized with the line's height (24 at 480,
  as levelcard.cpp measures one), cream under the white box.
- **The keys:** Y on About puts it up (`Ui::aboutQr`, gated on `page == Page::About`); B or Y puts it down, a finger's
  down anywhere puts it down, and leaving About closes it (the flag is cleared when the page leaves, and `openAbout`
  clears it). While it is up the hint bar reads **B Close · Y Close**; on About it reads **UP/DOWN Scroll · Y QR ·
  L2/R2 Group · B Back**. A tap on the 84-unit code at the foot does what Y does (`aboutQrRect`, the same box
  `drawAboutSupport` draws).

## Checked

Desktop build (macOS), `./build/core-test` ends `core: all passed` (with `testKitLayout`'s settings checks: the tiles and
rows start under the strip, the tiles end above the hint bar, and the seven rows are still clear of the help, and the
QR panel's boxes at 640×480 and 1280×960), and `cmake --build build --target manga-check` prints `manga: all passed`.

Scripted runs (`--press`, `BP_SHOTS=redesign-shots/q7`, a scratch state, no media): the Settings tiles with the inline
**Settings** and **Banana Player 0.7.27**, the Subtitles group (**Subtitles**, **Settings · 2 of 10** and **L2 Playback ·
Music R2** in the same strip, six rows and the preview), Power & screens with seven rows shown and its bar, About with
**Y QR** in its hint bar, the QR large on the top panel after Y with the address under it, the same after a tap on the
small code, and the top panel back to its idle icon after B. Shots (not committed): `settings-bottom.png`,
`group-bottom.png`, `power-bottom.png`, `about-bottom.png`, `qr-top.png`, `qr-bottom.png` (B Close · Y Close),
`qr-tap-top.png`, `about-closed-top.png`.

The arm64 bundle: `packaging/build-bundle.sh --quick` was run once at the end (`dist/ports/BananaPlayer`, 57 MB): it
compiles the arm64 player (with `supportqr.cpp`) and copies `packaging/support/kofi-qr.png` into its `share/`.

## Not verified

- **Scanning it with a phone:** the picture is the same `ko-fi.com/tonysong` code the file holds, nearest-scaled with a
  white ring; no camera read it here.
- The handheld's own panel at one pixel a unit: 480 px square (the code about 310) and the 24-px address line under
  it. On this Mac the panel is 960 tall and the code 620.
- A real finger on the 84-unit code (the tap drove it in the script, at 60,380).
