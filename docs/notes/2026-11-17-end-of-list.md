# The end of a song list: the last song stays, paused at its start

**Asked:** when the last song of a list ends (Loop off: nothing next, or the only song), do not close the media. The last
song stays loaded, back at its beginning (0:00), paused, with the player screen still showing it, so **A**, the power key
(with the lid shut, Keep playing) or a page's Play plays it again from the start. Repeat all/one unchanged; videos keep
what they did.

## What it did

`Ended` on the last item with Loop off called `finishList()`: `stopItem()` (`index = -1`, `HhUnload`), the note
"Finished", the finished list (`ended`, docs/design/session.md §13) for the pages, and main's `stopPlaying(End)` (the
screens back to the list, the app out at the end of a file it was started with). The same for songs and videos. The
handheld's `player.playing()` (a non-empty playlist) then went false, which the lid guard and the power key read.

## What it does

- **Session** (`src/session.cpp`, `Ended`): after `nextItem()` says there is nowhere to go, a song (`s.audioOnly`, from
  the output's `Loaded`) keeps its `index`, sets `pos = 0`, `playing = false` (so `playV` moves, the page follows),
  bumps `seekN`, and for the handheld adds `HhPause` then `HhSeek(0)`. `load` stays: nothing reloads, no `HhUnload`, no
  "Finished", no `ended` list. A video still takes `finishList()` (index -1, unload, the pages' finished view). The
  branch is per item, so a mixed folder whose last item is a song holds the song; one whose last is a video stops.
- **Idempotent**: the file's end heard again before the seek lands (paused and already at or under 0) returns with no
  change and no effects — `checkSongEnd` can fire on the passes before the seek arrives.
- **Invariant I16** (`checkTransition`, so the fuzz checks it on every apply): an applied `Ended` on the last song of a
  list with Loop off leaves that item, `pos == 0`, paused, the same `load`, and no `HhUnload`.
- **mpv** (`src/player.cpp`): `keep-open=always` while a song is the file (a video's stays `no`). With it mpv pauses at
  a song's end and sends **no `END_FILE`**, so the Player observes **`eof-reached`** and takes that as the end (`finished`,
  the same path an ended file took): a song with one after it goes on to it, the list's last is kept. A song whose
  visualizer/art graph never ends by itself (the picture goes on) is still ended by `Player::checkSongEnd`. Both effects
  are asynchronous (`prop("pause")`, `seek absolute-percent+exact`); nothing waits for mpv.
- **Browser output**: the same state in the session, no handheld effect. `browser-player.js` already follows `playV`
  (pause) and `seekN` (`seekTo(0)`), so the page shows the song paused at 0:00 and its Play resumes it. `index >= 0`
  keeps `finishedList()` off: the finished view no longer appears for a song list, only for a video's and for a list
  the browser couldn't play (`browserCant` still `finishList()`s).
- **Place**: songs were never saved with a position (`saveResume` remembers videos only), so nothing saved the end. The
  session's `pos` is 0.

## The bug 8dfb2f3 left: every song stopped at its end

`keep-open=always` makes mpv pause at a song's end and send no `END_FILE` (reason `eof`), which was the only thing that
set `Player::finished` for a song with no graph — so `Ended` never reached the Session and a song mid-list sat at its
end, paused, instead of the next one playing. `checkSongEnd` didn't take it either: its clock test wants
`position ≥ duration + 0.25`, and mpv stops the clock at `duration` (a 3 s sine MP3: `time-pos 3.0`, `eof-reached yes`,
`pause yes`, no `session: Ended`).

**The fix** (one place, `src/player.cpp`): `eof-reached` is an observed property, and its true for a loaded song sets
`finished`, exactly what an ended file's `END_FILE` did: main's end path sends `Ended`, the Session's `nextItem()` loads
the next song, and only at the list's end is the song kept (the `HhPause`/`HhSeek(0)` above). `keep-open=always` stays,
so that last seek still has a file to land in; a video (`keep-open=no`) still ends by `END_FILE`, and a still-art graph
that never ends is still `checkSongEnd`'s.

**Checked on the Mac** (three 3 s sine MP3s in one folder, started on the first): `a1 (1/3) → b2 (2/3) → c3 (3/3)`, each
step a `session: Ended … fx=[HhLoad,HhPlay] out=hh file=b2.mp3` and `Loaded`; the third `session: Ended v6→7
fx=[HhPause,HhSeek] out=hh file=c3.mp3 paused`, and it stays `3/3`, `paused=1`, clock **0:00** (mpv's `time-pos` about
0.2 s: the graph's first frames), Play there. core-test (I16 now pins a mid-list song's advance as well) and
manga-check pass.

## Measured on the Mac, and the one oddity

A 3 s and a 10 s sine MP3 (`ffmpeg -f lavfi -i sine=duration=N`), the handheld as the output: the end leaves the item
at `index 1/1`, paused, and `A` plays it again with no reload (`session: Play ... fx=[HhPlay]`). mpv's `time-pos` reads
about **0.6 s** after the seek to the song's start; a manual pause and a tap at the far left of the progress bar read
the same, so it is the visualizer graph's first frames, not the end state. The clock shows 0:00; the bar is a fifth of
a 3 s file and a sliver of a song of minutes, and playback starts from the beginning. The browser has no graph and seeks
its element to 0 exactly.

## Tested

- `cmake --build build -j`, `./build/core-test`: `core: all passed`, the fuzz (2000 × 200) included; the new named case
  covers the handheld and browser outputs, a single song, the repeated end, Play with no reload, Loop one/all and a
  mid-list end. `cmake --build build --target manga-check`: passed.
- One scripted handheld run with the 3 s sine MP3 (`--press`, `BP_SHOTS`), its `prop:time-pos` / `prop:pause` lines and
  the `shot:end` picture showing the paused song and the clock at 0:00.
- The fix's run: three 3 s sine MP3s in one folder, started on the first — `1/3 → 2/3 → 3/3` by their ends, the last
  kept paused at 0:00 (`session: Ended … fx=[HhPause,HhSeek]`), as above. I16 now pins a mid-list song's advance too,
  and the named case checks its new `load`, `pos 0` and no pause/seek of the song that ended.

## Not tested

The handheld itself (the graph's start offset there, a network song's seek back after its end, the real ALSA path) and
Safari/the dashboard's player with a song played to its end. Worth checking: a folder of one song, a video's list still
saying Finished, and the dashboard's player showing the song paused at 0:00 with Play resuming it.
