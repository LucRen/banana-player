#pragma once
#include "idlesleep.h"
#include <functional>
#include <string>

namespace bp
{
	// The files and the signal side of the idle sleep (main's loop calls tick() about once a second, on the main
	// thread). Paths: /run/banana, /etc/banana/bananad.conf, /data/banana/bananad.conf; for tests BP_RUN_DIR moves the
	// run folder and BP_BANANAD_CONF names a folder holding "etc.conf" and "data.conf". BP_IDLE_SLEEP_S sets the idle
	// seconds directly (tests). Without /run/banana (not BananaOS) it does nothing.
	class IdleSleep
	{
	public:
		// a button or touch (ms: SDL_GetTicks)
		void input(unsigned ms) { lastInputMs = ms; }
		// nowMs: SDL_GetTicks. Logs, and asks for the sleep, when it is due. beforeSleep runs on this (main) thread just
		// before the request: incognito's Close all or Quit happens first, as the spec has it; it may change the holds
		void tick(unsigned nowMs, const IdleHolds& holds, const std::function<void()>& beforeSleep = {});

	private:
		double minutes(unsigned nowMs);
		unsigned lastInputMs = 0, confAt = 0, lastTickMs = 0;
		bool haveConf = false, said = false;
		double confMin = 0;
		std::string lastHeld;	// the hold last logged ("" none): once per change
		bool heldLogged = false;
	};
}
