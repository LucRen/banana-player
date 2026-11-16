# 2026-11-16 Manga library scan

## The problem as seen

A user with a huge library: opening the Manga app "froze the entire system" for two or three minutes after CBZ and PDF
support landed. Nothing was stuck: the app was reading the card.

## Cause

`Ui::Manga::mangaScan()` ran on the main thread, synchronously, at every open of the app (and after a delete or a
finished download, and when the dashboard asked for a local series first). It called `scanMangaFolder()` for each Manga
folder, and that opened **every chapter file** with `openPageBook` (a CBZ's central directory, a CBR through
libarchive, a PDF through pdfium) for its page count, its first page and its ComicInfo.xml. Nothing was remembered
between opens, so a big card cost minutes of frozen panels every time.

The Downloads tab had the same shape: `mangaDlEnsureCard()` opened every `.cbz` under the Manga folder (to read its
ComicInfo and tell downloads from the card's own series) on the main thread, again every 5 s while the tab showed.

## The design

- **Listing opens no file.** `listMangaFolder()` (mangalocal.h) walks the folders with opendir/lstat as before and
  builds the same series and chapters, but an archive is only its name: `known` false, pages -1, number and volume
  from the name, cover `"archive.cbz#"` (CoverWorker takes an empty entry as the first page). `scanMangaFolder()` is
  now listing + `fillMangaSeries()` of everything: same answers as before (the tests pin it).
- **One thread does the card.** `MangaLibWorker` (mangalibworker.h, main makes it, `evMangaLib`): a scan lists every
  root and answers at once; then it opens each series' first chapter if still unknown (one file a series: what the
  cover, the title and a download's link need) and answers in 250 ms batches; then prunes and saves the index. A
  series page's `fill` (its unknown chapters) and the Downloads tab's `files` go before the rest of the probes. Main
  takes the answers in `Ui::Manga::mangaLibReady()`; the lists keep their cursors by path (`mangaLocalSet`,
  `mangaLocalMetas`). `mangaLocalSerial` is in the signature.
- **Remembered results.** `ChapterMetaCache`: per chapter file, keyed by path and checked against size + mtime, what
  opening it told (pages, first page, ComicInfo number/volume/page count/series/seriesKey/source/title). The file is
  **`<state>/manga/library-index.txt`**, its own file: not in `durations.txt`, not under `<state>/manga/cache` (the
  page cache that Clear cache empties and whose size limit it would count against). One line per file, tab-separated
  and escaped, each ending with its own FNV-1a checksum: a torn or garbled line (the owner's SD cards corrupt) is
  skipped and that file read again. Written via tmp + rename (`writeFileAtomic`: a cache, no fsync). A whole scan drops
  entries under its roots it didn't see; a card that's out keeps its entries.
- **The store.** A new series goes into the store (the Library) only once its first chapter is known, so a folder of
  downloads never shows in the Library first (`inLibrary` false when a known chapter's ComicInfo names a source, as
  before). A series not yet fully known keeps the title the store had (its ComicInfo Series) rather than flapping to
  the folder's name. The store is written once at the scan's end, at once for a series page's fill.
- **What the app shows meanwhile.** The Library is the store's at once (its covers from the store's cover strings);
  "Reading your library… N series" on its label line, as the empty page when there is nothing yet, in the footer of
  On this handheld, and "Reading…" on Browse's row. A series page whose files are still read says "Reading the
  chapters…" and re-sorts in place when ComicInfo numbers arrive. The dashboard's `/api/manga/chapters` for a local
  series answers 202 pending until the listing and that series' files are in (the page already retries on pending).
- **Callers that relied on the scan being done.** Deletes (On this handheld's Delete, the Downloads tab's chapter and
  series deletes) now take the files out of the list at once (`mangaLocalForget`) instead of scanning the card. A
  download opened from the Downloads tab whose series or chapter isn't listed yet lists that one folder now
  (`mangaLocalAddOne`: names and stats only). `mangaMissing` keeps its stat fallback until the first listing. The
  Downloads tab reads each CBZ's ComicInfo from the index and asks the worker for the ones not in it (they show when
  it answers).
- Log lines: `manga: app opened in N ms on the main thread`, `manga: library listed in N ms (...)`, `manga: library
  read in N ms (S series, C chapters, F files opened)`, `manga: <series>: N chapter files read in N ms (K opened)`.

## Numbers (Mac, warm disk cache)

A generated library (ImageMagick PNG pages zipped into CBZs, hand-written PDFs; no copyrighted material): 300 series,
2550 chapters (30 PDF series), 126 MB. `--press "Down,Right,A,A,..."` from Home.

| | main thread blocked at open | files opened |
|---|---|---|
| before, every open | 119 ms, 127 ms | 2550 |
| after, first open | 4 ms (listing 24 ms, probe done at 44 ms, background) | 300 |
| after, second open | 5 ms (listing 24 ms, done at 25 ms, background) | 0 |

The Mac's SSD and page cache make every number small; on the handheld's SD card the open of each file is what took
minutes, and the counts are what changed: 2550 opens on the main thread at every open became 300 in the background
once, then none. Not measured on the handheld.

## How to check

`BP_MANGA_SLOW=8` slows the listing by 8 ms a series, so `shot:` can catch the "Reading your library" line;
`tests/core_test.cpp` `testMangaLocalLazy` pins listing without opening, the probe's file count, fill = full scan, the
index's round trip, a torn line, and pruning.

## Not done / unsure

- A series opened before the probe reached it shows as a local series page even if it is a folder of downloads (the
  redirect to its source series needs its ComicInfo); the fill is asked at once, so this only lasts a moment, and the
  next open is right.
- The scan's probe opens one file per series even for series never shown, which goes slightly past "covers only when
  on screen": needed to keep a folder of downloads out of the Library and to give a series its ComicInfo title.
- A cover requested with the store's string that fails after the listing arrived stays the generated one until the
  next open.
