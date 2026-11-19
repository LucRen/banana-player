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

// The simple styles (2026-11-19, docs/notes/2026-11-19-viz-simple.md): Mirror bars, Dots, Horizon, Steps, Barcode
// and Rings. Plain shapes in the theme's own colours, two or three roles each (ACCENT for what moves, TEXT or DIM for
// a second mark, BUTTON for a faint backdrop), on the theme's BG as Peaks is; rebuilt on a theme change as Peaks is
// (Player::themeChanged).
//
// One recipe for all, Peaks' (settings.cpp): what changes each frame is worked out at half size (320x240 alone,
// 320x100 under the art) as a gray picture of small codes (a class, or a strength between two inks), by lut2
// against a still picture (one frame, made once in the graph, or Rings' tables read from a file: never a loop).
// Half size is the chroma's size, so U and V are a lut each of that picture as it is; only the luma is scaled up
// (x2: bilinear for the round one alone, nearest for the square ones). YUV straight to mpv, nothing converted per
// frame. Over the art (the band: the theme's ground, vizPlaceArt) the art's planes are each a lut2's second input,
// kept where the code is 0. Written at 60 frames a second (rate=60), drawn at the visualizer-fps setting (vizAtFps).

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

		// a strength: code 0 the ground, 1..251 from `from` (1) to `to` (251)
		PlaneExpr between(const Yuv& from, const Yuv& to)
		{
			return [from, to](int p, const std::string& var, const std::string& ground) {
				return "if(" + var + ",floor(" + n(plane(from, p)) + "+" + n(plane(to, p) - plane(from, p)) + "*(min(" + var + ",251)-1)/250+0.5)," +
					   ground + ")";
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
					   "[" + t + "Y] [" + t + "U] [" + t + "W] mergeplanes=0x001020:yuv420p [strip]; [top] [strip] vstack=shortest=0 [vo]";
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

		// A filter reading a still (one frame, then EOF) must repeat that frame and never end when it does: the
		// visualizer branch shares the sound with [ao] through asplit, so a branch that stops pulling blocks the
		// speakers (docs/notes/2026-11-19-viz-simple.md, "The stall"). The framesync defaults are these; saying them
		// keeps the graphs safe if a build's defaults change (remap takes no framesync options: it holds its maps).
		const char* STILL_REPEAT = ":eof_action=repeat:shortest=0:repeatlast=1";

		const char* themeName() { return theme::themeId(theme::currentTheme()); }

		struct Timer
		{
			std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
			double ms() const { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); }
		};

		// Rings' tables (remap's xmap and ymap, 16-bit) for the top left quarter of its square (the others are it
		// mirrored, hflip and vflip: a quarter of the lookups): alone a 240 square, under the art a 100 square. Each
		// pixel points into the rings' picture (11 x RG_T): its ring's column (0 the centre's, 9 the rim's) and its
		// distance from the ring's middle line (row 0 on it, then out to the widest the ring grows), column 10 the
		// centre dot; elsewhere off the picture (remap's fill: code 0). A new name whenever they change
		const int RG_N = 10, RG_T = 8;
		struct RingsBox { int box; double dot, first, gap; const char* xf; const char* yf; };
		const RingsBox RG_ALONE = {240, 6, 16, 10.6, "rings1-ax.pgm", "rings1-ay.pgm"};
		const RingsBox RG_STRIP = {100, 3, 7, 4.5, "rings1-sx.pgm", "rings1-sy.pgm"};

		bool writeRings(const std::string& dir, const RingsBox& b)
		{
			const std::string xp = dir + "/" + b.xf, yp = dir + "/" + b.yf;
			const int w = b.box / 2;
			const size_t size = pgmFileSize(w, w, 65535);
			if (pgmFileOk(xp, size) && pgmFileOk(yp, size)) return true;
			std::vector<unsigned char> xs((size_t)w * w * 2), ys(xs.size());
			const double c = b.box / 2.0, reach = 0.42 * b.gap, core = 0.75;
			for (int y = 0; y < w; y++)
				for (int x = 0; x < w; x++)
				{
					const double d = std::hypot(x + 0.5 - c, y + 0.5 - c);
					int mx = 65535, my = 65535;
					if (d <= b.dot) mx = RG_N, my = 0;
					else
					{
						const int k = (int)std::lround((d - b.first) / b.gap);
						const double o = std::fabs(d - (b.first + k * b.gap));
						if (k >= 0 && k < RG_N && o < reach)
						{
							mx = k;
							my = o < core ? 0 : std::min(RG_T - 1, 1 + (int)std::floor((o - core) / (reach - core) * (RG_T - 1)));
						}
					}
					const size_t i = ((size_t)y * w + x) * 2;
					xs[i] = (unsigned char)(mx >> 8), xs[i + 1] = (unsigned char)mx;
					ys[i] = (unsigned char)(my >> 8), ys[i + 1] = (unsigned char)my;
				}
			mkdir(dir.c_str(), 0755);
			return writePgmFile(xp, w, w, 65535, 1, xs) && writePgmFile(yp, w, w, 65535, 1, ys);
		}
	}

	// Mirror bars: the spectrum in 40 bars, each grown up and down from a centre line by the same height. Bars ACCENT,
	// the centre line DIM (in the gaps, and under a bar that's quiet). A local song's Mirror bars are drawn by the player
	// (src/viznative.h, docs/notes/2026-11-19-viz-native.md); this graph is a network song's, and a file the tap can't read
	// (so are this file's other five styles and Peaks: their graphs remain only as that fallback)
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
						"[" + t + "V] [" + t + "R] bplut2=c0='if(eq(y,254),2,lt(y,253)*lte(y,x))'" + STILL_REPEAT + " [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::ACCENT), yuvOf(theme::DIM)}));
		fprintf(stderr, "visualizer: Mirror bars in %s: bars #%s, line #%s on #%s; graph built in %.1f ms\n", themeName(),
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
		// alone 17 rows from 10 above the foot: the tallest column tops out at 75% of the height (the song's lines
		// above it, on the ground); in the band 9, centred
		const int S = overCover ? 9 : 17, Y0 = overCover ? (H - S * CW) / 2 : H - 10 - S * CW;
		// a pixel a dot (N x S): 1 unlit, 2 lit (the level past the dot's middle), by a still of each dot's row
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, N) +
						"[" + t + "L] scale=" + n(N) + ":" + n(S) + ":flags=neighbor,setsar=1 [" + t + "V]; " +
						still(N, S, n(S - 1) + "-Y") + " [" + t + "R]; "
						"[" + t + "V] [" + t + "R] bplut2=c0='1+gte(x,floor(252*(y+0.5)/" + n(S) + "))'" + STILL_REPEAT + ","
						// the dots' grid: each a 10 x 10 cell (the dot 8 x 8, its corners cut: round at this size)
						"scale=" + n(N * CW) + ":" + n(S * CW) + ":flags=neighbor,pad=" + n(W) + ":" + n(H) + ":0:" + n(Y0) + ",setsar=1 [" + t + "G]; " +
						still(W, H, "between(Y," + n(Y0) + "," + n(Y0 + S * CW - 1) + ")*between(mod(X,10),1,8)*between(mod(Y-" + n(Y0) +
							",10),1,8)*(1-(eq(mod(X,10),1)+eq(mod(X,10),8))*(eq(mod(Y-" + n(Y0) + ",10),1)+eq(mod(Y-" + n(Y0) + ",10),8)))") +
						" [" + t + "M]; "
						"[" + t + "G] [" + t + "M] bplut2=c0='if(y,x,0)'" + STILL_REPEAT + " [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::BUTTON), yuvOf(theme::ACCENT)}));
		fprintf(stderr, "visualizer: Dots in %s: lit #%s, unlit #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::BG).c_str(), tm.ms());
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
						"[" + t + "V] [" + t + "R] bplut2=c0='if(lte(3*mod(y,128),x),1,if(gte(y,128),2,0))'" + STILL_REPEAT + ",split [" + t + "s1] [" + t + "s2]; "
						// the reflection: its first two rows the line (2 in the still), then every other row (1)
						"[" + t + "s2] vflip,scale=" + n(W) + ":" + n(REF) + ":flags=neighbor,setsar=1 [" + t + "F]; " +
						still(W, REF, "if(lt(Y,2),2,mod(Y,2))") + " [" + t + "M]; "
						"[" + t + "F] [" + t + "M] bplut2=c0='if(eq(y,2),3,if(y*eq(x,1),3,0))'" + STILL_REPEAT + " [" + t + "r]; "
						"[" + t + "s1] [" + t + "r] vstack=shortest=0 [" + t + "C]; ";
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
		// each row's step (1..84, 3 levels a step). Alone the steps reach only 75% of the height (row T: the song's
		// lines above, on the ground): there 1..83 up to T, and 84 above it, never lit nor a peak (`lit`, `step`)
		const int T = overCover ? 0 : H / 4;
		const std::string r = overCover ? "1+floor(83*(" + n(H - 1) + "-Y)/" + n(H - 1) + ")"
										: "if(lt(Y," + n(T) + "),84,1+floor(82*(" + n(H - 1) + "-Y)/" + n(H - 1 - T) + "))";
		const std::string lit = overCover ? "lte(3*mod(y,85),x)" : "(lte(3*mod(y,85),x)*lt(mod(y,85),84))";
		const std::string peak = overCover ? "eq(mod(y,85),floor(x/3))" : "eq(mod(y,85),floor(x/3))*lt(mod(y,85),84)";
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, N) +
						"[" + t + "L] split=3 [" + t + "l1] [" + t + "l2] [" + t + "l3]; "
						"[" + t + "l1] " + stretch(W, H, "neighbor") + " [" + t + "V]; "
						// the band on the left's level (the first band's: nothing)
						"[" + t + "l2] pad=" + n(N + 1) + ":1:1:0:black,crop=" + n(N) + ":1:0:0,format=gray," + stretch(W, H, "neighbor") + " [" + t + "Vl]; "
						// the held peaks: down over ~3.4 s for the whole height
						"[" + t + "l3] format=gray16le,lut=c0='65535*exp((val/257-252)/24)',lagfun=decay=0.95,"
						"lut=c0='clip(257*(252+24*log(max(val,1)/65535)),0,257*252)',format=gray," + stretch(W, H, "neighbor") + " [" + t + "P]; " +
						still(W, H, r + "+85*gte(X," + n(BW) + ")*lt(mod(X," + n(BW) + "),2)") + " [" + t + "R]; "
						"[" + t + "Vl] [" + t + "R] bplut2=c0='if(gte(y,85),85+mod(y,85)+85*" + lit + ",y)'" + STILL_REPEAT + " [" + t + "Q]; "
						// 1 the outline, 2 the fill, else the step (as class*85 + step: the peak's lut2 needs the step)
						"[" + t + "V] [" + t + "Q] bplut2=c0='85*if(gt(3*mod(y,85),x-" + n(3 * E) + ")*" + lit + "+gte(y,85)*"
						"(1-eq(" + lit + ",gte(y,170))),1,if(" + lit + ",2,0))+mod(y,85)'" + STILL_REPEAT + " [" + t + "S]; "
						// the peak's step: 3 (DIM) where not the outline
						"[" + t + "P] [" + t + "S] bplut2=c0='if(eq(floor(y/85),1),1,if(gt(x,3)*" + peak + ",3,floor(y/85)))'" + STILL_REPEAT + " [" + t + "C]; ";
		g += paint(t, overCover, false, classes({yuvOf(theme::ACCENT), yuvOf(theme::BUTTON), yuvOf(theme::DIM)}));
		fprintf(stderr, "visualizer: Steps in %s: outline #%s, fill #%s, peaks #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::DIM).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Barcode: the spectrum as 96 full-height hairlines, each lit from faint (BUTTON) to full (ACCENT) by its band.
	// The 96 levels stretched (nearest) along one row, a still of the lines' columns keeps one column a band (the
	// others the ground), the row stretched down the picture (row copies); the strength painted between the two
	std::string barcodeGraph(bool overCover)
	{
		Timer tm;
		const std::string t = "bc";
		const int N = 96, W = 320, H = overCover ? 100 : 240;
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, N) +
						"[" + t + "L] scale=" + n(W) + ":1:flags=neighbor,setsar=1 [" + t + "V]; " +
						still(W, 1, "255*(eq(X,0)+lt(floor((X-1)*" + n(N) + "/" + n(W) + "),floor(X*" + n(N) + "/" + n(W) + ")))") + " [" + t + "M]; "
						"[" + t + "V] [" + t + "M] bplut2=c0='if(y,1+floor(x*250/252),0)'" + STILL_REPEAT + "," + stretch(W, H, "neighbor") + " [" + t + "C]; ";
		g += paint(t, overCover, false, between(yuvOf(theme::BUTTON), yuvOf(theme::ACCENT)));
		fprintf(stderr, "visualizer: Barcode in %s: lines #%s to #%s on #%s; graph built in %.1f ms\n", themeName(),
				hex(theme::BUTTON).c_str(), hex(theme::ACCENT).c_str(), hex(theme::BG).c_str(), tm.ms());
		return g;
	}

	// Rings: ten concentric rings, the bass at the centre and the treble at the rim, each thicker as its band is
	// louder: rings ACCENT, their middle lines (where a quiet band's ring is not lit) BUTTON, the centre dot TEXT.
	// The 10 levels (and a column for the dot) stretched down RG_T rows and told apart by a still of each row's
	// distance out: the rings' picture, 11 x 8, laid round by remap from two still tables (a quarter; the others its
	// mirrors), as Radial was
	std::string ringsGraph(bool overCover, const std::string& dir)
	{
		Timer tm;
		const std::string t = "rg";
		const RingsBox& b = overCover ? RG_STRIP : RG_ALONE;
		const bool ok = writeRings(dir, b);
		const int W = 320, H = overCover ? 100 : 240;
		std::string xm, ym;
		lavfiPath(dir + "/" + b.xf, xm);
		lavfiPath(dir + "/" + b.yf, ym);
		// a row lit when the level passes its share above the spectrum's bed (30)
		std::string g = "[aid1] asplit [ao] [a]; " + (overCover ? artIn(t) : std::string()) + spectrumRow(t, RG_N) +
						"[" + t + "L] pad=" + n(RG_N + 1) + ":1:0:0,format=gray," + stretch(RG_N + 1, RG_T, "neighbor") + " [" + t + "V]; " +
						still(RG_N + 1, RG_T, "if(eq(X," + n(RG_N) + "),255,Y)") + " [" + t + "R]; "
						"[" + t + "V] [" + t + "R] bplut2=c0='if(eq(y,255),3,if(gt(x,30+floor(222*y/" + n(RG_T) + ")),1,if(eq(y,0),2,0)))'" + STILL_REPEAT + " [" + t + "P]; "
						// (remap has no framesync options and needs none: its source [P] drives it; its one-frame maps are
						// held, checked with ffmpeg)
						"movie=" + xm + " [" + t + "xm]; movie=" + ym + " [" + t + "ym]; "
						"[" + t + "P] [" + t + "xm] [" + t + "ym] remap,split [" + t + "q1] [" + t + "q2]; [" + t + "q2] hflip [" + t + "q3]; "
						"[" + t + "q1] [" + t + "q3] hstack,split [" + t + "h1] [" + t + "h2]; [" + t + "h2] vflip [" + t + "h3]; "
						"[" + t + "h1] [" + t + "h3] vstack,pad=" + n(W) + ":" + n(H) + ":" + n((W - b.box) / 2) + ":" + n((H - b.box) / 2) +
						":black,setsar=1 [" + t + "C]; ";
		g += paint(t, overCover, !overCover, classes({yuvOf(theme::ACCENT), yuvOf(theme::BUTTON), yuvOf(theme::TEXT)}));
		fprintf(stderr, "visualizer: Rings in %s: rings #%s, guides #%s, dot #%s on #%s; tables %s, graph built in %.1f ms\n", themeName(),
				hex(theme::ACCENT).c_str(), hex(theme::BUTTON).c_str(), hex(theme::TEXT).c_str(), hex(theme::BG).c_str(),
				ok ? "ready" : "NOT written", tm.ms());
		return g;
	}

	bool writeRingsMaps(const std::string& dir)
	{
		bool ok = writeRings(dir, RG_ALONE);
		return writeRings(dir, RG_STRIP) && ok;
	}
}
