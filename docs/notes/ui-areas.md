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
each (seven since `pointerFirst`, eight since `pointerDownLate`, below), and they name no area:

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

`Docs` used them as `ui.readerStatus`, `ui.mangaPagesOpen(p)`… Splitting them then would have meant either two copies
or a third struct invented before the Manga reader's own shape was known. They became **`Ui::PageReader`** when Manga
became an area (below, "PageReader and Manga").

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

## Sound and Web (the second and third areas), and Extensions (not done then)

Same pattern, `ui.h` 2,601 → 2,550 (Sound) → 2,542 (Web). Two things the first area did not need:

- **`Area::pointerFirst()`**: Sound and Web have always taken their touches before the updater's pop-up, the lock, the
  mini bar and the list/home touches (the mini bar is drawn on Sound with no dialog open, and the page's own taps come
  first), while the readers' come after the mini bar. `Ui::pointer` asks `area()` twice: where the Sound and Web pages
  were dispatched, for an area that says `pointerFirst()`; and where the readers' were, for the rest. Default false:
  `Docs` is unchanged. An area whose page is dispatched early in `pointer` today sets it.
- **An area with a global part**: both pages have a part every page shows, and it stayed `Ui`'s.

### Sound (`src/ui_sound.h`, `src/ui_sound.cpp`)

`Page::Sound`: the two tabs, the band cursor, the preset list/Options/Delete dialogs, naming a preset, the stash of
Custom, the effect cursor, `closeSound`'s way back to Settings or Home. 56 lines of members left `ui.h`.

- **Callers elsewhere**: `sound->open(p, fromSettings)` (Settings' Music row, the song's line), `sound->nameDone(p)` and
  `sound->eqNameSubmit/eqNameNote` (the keyboard, `ui_forms.cpp`), `sound->dialogOpen()` (the mini bar is not under a
  dialog), `sound->tabRect(t)` (`buttonRect`), `sound->eqPick/eqToggle/fxChoose/eqCustom/stashOn` (the dashboard's
  `/api/eq`, `/api/fx`, `web/api_player.cpp`).
- **Stayed `Ui`'s**: the **Display popup** (SELECT over a song's page, `page == Main`: `displayOpen`, its chips, its
  keys), still in `ui_sound.cpp`; the equalizer's touch ids (`SOUND_TAB0`, `EQ_*`, `DLG_ROW`, `FX_ROW`, which
  `buttonRect` and `act` share with the grid), the slider (`ui.slideGrab(SL_EQ…)` and `slide`), `eqWebDirtyAt` (the
  dashboard writes it, `tick` commits the curve a second later), the keyboard's `EQ_NAME` field. The signature's `|snd`
  term split in two: the area's (tab, band, cursor, dialogs, list, preset serial, stash, slider held) and Ui's `|dsp`
  for the Display popup. Key repeat and L2/R2 are page lists in `Ui::repeats`/`press` and did not change.

### Web (`src/ui_web.h`, `src/ui_web.cpp`)

