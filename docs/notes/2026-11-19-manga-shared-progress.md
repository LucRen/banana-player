# 2026-11-19 Manga: one place for a chapter and its download (issue #15)

## The problem as seen

"Bookmark of downloaded manga is not synchronised with bookmark from browsed section." A chapter read online
(Browse → a source's series) and the same chapter's downloaded `.cbz` (Downloads, or *On this handheld*) each kept
their own page, read mark and History row.

## Cause

`MangaStore` keys a place by chapter id. Online (and a card copy opened from the source's series page, through
`mangaDlOpenSourceChapter`) the id is the source's `src:<source>:<key>` under the source's series id. Opened from the
Downloads tab or *On this handheld*, the reader is given the file's path as the chapter id and `local:<folder>` (or
`dl:<folder>`) as the series: a second entry. The CBZ's ComicInfo had `BPSource` and `BPSeriesKey` but not the
chapter, so nothing could map the path back.

## The fix

- **The link stored.** A download now writes `BPChapter` (the queue item's `chapterId`, i.e. `src:<source>:<key>`)
  into its ComicInfo.xml beside `BPSource`/`BPSeriesKey`. It is read into `ChapterFileMeta::ciChapterId`, kept in the
  library index (`ChapterMetaCache`: a 14th field; a 13-field line from before is still read, with no chapter id)
  and into `MangaDlCardChapter::chapterId`.
- **One key.** `MangaStore::linkChapter(path, chapterId, seriesId)` registers the file as a copy of the source
  chapter (in memory only, rebuilt from the files each run, capped at `MAX_PROGRESS`). `progress`, `setProgress`,
  `markRead` and `unread` resolve a linked path to the source id, so every page that asks by path (the local series
  page, the Downloads card, the dashboard, the Library's unread counts) sees the one entry. `setProgress` still moves
  the folder's series' `lastRead` too, so the Library order is as before. `moveSeries`/`moveChapters` (a Java engine
  reinstall) carry the links along.
- **Where links are made.** `Ui::Manga::mangaDlLinkCard()`, at the end of every card scan (`mangaDlEnsureCard`): at
  the app's open (a new forced scan there), when the library worker answers for the card's files, and as before on
  the Downloads tab and a source's series page.
- **Older downloads (no BPChapter).** Matched in the source's chapter list: the series page's own when it is open,
  else its cache file (`<state>/manga/series/<hash>.json`, read once per series per run), by the title the
  downloader wrote (`dlChapterName`) first, then by number. One with no cached list, or no match, keeps its
  path-keyed place, as do chapters added as plain files (no `BPSeriesKey`); it links on a later scan once the series
  page has been opened (which writes the cache).
- **Migration.** Linking merges: a place under the path with none under the source id is renamed to it; with both,
  the later (`at`) place wins, `read` if either was, the row stays in the History while either was visible, and the
  path's entry goes. Logged as `manga: merged|moved the downloaded copy's place <path> into <id>`. It runs at each
  link, so a path entry made before the link was known (a file not read yet) is folded in at the next scan.

## Not changed

The reader's own chapter id for a local open is still the path (its chapter buttons and series page need it); only
the store maps it. A History row of a merged chapter now opens the source's series page (cached list), where the card
copy opens with no network, as it already did.

## Tests

`testMangaStoreLinkedDownloads` (canonical key, merge, a copy's place seen from the online id, unread/markRead by
path), BPChapter round trip in `testComicInfo`, the 13/14-field cache lines in the ChapterMetaCache test.
