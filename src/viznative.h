#pragma once
// Native visualizers (docs/notes/2026-11-19-viz-native.md): styles the player measures and draws itself, with no mpv
// lavfi-complex graph (the graph-built styles stalled mpv on the handheld). The sound is decoded a second time on a
// worker (src/viztap.h), its spectrum taken with PFFFT, and the shapes drawn on the top panel in GL (topscreen.cpp).
// Mirror bars is the first. This header is the pure part (bpcore, tested in core_test): the spectrum, the bands, the
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

	// the bands' first bins (bars + 1 edges: band i is bins [e[i], e[i+1])), log spaced from lo to hi Hz (the bass
	// left), each at least one bin wide (the lowest bands are one bin each)
	std::vector<int> vizBandEdges(int bars, int fft, int rate, double lo, double hi);

	// each band's target level 0..VN_TOP from the magnitudes, as the graph style's spectrumRow did it: the band's
	// loudest bin in showfreqs' log scale (-120..0 dB), a slope of +3 dB an octave from 500 Hz (music falls to the
	// right; log bands show it more than the graph's near-linear columns did), its contrast pushed (each bar against
	// its neighbours, Pocket console's), and the bottom 40 steps (the spectrum's near-constant bed) cut off
	void vizMirrorTargets(const float* mag, const std::vector<int>& edges, int rate, int fft, float* out);

	// an LED meter's easing (the graph's lagfun): up at once, down by the whole height over 0.15 s
	float vizEase(float level, float target, double dt);

	// a shape to draw, in 640x480 units: ink 0 ACCENT (the bars), 1 DIM (the centre line)
	struct VizRect
	{
		float x0, y0, x1, y1;
		int ink;
	};
	// Mirror bars: n bars each grown up and down from a centre line by its level (the graph's: 16 units a bar, 12
	// wide; the line 4 tall). band: Art + visualizer's band along the foot (rows 280..480, 94 at most each way);
	// alone the whole panel (the centre at 240, 218 each way)
	void mirrorBarsRects(const float* levels, int n, bool band, std::vector<VizRect>& out);
	// the shapes with a box cut out of them (a card mpv shows over the panel: the bars are drawn first by mpv's
	// order, so they go round it): each shape's parts outside the box, at most four
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
