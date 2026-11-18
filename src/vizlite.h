#pragma once
// Lite, the cheap visualizer: the beat and the bars are worked out from the tap's astats readings (the sound's level
// and its bass), not from a picture FFmpeg draws for every frame. This is the measuring part only: no window, no mpv,
// so the tests can drive it (tests/core_test.cpp).
//
// Two things about the tap shape what is here. mpv reports af-metadata only as often as it drains the audio chain
// (about 18 times a second on the handheld, whatever the tap's frame size), so a reading is a slice of the sound and
// the next may be 55 ms later; nothing here may depend on a reading arriving at a video frame's rate, and the picture
// interpolates between two readings. And the tap filters ahead of the speakers by mpv's sound buffer, so the newest
// reading is not what is heard: the meters keep a short queue and the player shows the reading that is audible now.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bp
{
	// One signal's automatic gain: astats' RMS_level (dB) to 0..1 against a floor and a ceiling that follow the song
	// over a few seconds. The ceiling jumps up at a new peak and comes back down slowly; the floor drops fast at a
	// quiet part and climbs slowly. A quiet and a loud song therefore both fill the meter, and a steady tone settles in
	// the middle. Peak_level was the old level and sat near full scale on most music: that is why Lite looked weak.
	struct LiteGain
	{
		static constexpr double MIN_SPAN_DB = 18;	// the narrowest floor-to-ceiling span the meter uses
		static constexpr double CEIL_RISE_S = 0.12;	// a new peak: the ceiling follows at once
		static constexpr double CEIL_FALL_S = 4.5;	// then it comes down over a verse
		static constexpr double FLOOR_FALL_S = 0.30;	// a quieter part: the floor drops fast
		static constexpr double FLOOR_RISE_S = 4.5;	// and climbs again slowly
		static constexpr double KNEE = 0.65;		// a little compression above this:
		static constexpr double QUIET_DB = -70;		// anything under this is silence, not a reading

		void reset()
		{
			floorDb = ceilDb = 0;
			primed = false;
		}

		// one reading (dBFS) and the seconds since the one before; 0..1
		double map(double db, double dt)
		{
			if (!std::isfinite(db)) db = QUIET_DB - 20;
			db = std::clamp(db, QUIET_DB - 20, 0.0);
			dt = std::clamp(dt, 0.0, 0.5);
			if (!primed)
			{
				// the first reading is the middle of the span: a song's first note has no history to stand on
				floorDb = ceilDb = db;
				primed = true;
			}
			else
			{
				auto glide = [dt](double cur, double target, double tau) {
					return cur + (target - cur) * (1 - std::exp(-dt / tau));
				};
				ceilDb = db >= ceilDb ? glide(ceilDb, db, CEIL_RISE_S) : glide(ceilDb, db, CEIL_FALL_S);
				floorDb = db <= floorDb ? glide(floorDb, db, FLOOR_FALL_S) : glide(floorDb, db, FLOOR_RISE_S);
				if (ceilDb < floorDb) ceilDb = floorDb;
			}
			double lo = floorDb, hi = ceilDb;
			if (hi - lo < MIN_SPAN_DB)
			{
				// a steady sound has no peak to put at the top: centre it in the narrowest span
				double mid = (lo + hi) * 0.5;
				lo = mid - MIN_SPAN_DB * 0.5;
				hi = mid + MIN_SPAN_DB * 0.5;
			}
			double v = std::clamp((db - lo) / (hi - lo), 0.0, 1.0);
			// a little compression above the knee: one loud transient shouldn't flatten the body of the song below it
			if (v > KNEE) v = KNEE + (1 - KNEE) * std::tanh((v - KNEE) / (1 - KNEE) * 0.6) / std::tanh(0.6);
			return std::clamp(v, 0.0, 1.0);
		}

		double floorDb = 0, ceilDb = 0;
		bool primed = false;
	};

	// One signal's readings, its beat and its level. The whole sound and the bass each have one.
	struct LiteMeter
	{
		static constexpr int HISTORY = 192;	// readings kept: 4 s at 45/s, longer when mpv reports more slowly
		static constexpr double QUIET_DB = -60;	// under this (dBFS) the sound is thought to have stopped
		static constexpr uint64_t SILENT_MS = 700;	// this long under it: Lite stops redrawing
		static constexpr double BEAT_RATIO = 1.4;	// bass energy this much over its EMA: a beat
		static constexpr double BEAT_EMA_S = 1.0;	// the bass's ~1 s average, an EMA (no 1 s window to reset)
		static constexpr uint64_t BEAT_GAP_MS = 120;	// at most one beat this often

		void reset()
		{
			head = count = 0;
			level = 0;
			beats = samples = 0;
			lastLoud = lastBeat = lastAt = 0;
			ema = 0;
			silent = true;
			beatNow = false;
			gain.reset();
		}

		// one reading (RMS_level in dBFS) at nowMs (a steady clock, ms)
		void sample(double db, uint64_t nowMs)
		{
			double dt = samples ? std::clamp((nowMs - lastAt) / 1000.0, 0.0, 0.5) : 0.0;
			lastAt = nowMs;
			level = gain.map(db, dt);
			beatNow = false;
			if (std::isfinite(db) && db >= QUIET_DB) lastLoud = nowMs;
			silent = lastLoud == 0 || nowMs - lastLoud > SILENT_MS;
			// the beat: bass energy over 1.4 times its own slowly moving average (amplitude, where 1.4 really is
			// 1.4), at most one each 120 ms. A one-second box average used to make the trigger's sensitivity move
			// with the window's edge; an EMA has no edge and no periodic drop
			double amp = std::isfinite(db) ? std::min(8.0, std::pow(10.0, db / 20)) : 0;
			if (samples == 0) ema = amp;
			bool beat = amp > 0.002 && amp > BEAT_RATIO * ema && (lastBeat == 0 || nowMs - lastBeat >= BEAT_GAP_MS);
			ema += (amp - ema) * (1 - std::exp(-dt / BEAT_EMA_S));
			if (beat && !silent)
			{
				beats++;
				lastBeat = nowMs;
				beatNow = true;
			}
			values[head] = level;
			times[head] = nowMs;
			serials[head] = beats;
			head = (head + 1) % HISTORY;
			if (count < HISTORY) count++;
			samples++;
		}

		// the display value at a time in the readings' clock: interpolated between the two readings around it, so a
		// picture at 45 or 60 fps moves smoothly even when mpv reports 18 times a second. 0 before the first reading
		double at(uint64_t whenMs) const
		{
			int k = indexAt(whenMs);
			if (k < 0) return 0;
			int n = (k + 1) % HISTORY;
			// (n is the reading after k; head is the next slot, so n == head means k is the newest)
			if (n != head && count > 1 && times[n] > times[k] && whenMs > times[k])
			{
				double f = (double)(whenMs - times[k]) / (double)(times[n] - times[k]);
				return values[k] + (values[n] - values[k]) * std::clamp(f, 0.0, 1.0);
			}
			return values[k];
		}

		// how many beats had been seen by whenMs: the glow counts beats at the time that is audible, not at arrival
		uint64_t beatsAt(uint64_t whenMs) const
		{
			int k = indexAt(whenMs);
			return k < 0 ? 0 : serials[k];
		}

		double level = 0;	// the newest display value
		double gainFloorDb() const { return gain.floorDb; }
		double gainCeilDb() const { return gain.ceilDb; }
		uint64_t beats = 0, samples = 0;
		bool silent = true, beatNow = false;
		LiteGain gain;

	private:
		// the slot of the newest reading at or before t, -1 when t is before the history
		int indexAt(uint64_t t) const
		{
			if (!count) return -1;
			int k = (head - 1 + HISTORY) % HISTORY;
			for (int i = 0; i < count; i++)
			{
				if (times[k] <= t) return k;
				k = (k - 1 + HISTORY) % HISTORY;
			}
			return -1;
		}

		double values[HISTORY] = {};	// display values, 0..1
		uint64_t times[HISTORY] = {};
		uint64_t serials[HISTORY] = {};	// beats seen by that reading
		int head = 0, count = 0;
		uint64_t lastLoud = 0, lastBeat = 0, lastAt = 0;
		double ema = 0;	// the bass's linear amplitude average
	};
}
