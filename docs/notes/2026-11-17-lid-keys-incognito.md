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

### The false lid closes on the handheld (2026-11-18)

The handheld's log had the lid/sleep Close all firing with the lid open and incognito off, always right after
manga or Java-extension activity (`manga: library read`, `javaext: … answering`, `javaext: … idle 300 s:
stopping it`): `incognito: the lid closed: close all`, then `incognito: close all: stop, close, Home,
incognito off`, then the Session's Stop.

**Cause: the SDL user-event numbers, not the lid switch.** `SDL_RegisterEvents(13)` reserved
`evMpv`…`evMpv+12`, but `evVolKey` and `evLidIncognito` were written as `+13` and `+14`. The Manga app's two
later registrations (`evMangaFetch`, `evMangaWorker`) took exactly those two numbers. So every MangaWorker wake
— a library read, the Java extension starting, answering or its idle stop — arrived as `evLidIncognito`. Its
handler (`main`) took any code other than Quit as **Close all**, never checked incognito, and ran before
`evMangaWorker` in the chain, so it also swallowed the worker's event. Nothing in the lid path caused it:
`SleepWatch`'s `lidClosed`/`SW_LID` read, the `SIGCONT` wake and the loop's time gaps were right. That is also
why the log line is the main thread's `close all` and never the watch's `(decided on the main thread)` one.

**Fix:**

- The pool is 15 (`evMpv` + the 14 below it), so each event type has its own number again.
- `bp::incognitoLidAction` (`src/lidtrigger.*`, bpcore, `tests/core_test.cpp`'s `testIncognitoLid`) is the one
  rule: a trigger only for an open → closed transition with incognito on at that moment and the setting not
  Nothing; a stale copy or a value from nowhere is `NotInCharge`.
- `SleepWatch` fires only on `SW_LID` open → closed and re-reads the switch (`EVIOCGSW`) before calling back;
  a repeated or replayed closed event, a `SIGCONT` and a busy main loop are not a close.
- Every entry checks again on the main thread: the lid event handler (incognito on and the switch still shut;
  otherwise the ordinary lid pause / Keep playing), the power key's 2 s hold (the sleep still happens; only the
  Close all / Quit is dropped), and the idle sleep's hook. `Ui::incognitoCloseAll` is the last gate.
- Logs: the trigger's source and what it does — `incognito: lid switch: closed: close all (decided on the main
  thread)`, `incognito: lid switch: close all`, `incognito: power hold: close all, then sleeping`,
  `incognito: idle sleep: close all, then sleeping` (and `…: quitting` for Quit); a trigger ignored because
  incognito is off says `incognito: lid switch|power hold|idle sleep ignored: incognito is off` once, not per
  event; a lid event whose switch reads open says `incognito: lid switch: ignored (the lid is open)`.

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
Settings table's new rows and `testIncognitoLid`); `cmake --build build --target manga-check` prints
`manga: all passed`. The false-lid fix of 2026-11-18 built the same way; `packaging/build-bundle.sh --quick`
could not be run in that session (the harness shell refused the script).

Scripted runs, generated media only (a testsrc H.264 MP4 with a quiet sine, a quiet sine MP3):
Settings' ten groups and Power & screens scrolled to the two lid rows; incognito on from Quick settings
(its second line and the Web tile at 45%), the Settings tiles with "Off in incognito", the Web dashboard page
held off with Leave incognito, and leaving it: the dashboard came back on its own with the same PIN and
address. Shots: `redesign-shots/settings/settings-groups-*`,
`set-playback-*`, `set-subtitles-*`, `set-music-*`, `set-photos-*`, `set-reading-*`, `set-files-*`,
`set-power-*`, `set-power-lidincog-*`, `set-web-*`, `set-updates-*`, `set-about-*`, `incog-quick-*`,
`incog-settings-tiles-*`, `incog-web-*`, `incog-web-back-*` (not committed).

The idle-sleep hook was driven on a Mac with `BP_IDLE_SLEEP_S=2` and a run folder: incognito on over a paused
song then no input for 2 s — Close all stopped it, Home showed, the incognito mark went and no note appeared
(`redesign-shots/settings/close-all-fixed-*`); with Quit the app exited after the same idle ('incognito: idle sleep:
quitting'). Stop's own way back (the file's folder) ran after Home at first: `backFromPlayer` now checks Close
all's flag and stays on Home.

## Not verified

- **The handheld's own devices**: no lid switch, `gpio-keys-volume` or power key on a Mac. The arm64 Docker
  check (DEVELOPMENT.md) could not run in this session (the shell here refuses `docker`), so the Linux
  branches were not compiled and the grab, the 0.6 s and 5 s timings, a press's volume step and BananaOS's
  `volume` script were not run. The 100% stop and the browser-volume branch are code paths only.
- **The ask sheet with a browser signed in**: no browser was connected in these runs, so it was not drawn.
  The text is built from `WebServer::clients()`/`playTabs()`; a handheld and a phone are needed for it.
- **The dashboard coming back in place**: the same PIN and address came back (driven); a browser still
  signed in was not (it needs a browser), the server's restart path being the one its own Off/On switch
  already uses (`start()` reads `web.conf` and `web-sessions.tsv`).
- **Close all / Quit at the lid and the power key's 2 s hold**: no lid or power key on a Mac (the idle
  sleep's hook was driven, above); the lid and power events were not, only their code and logs.
- The quick settings' tile (and the whole quick sheet) is still the old centred box, not the redesign's tile
  sheet: only its Web / Incognito lines and the 45% treatment changed here.
