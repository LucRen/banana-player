# The redesign's List sheet (Y), and how it was built (2026-11-16)

Step A of `2026-11-16-redesign-kit.md`'s handover: **Y · List** is a sheet over the player with **Up next** and
**Chapters** side by side, and Shuffle and Repeat above the list they change. The design: `docs/redesign/src/screens.js`
"video-list" and "video-chapters-long", `components.md` (tabs, list rows, switch, segmented choice).

## What it is

`Ui::ListSheet` (`src/ui_player.cpp`) is one `Ui::Overlay` named "list", pushed by `Ui::PlayerPage::openList`.

- **Tabs** by `lay::tabs` (44 tall, 6 apart, a key cap at each end) drawn by `Kit::tabs`. Up next is always there;
  Chapters (with the count, "Chapters 42") only when the file has some. `L` / `R` step them, no wrap.
- **Up next** is `ui.upNextList(p, cur)` — the Session's queue when it has this file, else the player's own list, in
  its own order under Shuffle. `A` is `ui.playUpNext(i, p)`: the same pick the old Up next page made.
- **Chapters** are `p.chapters`; `A` is what `Ui::choose` does on `Page::Chapters`: `seekChapter`, unpause, the jump's
  card. A tap on the player's chapter line opens this tab; a tap on the Next line opens Up next.
- **Rows** are `Kit::row`: 46 tall; the one playing gets the play / pause / browser mark and its **Now** / **Paused** /
  **Browser** pill, the others their place. A chapter's note is its start; an Up next row's its length where known.
  Seven are shown (`lay::LIST_ROWS`), LEFT / RIGHT move a page, `lay::listScroll` keeps the cursor in view and the
  scroll mark is `Ui::drawScrollBar`. The sheet opens on what plays, three rows above it.
- **Shuffle** is `Kit::toggle` in the header, set through `SessionHost::setShuffle` (the same intent the dashboard's
  Shuffle uses, which also writes the setting). **Repeat** is `Kit::segment` (Off / All / One) through the `loop`
  setting, as the dashboard's Repeat does. Each is set alone: the pairs the web allows are all reachable.
- **Keys**: UP / DOWN move, LEFT / RIGHT page, A plays from the row, L / R step the tabs, L2 / R2 run
  `Ui::chapterStep` (the block moved out of `Ui::press`, so the sheet and the player jump chapters the same way), Y / B
  close. Touch: a tab, a row, the Shuffle control or a Repeat chip acts on the lift (slide off to cancel); outside
  closes.
- **Where it opens**: `Y` and the List key (`listAct`) open it on the tab last used; a playlist's song still leaves for
  the playlist's page (`listAct`'s first branch, unchanged); the chapter line opens Chapters; the Next line opens Up
  next; a video's **Repeat** chip opens it on the last tab; a song's fourth chip is **Shuffle**, which turns Shuffle
  on or off in place.
- **Closing itself**: like `PlayOnSheet`, `draw` closes it when nothing plays, the page is no longer Main, or the list
  is empty (a file ended under it).

## The pieces added for it

- `lay::tabs(area, widths, gap)`, `lay::listSheet(rows, header)`, `lay::listScroll(cursor, scroll, shown, n)`
  (`src/kitlayout.*`, tests in `testKitLayout`).
- `Kit::tabs` / `Kit::tabRects`, `Kit::segment` and `Kit::sheetBack` (the sheet body without a header), plus
  `Kit::Row::lead` (a number in the icon's slot) in `src/ui_kit.*`.
- `Ui::chapterStep(dir, p)`: `Ui::press`'s L2 / R2 block, moved so an overlay that takes the keys first can pass it on.

## Checked (desktop, macOS; generated media only)

Build, `core-test` (all passed; new layout tests), `manga-check` (all passed). Scripted runs with the note's `run.sh`
(a 90 s test card remuxed with 42 chapters, some long names; two 75 s tones with a cover):

- a video: Y on Up next, R to Chapters, RIGHT paging, the sheet opening scrolled to the playing chapter; each Repeat
  chip (Off / All / One) and Shuffle on, and the player's Repeat chip then reading All / One; L2 with the sheet open;
  a tap on the chapter line (Chapters) and on the Next line (Up next); DOWN then A playing the next file;
- a song: Y (one tab, no chapter line), the Shuffle switch, B closing, the fourth chip reading **Shuffle On** with its
  dot, a tap on it turning Shuffle off, the Next line saying "2 of 2 · shuffled" meanwhile;
- `--press` names unchanged (only `Y`, `R`, `L`, `L2`, arrows, `A`, `B` used above).

## Not verified, or left for later

- The handheld's panel at one pixel a unit: the 12-13 unit text in the header's controls and the 19-unit caps.
- A playlist's song's List key (the branch is untouched) and a file that can't play with the sheet open.
- Drag-to-scroll inside the sheet (the mock-up's list rows allow it; LEFT / RIGHT page today) and a hold to start
  select mode are not part of the List.
- SELECT still opens today's Tracks page, whose Chapters tab remains until step B (`video-view`) moves it: the List
  sheet now has Chapters too, and both act the same way.
