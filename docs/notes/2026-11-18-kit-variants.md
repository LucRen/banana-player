# Kit variants: fourteen more `Ui::Kit` components (2026-11-18)

Branch `kit-variants`. The components of `docs/redesign/components.md` marked "exists, needs a variant" that the kit
still lacked, as the redesign draws them. Declarations: `ui_kit.h`, the block "KIT VARIANTS" at the end of `Kit`;
drawing: `src/ui_kit_variants.cpp`; layout maths (pure): the end of `kitlayout.h` / `.cpp`; one named test each in
`core_test.cpp` (`testKitVariant*`). No screen was switched: the old drawing is where it was.

| Component | Kit call, layout | Takes | Old drawing (still used) |
|---|---|---|---|
| Status cluster, badges | `status`, `badge`; `lay::statusRow`, `badgeW` | `Status` (badges, playing bars, Wi-Fi bars, battery % and bolt, clock); returns the left edge | `drawBattery`, `drawWebIndicator`, `drawMark` (ui_draw.cpp) |
| Mini bar | `miniBar`; `lay::miniBar` | `Mini`: kind icon, title, time, progress, pills, the SELECT cap, `hold` (0..1) on the 56 x 42 mark | `drawMiniBar` (ui_home.cpp), `MINI_BOX` |
| Filter chips | `filters`, `filterWidths`; `lay::filterChips`, `filterAt` | labels with counts, chosen, focused; chips 40 tall, touch strip 56 | `drawRecentTabs`, `drawTab` |
| Form row | `formRow`; `lay::formRow`, `formKeys` | label, value (secret shows `*`), placeholder | `ui_forms.cpp`, `drawMangaServerForm` |
| Alert sheet | `alertSheet`; `lay::alertSheet` | title, wrapped lines (`sheetText`), key label, danger | `Ui::AlertBox` (ui_overlay.cpp) |
| Confirm sheet | `confirmSheet`; `lay::confirmSheet` | safe / action labels, danger ink, focus 0/1, optional "don't ask again" line | `drawQuitDialog`, `drawUpdatePopup`, press-twice rows |
| Busy / job sheet | `busySheet`; `lay::busySheet` | title, file, share (-1: no bar), left and right amounts, job glyph or spinner `tick`, 12-tall bar | `drawBusy`, `drawFileJob` (ui_fileops.cpp) |
| Keyboard | `keyboard`; `lay::keyboard` | `KeyboardView`: value, caret, secret, a special row, letter rows, layer keys, caps per bottom key, cursor | `drawKeyboard` (ui_forms.cpp) |
| Volume bar | `volumeBar`, `volumeInk`; `lay::volumeBar`, `volumeX`, `volumeZone` | value, max; TEXT to 100, ACCENT to 120, DANGER past; mark at 100 | `drawLevel`, `drawQuick`'s bar |
| Tile with a summary | `tile`; `lay::tileCell` | glyph, name, note (summary or reason when off), corner mark; 84 tall = menu tile, under 70 = shortcut / toggle | `drawHome`'s tiles; `settingTile` stays for Settings |
| Recent card, Continue card | `recentCard`, `continueCard`; `lay::recentCard`, `continueCard` | kind, title, note, progress, label, key; a `picture` callback for a cover | `drawHome` |
| Empty state | `emptyState`; `lay::emptyState` | glyph, title, lines, keys A / Y | `drawEmpty` |
| Notice row | `notice`; `lay::notice` | tone, glyph, text, second line, share, key | `drawExtNotice`, `drawMangaToast` |
| Select bar, clip bar | `selectBar`, `clipBar`; `lay::selectBar`, `clipBar` | four `BarKey`s; what is held, its size, two keys | `drawSelBar`, `drawClipBar` |

The sheets draw with `Kit::sheet` / `sheetBack` and their own hint bar and take the Rects of `lay::*Sheet`, so an
`Ui::Overlay` writes `draw()` as one call and tests touches with the same Rects (see `PlayOnSheet`). None is wired as
an overlay yet; that is the step that switches a dialog over.

## Gallery

`--press gallery:variants[:PAGE]` (test only; `Ui::VariantsGallery`, an overlay, `B` closes) draws pages 1 to 8:
widgets, cards and bars, mini bar and select bar, empty state, keyboard, alert, confirm, busy. For `shot:`.

## Differences from the spec

- Type sizes: the kit has Pixelify at 12, 13, 14, 16, 18, 24 and Inter at 12, 14, so the spec's 15, 17, 22 and 13.5
  are drawn at 14, 18 (F_SMALL), 24 (F_NORMAL) and 14 / 12. A new size is one line in `FONT_DEFS` (shared), so not here.
- Wi-Fi is four bars (no Wi-Fi glyph exists); the playing bars step with `Status::tick`.
- The busy sheet is at least 226 tall (`lay::sheet`'s floor), the spec's 214 is not reachable without changing it.
- The keyboard: kit part only. The echo of the text on the top screen is not wired; the field's text is
  `KeyboardView::value`. Its layout starts under a 54-tall header the screen draws (back key, title).
- The kind mark on a Recent card picture uses `TOP_CARD` / `TOP_TITLE` (dark in every theme), as the "fixed on a
  picture" rule needs a token.

## Which screen should switch first

The **Confirm** and **Alert** sheets: every dialog goes through them (`AlertBox` is one overlay and one call, `alert()`),
so one switch changes about forty messages and tests the sheet look on the handheld. Then the **empty state**
(`drawEmpty`, six pages) and the **notice row** (Manga). The keyboard last: it carries the input path and the top
screen's echo.
