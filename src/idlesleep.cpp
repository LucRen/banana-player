#include "idlesleep.h"
#include <cstdlib>
#include <sstream>

namespace bp
{
	std::string idleHeldBy(const IdleHolds& h)
	{
		if (h.charging) return "on the charger";
		if (h.playing) return "something is playing";
		if (h.browserOut) return "a dashboard page is playing";
		if (h.fileJob) return "a file job is running";
		if (h.mangaDownloads) return "manga downloads are running";
		if (h.mangaEngine) return "the manga engine is starting";
		if (h.webActive) return "the dashboard was used in the last 2 minutes";
		if (h.updating) return "an update is downloading";
		return "";
	}

	IdleDecision idleDecide(double idleSec, double autosleepMin, const IdleHolds& h)
	{
		IdleDecision d;
		if (autosleepMin <= 0) { d.verdict = IdleVerdict::Off; return d; }
		if (idleSec < autosleepMin * 60.0) return d;
		d.why = idleHeldBy(h);
		d.verdict = d.why.empty() ? IdleVerdict::Sleep : IdleVerdict::Held;
		return d;
	}

	double parseAutosleepMinutes(const std::string& conf, bool& found)
	{
		found = false;
		double v = 0;
		std::istringstream in(conf);
		std::string line;
		auto trim = [](const std::string& s) {
			size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		};
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			if (trim(line.substr(0, eq)) != "autosleep_minutes") continue;
			std::string val = trim(line.substr(eq + 1));
			char* end = nullptr;
			double x = strtod(val.c_str(), &end);
			if (end == val.c_str()) continue;
			v = x;
			found = true;
		}
		return v;
	}
}
