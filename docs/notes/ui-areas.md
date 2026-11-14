# Ui areas: a part of the app in its own struct (2026-11-15)

## The problem

`Ui` held every page's state and handlers: `src/ui.h` was 2,712 lines, and each feature added members to it. So most
edits recompiled all 33 files that include it, a page's state could be read and written from anywhere, and "what does
the text reader keep?" was answered by grepping a prefix.

## The pattern

An **area** is a part of the app with pages of its own. Its state and its handlers are one struct, in its own header
and `.cpp`; `Ui` owns it and sends it everything while one of its pages is shown. `Ui::Area` (`src/ui.h`):

```cpp
struct Area
{
	Ui& ui;
	virtual bool has(Page pg) const = 0;	// the page is one of its own
	virtual bool key(Button b, Player& p) = 0;	// as Ui::press (true leaves the player)
	virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;	// x, y in 640x480 units
	virtual void tick(Player&) {}	// each turn of the loop while its page shows
	virtual void draw(const Player& p) = 0;
	virtual std::string signature(const Player& p) const = 0;	// what draw() shows that Settings doesn't hold
	virtual bool animating() const { return false; }	// something of its moves by itself
};
```

`Ui::area()` is the area whose page is shown (null: a page that is still `Ui`'s own). It is asked in six places, once
each, and they name no area:

- `press`: `if (Area* a = area()) return a->key(b, p);` where the pages' own keys start, so everything global has
  had the key first (the dark panel, the overlays, the file job's card, quick settings, the keyboard, the Options
  menu, L2/R2 over what plays, START's menu, Files' clip).
- `pointer`: `if (Area* a = area()) return a->pointer(pt, x, y, p);` after the held slider, the overlays, the
  keyboard and the mini bar.
- `tick`, `renderPage` (the overlays, the mini bar and the file job's card are drawn over it), `signature`,
  `animating`.

An area is a struct **nested in `Ui`**, declared in `ui.h` by name only and defined in its own header:

```cpp
// ui.h
struct Docs;
std::unique_ptr<Docs> docs;

// ui_docs.h
struct Ui::Docs : Ui::Area { ... };

// ui.cpp (Ui's constructor: the one place that lists the areas)
docs = std::make_unique<Docs>(*this);
areas.push_back(docs.get());
```

Why this shape:

- **Nested, with a `Ui&`** (as the overlays are; docs/notes/overlay-stack.md). Nested: it may use `Ui`'s private
  drawing and state (`ui.drawTextMid`, `ui.drawBox`, `ui.fillRound`, `ui.page`, `ui.miniUp`, `ui.slideGrab`,
  `ui.openLight()`), with no friend list and no context struct to keep in step. And `Ui`'s types, constants and
  static helpers need no prefix inside it (`Rect`, `Page`, `W`, `H`, `SL_DOC`, `PAN_W`, `rowCell`, `gridCell`), nor
  do `ui_internal.h`'s names (`BG`, `ACCENT`, `R_BUTTON`). So the code reads as it did, with `ui.` where it reaches
  outside itself, which is exactly the list of what the area depends on.
- **Behind a pointer, its header not included by `ui.h`.** That is what takes it out of `ui.h`: a member, a helper or
  a whole feature added to the readers recompiles the 8 files that include `ui_docs.h`, not the 33 that include
  `ui.h`. (`Ui` got a declared constructor for this: `Docs` is not a whole type in `ui.h`.)
- **The `Player&` comes with each call**, as for every `Ui` method and overlay (it isn't kept: main hands the
  photos' player in while a photo shows).
- **One struct for both readers**, not one per page: the text viewer and the PDF/comic viewer share the document
  shown, its place, the menus, the finger's state and the way out. An area is cut where the state is shared, and
  `has()` may name several pages.
- **A virtual interface for one area** would be early if it stayed one. It is there so that the six places above
  never name an area: the second area adds two lines to the constructor, nothing to `press`/`pointer`/`tick`/
  `renderPage`/`signature`/`animating`. That is the same trade the overlay stack made, and the cost is a virtual call
  per key.

Names inside the area were left as they were (`docMenu`, `docPressed`, `findQuery`…) so the move is a diff of `ui.`
prefixes; only the six of the interface (`key`, `pointer`, `tick`, `draw`, `signature`, `open`) were renamed.

## What moved (the proof): the document viewers

`Ui::Docs` (`src/ui_docs.h`, `src/ui_docs.cpp`): `Page::DocText` (a TXT or a book of text on main's `TextReader`) and
`Page::DocPages` (a PDF, a comic, a comic EPUB on main's `MangaReader`). 133 lines left `ui.h` for
`ui_docs.h` (2,712 → 2,608 with `Area` added): the text viewer's request, commands and status, the pan area, the
places, bookmarks, Find, the menus, the finger's state, the textures and the rects.

- **Callers elsewhere in `Ui`** ask it through a few methods instead of its members: `docs->placeOf(path)` (Home's
  Recent cards, the dashboard), `docs->forgetPlace(path)` (Recent's "Open from the start"), `docs->pathMoved(from,
  to)` (Rename, Move), `docs->findTyped(query)` (the keyboard's Done). `Ui::openDocument(path, p)` stays, forwarding
  to `docs->open`, so Files' way in is untouched.
- **main** takes the text viewer's channel from it: `Ui::Docs& docs = *ui.docs;` then `docs.takeTextRead`,
  `docs.textCmds`, `docs.textStatus`, `docs.textCloseAsked`, `docs.textEncoding`, `docs.docHighlight()`; and
  `ui.docs->docPagesFilter(player)`, `ui.docs->docPan` for the PDF viewer.
- `signature()` took the two terms `Ui::signature` added for its pages (the pages grid, fullscreen) with it.

### What stayed `Ui`'s, on purpose: the page reader's channel

`Page::DocPages` and the Manga reader (`Page::MangaRead`) are two bottom panels for **one** reader on the top panel
(main's `MangaReader`). What they both use is that reader's channel and its shared views, and it stayed in `Ui`:

- `ReaderCmd`/`readerCmds`, `ReaderStatus`/`readerStatus`, `ReaderBottom`, `ReaderThumbs`, `mangaRead` (the request),
  `mangaReadChapterTitle`;
- the pages grid (`mangaPages`, `mangaPagesOpen/Key/Pointer`, `drawMangaPages`, `mpTex`), fullscreen's drawing
  (`drawMangaFull`, `mfTex`), the zoom (`mangaZoomStep/To/Toggle`), `mangaSeek`, `mangaRetry`, the dragged lists
  (`mangaListBegin/Pointer`);
- the Brightness sheet's `openLight`, `lightNote`, `lightFilter` (all three readers), and `readFilterNow`.

`Docs` uses them as `ui.readerStatus`, `ui.mangaPagesOpen(p)`… Splitting them now would have meant either two copies
or a third struct invented before the Manga reader's own shape is known. When Manga becomes an area, these are the
natural **`Ui::PageReader`** (the top reader's channel plus the grid, fullscreen and zoom), used by both `Docs` and
the Manga area; until then the `ui.` prefixes in `ui_docs.cpp` on `reader*`/`manga*` names are the exact list.

DocPages-only, so in `Docs`: `DocPan`/`docPan` and its texture (main fills it only for a document),
`docPagesFilter`, the single page's grid of buttons and the strip view's row, the bar, the pad.

## Adding a feature inside an area

Say a "Night" switch in the text viewer's options: an enum value and its row in `docMenuRows()`, its case in
`docMenuStep()` and its label and value in `drawDocMenu()` (all `ui_docs.cpp`); state it needs is a member of `Docs`
in `ui_docs.h`, and if `draw()` shows it, a term in `Docs::signature()`. A setting that is kept goes in `Settings` +
the `REMEMBER` table as always. If main must know (the top panel), a field of `Docs::TextStatus` or a `TextCmd`.
`ui.h` is not opened.

The rules of `Ui` hold inside an area: redraw only on `signature()` (an area's part is its own `signature()`),
640×480 units, colours and radii from `theme.h` through `ui.fillRound`/`ui.drawBox`, a dialog is an overlay
(`ui.pushOverlay`), a bar a finger moves is a slider (`ui.slideGrab`).

## Adding an area (say, a "Books" section)

```cpp
// ui.h: its name and its owner, nothing else
struct Books;
std::unique_ptr<Books> books;
// and its pages in `enum class Page` (Page::Books, Page::BookShelf)

// ui_books.h
#pragma once
#include "ui.h"
namespace bp
{
	struct Ui::Books : Ui::Area
	{
		explicit Books(Ui& u) : Area(u) {}
		bool has(Page pg) const override { return pg == Page::Books || pg == Page::BookShelf; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;
		void open(Player& p);	// what the rest of Ui calls: sets ui.page
		int cursor = 0, scroll = 0;
	};
}

// ui_books.cpp: the definitions, drawing with ui.drawHeader, ui.drawFocus, ui.drawHints...
// ui.cpp, Ui's constructor
books = std::make_unique<Books>(*this);
areas.push_back(books.get());
```

and the new file in `CMakeLists.txt`. What opens it (a tile on Home) includes `ui_books.h` and calls
`books->open(p)`. Nothing in `press`, `pointer`, `tick`, `renderPage`, `signature`.

Things an area does not get by itself, to check when one needs them:

- **Key repeat**: `Ui::repeats` decides by page (it never knew the readers: unchanged). An area whose lists should
  repeat adds its pages there, or `Area` gets a `repeats(Button)` hook when the first one needs it.
- **L2/R2** are the chapter keys over what plays; `press` lists the pages that keep them for themselves.
- **Leaving**: an area sets `ui.page` itself (as `docClose` does) and remembers where it came from.
- **Its textures** are freed when its page closes (as `docClose` does), not in a destructor.

## Checked

Desktop build (macOS, pdfium on), against a build of the commit before, with the scripted harness: 98 screenshots
(49 steps, both panels) byte for byte the same, and the harness's state lines the same.

- **Text** (`doc.txt`): at rest; Options (START), a row stepped; Bookmarks (X), one added; Find (L3: the keyboard, a
  letter typed, Done: found, R the next, B leaves it); Book mode (SELECT) and back; the Brightness sheet (the sun);
  Options by touch; closed.
- **Book** (`novel.epub`): opened; Contents (Y), a chapter chosen; scrolled by keys; the bar held under a finger
  (`down:`/`move:`/shot/`up:`), a drag on the pad, a tap on it; Book mode, a page turned, its options by a tap in the
  middle; closed and opened again at its place.
- **PDF** (`paper.pdf`): single page; Options, Pages (Y) and its cursor; zoom; the next page; Vertical scroll (X); its
  bar scrubbed with the finger held; fullscreen (SELECT) and back; the Brightness sheet from the strip's row; closed
  and opened again.
- **A video behind a reader**: `clip.mkv` opened, paused, left with B, `doc.txt` opened over it (the mini bar under the
  reader); a tap on the mini bar shows the player (the top panel the video); B comes back to the reader; the same
  with `paper.pdf`.
- `core-test` passes.

Two things made a pair differ that were not the change, both seen between two runs of the same build as well: the
header's clock when the two runs straddle a minute (the browser and the player show it; the readers don't), and a
PDF page still decoding when the shot is taken on a busy machine (once in nine runs). The pairs counted above are
runs where neither happened; the video scenario was run with the video paused and both builds inside one minute.
`core-test` is not steady on this machine while other worktrees run it: the first run of the new build said
"3 failed" (which checks was not kept), a run of the old build failed `vizCacheLuts` (tests/core_test.cpp:10070);
the eight runs of the new build after that all passed. bpcore is untouched by this change.

## Not verified

- The handheld (the desktop build only) and the Linux build: no Linux-only file was touched and `ui.h`'s includes
  are as they were, so the arm64 Docker check was not run.
- A comic (CBZ/CBR, the Manga reader turned on) through `Page::DocPages`: the same code as the PDF's, not driven.
- Rename/Move of an open or bookmarked document (`docs->pathMoved`), Recent's "Open from the start"
  (`docs->forgetPlace`) and the dashboard's places (`docs->placeOf`): the bodies are the old lines moved, not driven.

## The other areas: order, and what `ui.h` loses

`ui.h` is 2,608 lines now. Roughly what each area holds there (counted by its section or its members' prefixes):

| # | Area | Pages | In `ui.h` | Notes |
|---|---|---|---|---|
| 1 | **Sound** (`ui_sound.cpp`) | `Sound` | ~50 | The easiest: one page, already dispatched beside the readers in `press` and `pointer`, its state only its own (`eq*`, `fx*`, the preset's name). The Display popup in the same file is not the page's: it becomes an overlay (it needs the stack's `alive()` hook) or stays. |
| 2 | **Web** (`ui_web.cpp`) | `Web` | ~20 | One page. The header's globe (`webInd`) is every page's: it stays `Ui`'s. |
| 3 | **Extensions** (`ui_ext.cpp`) | `Extensions` | ~30 | Small; `mangaOn` and the drawer on Home stay shared. Do it before Manga, which it hands over to. |
| 4 | **Manga** (`ui_manga*.cpp`, 10 files) | `Manga`, `MangaSeries`, `MangaRead`, `MangaSettings`, `MangaSources` | ~1,100 | The big one: lines 1488–2525 and the public channel at 184–265. Not one struct: first **`PageReader`** (the top reader's channel, the pages grid, fullscreen, zoom: what `Docs` shares, see above), then **`MangaRead`** (the reader's page), then the library (`Manga`, `MangaSeries`), then Sources/Java/Servers/Downloads/Net folders, which are already a file each and mostly modal sheets: those go on the overlay stack first (overlay-stack.md, item 4), which empties much of their state before the area is cut. The dashboard's glue (`api_manga.cpp`) reads the store and the downloads through `Ui`: it needs the area's header, as `api_playlists.cpp` needed `ui_docs.h`. |
| 5 | **Settings** (`ui_settings.cpp`) | `Settings` | ~60 | `settingRows`/`changeSetting`/`drawSettingRow` are shared with Manga's settings page and START's menu tab: the area exposes those; `inMenu`/`menuTab` stay `Ui`'s. After Manga, so the shared row drawing has one more user known. |
| 6 | **Playlists** (`ui_playlists.cpp`) | (pseudo-folders of `Browse`) | ~90 | Not pages: `browseDir` `PLROOT`/`PLDIR`. `has()` can't tell by page, so this is a **helper struct** owned by the browser's area rather than an `Area`, or `has()` grows to "page and folder". The picker is an overlay first (overlay-stack.md, item 6). |
| 7 | **Browse + Home + Files' operations** (`ui_browse.cpp`, `ui_home.cpp`, `ui_fileops.cpp`, `ui_forms.cpp`) | `Browse` | ~350 | The most entangled: the list, Home, the sections, the network form and found servers (pseudo-folders), the clip, the file job, the mini bar. Cut inside it by pseudo-folder (Home, the form) once Playlists has shown how. The mini bar and what plays behind (`bgPlay`, lines 2526 on) are every page's: `Ui`'s. |
| 8 | **Player** (`ui_main.cpp`, `ui_menus.cpp`) | `Main`, `Tracks`, `Chapters`, `UpNext`, `Info`, `About`, `Exit`, `Playing` | ~400 | Last: `press` and `pointer` are mostly this area's special cases today (the hold to play here, predicted position, L2/R2, the photo's pan and flick, START's menu over any of them). What remains in `Ui::press` after the others have left **is** the player; it moves when that code is short enough to read as one area's `key()`. |

Expected: after 1–3 `ui.h` is about 2,500; after Manga about 1,400; after Settings, Playlists and Browse about 900;
with the player out, `ui.h` is the frame (pages, areas, overlays, sliders, drawing helpers, the mini bar, the
keyboard, quick settings): 500–600 lines, and an edit inside any area recompiles that area's files.

Each step is this one again: a header for the struct, the `.cpp`'s definitions renamed `Ui::X::`, `ui.` put where
the compiler says a member of `Ui` is used without an object (`-ferror-limit=0` lists every one, with its line and
column: the readers' 277 were placed by a script from that list, none by hand), the outside callers moved to a few
methods, and before/after screenshots of every page of the area.

**An area's position in `press`/`pointer`** is where the readers' was. Sound, Web, Playing, Extensions and the Manga
pages are dispatched in the lines right after it today, so they move there without a change of order; a page that is
handled earlier in `press` today (Exit's dialog, Settings' B, the menu's tabs) must keep what runs before it, so
check that page's special cases above the dispatch before moving it.
