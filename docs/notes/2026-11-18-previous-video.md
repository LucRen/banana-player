# Previous on a video (2026-11-18)

**As seen:** 5 s or more into any file, Previous (L, the Prev button, a page's Previous, the List sheet, the lid-shut
shoulder hold) went back to that file's start. The owner wants that for **songs only**: a video's Previous goes to the
video before it at any place, and on the first video of the list it does nothing (no restart, no wrap) unless Repeat
All is on, which wraps to the last as before; songs stay exactly as they were.

**Why it happened:** `Step` in `src/session.cpp` had one rule for both kinds (`posNow >= RESTART_S && n < 0` →
restart), written before a video had to differ. The kind was already in the session: `audioOnly`, set by the output's
`Loaded` (the handheld's `player.playingAudio()`, the page's `audio`), the same field I16 uses for the end of a song
list. No new state was needed.

**What changed** (`src/session.cpp`, `Step`):

- the restart branch is `k < 0 && s.audioOnly && posNow(s, now) >= RESTART_S`;
- the wrap branch is `k > 0 || s.audioOnly || s.loop == LOOP_ALL`, so Next wraps for both kinds and both loops, a
  song's Previous keeps wrapping with Repeat One, and a video's Previous wraps only with Repeat All (a video at its
  first one with Repeat One is `first`, the note "This is the first one");
- **I17** in `checkTransition`: an applied `Step(n<0)` on a video never leaves it on the same item — it moves or is
  refused (the fuzz checks it on every apply).

The browser output needed no change of its own: the page's Previous is a `Step` intent and its `Loaded` already sends
`audio: B.meta.kind === 'audio'`, so the session knows a song there too; a refusal shows the page's own toast.

**Verify:** `./build/core-test` (six named cases: a song at 6 s restarts, a song at 2 s goes previous, a video at 6 s
and 2 s both go previous, the first video does nothing, the first video with Repeat All wraps; plus the fuzz). One
scripted run on the Mac with two generated 10 s videos (ffmpeg `testsrc` + `sine`):

```sh
./build/banana-player <dir>/01.mp4 --state-dir <state> \
  --press "R,info,info,info,info,info,info,info,L,info,info,info,info,info,info,info,L,info,info"
```

`R` puts the second file at 0.8 s; at 6.5 s `L` logs `Step v4→5 … file=01.mp4` (the first) and its clock starts at 0;
at 6.4 s into the first `L` logs `Step rejected (first)` and the clock goes on 7.2, 8.1 — no restart. Before the change
that second `L` seeked the first back to 0.

**Not verified:** a real handheld run and Safari; Repeat One's no-wrap choice for a video's Previous is the reading of
the owner's "unless Repeat All" (Next still wraps under Repeat One), and the `Player::back()` fallback (only used when
the session doesn't hold the item) still restarts a video — every named path sends `Step`.
