#pragma once
#include <string>

namespace bp
{
	// The file and signal side of "When the lid closes: Keep playing" (the rules: lidsleep.h). On the main thread:
	// main calls tick() a few times a second. While the setting is on and something plays here, BananaOS's lid sleep
	// is turned off (only if it's on); a few seconds after playing stops, it's put back, and with the lid shut then,
	// bananad is asked for the sleep the lid would have brought. The note ("lid-restore" in the state folder) lets
	// launch.sh undo it after a crash, and recover() after a power loss.
	// Paths: /run/banana, /etc/banana/bananad.conf, /data/banana/bananad.conf; tests: BP_RUN_DIR moves the run folder,
	// BP_BANANAD_CONF names a folder holding "etc.conf" and "data.conf" (as for the idle sleep). Without the run folder
	// (not BananaOS) it never switches anything.
	class LidGuard
	{
	public:
		void configure(const std::string& stateDir) { noteDir = stateDir; }
		// at start: a note left behind (the power went, a kill the launcher didn't see) undone before anything else
		void recover();
		// wants: the setting is on and something plays on the handheld. lidClosed: the lid's switch now
		void tick(unsigned nowMs, bool wants, bool lidClosed);
		// quitting (or the setting turned off): put back now
		void release(bool lidClosed);
		bool holding() const { return held; }

	private:
		void hold();
		void restore(bool lidClosed);
		std::string notePath() const { return noteDir + "/lid-restore"; }
		std::string noteDir;
		bool held = false, tried = false;
		unsigned offSince = 0;
	};

	// ask bananad for a sleep now, as closing the lid with lid sleep on does: /run/banana/sleep-now, then a SIGHUP
	// (its reload unlinks the file and sleeps). False: no run folder (not BananaOS) or the file couldn't be made
	bool askBananadSleep();
}
