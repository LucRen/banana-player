#include "levelcard.h"
#include "theme.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <map>

namespace bp
{
	using namespace theme;

	// The card's design, in a 960x720 canvas's units (what the top panel's ASS cards were drawn in, so it stands
	// where and as large as the chapter and equalizer cards do): a 300x84 box, the label and percent at 20,10 in a
	// 30-high line of Inter (the top panel's OSD font), a 260x12 bar at 20,54
	static const float CARD_W = 300, CARD_H = 84, PAD_X = 20, TEXT_Y = 10, TEXT_SIZE = 30, BAR_Y = 54, BAR_W = 260, BAR_H = 12;
	// the label alone (the reader's zoom): as high as its line and the same padding round it
	static const float SOLO_PAD_Y = 14, SOLO_MIN_W = 120;
	static const int CARD_ALPHA = 223;	// (the ASS card's \1a&H20&: the picture shows a little through)

	int levelTop(const std::string& label, int volumeMax)
	{
		return label == "Volume" ? std::max(100, volumeMax) : 100;
	}

	float levelCardMiddle(bool playing)
	{
		// the box's middle: 360 of 720 over what plays, 610 with nothing (under the idle icon and name)
		return playing ? 0.5f : 610.f / 720;
	}

	// Inter at the size whose line (ascent + descent, as libass sizes a font) is px high: opened once a size
	static TTF_Font* lineFont(const std::string& fontsDir, float px)
	{
		static std::map<std::string, TTF_Font*> fonts;
		static std::map<std::string, float> perPt;	// a font's line height per point
		std::string path = fontsDir + "/Inter-Regular.ttf";
		if (!perPt.count(path))
		{
			TTF_Font* probe = TTF_OpenFont(path.c_str(), 100);
			if (!probe) return nullptr;
			perPt[path] = TTF_FontHeight(probe) / 100.f;
			TTF_CloseFont(probe);
		}
		int pt = std::max(6, (int)std::lround(px / std::max(0.5f, perPt[path])));
		std::string key = path + "@" + std::to_string(pt);
		auto it = fonts.find(key);
		if (it != fonts.end()) return it->second;
		TTF_Font* f = TTF_OpenFont(path.c_str(), pt);
		if (f) fonts[key] = f;	// (kept: a handful of sizes at most, the panel's)
		return f;
	}

