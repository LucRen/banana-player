#pragma once
#include <atomic>
#include <cstdint>
#include <thread>

namespace bp
{
	// The power key with the lid shut (docs/notes/2026-11-17-power-key.md). BananaOS's bananad reads the power key
	// ("rk805 pwrkey", an evdev device, found by name as bananad finds it) and has no setting for it, so while the lid
	// is shut, "When the lid closes: Keep playing" is on and something is loaded here (playing or paused, not
	// stopped), the player takes that device with EVIOCGRAB: a short press is play/pause (main gets an SDL user event,
	// code Press) and a hold of 2 s lets it go and asks bananad for the sleep closing the lid would bring. Any of the
	// three ending drops the grab at once, as does a main loop that stopped beating (frozen, 3 s), quitting, and a
	// crash (the kernel drops a grab when its fd closes). After a hold's sleep it stays off until the wake is seen (a
	// gap in its clock: a grabbed fd would hide the power key from bananad while the handheld sleeps, and that key is
	// how it wakes), when it can be taken again. Bananad holding the device exclusively (EBUSY) leaves it off quietly
	// for that stretch.
	// The thread owns the fd and never touches the Player or the Ui: a press is an SDL user event. Linux only: on a
	// Mac start() does nothing.
	class PowerKey
	{
	public:
		static constexpr int Press = 1;	// the SDL user event's code: a short press (play/pause)
		// event: the SDL user event's type (main's, from SDL_RegisterEvents). False: not started (a Mac, no pipe)
		bool start(uint32_t event);
		void stop();
		~PowerKey() { stop(); }
		// main, each pass: grab while want; why names the condition that ended (read only when want is false: "the lid
		// opened", "nothing is loaded"...). The beat tells the thread this loop still runs
		void tick(bool want, const char* why);

	private:
		void run();
		std::thread thread;
		int stopPipe[2] = {-1, -1};
		std::atomic<bool> wantGrab{false};
		std::atomic<const char*> wantWhy{"nothing is loaded"};
		std::atomic<long long> beat{0};	// CLOCK_MONOTONIC, ms
		std::atomic<bool> quitting{false};
		uint32_t eventType = 0;
	};
}
