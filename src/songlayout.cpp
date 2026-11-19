#include "songlayout.h"
#include <cstring>
#include <cctype>

#include <algorithm>
#include <cmath>

namespace bp
{
	SongLayout songLayout(bool showArt, int widest)
	{
		SongLayout l;
		l.art = true;
		// Album art only: the whole panel; Art + visualizer: the space above the band
		const int top = 0, bottom = showArt ? 480 : SONG_BAND_TOP;
		l.artS = showArt ? 240 : 224;
		l.midY = (top + bottom) / 2;
		l.artY = l.midY - l.artS / 2;
		const int gap = 28, room = 560 - l.artS - gap;
		l.text = true;
		l.textW = std::clamp(widest, 0, room);
		// the group on its real width: a short title doesn't leave the art hugging the left
		l.artX = 40 + (room - l.textW) / 2;
		l.textX = l.artX + l.artS + gap;
		return l;
	}

	// a letter's width at fs: wide (CJK and the like: 3 or 4 UTF-8 bytes) or not
	static double letterWidth(unsigned char c, int fs, bool bold, size_t& len)
	{
		len = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
		return (len >= 3 ? 1.0 : bold ? 0.48 : 0.45) * fs;
	}

	int songTextWidth(const std::string& s, int fs, bool bold)
	{
		double w = 0;
		for (size_t i = 0; i < s.size();)
		{
			size_t len;
			w += letterWidth((unsigned char)s[i], fs, bold, len);
			i += len;
		}
		return (int)std::ceil(w - 1e-6);
	}

	std::string songFitText(const std::string& s, int fs, int maxW, bool bold)
	{
		const double dots = (bold ? 0.48 : 0.45) * fs * 2;	// (the ellipsis is about two letters wide)
		double w = 0;
		size_t cut = std::string::npos;
		for (size_t i = 0; i < s.size();)
		{
			size_t len;
			double cw = letterWidth((unsigned char)s[i], fs, bold, len);
			if (cut == std::string::npos && w + cw > maxW - dots) cut = i;	// where "…" would go, if it comes to that
			w += cw;
			if (w > maxW + 1e-6)
			{
				std::string kept = s.substr(0, cut);
				while (!kept.empty() && kept.back() == ' ') kept.pop_back();	// "Round…", not "Round …"
				return kept + "\xe2\x80\xa6";
			}
			i += len;
		}
		return s;
	}

	std::string vizPlaceArt(const std::string& graph, int x, int y, int art, const std::string& ground, const std::string& bandGround)
	{
		// the band's rows in their own ground over the top's (one frame: drawn once, held)
		const std::string band = ground == bandGround ? "" : ",drawbox=x=0:y=" + std::to_string(SONG_BAND_TOP) + ":w=640:h=" +
			std::to_string(480 - SONG_BAND_TOP) + ":c=" + bandGround + ":t=fill";
		static const std::string from = "[vid1] scale=640:480:", until = ",setsar=1";
		std::string g = graph;
		for (size_t at = g.find(from); at != std::string::npos; at = g.find(from, at + 1))
		{
			size_t end = g.find(until, at);
			if (end == std::string::npos) break;
			// one frame either way (a still: the art's picture, or one black frame), held by what follows (settb, split)
			std::string in = art <= 0 ? "color=" + ground + ":s=640x480:r=1:d=1" + band
				: "[vid1] scale=" + std::to_string(art) + ":" + std::to_string(art) + ":force_original_aspect_ratio=increase,crop=" +
					std::to_string(art) + ":" + std::to_string(art) + ",pad=640:480:" + std::to_string(x) + ":" + std::to_string(y) + ":" +
					ground + band;
			g.replace(at, end - at, in);
		}
		return g;
	}

	namespace
	{
		const char* const TITLE_SEPS[] = {" - ", " \xE2\x80\x93 ", " \xE2\x80\x94 ", ": ", " | ", "_"};

		bool startsNoCase(const std::string& s, const std::string& start)
		{
			if (start.empty() || s.size() < start.size()) return false;
			for (size_t i = 0; i < start.size(); i++)
				if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)start[i])) return false;
			return true;
		}

		// letters (a byte that starts a non-ASCII character counts as one: a letter in another script)
		int letters(const std::string& s)
		{
			int n = 0;
			for (unsigned char c : s)
				if (std::isalpha(c) || c >= 0xC0) n++;
			return n;
		}

		// the length of the separator at s[at], 0 if none
		size_t sepAt(const std::string& s, size_t at)
		{
			for (const char* sep : TITLE_SEPS)
				if (s.compare(at, strlen(sep), sep) == 0) return strlen(sep);
			return 0;
		}
	}

	std::string shownTitle(const std::string& title, const std::vector<std::string>& starts, const std::function<bool(const std::string&)>& fits)
	{
		if (fits(title)) return title;
		std::string t = title;
		int dropped = 0;
		for (const std::string& st : starts)
		{
			if (dropped >= 2) break;
			if (!startsNoCase(t, st)) continue;
			const size_t sep = sepAt(t, st.size());
			if (!sep) continue;
			std::string rest = t.substr(st.size() + sep);
			if (letters(rest) < 3) continue;
			t = rest;
			dropped++;
		}
		return t;
	}

	std::string sharedStart(const std::vector<std::string>& names)
	{
		if (names.size() < 3) return "";
		size_t n = names[0].size();
		for (const std::string& s : names)
		{
			size_t k = 0;
			while (k < n && k < s.size() && std::tolower((unsigned char)s[k]) == std::tolower((unsigned char)names[0][k])) k++;
			n = k;
		}
		// back to the last separator inside it: the start is what comes before that separator
		for (size_t at = n; at-- > 0;)
			if (sepAt(names[0], at) && at + sepAt(names[0], at) <= n + 0)
			{
				std::string st = names[0].substr(0, at);
				return letters(st) >= 4 ? st : "";
			}
		// (a separator that begins inside the shared part but ends after it: every name has it too only if shared)
		return "";
	}

	std::vector<std::string> titleStarts(const std::string& artist, const std::string& album, const std::string& path,
		const std::vector<std::string>& names)
	{
		std::vector<std::string> st;
		if (!artist.empty()) st.push_back(artist);
		if (!album.empty()) st.push_back(album);
		const size_t slash = path.find_last_of('/');
		if (slash != std::string::npos && slash > 0)
		{
			const size_t up = path.find_last_of('/', slash - 1);
			std::string dir = path.substr(up == std::string::npos ? 0 : up + 1, slash - (up == std::string::npos ? 0 : up + 1));
			if (!dir.empty()) st.push_back(dir);
		}
		const std::string shared = sharedStart(names);
		if (!shared.empty()) st.push_back(shared);
		return st;
	}
}
