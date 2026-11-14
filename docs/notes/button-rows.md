# Button rows on the bottom panel

Rows of buttons used to be laid out page by page (`16 + i * 154`, `16 + i * 140`, hard-coded x lists), with the
label-and-key drawing repeated beside each. Three small helpers in `ui.h` / `ui_draw.cpp` now hold that, with no
visible change.

- `Ui::rowCell(area, n, gap, i)`: the i-th of n equal cells across `area`, `gap` apart. Integer division, so a row
  whose sizes don't divide exactly would lose the remainder at the right: pick `area.w` so they do.
- `Ui::gridCell(first, cols, gapX, gapY, i)`: cell i of a grid, row by row, every cell the size of `first`.
- `Ui::drawKeyedLabel(b, label, key, off, labelW, keyW)`: the label, and the key under it (the label moves up) when
  `key` is not empty; the caller decides whether the key shows (`hints ? key : ""`). Widths default to the button
  less 6.

A row has one function that returns a rect (`docBtn`, `photoBtn`, `mangaChapterCell`); drawing and touch both ask it.

## Adding a row

```cpp
Rect Ui::fooBtn(int i) const { return rowCell({16, y, 608, 56}, 3, 10, i); }	// three of 196
...
for (int i = 0; i < 3; i++)
{
	Rect b = fooBtn(i);
	drawBox(b, fooPressed == i ? PRESSED : BUTTON, R_BUTTON);
	drawKeyedLabel(b, labels[i], p.settings.buttonHotkeys ? keys[i] : "");
}
```

## Converted

Text reader row (four across 554, gap 6; the 50-wide sun stays its own rect: its 4 clear of the row does not match
the 6 between the others), its Find bar (3 x 196, gap 10), the PDF/comic strip view (4 x 146, gap 8), the single-page
grid (2 columns, beside the pan area) and the photo viewer's grid (same cell, `gridCell`), the Manga reader's four
chapter buttons (`mangaChapterCell`; the four named rects delegate), the EQ page's Reset/Save/Options (`buttonRect`).
`drawKeyedLabel` also replaces the by-hand label/key pairs in those.

## Left alone

- Player page grid (`buttonRect` default: `BTN_STEP` columns) and the Lock/Info/Screen off/Quit row: they use
  `drawButton` with glyphs and unequal, named widths.
- `mangaCtrlBtnRect` (widths 140/140/168/136, uneven) and the Select-mode bar (`SEL_*`, widths change with the mode).
- Manga tabs / source segments / Sources rows: possible with `rowCell`, but not reachable by the desktop harness
  here, so not converted blind.
- The strip's and single page's Brightness cells and the text reader's sun draw their own glyph; they keep that code.

## Checks

Before/after bottom-panel PNGs byte-identical (cmp) for: text reader, with Find bar, comic single page, comic
vertical scroll (all with Button hotkeys on and off), the photo viewer (pan area left and right), and taps on
Options, Next, a strip button and the Find bar. The PDF page and the Manga reader page (Page::MangaRead) were not
reachable on a desktop build (no pdfium; the reader needs a manga source), so the PDF strip is covered through the
same code with a CBZ, and the Manga chapter row's rects checked by arithmetic: 16/170/324/478, width 146, as before.
The EQ row was checked by arithmetic too: (608 - 20) / 3 = 196, step 206.