`Page::Web`: keys, touches, the drawing (QR code, address, PIN boxes, the empty views), where it was opened from
(`webBack`), New PIN's two presses and the empty page's button. 8 lines left `ui.h` (the area is named `webPage`:
`web` is main's server). Its tick (the address read every 10 s, the panel dark after 90 s without a touch) is
`Web::tick`.

- **Stayed `Ui`'s**: everything every page shows of the dashboard: the header's globe (`webInd`, `drawWebIndicator`,
  `webIndicatorShown`), `webView/webClients/webPin/webAddress`, `webTurn` (the quick settings' row), `webIp`,
  `readWebIp`, `webInputAt` and `pinArmed` (the screen-off timer and the draw loop use them), `pinFont` (loaded
  with the other fonts), and `webSig`, which is every page's signature term. `Web::signature` is therefore empty: the
  page draws from state `webSig` and `Ui`'s own signature already cover. `Ui::openWeb()` stays as the one way in and
  forwards to `webPage->open()` (Home's menu, a settings group, the globe, the quick settings, the manga sources).

### Extensions: stopped then, done with Manga since (below, "PageReader and Manga")

Not cut alone, because a clean split was not possible, for three reasons the code shows:

1. **The page is one of three that share its state.** `extSheet`, `extCursor`, `extPressed` and the touch ids serve
   Manage extensions (`Page::Extensions`), the Extensions **drawer** on Home (a pseudo-folder of `Page::Browse`:
   `browseDir == EXTDIR`, drawn from `ui_browse.cpp`, keyed in `Ui::press` before the Browse keys) and the manga
   reader's page (`extKey` and `extPointer` hand `Page::Manga` to `mangaKey`/`mangaPointer`; `openManga` clears
   `extPressed`). `has(Page)` can name only the first.
2. **Its touches are split across `Ui::pointer`.** The lift is taken early (after the manga reader's drag, before the
   header) and the down late (after the header, "<" and the mini bar), for all three of those pages at once. An area's
   `pointer` is called for every kind of event at one point, so it would take the down before the header and the
   mini bar. It needs a third dispatch point (`pointerDown`) and the drawer as part of the area, and it hands over
   to Manga's pointer.
3. **`signature` and `act`**: the `|ext` term is shared by the three pages (`mangaOn` is in the settings too), and
   `Ui::act` forwards the ids `EXT_TILE..EXT_DONE` to `extAct`.

So it belongs with Manga: the natural unit is one area for "extensions and what they host" (the drawer, Manage
extensions and the manga reader's pages), cut after `PageReader` (the table's #4). Cutting only `Page::Extensions`
today would leave `ui.h` with the same members and a struct that calls back into `Ui` for the rest.

### Checked (Sound, Web)

Desktop build (macOS), against a build of `main` (42cd748) with the scripted harness (`--press`, `BP_SHOTS`, a fresh
`--state-dir` per run, `settings.conf` touched to a fixed time, a generated song and clip, the player paused so
nothing moves; `core-test` passes after each commit).

- **Sound**, four runs, 51 bottom-panel shots: the song's page and the Display popup before and over it; a tap on the
  song's line opens Sound; bands stepped and edited by keys, Custom made, the presets before/after (Y/X), On/Off
  (SELECT), the tab to Effects and back, an effect chosen, B out; Options (START), Save as new preset on the
  keyboard (a letter typed, Done), Rename, Delete (the question, Cancel/Delete), All presets (the list, scrolled,
  one picked); by touch: both tabs, the previous/next preset, the preset name (the list, a drag, a row), a band dragged
  with `down:`/`move:`/`up:` shot mid-drag, a double tap on a band, Reset, Options, a tap outside a dialog, the back
  icon; and Settings > Music > Sound FX (through START's menu) in, B back to its row, in by a tap, B, B.
- **Web**, four runs, 21 pairs (bottom and top, 42 shots): Settings > Web dashboard opens the page (on, with the
  QR code and PIN), New PIN armed (Y), Turn off (X), Turn on (A), B out; by touch: the header's globe, New PIN, Turn
  off, Turn on, "<", the globe again; the No Wi-Fi view (`BP_WEB_IP=none`) and the Couldn't start view
  (`BP_WEB_FAIL=1`) with "Try again". localhost only, `BP_WEB_FORCE=1 BP_WEB_PIN=ACDE BP_WEB_PORT=8093`.
- A song's page carries a paused clock and the mini bar's progress sliver that differ by a pixel or two between two
  runs of the same build (the pause lands a few ms apart), and the header's clock by the minute. The Sound
  comparison masks the mini bar's strip and the clock (and the song page's header and progress rows for the shots of
  the song's own page), and checks the strip separately (a mini bar that appeared or vanished would differ by
  thousands of pixels, not by a few); with that every Sound shot is identical to `main`'s, and so is every Web shot
  without any mask (top panel included). Two runs of `main` against each other give the same result.

### Not verified (Sound, Web)

- The handheld and the Linux build (no Linux-only file changed; `ui.h`'s includes are as they were).
- The Web page's tick: the address read again after 10 s, the panel dark after 90 s (the body is the old lines
  moved, not waited for). A second browser connected (the "N browsers" line, the globe turning yellow).
- Sound opened from Settings outside START's menu (`soundFromMenu` false) and the Rename of a preset that is the one
  playing while its list is open by the dashboard; the dashboard's `/api/eq` and `/api/fx` (they now go through
  `sound->`; the web smoke was not run).

## PageReader and Manga (with Extensions): the fourth and fifth

Two commits, `ui.h` 2,542 → 2,403 (PageReader) → 1,356 (Manga and Extensions; the manga headers' `#include`s went
with it, so `ui.h` no longer pulls `javaext.h`, `manga*.h`, `pagecache.h`, `coverworker.h` into its 35 files).
`ui_manga.h` is 1,099 lines, `ui_reader.h` 169.

### PageReader (`src/ui_reader.h`, `src/ui_reader.cpp`): a plain struct, not an area

`Ui::PageReader` is nested in `Ui` with a `Ui&` (as an area is) and owned by `Ui::pageReader`, but it is **not** an
`Area`: it has no page of its own (`Page::DocPages` is `Docs`', `Page::MangaRead` is Manga's). Nothing in `press`,
`pointer` or `renderPage` dispatches to it; the two areas call its views from theirs (the grid's keys, touches and
drawing, fullscreen's drawing). It has no `signature()` either: what it draws was already in the two pages' terms
(`Docs::signature` for DocPages; `|rs`, `|pg`, `|full` in Manga's for MangaRead), unchanged.

- **Moved:** the channel (`MangaReadRequest`/`mangaRead`/`takeMangaRead`, `ReaderCmd`/`readerCmds`,
  `ReaderStatus`/`readerStatus`, `ReaderBottom`, `ReaderThumbs`, `readerOpen`); the chapter shown
  (`mangaReadSeriesId/ChapterId/ChapterTitle`: Docs clears the ids and names the document); the seek
  (`mangaSliderPage`, `mangaSeek`); the zoom (`mangaZoomAsked/Step/Toggle/To`); Try again (`mangaRetry`,
  `mangaRetryRect`, `drawMangaRetry`); fullscreen's pages (`FullTex`, `mfTex`, `mfGen`, `mfWaiting`, `mfTagUntil`,
  `mfLastPage`, `drawMangaFull`, `mfFree`, and `readFilterNow`/`readDimNow`, which only it reads); the pages grid
  (`mangaPages*`, `ThumbTex`, `mpTex`, `mpGen`, `mpMade`, `drawMangaPages`, `mangaPagesOpen/Close/Key/Pointer`,
  `mpFree`, `MPG_*`, `mangaPageRect`); the dragged lists (`MangaListDrag`, `mlist`, `mangaListBegin/Pointer`,
  `MG_LIST_SLOP`), which Docs' menus use too.
- **Stayed `Ui`'s:** the Brightness sheet's `openLight`, `lightFilter`, `lightNote`: they are the `LightSheet`
  overlay's and serve the text viewer as well, which is not a page reader.
- **What it reaches outside itself**, besides `ui.`'s drawing: two things that are the Manga reader's, through
  `ui.manga->`: `takeMangaRead` fills the request's Book and place from the store (`mangaBookFor`, `mangaWithinFor`),
  and `drawMangaFull` times out and draws Manga's controls sheet, Book tag and first-time toast over the pages. They
  were one function; the order (pages, tag, Try again, controls, toast) is kept by leaving it so.
- **main** takes the channel as `ui.pageReader->takeMangaRead`, `->readerCmds`, `->readerStatus`, `->readerBottom`,
  `->readerThumbs`; the types are `Ui::PageReader::ReaderCmd`… Docs uses `ui.pageReader->`.

### Manga (`src/ui_manga.h`; `ui_ext.cpp`, `ui_manga*.cpp`): one area for extensions and what they host

`has()`: `Extensions`, `Manga`, `MangaSeries`, `MangaRead`, `MangaSettings`, `MangaSources`. **One struct**, not one
per page or per file: the switch, its sheet and the touch ids are shared by Manage extensions, the drawer and the
app (above, "Extensions: stopped"); the app's tabs, a source's grid and series, a server's pages, the Downloads tab
and the Download sheet share the store, the worker's jobs and serials, the covers and the finger (`mangaPressed`,
`mangaView`: a source's page and a server's are views of `Page::Manga`, not pages). Splitting reader / browse+sources
/ downloads / servers would have cut through all of those; the files stay split as they were.

- **Moved:** everything `ui.h` had from "extensions (ui_ext.cpp)" to "the manga reader's settings" (~980 lines), and
  the public part main and the dashboard use: `MangaFilterSession`, `mangaWithinFor`, `MangaWebChapter/Series`,
  `mangaWebChapters`, `mangaWebDownload`, `mangaDlQueueChapters`, `mangaNet`, `mangaStorePublic`,
  `mangaPageCacheRef`, `mangaCoverCacheRef`, `coverWorker`, `coversReady`.
- **Stayed `Ui`'s:** `mangaOn` (Home's tile reads it), `stateDirPath` (the readers' places and the playlists use it
  too), `drawLoader`/`loaderShown` (every page's loading mark; `Ui::animating`), `alert`, the keyboard's field numbers
  (`SRC_URL_FIELD`…), Settings' row ids (`EXT_NOTE`, `MANGA_CLEAR_ROW`, `MANGA_JAVA_ROW`), and the page lists in
  `Ui::repeats` and `press`'s L2/R2 rule.
- **Every page's parts are the area's state, called by `Ui` where they were:** the browser-check dialog
  (`manga->mangaErrOpen`, `drawMangaErr`), the extensions' notice pill (`drawExtNotice`, `extNoticeText` in the
  signature), `mangaVideoGuard` and `mangaEngineWatch` in `tick`, the workers' answers while it's on
  (`mangaBrowsePoll`, `mangaDlAskPump`, `mangaNotesTick`), the covers freed in `~Ui`, `act`'s `EXT_*` ids.
- **Home's drawer** stays Browse's (`browseDir == EXTDIR`): `press` calls `manga->extDrawerKey`, `ui_browse.cpp`
  `manga->drawExtensionsDrawer`, `pointer` `manga->extPointer` at its two places (the lift early, the down late, as
  before), and `signature` adds `manga->extSignature()` when Browse is on it and the page isn't the area's (the area
  adds the same term on its own pages, for `Extensions`/`Manga` or `browseDir == EXTDIR`, as the one old term did).
- **Touch order: `Area::pointerDownLate()`** (new, default false). `Ui::pointer` asks `area()` where the readers' were
  for every event but a late down, and again after "<" and the empty page's button for the late down. Manga answers
  true on every page but the reader's (whose down always came early). `Manga::pointer` is the old sequence: a late
  down goes to `extPointer` (Extensions, Manga), `mangaPointer` (MangaSeries, MangaSources) or the settings list
  (MangaSettings); otherwise the reader's page to `mangaPointer`, a source grid's moves and lift to `mangaPointer`, a
  lift on Extensions or Manga to `extPointer`, and false for the rest (the old `if (pt.kind != Down) return false`).
  What lay between the two points for these pages was only "<" and the empty page's button (the menu's tabs and
  Exit/Playing are other pages), so the order is the same.
