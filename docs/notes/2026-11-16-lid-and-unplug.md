# Keep playing with the lid shut; pause when the headphones go

**Asked:** music on with the lid closed (a pocket), and a pause when the headphones come out instead of sound from the
speakers.

## What bananad does (its source, read for this)

- Settings: `/etc/banana/bananad.conf`, then `/data/banana/bananad.conf` (`key = value`; a line whose first byte is `#`,
  or without `=`, is skipped; key and value trimmed; the last line wins). Read at start and again on SIGHUP only.
  `lid_sleep`: on by default; `yes` or `1` is on, anything else off. BananaOS's Settings writes the `/data` file and
  sends SIGHUP. The `/data` file need not exist (the defaults are in `/etc`).
- Lid closed with `lid_sleep` on: suspends at once. The player is the session's game (`/run/banana/game.pid`), not a DS
  game, and has no download hold, so nothing delays it. With `lid_sleep` off the lid only turns the backlights off; the
  process runs on. Lid still closed and `lid_sleep` on again after a SIGHUP: it suspends after 5 s without input ("lid
  still closed").
- A sleep the sanctioned way: create `/run/banana/sleep-now`, SIGHUP bananad (its reload unlinks the file and sleeps
  half a second later). One SIGHUP covers both a conf change and the sleep.
- Sound: the jack is `SW_HEADPHONE_INSERT` on an input device (bananad reads it with `EVIOCGSW` and events);
  `/run/banana/bt/audio` (`<MAC> <PCM>`) exists while A2DP headphones are connected, and bananad routes to them;
  a USB sound card is used only when it is the only card (no rk817, no aw88166), so on the RG DS it never plays and its
  unplug is moot there; it's watched anyway (cheap, and right on a handheld where it would play).

## Design

- `src/lidsleep.*` (bpcore, tested in `testLidSleepConf`): reading `lid_sleep` as bananad does, the effective value
  (default, `/etc`, `/data`), the rewrite (the last `lid_sleep` line in `/data` made `lid_sleep = no`, or one appended,
  or the file made), the note (`lid-restore` in the state folder: `mode` changed|added|created, `conf`, `was`, `wrote`,
  `nl`), and the undo. The undo happens only if the file's last `lid_sleep` line is still byte for byte what the player
  wrote; otherwise BananaOS's Settings changed it and that wins (the note just goes). Every other byte of the file is
  kept, including a missing final newline; a file the player made is removed if nothing else went in it.
- `src/lidsleep_host.*` (`LidGuard`, main thread): main ticks it every 250 ms with "the setting is on and the handheld
  plays" (`player.playing() && !player.paused`, not a photo; a dashboard page playing holds the handheld paused, so it
  doesn't count) and the lid's state. Playing: switch once (only if the effective value is on), note first, then the
  conf (both `writeFileDurable`), then SIGHUP (`pokeBananad`). Not playing for 3 s (a grace, so a track change or a seek
  can't put the lid sleep back and, lid shut, sleep between two songs): undo, and with the lid shut and lid sleep on
  again, `sleep-now` + SIGHUP. Quitting: undo at once. Start: a note left (power loss) is undone first thing in `main`.
- `SleepWatch`: the lid callback doesn't pause with Keep playing (`lid: closed, keeps playing`); SIGCONT still pauses
  (a sleep did happen). It now keeps the lid's state (`EVIOCGSW` at start and after a SIGCONT, then the events) for the
  guard. It opens every input device with `SW_LID` or `SW_HEADPHONE_INSERT` (found by capability), and once a second
  looks at `$BP_RUN_DIR/bt/audio` and the USB cards (`BP_ASOUND_CARDS`). A loss calls `onUnplug` on its thread; main
  gets an SDL user event (`evUnplug`) and, if the handheld plays, pauses as A does (Session `Pause`, then mpv). A change
  seen across a SIGCONT is taken as the new state, not an unplug.
- `packaging/launch.sh`: the player now runs in the background and is waited for, with a trap on TERM/INT/HUP that
  passes the signal on and waits again (bash runs a trap only between commands, so a foreground child would hide it).
  After it exits for any reason, `lid_restore` undoes a left note by the same rule (awk; keep it in step with
  `lidRestore`) and SIGHUPs bananad (found in `/proc` by `comm`). `BP_LID_RESTORE_ONLY=STATEDIR` runs only that.
  Its stdin is passed explicitly (`<&0`): a background job in a script would otherwise get /dev/null.

## Tested

- `core-test` (`testLidSleepConf`: bananad's parsing, changed/added/created and their exact undo, the user's change
  winning, `/proc/asound/cards`).
- Desktop, temp `BP_RUN_DIR`/`BP_BANANAD_CONF`/state: playing flipped `data.conf` and wrote the note within a second;
  A paused; 3 s later restored, byte-identical to the original, note gone. `kill -9` mid-play left the flipped conf
  and the note; `BP_LID_RESTORE_ONLY` (the launcher's function) restored it byte-identically; with the line changed in
  between, it was left alone ("restore skipped"). A note present at start was undone by the player before anything
  else. The launcher's undo for added (with and without the newline put in) and created, on macOS's awk and on
  busybox's awk/sed/tail (arm64 Docker).
- Unplug, desktop: removing `$BP_RUN_DIR/bt/audio` paused within a second (Session `Pause`), A played again, a USB card
  leaving the fake cards file paused again.
- arm64 Docker: `sleepwatch.cpp`, `lidsleep*.cpp` compile clean with `-Wall -Wextra`.

## Not tested on the handheld

- The lid itself, a real bananad's SIGHUP and suspend, the sleep asked for with the lid shut after the music ends, the
  jack's `SW_HEADPHONE_INSERT` events (it assumes the jack is an evdev switch, as bananad reads it), real Bluetooth
  disconnects, the launcher's background run on the handheld (KMSDRM, the Store's session, Ports' run-port), and a
  group TERM reaching the trap. Worth checking first: close the lid with music on and Keep playing (it should play on;
  `bananad.conf` shows `lid_sleep = no`), let the song list end lid-shut (it should sleep within about 4 s), and look at
  `log.txt` for the `lid:` lines.
