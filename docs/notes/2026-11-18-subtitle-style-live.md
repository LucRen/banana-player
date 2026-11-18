# A subtitle style change on what plays (2026-11-18)

The owner, on the handheld (0.7.28): changing a subtitle style setting did not show on the video playing until the file
was opened again. Both ways in — View › Subtitles › **Subtitle style…**, and Settings › **Subtitles** — persisted the
value as they should (settings.conf), and both reach `Player::setSettings`.

## What it was

`setSettings` already sent every key's `sub-*` property to mpv, async, whenever the style differed. What did not change
at once was the **subtitle track mpv had already made**: its look is the one it was made with, so the new values only
reached a track made after them — the next subtitle file, or the one reopening the file adds. That is why the change
looked saved but not shown, and why it appeared later.

## The fix

`Player::setSettings`, after the properties, when one of the five visual keys changed (`lookChanged`, Text encoding
excluded: its own block below reloads a showing file for the code page):

- A **local external** plain subtitle: mpv's `sub-reload` (its documented way, already used for Text encoding) reads the
  file showing again, on the spot, async.
- An **embedded** plain subtitle: choose it again **off and on, by its id** (`sid`, as a track is chosen; never "auto"),
  which makes mpv make the track again. Async, so the main thread never waits for mpv.
- **ASS/SSA** were left alone then (see the follow-up below); pictures (PGS, VobSub) aren't styled. A **network**
  file's subtitle isn't fetched again at each step.

The setting is still written to settings.conf exactly as before (`persist`), and the log now says which way it went:
`subtitles: the style changed; its file read again` / `... track N made again`.

## ASS/SSA too (the follow-up)

The owner, on the handheld: with an ASS subtitle a style change showed nothing live, but stopping the video and
playing it again showed it. The open path was not leaving ASS alone. `sub-ass-override` is mpv's default, `scale`
(mpv's manual: "like yes, but also apply --sub-scale"), so an ASS track is made with the style's values too: the
script's own sizes and colours stay, but a font it names that the card doesn't have (on the handheld, everything
but the bundle's Inter, Pixelify Sans and Fusion Pixel) falls back to `sub-font` **when the track is made**. A track
made again by reopening therefore takes the new font; the track already showing kept the old one. No per-file
`loadfile` option is involved — `start`, `aid` and `sid` are all a local file's options carry.

The live path now makes an ASS/SSA track again exactly as it makes a plain one again: `sub-reload` for an external
file, `sid` off and on by its id for an embedded one (both async). That is the track reopening makes, so the result is
the same and nothing else changes; pictures (PGS, VobSub) are still left alone.

Checked with a generated testsrc2 640×480 30 s clip + sine, `BP_FONT_PROVIDER=none` (the bundle's fonts only, as on
BananaOS), `--press`: `clip.ass` beside it (Style `Big`, font `BananaTest Sans`, 64, red) and `clip3.mkv` with the
same ASS embedded, changing Font from Settings › Subtitles (Inter → Pixelify Sans). Before the fix `shot:a-live` was
still Inter while `shot:a-reopened` was Pixelify (the report); after it `a-live` and `a-reopened` are the same
Pixelify line, external and embedded (`e-live`, `e-reopened`), and the log says `its file read again` /
`track 1 made again`. A plain SRT beside a clip still restyles live and matches reopening (size 46 → 56, white →
yellow; `s-live`, `s-reopened`). Shots in `shots-ass/` (not committed).

## Checked

`cmake --build build -j`, `./build/core-test` (`core: all passed`), `cmake --build build --target manga-check`
(`manga: all passed`). A generated `testsrc` + `sine` video (`subtest.mp4`) with `subtest.srt` beside it, `--press`:
size and colour changed from **Settings › Subtitles** and from **View › Subtitles › Subtitle style…**; `prop:sub-font-size`
and `prop:sub-color` went `46` → `56` and `#FFFFFFFF` → `#FFFFE45C` live, and `shot:before` / `shot:after` of the top
screen show the larger, yellow line. The same with the subtitle embedded (mov_text): the off/on path keeps it showing.
`-v` shows mpv's `Reloaded:` after each external change.

## When the browser is the output

The page plays the video and draws its own plain cues (`video::cue` in `web/style.css`), with its own **Size** and
**Position** chips (`cueStyle`, applied live through the `--cue` variable). The handheld's style rows in the page's
**Subtitle style** panel are saved on the handheld and its preview follows them; they do not restyle the page's cues
whether changed live or after reopening — the page's own Size/Position stay the ones that apply there.
