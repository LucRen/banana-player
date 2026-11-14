// A slider's arithmetic: the value for a finger's place along a track (clamped, snapped to the slider's steps),
// the value's share of the track for drawing, and how often a costly target is told while the finger moves.
// No window, no SDL: the bottom panel's sliders (Ui::slideGrab, ui_slider.cpp) are built on it, and
// tests/core_test.cpp checks it. docs/notes/sliders.md.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bp
{
	// what a slider goes over: lo at the track's left (or its foot, upright), hi at the other end. step 0: any value
	// between; else lo, lo + step, ... (hi itself too, when the steps don't land on it)
	struct SliderRange
	{
		double lo = 0, hi = 1, step = 0;
	};

	inline double sliderSnap(double v, const SliderRange& r)
	{
		if (r.step > 0) v = r.lo + std::round((v - r.lo) / r.step) * r.step;
		return std::clamp(v, r.lo, r.hi);
	}

	// the value under a finger at `at` (x, or y for an upright one) on a track that starts at `from` and is `len`
	// long. Past either end: that end's value (a finger that slides off the track's end still holds the slider).
	// upright: the top of the track is hi (y grows downwards)
	inline double sliderValueAt(int at, int from, int len, const SliderRange& r, bool upright = false)
	{
		double f = len > 0 ? std::clamp((at - from) / (double)len, 0.0, 1.0) : 0;
		if (upright) f = 1 - f;
		return sliderSnap(r.lo + f * (r.hi - r.lo), r);
	}

	// Where a held slider is, given where it was and where the finger is now: it goes with the finger less `slack`
	// either way. So the small shift of a fingertip as it lands or lifts (under the slack) changes nothing: a tap
	// stays where it touched, a drag ends where it was left; a real move is followed at once, the slack behind.
	inline int sliderFollow(int was, int finger, int slack)
	{
		return std::clamp(was, finger - slack, finger + slack);
	}

	// a value's share of its track, 0..1 (drawing: the fill and the knob)
	inline double sliderShare(double v, const SliderRange& r)
	{
		return r.hi > r.lo ? std::clamp((v - r.lo) / (r.hi - r.lo), 0.0, 1.0) : 0;
	}

	// How often a slider tells its target while it is held. A cheap target (a volume) hears every change; a costly
	// one (a seek in a video, a page to decode) one every `everyMs` at most, and the last value always when the
	// finger lifts (the caller's part: due() is only for the moves).
	struct SliderPace
	{
		uint32_t everyMs = 0, sentAt = 0;
		double sent = 0;
		bool any = false;	// something was sent since the grab

		// the clock starts as the finger lands: a costly target hears nothing for a plain tap but its final value
		void start(uint32_t now, uint32_t every)
		{
			everyMs = every;
			sentAt = now;
			any = false;
		}
		// whether v goes out now; noted as sent when it does
		bool due(double v, uint32_t now)
		{
			if (any && v == sent) return false;
			if (everyMs && now - sentAt < everyMs) return false;
			sent = v;
			sentAt = now;
			any = true;
			return true;
		}
	};
}
