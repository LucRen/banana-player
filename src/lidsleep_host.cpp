#include "lidsleep_host.h"
#include "filesave.h"
#include "lidsleep.h"
#include "volume.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	static std::string runDir()
	{
		const char* e = getenv("BP_RUN_DIR");
		return e && *e ? e : "/run/banana";
	}

	static std::string etcConf()
	{
		const char* t = getenv("BP_BANANAD_CONF");
		return t ? std::string(t) + "/etc.conf" : "/etc/banana/bananad.conf";
	}

	static std::string dataConf()
	{
		const char* t = getenv("BP_BANANAD_CONF");
		return t ? std::string(t) + "/data.conf" : "/data/banana/bananad.conf";
	}

	static bool slurp(const std::string& path, std::string& out)
	{
		out.clear();
		std::ifstream f(path, std::ios::binary);
		if (!f) return false;
		std::stringstream ss;
		ss << f.rdbuf();
		out = ss.str();
		return true;
	}

	static unsigned modeOf(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0 ? (st.st_mode & 07777) : 0644;
	}

	// bananad reads its settings again (and, with sleep-now there, sleeps)
	static void poke()
	{
#ifdef __linux__
		pokeBananad();
#endif
	}

	void LidGuard::hold()
	{
		struct stat st;
		if (stat(runDir().c_str(), &st) != 0) return;	// not BananaOS
		std::string etc, data;
		slurp(etcConf(), etc);
		bool exists = slurp(dataConf(), data);
		if (!lidSleepEffective(etc, data))
		{
			fprintf(stderr, "lid: BananaOS's lid sleep is off already: nothing to change\n");
			return;
		}
		LidNote note;
		note.conf = dataConf();
		std::string out = lidSleepOff(data, exists, note);
		// the note first: whatever happens after, the launcher (or the next start) knows what to undo
		if (!writeFileDurable(notePath(), lidNoteText(note)))
		{
			fprintf(stderr, "lid: can't write %s: lid sleep left on\n", notePath().c_str());
			return;
		}
		if (!writeFileDurable(note.conf, out, modeOf(note.conf)))
		{
			fprintf(stderr, "lid: can't write %s: lid sleep left on\n", note.conf.c_str());
			unlink(notePath().c_str());
			return;
		}
		held = true;
		poke();
		fprintf(stderr, "lid: sleep off while playing (was yes; %s)\n", note.mode == "changed" ? "its line changed" : note.mode == "added" ? "a line added" : "the file made");
	}

	void LidGuard::restore(bool lidClosed)
	{
		held = false;
		std::string text;
		if (!slurp(notePath(), text)) return;
		LidNote note;
		if (!parseLidNote(text, note))
		{
			fprintf(stderr, "lid: %s unreadable: removed\n", notePath().c_str());
			unlink(notePath().c_str());
			return;
		}
		std::string data, out;
		bool exists = slurp(note.conf, data);
		LidRestore r = lidRestore(data, exists, note, out);
		bool ok = true;
		if (r == LidRestore::Write) ok = writeFileDurable(note.conf, out, modeOf(note.conf));
		else if (r == LidRestore::Remove) ok = unlink(note.conf.c_str()) == 0;
		if (!ok)
		{
			// the note stays: the launcher, or the next start, tries again
			fprintf(stderr, "lid: can't put %s back\n", note.conf.c_str());
			return;
		}
		unlink(notePath().c_str());
		if (r == LidRestore::Skip) fprintf(stderr, "lid: restore skipped, changed in BananaOS Settings\n");
		else fprintf(stderr, "lid: sleep restored\n");
		// shut while it played: the sleep the lid would have brought, now (only if closing the lid sleeps again)
		if (lidClosed)
		{
			std::string etc, now;
			slurp(etcConf(), etc);
			slurp(note.conf, now);
			if (lidSleepEffective(etc, now))
			{
				fprintf(stderr, "lid: closed: asking bananad to sleep\n");
				if (FILE* f = fopen((runDir() + "/sleep-now").c_str(), "w")) fclose(f);
			}
		}
		poke();
	}

	void LidGuard::recover()
	{
		struct stat st;
		if (noteDir.empty() || stat(notePath().c_str(), &st) != 0) return;
		fprintf(stderr, "lid: a note from a run that didn't end: putting BananaOS's lid sleep back\n");
		restore(false);
	}

	void LidGuard::tick(unsigned nowMs, bool wants, bool lidClosed)
	{
		if (noteDir.empty()) return;
		if (wants)
		{
			offSince = 0;
			// once per stretch of playing (a failure isn't tried again every pass)
			if (!held && !tried) hold();
			tried = true;
			return;
		}
		tried = false;
		if (!held) return;
		// a moment's grace: between two songs, or a seek, isn't the end (with the lid shut, a sleep would cut the next)
		if (!offSince) offSince = nowMs ? nowMs : 1;
		else if (nowMs - offSince >= 3000)
		{
			offSince = 0;
			restore(lidClosed);
		}
	}

	void LidGuard::release(bool lidClosed)
	{
		offSince = 0;
		tried = false;
		if (held) restore(lidClosed);
	}
}