	LevelCardImage drawLevelCard(const std::string& fontsDir, int panelH, const std::string& label, int percent, int top)
	{
		LevelCardImage out;
		const float k = (panelH > 0 ? panelH : 720) / 720.f;	// panel pixels per canvas unit
		const bool bar = top > 0;
		if (bar) percent = std::clamp(percent, 0, top);
		std::string text = bar ? label + "  " + std::to_string(percent) + "%" : label;
		TTF_Font* font = lineFont(fontsDir, TEXT_SIZE * k);
		SDL_Surface* ts = nullptr;
		if (font)
		{
			SDL_Color ink{TEXT.r, TEXT.g, TEXT.b, 255};
			if (SDL_Surface* t = TTF_RenderUTF8_Blended(font, text.c_str(), ink))
			{
				ts = SDL_ConvertSurfaceFormat(t, SDL_PIXELFORMAT_RGBA32, 0);
				SDL_FreeSurface(t);
			}
		}
		const int textW = ts ? ts->w : 0, textH = ts ? ts->h : (int)std::lround(TEXT_SIZE * k);
		out.w = bar ? (int)std::lround(CARD_W * k) : std::max((int)std::lround(SOLO_MIN_W * k), textW + (int)std::lround(2 * PAD_X * k));
		out.h = bar ? (int)std::lround(CARD_H * k) : textH + (int)std::lround(2 * SOLO_PAD_Y * k);
		out.rgba = std::make_shared<std::vector<uint8_t>>((size_t)out.w * out.h * 4, 0);
		auto& v = *out.rgba;
		const int W = out.w, H = out.h;
		// one source over what's drawn (straight alpha)
		auto blend = [&](int x, int y, Rgb c, float a) {
			if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
			uint8_t* p = &v[((size_t)y * W + x) * 4];
			float da = p[3] / 255.f, oa = a + da * (1 - a);
			if (oa <= 0) return;
			p[0] = (uint8_t)std::lround((c.r * a + p[0] * da * (1 - a)) / oa);
			p[1] = (uint8_t)std::lround((c.g * a + p[1] * da * (1 - a)) / oa);
			p[2] = (uint8_t)std::lround((c.b * a + p[2] * da * (1 - a)) / oa);
			p[3] = (uint8_t)std::lround(oa * 255);
		};
		// a box with round corners, in canvas units from the card's corner, edges smoothed by each pixel's cover
		// (as libass draws the ASS cards' shapes)
		auto box = [&](float x0, float y0, float bw, float bh, float r, Rgb c, int alpha) {
			x0 *= k; y0 *= k; bw *= k; bh *= k;
			r = std::min({r * k, bw / 2, bh / 2});
			float cx = x0 + bw / 2, cy = y0 + bh / 2, hx = bw / 2 - r, hy = bh / 2 - r;
			for (int y = std::max(0, (int)std::floor(y0)); y < std::min(H, (int)std::ceil(y0 + bh)); y++)
				for (int x = std::max(0, (int)std::floor(x0)); x < std::min(W, (int)std::ceil(x0 + bw)); x++)
				{
					// the distance outside the rounded box, from the pixel's middle (negative inside)
					float qx = std::abs(x + 0.5f - cx) - hx, qy = std::abs(y + 0.5f - cy) - hy;
					float d = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) + std::min(std::max(qx, qy), 0.f) - r;
					float cover = std::clamp(0.5f - d, 0.f, 1.f);
					blend(x, y, c, cover * alpha / 255.f);
				}
		};
		box(0, 0, W / k, H / k, R_TOP_CARD, TOP_CARD, CARD_ALPHA);
		if (bar)
		{
			float fill = BAR_W * percent / top;
			box(PAD_X, BAR_Y, BAR_W, BAR_H, R_TOP_BAR, TOP_TRACK, 255);
			if (fill > 0) box(PAD_X, BAR_Y, fill, BAR_H, R_TOP_BAR, TOP_TITLE, 255);
			if (top > 100)
			{
				// past 100% (Max volume 120 or 150): a mark at 100, and the part above it louder in colour, red
				// past 120 (where the hearing warning starts)
				float at = BAR_W * 100 / top;
				if (percent > 100) box(PAD_X + at, BAR_Y, fill - at, BAR_H, R_TOP_BAR, percent > 120 ? DANGER : ACCENT, 255);
				box(PAD_X + at - 1, BAR_Y - 4, 3, BAR_H + 8, 0, TEXT, 255);
			}
		}
		if (ts)
		{
			int tx = bar ? (int)std::lround(PAD_X * k) : (W - ts->w) / 2, ty = bar ? (int)std::lround(TEXT_Y * k) : (H - ts->h) / 2;
			for (int y = 0; y < ts->h; y++)
				for (int x = 0; x < ts->w; x++)
				{
					const uint8_t* q = (const uint8_t*)ts->pixels + y * ts->pitch + x * 4;
					blend(tx + x, ty + y, Rgb{q[0], q[1], q[2]}, q[3] / 255.f);
				}
			SDL_FreeSurface(ts);
		}
		return out;
	}

	std::vector<uint8_t> premultipliedBgra(const LevelCardImage& card)
	{
		std::vector<uint8_t> bgra((size_t)card.w * card.h * 4);
		if (!card.ok()) return bgra;
		const uint8_t* s = card.rgba->data();
		for (size_t p = 0; p < (size_t)card.w * card.h; p++)
		{
			const uint8_t* c = s + p * 4;
			int a = c[3];
			bgra[p * 4 + 0] = (uint8_t)((c[2] * a + 127) / 255);
			bgra[p * 4 + 1] = (uint8_t)((c[1] * a + 127) / 255);
			bgra[p * 4 + 2] = (uint8_t)((c[0] * a + 127) / 255);
			bgra[p * 4 + 3] = (uint8_t)a;
		}
		return bgra;
	}
}
