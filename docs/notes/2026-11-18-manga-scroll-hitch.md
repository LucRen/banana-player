# Manga scroll hitches while flicking (2026-11-18)

## The report

On the handheld, reading manga in the one-screen / fullscreen strip and flicking: the scroll hitches every few
seconds. The log named the page decodes (`timing: page N decoded in 20 to 57 ms`, and 200 to 400 ms for the CBZ
pages) and many `manga: progress` lines per page, so the suspicion was decode and progress saves on the main thread.

## What was already on a worker

`MangaReader::init` starts two `decodeLoop` threads. They read the page (or archive/PDF), call `decodePage` /
`decodeBand` (libavcodec + the swscale conversion), and push an SDL user event (`evReader`, registered by
`TopScreen` with `SDL_RegisterEvents(1)`) when a page is done. No decode happens on the main thread: the `timing: page
N decoded in … ms` line is printed by `MangaReader::frame` when the result is taken in, but `d.ms` is measured on the
worker that decoded it. The pixels arrive as a ready RGBA buffer (the "texture source"); `frame()` moves it into a
`shared_ptr` and hands it to `PageRenderer`, which uploads it.

The texture upload was already bounded: `PageRenderer::draw` uploads one 512-row tile per frame, from the buffer
decoded at the view's width (no full-page upload, no rescale on the main thread). A 1024x1575 page is 4 tiles over 4
frames, not one 9 MB upload.

## What did run on the main thread during a scroll

1. **Progress saves with an fsync, every second** (the periodic hitch). `Ui::Manga::mangaTick` recorded the page as
   the reader moved and then called `mangaSave()` once a second. `MangaStore::save` uses `writeFileDurable`, which
   writes a `.tmp`, `fsync`s the file, renames it, and `fsync`s the directory. On an SD card that is tens of
   milliseconds, on the main thread, every second while a chapter is read.
   Fixed: saves are debounced to `MANGA_SAVE_MS` (5 s). Leaving the chapter still saves at once, and a new
   `mangaFlush()` (called on quitting, next to `flushPlaylist`) records the current place and writes it before the app
   leaves. The write is still `writeFileDurable` (atomic rename, same as before), just far less often, which also cuts
   card wear.
2. **`stat` per wanted page per frame for URL chapters.** `MangaReader::want()`, `pagesToFetch()` and
   `jumpTargetIn()` called `fileThere()` (a `stat` on the card) for every wanted page on every pass while the view
   moved, and `ReaderFetch::pass()` ran the same checks again. On a remote chapter that was dozens of card lookups a
   frame. Fixed with `MangaReader::onCard`, filled at open for pages already cached and at each `pageArrived`; the
   three callers read the set instead. `fileThere` now runs once per page, not per frame.
3. **The route watcher** (`/run/banana/asound.conf`, every 500 ms) only `stat`s and reads the file only when its inode
   changes. It is cheap, and now it is skipped entirely when nothing is loaded: a new file opens its sound on the
   route of the moment. The idle-sleep hold (5 s) reads `/run/banana/game.pid` and `/proc/self/stat` (tmpfs), the
   idle rule reads the battery supply every 10 s (sysfs), the lid guard touches files only on a transition, and the
   power/volume key beats are atomic stores. They were left as they are, and the watch below would name them if they
   ever cost a frame.
4. **Taking many decoded pages in one pass** is still done as they arrive (`decode take`); each is a few ms at most
   (`makeThumb` is 96 px wide, `addPage` only stores the buffer). If the new log shows this over 30 ms with more than
   one page, the next step is to cap the pages taken per pass.

## The new log line

`src/framewatch.h` is the frame-time watchdog. Main starts a pass after its event wait and ends it after both panels
are drawn; code that can hold the pass adds its time under a tag. A pass of 30 ms or more is logged at most once a
second:

```
timing: frame 43 ms (events 1, ui tick 4, progress save 31, reader sync 1, top draw 5, reader frame 2, reader draw 3, upload 2)
```

The tags are `events`, `ui tick`, `progress save`, `idle hold`, `idle tick`, `lid guard`, `route check`,
`reader sync`, `decode take`, `want`, `fetch`, `reader frame`, `reader draw`, `upload`, `top draw` and `bottom draw`.
Nested tags (a draw containing an upload) both appear, so the line says both where the pass went and what inside it
was heavy.

