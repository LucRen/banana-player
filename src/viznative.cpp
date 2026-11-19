#include "viznative.h"
#include "settings.h"

#include "../third_party/pffft/pffft.h"

#include <algorithm>
#include <cmath>

namespace bp
{
	VizSpectrum::VizSpectrum()
	{
		setup = pffft_new_setup(VN_FFT, PFFFT_REAL);
		buf = (float*)pffft_aligned_malloc(sizeof(float) * VN_FFT);
		work = (float*)pffft_aligned_malloc(sizeof(float) * VN_FFT);
		window.resize(VN_FFT);
		double sq = 0;
		for (int i = 0; i < VN_FFT; i++)
		{
			window[i] = (float)(0.5 - 0.5 * std::cos(2 * M_PI * i / (VN_FFT - 1)));
			sq += (double)window[i] * window[i];
		}
		scale = (float)(1.0 / sq);
	}

	VizSpectrum::~VizSpectrum()
	{
		if (setup) pffft_destroy_setup((PFFFT_Setup*)setup);
		pffft_aligned_free(buf);
		pffft_aligned_free(work);
	}

	void VizSpectrum::magnitudes(const float* in, float* mag)
	{
		if (!setup)
		{
			std::fill(mag, mag + VN_FFT / 2, 0.0f);
			return;
		}
		for (int i = 0; i < VN_FFT; i++) buf[i] = in[i] * window[i];
		pffft_transform_ordered((PFFFT_Setup*)setup, buf, buf, work, PFFFT_FORWARD);
		// ordered real output: [0] DC, [1] Nyquist, then bin k's re, im at 2k, 2k+1
		mag[0] = std::fabs(buf[0]) * scale;
		for (int k = 1; k < VN_FFT / 2; k++) mag[k] = std::sqrt(buf[2 * k] * buf[2 * k] + buf[2 * k + 1] * buf[2 * k + 1]) * scale;
	}

	std::vector<int> vizBandEdges(int bars, int fft, int rate, double lo, double hi)
	{
		std::vector<int> e(bars + 1);
		const double hz = (double)rate / fft, half = fft / 2;
		for (int i = 0; i <= bars; i++)
		{
			int b = (int)std::lround(lo * std::pow(hi / lo, (double)i / bars) / hz);
			if (i > 0) b = std::max(b, e[i - 1] + 1);
			e[i] = std::clamp(b, 1, (int)half);
		}
		return e;
	}

