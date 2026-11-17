#pragma once
// Lite, the cheap visualizer: the beat and the bars are worked out from one astats reading (the sound's level), not
// from a picture FFmpeg draws for every frame. This is the measuring part only: no window, no mpv, so the tests can
// drive it (tests/core_test.cpp).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bp
{
	// how many of the meter's last readings a bar may look back through (about 250 ms at 1024 samples and 48 kHz)
	struct LiteMeter
	{
		static constexpr int HISTORY = 64, WINDOW_MS = 1000;
		static constexpr double QUIET = 0.04;	// below this (0..1) the sound is thought to have stopped
		static constexpr uint64_t SILENT_MS = 700;	// this long under it: Lite stops redrawing
		static constexpr double BEAT_RATIO = 1.4;	// louder than this times the last second's average: a beat
		static constexpr uint64_t BEAT_GAP_MS = 120;

		// astats' RMS_level in dB to 0..1: -60 dB or less is 0, 0 dB is 1 (music sits around 0.6-0.8)
		static double normalizeDb(double db)
		{
			if (!std::isfinite(db)) return 0;
			return std::clamp((db + 60) / 60, 0.0, 1.0);
		}

		// the same for a linear amplitude (10^(dB/20)): what the beat is judged by, where 1.4 times really is 1.4
		// times the energy
		static double normalizeAmp(double amp)
		{
			if (!(amp > 0) || !std::isfinite(amp)) return 0;
			return std::clamp((20 * std::log10(amp) + 60) / 60, 0.0, 1.0);
		}

		void reset()
		{
			head = count = 0;
			level = average = 0;
			beats = samples = 0;
			lastLoud = lastBeat = 0;
			silent = true;
			beatNow = false;
		}

		// one astats reading (RMS dB, or peak when there is no RMS) at nowMs (a steady clock, ms)
		void sample(double db, uint64_t nowMs)
		{
			double amp = std::isfinite(db) ? std::min(8.0, std::pow(10, db / 20)) : 0;
			double v = normalizeAmp(amp);
			level = v;
			samples++;
			beatNow = false;
			if (v >= QUIET) lastLoud = nowMs;
			silent = lastLoud == 0 || nowMs - lastLoud > SILENT_MS;
			values[head] = amp;
			times[head] = nowMs;
			head = (head + 1) % HISTORY;
			if (count < HISTORY) count++;
			double sum = 0;
			int n = 0;
			for (int i = 0; i < count; i++)
			{
				int k = (head - 1 - i + HISTORY) % HISTORY;
				if (nowMs - times[k] > WINDOW_MS) break;
				sum += values[k];
				n++;
			}
			average = n ? sum / n : 0;
			// (the first few readings are the average growing; no beat till there's a second's worth to compare with)
			if (n >= 8 && average > 0.002 && amp > BEAT_RATIO * average && (lastBeat == 0 || nowMs - lastBeat >= BEAT_GAP_MS))
			{
				beats++;
				lastBeat = nowMs;
				beatNow = true;
			}
		}

		// the reading `back` samples ago (0: the newest), 0 past what it has
		double recent(int back) const
		{
			if (back < 0 || back >= count) return 0;
			return normalizeAmp(values[(head - 1 - back + HISTORY * 2) % HISTORY]);
		}

		double level = 0, average = 0;
		uint64_t beats = 0, samples = 0;
		bool silent = true, beatNow = false;

	private:
		double values[HISTORY] = {};	// linear amplitudes
		uint64_t times[HISTORY] = {};
		int head = 0, count = 0;
		uint64_t lastLoud = 0, lastBeat = 0;
	};
}
