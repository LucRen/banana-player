#pragma once
// About's support QR (packaging/support/kofi-qr.png: ko-fi.com/tonysong), large on the top panel. About's Y, or a
// tap on the small code at its foot, puts it up; the panel is the Ui's own picture there, as it is under a reader's
// pages (topscreen.h draws it with its PageRenderer, 1:1, nothing of mpv's). The picture is composed at the panel's
// own pixels, the code scaled nearest so every edge stays the picture's: a white quiet zone round it, black around
// that, and the address written under it. No thread: composed once per panel size, in the top window's context.
#include <cstdint>
#include <string>
#include <vector>

namespace bp
{
	struct SupportQrImage
	{
		int w = 0, h = 0;
		std::vector<uint8_t> rgba;	// w * h * 4, straight alpha, top row first
		bool ok() const { return w > 0 && h > 0 && rgba.size() == (size_t)w * (size_t)h * 4; }
	};
	// the picture at a panel w x h pixels; the code is read where About's small one is (the bundle's, beside the
	// fonts' folder, then a source build's packaging/support)
	SupportQrImage composeSupportQr(const std::string& fontsDir, int w, int h);
}
