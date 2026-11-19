#include "viznative.h"

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

	void vizMirrorTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, float* out)
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
			if (r.y0 < box.y0) out.push_back({r.x0, r.y0, r.x1, box.y0, r.ink});	// above
			if (r.y1 > box.y1) out.push_back({r.x0, box.y1, r.x1, r.y1, r.ink});	// below
			const float y0 = std::max(r.y0, box.y0), y1 = std::min(r.y1, box.y1);
			if (r.x0 < box.x0) out.push_back({r.x0, y0, box.x0, y1, r.ink});	// left
			if (r.x1 > box.x1) out.push_back({box.x1, y0, r.x1, y1, r.ink});	// right
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
}
