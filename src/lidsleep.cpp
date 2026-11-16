#include "lidsleep.h"
#include <cctype>
#include <cstdio>
#include <sstream>

namespace bp
{
	static std::string trim(const std::string& s)
	{
		size_t a = 0, b = s.size();
		while (a < b && isspace((unsigned char)s[a])) a++;
		while (b > a && isspace((unsigned char)s[b - 1])) b--;
		return s.substr(a, b - a);
	}

	// a line of the file: where it starts and how long it is (its '\n' not counted)
	struct Line
	{
		size_t at, len;
	};

	static std::vector<Line> linesOf(const std::string& text)
	{
		std::vector<Line> out;
		size_t at = 0;
		while (at < text.size())
		{
			size_t nl = text.find('\n', at);
			if (nl == std::string::npos) nl = text.size();
			out.push_back({at, nl - at});
			at = nl + 1;
		}
		return out;
	}

	// bananad's test for a line it reads as lid_sleep (value: the trimmed text after '=')
	static bool isLidLine(const std::string& line, std::string* value = nullptr)
	{
		if (line.empty() || line[0] == '#') return false;
		size_t eq = line.find('=');
		if (eq == std::string::npos) return false;
		if (trim(line.substr(0, eq)) != "lid_sleep") return false;
		if (value) *value = trim(line.substr(eq + 1));
		return true;
	}

	// the last lid_sleep line (the one bananad ends up with), or -1
	static int lastLidLine(const std::string& text, const std::vector<Line>& lines)
	{
		int last = -1;
		for (size_t i = 0; i < lines.size(); i++)
			if (isLidLine(text.substr(lines[i].at, lines[i].len))) last = (int)i;
		return last;
	}

	bool lidSleepIn(const std::string& conf, bool& found)
	{
		found = false;
		bool on = false;
		std::vector<Line> lines = linesOf(conf);
		for (const Line& l : lines)
		{
			std::string v;
			if (!isLidLine(conf.substr(l.at, l.len), &v)) continue;
			found = true;
			on = v == "yes" || v == "1";
		}
		return on;
	}

	bool lidSleepEffective(const std::string& etcConf, const std::string& dataConf)
	{
		bool on = true, f = false;	// bananad's default
		bool e = lidSleepIn(etcConf, f);
		if (f) on = e;
		bool d = lidSleepIn(dataConf, f);
		if (f) on = d;
		return on;
	}

	std::string lidNoteText(const LidNote& n)
	{
		return "mode=" + n.mode + "\nconf=" + n.conf + "\nwas=" + n.was + "\nwrote=" + n.wrote + "\nnl=" + (n.nl ? "1" : "0") + "\n";
	}

	bool parseLidNote(const std::string& text, LidNote& n)
	{
		n = LidNote();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (eq == std::string::npos) continue;
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			if (k == "mode") n.mode = v;
			else if (k == "conf") n.conf = v;
			else if (k == "was") n.was = v;
			else if (k == "wrote") n.wrote = v;
			else if (k == "nl") n.nl = v == "1";
		}
		return (n.mode == "changed" || n.mode == "added" || n.mode == "created") && !n.wrote.empty() && !n.conf.empty();
	}

	std::string lidSleepOff(const std::string& data, bool exists, LidNote& note)
	{
		const std::string wrote = "lid_sleep = no";
		note.wrote = wrote;
		note.nl = false;
		note.was.clear();
		std::vector<Line> lines = linesOf(data);
		int i = lastLidLine(data, lines);
		if (i >= 0)
		{
			// the one bananad reads, in its place; the rest as it was
			note.mode = "changed";
			note.was = data.substr(lines[i].at, lines[i].len);
			return data.substr(0, lines[i].at) + wrote + data.substr(lines[i].at + lines[i].len);
		}
		note.mode = exists ? "added" : "created";
		note.nl = !data.empty() && data.back() != '\n';
		return data + (note.nl ? "\n" : "") + wrote + "\n";
	}

	LidRestore lidRestore(const std::string& data, bool exists, const LidNote& note, std::string& out)
	{
		out.clear();
		if (!exists) return LidRestore::Skip;
		std::vector<Line> lines = linesOf(data);
		int i = lastLidLine(data, lines);
		if (i < 0 || data.compare(lines[i].at, lines[i].len, note.wrote) != 0 || lines[i].len != note.wrote.size())
			return LidRestore::Skip;
		const Line& l = lines[i];
		if (note.mode == "changed")
		{
			out = data.substr(0, l.at) + note.was + data.substr(l.at + l.len);
			return LidRestore::Write;
		}
		// added or created: the line out, with its newline (and the one put in before it, when it was the last)
		size_t end = l.at + l.len;
		bool hadNl = end < data.size();
		out = data.substr(0, l.at) + data.substr(hadNl ? end + 1 : end);
		bool wasLast = (size_t)i == lines.size() - 1;
		if (note.nl && wasLast && !out.empty() && out.back() == '\n') out.pop_back();
		if (note.mode == "created" && out.empty()) return LidRestore::Remove;
		return LidRestore::Write;
	}

	std::vector<int> usbSoundCards(const std::string& procCards)
	{
		std::vector<int> out;
		std::istringstream in(procCards);
		std::string line;
		while (std::getline(in, line))
		{
			int n;
			if (sscanf(line.c_str(), " %d [", &n) != 1) continue;
			if (line.find("USB-Audio") != std::string::npos) out.push_back(n);
		}
		return out;
	}
}
