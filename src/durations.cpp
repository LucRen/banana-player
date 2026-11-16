#include "durations.h"
#include "format.h"
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	namespace
	{
		// one line of the durations file: realPath TAB size TAB mtime TAB seconds. A key is those first three joined
		// by newlines, so the path is everything before the last two newlines; false for a path that can't sit on a line
		bool keyLine(const std::string& key, double secs, std::string& line)
		{
			size_t b = key.rfind('\n');
			if (b == std::string::npos || b == 0) return false;
			size_t a = key.rfind('\n', b - 1);
			if (a == std::string::npos) return false;
			std::string path = key.substr(0, a);
			// (a tab would shift the fields, a newline would make two lines: neither would read back)
			if (path.find_first_of("\t\n") != std::string::npos) return false;
			// 17 digits: the exact double, so a value reads back as the one cached
			char text[32];
			snprintf(text, sizeof text, "%.17g", secs);
			line = path + '\t' + key.substr(a + 1, b - a - 1) + '\t' + key.substr(b + 1) + '\t' + text + '\n';
			return true;
		}

		// a whole line (no newline) as a key and seconds; malformed: false
		bool parseLine(const std::string& line, std::string& key, double& secs)
		{
			size_t t1 = line.find('\t');
			if (t1 == std::string::npos) return false;
			size_t t2 = line.find('\t', t1 + 1);
			if (t2 == std::string::npos) return false;
			size_t t3 = line.find('\t', t2 + 1);
			// (a fourth tab: the path had one, or a field did)
			if (t3 == std::string::npos || line.find('\t', t3 + 1) != std::string::npos) return false;
			if (t1 == 0 || t3 + 1 >= line.size()) return false;
			long long size = 0, mtime = 0;
			auto number = [](const std::string& s, long long& v) {
				if (s.empty()) return false;
				errno = 0;
				char* end = nullptr;
				v = strtoll(s.c_str(), &end, 10);
				return errno == 0 && end && *end == '\0';
			};
			if (!number(line.substr(t1 + 1, t2 - t1 - 1), size)) return false;
			if (!number(line.substr(t2 + 1, t3 - t2 - 1), mtime)) return false;
			errno = 0;
			char* end = nullptr;
			secs = strtod(line.c_str() + t3 + 1, &end);
			if (errno != 0 || !end || *end != '\0' || !std::isfinite(secs)) return false;
			key = durationKey(line.substr(0, t1), size, mtime);
			return true;
		}
	}

	std::string durationKey(const std::string& realPath, long long size, long long mtime)
	{
		return realPath + '\n' + std::to_string(size) + '\n' + std::to_string(mtime);
	}

	DurationCache::~DurationCache()
	{
		// (the shared one is never destroyed; a test's is closed here)
		if (out) fclose(out);
	}

	bool DurationCache::load(const std::string& file)
	{
		std::lock_guard<std::mutex> g(m);
		if (out)
		{
			fclose(out);
			out = nullptr;
		}
		// (a second load replaces what was known: the file is the whole answer)
		order.clear();
		index.clear();
		size_t lines = 0;
		bool discarding = false;
		if (FILE* in = fopen(file.c_str(), "r"))
		{
			std::string line;
			char buf[4096];
			while (fgets(buf, sizeof buf, in))
			{
				line += buf;
				if (line.empty() || line.back() != '\n')
				{
					// no path is this long: malformed, and the rest of it is dropped with the newline
					if (line.size() > 16384)
					{
						discarding = true;
						line.clear();
					}
					continue;
				}
				lines++;
				if (discarding)
				{
					discarding = false;
					line.clear();
					continue;
				}
				line.pop_back();
				std::string key;
				double secs = 0;
				if (parseLine(line, key, secs)) setLocked(key, secs);
				line.clear();
			}
			// a last line with no newline was cut short (a card pulled while it was written): it is not an entry
			if (!line.empty()) lines++;
			fclose(in);
		}
		// replaced lines and evicted entries still sit in the file: rewrite it now, while it can't grow
		if (lines > 2 * order.size() || lines > cap)
		{
			std::string tmp = file + ".tmp";
			if (FILE* w = fopen(tmp.c_str(), "w"))
			{
				bool ok = true;
				for (auto& e : order)
				{
					std::string line;
					if (!keyLine(e.first, e.second, line)) continue;
					if (fwrite(line.data(), 1, line.size(), w) != line.size())
					{
						ok = false;
						break;
					}
				}
				if (ok && fflush(w) != 0) ok = false;
				if (ok && fsync(fileno(w)) != 0) ok = false;
				if (fclose(w) != 0) ok = false;
				// (only a whole rewrite may replace the old file: a full card leaves it as it was)
				if (!ok || rename(tmp.c_str(), file.c_str()) != 0) unlink(tmp.c_str());
			}
		}
		out = fopen(file.c_str(), "a");
		return out != nullptr;
	}

	bool DurationCache::find(const std::string& key, double& secs)
	{
		std::lock_guard<std::mutex> g(m);
		auto it = index.find(key);
		if (it == index.end()) return false;
		order.splice(order.begin(), order, it->second);
		secs = it->second->second;
		return true;
	}

	void DurationCache::setLocked(const std::string& key, double secs)
	{
		auto it = index.find(key);
		if (it != index.end())
		{
			it->second->second = secs;
			order.splice(order.begin(), order, it->second);
		}
		else
		{
			order.emplace_front(key, secs);
			index[key] = order.begin();
		}
		while (order.size() > cap && !order.empty())
		{
			index.erase(order.back().first);
			order.pop_back();
		}
	}

	void DurationCache::appendLocked(const std::string& key, double secs)
	{
		if (!out) return;
		std::string line;
		if (!keyLine(key, secs, line)) return;
		fwrite(line.data(), 1, line.size(), out);
		// whole lines before this one stay (a yank can't lose the run's answers, only the one being written)
		fflush(out);
	}

	void DurationCache::put(const std::string& key, double secs)
	{
		std::lock_guard<std::mutex> g(m);
		auto it = index.find(key);
		// the same answer (a probe and mpv both knowing it): nothing newer for the file
		if (it != index.end() && it->second->second == secs) return;
		setLocked(key, secs);
		appendLocked(key, secs);
	}

	size_t DurationCache::size()
	{
		std::lock_guard<std::mutex> g(m);
		return order.size();
	}

	DurationCache& DurationCache::shared()
	{
		static DurationCache* c = new DurationCache(50000);
		return *c;
	}

	double saneDuration(double secs, long long size)
	{
		// a week: longer is a header that lies, not a recording
		if (!std::isfinite(secs) || secs <= 0 || secs >= 7 * 24 * 3600.0) return -1;
		if (size >= 0 && double(size) * 8 / secs < 64) return -1;
		return secs;
	}

	std::string fileNote(const std::string& at, double duration, long long size)
	{
		static const std::string DOT = " \xC2\xB7 ";
		std::string len = duration > 0 ? formatTime(duration) : "";
		std::string head = at.empty() ? len : len.empty() ? at : at + " / " + len;
		std::string sz = size >= 0 ? formatBytes(size) : "";
		return head.empty() ? sz : sz.empty() ? head : head + DOT + sz;
	}

	bool durationInHeader(const std::string& name)
	{
		size_t dot = name.rfind('.');
		if (dot == std::string::npos) return true;
		std::string e = name.substr(dot + 1);
		for (auto& c : e) c = (char)tolower((unsigned char)c);
		for (const char* x : {"ts", "m2ts", "mts", "m2t", "tp", "trp", "mpg", "mpeg", "vob", "m2v",
				 "aac", "ac3", "eac3", "dts", "h264", "264", "hevc", "h265", "265"})
			if (e == x) return false;
		return true;
	}

	int mp3FrameBitrate(const unsigned char* p, size_t n)
	{
		if (n < 4 || p[0] != 0xff || (p[1] & 0xe0) != 0xe0) return 0;
		int version = (p[1] >> 3) & 3, layer = (p[1] >> 1) & 3, index = p[2] >> 4, rate = (p[2] >> 2) & 3;
		// version 1 reserved; layer 1 is Layer III; index 0 "free", 15 bad; sample rate 3 reserved
		if (version == 1 || layer != 1 || index == 0 || index == 15 || rate == 3) return 0;
		static const int MPEG1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
		static const int MPEG2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};	// (and 2.5)
		return (version == 3 ? MPEG1 : MPEG2)[index] * 1000;
	}

	int mp3CbrBitrate(const unsigned char* p, size_t n)
	{
		int rate = 0, frames = 0;
		size_t at = 0;
		while (at + 4 <= n)
		{
			int bps = mp3FrameBitrate(p + at, n - at);
			if (!bps || (rate && bps != rate)) return 0;
			rate = bps;
			int version = (p[at + 1] >> 3) & 3, sr = (p[at + 2] >> 2) & 3, pad = (p[at + 2] >> 1) & 1;
			static const int RATES[] = {44100, 48000, 32000};
			int hz = RATES[sr] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
			size_t len = (size_t)((version == 3 ? 144 : 72) * (long long)bps / hz + pad);
			if (len < 4 || at + len > n) break;	// (the last, cut by the read)
			at += len;
			frames++;
		}
		return frames >= 8 ? rate : 0;
	}

	bool cachedDuration(const std::string& realPath, double& secs, long long* size)
	{
		struct stat st;
		if (stat(realPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
		if (size) *size = (long long)st.st_size;
		return DurationCache::shared().find(durationKey(realPath, (long long)st.st_size, (long long)st.st_mtime), secs);
	}

	void recordDuration(const std::string& path, double secs)
	{
		// under the path as given (the file lists look a length up by it) and, when a symlink leads elsewhere, the
		// real one too (the dashboard's probe keys by it): the two never miss each other
		struct stat st;
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return;
		double sane = saneDuration(secs, (long long)st.st_size);
		if (sane <= 0) return;
		DurationCache& c = DurationCache::shared();
		c.put(durationKey(path, (long long)st.st_size, (long long)st.st_mtime), sane);
		if (char* real = realpath(path.c_str(), nullptr))
		{
			if (path != real) c.put(durationKey(real, (long long)st.st_size, (long long)st.st_mtime), sane);
			free(real);
		}
	}
}
