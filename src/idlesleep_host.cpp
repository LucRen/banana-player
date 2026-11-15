#include "idlesleep_host.h"
#include "volume.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace bp
{
	static std::string slurp(const std::string& path)
	{
		std::ifstream f(path);
		std::stringstream ss;
		ss << f.rdbuf();
		return ss.str();
	}

	static std::string runDir()
	{
		const char* e = getenv("BP_RUN_DIR");
		return e && *e ? e : "/run/banana";
	}

	// BananaOS's setting: /data/banana/bananad.conf over /etc/banana/bananad.conf. Read again every minute (cheap).
	// Nowhere found, or no /run/banana (not BananaOS): off
	double IdleSleep::minutes(unsigned nowMs)
	{
		struct stat st;
		if (stat(runDir().c_str(), &st) != 0) return 0;
		if (const char* s = getenv("BP_IDLE_SLEEP_S")) return atof(s) / 60.0;
		if (haveConf && nowMs - confAt < 60000) return confMin;
		confAt = nowMs;
		haveConf = true;
		const char* t = getenv("BP_BANANAD_CONF");
		std::string etc = t ? std::string(t) + "/etc.conf" : "/etc/banana/bananad.conf";
		std::string data = t ? std::string(t) + "/data.conf" : "/data/banana/bananad.conf";
		bool f1 = false, f2 = false;
		double m1 = parseAutosleepMinutes(slurp(etc), f1), m2 = parseAutosleepMinutes(slurp(data), f2);
		double m = f2 ? m2 : f1 ? m1 : 0;
		if (!said || m != confMin)
			fprintf(stderr, "idle sleep: BananaOS's auto-sleep is %g min%s\n", m, m > 0 ? "" : " (off: the player doesn't sleep itself)");
		said = true;
		confMin = m;
		return m;
	}

	void IdleSleep::tick(unsigned nowMs, const IdleHolds& holds)
	{
		// the process was stopped (a sleep, the lid): waking is input, whatever the clock counted meanwhile
		if (lastTickMs && (unsigned)(nowMs - lastTickMs) > 10000) lastInputMs = nowMs;
		lastTickMs = nowMs;
		double min = minutes(nowMs);
		double idle = (unsigned)(nowMs - lastInputMs) / 1000.0;
		IdleDecision d = idleDecide(idle, min, holds);
		switch (d.verdict)
		{
		case IdleVerdict::Off:
		case IdleVerdict::NotYet:
			heldLogged = false;
			lastHeld.clear();
			break;
		case IdleVerdict::Held:
			// once per change of the hold, not every pass
			if (!heldLogged || d.why != lastHeld)
				fprintf(stderr, "idle sleep: %.0f s idle, kept awake: %s\n", idle, d.why.c_str());
			heldLogged = true;
			lastHeld = d.why;
			break;
		case IdleVerdict::Sleep:
			heldLogged = false;
			lastHeld.clear();
			fprintf(stderr, "idle sleep: asking bananad to sleep: %.0f s without input, nothing keeps it awake\n", idle);
			if (FILE* f = fopen((runDir() + "/sleep-now").c_str(), "w")) fclose(f);
			else fprintf(stderr, "idle sleep: can't write %s/sleep-now\n", runDir().c_str());
#ifdef __linux__
			pokeBananad();
#endif
			// the request counts as input: after the wake (the process was stopped meanwhile) a whole idle period
			// starts again, and a sleep that didn't happen is asked for again only after one
			lastInputMs = nowMs;
			break;
		}
	}
}