- **`Ui::settingsDown(x, y)`**: the Manga settings page's rows are Settings' list (`settingRows`/`changeSetting`/
  `drawSettingRow`, `listTouch`); its down was the Settings block's in `pointer`. The block's lines became this
  method, called by Settings and by `Manga::pointer`, at the same point of the order.
- `key`: the five old lines, at the area's point (`Page::Playing`'s check, between, is another page). `draw`: the six
  page draws. `signature`: the `|mg`/`|ms`/`|rs`/`|pg`/`|full` block, `|ext` on its pages and the Manga settings'
  `|mc`/`|mj` moved from `Ui::signature` unchanged; `Ui` keeps `|me`, `|xn`, `|ld` (every page's).
- `tick`: the three page ticks (`mangaTick` on the reader, `mangaSourcesTick`, `mangaDownloadsTick`) now run at the
  area's point of `Ui::tick`, before `fileJobTick`, `rootsCheck`, `mangaEngineWatch` and the playlist's flush instead
  of after them. None of those reads or writes what the three touch.
- Names inside are unchanged: the diff is `ui.` in the area's files (placed by a script from the compiler's errors,
  as before: 1,087), `manga->` in `Ui`'s files (84), `u.manga->` in `api_manga.cpp` (79), `ui.manga->` in main
  (`mangaNet`, `coverWorker`, `coversReady`, `mangaPageCacheRef`). `ui_manga.h` is included by the area's 11 files,
  main, `api_manga.cpp`, `ui_reader.cpp` and the 7 files of `Ui` that call into it (`ui.cpp`, `ui_draw.cpp`,
  `ui_browse.cpp`, `ui_home.cpp`, `ui_settings.cpp`, `ui_forms.cpp`, `ui_fileops.cpp`).

