#pragma once
// The native visualizers' sound (docs/notes/2026-11-19-viz-native.md): libmpv hands the app no samples, so a worker
// decodes the song a second time with libavformat/libavcodec (the first audio stream: what the graph's [aid1] plays),
// resampled to mono VN_RATE, and reads it where mpv's audio-pts says the sound heard is (VizClock: told by the main
// thread, run on between mpv's updates). It takes the spectrum there at the visualizer's frame rate (PFFFT), keeps the
// bars' targets for the main thread, and wakes it (a callback that pushes an SDL event). It seeks itself when the
// clock jumps (a seek in mpv), sleeps while nothing moves (paused), and never touches the Player or the Ui.
//
// Each song is a job of its own on a detached thread, let go by stop() without waiting (a slow card never holds the
// main thread); the thread ends at its next look at the stop flag.
#include "viznative.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace bp
{
	class VizTap
	{
	public:
		struct Job;
		struct Stats
		{
			uint64_t spectra = 0, seeks = 0, clockTold = 0;
		};

		// a song's tap: its path (local), the frame rate (30, 45, 60), wake: called from the worker when new targets
		// are ready (coalesced: once until taken)
		void start(const std::string& path, int fps, int vis, std::function<void()> wake);
		void stop();
		bool running() const { return (bool)job; }
		const std::string& path() const { return jobPath; }
		void setFps(int fps);
		void setStyle(int vis);	// a native style's bands (vizStyleBands): the next spectrum is measured for it
		// the clock from mpv (main thread): pts < 0 unknown
		void setClock(const VizClock& c);
		// the newest targets (n of them, measured for style vis; out holds VN_MAX_BARS) and the hint (the loudness, the
		// beats) when newer than seq; seq moves on
		bool take(float* out, uint64_t& seq, int& vis, int& n, VizHint& hint);
		// the file couldn't be opened or decoded: the caller falls back to the graph style
		bool failed() const;
		std::string info() const;	// what is decoded, for the log
		Stats stats() const;

	private:
		std::shared_ptr<Job> job;
		std::string jobPath;
	};
}
