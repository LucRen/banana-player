#include "viz_more.h"
#include "settings.h"
#include "theme.h"
#include "vizcache.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <sys/stat.h>
#include <vector>

// The six simple styles (2026-11-19, docs/notes/2026-11-19-viz-simple.md): Mirror bars, Wave, Dots, Levels, Horizon
// (Area's place) and Steps (Radial's). Plain shapes in the theme's own colours, two or three roles each (ACCENT for
// what moves, TEXT or DIM for a second mark, BUTTON for a faint backdrop), on the theme's BG as Peaks is; rebuilt on a theme change as Peaks is
// (Player::themeChanged).
//
// One recipe for all six, Peaks' (settings.cpp): what changes each frame is worked out at half size (320x240 alone,
// 320x100 under the art) as a gray picture of small codes (a class, or a strength from the ground to ACCENT), by
// lut2 against a still picture (one frame, made once in the graph: never a loop). Half size is
// the chroma's size, so U and V are a lut each of that picture as it is; only the luma is scaled up (x2: bilinear
// for Wave's line, nearest for the square ones). YUV straight to mpv, nothing converted per frame.
// Over the art (the band: the theme's ground, vizPlaceArt) the art's planes are each a lut2's second input, kept
// where the code is 0. Written at 60 frames a second (rate=60), drawn at the visualizer-fps setting (vizAtFps).

namespace bp
{
	namespace
	{
		struct Yuv { int y, u, v; };

		// BT.601 limited (mpv's reading of a picture this size)
		Yuv yuvOf(theme::Rgb c)
		{
			double r = c.r, g = c.g, b = c.b;
			return {(int)std::lround(16 + (65.481 * r + 128.553 * g + 24.966 * b) / 255),
					(int)std::lround(128 + (-37.797 * r - 74.203 * g + 112 * b) / 255),
					(int)std::lround(128 + (112 * r - 93.786 * g - 18.214 * b) / 255)};
		}

		int plane(const Yuv& c, int p) { return p == 0 ? c.y : p == 1 ? c.u : c.v; }
		std::string n(int v) { return std::to_string(v); }
		std::string hex(theme::Rgb c)
		{
			char s[8];
			snprintf(s, sizeof s, "%02X%02X%02X", c.r, c.g, c.b);
			return s;
		}

		// a plane's expression: the value for code `var` in plane p; `ground` the ground's value ("y": the art's)
		using PlaneExpr = std::function<std::string(int p, const std::string& var, const std::string& ground)>;

		// classes: code k (1..) the k-th ink, 0 the ground
		PlaneExpr classes(const std::vector<Yuv>& inks)
		{
			return [inks](int p, const std::string& var, const std::string& ground) {
				std::string e;
				for (size_t k = 0; k < inks.size(); k++) e += "if(eq(" + var + "," + n((int)k + 1) + ")," + n(plane(inks[k], p)) + ",";
				return e + ground + std::string(inks.size(), ')');
			};
		}

		// a strength: code 0..250 the ground to `ink`; 255 the second ink `mark` (when given)
		PlaneExpr ramp(const Yuv& ink, const Yuv* mark = nullptr)
		{
			const Yuv m = mark ? *mark : ink;
			const bool hasMark = mark != nullptr;
			return [ink, m, hasMark](int p, const std::string& var, const std::string& ground) {
				const std::string g = "(" + ground + ")";
				std::string e = "floor(" + g + "+(" + n(plane(ink, p)) + "-" + g + ")*min(" + var + ",250)/250+0.5)";
				return hasMark ? "if(eq(" + var + ",255)," + n(plane(m, p)) + "," + e + ")" : e;
			};
		}

		// the art's slice under the band, in planes (as Peaks): [top] the art's top, [<t>ay] [<t>au] [<t>av] its strip
		std::string artIn(const std::string& t)
		{
			return "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=yuv420p,"
				   "settb=AVTB,split [" + t + "art1] [" + t + "art2]; [" + t + "art1] crop=640:280:0:0 [top]; [" + t +
				   "art2] crop=640:200:0:280,extractplanes=y+u+v [" + t + "ay] [" + t + "au] [" + t + "av]; ";
		}

