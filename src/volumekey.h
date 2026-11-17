#pragma once
#include <atomic>
#include <cstdint>
#include <thread>

namespace bp
{
	// The volume keys with the lid shut (docs/redesign/lid-and-incognito.md, "The volume keys: no jump and back").
	// BananaOS's bananad moves the volume itself on a volume key and repeats while it is held, so with the lid shut,
	// "When the lid closes: Keep playing" on and something loaded here, the player takes the volume keys' own evdev
	// device ("gpio-keys-volume", found by name as bananad finds its keys) with EVIOCGRAB: BananaOS never sees them,
	// nothing moves by itself. A press let go before 0.6 s is one volume step (an SDL user event, code VolumeUp or
	// VolumeDown, applied on the main thread through Volume); still down at 0.6 s is Next (VolumeUp) or Previous
	// (VolumeDown), once, and the volume is not touched. A key down 5 s is taken as stuck and ignored until it is let
	// go. Any of the three conditions ending drops the grab at once, as does a main loop that stopped beating
	// (frozen, 3 s), quitting, and a crash (the kernel drops a grab when its fd closes). Bananad holding the device
	// exclusively leaves it off quietly for that stretch.
	// The thread owns the fd and never touches the Player or the Ui: its actions are SDL user events. Linux only: on a
	// Mac start() does nothing.
	class VolumeKey
	{
	public:
		enum Action { VolumeUp = 1, VolumeDown = 2, Next = 3, Previous = 4 };	// the SDL user event's code
		// event: the SDL user event's type (main's, from SDL_RegisterEvents). False: not started (a Mac, no pipe)
		bool start(uint32_t event);
		void stop();
		~VolumeKey() { stop(); }
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
