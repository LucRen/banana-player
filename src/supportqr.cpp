#include "supportqr.h"
#include "kitlayout.h"
#include "pagedecode.h"
#include "theme.h"
#include <SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace bp
{
	namespace
	{
		// Inter at the point size whose line is `px` high (as levelcard.cpp measures one): the address is read from
		// across a room, so its size follows the panel's
		TTF_Font* lineFont(const std::string& fontsDir, int px)
		{
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
			return TTF_OpenFont(path.c_str(), pt);
		}

		// the code's file: the bundle's share/banana-player/kofi-qr.png (the fonts' folder one up), then a source
		// build's packaging/support/
		PageImage readQr(const std::string& fontsDir)
		{
			PageImage img = decodePageFile(fontsDir + "/../kofi-qr.png", 0);
			if (!img.ok()) img = decodePageFile(fontsDir + "/../../packaging/support/kofi-qr.png", 0);
			return img;
		}
	}

	SupportQrImage composeSupportQr(const std::string& fontsDir, int w, int h)
	{
		SupportQrImage out;
		if (w <= 0 || h <= 0) return out;
		PageImage qr = readQr(fontsDir);
		if (!qr.ok())
		{
			fprintf(stderr, "qr: the support code not read: %s\n", qr.error.c_str());
			return out;
		}
		lay::QrPanel p = lay::qrPanel(w, h);
		out.w = w;
		out.h = h;
		out.rgba.assign((size_t)w * h * 4, 0);
		auto& v = out.rgba;
		// the panel's black, and the quiet zone's white: both opaque (the code has nothing to show through)
		for (size_t i = 0; i < (size_t)w * h; i++) v[i * 4 + 3] = 255;
		for (int y = p.quiet.y; y < p.quiet.y + p.quiet.h; y++)
			for (int x = p.quiet.x; x < p.quiet.x + p.quiet.w; x++)
			{
				uint8_t* d = &v[((size_t)y * w + x) * 4];
				d[0] = d[1] = d[2] = 255;
			}
		// the code inside it, nearest: a pixel of it is one source pixel, so no edge is smeared
		if (p.code.w > 0 && p.code.h > 0)
			for (int y = 0; y < p.code.h; y++)
			{
				int sy = (int)(((long long)(2 * y + 1) * qr.h) / (2 * p.code.h));
				for (int x = 0; x < p.code.w; x++)
				{
					int sx = (int)(((long long)(2 * x + 1) * qr.w) / (2 * p.code.w));
					const uint8_t* s = &qr.rgba[((size_t)sy * qr.w + sx) * 4];
					uint8_t* d = &v[((size_t)(p.code.y + y) * w + p.code.x + x) * 4];
					// over the quiet zone's white (a transparent corner of the file stays white)
					int a = s[3];
					for (int c = 0; c < 3; c++) d[c] = (uint8_t)((s[c] * a + 255 * (255 - a) + 127) / 255);
				}
			}
		// the address, centred under it
		if (TTF_Font* f = lineFont(fontsDir, p.line.h))
		{
			SDL_Color ink{theme::TEXT.r, theme::TEXT.g, theme::TEXT.b, 255};
			SDL_Surface* t = TTF_RenderUTF8_Blended(f, "ko-fi.com/tonysong", ink);
			TTF_CloseFont(f);
			if (t)
			{
				SDL_Surface* s = SDL_ConvertSurfaceFormat(t, SDL_PIXELFORMAT_RGBA32, 0);
				SDL_FreeSurface(t);
				if (s)
				{
					int tx = (w - s->w) / 2, ty = p.line.y + (p.line.h - s->h) / 2;
					for (int y = 0; y < s->h; y++)
						for (int x = 0; x < s->w; x++)
						{
							int dx = tx + x, dy = ty + y;
							if (dx < 0 || dy < 0 || dx >= w || dy >= h) continue;
							const uint8_t* q = (const uint8_t*)s->pixels + y * s->pitch + x * 4;
							uint8_t* d = &v[((size_t)dy * w + dx) * 4];
							int a = q[3];	// (over black: the ink's own strength)
							for (int c = 0; c < 3; c++) d[c] = (uint8_t)((q[c] * a + 127) / 255);
						}
					SDL_FreeSurface(s);
				}
			}
		}
		return out;
	}
}
