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

	void vizBandTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, bool plain, float* out)
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
			out[i] = std::clamp((c - 40) * VN_TOP / 212, 0.0f, VN_TOP);
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
			   vis == VIS_HORIZON;
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
	}

	// the held peaks' fall, levels a second: Peaks' caps (lagfun 0.8679 at 30 fps: 3.4 steps a frame, ~2.5 s for the
	// height), Steps' peak line (0.95 at 60: 1.23 a frame, ~3.4 s)
	static const double PEAKS_CAP_FALL = 102, STEPS_PEAK_FALL = 73.9;

	void vizStep(VizStyleState& s, double dt)
	{
		for (int i = 0; i < s.n; i++)
		{
			// Peaks' bars are showfreqs' as measured each frame (its graph eased only the caps)
			s.levels[i] = s.vis == VIS_PEAKS_BANANA ? s.targets[i] : vizEase(s.levels[i], s.targets[i], dt);
			if (s.vis == VIS_PEAKS_BANANA) s.holds[i] = std::min(250.0f, vizHold(s.holds[i], s.levels[i], PEAKS_CAP_FALL, dt));
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
		default: mirrorBarsRects(s.levels, s.n, band, out); break;
		}
	}
}