### Checked (PageReader, Manga)

Desktop build (macOS, pdfium on) against a build of `main` (bbdc026), each commit, with the scripted harness: a fresh
`--state-dir` per run (`settings.conf` with `manga-reader = yes`, touched to a fixed time), `BP_BROWSE` a generated
library (a Manga folder with two series of CBZ chapters of numbered pages, `doc.txt`, `paper.pdf`), the network denied
to the process (`sandbox-exec`, localhost only), both panels shot and compared byte for byte (the header's clock
masked), and the harness's state lines compared. `core-test` passes after each commit.

- **Scenarios** (keys and touch): the drawer and Manage extensions (the switch by its chip and its row, the
  first-time sheet's Done, the empty drawer and its button, "<"); entering Manga (the engine's offer declined with B
  or Down: never A); the four tabs by L/R and by taps; Browse's rows (On this handheld's list, Manage sources: the
  Sources page, its Available/Repositories segments by R and by a tap, its Filter menu; Add server's form; Network
  folders); Downloads and its drag; History; the Library's covers, a drag on the grid, a series' page and its rows;
  the reader: fullscreen and back (SELECT, the button), the pages grid (Y, moved, A), the zoom (Up, A), the Reading
  menu (START, rows stepped), the Brightness sheet (its button), the reading mode (X), a page (R), the next and the
  previous chapter (R2/L2), the controls over fullscreen by a tap, a double tap's zoom; the Manga settings page (START
  on the Library: rows stepped by keys, a tap, a drag); and Files' readers: `doc.txt` (Options, Bookmarks, Book mode),
  `paper.pdf` (Options, Pages and its cursor, the next page, Vertical scroll, the bar held, fullscreen, closed and
  opened again) and a CBZ from Files (the PDF/comic viewer, the Manga reader on: the grid, fullscreen, Options).
- **PageReader commit**: 5 scenarios, 202 shots: identical but one top-panel shot (the text reader opened again at
  its place, `d16`): `main` against itself gives the same difference (the place lands a frame early or late).
- **Manga commit**: 7 scenarios, 294 shots, identical; the same `d16` pair again, and its bottom panel once ("0%" or
  "1%" under the title, the same race). The final build after a header reorder: five of the scenarios again, the same.
- `tools/web-smoke.sh` against the Manga commit's build, `api_manga.cpp` having changed (localhost, `BP_WEB_FORCE=1
  BP_WEB_PIN=ACDE BP_WEB_PORT=8093`, a generated `Music/Yellow Album`): the reader on, 207 passed; off, 202 passed;
  none failed. (Its seek and hand-off checks need songs longer than 30 s: a first run with 20 s ones failed those three.)

### Not verified (PageReader, Manga)

- **The manga engine (Java/Suwayomi)**: never started. It isn't on this machine's state, its download was declined
  at every entry, and nothing was fetched: a Java source's page, the relink, the engine's check, the lighter
  runtime's offer, the engine's notice pill, `mangaVideoGuard` with a video playing, all as the old lines moved.
- **Anything that needs the network**: a repository's sources (none added), a source's grid and series, filters,
  the Download sheet and a running download queue, a saved server (the form opened, nothing tested or saved), a
  network folder (none saved), the dashboard's manga routes beyond what the smoke test asks, Find on the network.
- Book mode (the handheld held sideways), a reader error's dialog (`mangaErrOpen`), History with entries, the
  Library's options sheet and On this handheld's delete.
- The handheld, and the Linux build: `ui.h`'s `#include`s changed (the manga headers moved to `ui_manga.h`; every
  file compiles on macOS), but the arm64 Docker check installs its packages from the network, so it was not run.

## The other areas: order, and what `ui.h` loses

