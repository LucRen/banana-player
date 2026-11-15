#pragma once
#include <string>

namespace bp
{
	// The player's own idle sleep. bananad never dims or sleeps while a game is named in /run/banana/game.pid, and the
	// player names itself there (main's holdIdleSleep: a film must not dim, issue #4), so with nothing playing it held
	// the handheld awake for ever. This follows BananaOS's rule instead: after `autosleep_minutes` without a button or
	// touch in the player, and only while nothing needs the handheld awake, ask bananad for a sleep as its menu's
	// Sleep does (/run/banana/sleep-now + SIGHUP).

	// what keeps it awake (each is a fact main reads from the player, the Ui, the web server...)
	struct IdleHolds
	{
		bool playing = false;		// a song or video is playing (not paused)
		bool browserOut = false;	// a dashboard page is the output and plays
		bool fileJob = false;		// a copy, move or delete, or a network copy of a book
		bool mangaDownloads = false;	// the chapter queue has work (a running or waiting chapter, not all-paused)
		bool mangaEngine = false;	// the extension engine is starting (about 25 s) or being installed
		bool webActive = false;		// a dashboard request or upload in the last 2 minutes
		bool updating = false;		// an update downloading
		bool charging = false;		// on the charger (bananad: a sleep would put the charging light out)
	};

	// the first hold's name, "" when none
	std::string idleHeldBy(const IdleHolds& h);

	enum class IdleVerdict { Off, NotYet, Held, Sleep };
	struct IdleDecision
	{
		IdleVerdict verdict = IdleVerdict::NotYet;
		std::string why;	// Held: the hold's name
	};
	// autosleepMin <= 0 (or unknown): Off. idleSec: seconds since the last button or touch
	IdleDecision idleDecide(double idleSec, double autosleepMin, const IdleHolds& h);

	// `autosleep_minutes` from a bananad.conf's text ("key = value" lines, # comments); the last one wins, as
	// bananad reads it. found false: the key isn't there. The override (/data/banana) is read after the base file
	// (/etc/banana) with the same function, and its value used when found
	double parseAutosleepMinutes(const std::string& conf, bool& found);
}
