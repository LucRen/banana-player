#pragma once
// The album-art placeholder's sums (docs/redesign/components.md, "Art placeholder"): a flat record in its sleeve.
// Pure, so the bottom panel (Ui::Kit::artPlaceholder), the top panel's drawing (player.cpp) and the tests share them;
// the dashboard repeats the same sum in JS (web/js/components.js).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace artph
{
	// FNV-1a, 32 bits, over the key's UTF-8 bytes
	inline uint32_t hash(const std::string& key)
	{
		uint32_t h = 2166136261u;
		for (unsigned char c : key) h = (h ^ c) * 16777619u;
		return h;
	}

	// a diameter rounded to an even number of units (at least 2), so a centre on a whole unit has whole edges
	inline int even(double x) { return std::max(2, 2 * (int)std::lround(x / 2)); }

	struct Shape
	{
		int variant = 0;	// 0 whole, centred; 1 half out of the bottom; 2 half out of the right; 3 half out of the top
		int cx = 0, cy = 0;	// the discs' shared centre, from the square's top left
		int record = 0, band = 0, label = 0, hole = 0;	// diameters; band 0: none (a small square)
	};

	// the square of side s, keyed by the album's name (else the title, else the file's name)
	inline Shape shape(int s, const std::string& key)
	{
		Shape r;
		const bool big = s >= 64;
		r.variant = big ? (int)(hash(key) >> 30) : 0;
		r.record = even(s * (r.variant ? 0.88 : 0.72));
		r.band = big ? even(r.record * 0.62) : 0;
		r.label = even(r.record * 0.34);
		r.hole = even(r.record * 0.07);
		const int px[4] = {s / 2, s / 2, s, s / 2}, py[4] = {s / 2, s, s / 2, 0};
		r.cx = px[r.variant];
		r.cy = py[r.variant];
		return r;
	}
}