`ui.h` is 1,058 lines now (2,608 when this table was drawn; Sound, Web, PageReader, Manga with Extensions, Settings, Browse with Playlists and Home are done: only the player, #8, is left). Roughly what each area held there (counted by its section or its members' prefixes):

| # | Area | Pages | In `ui.h` | Notes |
|---|---|---|---|---|
| 1 | **Sound** (`ui_sound.cpp`) (done) | `Sound` | ~50 | The easiest: one page, already dispatched beside the readers in `press` and `pointer`, its state only its own (`eq*`, `fx*`, the preset's name). The Display popup in the same file is not the page's: it becomes an overlay (it needs the stack's `alive()` hook) or stays. |
| 2 | **Web** (`ui_web.cpp`) (done) | `Web` | ~20 | One page. The header's globe (`webInd`) is every page's: it stays `Ui`'s. |
| 3 | **Extensions** (`ui_ext.cpp`) (done, in Manga's area) | `Extensions` | ~30 | Shares its state with the drawer on Home and the manga pages, and its touches are split across `Ui::pointer`: done together with Manga (`Area::pointerDownLate`). |
| 4 | **Manga** (`ui_manga*.cpp`, 10 files) (done: `ui_manga.h`, one area; `PageReader` first, `ui_reader.h`) | `Manga`, `MangaSeries`, `MangaRead`, `MangaSettings`, `MangaSources` | ~1,100 | Planned as several structs (`PageReader`, then `MangaRead`, the library, then Sources/Java/Servers/Downloads/Net folders after their sheets went on the overlay stack). Done as `PageReader` plus **one** `Manga` area: the pages share the store, the worker's jobs, the covers and the finger, and a source's and a server's pages are views of `Page::Manga`. Their modal sheets are still flags inside the area (overlay-stack.md, item 4, is still to do, now inside `ui_manga.h`). The dashboard's glue (`api_manga.cpp`) includes `ui_manga.h`. |
| 5 | **Settings** (`ui_settings.cpp`) (done: `ui_settings.h`) | `Settings` | ~60 | `settingRows`/`changeSetting`/`drawSettingRow` are shared with Manga's settings page and START's menu tab: the area exposes those; `inMenu`/`menuTab` stay `Ui`'s. After Manga, so the shared row drawing has one more user known. |
| 6 | **Playlists** (`ui_playlists.cpp`) (done: `ui_playlists.h`, a plain struct the browser owns) | (pseudo-folders of `Browse`) | ~90 | Not pages: `browseDir` `PLROOT`/`PLDIR`. `has()` can't tell by page, so this is a **helper struct** owned by the browser's area rather than an `Area`, or `has()` grows to "page and folder". The picker is an overlay first (overlay-stack.md, item 6). |
| 7 | **Browse + Home + Files' operations** (`ui_browse.cpp`, `ui_home.cpp`, `ui_fileops.cpp`, `ui_forms.cpp`) (done: `ui_browse.h`, the area; Home a plain struct, `ui_home.h`) | `Browse` | ~350 | The most entangled: the list, Home, the sections, the network form and found servers (pseudo-folders), the clip, the file job, the mini bar. Cut inside it by pseudo-folder (Home, the form) once Playlists has shown how. The mini bar and what plays behind (`bgPlay`, lines 2526 on) are every page's: `Ui`'s. |
| 8 | **Player** (`ui_main.cpp`, `ui_menus.cpp`) | `Main`, `Tracks`, `Chapters`, `UpNext`, `Info`, `About`, `Exit`, `Playing` | ~400 | Last: `press` and `pointer` are mostly this area's special cases today (the hold to play here, predicted position, L2/R2, the photo's pan and flick, START's menu over any of them). What remains in `Ui::press` after the others have left **is** the player; it moves when that code is short enough to read as one area's `key()`. |

Expected: after 1–3 `ui.h` is about 2,500; after Manga about 1,400 (it is 1,356); after Settings, Playlists and Browse about 900;
with the player out, `ui.h` is the frame (pages, areas, overlays, sliders, drawing helpers, the mini bar, the
keyboard, quick settings): 500–600 lines, and an edit inside any area recompiles that area's files.

Each step is this one again: a header for the struct, the `.cpp`'s definitions renamed `Ui::X::`, `ui.` put where
the compiler says a member of `Ui` is used without an object (`-ferror-limit=0` lists every one, with its line and
column: the readers' 277 were placed by a script from that list, none by hand), the outside callers moved to a few
methods, and before/after screenshots of every page of the area.

**An area's position in `press`/`pointer`** is where the readers' was. Sound, Web, Playing, Extensions and the Manga
pages were dispatched in the lines right after it, so they moved there without a change of order (Manga's late downs
with `pointerDownLate`); a page that is
handled earlier in `press` today (Exit's dialog, Settings' B, the menu's tabs) must keep what runs before it, so
check that page's special cases above the dispatch before moving it.

## Settings (the sixth): `Ui::SettingsPage`

`src/ui_settings.h`, `src/ui_settings.cpp`; `Page::Settings`; named `SettingsPage` (`Settings` is the settings struct),
owned as `settingsPage`. `ui.h` 1,356 -> 1,347: the page's state was small (the open group lives in `Ui`, below); what
moved is its code: the groups' tiles and a group's rows (`settingRows`), their help, stepping (`changeSetting`: the
table's rows, the history and cache Clear, the backlight rows, the updater's rows) and drawing (`drawSettingRow`,
`draw`, the subtitle style's `drawPreview`), `stepGroup`/`openGroup`, `styleFromSubs`, the group tiles' rect, the page's
keys (`key`: the groups' grid, the rows, Y/X between groups), its down (`pointer`: the neighbours, a row, a tile:
`settingsDown`) and the Controls group's `|bl` term of the signature (`signature`).

- **Callers go through it**: `settingsPage->changeSetting/drawSettingRow/settingHelp/settingsDown` (Manga's settings
  page, `Page::MangaSettings`: its rows are this list's; `Ui::settingsDown` became `SettingsPage::settingsDown`, the
  same lines, called by `Manga::pointer` at the same point), `settingsPage->settingRows/openGroup/stepGroup` (the
  list's lift in `Ui::pointer`, the group buttons in `act`, Sound's way back), `settingsPage->styleFromSubs` (the
  subtitle style row of the Tracks page, `showTab`, Sound, Web).
- **`menuBack(p)`**: in START's menu the keys run before an area's (`press` does L/R tabs, B, START, the Exit page,
  then the area), so the two B cases that belong to this page (back from the subtitle style opened from the Subtitles
  list; back from a group to the tiles) were cut out whole into this method and `press` asks it for B on this page, at
  the same point, before `closeMenu`.
- **`pointerDownLate()` is true**: the page's downs were after the header's "<", the menu's tabs and the empty page's
  button, as Manga's; its moves and lifts were already the list's (`Ui::pointer`'s `listTouch` block), after which
  nothing but `return false` was left for a move or a lift, so the area's `pointer` returns false for them.
- **Stayed `Ui`'s, on purpose**: `setGroup` (the open group: `showPlayer`'s `playerBack`, the dashboard's `webBack`,
  Browse's way-back stash all copy it, and `ui.h` reads it inline), `SetGroup` (GROUP_*), the row ids (`STYLE_ROW0`..
  `MANGA_JAVA_ROW`, `SET_ROW0`), the list's measures (`SET_Y`, `SET_H`, `SET_SHOWN`, `HELP_Y`, `SET_ARROW_X`, shared with
  Manga's page and the document menus), `GROUP_PREV/NEXT` (`buttonRect`), the list's lift in `Ui::pointer` (one block
  for the tracks, the browser, Info, Settings and Manga's settings), `setBrightness` (the quick settings' too), the
  `Settings` table (`settings.cpp`, not part of this).
- **Key repeat and L2/R2** do not name this page (`Ui::repeats` by direction, L2/R2's page list), so unchanged.

### Checked (Settings)

Desktop build (macOS) against a build of `main` (2c2e654) in a separate directory, the scripted harness (`--press`,
`BP_SHOTS`, a fresh `--state-dir` per run, `settings.conf` touched to a fixed time, the network denied to the process
by `sandbox-exec` with localhost only, generated media), both panels shot and compared (the header's clock masked), and
the harness's state lines compared. `core-test` passes. Six scenarios, 130 shot files (both panels), all identical, logs identical:

- START's menu: the tiles, a group opened by A, every group walked with X (a row stepped in each but Updates), Y back
  twice, Left/Right/Up/Down on the tiles, B to the tiles, B out; the subtitle group's preview and Text encoding's help.
- By touch: a tile, a row's right and its left arrow, a finger down and moved (shot mid-drag) and up (the list
  scrolls), the back icon, the neighbours' buttons (X/Y chips), a drag on the tiles and on a group, L/R tabs.
- Home's Options > Settings, and out by B twice to Home.
- The Manga settings page (`manga-reader = yes`, the engine's offer declined with B): rows stepped by keys, a tap, a
  finger dragged (the list scrolled) through `settingsPage->settingsDown`.
- A video paused behind: Tracks > Subtitle style... opens the Subtitles group (`styleFromSubs`), a row stepped, B back
  to the Tracks list on its last row, and Y/X away from the group, B to the player.
- Music > Sound FX (A) and B back to its row, the Web dashboard tile (A) and B, X from the last group into the Web page.

### Not verified (Settings)

- The handheld and the Linux build (no Linux-only file changed). Settings' Updates group with a real updater (Check,
  Update, its states): on the desktop the updater is off, so its rows were drawn but not driven. The Controls group's
  brightness rows and `|bl` signature term (no backlight on the desktop; `BP_BACKLIGHT_DIR` was not used). The Manga
  page's Clear cache and Extension support rows (the engine's download is never started in a test).

## Playlists: stopped at first, then done after Browse (2026-11-15)

The first attempt stopped (the reasons below, kept); the order it proposed (Browse first, then Playlists inside it) is
what was done next, in the section after this one.

Not done then, for the same kind of reason as Extensions was: there is no page to cut at, and the state is shared.

1. **It is not a page.** A playlist's list and a playlist's items are `Page::Browse` with `browseDir` `PLROOT` or
   `PLDIR` + its file (`isPlDir`), next to Home, Files, Network and Recent. `Area::has(Page)` and `Ui::area()` choose by
   page, so an area cannot be asked "is this a playlist page"; and `Ui::press`/`pointer`/`draw` for Browse are
   `ui_browse.cpp` (1,475 lines), where the playlist rows, the Edit mode's keys and the grip drags are interleaved
   with the Files' and the tabs' (`plCur`, `plLoaded`, `plDrag`, `plSel`, `plMissing`: ~60 uses there, `ui_menus.cpp`,
   `ui_draw.cpp`, `ui_home.cpp`).
2. **Its rows are Browse's.** `plRows` fills `browseRows` from `plCur` and `plRefresh` keeps Browse's cursor and scroll,
   so every playlist method reads or writes `browseRows`/`browseDir`/`browseTab` (31 places in `ui_playlists.cpp`
   alone); the area would call `ui.` for most of its lines.
3. **The select mode, the Add picker and the busy card serve more than playlists.** `selMode` (Edit, Add files, Pick a
   folder) is also Files' and Network's way to tick files and add them, `pickerOpen` is opened from Main's Add square
   (`ui.cpp`), the Playing tab, a Recent card, Files' Y menu and a folder's menu (`openAddOne`, `openPicker` from
   `ui_menus.cpp`, `ui_browse.cpp`), `addPending` and `cancelAdd` are the long add's card, and `Ui::press`/`pointer`/
   `tick`/`signature`/`animating` test `pickerOpen`, `selMode`, `plDrag` and `addPending` at six of their points as
   overlays-before-the-page, which an area's one dispatch point does not model (the picker is drawn and keyed over any
   page).
4. **What must keep working crosses it:** `Ui::flushPlaylist` (main at quit and on a lid close, `SessionHost`, the
   dashboard's routes), `plTellPlayer` -> `SessionHost::playlistEdited` (called from `plSaved`, `addTo`, the dashboard's
   edits and `ui_menus.cpp`'s Remove), `plStore` (73 uses in 8 files and the dashboard's `api_playlists.cpp`,
   `api_player.cpp`), `plOrder` (the dashboard's Now playing). Moving them is a rename of 100+ call sites with no page
   dispatch to take out of `ui.h`; the gain would be about the 120 lines of members.

The natural order is the one Extensions took: **Browse first** (one area for Home, Files, Playlists, Network, Recent
and the drawer, which needs the late/early touch hooks and a dispatch by `browseDir`, not only by page), then Playlists as
a plain owned struct inside it (as `PageReader` is), with the select mode and the picker as overlays (`Ui::Overlay`,
docs/notes/overlay-stack.md) rather than flags: the picker is already a dialog, and the select mode's header and bar
could be one. The first two were done (below); the overlays were not.

## Browse, then Playlists and Home inside it: the seventh (2026-11-15)

Three commits, `ui.h` 1,349 → 1,261 (Browse) → 1,127 (Playlists) → 1,058 (Home). `ui_browse.h` is 132 lines,
`ui_playlists.h` 168, `ui_home.h` 97.

### Browse (`src/ui_browse.h`; `ui_browse.cpp`, `ui_fileops.cpp`, `ui_forms.cpp`): the area

`Ui::Browse` (`has()`: `Page::Browse`), owned as `browse`. Home, Files, Playlists, Network, All recent and the
Extensions drawer are folders of this one page (`browseDir`), so they are one area; what is told apart by folder is
told apart inside it, as before.

- **Moved:** the folder shown and its rows (`browseDir`, `browseRows`, `browsePaths`, `placeIn`, `enterFolder`,
  `browseUp`, the ".." row, `fitList`, the empty pages' action), the sections (`browseTab`, `tabDir`, `switchTab`,
  `openSection`, the header's markers), the network listing's serials and state, Files' own operations (the clip and
  its bar, Rename, the network folders' actions and copies) and the Network tab's form and found servers.
- **Dispatch:** `key` is the old Browse block of `press` at the area's point (the updater's pop-up over the list, the
  busy card, the picker, SELECT/L3 over what plays, the select mode, Home, All recent, the list's keys);
  `pointerDownLate()` is true (its downs were after the header's "<" and the empty page's button), its lifts go where
  the readers' do (only the drawer's lift was Browse's). Three blocks shared with other list pages were cut out of
  `press`/`pointer` into `Ui` methods so both call the same lines: `popupKey` (the updater's pop-up), `listKey` (a list
  page's keys) and `listDown` (a finger down on a list's rows).
- **Stayed `Ui`'s:** the shared list's drawing and keys (`drawTracks`, `rows`, `choose`, `listKey`, `listDown`: the
  track lists use them), the cursor and the scroll (every list page's), the Options menu (`ctx*`: the mini bar's hold
  opens it on any page), the updater's pop-up, the file job's card and Info's sheet (over any page; `fileJobActive()`,
  which main's idle sleep reads, is unchanged), the places (`roots`, `rootsCheck`), and the Browse touches that
  `Ui::pointer` takes before the area's point (All recent's tabs, Home's quit dialog, the busy card, the picker, a
  grip's drag, a finger on Home): they call into `browse->` where they always were.

### Playlists (`src/ui_playlists.h`, `ui_playlists.cpp`): a plain struct of the browser's

`Ui::Playlists`, owned by the area (`browse->playlists`), not an `Area` (as `PageReader` isn't): a playlist is a
folder of `Page::Browse`, so nothing dispatches to it by page; Browse and `Ui` call in.

- **Moved:** the store (`plStore`, Liked built in), the playlist open (`plCur`, `plLoaded`, `plOrder`) and its rows,
  its edits and how they reach the session (`plSaved`, `plTellPlayer` → `SessionHost::playlistEdited`, `plFlush`),
  the select mode (Edit, Add files, Pick a folder: `selMode`, `selKey`, its header and bar), Move and the grip's drag
  (`plDrag`), the Add to... picker and the busy card of a network folder being added (`pickerOpen`, `addPending`),
  naming a playlist, and the dashboard's edits (`plWeb*`).
- **Stayed `Ui`'s:** `flushPlaylist`, `playlistSerial`, `plIsLiked` (main, `SessionHost`, the dashboard call them;
  they forward), Like (`setLikedNow`, `setLikedPath`: the heart over any page), the Options menu that names a playlist
  (`ctxTitle`, `ctxNote`), `playRequest`, `SelMode`'s values, the Main page's source line, `whereOf`; and the `|pl` and
  select-mode terms of `Ui::signature` (the picker and the busy card are drawn over other pages).
- **Not overlays yet:** the picker's keys are taken at two points of `press` (over the player before the keyboard's
  turn is over; on the browser after the updater's pop-up and the busy card), its New playlist... opens the keyboard
  over it, and the select mode is the list's own header and bar. Moving them onto the stack (overlay-stack.md, item 6)
  is a behaviour-sensitive change of its own, not part of a pure move.

### Home (`src/ui_home.h`, `ui_home.cpp`): a plain struct of the browser's, not the shell

Home is not the shell: the frame every page shares (the header, the mini bar, quick settings, the overlays) is `Ui`'s
and stayed there. Home is the browser's first folder (`browseDir == HOMEDIR`) and All recent (`RECENT`), so it is a
plain struct owned by the area (`browse->homeView`; `home` is `Ui::home()`, the page to go back to), like Playlists.

- **Moved:** the Recent list (`RecentItem`, `recent`, `listRecent`, `recentKeepOrder`, `recentInOrder`,
  `recentEdited`, `homeListedFor`), All recent's tabs, the focus (`homeZone/Strip/Grid/Col`, `homeFresh`), the strip
  and its easing (`stripOff`, `stripT0`, `setStrip`, `stripStep`), the tiles (`tileRect`, `tileSub` and its cache), the
  keys (`homeKey`, `goHome`, `homeOpen`), a finger on a card or a tile (`homePointer`, `homeTouch`, `homePress`,
  `homeHoldAt`), B's quit dialog (`quitOpen`, `quitCursor`, `drawQuitDialog`), the card's menu and Home's Options
  (`ctxRecent`, `openRecentMenu`, `openHomeOptions`) and `drawHome`.
- **Signature:** Home's `|home…|quit…` term moved from `Ui::signature` into `Browse::signature` (asked only while
  `page == Browse`, the only page that draws Home: `drawTracks` draws it when `!inMenu`, and that is still in the
  string the same way).
- **Stayed `Ui`'s:** `openFile` and `showInFolder` (the dashboard and the Playing tab use them), the mini bar and the
  playing file's chip (every page's; their code is still in `ui_home.cpp`), `showHidden` and `mangaOn` (mirrored for
  drawing), and the calls in from `Ui`: the quit dialog taking every finger and a card's drag in progress
  (`Ui::pointer`, before the area's point), the strip's easing and Recent listed again when what plays changes
  (`Ui::tick`), the hold that opens a card's menu (`draw`'s timers), `stripT0` in `animating`, `recentEdited` from
  the dashboard (`/api/recent/remove`, `/api/recent/clear`) and from Move/Rename. The Extensions drawer is still Browse's
  (`browseDir == EXTDIR`, keyed and touched through `manga->`, as in the Manga section).
- Names inside are unchanged: the diff is `homeView->` / `browse->homeView->` and `Home::` on the enum and constants.

### Checked (Browse, Playlists, Home)

The three commits rebased onto `main` (458ecd1, idle sleep: no conflict; `fileJobActive()` reads `Ui::fileJob`,
which stayed `Ui`'s). Desktop build (macOS) against a build of 458ecd1 in a separate directory, the scripted harness
(a fresh `--state-dir` per run, `settings.conf` touched to a fixed time, the network denied to the process by
`sandbox-exec` with localhost only, a generated library: three 40 s tones, two 20 s clips, `doc.txt`), both panels
compared byte for byte with the header's clock masked, and the state lines compared. `core-test` passes.
Ten scenarios, 268 shot files (134 pairs):

- **Home by keys** (a seeded history: four Recent cards), 72 identical: the strip stepped to All recent, All recent
  and its tabs (L/R), a row's menu, Options, back to its card; the tiles walked in every direction and up to the
  strip; a card's menu and Home's Options; B's quit dialog, its rows, Cancel; each tile opened (Files, Playlists,
  Network, the Extensions drawer and Manage extensions) and B back; START's menu.
- **Home by touch**, 30 and 30: the tiles by a tap, the strip dragged (shot mid-drag) and swiped back, a held card,
  the quit dialog (a tap outside, a tap on Cancel), All recent's tabs tapped, a held row. Identical but a few pairs
  where a song had started from a card: its progress on the card and the mini bar moved by a pixel or two (a run of
  the base build against itself differs at the same shots).
- **Files and the select mode**, 32: a folder's menu, Copy (the clip bar), carried to another folder, let go (X);
  Select files, two ticked, Add to... (no playlists: the name's keyboard), Done; the new playlist, an item's menu, the
  playlist's Options. Identical but the last two pairs: Shuffle play was chosen there (a random first song; the logs
  differ only by it).
- **Playlists**, four runs, 60: a three-item playlist played from its Options, paused; the heart (L3) on the player;
  Liked with it; then, with the song paused behind, the playlist opened, an item moved (Move, Down, Done) and one
  removed: `SessionHost` logs the two `Requeue`s in both builds; quit (the playlist flushed: both state folders' `.m3u`
  identical, the reorder and the removal in it) and reopened from that state: the playlist, Delete playlist pressed
  twice; Like from a Recent card's menu, then Liked. Identical but the first run's paused pairs: where the pause
  landed (0.4 s or 0.6 s, in the state lines too) moves the progress bar and the mini bar's sliver.
- **Network**: the empty page, Y's Add connection form, its rows stepped, B, B Home. **Files by touch**: rows
  tapped, a ".." tap, a drag, a held row (its menu), the header's "<" back to Home. 28, identical.
- `tools/web-smoke.sh` (localhost, `BP_WEB_FORCE=1 BP_WEB_PIN=ACDE BP_WEB_PORT=8093`, a generated
  `Music/Yellow Album` of 60 s songs): the reader on, 207 passed; off, 202 passed; none failed.

### Not verified (Browse, Playlists, Home)

- The handheld and the Linux build (no Linux-only file changed; `ui.h`'s includes are as they were).
- Anything on the network: a saved connection, a network folder (its listing, its file operations, a network folder
  added to a playlist and the busy card), Find on the network.
- A paste (Copy/Move carried out), Rename, Delete in Files, the file job's card: not driven (they write the library);
  the bodies are the old lines with `browse->`. The grip's drag in Edit by touch, Add files and Pick a folder, the
  picker opened from the player's Add square, the dashboard's playlist routes beyond the smoke test.
