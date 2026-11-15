// The level card (Volume, Brightness, Browser volume; the manga reader's zoom) over the top panel, as one picture.
// Every top-panel screen shows this same picture: mpv's (video, music, a photo, the idle screen) as an image overlay
// (Player::showLevel), the manga and text readers' as a card over their pages (CardOverlay in pagerender.h). One
// drawing, so the screens can't drift apart. Main thread only (its font is opened once and kept).
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace bp
{
	struct LevelCardImage
	{
		std::shared_ptr<std::vector<uint8_t>> rgba;	// straight alpha, w*h*4
		int w = 0, h = 0;
		bool ok() const { return rgba && w > 0 && h > 0; }
	};

	// the bar's whole for a label: the volume's is the Max volume setting (120, 150: a mark at 100, louder in
	// colour), everything else's 100
	int levelTop(const std::string& label, int volumeMax);
	// the card at a panel panelH pixels high (its proportions are a 960x720 canvas's). top <= 0: the label alone,
	// without the bar or a percent (the reader's zoom)
	LevelCardImage drawLevelCard(const std::string& fontsDir, int panelH, const std::string& label, int percent, int top);
	// where its middle goes, as a fraction of the panel's height: the middle over what plays; lower with nothing
	// playing (under the idle screen's icon and name) and over a reader's pages (the text being read stays clear)
	float levelCardMiddle(bool playing);
	// mpv's overlay-add takes premultiplied BGRA
	std::vector<uint8_t> premultipliedBgra(const LevelCardImage& card);
}