## Checked

- `cmake --build build -j`
- `./build/core-test`: `core: all passed`
- `cmake --build build --target manga-check`: `manga: all passed`

The new line is not in the older `handheld-log2.txt`: the next log from the handheld will pin the next hitch (or show
that the debounced save and the cached card lookups removed it).

## Follow-up: the durable write moved off the main thread

The 5 s save still ran `writeFileDurable` (file fsync, rename, folder fsync: tens of ms on the SD card) on the main
thread, so a flick could still hitch once every five seconds. It is now only copied there.

- `src/writequeue.h` / `writequeue.cpp`: `WriteSlot` is the pure one-slot queue (a push replaces whatever wasn't taken
  yet); `DurableWriter` owns one detached thread, started on the first write and never destroyed (like `DurationWorker`:
  no join in a destructor on main's path). The main thread copies the texts into the slot and returns; the thread takes
  the newest bundle after any write in flight and puts each file down with the same `writeFileDurable`. It never touches
  Player or Ui. `tests/core_test.cpp`'s `testWriteQueue` pins the replace-latest rule.
- `MangaStore::filesToSave()` serializes the two texts on the caller's thread (`save()` is still the synchronous form for
  a caller that wants it). Both `library.tsv` and `progress.tsv` are carried every time: a chapter's place moves its
  series' `lastRead`, so the library text changes with the progress as well, and a failed write is retried rather than
  remembered as done.
- main creates one writer (`Ui::Manga::writer`) beside the other workers and never destroys it. `mangaSave()` copies
  (`FrameWatchScope "progress save"` now measures only that copy); `mangaFlush()` records the place, queues it and waits
  at most a second on quit. `mangaCloseReader`'s save is queued the same way. The dashboard's two store edits
  (`api_manga.cpp`) go through `mangaStoreChanged()`, so they never race the writer on the same `.tmp`.
- The same 2 s throttled durable save ran while a document was read (`Ui::Docs::docPlacesSave`). It is on a reading hot
  path too, so it uses its own writer (`Ui::Docs::writer`); `docFlush()` records the place and waits at most a second on
  quit. `doc-bookmarks.conf` stays synchronous: it is written only when the user adds, renames or removes a bookmark.

### The other durable and fsync writes, and whether they were a risk

| Write | Where / when | Risk while reading or playing |
|---|---|---|
| `settings.conf`, `eq-presets.tsv` | `Player::writeFile`, at most one sync a second per file | Only when a setting or EQ preset is changed (a deliberate action, not a loop). Not moved. |
| `resume.tsv` (positions) | `Player::saveResume`: on open/stop/end/wake, and every 60 s while playing | One fsync a minute is not a hot path, but it is the next candidate if the log ever shows it. Not moved. |
| `history.tsv` (recent list) | On file open, end, clear/remove, a move | Not per frame; coalesced by `writeFile`. Not moved. |
| `playlists/*.m3u` | Edits debounced 1 s, flushed on leaving and quit | While a playlist is edited, not while reading or playing without an edit. Not moved. |
| `doc-places.conf` | Docs' tick, every 2 s while a document is read | **Hot**: moved to `DurableWriter` (above). |
| `doc-bookmarks.conf` | A bookmark added, renamed or removed | User action only. Not moved. |
| `durations.tsv` | `DurationCache`: append uses `fflush`, the rare whole rewrite fsyncs | Runs on `DurationWorker` and the web probe's thread, not main. No risk. |
| page cache index, covers, series cache, viz cache, subtitle cache, photo wav | `writeFileAtomic` (no sync) or a cache's own write | No fsync; a lost one is rebuilt. No risk. |
| sources/repos/servers/keys/lid notes | Install, remove, edit, a lid transition | Rare, deliberate actions. Not moved. |
| `cbzwrite`, `treecopy`, `xfer`, the web upload | Download, copy/move and upload jobs (worker threads) | Not main, and not reading or playback. No risk. |

Checked: `cmake --build build -j`, `./build/core-test` (`core: all passed`, including `testWriteQueue`) and
`cmake --build build --target manga-check` (`manga: all passed`). The next handheld log should show `progress save` near
zero; a remaining frame there is the two texts' serialization, not the card.

