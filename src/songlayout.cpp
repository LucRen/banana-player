#include "songlayout.h"

#include <algorithm>
#include <cmath>

namespace bp
{
	SongLayout songLayout(bool showArt, bool info, int widest)
	{
		SongLayout l;
		l.art = true;
		// Album art only: the whole panel; Art + visualizer: the space above the band
		const int top = 0, bottom = showArt ? 480 : SONG_BAND_TOP;
		l.artS = showArt ? (info ? 240 : 260) : (info ? 224 : 240);
		l.midY = (top + bottom) / 2;
		l.artY = l.midY - l.artS / 2;
		if (!info)
		{
			l.artX = (640 - l.artS) / 2;
			return l;
		}
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

	std::string vizPlaceArt(const std::string& graph, int x, int y, int art)
	{
		static const std::string from = "[vid1] scale=640:480:", until = ",setsar=1";
		std::string g = graph;
		for (size_t at = g.find(from); at != std::string::npos; at = g.find(from, at + 1))
		{
			size_t end = g.find(until, at);
			if (end == std::string::npos) break;
			// one frame either way (a still: the art's picture, or one black frame), held by what follows (settb, split)
			std::string in = art <= 0 ? "color=black:s=640x480:r=1:d=1"
				: "[vid1] scale=" + std::to_string(art) + ":" + std::to_string(art) + ":force_original_aspect_ratio=increase,crop=" +
					std::to_string(art) + ":" + std::to_string(art) + ",pad=640:480:" + std::to_string(x) + ":" + std::to_string(y) + ":black";
			g.replace(at, end - at, in);
		}
		return g;
	}
}
