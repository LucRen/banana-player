#pragma once
// Native visualizers (docs/notes/2026-11-19-viz-native.md): styles the player measures and draws itself, with no mpv
// lavfi-complex graph (the graph-built styles stalled mpv on the handheld). The sound is decoded a second time on a
// worker (src/viztap.h), its spectrum taken with PFFFT, and the shapes drawn on the top panel in GL (topscreen.cpp).
// Mirror bars was the first; Peaks, Dots, Steps, Barcode, Rings and Horizon followed. This header is the pure part (bpcore, tested in core_test): the spectrum, the bands, the
// levels as the graph style measured them, the easing, the shapes in 640x480 units, the clock.
#include <cstdint>
#include <vector>

namespace bp
{
	constexpr int VN_RATE = 32000;	// the copy's sample rate, mono: 31.25 Hz a bin, up to 16 kHz
	constexpr int VN_FFT = 1024;	// 32 ms at VN_RATE
	constexpr int VN_BARS = 40;	// Mirror bars' bars
	constexpr float VN_TOP = 252;	// a level's full height (the graph's codes: 0..252)

	// a 1024-point real FFT (PFFFT: NEON on aarch64, SSE on x86) with a Hann window. Not thread-safe: one per worker
	class VizSpectrum
	{
	public:
		VizSpectrum();
		~VizSpectrum();
		VizSpectrum(const VizSpectrum&) = delete;
		VizSpectrum& operator=(const VizSpectrum&) = delete;
		bool ok() const { return setup != nullptr; }
		// in: VN_FFT samples; mag: VN_FFT / 2 amplitudes (bin 0 DC .. 511), each |X| over the window's sum of squares,
		// as FFmpeg's showfreqs measures (the graph style's numbers: a full-scale sine is about 0.67)
		void magnitudes(const float* in, float* mag);

	private:
		void* setup = nullptr;
		float* buf = nullptr;
		float* work = nullptr;
		std::vector<float> window;
		float scale = 1;
	};

	constexpr int VN_MAX_BARS = 96;	// the most bands a style measures (Barcode)

	// the bands' first bins (bars + 1 edges: band i is bins [e[i], e[i+1])), log spaced from lo to hi Hz (the bass
	// left), each at least one bin wide (the lowest bands are one bin each)
	std::vector<int> vizBandEdges(int bars, int fft, int rate, double lo, double hi);

	// The native styles (the graph styles they replace: src/viz_simple.cpp, Peaks' peaksGraph in settings.cpp)
	bool vizNativeStyle(int vis);
	// what a style measures: its band count, and plain (Peaks: showfreqs' levels as they are) or the simple styles'
	// spectrumRow (contrast pushed, the bed cut)
	struct VizBands
	{
		int bars = 40;
		bool plain = false;
	};
	VizBands vizStyleBands(int vis);

	// each band's target level 0..VN_TOP from the magnitudes, as the graph styles' spectrumRow did it: the band's
	// loudest bin in showfreqs' log scale (-120..0 dB), a slope of +3 dB an octave from 500 Hz (music falls to the
	// right; log bands show it more than the graph's near-linear columns did); unless plain, its contrast pushed (each
	// bar against its neighbours, Pocket console's) and the bottom 40 steps (the spectrum's near-constant bed) cut off
	void vizBandTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, bool plain, float* out);
	inline void vizMirrorTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, float* out)
	{
		vizBandTargets(mag, edges, rate, fft, false, out);
	}

	// an LED meter's easing (the graph's lagfun): up at once, down by the whole height over 0.15 s
	float vizEase(float level, float target, double dt);
	// a held peak let fall at an even speed (levels a second): up at once with the level
	float vizHold(float held, float level, double perSecond, double dt);

	// a style's moving state on the top panel: the levels shown and the held peaks (Peaks' caps, Steps' peak line)
	struct VizStyleState
	{
		int vis = -1, n = 0;
		float targets[VN_MAX_BARS] = {}, levels[VN_MAX_BARS] = {}, holds[VN_MAX_BARS] = {};
		void reset(int style);
	};
	// a frame on: the levels eased (Peaks: as measured, its bars aren't eased) and the holds let fall
	void vizStep(VizStyleState& s, double dt);

	// a shape to draw, in 640x480 units. kind 0 a box (x0,y0)-(x1,y1); kind 1 a ring round (x0,y0) from radius x1 to
	// y1 (0: a disc). ink: VizInk; mix: VI_RAMP's place from BUTTON (0) to ACCENT (1); alpha: 1 opaque
	enum VizInk { VI_ACCENT, VI_DIM, VI_BUTTON, VI_TEXT, VI_BG, VI_RAMP, VI_WHITE };
	struct VizRect
	{
		float x0, y0, x1, y1;
		int ink;
		float mix = 0, alpha = 1;
		int kind = 0;
	};
	// Mirror bars: n bars each grown up and down from a centre line by its level (the graph's: 16 units a bar, 12
	// wide; the line 4 tall). band: Art + visualizer's band along the foot (rows 280..480, 94 at most each way);
	// alone the whole panel (the centre at 240, 218 each way)
	void mirrorBarsRects(const float* levels, int n, bool band, std::vector<VizRect>& out);
	// any native style's shapes from its state (Mirror bars as above; the others as their graphs drew them, at their
	// half size doubled), in drawing order
	void vizShapes(const VizStyleState& s, bool band, std::vector<VizRect>& out);
	// the boxes with a box cut out of them: each box's parts outside it, at most four (the panel's free parts round
	// the song's lines and mpv's cards, which the shapes are kept off: they're drawn after mpv's picture)
	void vizCutOut(std::vector<VizRect>& rects, const VizRect& box);

	// where the sound heard is, from mpv's audio-pts (its output's delay already taken off) and when it came: between
	// mpv's updates (its playloop's ticks) the time runs on at the speed
	struct VizClock
	{
		double pts = -1;	// seconds in the file; < 0 unknown
		uint64_t atNs = 0;	// steady clock, when pts was told
		double speed = 1;
		bool moving = false;	// playing (not paused, seeking or waiting on the cache)
		double at(uint64_t nowNs) const
		{
			if (pts < 0) return -1;
			if (!moving || nowNs <= atNs) return pts;
			return pts + (double)(nowNs - atNs) / 1e9 * speed;
		}
	};
}
