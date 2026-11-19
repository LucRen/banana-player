# Native visualizers: Mirror bars drawn by the player (2026-11-19)

Why: the graph-built simple styles stalled mpv on the handheld (2026-11-19-viz-simple.md, "The stall"). A style the
player draws itself has no graph to stall, no sound reopen on a switch, and costs less. Mirror bars is the pilot; the
path is meant for the other simple styles too.

## How it works

- **The sound: a second decode.** libmpv gives the app no samples. `VizTap` (src/viztap.*) opens the song itself with
  libavformat/libavcodec on a detached worker thread: the first audio stream (what the blank graph's `[aid1]` plays),
  the others discarded, resampled by libswresample to mono float at 32 kHz. It decodes only around the time it needs
  and keeps half a second behind; a jump of the clock (an mpv seek) seeks it too. A network song (bpnet://, a URL)
  keeps the graph Mirror bars: the decode reads local files only and a second read would double the traffic. A file
  the tap can't open falls back to the graph for that song (`viz-native: <file>: the graph style instead`).
- **The clock: mpv's `audio-pts`**, observed (never read on the main thread). It is mpv's position of the sound being
  heard, its output's delay already taken off (`playing_audio_pts`: written pts minus `ao_get_delay`), so no extra
  `audio-buffer` shift is needed (Lite's tap ran ahead of the speakers; this reads the file, not the chain). mpv updates
  it on its playloop ticks: **about 5 a second here**, tied to the blank graph's 5 fps frames, so `VizClock` runs it on
  at the speed between updates; play after a pause restarts it from that moment. `time-pos` would not do: under a
  graph it follows the picture's frames.
- **The spectrum: PFFFT** (third_party/pffft, NEON on aarch64, SSE on x86), 1024 points (32 ms) with a Hann window,
  centred a frame ahead of now (the draw and the swap that follow). 40 log-spaced bands from 40 Hz to 14 kHz (the
  bass left; the lowest bands one bin each), each its loudest bin. The levels are the graph's: showfreqs' log scale
  (−120..0 dB as 0..252), `vizContrast(…, "4", "1.5", "160")`, the bottom 40 steps cut, eased as an LED meter (up at
  once, the whole height down in 0.15 s). One change: **+3 dB an octave from 500 Hz**, since log bands show music's
  falling treble more than the graph's near-linear columns did (`vizMirrorTargets`; a constant to tune by eye).
- **The worker** wakes at the Visualizer frame rate setting (30/45/60), takes one spectrum, stores 40 targets and
  pushes `evViz` (once until taken). Paused or seeking it sleeps; silent for half a second it stops waking the loop.
- **mpv's picture:** the style's graph is `blankGround()` (None's: the theme's ground at 5 fps, the sound through
  `asplit`), so None ↔ Mirror bars and the reader resting it change no graph (no sound reopen) and the audio track is
  never unselected. Art + visualizer: the art is the overlay (as for every local song), the band is ours.
- **The draw** (src/topviz.cpp, `TopScreen::drawNative`): when the tap has new targets or mpv a new frame, mpv
  renders its picture (ground, art, lines, cards; the bars are cut round the cards) **without waiting for a frame's display time** (`render(…, noWait)`:
  with the wait, each 5 fps ground frame held the main thread ~60 ms), the bars are eased by the real time since the
  last draw (not while paused: the picture freezes as the graph's did), 41 quads in the theme's ACCENT and DIM (read
  each frame: a theme change shows at once; the ground follows with None's graph rebuild 500 ms later), and the top
  window is **swapped here** (KMSDRM: nothing else presents the panel while the style is up). Screenshots draw the same.
  Geometry is the graph's doubled: 16 units a bar, 12 wide, the line 4 tall; alone centred at 240, 218 each way at
  most; in the band (rows 280..480) centred at 380, 94 each way.

## Log lines

| line | when |
|---|---|
| `viz-native: Mirror bars for <file> (<mode>), N fps: a second decode, mono 32000 Hz, FFT 1024 (PFFFT), on mpv's audio-pts …` | the tap starts for a song |
| `viz-native: the top panel is the player's: …` | the first native frame after another picture |
| `viz-native: GL ready` (or `no glX` / `shader: …`) | the GL side, once a run |
| `viz-native: <file>: in 5.0 s 34.0 frames drawn a second, 28.6 spectra a second, audio-pts told 5.0 a second, N seeks; decoding aac 44100 Hz 2 ch` | once, 5 s in |
| `viz-native: off for <file> after N frames drawn, N spectra, N seeks (<why>)` | it stops: another style, another file, no visualizer shown, nothing plays |
| `viz-native: <file>: the second decode couldn't open it` + `… the graph style instead` | the fallback |

## Checked on the Mac

Build, core-test (`testVizNative`: bands, a sine's bar is its band's, silence is flat, the easing, the shapes, the
clock), and two scripted runs on generated tones: 45 fps → 46.9 frames drawn a second (the tap's and mpv's), 30 fps →
34; a forward and a backward seek (2 seeks, bars on), pause and play, Peaks and back (one graph change each way),
None and back (no graph change). One screenshot (Art + visualizer) matched the graph's look.

## What may differ on the handheld

The second decode's CPU (an MP3/AAC decode and a 32 kHz resample: a few percent of one A55 core expected; the worker
runs at nice 5); `audio-pts`'s update rate under the bundled mpv 0.40 (the 5 s line says it); GLES's attribute
locations and the swap under KMSDRM; the song's lines (mpv's ASS) are drawn under the bars, so in Visualizer only a very
loud treble bar (they reach row 22) can cross the title at the top left, where the graph's lines stayed on top.
mpv's cards (volume, state, note) are cut out of the bars (`vizCutOut`, `Player::cardsUp`), so they stay readable.

## Left out

The other simple styles (Dots, Steps, Horizon, Barcode, Rings) still use graphs; the shapes and the targets are the
parts to add (`mirrorBarsRects` / `vizMirrorTargets` beside new ones). The tap ignores a chosen second audio track
(the blank graph plays `[aid1]` too).
