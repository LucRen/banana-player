#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	// Pauses before the handheld sleeps and keeps it paused after. bananad sends the player's process group SIGSTOP
	// when the lid closes (or power is pressed) and SIGCONT when it opens: on its own mpv would play on from there.
	//   - the lid closing (SW_LID, read from evdev as bananad does): pause at once. This is also the whole of it when
	//     lid sleep is off and closing the lid only turns the backlights off.
	//   - SIGCONT, after any sleep: pause, in case bananad stopped us before the lid's event was read.
	// Opening the lid does nothing: play is the player's to press.
	// Both callbacks run on the watch's own thread: onSleep(lid) as the lid closes (lid true: at once, before bananad
	// stops the process, best effort) and on SIGCONT (lid false); onAwake after a SIGCONT, and 1.5 s after the lid
	// closed if no SIGCONT came by then (lid sleep off: the process was never stopped)
	//
	// It also sees the sound's way out go (onUnplug, on the watch's thread, with what went): the headphone jack's
	// switch (SW_HEADPHONE_INSERT 1 -> 0, on whichever input device has it), BananaOS's /run/banana/bt/audio going
	// (Bluetooth headphones disconnected), a USB sound card leaving /proc/asound/cards. The last two are looked at once
	// a second (two small reads). Tests: BP_RUN_DIR, BP_ASOUND_CARDS (a file standing in for /proc/asound/cards)
	class SleepWatch
	{
	public:
		bool start(std::function<void(bool lid)> onSleep, std::function<void()> onAwake = nullptr,
			std::function<void(const std::string& what)> onUnplug = nullptr);
		void stop();
		~SleepWatch() { stop(); }
		// the lid's switch as last read (false where there's none)
		bool lidClosed() const { return lidShut.load(); }

	private:
		void run();
		void lookAtSound(bool first);
		std::function<void(bool)> onSleep;
		std::function<void()> onAwake;
		std::function<void(const std::string&)> onUnplug;
		std::thread thread;
		int stopPipe[2] = {-1, -1};
		std::vector<int> swFds;	// input devices with a lid or headphone switch
		std::atomic<bool> lidShut{false};
		bool jackIn = false, btIn = false;
		std::vector<int> usbCards;
	};
}