		// the codes picture [<t>C] (320 x H) painted: alone on the theme's BG, or over the art's planes. smooth: the
		// luma scaled up bilinear (alone the colours are scaled, over the art the codes: only a ramp's codes may be
		// blended there, a class's would turn into another class at the edges)
		std::string paint(const std::string& t, bool overCover, bool smooth, const PlaneExpr& e)
		{
			const Yuv bg = yuvOf(theme::BG);
			const std::string c = "[" + t + "C] split=3 [" + t + "p0] [" + t + "p1] [" + t + "p2]; ";
			if (overCover)
				return c + "[" + t + "p0] scale=640:200:flags=" + (smooth ? "bilinear" : "neighbor") + " [" + t + "p0s]; "
					   "[" + t + "p0s] [" + t + "ay] bplut2=c0='" + e(0, "x", "y") + "' [" + t + "Y]; "
					   "[" + t + "p1] [" + t + "au] bplut2=c0='" + e(1, "x", "y") + "' [" + t + "U]; "
					   "[" + t + "p2] [" + t + "av] bplut2=c0='" + e(2, "x", "y") + "' [" + t + "W]; "
					   "[" + t + "Y] [" + t + "U] [" + t + "W] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack [vo]";
			return c + "[" + t + "p0] bplut=c0='" + e(0, "val", n(bg.y)) + "',scale=640:480:flags=" + (smooth ? "bilinear" : "neighbor") +
				   " [" + t + "Y]; "
				   "[" + t + "p1] bplut=c0='" + e(1, "val", n(bg.u)) + "' [" + t + "U]; "
				   "[" + t + "p2] bplut=c0='" + e(2, "val", n(bg.v)) + "' [" + t + "W]; "
				   "[" + t + "Y] [" + t + "U] [" + t + "W] mergeplanes=0x001020:yuv420p [vo]";
		}

		// the spectrum in N levels as one row [<t>L] (N x 1, 0..252), measured as Peaks and Pocket console measure it
		// (the top eighth, 16-24 kHz, cut off), its contrast pushed as Pocket console's, the bottom 40 steps (the
		// spectrum's near-constant bed) cut off so the shapes don't stand tall through any song, eased as an LED
		// meter's (up at once, down over ~0.15 s for the whole height). (format=gray after a bplut: the conversion to
		// 16 bits stays after it)
		std::string spectrumRow(const std::string& t, int N)
		{
			const int TOP = std::max(2, N / 8);
			return "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + n(N + TOP) +
				   "x256:mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=60:colors=white,crop=" + n(N) +
				   ":256:0:0,scale=" + n(N) + ":1:flags=area,format=gray,bplut=c0='val*252/255'" + vizContrast(t, 1, "4", "1.5", "160") +
				   ",bplut=c0='clip((val-40)*252/212,0,252)',format=gray,format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.311,"
				   "lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray [" + t + "L]; ";
		}

		// a row stretched to W x H (row copies: scale on one row, pad, fillborders), as Peaks does
		std::string stretch(int W, int H, const char* flags)
		{
			const int mid = H / 2 - 1;
			return "scale=" + n(W) + ":1:flags=" + flags + ",pad=" + n(W) + ":" + n(H) + ":0:" + n(mid) + ",fillborders=top=" + n(mid) +
				   ":bottom=" + n(H - 1 - mid) + ":mode=smear,setsar=1";
		}

		// a still gray picture, one frame
		std::string still(int W, int H, const std::string& lum)
		{
			return "color=black:s=" + n(W) + "x" + n(H) + ":r=1:d=1,format=gray,geq=lum='" + lum + "'";
		}

		const char* themeName() { return theme::themeId(theme::currentTheme()); }

		struct Timer
		{
			std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
			double ms() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
		};

	}

