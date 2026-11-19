#!/bin/bash
# The README's pictures: scripted scenes of the desktop build on a demo library, both panels captured (--press's
# shot:NAME) and stacked like the handheld's, in docs/img/. Each scene has its own state folder and theme (the
# README shows dark and light ones side by side: a scene's theme is the `theme =` line of its settings).
#   docs/make-screenshots.sh DEMO_DIR
# DEMO_DIR (made by hand, generated or CC BY media only; see docs/workflow.md):
#   Movies/Big Buck Bunny (trailer).mkv  the trailer (download.blender.org/peach/trailer/trailer_480p.mov, © Blender
#                                        Foundation, CC BY 3.0) remuxed with 6 chapters (Opening 0:00, The meadow 0:04,
#                                        Big Buck 0:09, Three rodents 0:13, Payback 0:17, Credits 0:24) and an English
#                                        SRT track (written for it, lines at 0:09, 0:13, 0:16, 0:19, 0:24)
#   Movies/Meadow morning.mp4, Movies/Apples and acorns.mp4   two cuts of the trailer
#   Music/Night Drive/, Music/Paper Boats/   generated tones with cover.jpg (generated) and made-up tags
#   Photos/Big Buck Bunny/*.jpg          stills of the trailer
#   Manga/Midnight Tea.cbz               one colour page (made for Banana Player)
# The Network scene needs a Samba share "Media" (user tony, password banana) of DEMO_DIR on 127.0.0.1:1445, and an
# FTP server (same user/password) on 127.0.0.1:2121; the build needs libsmb2 (-DBP_NET=ON). Without them the scene is
# skipped and the old picture stays. Samba: docker run -d -p 1445:445 -v DEMO_DIR:/mnt/media:ro dperson/samba \
#   -u "tony;banana" -s "Media;/mnt/media;yes;yes;no;tony"
set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
DEMO="$(cd "$1" && pwd)"
BIN="$R/build/banana-player"
OUT="$R/docs/img"
WORK="$(mktemp -d)"
export BP_SHOTS="$WORK" BP_BROWSE="$DEMO"
MOVIE="$DEMO/Movies/Big Buck Bunny (trailer).mkv"
SONG="$DEMO/Music/Night Drive/01 Neon Highway.mp3"
PHOTO="$DEMO/Photos/Big Buck Bunny/Big Buck Bunny 16.jpg"

# [AT=SECONDS] scene NAME "settings lines (\n separated)" STEPS [FILE]: a fresh state folder with those settings, one
# run; AT: the trailer is resumed there (a frame worth showing: the film is mostly title cards), subtitles on
scene() {
	local name="$1" conf="$2" steps="$3"
	shift 3
	local st="$WORK/state-$name"
	rm -rf "$st"
	mkdir -p "$st"
	printf "$conf\nlid-close = pause\n" > "$st/settings.conf"
	[ -z "${AT:-}" ] || printf '%s\t1\t1\t%s\n' "$AT" "$MOVIE" > "$st/resume.tsv"
	"$BIN" --state-dir "$st" --press "$steps" "$@" > "$WORK/log-$name.txt" 2>&1 || true
	[ -f "$WORK/$name-top.png" ] || echo "no picture for $name (see $WORK/log-$name.txt)" >&2
}

# 1. the browser, Dark: Files in Movies, with a film left half way (history from a run first)
HIST="$WORK/state-browser"
mkdir -p "$HIST"
printf 'lid-close = pause\ntheme = dark\nmusic-show = both\nvisualizer = ribbon\n' > "$HIST/settings.conf"
"$BIN" --state-dir "$HIST" --press Right,Right,B,B "$DEMO/Movies/Meadow morning.mp4" > "$WORK/log.txt" 2>&1 || true
"$BIN" --state-dir "$HIST" --press Right,B,B "$SONG" > "$WORK/log.txt" 2>&1 || true
"$BIN" --state-dir "$HIST" --press prop:pause,prop:pause,Right,Right,Right,B,B "$DEMO/Movies/Apples and acorns.mp4" > "$WORK/log.txt" 2>&1 || true
"$BIN" --state-dir "$HIST" --press prop:pause,shot:browser "$DEMO/Movies" > "$WORK/log-browser.txt" 2>&1 || true