	void vizBandTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, bool plain, float* out, bool cut)
	{
		const int n = (int)edges.size() - 1;
		std::vector<float> l(n);
		const double hz = (double)rate / fft;
		for (int i = 0; i < n; i++)
		{
			float peak = 0;
			for (int k = edges[i]; k < std::max(edges[i + 1], edges[i] + 1) && k < fft / 2; k++) peak = std::max(peak, mag[k]);
			// showfreqs' ascale=log: 1 - log(a)/log(1e-6), a clipped to 1e-6..1; as a level 0..252
			double db = 20 * std::log10(std::clamp((double)peak, 1e-6, 1.0));
			const double mid = (edges[i] + std::max(edges[i + 1], edges[i] + 1)) * 0.5 * hz;
			db += 3 * std::log2(mid / 500);
			l[i] = (float)std::clamp(VN_TOP * (1 + db / 120), 0.0, (double)VN_TOP);
		}
		if (plain)
		{
			std::copy(l.begin(), l.end(), out);
			return;
		}
		for (int i = 0; i < n; i++)
		{
			// vizContrast(t, 1, "4", "1.5", "160"): clip(160 + 1.5 (x - 160) + 4 (x - the 3-wide average), 0, 252)
			float sum = 0;
			int cnt = 0;
			for (int j = std::max(0, i - 1); j <= std::min(n - 1, i + 1); j++) sum += l[j], cnt++;
			float c = std::clamp(160 + 1.5f * (l[i] - 160) + 4 * (l[i] - sum / cnt), 0.0f, VN_TOP);
			// the bed cut: clip((v - 40) * 252 / 212, 0, 252)
			out[i] = cut ? std::clamp((c - 40) * VN_TOP / 212, 0.0f, VN_TOP) : c;
		}
	}

	float vizEase(float level, float target, double dt)
	{
		// the graph's lagfun=decay=0.311 at 60 frames a second on exp((v - 252) / 24): 28 steps a frame, the whole
		// 252 in 0.15 s
		if (target >= level) return target;
		return std::max(target, level - (float)(VN_TOP / 0.15 * std::max(0.0, dt)));
	}

	void vizCutOut(std::vector<VizRect>& rects, const VizRect& box)
	{
		std::vector<VizRect> out;
		out.reserve(rects.size() + 8);
		for (const VizRect& r : rects)
		{
			if (r.x1 <= box.x0 || r.x0 >= box.x1 || r.y1 <= box.y0 || r.y0 >= box.y1)
			{
				out.push_back(r);
				continue;
			}
			auto part = [&](float x0, float y0, float x1, float y1) {
				VizRect q = r;
				q.x0 = x0, q.y0 = y0, q.x1 = x1, q.y1 = y1;
				out.push_back(q);
			};
			if (r.y0 < box.y0) part(r.x0, r.y0, r.x1, box.y0);	// above
			if (r.y1 > box.y1) part(r.x0, box.y1, r.x1, r.y1);	// below
			const float y0 = std::max(r.y0, box.y0), y1 = std::min(r.y1, box.y1);
			if (r.x0 < box.x0) part(r.x0, y0, box.x0, y1);	// left
			if (r.x1 > box.x1) part(box.x1, y0, r.x1, y1);	// right
		}
		rects.swap(out);
	}

	void mirrorBarsRects(const float* levels, int n, bool band, std::vector<VizRect>& out)
	{
		out.clear();
		// the graph's geometry, at its half size doubled: bars 8 half-units apart (2 of them gap), the line the two
		// half-rows at the centre, a bar's half-height in half-rows 1 + floor((level - 1) * MAXD / 250)
		const float centre = band ? 380 : 240, maxd = band ? 46 : 108, pitch = 640.0f / n;
		out.push_back({0, centre - 2, 640, centre + 2, 1});
		for (int i = 0; i < n; i++)
		{
			const float v = std::clamp(levels[i], 0.0f, VN_TOP);
			if (v < 1) continue;
			const float e = 2 * (1 + std::floor((v - 1) * maxd / 250));
			const float x0 = i * pitch + pitch / 8, x1 = (i + 1) * pitch - pitch / 8;
			out.push_back({x0, centre - e, x1, centre + e, 0});
		}
	}

	bool vizNativeStyle(int vis)
	{
		return vis == VIS_MIRROR || vis == VIS_PEAKS_BANANA || vis == VIS_DOTS || vis == VIS_STEPS || vis == VIS_BARCODE || vis == VIS_RINGS ||
			   vis == VIS_HORIZON || vis == VIS_POCKET || vis >= VIS_RIBBON;
	}

	bool vizFullPanel(int vis) { return vis == VIS_SPARKS || vis == VIS_STARFIELD; }

	float vizLoudness(const float* samples, int n)
	{
		double sq = 0;
		for (int i = 0; i < n; i++) sq += (double)samples[i] * samples[i];
		const double rms = n > 0 ? std::sqrt(sq / n) : 0;
		const double db = 20 * std::log10(std::max(rms, 1e-9));
		return (float)std::clamp((db + 42) / 36, 0.0, 1.0);
	}

	float vizBassEnergy(const float* mag, int rate, int fft)
	{
		const double hz = (double)rate / fft;
		float e = 0;
		for (int k = std::max(1, (int)std::lround(40 / hz)); k <= (int)std::lround(160 / hz) && k < fft / 2; k++) e += mag[k] * mag[k];
		return e;
	}

	bool VizBeat::step(float bass, double dt)
	{
		dt = std::max(0.0, dt);
		since += dt;
		if (mean < 0)
		{
			mean = bass;
			return false;
		}
		const bool beat = since >= 0.25 && bass > 1.5 * mean && bass > 1e-4;
		if (beat) since = 0;
		// the mean of about the last second
		mean += (bass - mean) * (1 - std::exp(-dt / 1.0));
		return beat;
	}

	VizBands vizStyleBands(int vis)
	{
		switch (vis)
		{
		case VIS_PEAKS_BANANA: return {64, true};
		case VIS_DOTS: return {32, false};
		case VIS_STEPS:
		case VIS_HORIZON: return {20, false};
		case VIS_BARCODE: return {96, false};
		case VIS_RINGS: return {10, false};
		// Pocket console: its graph's 15 bars, contrast pushed but nothing cut
		case VIS_POCKET: return {15, false, false, 8000};
		case VIS_RIBBON: return {48, false};
		case VIS_GRID: return {VizStyleState::GRID_BANDS, false};
		case VIS_STARFIELD: return {16, false};	// (the loudness only: a few bands for the wake)
		case VIS_STRINGS: return {8, false};
		case VIS_HELIX:
		case VIS_SPARKS: return {32, false};
		default: return {VN_BARS, false};
		}
	}

	float vizHold(float held, float level, double perSecond, double dt)
	{
		return std::max(level, held - (float)(perSecond * std::max(0.0, dt)));
	}

	void VizStyleState::reset(int style)
	{
		vis = style;
		n = vizStyleBands(style).bars;
		std::fill(std::begin(targets), std::end(targets), 0.0f);
		std::fill(std::begin(levels), std::end(levels), 0.0f);
		std::fill(std::begin(holds), std::end(holds), 0.0f);
		std::fill(std::begin(slow), std::end(slow), 0.0f);
		loud = loudShown = 0;
		beatsSeen = beats;
		phase = 0;
		gridCount = gridHead = 0;
		sparkN = 0;
		starV = 0;
		rng = 0x9E3779B9u;
		for (Star& st : stars)
		{
			st.x = rand01() * 2 - 1;
			st.y = rand01() * 2 - 1;
			st.z = 0.05f + rand01() * 0.95f;
		}
	}

	float VizStyleState::rand01()
	{
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (rng >> 8) * (1.0f / 16777216.0f);
	}

	// the held peaks' fall, levels a second: Peaks' caps (lagfun 0.8679 at 30 fps: 3.4 steps a frame, ~2.5 s for the
	// height), Steps' peak line (0.95 at 60: 1.23 a frame, ~3.4 s)
	static const double PEAKS_CAP_FALL = 102, STEPS_PEAK_FALL = 73.9;

	namespace
	{
		// the proposals' easing, told per frame at 30 a second: a step of f toward the target, over dt
		float toward(float v, float target, float f, double k)
		{
			return v + (target - v) * (float)(1 - std::pow(1 - f, k));
		}

		// the new six's moving parts (the proposals' motions, their per-frame numbers at 30 frames a second, k frames on)
		void stepNew(VizStyleState& s, double dt)
		{
			const double k = std::max(0.0, dt) * 30;
			float up = 0.5f, down = 0.14f;
			switch (s.vis)
			{
			case VIS_GRID: down = 0.16f; break;
			case VIS_SPARKS: up = 0.6f, down = 0.18f; break;
			case VIS_STRINGS: up = 0.6f, down = 0.1f; break;
			case VIS_HELIX: up = 0.55f; break;
			default: break;
			}
			for (int i = 0; i < s.n; i++)
				s.levels[i] = toward(s.levels[i], s.targets[i], s.targets[i] > s.levels[i] ? up : down, k);
			switch (s.vis)
			{
			case VIS_RIBBON:
				for (int i = 0; i < s.n; i++) s.slow[i] = toward(s.slow[i], s.levels[i], 0.03f, k);
				break;
			case VIS_GRID:
			{
				// a row every 0.1 s (every third frame at 30), the newest at gridHead
				s.phase += std::max(0.0, dt) * 10;
				if (s.phase > 2) s.phase = 1;	// (a long gap: one row, not a burst)
				while (s.phase >= 1)
				{
					s.phase -= 1;
					s.gridHead = (s.gridHead + 1) % VizStyleState::GRID_ROWS;
					for (int i = 0; i < VizStyleState::GRID_BANDS; i++) s.gridRows[s.gridHead][i] = i < s.n ? s.levels[i] / VN_TOP : 0;
					s.gridCount = std::min(s.gridCount + 1, VizStyleState::GRID_ROWS);
				}
				break;
			}
			case VIS_STARFIELD:
			{
				s.loudShown = toward(s.loudShown, s.loud, 0.25f, k);
				s.starV = 0.003f + 0.03f * s.loudShown * s.loudShown;
				for (VizStyleState::Star& st : s.stars)
				{
					st.z -= s.starV * (float)k;
					if (st.z < 0.05f)
					{
						st.z = 1;
						st.x = s.rand01() * 2 - 1;
						st.y = s.rand01() * 2 - 1;
					}
				}
				break;
			}
			case VIS_STRINGS: s.phase += std::max(0.0, dt) * 27; break;	// (0.9 a frame at 30)
			case VIS_HELIX:
				s.loudShown = toward(s.loudShown, s.loud, 0.2f, k);
				s.phase += (0.02 + 0.07 * s.loudShown) * k;
				break;
			case VIS_SPARKS:
			{
				const float H = 480, W = 640;
				auto add = [&](float px, float v, bool big) {
					if (s.sparkN >= VizStyleState::SPARKS) return;
					VizStyleState::Spark& p = s.sparks[s.sparkN++];
					const float life = 45 + s.rand01() * 55;
					p = {px, 0, (s.rand01() - 0.5f) * 0.9f, H * (0.0035f + 0.0125f * v) * (0.7f + 0.6f * s.rand01()), life, life,
						big ? 4.5f : 1.8f + v * 2.6f, s.rand01() * 6.28f, big};
				};
				for (int i = 0; i < s.n; i++)
				{
					const float v = s.levels[i] / VN_TOP, p = v * v * 0.75f;
					if (s.rand01() < (float)(1 - std::pow(1 - p, k))) add((i + s.rand01()) / s.n * W, v, false);
				}
				// a beat: a brighter handful across the width
				if (s.beats != s.beatsSeen)
				{
					s.beatsSeen = s.beats;
					for (int i = 0; i < 14; i++) add(s.rand01() * W, 0.75f + 0.25f * s.rand01(), true);
				}
				const float drag = (float)std::pow(0.985, k);
				for (int i = s.sparkN - 1; i >= 0; i--)
				{
					VizStyleState::Spark& p = s.sparks[i];
					p.y += p.vy * (float)k;
					p.vy *= drag;
					p.x += (p.vx + std::sin(p.ph + p.life * 0.09f) * 0.5f) * (float)k;
					p.life -= (float)k;
					if (p.life <= 0) p = s.sparks[--s.sparkN];	// (the last in its place: the pool stays packed)
				}
				break;
			}
			default: break;
			}
		}
	}

	void vizStep(VizStyleState& s, double dt)
	{
		if (s.vis >= VIS_RIBBON)
		{
			stepNew(s, dt);
			return;
		}
		for (int i = 0; i < s.n; i++)
		{
			// Peaks' bars are showfreqs' as measured each frame (its graph eased only the caps)
			s.levels[i] = s.vis == VIS_PEAKS_BANANA ? s.targets[i] : vizEase(s.levels[i], s.targets[i], dt);
			if (s.vis == VIS_PEAKS_BANANA || s.vis == VIS_POCKET) s.holds[i] = std::min(250.0f, vizHold(s.holds[i], s.levels[i], PEAKS_CAP_FALL, dt));
			else if (s.vis == VIS_STEPS) s.holds[i] = vizHold(s.holds[i], s.levels[i], STEPS_PEAK_FALL, dt);
		}
	}

	namespace
	{
		// how many half-size rows from the foot a level q lights, for rows told by step(Y) = 1 + floor(K (H-1-Y) /
		// (H-1-T)) for Y >= T (the graphs' still "height of this row" pictures): the rows whose step is at most q
		int rowsUpTo(double q, int H, int T, int K)
		{
			q = std::floor(q);
			if (q < 1) return 0;
			return std::min(H - T, (int)std::ceil(q * (H - 1 - T) / K - 1e-9));
		}

		void box(std::vector<VizRect>& out, float x0, float y0, float x1, float y1, int ink, float mix = 0, float alpha = 1)
		{
			if (x1 > x0 && y1 > y0) out.push_back({x0, y0, x1, y1, ink, mix, alpha, 0});
		}

		void ring(std::vector<VizRect>& out, float cx, float cy, float r0, float r1, int ink)
		{
			out.push_back({cx, cy, std::max(0.0f, r0), r1, ink, 0, 1, 1});
		}

		// Peaks: 64 bars from the foot (8 units, a 2-unit gap), each with a 4-unit cap (TEXT) sitting on its held
		// peak; alone, a faint white line every 96 units over them
		void peaks(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int H = band ? 100 : 240;
			for (int i = 0; i < s.n; i++)
			{
				const float x0 = i * 10.0f, x1 = x0 + 8;
				const int bar = rowsUpTo(s.levels[i], H, 0, 251), cap = rowsUpTo(s.holds[i], H, 0, 251);
				box(out, x0, 480.0f - 2 * bar, x1, 480, VI_ACCENT);
				box(out, x0, 480.0f - 2 * cap - 4, x1, 480.0f - 2 * cap, VI_TEXT);
			}
			if (!band)
				for (int y = 96; y < 480; y += 96) box(out, 0, (float)y, 640, y + 1.0f, VI_WHITE, 0, 0.1f);
		}

		// Dots: 32 columns of round-cornered dots (a 20-unit cell, the dot 16 with its corners cut), lit from the foot
		// (ACCENT) over the unlit ones (BUTTON). Alone 17 rows from 20 above the foot (75% of the height), in the
		// band 9, centred
		void dots(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int S = band ? 9 : 17;
			const float top = band ? 290 : 120;
			for (int c = 0; c < s.n; c++)
				for (int j = 0; j < S; j++)
				{
					const int v = S - 1 - j;
					const bool lit = s.levels[c] >= std::floor(252.0 * (v + 0.5) / S);
					const float x = c * 20.0f, y = top + j * 20.0f;
					const int ink = lit ? VI_ACCENT : VI_BUTTON;
					box(out, x + 2, y + 4, x + 18, y + 16, ink);
					box(out, x + 4, y + 2, x + 16, y + 4, ink);
					box(out, x + 4, y + 16, x + 16, y + 18, ink);
				}
		}

		// Steps: a 20-band staircase (32 units a band), filled (BUTTON), its outline (ACCENT: the top of each step
		// and the rises and falls between bands, at a band's first 4 units) and each band's held peak (DIM). Alone it
		// reaches 75% of the height
		void steps(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int H = band ? 100 : 240, T = band ? 0 : 60, K = band ? 83 : 82, E = band ? 2 : 1;
			const int maxStep = band ? 84 : 83;
			auto q = [&](float v) { return std::min((double)maxStep, std::floor(v / 3.0)); };
			auto y = [&](int rows) { return 480.0f - 2 * rows; };
			std::vector<int> f(s.n);
			for (int i = 0; i < s.n; i++) f[i] = rowsUpTo(q(s.levels[i]), H, T, K);
			for (int i = 0; i < s.n; i++) box(out, i * 32.0f, y(f[i]), i * 32.0f + 32, 480, VI_BUTTON);
			for (int i = 0; i < s.n; i++)
			{
				if (s.holds[i] <= 3) continue;
				const double p = std::floor(s.holds[i] / 3);
				if (p > maxStep) continue;
				box(out, i * 32.0f, y(rowsUpTo(p, H, T, K)), i * 32.0f + 32, y(rowsUpTo(p - 1, H, T, K)), VI_DIM);
			}
			for (int i = 0; i < s.n; i++)
			{
				if (f[i] > 0) box(out, i * 32.0f, y(f[i]), i * 32.0f + 32, y(rowsUpTo(q(s.levels[i]) - E, H, T, K)), VI_ACCENT);
				if (i > 0 && f[i] != f[i - 1]) box(out, i * 32.0f, y(std::max(f[i], f[i - 1])), i * 32.0f + 4, y(std::min(f[i], f[i - 1])), VI_ACCENT);
			}
		}

		// Barcode: 96 hairlines (2 units) the region's height, each from faint (BUTTON) to full (ACCENT) by its band
		void barcode(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const float top = band ? 280 : 0;
			for (int i = 0; i < s.n; i++)
			{
				const int X = (i * 320 + s.n - 1) / s.n;	// the band's first column at half size
				const float mix = std::floor(std::clamp(s.levels[i], 0.0f, VN_TOP) * 250 / 252) / 250;
				box(out, 2.0f * X, top, 2.0f * X + 2, 480, VI_RAMP, mix);
			}
		}

		// Rings: ten rings round a dot (TEXT), the bass innermost; each band's ring ACCENT, thicker as it's louder (8
		// widths), and a quiet one's faint guide (BUTTON). Alone a 480-unit square in the middle, in the band 200
		void rings(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const float cx = 320, cy = band ? 380 : 240, k = 2;	// (half size doubled)
			const double dot = band ? 3 : 6, first = band ? 7 : 16, gap = band ? 4.5 : 10.6, reach = 0.42 * gap, core = 0.75;
			for (int i = 0; i < s.n; i++)
			{
				int m = 0;	// the widths lit: a row lit when the level passes 30 + its share
				for (int my = 0; my < 8; my++)
					if (s.levels[i] > 30 + std::floor(222.0 * my / 8)) m++;
				const double r = first + i * gap, w = m == 0 ? core : m >= 8 ? reach : core + (m - 1) * (reach - core) / 7;
				ring(out, cx, cy, (float)(k * (r - w)), (float)(k * (r + w)), m == 0 ? VI_BUTTON : VI_ACCENT);
			}
			ring(out, cx, cy, 0, (float)(k * dot), VI_TEXT);
		}

		// Horizon: the 20 bands mirrored (the bass in the middle) as a skyline (ACCENT, 16 units a column, two
		// thirds of the sky at most) on a horizon line (DIM, 4 units), a low sun behind it (BUTTON) and, under the
		// line, the skyline's reflection squashed and striped (DIM, every other 2-unit row)
		void horizon(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int H = band ? 100 : 240, SKY = band ? 64 : 160, REF = H - SKY;
			const float top = band ? 280 : 0, line = top + 2 * SKY;
			ring(out, 320, top + 2 * (SKY * 4 / 5), 0, (float)SKY, VI_BUTTON);
			box(out, 0, line, 640, 480, VI_BG);	// (the sun's part under the line)
			int rows[40];
			for (int c = 0; c < 40; c++)
			{
				const int b = c < 20 ? 19 - c : c - 20;
				rows[c] = b < s.n ? rowsUpTo(std::floor(s.levels[b] / 3), SKY, 0, 125) : 0;
				box(out, c * 16.0f, line - 2 * rows[c], c * 16.0f + 16, line, VI_ACCENT);
			}
			box(out, 0, line, 640, line + 4, VI_DIM);
			// the reflection: row Y (from the line) is the sky's floor(Y SKY / REF)-th row up, lit on odd rows from 3
			for (int Y = 3; Y < REF; Y += 2)
			{
				const int up = Y * SKY / REF;
				for (int c = 0; c < 40;)
				{
					if (rows[c] <= up) { c++; continue; }
					int e = c;
					while (e < 40 && rows[e] > up) e++;
					box(out, c * 16.0f, line + 2 * Y, e * 16.0f, line + 2 * Y + 2, VI_DIM);
					c = e;
				}
			}
		}

		// ---- the final ten's new shapes (2026-11-19): quads, lines with joins, discs ----

		void quad(std::vector<VizRect>& out, float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, int ink, float alpha,
			float alpha2)
		{
			if (alpha <= 0 && alpha2 <= 0) return;
			VizRect r{ax, ay, bx, by, ink, 0, alpha, 2};
			r.x2 = cx, r.y2 = cy, r.x3 = dx, r.y3 = dy, r.alpha2 = alpha2;
			out.push_back(r);
		}

		void disc(std::vector<VizRect>& out, float cx, float cy, float r, int ink, float alpha)
		{
			if (r > 0 && alpha > 0) out.push_back({cx, cy, 0, r, ink, 0, alpha, 1});
		}

		// the canvas's smooth curve through the points (a quadratic through each middle point, ending on the mid-points
		// between them), each piece in two: about twice the points
		void smooth(const float* p, int n, std::vector<float>& out)
		{
			out.clear();
			if (n < 3)
			{
				out.assign(p, p + 2 * n);
				return;
			}
			out.push_back(p[0]), out.push_back(p[1]);
			float sx = p[0], sy = p[1];
			for (int i = 1; i < n - 1; i++)
			{
				const float cx = p[2 * i], cy = p[2 * i + 1], ex = (p[2 * i] + p[2 * i + 2]) / 2, ey = (p[2 * i + 1] + p[2 * i + 3]) / 2;
				for (int t = 1; t <= 2; t++)
				{
					const float u = t / 2.0f, a = (1 - u) * (1 - u), b = 2 * (1 - u) * u, c = u * u;
					out.push_back(a * sx + b * cx + c * ex), out.push_back(a * sy + b * cy + c * ey);
				}
				sx = ex, sy = ey;
			}
			out.push_back(p[2 * n - 2]), out.push_back(p[2 * n - 1]);
		}

		// a line w wide through the points: a quad a piece, the pieces meeting on the mitred joins (no overlaps, so a
		// faint glow has no dots at its joins); a sharp turn's mitre kept to twice the width
		void polyline(std::vector<VizRect>& out, const std::vector<float>& p, float w, int ink, float alpha)
		{
			const int n = (int)p.size() / 2;
			if (n < 2 || alpha <= 0) return;
			float pl[2] = {}, pr[2] = {};
			for (int i = 0; i < n; i++)
			{
				// the join's normal: the two pieces' own normals averaged
				float nx = 0, ny = 0;
				auto add = [&](int a, int b) {
					const float dx = p[2 * b] - p[2 * a], dy = p[2 * b + 1] - p[2 * a + 1], l = std::sqrt(dx * dx + dy * dy);
					if (l > 1e-4f) nx += -dy / l, ny += dx / l;
				};
				if (i > 0) add(i - 1, i);
				if (i < n - 1) add(i, i + 1);
				float l = std::sqrt(nx * nx + ny * ny);
				if (l < 1e-4f) nx = 0, ny = 1, l = 1;
				nx /= l, ny /= l;
				float m = w / 2;
				if (i > 0 && i < n - 1)
				{
					// the mitre: half the width over the cosine of half the turn
					const float dx = p[2 * i + 2] - p[2 * i], dy = p[2 * i + 3] - p[2 * i + 1], dl = std::sqrt(dx * dx + dy * dy);
					if (dl > 1e-4f)
					{
						const float c = std::fabs(nx * (-dy / dl) + ny * (dx / dl));
						m = std::min(w, w / 2 / std::max(c, 0.25f));
					}
				}
				const float lx = p[2 * i] + nx * m, ly = p[2 * i + 1] + ny * m, rx = p[2 * i] - nx * m, ry = p[2 * i + 1] - ny * m;
				if (i > 0) quad(out, pl[0], pl[1], lx, ly, rx, ry, pr[0], pr[1], ink, alpha, alpha);
				pl[0] = lx, pl[1] = ly, pr[0] = rx, pr[1] = ry;
			}
		}

		// the area between the curve and the line y = base, alpha at the curve (by height: aTop at `top` rows over the
		// base, 0 at the base when fade) or even (no fade)
		void fillUnder(std::vector<VizRect>& out, const std::vector<float>& p, float base, int ink, float alpha, bool fade, float top = 1)
		{
			auto at = [&](float y) { return fade ? alpha * std::clamp((base - y) / top, 0.0f, 1.0f) : alpha; };
			for (size_t i = 0; i + 3 < p.size(); i += 2)
			{
				const float ax = p[i], ay = p[i + 1], bx = p[i + 2], by = p[i + 3];
				// (each piece's two top corners: its alpha the pair's mean, the fade's foot 0)
				const float a = (at(ay) + at(by)) / 2;
				quad(out, ax, ay, bx, by, bx, base, ax, base, ink, a, fade ? 0 : alpha);
			}
		}

		// the region a style draws in: the band (Art + visualizer) or the whole panel
		void region(bool band, float& y0, float& h)
		{
			y0 = band ? 280.0f : 0.0f;
			h = band ? 200.0f : 480.0f;
		}

		// Ribbon: the 48 bands as one smooth line (ACCENT, a soft glow round it), a fade under it to the base, a slow
		// line behind (DIM: where the song has been sitting) and three level rules and the base (BUTTON)
		void ribbon(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = s.n;
			const float base = y0 + h * 0.88f, top = h * 0.72f;
			for (int i = 0; i < 3; i++)
			{
				const float y = std::round(base - top * (i + 1) / 3);
				box(out, 0, y, 640, y + 2, VI_BUTTON);
			}
			box(out, 0, std::round(base), 640, std::round(base) + 2, VI_BUTTON);
			float p[2 * VN_MAX_BARS], q[2 * VN_MAX_BARS];
			for (int i = 0; i < n; i++)
			{
				const float x = n > 1 ? i * 640.0f / (n - 1) : 0;
				p[2 * i] = q[2 * i] = x;
				p[2 * i + 1] = base - s.levels[i] / VN_TOP * top;
				q[2 * i + 1] = base - s.slow[i] / VN_TOP * top * 1.25f;
			}
			std::vector<float> line, slow;
			smooth(p, n, line);
			smooth(q, n, slow);
			fillUnder(out, line, base, VI_ACCENT, 0.34f, true, top);
			polyline(out, slow, 2, VI_DIM, 0.9f);
			polyline(out, line, 18, VI_ACCENT, 0.07f);
			polyline(out, line, 10, VI_ACCENT, 0.14f);
			polyline(out, line, 3.5f, VI_ACCENT, 1);
		}

		// Grid: a floor in perspective, the front ridge now (bass in the middle) and one a tenth of a second older each
		// row back, rolling to the horizon and fading (ACCENT); each ridge hides what's behind it (BG); the floor's lines
		// BUTTON
		void grid(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const float cx = 320, hy = y0 + h * 0.2f, yb = y0 + h * 0.95f, NW = 640 * 0.74f, amp = h * 0.5f;
			auto sc = [](float d) { return 1 / (1 + d * 2.8f); };
			auto fy = [&](float d) { return hy + (yb - hy) * sc(d); };
			std::vector<float> ln;
			for (int i = -4; i <= 4; i++)
			{
				const float u = i / 4.0f;
				ln = {cx + u * NW * 1.4f, hy + (yb - hy) * 1.4f, cx + u * NW * sc(1), fy(1)};
				polyline(out, ln, 2, VI_BUTTON, 1);
			}
			box(out, 0, fy(1) - 1, 640, fy(1) + 1, VI_BUTTON);
			const int R = VizStyleState::GRID_ROWS - 1;
			const float frac = (float)s.phase;
			float p[2 * 33];
			std::vector<float> pts, sm;
			for (int j = s.gridCount; j >= 0; j--)
			{
				const float d = j ? (j - 1 + frac) / R : 0;
				if (d >= 1) continue;
				const float* a = j ? s.gridRows[(s.gridHead - (j - 1) + VizStyleState::GRID_ROWS) % VizStyleState::GRID_ROWS] : nullptr;
				const float k = sc(d), by = fy(d);
				for (int i = -16; i <= 16; i++)
				{
					const int b = std::abs(i);
					const float v = a ? a[b] : (b < s.n ? s.levels[b] / VN_TOP : 0);
					p[2 * (i + 16)] = cx + i / 16.0f * NW * k;
					p[2 * (i + 16) + 1] = by - v * amp * k;
				}
				// (the front ridge smoothed; the rows behind are small enough as they are)
				if (j == 0) smooth(p, 33, pts);
				else pts.assign(p, p + 66);
				fillUnder(out, pts, by + 1, VI_BG, 1, false);
				polyline(out, pts, j ? 1 + 1.2f * k : 3, VI_ACCENT, std::pow(1 - d, 1.4f));
			}
		}

		// Starfield: 160 stars streaming out of the middle, faster as the song is louder, each a streak from where it
		// was to where it is (ACCENT; one in six TEXT, wider), over a glow at the centre (BUTTON) that swells with it.
		// Always the whole panel
		void starfield(const VizStyleState& s, std::vector<VizRect>& out)
		{
			const float cx = 320, cy = 240, f = 640 * 0.3f;
			disc(out, cx, cy, 16 + 34 * s.loudShown, VI_BUTTON, 0.9f);
			std::vector<float> seg(4);
			for (int i = 0; i < VizStyleState::STARS; i++)
			{
				const VizStyleState::Star& p = s.stars[i];
				const bool hot = i % 6 == 0;
				const float z0 = p.z + s.starV * 2.2f, near = 1 - p.z;
				const float alpha = std::clamp(0.15f + near * 1.1f, 0.0f, 1.0f) * std::clamp((1 - p.z) * 8, 0.0f, 1.0f);
				const float w = 1 + near * (hot ? 4 : 3);
				const float ax = cx + p.x / z0 * f, ay = cy + p.y / z0 * f, bx = cx + p.x / p.z * f, by = cy + p.y / p.z * f;
				if ((ax < -20 && bx < -20) || (ax > 660 && bx > 660) || (ay < -20 && by < -20) || (ay > 500 && by > 500)) continue;
				const float dx = bx - ax, dy = by - ay, l = std::sqrt(dx * dx + dy * dy);
				if (l < w * 0.5f)
				{
					disc(out, bx, by, w / 2, hot ? VI_TEXT : VI_ACCENT, alpha);	// (slow: a point)
					continue;
				}
				// the canvas's round caps: half the width on at each end
				const float ex = dx / l * w / 2, ey = dy / l * w / 2;
				seg = {ax - ex, ay - ey, bx + ex, by + ey};
				polyline(out, seg, w, hot ? VI_TEXT : VI_ACCENT, alpha);
			}
		}

		// Strings: eight strings (five in the band), the thick bass one at the foot, each swinging as wide as its band is
		// loud on its own harmonic (ACCENT), the blur of its swing behind (BUTTON), two end bars (DIM)
		void strings(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = band ? 5 : std::min(8, s.n);
			float lv[8];
			for (int i = 0; i < n; i++)
			{
				// the band's five: each the loudest of the eight it covers
				const int a = i * s.n / n, b = std::max(a + 1, (i + 1) * s.n / n);
				lv[i] = 0;
				for (int j = a; j < b && j < s.n; j++) lv[i] = std::max(lv[i], s.levels[j] / VN_TOP);
			}
			const float m = 26, x0 = m, x1 = 640 - m, top = y0 + (band ? 14 : 62), bot = y0 + h - 14, gap = (bot - top) / std::max(1, n);
			const int S = 40;
			std::vector<float> line(2 * (S + 1));
			for (int i = 0; i < n; i++)
			{
				const float yc = bot - gap * (i + 0.5f), A = gap * 0.47f * std::pow(lv[i], 0.8f);
				const int hw = i + 1;
				const float sw = (float)std::cos(s.phase * (0.8 + i * 0.13) + i);
				for (int j = 0; j < S; j++)
				{
					const float xa = x0 + (x1 - x0) * j / S, xb = x0 + (x1 - x0) * (j + 1) / S;
					const float ea = A * (float)std::sin(M_PI * hw * j / S), eb = A * (float)std::sin(M_PI * hw * (j + 1) / S);
					quad(out, xa, yc - ea, xb, yc - eb, xb, yc + eb, xa, yc + ea, VI_BUTTON, 1, 1);
				}
				for (int j = 0; j <= S; j++)
				{
					line[2 * j] = x0 + (x1 - x0) * j / S;
					line[2 * j + 1] = yc + A * sw * (float)std::sin(M_PI * hw * j / S);
				}
				polyline(out, line, 1.5f + (n - i) * 0.45f, VI_ACCENT, 1);
			}
			box(out, x0 - 5, top, x0 - 1, bot, VI_DIM);
			box(out, x1 + 1, top, x1 + 5, bot, VI_DIM);
		}

		// Helix: two strands of beads twisting round each other, bass at the left: a bead swells with its band, the twist
		// turns faster as the song is louder; the front strand ACCENT, the other DIM, rungs between them BUTTON
		void helix(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			float y0, h;
			region(band, y0, h);
			const int n = s.n;
			const float cy = y0 + h / 2 + (band ? 0 : 10), A = h * (band ? 0.34f : 0.26f) * (0.8f + 0.2f * s.loudShown), k = band ? 0.75f : 1;
			float px[VN_MAX_BARS], sn[VN_MAX_BARS], cs[VN_MAX_BARS];
			for (int i = 0; i < n; i++)
			{
				px[i] = (i + 0.5f) / n * 640;
				const double phi = (double)i / n * M_PI * 3.2 - s.phase;
				sn[i] = (float)std::sin(phi), cs[i] = (float)std::cos(phi);
				const float e = A * std::fabs(sn[i]);
				box(out, px[i] - 1, cy - e, px[i] + 1, cy + e, VI_BUTTON);
			}
			for (int front = 0; front < 2; front++)
				for (int i = 0; i < n; i++)
					for (int sgn = 1; sgn >= -1; sgn -= 2)
					{
						const float d = cs[i] * sgn;
						if ((d >= 0) != (front == 1)) continue;
						const float v = s.levels[i] / VN_TOP;
						disc(out, px[i], cy + A * sn[i] * sgn, (2.5f + 9 * v) * (0.7f + 0.3f * d) * k, sgn > 0 ? VI_ACCENT : VI_DIM,
							0.6f + 0.4f * (d * 0.5f + 0.5f));
					}
		}

		// Sparks: lights lifting off the foot where the music is (ACCENT; a beat's brighter handful TEXT), over the ground
		// swell (BUTTON). Always the whole panel, the swell at the foot
		void sparks(const VizStyleState& s, std::vector<VizRect>& out)
		{
			const float base = 480, H = 480;
			float p[2 * VN_MAX_BARS];
			for (int i = 0; i < s.n; i++)
			{
				p[2 * i] = (i + 0.5f) / s.n * 640;
				p[2 * i + 1] = base - 6 - s.levels[i] / VN_TOP * H * 0.16f;
			}
			if (s.n > 0) p[0] = 0, p[2 * s.n - 2] = 640;
			std::vector<float> sm;
			smooth(p, s.n, sm);
			fillUnder(out, sm, base, VI_BUTTON, 1, false);
			for (int i = 0; i < s.sparkN; i++)
			{
				const VizStyleState::Spark& q = s.sparks[i];
				const float f = q.life / q.max;
				disc(out, q.x, base - 6 - q.y, q.r * (0.5f + 0.5f * f), q.big ? VI_TEXT : VI_ACCENT, std::min(1.0f, f * 1.6f));
			}
		}

		// Pocket console: the graph's LCD (Peaks on an old handheld's screen) as shapes: 15 bars of 4-dot columns, the
		// lower two thirds of a lit bar the ink, its top third and the caps and floor mid; dots of 8 units with a 1-unit
		// grid line. Alone, on the screen's colour with the grid over it all; in the band, the screen is the ground (the
		// art's band) and the lit dots the bright pair, the grid cut through them in BG
		void pocket(const VizStyleState& s, bool band, std::vector<VizRect>& out)
		{
			const int N = 15, BW = 5, L = 2, ROWS = band ? 25 : 60, T = band ? 2 : 14, BH = band ? 22 : 45;
			const float top = band ? 280 : 0;
			const int cap = (251 + BH / 2) / BH;
			const int dark = band ? VI_P_LIGHT : VI_P_INK, mid = band ? VI_P_BRIGHT : VI_P_MID;
			if (!band) box(out, 0, 0, 640, 480, VI_P_SCREEN);
			auto rowY = [&](int Y) { return top + Y * 8.0f; };
			for (int b = 0; b < N && b < s.n; b++)
			{
				const float x0 = (L + b * BW) * 8.0f, x1 = x0 + (BW - 1) * 8.0f;
				const float lv = std::clamp(s.levels[b], 0.0f, VN_TOP), pk = std::clamp(s.holds[b], 0.0f, VN_TOP);
				// k rows up from the bar's foot: step 1 + floor(251 k / (BH - 1)); lit while the step is at most the level
				int lit = 0, low = 0, c0 = -1, c1 = -1;
				for (int k = 0; k < BH; k++)
				{
					const int st = 1 + 251 * k / (BH - 1);
					if (st <= lv)
					{
						lit = k + 1;
						if (st <= lv * 0.66f) low = k + 1;
					}
					else if (st > pk && st <= pk + cap)
					{
						if (c0 < 0) c0 = k;
						c1 = k + 1;
					}
				}
				const int foot = T + BH;	// (the row under the bars' last)
				box(out, x0, rowY(foot - low), x1, rowY(foot), dark);
				box(out, x0, rowY(foot - lit), x1, rowY(foot - low), mid);
				if (c0 >= 0) box(out, x0, rowY(foot - c1), x1, rowY(foot - c0), mid);
			}
			// the floor: the screen's last row under the bars
			box(out, L * 8.0f, rowY(T + BH), (L + N * BW) * 8.0f, rowY(T + BH + 1), mid);
			// the grid: the 8th unit of every dot, across and down
			const int g = band ? VI_BG : VI_P_GRID;
			for (int c = 0; c < 80; c++) box(out, c * 8 + 7.0f, top, c * 8 + 8.0f, top + ROWS * 8.0f, g);
			for (int r = 0; r < ROWS; r++) box(out, 0, rowY(r) + 7, 640, rowY(r) + 8, g);
		}
	}

	void vizShapes(const VizStyleState& s, bool band, std::vector<VizRect>& out)
	{
		out.clear();
		switch (s.vis)
		{
		case VIS_PEAKS_BANANA: peaks(s, band, out); break;
		case VIS_DOTS: dots(s, band, out); break;
		case VIS_STEPS: steps(s, band, out); break;
		case VIS_BARCODE: barcode(s, band, out); break;
		case VIS_RINGS: rings(s, band, out); break;
		case VIS_HORIZON: horizon(s, band, out); break;
		case VIS_POCKET: pocket(s, band, out); break;
		case VIS_RIBBON: ribbon(s, band, out); break;
		case VIS_GRID: grid(s, band, out); break;
		case VIS_STARFIELD: starfield(s, out); break;
		case VIS_STRINGS: strings(s, band, out); break;
		case VIS_HELIX: helix(s, band, out); break;
		case VIS_SPARKS: sparks(s, out); break;
		default: mirrorBarsRects(s.levels, s.n, band, out); break;
		}
	}
}
