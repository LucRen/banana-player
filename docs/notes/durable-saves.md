# Durable saves

Why: an exFAT SD card was found with `doc-bookmarks.conf` holding font bytes and `settings.conf` holding the subtitle
style file's text. Probably a power cut mid-write. On exFAT a rename after an unsynced write can leave the new name
pointing at clusters never written. We can't stop filesystem damage; we can make every save of the user's own records
leave the old file or the new one whole.

## The helpers (`src/filesave.h/.cpp`, bpcore)

- `writeFileAtomic`: tmp + rename, no sync. For anything rebuilt when lost.
- `writeFileDurable(path, text, mode = 0644)`: write tmp, `fsync` it, `fchmod`, rename, `fsync` the folder. For anything
  the user would miss. `mode` 0600 for secrets. A missing folder returns false and leaves no `.tmp`.
- `playlists.h` still includes `filesave.h`, so older includes work. Test: `testFileSave` in `tests/core_test.cpp`.

## Durable now

settings.conf, resume.tsv, history.tsv, eq-presets.tsv (all through `Player::writeFile`); doc-places.conf and
doc-bookmarks.conf (`ui_docs.cpp`); playlists and Liked (`PlaylistStore::save`); servers.conf (`netstore.cpp`);
servers.key (`secret.cpp`, was tmp + rename); web.conf and web-sessions.tsv (`web/server.cpp`); Manga: library.tsv
(already), progress.tsv, repos.tsv, installed.tsv and the repo indexes (`mangarepos.cpp`), the download queue
(`dlqueue.cpp`), manga servers (`mangaservers.cpp`), source prefs (`mangasource.cpp`), Java repos/sources
(`mangajava.cpp`).

Before this, `Player::writeFile` (the settings, resume, history, EQ presets), `netstore`, the secret key, doc places and
doc bookmarks, the Manga prefs and servers wrote tmp + rename with no sync. Nothing wrote in place without a tmp except
the files under "left alone".

## Cost

An fsync on an SD card can take tens of ms.

- **settings.conf**: `setSettings` runs on every change (a toggle, a menu step, held Left/Right on a value). The EQ band
  drag already deferred (`persist = false`, written by `commitEq`). `Player::writeFile` now coalesces per file: the
  first save goes at once; a save of the same file within 1 s waits in `pendingWrites` and the main loop's
  `flushWrites()` writes the last value when its second is up; `Player::shutdown` flushes everything. So at most one sync
  a second per file, and the last value always lands, quit included. `clearHistory` drops a waiting history save so it
  can't bring the file back.
- **resume.tsv**: saved on open/stop/end of a file, every 60 s while playing, at a wake and at quit. Photos return early.
  Pressing Next quickly writes resume + history per file: now at most one sync a second each.
- **doc places**: already throttled to 2 s (`docPlacesSave(false)`), forced on a move. **doc bookmarks**: one write per
  user action.
- **playlists**: the browser's edits already wait a second (`flushPlaylist`), also flushed at quit.
- **web.conf / web-sessions.tsv**: on server threads, under the server lock: on a sign-in, a sign-out, a PIN change, and
  a session renewal (at most once per half-week per browser). Rare; a stall of tens of ms there is fine.
- **Manga progress/library** (`Ui::mangaSave`): reading marks it dirty and `ui_manga.cpp` writes it at most once a second
  (already throttled); the library file is only rewritten when it changed. **Download queue / repos / servers / prefs**:
  on state changes of a download (finished, failed, queued) or user actions in the Manga pages, not per progress tick.
- Nothing moved to a background thread.

## Left alone

- Caches: viz `.pgm` maps and `vizcache`, `pagecache`, webcache, thumbnails, `mangaseriescache`, crash log, downloaded
  runtimes (`javaext`), Java `extensions.json` (refetched; `writeFileAtomic`), the IR wavs, `.idle-icon.bgra`.
- The circle/visualizer maps in `settings.cpp` (`writeCircleMaps` and the others): derived data rewritten if missing, and
  25 files with an fsync each would cost startup time. They stay tmp + rename.
- `backlight.cpp` and `panel.cpp`: BananaOS's own files (`/data/banana/backlight`, sysfs), tiny, written per brightness step.
  An in-place write, but not ours to move, and a slider drag would pay a sync per step.
- `.hidden` marker (empty file), `game.pid`, the manga/web download parts (written to `.part` then renamed after a
  download; the content is re-fetchable).
- `fsync` on macOS doesn't force the disk cache (`F_FULLFSYNC` would). Only the handheld matters here.
