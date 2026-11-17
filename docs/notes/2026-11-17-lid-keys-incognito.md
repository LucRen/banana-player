# The lid-shut keys and enhanced incognito (2026-11-17)

Two of the spec's `docs/redesign/lid-and-incognito.md`: option B of the keys with the lid shut (the owner's
decision), and incognito's lid, sleep and Web-dashboard rules. The settings rows are in Settings › Power &
screens (`set-screen`); the screens are `lid-keys`, `incog-lid`, `incog-web` and `incog-web-ask`.

## 1. The keys with the lid shut

**When:** the lid is shut, **When the lid closes: Keep playing** is on, and something is loaded (playing or
paused, not stopped; a photo doesn't count). The same condition drives both input devices and the input filter.

- **Volume keys** (`src/volumekey.*`, Linux only, a thread of its own). The player finds `/dev/input/event*`
  whose `EVIOCGNAME` is `gpio-keys-volume` (BananaOS's name for it; apart from the power key and the lid
  switch), opens it and takes it with `EVIOCGRAB`. BananaOS never sees a volume key while it is held, so
  nothing moves by itself.
  - down, let go before 0.6 s: one volume step (+ / −5%), on the letting go, on the main thread through
    `Ui::setVolumeTotal` → `Volume::set` → `/usr/libexec/banana/volume set N` (never amixer). It stops at
    100%: a boost set before stays, the keys don't add to it. A browser that has the sound gets the step in
    its own volume instead (the existing `volume` event), as with the lid open.
  - still down at 0.6 s: **Next** (Volume +) or **Previous** (Volume −), once; the volume is not touched. One
    hold, one step. The same Session intents L / R send (`SessionHost::step`), so a video and a song behave
    alike and a browser output follows.
  - down 5 s: taken as stuck and ignored until it is let go (a pocket). A press that comes back after the
    conditions ended does nothing.
  - The grab goes when the lid opens, Keep playing is off, nothing is loaded, the player quits, or the main
    loop stopped beating for 3 s (a frozen player must not keep the keys from bananad). `EBUSY` (bananad has
    it) leaves it off quietly. The thread never touches the Player or the Ui: its actions are SDL user
    events, handled in `main`.
  - Logs: `volume keys: … taken: lid shut…`, `Volume + down`, `Volume + press: one volume step (120 ms)`,
    `Volume + press: volume 45%`, `Volume + held 601 ms: next`, `… stuck, ignored until let go`,
    `volume keys: released (…)`.
- **Power** is the existing `src/powerkey.*`: a press (let go before 2 s) is play / pause; a 2 s hold lets
  the key go and asks bananad to sleep. It now also reads incognito's choice (below).
- **Shoulder keys** (`lid-shoulder`): **Ignored** (default), **Hold** (L / R previous / next, L2 / R2
  previous / next chapter, when held 0.6 s; one hold, one step) or **As usual** (on the press). With
  **When the lid closes: Pause** the row is at 45% with "The lid pauses" and changes nothing. The keys act on
  what plays, never on the page left open; Button hotkeys off silences them, as it does the player's buttons.
- **Everything else under the lid is ignored** while those conditions hold: the face keys, the D-pad, START,
  SELECT, F, the stick and the touch screen. They are swallowed before `Ui::press`/`pointer` see them; the
  first press after the lid opens acts (the condition is read per event, not once per loop).

The spec's option B table is the behaviour; one grammar: a press changes only what a second press undoes
(volume, play / pause), changing the track takes a hold.

## 2. Incognito and the lid, sleep and the dashboard

**Setting:** **Lid or sleep in incognito** (`lid-incognito`): **Nothing**, **Close all** (default) or **Quit**.
Incognito itself is still never saved. The choice is applied first:

- **The lid closes** (`SleepWatch`'s callback): with Nothing it does what When the lid closes says. With Close
  all or Quit the watch's thread only pushes an event; main decides (it owns the Player and the Ui), so a
  handheld that suspends before it finishes closes on waking, before the next picture.
- **The power key's 2 s hold:** `PowerKey` gets the choice each pass. With Close all or Quit it sends an event
  instead of asking for the sleep; main closes (or quits), then calls `askBananadSleep`.
- **Idle sleep:** `IdleSleep::tick` takes a `beforeSleep` hook; main runs Close all / Quit just before the
  sleep request.

**Close all:** `Ui::incognitoCloseAll` sets Stop (`stopPressed`: the Session's Stop everywhere), closes a
book / PDF / comic (`Docs::docClose`), the Manga reader (`mangaCloseReader`), every sheet, the menu and the
picker, goes Home (`Home::goHome`), and leaves incognito with no note. main stops its photo viewer first (a
player of its own). Places, read marks and resume points are kept as Stop keeps them; copies and downloads go
on. With Keep playing, nothing is loaded after it, so `LidGuard` restores BananaOS's lid sleep and the
handheld sleeps a few seconds later.

**Quit:** main quits; with a power hold it asks for the sleep first.

**The Web dashboard:** while incognito is on the server does not listen. `Ui::applyIncognito(true)` stops it
through main's `webPower`, but never writes `settings.webDashboard`: the switch keeps its saved value.
Leaving incognito (the switch, Leave incognito on the page, or the lid / sleep's Close all) starts it again
when the switch is on; the same PIN and signed-in browsers come back (both live in the state folder and
`WebServer::start` reads them again). `webView` has `WEB_INCOGNITO_VIEW`: the page shows the switch at 45%
with **Off in incognito**, one sentence and **Leave incognito** (Y), no QR, address or PIN. Quick settings'
Web tile is at 45% with the same note and A only says "Leave incognito to use the Web dashboard"; Settings'
tile's summary says it too; the header's globe is hidden. Play on… is not offered. A browser that was
connected gets the server's generic `bye` `off` and `web/js/down.js` shows the spec's **The dashboard is off**
text with **Try now**; the word incognito is never sent.

Turning incognito on with a browser signed in opens `IncognitoAsk` (an overlay): "Turn on incognito?" with
the browser count and name, "What it plays stops; its place is kept." when a browser is the output, the lid
choice in a dim line, **Cancel** (B, yellow) and **Turn on** (A). With nobody signed in it turns on at once.

## Checked

Desktop build (macOS): `cmake --build build -j`; `./build/core-test` ends `core: all passed` (including the
Settings table's new rows); `cmake --build build --target manga-check` prints `manga: all passed`.

Scripted runs, generated media only (a testsrc H.264 MP4 with a quiet sine, a quiet sine MP3):
Settings' ten groups and Power & screens scrolled to the two lid rows; incognito on from Quick settings
(its second line and the Web tile at 45%), the Settings tiles with "Off in incognito", the Web dashboard page
held off with Leave incognito, and leaving it. Shots: `redesign-shots/settings/settings-groups-*`,
`set-playback-*`, `set-subtitles-*`, `set-music-*`, `set-photos-*`, `set-reading-*`, `set-files-*`,
`set-power-*`, `set-power-lidincog-*`, `set-web-*`, `set-updates-*`, `set-about-*`, `incog-quick-*`,
`incog-settings-tiles-*`, `incog-web-*`, `incog-web-back-*` (not committed).

## Not verified

- **The handheld's own devices**: no lid switch, `gpio-keys-volume` or power key on a Mac. The arm64 Docker
  check (DEVELOPMENT.md) builds the Linux code; the grab, the 0.6 s and 5 s timings, a press's volume step and
  BananaOS's `volume` script were not run. The 100% stop and the browser-volume branch are code paths only.
- **The ask sheet with a browser signed in**: no browser was connected in these runs, so it was not drawn.
  The text is built from `WebServer::clients()`/`playTabs()`; a handheld and a phone are needed for it.
- **The dashboard coming back in place** (same PIN, same signed-in browsers) after incognito: not driven; the
  server's restart path is the one its own Off/On switch already uses, and `start()` reads `web.conf` and
  `web-sessions.tsv`.
- **Close all / Quit at the lid, the power hold and idle sleep**: no lid, power key or bananad on a Mac. The
  hooks and logs are in place; the screenshots stop at the settings, quick settings and dashboard screens.
- The quick settings' tile (and the whole quick sheet) is still the old centred box, not the redesign's tile
  sheet: only its Web / Incognito lines and the 45% treatment changed here.
