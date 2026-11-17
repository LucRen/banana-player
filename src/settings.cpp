#include "settings.h"
#include "viz_more.h"
#include "vizcache.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	// the document viewers' choices, by name (textreader.h's order)
	static const char* TXT_FONT_IDS[] = {"serif", "sans", "mono", "pixel", "cjk-pixel"};
	static const char* TXT_SIZE_IDS[] = {"small", "medium", "large", "larger", "largest"};
	static const char* TXT_SPACING_IDS[] = {"tight", "normal", "loose"};
	static const char* TXT_MARGIN_IDS[] = {"narrow", "normal", "wide"};
	static const char* TXT_THEME_IDS[] = {"dark", "light", "sepia", "black"};
	static const char* DOC_FILTER_IDS[] = {"off", "warm", "warmer", "dim"};
	static const char* TXT_VIEW_IDS[] = {"scroll", "book-left", "book-right"};
	static const char* TXT_TURN_IDS[] = {"fold", "off"};

	static const char* SHOW_IDS[SHOWS] = {"art", "both", "visualizer", "none"};
	static const char* SHOW_LABELS[SHOWS] = {"Album art", "Art + visualizer", "Visualizer", "None"};
	static const char* VIS_IDS[VISUALIZERS] = {"bars", "waves", "spectrum", "circle", "peaks", "peaks-yellow", "pocket", "led",
		"synthwave", "banana-stack", "night-city", "oscilloscope", "heartbeat", "campfire", "aurora", "sound-garden", "turntable", "pixel-sea",
		"bunny-hop", "bubble-tea", "jellyfish", "music-box", "fishbowl", "disco-ball", "banana-dancer", "vibing-cat",
		"popcorn-machine", "jelly-wobble", "tube-man", "long-cat", "washing-machine", "lite"};
	static const char* VIS_LABELS[VISUALIZERS] = {"Bars", "Waves", "Spectrum", "Circle", "Peaks", "Peaks Banana", "Pocket console",
		"Hi-fi LED", "Synthwave", "Banana stack", "Night city", "Oscilloscope", "Heartbeat", "Campfire", "Aurora",
		"Sound garden", "Turntable", "Pixel sea", "Bunny hop",
		"Bubble tea", "Jellyfish", "Music box", "Fishbowl", "Disco ball", "Banana dancer", "Vibing cat",
		"Popcorn machine", "Jelly wobble", "Tube man", "Long cat", "Washing machine", "Lite"};
	static const char* LOOP_IDS[LOOPS] = {"off", "all", "one"};
	static const char* MANGA_MODE_IDS[MANGA_MODES] = {"rtl", "ltr", "strip"};
	static const char* MANGA_FIT_IDS[MANGA_FITS] = {"page", "width", "fill"};
	static const char* MANGA_CACHE_IDS[MANGA_CACHES] = {"100m", "300m", "1g"};
	static const long long MANGA_CACHE_MB[MANGA_CACHES] = {100, 300, 1024};
	static const char* CLOCK_IDS[CLOCKS] = {"24h", "12h", "off"};
	static const char* SCREEN_OFF_IDS[SCREEN_OFFS] = {"never", "15s", "30s", "1m", "2m", "5m"};
	static const char* SCREEN_OFF_LABELS[SCREEN_OFFS] = {"Never", "15 seconds", "30 seconds", "1 minute", "2 minutes", "5 minutes"};

	int stepMangaPreload(int v, int dir)
	{
		int i = 0;
		for (int k = 0; k < MANGA_PRELOAD_COUNT; k++)
			if (MANGA_PRELOADS[k] == v) i = k;
		i = (i + (dir < 0 ? -1 : 1) + MANGA_PRELOAD_COUNT) % MANGA_PRELOAD_COUNT;
		return MANGA_PRELOADS[i];
	}

	uint64_t mangaCacheBytes(int v)
	{
		int k = v >= 0 && v < MANGA_CACHES ? v : MANGA_CACHE_DEFAULT;
		return (uint64_t)MANGA_CACHE_MB[k] * 1024 * 1024;
	}

	int screenOffSeconds(int v)
	{
		static const int SECS[SCREEN_OFFS] = {0, 15, 30, 60, 120, 300};
		return v >= 0 && v < SCREEN_OFFS ? SECS[v] : 0;
	}

	const char* screenOffLabel(int v)
	{
		return v >= 0 && v < SCREEN_OFFS ? SCREEN_OFF_LABELS[v] : SCREEN_OFF_LABELS[0];
	}
	static const char* LOOP_LABELS[LOOPS] = {"No loop", "Loop all", "Loop one"};

	const char* musicShowLabel(int v) { return SHOW_LABELS[v >= 0 && v < SHOWS ? v : 0]; }
	const char* visualizerLabel(int v) { return VIS_LABELS[v >= 0 && v < VISUALIZERS ? v : 0]; }

	int visualizerSlot(int v)
	{
		for (int i = 0; i < VIS_SHOWN_COUNT; i++)
			if (VIS_SHOWN[i] == v) return i;
		return 0;
	}

	int stepVisualizer(int v, int dir)
	{
		int i = visualizerSlot(v) + (dir < 0 ? -1 : 1);
		return VIS_SHOWN[(i % VIS_SHOWN_COUNT + VIS_SHOWN_COUNT) % VIS_SHOWN_COUNT];
	}
	const char* loopLabel(int v) { return LOOP_LABELS[v >= 0 && v < LOOPS ? v : 0]; }

	// Circle: the spectrum's bars (BARS_W x BARS_H, low notes left, growing up) and beside them PALETTE columns, the
	// ring's glow fading out to black; each pixel of the square picture is one of theirs (remap), so a frame costs a
	// lookup a pixel. The bars run from the top down both sides, mirrored, SEGMENTS a side
	static const int BARS_W = 256, BARS_H = 128, PALETTE = 16, SEGMENTS = 40;
	static const double BAR_FROM = 84, BAR_TO = 200, RING = 76, CENTRE_Y = 274;
	// the share of the measured spectrum the ring shows: showfreqs runs to half the sample rate, and its top ninth
	// (16-24 kHz at 44.1/48 kHz, as Peaks cuts) has next to nothing in music, so the bars at the bottom never moved
	static const double SHOWN = 8.0 / 9;
	static const int SHOWN_W = (int)(BARS_W * SHOWN);
	// a new name whenever the tables change: they're cached by size
	static const char* CIRCLE_MAPS[2] = {"circle3-x.pgm", "circle3-y.pgm"};

	bool writeCircleMaps(const std::string& dir)
	{
		const int w = CIRCLE_BOX, h = CIRCLE_BOX;
		size_t bytes = (size_t)w * h * 2;
		auto ready = [&](const std::string& path) {
			return pgmFileOk(path, pgmFileSize(w, h, 65535));
		};
		if (ready(dir + "/" + CIRCLE_MAPS[0]) && ready(dir + "/" + CIRCLE_MAPS[1])) return true;
		std::vector<unsigned char> xs(bytes), ys(bytes);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				double dx = x + CIRCLE_X - CIRCLE_W / 2.0 + 0.5, dy = y + CIRCLE_Y - CENTRE_Y + 0.5, r = std::hypot(dx, dy);
				double seg = std::fabs(std::atan2(dx, -dy)) / M_PI * SEGMENTS;	// 0 at the top, SEGMENTS at the bottom
				int mx, my;
				if (r >= BAR_FROM && r <= BAR_TO && seg - std::floor(seg) < 0.6)
				{
					// a bar: its frequency by the angle, its height by the distance out
					mx = (int)((std::floor(seg) + 0.5) / SEGMENTS * SHOWN_W);
					my = (int)std::lround((BARS_H - 1) * (1 - (r - BAR_FROM) / (BAR_TO - BAR_FROM)));
				}
				else
				{
					// the ring and its glow, else black (the palette's last column)
					mx = BARS_W + std::min(PALETTE - 1, (int)std::lround(std::fabs(r - RING) * 2));
					my = 0;
				}
				size_t i = ((size_t)y * w + x) * 2;
				xs[i] = (unsigned char)(mx >> 8);
				xs[i + 1] = (unsigned char)mx;
				ys[i] = (unsigned char)(my >> 8);
				ys[i + 1] = (unsigned char)my;
			}
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 2; k++)
		{
			if (!writePgmFile(dir + "/" + CIRCLE_MAPS[k], w, h, 65535, 1, k ? ys : xs)) return false;
		}
		return true;
	}

	bool lavfiSafePath(const std::string& dir)
	{
		return !dir.empty() && dir.find_first_of(":='\\") == std::string::npos;
	}

	// a path as a filter's argument: quoted (spaces, commas, brackets, ';'); one with ':', '=', a quote or a
	// backslash can't be named at all
	bool lavfiPath(const std::string& p, std::string& out)
	{
		if (!lavfiSafePath(p)) return false;
		out = "'" + p + "'";
		return true;
	}

	static std::string circleGraph(bool overCover, const std::string& dir)
	{
		std::string xm, ym;
		lavfiPath(dir + "/" + CIRCLE_MAPS[0], xm);
		lavfiPath(dir + "/" + CIRCLE_MAPS[1], ym);
		std::string bars = std::to_string(BARS_W) + "x" + std::to_string(BARS_H), sw = std::to_string(SHOWN_W);
		// cyan (left: the low notes, at the top) to blue-purple to pink (the high, at the bottom), multiplied into
		// the white bars; the ring a pale blue, fading over the palette's columns
		// The still inputs (the gradient, the palette, the tables) are pictures that end, never loops: blend, hstack
		// and remap go on with an ended input's last frame, and end when the bars (the sound) do. A loop never ends,
		// so neither did the graph: past the song's end its sources poured out frames as fast as they could, mpv's
		// clock stopped (the song never ended) and its memory grew by the gigabyte. One frame each (r=1): nullsrc's
		// 25 a second over d=1 made the first second 55 frames, each one remapped
		std::string g =
			"[aid1] asplit [ao] [a]; "
			"[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + bars + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white|white,format=gbrp [bars]; "
			"nullsrc=s=" + bars + ":r=1:d=1,format=gbrp,"
			"geq=r='59+196*clip(2*X/" + sw + "-1,0,1)':g='232-140*clip(2*X/" + sw + ",0,1)-13*clip(2*X/" + sw + "-1,0,1)':b='224+31*clip(2*X/" + sw + ",0,1)-39*clip(2*X/" + sw + "-1,0,1)' [grad]; "
			"[bars] [grad] blend=all_mode=multiply [cbars]; "
			"nullsrc=s=" + std::to_string(PALETTE) + "x" + std::to_string(BARS_H) + ":r=1:d=1,format=gbrp,"
			"geq=r='207*exp(-pow(X/4,2))':g='232*exp(-pow(X/4,2))':b='255*exp(-pow(X/4,2))' [pal]; "
			"[cbars] [pal] hstack [src]; "
			"movie=" + xm + " [xm]; movie=" + ym + " [ym]; ";
		std::string pad = ",pad=" + std::to_string(CIRCLE_W) + ":" + std::to_string(CIRCLE_H) + ":" + std::to_string(CIRCLE_X) + ":" + std::to_string(CIRCLE_Y) + ":black";
		if (!overCover) return g + "[src] [xm] [ym] remap" + pad + " [vo]";
		// the art inside the ring
		return g + "[src] [xm] [ym] remap" + pad + " [ring]; "
			"[vid1] scale=96:96:force_original_aspect_ratio=decrease,format=gbrp [cover]; "
			"[ring] [cover] overlay=x=(W-w)/2:y=" + std::to_string((int)CENTRE_Y) + "-h/2:eof_action=repeat:format=gbrp [vo]";
	}

	// Peaks: the spectrum in 64 bars (coral), each with a cap (green) that sits on the bar's highest point and falls
	// back slowly. Bars draws each frame at full size in showfreqs, which paints pixel by pixel; here showfreqs only
	// measures (64 columns, averaged down to one row, one value a bar), and the picture is made from those numbers:
	// - the caps: a value held and let fall by lagfun, which keeps max(now, last x decay). Its decay is a product, so
	//   the values go through exp() before it and log() after (16 bits for the range): a product in exp() is a
	//   constant step in the value, a cap falling at an even speed
	// - the drawing at half size (320x240; 320x100 over the art): each row of numbers stretched to a picture (scale
	//   on one row, then pad and fillborders: row copies), and two lut2 against a still "height of this row" picture
	//   (one frame, repeated: lut2's secondary input) give each pixel a cap, a bar or nothing (gaps: a 1-column one
	//   every 5). Half size is the chroma's size at full size, so the colour planes come from it as they are
	// - full size: only the luma, scaled x2 (nearest: a bar 8 pixels, a gap 2, a cap 4)
	// No RGB anywhere, nothing converted: YUV straight to mpv. Over the art no alpha either: the art's slice (made
	// once, one frame) is split into its planes and each plane is a lut2's second input, kept where nothing's drawn
	struct PeaksColours { int barY, barU, barV, capY, capU, capV; };
	static std::string peaksGraph(bool overCover, const PeaksColours& c)
	{
		const int N = 64, BW = 5, W = N * BW;	// at half size: a bar 4 columns and a 1-column gap
		const int H = overCover ? 100 : 240;
		const std::string w = std::to_string(W), h = std::to_string(H);
		// the cap 4 pixels at full size (2 rows here), in the height's 251 steps; the fall, ~2.4 s for the whole
		// height: 3.4 steps a frame, a decay of exp(-3.4/24) in the exp(value/24) domain
		const std::string cap = std::to_string((2 * 251 + H / 2) / H);
		const int mid = H / 2 - 1;
		const std::string stretch = "scale=" + w + ":1:flags=neighbor,pad=" + w + ":" + h + ":0:" + std::to_string(mid) +
									",fillborders=top=" + std::to_string(mid) + ":bottom=" + std::to_string(H - 1 - mid) +
									":mode=smear,setsar=1";
		// the colours in BT.601 limited YUV (mpv's reading of a picture this size); black is 16/128/128. Coral
		// #F0806E and green #9EDB98: bar 153/103/178, cap 182/108/106. A bar's and a cap's Y must differ (the planes
		// are told apart by it)
		const std::string bY = std::to_string(c.barY), bU = std::to_string(c.barU), bV = std::to_string(c.barV),
						  cY = std::to_string(c.capY), cU = std::to_string(c.capU), cV = std::to_string(c.capV);
		std::string g = "[aid1] asplit [ao] [a]; ";
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [art1] [art2]; [art1] crop=640:280:0:0 [top]; [art2] crop=640:200:0:280,extractplanes=y+u+v "
				 "[pkay] [pkau] [pkav]; ";
		// TOP more bars measured than drawn, the highest cut off: at 44.1/48 kHz they're 16-24 kHz, where music has next
		// to nothing, so the rightmost bars sat still or twitched at the noise floor
		const int TOP = 8;
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + std::to_string(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white,crop=" + std::to_string(N) +
			 ":256:0:0,scale=" + std::to_string(N) +
			 ":1:flags=area,format=gray,bplut=c0='val*252/255',split [pkv] [pkp]; "
			 "[pkp] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.8679,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*250)',format=gray," + stretch + " [pkP]; "
			 "[pkv] " + stretch + " [pkV]; "
			 "color=black:s=" + w + "x" + h + ":r=1:d=1,format=gray,geq=lum='if(gte(mod(X," + std::to_string(BW) + ")," +
			 std::to_string(BW - 1) + "),253,1+floor(251*(" + h + "-1-Y)/(" + h + "-1)))' [pkR]; "
			 // 254: a cap (over the cap's value, within cap steps of it), else the row's height (253: a gap)
			 "[pkP] [pkR] bplut2=c0='if(gt(y,x)*lte(y,x+" + cap + "),254,y)' [pkQ]; ";
		if (overCover)
		{
			// what each pixel is (0 nothing, 1 bar, 2 cap), then each plane: the art's where nothing, else a colour
			auto paint = [](const char* bar, const char* capc) {
				return std::string("'if(eq(x,0),y,if(eq(x,1),") + bar + "," + capc + "))'";
			};
			return g + "[pkV] [pkQ] bplut2=c0='if(eq(y,254),2,if(lte(y,x)*lt(y,253),1,0))',split=3 [pkc0] [pkc1] [pkc2]; "
					   "[pkc0] scale=640:200:flags=neighbor [pkcy]; "
					   "[pkcy] [pkay] bplut2=c0=" + paint(bY.c_str(), cY.c_str()) + " [pkY]; "
					   "[pkc1] [pkau] bplut2=c0=" + paint(bU.c_str(), cU.c_str()) + " [pkU]; "
					   "[pkc2] [pkav] bplut2=c0=" + paint(bV.c_str(), cV.c_str()) + " [pkW]; "
					   "[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// the luma at half size, then U and V from it (a lut each) and the luma scaled up
		g += "[pkV] [pkQ] bplut2=c0='if(eq(y,254)," + cY + ",if(lte(y,x)*lt(y,253)," + bY + ",16))',split=3 [pky] [pku] [pkw]; "
			 "[pky] scale=640:480:flags=neighbor [pkY]; "
			 "[pku] bplut=c0='if(eq(val," + cY + ")," + cU + ",if(eq(val," + bY + ")," + bU + ",128))' [pkU]; "
			 "[pkw] bplut=c0='if(eq(val," + cY + ")," + cV + ",if(eq(val," + bY + ")," + bV + ",128))' [pkW]; "
			 "[pkY] [pkU] [pkW] mergeplanes=0x001020:yuv420p";
		// a faint line every 96 pixels (white at 10%), over the bars too: four 1-pixel boxes
		for (int y = 96; y < 480; y += 96) g += ",drawbox=x=0:y=" + std::to_string(y) + ":w=640:h=1:c=white@0.1:t=fill";
		return g + " [vo]";
	}

	// Pocket console: Peaks as an old handheld's screen. The spectrum in 15 bars and their falling caps, as Peaks measures
	// them, drawn at the LCD's own size (80 dots across, a dot each pixel: 15 bars of 4 dots and a gap 1), every pixel one of
	// three greens by a lookup (the rows' "height" picture is still: one frame, repeated), then scaled 8x (nearest: a dot
	// 8 pixels square) and a still grid of 1-pixel lines laid over the luma, so the dots show. The bars are dark, their
	// top third mid green; the caps and the floor mid green; the screen the lightest. YUV only, nothing converted.
	// Over the art: the art's top as it is, and under it the console's 25 dots high with no screen of their own: the art
	// shows through everywhere but the lit dots
	static std::string pocketGraph(bool overCover)
	{
		// the greens in BT.601 limited YUV: dark #0F380F, mid #306230, screen #9BBC0F; the grid #93B20E (luma only)
		const int DY = 50, DU = 116, DV = 113, MY = 82, MU = 113, MV = 110, SY = 152, SU = 57, SV = 126, GY = 145;
		// over the art the dots are the bright ones, so they pop on any cover: bars light #8BAC0F, their top, the caps
		// and the floor a vivid lime #D4F75A
		const int LY = 140, LU = 64, LV = 125, BY = 204, BU = 64, BV = 124;
		// 15 bars of 4 dots and a 1-dot gap, inset 2 dots from each side as the floor is (80 dots across)
		const int N = 15, BW = 5, W = 80, L = 2;
		const int ROWS = overCover ? 25 : 60;	// dots high: 200 or 480 pixels
		// the bars' rows (their top, their height) and the floor under them: on the screen's last row, no gap below
		const int T = overCover ? 2 : 14, BH = overCover ? 22 : 45;
		const int TOP = 8;	// as Peaks: the highest bands (16-24 kHz) measured and cut off
		const std::string w = std::to_string(W), rows = std::to_string(ROWS), bh = std::to_string(BH), t = std::to_string(T);
		const std::string cap = std::to_string((251 + BH / 2) / BH);	// one dot, in the height's 251 steps
		auto n = [](int v) { return std::to_string(v); };
		// (fillborders refuses an odd height: the strip's 25 rows are smeared as 26 and the extra one cut off)
		const int EV = ROWS + (ROWS & 1), mid = EV / 2 - 1;
		const std::string stretch = "scale=" + n(N * BW) + ":1:flags=neighbor,pad=" + w + ":" + n(EV) + ":" + n(L) + ":" + n(mid) +
									",fillborders=top=" + n(mid) + ":bottom=" + n(EV - 1 - mid) + ":mode=smear" +
									(EV != ROWS ? ",crop=" + w + ":" + rows + ":0:0" : std::string()) + ",setsar=1";
		std::string g = "[aid1] asplit [ao] [a]; ";
		// over the art: its top as it is, and its bottom slice in planes, each kept wherever no dot is lit (as Peaks)
		if (overCover)
			g += "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				 "settb=AVTB,split [pcart1] [pcart2]; [pcart1] crop=640:280:0:0 [top]; "
				 "[pcart2] crop=640:200:0:280,extractplanes=y+u+v [pcay] [pcau] [pcav]; ";
		// measured and drawn 60 times a second (an exception to the 30 fps rule, for the owner to judge on the
		// handheld): the 1024-sample windows overlap, so a short hit always counts (at 30 a third of the sound went
		// unmeasured), and the bars move with it
		g += "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
			 "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
			 ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,bplut=c0='val*252/255'" + vizContrast("pc", 1, "4", "1.5", "160") + ",split [pcv] [pcp]; "
			 "[pcp] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.9316,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*250)',format=gray," + stretch + " [pcP]; "
			 // the bars themselves eased, as an LED meter's: up at once, down over ~0.15 s for the whole height (28 of
			 // the 252 steps a measure), so whole-dot steps glide rather than flicker at 60 frames a second. The same
			 // exp/log lagfun as the caps (15 values a frame: next to nothing); a cap's slower fall keeps it above
			 "[pcv] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
			 "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray," + stretch + " [pcV]; "
			 // each dot's "height" (1..252 up the bars' rows), 253 a gap or outside them, 255 the floor
			 "color=black:s=" + w + "x" + rows + ":r=1:d=1,format=gray,geq=lum='if(eq(Y," + n(T + BH) + ")*between(X," + n(L) + "," +
			 n(L + N * BW - 2) + "),255,if(lt(Y," + t + ")+gte(Y," + n(T + BH) + ")+lt(X," + n(L) + ")+gte(X," + n(L + N * BW - 1) +
			 ")+gte(mod(X-" + n(L) + "," + n(BW) + ")," + n(BW - 1) + "),253,1+floor(251*(" + n(T + BH - 1) + "-Y)/(" + bh + "-1))))' [pcR]; "
			 // 254: a cap (one dot above the cap's value)
			 "[pcP] [pcR] bplut2=c0='if(lt(y,253)*gt(y,x)*lte(y,x+" + cap + "),254,y)' [pcQ]; "
			 "";
		if (overCover)
		{
			// the art shows through: each dot nothing (0), dark (1) or mid (2), and between the dots (the grid's lines)
			// nothing either, so it reads as an LCD laid over the picture. Each plane the art's where nothing, else
			// the green's; the chroma's grid at its half size
			auto paint = [&](int dark, int midc) {
				return "'if(eq(x,0),y,if(eq(x,1)," + n(dark) + "," + n(midc) + "))'";
			};
			return g + "[pcV] [pcQ] bplut2=c0='if(gte(y,254),2,if(lte(y,x)*lt(y,253),if(gt(y,x*0.66),2,1),0))',split=3 [pcc0] [pcc1] [pcc2]; "
					   "color=black:s=640x200:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,8),7)+eq(mod(Y,8),7),1)' [pcG]; "
					   "color=black:s=320x100:r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,4),3)+eq(mod(Y,4),3),1)',split [pcGu] [pcGv]; "
					   "[pcc0] scale=640:200:flags=neighbor [pcc0s]; [pcc0s] [pcG] bplut2=c0='if(y,0,x)' [pcc0g]; "
					   "[pcc0g] [pcay] bplut2=c0=" + paint(LY, BY) + " [pcY]; "
					   "[pcc1] scale=320:100:flags=neighbor [pcc1s]; [pcc1s] [pcGu] bplut2=c0='if(y,0,x)' [pcc1g]; "
					   "[pcc1g] [pcau] bplut2=c0=" + paint(LU, BU) + " [pcU]; "
					   "[pcc2] scale=320:100:flags=neighbor [pcc2s]; [pcc2s] [pcGv] bplut2=c0='if(y,0,x)' [pcc2g]; "
					   "[pcc2g] [pcav] bplut2=c0=" + paint(LV, BV) + " [pcW]; "
					   "[pcY] [pcU] [pcW] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
		}
		// each dot's green: a cap or the floor mid, a bar dark (its top third mid), else the screen
		g += "[pcV] [pcQ] bplut2=c0='if(gte(y,254)," + n(MY) + ",if(lte(y,x)*lt(y,253),if(gt(y,x*0.66)," + n(MY) + "," + n(DY) +
			 ")," + n(SY) + "))',split=3 [pcy] [pcu] [pcw]; "
			 // the grid: the 8th line of every dot, across and down, a hair darker (a still picture, one frame)
			 "color=black:s=640x" + n(ROWS * 8) + ":r=1:d=1,format=gray,geq=lum='255*gte(eq(mod(X,8),7)+eq(mod(Y,8),7),1)' [pcG]; "
			 "[pcy] scale=640:" + n(ROWS * 8) + ":flags=neighbor [pcY0]; [pcY0] [pcG] bplut2=c0='if(y," + n(GY) + ",x)' [pcY]; "
			 "[pcu] scale=320:" + n(ROWS * 4) + ":flags=neighbor,bplut=c0='if(eq(val," + n(DY) + ")," + n(DU) + ",if(eq(val," + n(MY) +
			 ")," + n(MU) + "," + n(SU) + "))' [pcU]; "
			 "[pcw] scale=320:" + n(ROWS * 4) + ":flags=neighbor,bplut=c0='if(eq(val," + n(DY) + ")," + n(DV) + ",if(eq(val," + n(MY) +
			 ")," + n(MV) + "," + n(SV) + "))' [pcW]; "
			 "[pcY] [pcU] [pcW] mergeplanes=0x001020:yuv420p [vo]";
		return g;
	}

	bool visualizerNeedsMaps(int vis)
	{
		return vis == VIS_CIRCLE || (vis >= VIS_LED && vis < VIS_LITE);	// (Lite draws nothing from a file)
	}

	bool writeVisualizerMaps(const std::string& dir)
	{
		bool ok = writeCircleMaps(dir);
		ok = writeLedMaps(dir) && ok;
		ok = writeSynthwaveMaps(dir) && ok;
		ok = writeBananaMaps(dir) && ok;
		ok = writeCityMaps(dir) && ok;
		ok = writeScopeMaps(dir) && ok;
		ok = writeHeartMaps(dir) && ok;
		ok = writeFireMaps(dir) && ok;
		ok = writeAuroraMaps(dir) && ok;
		ok = writeGardenMaps(dir) && ok;
		ok = writeVinylMaps(dir) && ok;
		ok = writeSeaMaps(dir) && ok;
		ok = writeBunnyMaps(dir) && ok;
		ok = writeBobaMaps(dir) && ok;
		ok = writeJellyMaps(dir) && ok;
		ok = writeMusicboxMaps(dir) && ok;
		ok = writeFishbowlMaps(dir) && ok;
		ok = writeDiscoMaps(dir) && ok;
		ok = writeDancerMaps(dir) && ok;
		ok = writeCatMaps(dir) && ok;
		ok = writePopcornMaps(dir) && ok;
		ok = writeWobbleMaps(dir) && ok;
		ok = writeTubemanMaps(dir) && ok;
		ok = writeLongcatMaps(dir) && ok;
		ok = writeWasherMaps(dir) && ok;
		return ok;
	}

	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir, int fps)
	{
		return vizPlainLuts(vizAtFps(visualizerGraphMarked(vis, overCover, mapsDir), fps));
	}

	std::string visualizerGraphMarked(int vis, bool overCover, const std::string& mapsDir)
	{
		// Lite isn't a graph: the player measures the sound from an af tap and draws it (src/topscreen.cpp)
		if (vis == VIS_LITE) return "";
		if (vis == VIS_POCKET) return pocketGraph(overCover);
		// the newer ones (a file each, src/viz_*.cpp); with no stills yet, as Peaks
		if (vis >= VIS_LED && vis < VISUALIZERS)
		{
			std::string q;
			if (mapsDir.empty() || !lavfiPath(mapsDir, q)) vis = VIS_PEAKS_BANANA;
			else switch (vis)
			{
			case VIS_LED: return ledGraph(overCover, mapsDir);
			case VIS_SYNTHWAVE: return synthwaveGraph(overCover, mapsDir);
			case VIS_BANANA_STACK: return bananaGraph(overCover, mapsDir);
			case VIS_CITY: return cityGraph(overCover, mapsDir);
			case VIS_SCOPE: return scopeGraph(overCover, mapsDir);
			case VIS_HEART: return heartGraph(overCover, mapsDir);
			case VIS_FIRE: return fireGraph(overCover, mapsDir);
			case VIS_AURORA: return auroraGraph(overCover, mapsDir);
			case VIS_GARDEN: return gardenGraph(overCover, mapsDir);
			case VIS_VINYL: return vinylGraph(overCover, mapsDir);
			case VIS_SEA: return seaGraph(overCover, mapsDir);
			case VIS_BUNNY: return bunnyGraph(overCover, mapsDir);
			case VIS_BOBA: return bobaGraph(overCover, mapsDir);
			case VIS_JELLY: return jellyGraph(overCover, mapsDir);
			case VIS_MUSICBOX: return musicboxGraph(overCover, mapsDir);
			case VIS_FISHBOWL: return fishbowlGraph(overCover, mapsDir);
			case VIS_DISCO: return discoGraph(overCover, mapsDir);
			case VIS_DANCER: return dancerGraph(overCover, mapsDir);
			case VIS_CAT: return catGraph(overCover, mapsDir);
			case VIS_POPCORN: return popcornGraph(overCover, mapsDir);
			case VIS_WOBBLE: return wobbleGraph(overCover, mapsDir);
			case VIS_TUBEMAN: return tubemanGraph(overCover, mapsDir);
			case VIS_LONGCAT: return longcatGraph(overCover, mapsDir);
			default: return washerGraph(overCover, mapsDir);
			}
		}
		std::string quoted;
		if (vis == VIS_CIRCLE && !mapsDir.empty() && lavfiPath(mapsDir, quoted)) return circleGraph(overCover, mapsDir);
		if (vis == VIS_CIRCLE) vis = VIS_BARS;
		// drawn at the panel's shape (mpv scales it to the panel) and 30 frames a second: light enough for the A55s
		// beside the decoding
		int h = overCover ? 200 : 480;
		std::string size = "s=640x" + std::to_string(h);
		std::string draw;
		// a soft amber, not the menu's bright yellow: it fills much of a dark screen. One colour a channel: the filters
		// give a channel with none its own (white for the second), and stereo is most music
		const std::string ink = "0xC08A22|0xC08A22";
		// The sound as the visualizers see it (the speakers' branch, [ao], keeps the file's own): 44.1 or 48 kHz, so a
		// 96/192 kHz file costs no more than a CD (and its spectrum isn't mostly empty ultrasound), and Bars in mono
		// (its two channels are drawn in one colour anyway, and every bar is painted pixel by pixel: half the cost)
		const std::string in = "aformat=sample_rates=44100|48000";
		switch (vis)
		{
		case VIS_WAVES: draw = in + ",showwaves=" + size + ":mode=cline:split_channels=0:rate=30:colors=" + ink; break;
		// its own fps: a frame made only every 30th of a second (an fps filter after it took one per column, each a
		// copy of the whole picture, and dropped most)
		case VIS_SPECTRUM: draw = in + ",showspectrum=" + size + ":slide=scroll:mode=combined:color=fire:scale=log:fps=30"; break;
		default: draw = in + ":channel_layouts=mono,showfreqs=" + size + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=" + ink; break;
		}
		// Peaks: coral and green; Peaks Banana: Midnight's yellow #FFD23F bars, cream #E8E2D3 caps
		if (vis == VIS_PEAKS) return peaksGraph(overCover, {153, 103, 178, 182, 108, 106});
		if (vis == VIS_PEAKS_BANANA) return peaksGraph(overCover, {193, 57, 158, 210, 121, 132});
		// near the format the filter draws in, which mpv shows as it is: converting each frame to yuv420p cost 6.5-7 ms
		// a frame on the A55s, more than drawing Waves or Spectrum. RGBA's alpha dropped (rgb0, a byte shuffle: 0.5 ms):
		// mpv would lay a clear background over a checkerboard; Spectrum's YUV as it comes
		if (!overCover) return "[aid1] asplit [ao] [v]; [v] " + draw + (vis == VIS_SPECTRUM ? "" : ",format=rgb0") + " [vo]";
		// the art fitted once, as one frame, and cut in two; only the 640x200 strip under it is drawn each frame (the
		// visualizer over its slice of the art), in the visualizer's own format so no frame is converted.
		// Redrawing the whole picture cost 3.5-4.4 ms a frame on a Mac and over the 33 ms budget on the A55s, on the
		// thread that feeds the sound. The art's slice is overlay's main input: its one frame is held, and a frame is
		// made for each of the visualizer's, till the sound ends (its timebase fine, or the frames' times would be
		// rounded to the picture's 25ths). Bars' and Waves' background is already clear, and their colours already
		// scaled by their alpha (Waves' edges fade), hence premultiplied; Spectrum's is black, so it's drawn with alpha
		// (its loudness, 4x: only the near-black clear, as colorkey did, without its 5 ms a frame).
		// vstack repeats the top's ended frame, as Circle's hstack does
		bool yuv = vis == VIS_SPECTRUM;
		std::string strip = yuv ? "[a] " + draw + ":opacity=4,format=yuva444p" : "[a] " + draw;
		return "[aid1] asplit [ao] [a]; "
			   "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=rgba" +
			   std::string(yuv ? ",scale=out_range=full,format=yuv444p" : "") + ",settb=AVTB,split [art1] [art2]; "
			   "[art1] crop=640:280:0:0 [top]; "
			   "[art2] crop=640:200:0:280 [under]; " +
			   strip + " [s]; "
			   "[under] [s] overlay=format=" + (yuv ? "yuv444" : "rgb:alpha=premultiplied") + " [strip]; "
			   "[top] [strip] vstack [vo]";
	}

	std::string blankGraph()
	{
		return "[aid1] asplit [ao] [a]; [a] showwaves=s=32x24:rate=5,drawbox=c=black:t=fill,format=yuv420p [vo]";
	}


	SettingDef SettingDef::flag(const char* key, bool Settings::*f, const char* no, const char* yes)
	{
		// the words live as long as the table: a pair is kept per distinct (no, yes)
		static std::vector<std::vector<const char*>*> pairs;
		std::vector<const char*>* found = nullptr;
		for (auto* p : pairs)
			if (std::strcmp((*p)[0], no) == 0 && std::strcmp((*p)[1], yes) == 0) found = p;
		if (!found) pairs.push_back(found = new std::vector<const char*>{no, yes});
		SettingDef d;
		d.kind = FLAG; d.key = key; d.flagField = f; d.ids = found->data(); d.count = 2;
		return d;
	}

	SettingDef SettingDef::choice(const char* key, int Settings::*f, const char* const* ids, int count)
	{
		SettingDef d;
		d.kind = CHOICE; d.key = key; d.intField = f; d.ids = ids; d.count = count;
		return d;
	}

	SettingDef SettingDef::ints(const char* key, int Settings::*f, const int* values, int count)
	{
		SettingDef d;
		d.kind = INT_LIST; d.key = key; d.intField = f; d.values = values; d.count = count;
		return d;
	}

	SettingDef SettingDef::range(const char* key, int Settings::*f, int min, int max, int snap)
	{
		SettingDef d;
		d.kind = INT_RANGE; d.key = key; d.intField = f; d.min = min; d.max = max; d.snap = snap;
		return d;
	}

	SettingDef SettingDef::text(const char* key, std::string Settings::*f)
	{
		SettingDef d;
		d.kind = TEXT; d.key = key; d.textField = f;
		return d;
	}

	SettingDef SettingDef::custom(const char* key, void (*parse)(Settings&, const std::string&), bool (*write)(const Settings&, std::string&))
	{
		SettingDef d;
		d.kind = CUSTOM; d.key = key; d.parse = parse; d.write = write;
		return d;
	}

	// ---- the table: what a line of settings.conf means, and what the Settings page shows for it ----
	// In the order the file is written (an old file is read by key, so that order matters only for byte-for-byte
	// output). A choice is stored as its index in `ids`; the page's `place` orders a group's rows (the page's own rows,
	// such as Clear play history, have places of their own in ui_settings.cpp); a default is the member's initializer.

	// a style not offered (Bars, Waves, Spectrum, and the ones kept but not offered: VIS_SHOWN) is read as Peaks Banana
	// (the default), and so is Timeline (0.5.x, briefly: it became Peaks)
	static void readVisualizer(Settings& s, const std::string& value)
	{
		if (value == "timeline") s.visualizer = VIS_PEAKS_BANANA;
		if (std::find(std::begin(VIS_SHOWN), std::end(VIS_SHOWN), s.visualizer) == std::end(VIS_SHOWN)) s.visualizer = VIS_PEAKS_BANANA;
	}

	static void readSoundFx(Settings& s, const std::string& value) { s.soundFx = soundFxFromKey(value); }
	static bool writeSoundFx(const Settings& s, std::string& value) { value = soundFxKey(s.soundFx); return true; }
	static void readEqGains(Settings& s, const std::string& value) { parseEqGains(value, s.eqGains); }
	static bool writeEqGains(const Settings& s, std::string& value) { value = formatEqGains(s.eqGains); return true; }
	static void readRemindAfter(Settings& s, const std::string& value) { s.updateRemindAfter = atoll(value.c_str()); }
	// written only once there's a time to remember
	static bool writeRemindAfter(const Settings& s, std::string& value)
	{
		if (s.updateRemindAfter <= 0) return false;
		value = std::to_string(s.updateRemindAfter);
		return true;
	}

	const std::vector<SettingDef>& settingDefs()
	{
		using D = SettingDef;
		static const char* MODE_NAMES[MANGA_MODES] = {"Right to left", "Left to right", "Vertical scroll"};
		static const char* FIT_NAMES[MANGA_FITS] = {"Page", "Width", "Fill"};
		static const char* CACHE_NAMES[MANGA_CACHES] = {"100 MB", "300 MB", "1 GB"};
		static const char* CLOCK_NAMES[CLOCKS] = {"24-hour", "12-hour", "Off"};
		static const char* SHOWN_ADULT[2] = {"Hidden", "Shown"};
		static const char* SHOWN_DNS[2] = {"Off", "Cloudflare (1.1.1.1)"};
		static const char* SHOWN_PAN[2] = {"Left", "Right"};
		static const char* SHOWN_DRAG[2] = {"The photo", "The box"};
		static const char* SHOWN_LID[2] = {"Pause", "Keep playing"};
		static const int VIZ_FPS[] = {30, 45, 60};
		static const int VOLUME_TOPS[] = {100, 120, 150};
		static const std::vector<SettingDef> table = {
			D::choice("music-show", &Settings::musicShow, SHOW_IDS, SHOWS)
				.page(SG_MUSIC, 10, "Music display", "What the top screen shows while a song plays (SELECT while playing).").shownAs(SHOW_LABELS).stepped(true),
			D::choice("visualizer", &Settings::visualizer, VIS_IDS, VISUALIZERS)
				.page(SG_MUSIC, 20, "Visualizer", "How the music is drawn: twenty-one styles, from Lite to Washing machine (SELECT while playing).")
				.shownAs(VIS_LABELS).stepped(true, stepVisualizer).afterRead(readVisualizer),
			D::ints("visualizer-fps", &Settings::vizFps, VIZ_FPS, 3)
				.page(SG_MUSIC, 30, "Visualizer frame rate", "How smoothly the visualizers move: 60 is smoothest, 30 lightest on the battery. Peaks and Circle stay at 30.")
				.shownAs(nullptr, " fps"),
			D::range("manga-filter", &Settings::mangaFilter, 0, 3),
			D::range("read-dim", &Settings::readDim, 0, 80, 10).stepped(false, nullptr, 10),
			D::ints("volume-max", &Settings::volumeMax, VOLUME_TOPS, 3)
				.page(SG_CONTROLS, 40, "Max volume", "How far the volume keys go: past 100% the player turns the sound up itself (louder than the handheld's own top, a little rougher on loud parts). Over 120% can damage your hearing and the speakers.")
				.shownAs(nullptr, "%"),
			D::choice("loop", &Settings::loop, LOOP_IDS, LOOPS),
			D::choice("manga-mode", &Settings::mangaMode, MANGA_MODE_IDS, MANGA_MODES)
				.page(SG_MANGA, 10, "Reading mode", "How a series opens until you pick one for it (X while reading).").shownAs(MODE_NAMES).stepped(true),
			D::choice("manga-fit", &Settings::mangaFit, MANGA_FIT_IDS, MANGA_FITS)
				.page(SG_MANGA, 20, "Page fit", "Width: a page as wide as the screen, read down with RIGHT a screenful at a time.").shownAs(FIT_NAMES).stepped(true),
			// the document viewers' choices, by name (textreader.h's order); their rows are Files' viewers', not this page's
			D::choice("text-font", &Settings::txtFont, TXT_FONT_IDS, 5),
			D::choice("text-size", &Settings::txtSize, TXT_SIZE_IDS, 5),
			D::choice("text-spacing", &Settings::txtSpacing, TXT_SPACING_IDS, 3),
			D::choice("text-margins", &Settings::txtMargins, TXT_MARGIN_IDS, 3),
			D::choice("text-theme", &Settings::txtTheme, TXT_THEME_IDS, 4),
			D::choice("text-filter", &Settings::txtFilter, DOC_FILTER_IDS, 4),
			D::choice("pdf-filter", &Settings::pdfFilter, DOC_FILTER_IDS, 4),
			D::choice("text-view", &Settings::txtBook, TXT_VIEW_IDS, 3),
			D::choice("text-turn", &Settings::txtTurn, TXT_TURN_IDS, 2),
			D::flag("pdf-view", &Settings::pdfStrip, "page", "strip"),
			D::ints("manga-preload", &Settings::mangaPreload, MANGA_PRELOADS, MANGA_PRELOAD_COUNT)
				.page(SG_MANGA, 60, "Preload", "More: smoother on a slow server, more data.").shownAs(nullptr, " pages").stepped(true).digitsOnly(),
			// 0 to 200 in steps of 4 (a value a file kept from before may sit off the step: it snaps onto one on the page)
			D::range("manga-hinge", &Settings::mangaHinge, 0, 200)
				.page(SG_MANGA, 40, "Hinge gap", "The part of the page hidden under the hinge in fullscreen, so pictures line up across the gap. Measure on your handheld.")
				.shownAs(nullptr, " px").stepped(false, nullptr, 4).ignoreBlank(),
			D::choice("manga-cache", &Settings::mangaCacheSize, MANGA_CACHE_IDS, MANGA_CACHES)
				.page(SG_MANGA, 70, "Page cache", "Pages kept on the card to read again without downloading. Covers: 50 MB more.").shownAs(CACHE_NAMES).stepped(true),
			D::flag("remember-position", &Settings::rememberPosition)
				.page(SG_REMEMBER, 10, "Remember position", "A video opens where you stopped. Songs always start at the beginning."),
			D::flag("remember-audio", &Settings::rememberAudio)
				.page(SG_REMEMBER, 20, "Remember audio track", "Keeps each file's audio track. A new file starts in the language you chose last."),
			D::flag("remember-subtitles", &Settings::rememberSubs)
				.page(SG_REMEMBER, 30, "Remember subtitles", "Keeps each file's subtitles. A new file starts in the language you chose last."),
			D::flag("remember-folder", &Settings::rememberFolder)
				.page(SG_FILES, 10, "Remember last folder", "The file list opens where you last were."),
			D::flag("keep-history", &Settings::keepHistory)
				.page(SG_FILES, 30, "Keep play history", "Keeps Recent. Turning it off also deletes the history."),
			D::flag("keep-network-history", &Settings::keepNetHistory)
				.page(SG_FILES, 40, "Network files in Recent", "Files played from a network folder go in Recent too. Off: they're left out, and the ones there now go."),
			D::flag("button-hotkeys", &Settings::buttonHotkeys)
				.page(SG_CONTROLS, 50, "Button hotkeys", "Off: the buttons can't pause, skip or close while something plays (touch still can)."),
			D::flag("show-hidden-folders", &Settings::showHiddenFolders)
				.page(SG_FILES, 20, "Show hidden folders", "Shows the folders you've hidden (X on a folder), so you can unhide them."),
			D::flag("battery-percent", &Settings::batteryPercent)
				.page(SG_CONTROLS, 60, "Battery percentage", "Shows the battery's percent beside its icon."),
			D::flag("keep-photo-zoom", &Settings::keepPhotoZoom)
				.page(SG_PHOTOS, 10, "Keep zoom between photos", "The next photo opens at the same zoom and place. Off: each opens whole."),
			D::flag("check-updates", &Settings::checkUpdates)
				.page(SG_UPDATES, 10, "Check automatically", "Looks for a new version a few seconds after the start (Wi-Fi needed)."),
			D::flag("same-brightness", &Settings::sameBrightness)
				.page(SG_CONTROLS, 30, "Same for both screens", "One brightness for both screens. Off: each its own."),
			D::flag("shuffle", &Settings::shuffle),	// (no row: the play mode, as loop)
			D::flag("manga-reader", &Settings::mangaOn),
			D::flag("manga-intro-shown", &Settings::mangaIntroShown),
			D::flag("manga-swap-lr", &Settings::mangaSwapLR)
				.page(SG_MANGA, 30, "Swap Left/Right", "Off: RIGHT is always forward. On: LEFT is forward (to match a right-to-left book)."),
			D::flag("manga-adult", &Settings::mangaAdult)
				.page(SG_MANGA, 100, "Adult sources", "Sources a repository marks as adult.").shownAs(SHOWN_ADULT),
			D::flag("manga-history", &Settings::mangaHistory)
				.page(SG_MANGA, 110, "Reading history", "Off clears it."),
			D::flag("manga-recent", &Settings::mangaRecent),	// (kept, not shown: the reader stays off Home)
			D::flag("manga-full-seen", &Settings::mangaFullSeen),
			D::flag("manga-open-full", &Settings::mangaOpenFull)
				.page(SG_MANGA, 50, "Open fullscreen", "A chapter opens on both screens. SELECT leaves."),
			D::flag("manga-secure-dns", &Settings::mangaSecureDns)
				.page(SG_MANGA, 120, "Secure DNS", "Looks up sites through Cloudflare over HTTPS (one.one.one.one), past a network that blocks them.").shownAs(SHOWN_DNS),
			D::flag("song-info", &Settings::songInfo)
				.page(SG_MUSIC, 40, "Song info on top", "The song's title, artist and album over the top screen's picture (None shows the title anyway)."),
			D::choice("clock", &Settings::clock, CLOCK_IDS, CLOCKS)
				.page(SG_CONTROLS, 70, "Clock", "The time in the top corner of this screen").shownAs(CLOCK_NAMES).stepped(true),
			// (not round: Never at one end, 5 minutes at the other)
			D::choice("screen-off", &Settings::screenOffAfter, SCREEN_OFF_IDS, SCREEN_OFFS)
				.page(SG_CONTROLS, 80, "Screen off (music)", "While a song plays, this screen goes dark after that long untouched. A touch brings it back.").shownAs(SCREEN_OFF_LABELS),
			D::choice("screen-off-video", &Settings::screenOffVideo, SCREEN_OFF_IDS, SCREEN_OFFS)
				.page(SG_CONTROLS, 90, "Screen off (video)", "While a video plays, this screen goes dark after that long untouched. A touch brings it back.").shownAs(SCREEN_OFF_LABELS),
			D::flag("lid-close", &Settings::lidKeepPlaying, "pause", "keep-playing")
				.page(SG_CONTROLS, 10, "When the lid closes", "Keep playing: while something plays (or the web dashboard is on), BananaOS's \"Closing the lid\" is switched to stay awake, and back after (then the handheld sleeps if the lid is shut).")
				.shownAs(SHOWN_LID),
			D::flag("pan-area", &Settings::panRight, "left", "right")
				.page(SG_PHOTOS, 20, "Pan area", "Which side the pan area (the small photo) is on.").shownAs(SHOWN_PAN),
			D::flag("pan-drag", &Settings::dragBox, "photo", "box")
				.page(SG_PHOTOS, 30, "Drag moves", "The box: a drag moves the frame over the small photo. The photo: a drag moves the photo, as on a phone.").shownAs(SHOWN_DRAG),
			D::flag("eq", &Settings::eqOn, "off", "on"),
			D::text("eq-preset", &Settings::eqPreset),
			D::text("manga-langs", &Settings::mangaLangs),
			D::text("eq-origin", &Settings::eqOrigin),
			D::custom("eq-gains", readEqGains, writeEqGains),
			D::custom("sound-effect", readSoundFx, writeSoundFx),
			D::flag("web-dashboard", &Settings::webDashboard, "off", "on"),
			D::custom("update-remind-after", readRemindAfter, writeRemindAfter),
		};
		return table;
	}

	int settingIndex(const char* key)
	{
		const auto& defs = settingDefs();
		for (size_t i = 0; i < defs.size(); i++)
			if (std::strcmp(defs[i].key, key) == 0) return (int)i;
		return -1;
	}

	std::vector<int> settingsOfGroup(int group)
	{
		const auto& defs = settingDefs();
		std::vector<int> out;
		for (size_t i = 0; i < defs.size(); i++)
			if (defs[i].group == group) out.push_back((int)i);
		std::stable_sort(out.begin(), out.end(), [&](int a, int b) { return defs[a].place < defs[b].place; });
		return out;
	}

	std::string settingValueText(const SettingDef& d, const Settings& s)
	{
		switch (d.kind)
		{
		case SettingDef::FLAG:
		{
			bool on = s.*d.flagField;
			return d.shown ? d.shown[on] : on ? "On" : "Off";
		}
		case SettingDef::CHOICE:
		{
			int v = s.*d.intField;
			return d.shown[v >= 0 && v < d.count ? v : 0];
		}
		default: return std::to_string(s.*d.intField) + d.unit;
		}
	}

	void stepSetting(const SettingDef& d, Settings& s, int dir)
	{
		switch (d.kind)
		{
		case SettingDef::FLAG: s.*d.flagField = !(s.*d.flagField); break;
		case SettingDef::CHOICE:
		{
			int& v = s.*d.intField;
			if (d.stepFn) v = d.stepFn(v, dir);
			else if (d.wrap) v = ((v + dir) % d.count + d.count) % d.count;
			else v = std::clamp(v + dir, 0, d.count - 1);
			break;
		}
		case SettingDef::INT_LIST:
		{
			// a value that isn't listed starts from the first
			int i = 0;
			for (int k = 0; k < d.count; k++)
				if (d.values[k] == s.*d.intField) i = k;
			i = d.wrap ? (i + (dir < 0 ? -1 : 1) + d.count) % d.count : std::clamp(i + dir, 0, d.count - 1);
			s.*d.intField = d.values[i];
			break;
		}
		case SettingDef::INT_RANGE:
		{
			int v = std::clamp(s.*d.intField + dir * d.stepBy, d.min, d.max);
			s.*d.intField = v - v % d.stepBy;
			break;
		}
		default: break;
		}
	}

	std::string serializeSettings(const Settings& s)
	{
		std::string out = serializeSubStyle(s.style);
		for (const auto& d : settingDefs())
		{
			std::string value;
			switch (d.kind)
			{
			case SettingDef::FLAG: value = d.ids[s.*d.flagField ? 1 : 0]; break;
			case SettingDef::CHOICE: value = d.ids[std::clamp(s.*d.intField, 0, d.count - 1)]; break;
			case SettingDef::INT_LIST: case SettingDef::INT_RANGE: value = std::to_string(s.*d.intField); break;
			case SettingDef::TEXT: value = s.*d.textField; break;
			case SettingDef::CUSTOM: if (!d.write(s, value)) continue; break;
			}
			out += std::string(d.key) + " = " + value + "\n";
		}
		return out;
	}

	// one line's value into the setting it names; what isn't a value of it leaves the setting as it was
	static void readSetting(const SettingDef& d, Settings& s, const std::string& value)
	{
		switch (d.kind)
		{
		case SettingDef::FLAG:
			if (value == d.ids[0]) s.*d.flagField = false;
			else if (value == d.ids[1]) s.*d.flagField = true;
			break;
		case SettingDef::CHOICE:
			for (int v = 0; v < d.count; v++)
				if (value == d.ids[v]) s.*d.intField = v;
			break;
		case SettingDef::INT_LIST:
		{
			int n = std::atoi(value.c_str());
			for (int v = 0; v < d.count; v++)
				if (d.canonical ? value == std::to_string(d.values[v]) : n == d.values[v]) s.*d.intField = d.values[v];
			break;
		}
		case SettingDef::INT_RANGE:
		{
			int n = std::atoi(value.c_str());
			if (n >= d.min && n <= d.max && !(d.blankIgnored && value.empty())) s.*d.intField = n / d.snap * d.snap;
			break;
		}
		case SettingDef::TEXT: s.*d.textField = value; break;
		case SettingDef::CUSTOM: d.parse(s, value); break;
		}
		if (d.after) d.after(s, value);
	}

	Settings parseSettings(const std::string& text)
	{
		Settings s;
		s.style = parseSubStyle(text);	// it leaves out the keys below
		s.lidAsk = true;	// (until its line is read below)
		const auto& defs = settingDefs();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
			auto trim = [](std::string v) {
				size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
				return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
			};
			std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
			for (const auto& d : defs)
				if (key == d.key) { readSetting(d, s, value); break; }
			if (key == "lid-close") s.lidAsk = false;
			// the one setting before 0.1.1, kept: auto, cover-bars, bars, waves, spectrum, cover
			if (key == "audio-display")
			{
				if (value == "cover-bars") { s.musicShow = SHOW_BOTH; s.visualizer = VIS_PEAKS_BANANA; }
				else if (value == "bars" || value == "waves" || value == "spectrum")
				{
					s.musicShow = SHOW_VIS;
					s.visualizer = VIS_PEAKS_BANANA;	// retired: read as Peaks Banana
				}
				else s.musicShow = SHOW_ART;
			}
		}
		return s;
	}
}