# 2. a film, Midnight blue, resumed in the Three rodents chapter with its subtitle
AT=13.2 scene playing 'theme = midnight-blue' prop:pause,shot:playing "$MOVIE"
# 3. a song, Sakura (light), album art + the Ribbon visualizer
scene music 'theme = sakura\nmusic-show = both\nvisualizer = ribbon' prop:pause,prop:pause,shot:music "$SONG"
# 4. Sound, Dark purple: the equalizer on Rock (the Equalizer button, left of the player's second button row)
scene sound 'theme = dark-purple\nmusic-show = both\nvisualizer = peaks\neq = on\neq-preset = Rock\neq-gains = 4,3,2,0,-1,-1,1,2,3,4\nsound-effect = hall' prop:pause,tap:70:363,prop:pause,shot:sound "$SONG"
# 5. a photo, Light, zoomed (UP twice)
scene photo 'theme = light' Up,Up,shot:photo "$PHOTO"
# 6. Manga, Yellow: Home's Extensions tile, Manga reader, the series (single chapter), open on both screens; the engine
# download offer is dismissed with B
scene manga 'theme = yellow\nmanga-reader = yes\nmanga-intro-shown = yes' Down,Right,A,A,B,A,prop:pause,prop:pause,shot:manga
# 7. subtitles, Coffee: the Subtitles button of the player's third row opens Audio & Subtitles on Subtitles
AT=16 scene subs 'theme = coffee' prop:pause,prop:pause,tap:73:363,prop:pause,shot:subs "$MOVIE"
# 8. Settings, 16-bit: Power & screens, the Theme row (the top screen previews it)
AT=9 scene settings 'theme = 16-bit' Start,Down,Down,Down,A,shot:settings "$MOVIE"
# 9. Info, 32-bit: the More sheet's Info, scrolled to the video and audio parts
AT=7.5 scene info 'theme = 32-bit' prop:pause,tap:601:27,tap:397:324,Down,Down,Down,Down,Down,Down,shot:info "$MOVIE"
# 10. a visualizer on the whole top screen, Midnight blue
scene viz 'theme = midnight-blue\nmusic-show = visualizer\nvisualizer = sleepy-moon' prop:pause,prop:pause,shot:viz "$SONG"

# 11. Network, Mint: a folder of the local test servers (the first is Samba, the second FTP)
NET="$WORK/state-network"
if (echo > /dev/tcp/127.0.0.1/1445) 2>/dev/null && (echo > /dev/tcp/127.0.0.1/2121) 2>/dev/null; then
	mkdir -p "$NET"
	printf 'lid-close = pause\ntheme = mint\n' > "$NET/settings.conf"
	"$R/build/bp-servers" "$NET" add smb "Living room PC" 127.0.0.1 1445 tony banana > /dev/null
	"$R/build/bp-servers" "$NET" add ftp "Home server" 127.0.0.1 2121 tony banana > /dev/null
	"$BIN" --state-dir "$NET" --press Down,A,A,A,Down,A,prop:pause,shot:network > "$WORK/log-network.txt" 2>&1 || true
else
	echo "Network: no test servers on 127.0.0.1:1445/2121, picture kept" >&2
fi

# each pair stacked: top over bottom with a hinge between, on a dark frame
for s in browser playing music sound photo manga subs settings info viz network; do
	[ -f "$WORK/$s-top.png" ] || continue
	ffmpeg -loglevel error -y -i "$WORK/$s-top.png" -i "$WORK/$s-bottom.png" -filter_complex \
		"[0]scale=640:480:flags=lanczos[t];[1]scale=640:480:flags=lanczos[b];color=c=0x2A2931:s=640x20[h];[t][h][b]vstack=3,pad=iw+40:ih+40:20:20:color=0x2A2931" \
		-frames:v 1 "$OUT/screen-$s.png"
	echo "docs/img/screen-$s.png"
done
rm -rf "$WORK"