	// Mirror bars: the spectrum in 40 bars, each grown up and down from a centre line by the same height. Bars ACCENT,
	// the centre line DIM (in the gaps, and under a bar that's quiet)
	std::string mirrorGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "mb";
		const int W = 320, H = overCover ? 100 : 240, BW = 8, MAXD = overCover ? 46 : 108;
		// each pixel's code: its height from the centre (1..251; rows 0 the centre's two), 253 a gap or past the
		// tallest bar, 254 the centre line in a gap
		const std::string D = "((abs(2*Y+1-" + n(H) + ")-1)/2)";
		const std::string gap = "gte(mod(X," + n(BW) + ")," + n(BW - 1) + ")+lt(mod(X," + n(BW) + "),1)";
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, W / BW) +
						"[" + t + "L] " + stretch(W, H, "neighbor") + " [" + t + "V]; " +
						still(W, H, "if(" + gap + ",if(lt(" + D + ",1),254,253),if(gt(" + D + "," + n(MAXD) + "),253,1+floor(250*" + D + "/" + n(MAXD) + ")))") +
						" [" + t + "R]; "
						"[" + t + "V] [" + t + "R] bplut2=c0='if(eq(y,254),2,lt(y,253)*lte(y,x))' [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::ACCENT), yuvOf(theme::DIM)}));
		fprintf(stderr, "visualizer: Mirror bars in %s: bars #%s, line #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::DIM).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Wave: the sound's waveform as one thick, smooth line in ACCENT (drawn at a quarter size, soft-edged by the
	// scale up), a faint DIM centre line under it (alone)
	std::string waveGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "wv";
		const int W = 320, H = overCover ? 100 : 240;
		// mono, a lowpass (cymbals and hiss would turn the line to fuzz: the shape is the bass and the voices), a
		// little under full scale. showwaves draws straight into gray (255 on the line, 1 pixel); averaged down to
		// half this size (area: where the line passes, to a fraction of a pixel) and scaled back up (bilinear) it is
		// ~3 pixels wide here and soft, x4 makes it solid with a soft edge. (Drawn small and scaled up, it went in
		// steps; an afterglow, lagfun, left two or three ghost lines at 60 a second: not a simple line any more)
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) +
						"[a] aformat=sample_rates=44100|48000:channel_layouts=mono,lowpass=f=1200,volume=0.85,showwaves=s=" + n(W) + "x" +
						n(H) + ":mode=p2p:rate=60:draw=full:colors=white,format=gray,scale=" + n(W / 2) + ":" + n(H / 2) + ":flags=area,scale=" +
						n(W) + ":" + n(H) + ":flags=bilinear,format=gray,bplut=c0='min(val*4,250)' [" + t + (overCover ? "C" : "I") + "]; ";
		const Yuv dim = yuvOf(theme::DIM);
		// alone, 255: the centre line where the trace is faint. Over the art the line alone, its codes (a ramp only)
		// scaled bilinear: the centre line's code would blend into a fringe of ACCENT there
		if (!overCover)
			g += still(W, H, "255*eq(Y," + n(H / 2) + ")") + " [" + t + "M]; [" + t + "I] [" + t + "M] bplut2=c0='if(y*lt(x,60),255,x)' [" + t + "C]; ";
		g += paint(t, overCover, true, overCover ? ramp(yuvOf(theme::ACCENT)) : ramp(yuvOf(theme::ACCENT), &dim));
		fprintf(stderr, "visualizer: Wave in %s: line #%s, centre #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::DIM).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Dots: an LED dot-matrix spectrum, 32 columns of round-cornered dots, lit from the bottom (ACCENT), the unlit
	// ones a faint grid (BUTTON)
	std::string dotsGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "dt";
		const int N = 32, CW = 10, W = 320, H = overCover ? 100 : 240;
		const int S = overCover ? 9 : 22, Y0 = (H - S * CW) / 2;
		// a pixel a dot (N x S): 1 unlit, 2 lit (the level past the dot's middle), by a still of each dot's row
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, N) +
						"[" + t + "L] scale=" + n(N) + ":" + n(S) + ":flags=neighbor,setsar=1 [" + t + "V]; " +
						still(N, S, n(S - 1) + "-Y") + " [" + t + "R]; "
						"[" + t + "V] [" + t + "R] bplut2=c0='1+gte(x,floor(252*(y+0.5)/" + n(S) + "))',"
						// the dots' grid: each a 10 x 10 cell (the dot 8 x 8, its corners cut: round at this size)
						"scale=" + n(N * CW) + ":" + n(S * CW) + ":flags=neighbor,pad=" + n(W) + ":" + n(H) + ":0:" + n(Y0) + ",setsar=1 [" + t + "G]; " +
						still(W, H, "between(Y," + n(Y0) + "," + n(Y0 + S * CW - 1) + ")*between(mod(X,10),1,8)*between(mod(Y-" + n(Y0) +
							",10),1,8)*(1-(eq(mod(X,10),1)+eq(mod(X,10),8))*(eq(mod(Y-" + n(Y0) + ",10),1)+eq(mod(Y-" + n(Y0) + ",10),8)))") +
						" [" + t + "M]; "
						"[" + t + "G] [" + t + "M] bplut2=c0='if(y,x,0)' [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::BUTTON), yuvOf(theme::ACCENT)}));
		fprintf(stderr, "visualizer: Dots in %s: lit #%s, unlit #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Levels: two wide stereo meters (left above right), the level ACCENT along a faint track (BUTTON), a peak tick
	// (TEXT) held at the highest point and falling back slowly
	std::string levelsGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "lv";
		const int W = 320, H = overCover ? 100 : 240;
		const int BH = overCover ? 30 : 48, GAP = overCover ? 10 : 16, Y0 = (H - 2 * BH - GAP) / 2, X0 = 16, X1 = W - 16;
		// The two channels' loudness (RMS a frame) from showvolume, drawn as two 252-pixel bars on a log scale and
		// averaged down to a pixel each (the share lit); the scale's 2.68 steps a dB from ~0 dB at 247: -30 dB to 0
		// dB spread over 0..252 (a mastered song's loudness, -14 to -8 dB, about the middle). In stereo: the measured copy isn't evened out to mono as the other styles' is
		// (asplit=2: vizShapeInput leaves this graph as it is). The level eased (down over ~0.4 s), the peak (~2.5 s)
		std::string g = "[aid1] asplit=2 [ao] [a]; " + (overCover ? artIn(t) : std::string()) +
						// (format=gray after scale: else the lut would be given 16 bits, the conversion moved before it)
						"[a] aformat=sample_rates=44100|48000:channel_layouts=stereo,showvolume=rate=60:w=252:h=2:b=0:t=0:v=0:f=0:ds=log:m=r:"
						"c=0xFFFFFFFF,format=gray,scale=1:2:flags=area,format=gray,bplut=c0='clip((val-167)*252/80,0,252)',"
						"format=gray16le,lut=c0='65535*exp((val/257-252)/24)',split [" + t + "e1] [" + t + "e2]; "
						"[" + t + "e1] lagfun=decay=0.646,lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,"
						"scale=" + n(W) + ":" + n(H) + ":flags=neighbor,setsar=1 [" + t + "V]; "
						"[" + t + "e2] lagfun=decay=0.9316,lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray,"
						"scale=" + n(W) + ":" + n(H) + ":flags=neighbor,setsar=1 [" + t + "P]; " +
						// each pixel's place along its meter (1..252), 253 off the meters (the upper half of the picture
						// is the left channel's, the lower the right's: the bars sit one in each)
						still(W, H, "if(between(X," + n(X0) + "," + n(X1 - 1) + ")*(between(Y," + n(Y0) + "," + n(Y0 + BH - 1) + ")+between(Y," +
							n(Y0 + BH + GAP) + "," + n(Y0 + 2 * BH + GAP - 1) + ")),1+floor(251*(X-" + n(X0) + ")/" + n(X1 - X0 - 1) + "),253)") +
						" [" + t + "R]; "
						// 254: the peak's tick (3 steps up to the peak, ~4 pixels)
						"[" + t + "P] [" + t + "R] bplut2=c0='if(lt(y,253)*gt(x,3)*gt(y,x-4)*lte(y,x),254,y)' [" + t + "Q]; "
						"[" + t + "V] [" + t + "Q] bplut2=c0='if(eq(y,254),3,if(lt(y,253),1+lte(y,x),0))' [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::BUTTON), yuvOf(theme::ACCENT), yuvOf(theme::TEXT)}));
		fprintf(stderr, "visualizer: Levels in %s: level #%s, track #%s, peak #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::TEXT).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Horizon (in Area's place, 2026-11-19): the spectrum as a mirrored skyline (20 bands a side, the bass in the
	// middle falling away to the sides) on a horizon line, a low sun behind it, and under the line the skyline's
	// reflection, flipped, squashed and striped. Skyline ACCENT, sun BUTTON, the line and the reflection DIM.
	// The levels' row (showfreqs' bars measured as the others') is mirrored as a row (hflip + hstack: 40 values),
	// stretched to the sky and told apart by lut2 against a still of each row's height and the sun's disc; the
	// reflection is that picture vflipped, squashed (nearest) and masked by a still of stripes and the line; the two
	// stacked (vstack) are the codes the palette paints, as the other styles
	std::string horizonGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "hz";
		const int N = 20, W = 320, H = overCover ? 100 : 240, SKY = overCover ? 64 : 160, REF = H - SKY;
		// the sun: low, its middle under the line's height, part of it showing above
		const int SX = W / 2, SY = SKY * 4 / 5, SR = SKY / 2;
		// each sky pixel's height step (1..126, 3 levels a step: the tallest building two thirds of the sky, the sun
		// over it), +128 inside the sun
		const std::string h = "1+floor(125*(" + n(SKY - 1) + "-Y)/" + n(SKY - 1) + ")";
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, N) +
						"[" + t + "L] split [" + t + "l1] [" + t + "l2]; [" + t + "l1] hflip [" + t + "lf]; "
						"[" + t + "lf] [" + t + "l2] hstack,format=gray," + stretch(W, SKY, "neighbor") + " [" + t + "V]; " +
						still(W, SKY, h + "+128*lte(pow(X-" + n(SX) + ",2)+pow(Y-" + n(SY) + ",2)," + n(SR * SR) + ")") + " [" + t + "R]; "
						// 1 the skyline, 2 the sun, 0 the sky
						"[" + t + "V] [" + t + "R] bplut2=c0='if(lte(3*mod(y,128),x),1,if(gte(y,128),2,0))',split [" + t + "s1] [" + t + "s2]; "
						// the reflection: its first two rows the line (2 in the still), then every other row (1)
						"[" + t + "s2] vflip,scale=" + n(W) + ":" + n(REF) + ":flags=neighbor,setsar=1 [" + t + "F]; " +
						still(W, REF, "if(lt(Y,2),2,mod(Y,2))") + " [" + t + "M]; "
						"[" + t + "F] [" + t + "M] bplut2=c0='if(eq(y,2),3,if(y*eq(x,1),3,0))' [" + t + "r]; "
						"[" + t + "s1] [" + t + "r] vstack [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::ACCENT), yuvOf(theme::BUTTON), yuvOf(theme::DIM)}));
		fprintf(stderr, "visualizer: Horizon in %s: skyline #%s, sun #%s, line and reflection #%s on #%s; graph built in %.1f ms\n",
				themeName(), hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::DIM).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Steps (in Radial's place, 2026-11-19): the spectrum as a 20-band staircase drawn as one outline (ACCENT), faintly
	// filled beneath (BUTTON), and a slow second line holding each band's peak (DIM). Each band 16 columns here, by
	// nearest stretching of the levels' row; the outline is where the fill's edge is: its top (the step under the
	// level) and, at a band's first two columns, the rise or fall from the band on its left (a second row, the
	// levels shifted one band, says where that one stood). Two lut2 against a still of each pixel's step (1..84, 3
	// levels a step; +85 at a band's first columns, +170 there when under the left band's level), a third with the
	// held peaks: the codes the palette paints, as the other styles. (The proposal's mask minus itself shifted,
	// blend=difference, done by the same lookups: no full-size blend or shifted copy a frame)
	std::string stepsGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "sp";
		const int N = 20, BW = 16, W = 320, H = overCover ? 100 : 240;
		const int E = overCover ? 2 : 1;	// the outline's top, in steps (~2.4 and ~2.9 rows)
		const std::string r = "1+floor(83*(" + n(H - 1) + "-Y)/" + n(H - 1) + ")";
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, N) +
						"[" + t + "L] split=3 [" + t + "l1] [" + t + "l2] [" + t + "l3]; "
						"[" + t + "l1] " + stretch(W, H, "neighbor") + " [" + t + "V]; "
						// the band on the left's level (the first band's: nothing)
						"[" + t + "l2] pad=" + n(N + 1) + ":1:1:0:black,crop=" + n(N) + ":1:0:0,format=gray," + stretch(W, H, "neighbor") + " [" + t + "Vl]; "
						// the held peaks: down over ~3.4 s for the whole height
						"[" + t + "l3] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.95,"
						"lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray," + stretch(W, H, "neighbor") + " [" + t + "P]; " +
						still(W, H, r + "+85*gte(X," + n(BW) + ")*lt(mod(X," + n(BW) + "),2)") + " [" + t + "R]; "
						"[" + t + "Vl] [" + t + "R] bplut2=c0='if(gte(y,85),85+mod(y,85)+85*lte(3*mod(y,85),x),y)' [" + t + "Q]; "
						// 1 the outline, 2 the fill, else the step (as class*85 + step: the peak's lut2 needs the step)
						"[" + t + "V] [" + t + "Q] bplut2=c0='85*if(gt(3*mod(y,85),x-" + n(3 * E) + ")*lte(3*mod(y,85),x)+gte(y,85)*"
						"(1-eq(lte(3*mod(y,85),x),gte(y,170))),1,if(lte(3*mod(y,85),x),2,0))+mod(y,85)' [" + t + "S]; "
						// the peak's step: 3 (DIM) where not the outline
						"[" + t + "P] [" + t + "S] bplut2=c0='if(eq(floor(y/85),1),1,if(gt(x,3)*eq(mod(y,85),floor(x/3)),3,floor(y/85)))' [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::ACCENT), yuvOf(theme::BUTTON), yuvOf(theme::DIM)}));
		fprintf(stderr, "visualizer: Steps in %s: outline #%s, fill #%s, peaks #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::DIM).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}
}
