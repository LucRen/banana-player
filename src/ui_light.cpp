// The readers' Brightness sheet (the text viewer, the PDF/comic viewer, the Manga reader): the screens' backlight as
// BananaOS sets it, Extra dim past its lowest (the pages' colours scaled down: for reading in the dark when 10% is
// still too bright), and the reader's own filter (warm, warmer, dim). Over the bottom panel; any reader opens it.
#include "ui_internal.h"
#include "textreader.h"

namespace bp
{
	namespace
	{
		const char* LIGHT_FILTERS[DOC_FILTERS] = {"Off", "Warm", "Warmer", "Dim"};
	}

	std::vector<int> Ui::lightRows() const
	{
		std::vector<int> r;
		bool top = backlight.present(Backlight::TOP), bottom = backlight.present(Backlight::BOTTOM);
		if (top && bottom && sameBrightness) r.push_back(LR_SCREENS);
		else
		{
			if (top) r.push_back(LR_TOP);
			if (bottom) r.push_back(LR_BOTTOM);
		}
		if (top && bottom) r.push_back(LR_SAME);	// the tick box, as the quick settings' (one level, or each its own)
		r.push_back(LR_DIM);
		r.push_back(LR_FILTER);
		return r;
	}

	int& Ui::lightFilter(Settings& s) const
	{
		// the reader's own: the text viewer's, the PDF/comic viewer's, the Manga reader's
		return page == Page::DocText ? s.txtFilter : page == Page::DocPages ? s.pdfFilter : s.mangaFilter;
	}

	void Ui::openLight()
	{
		lightOpen = true;
		lightCursor = 0;
		lightDrag = -1;
	}

	// a row's height: a bar or the filters 70, the tick box 44 (6 between)
	static int lightRowH(bool tickBox) { return tickBox ? 44 : 70; }

	Ui::Rect Ui::lightBox() const
	{
		int h = 92 - 6;
		for (int row : lightRows()) h += lightRowH(row == LR_SAME) + 6;
		return {40, (H - h) / 2 - 10, W - 80, h};
	}

	Ui::Rect Ui::lightRow(int i) const
	{
		Rect b = lightBox();
		auto rows = lightRows();
		int y = b.y + 70;
		for (int j = 0; j < i && j < (int)rows.size(); j++) y += lightRowH(rows[j] == LR_SAME) + 6;
		return {b.x + 16, y, b.w - 32, i < (int)rows.size() ? lightRowH(rows[i] == LR_SAME) : 70};
	}

	void Ui::lightSame(Player& p)
	{
		// as the quick settings' box: one level for both (the bottom to the top's), or each its own; the cursor
		// stays on the box as the rows above it change
		Settings s = p.settings;
		s.sameBrightness = !s.sameBrightness;
		p.setSettings(s);
		sameBrightness = s.sameBrightness;
		int pm = backlight.permille(Backlight::TOP);
		if (sameBrightness && pm >= 100) backlight.set(Backlight::BOTTOM, pm);
		auto rows = lightRows();
		for (int i = 0; i < (int)rows.size(); i++)
			if (rows[i] == LR_SAME) lightCursor = i;
	}

	Ui::Rect Ui::lightTrack(int i) const
	{
		Rect r = lightRow(i);
		return {r.x + 16, r.y + 44, r.w - 32, 12};
	}

	Ui::Rect Ui::lightChip(int i, int k) const
	{
		Rect r = lightRow(i);
		int w = (r.w - 32 - 3 * 8) / 4;
		return {r.x + 16 + k * (w + 8), r.y + 34, w, 30};
	}

	// a row's value as 0..1 along its bar (screens 10-100%, Extra dim 0-80%)
	static double lightShare(int value, int lo, int hi) { return std::clamp((value - lo) / (double)(hi - lo), 0.0, 1.0); }

	void Ui::lightSet(int row, int value, Player& p)
	{
		Settings s = p.settings;
		switch (row)
		{
		case LR_SCREENS: case LR_TOP: case LR_BOTTOM:
		{
			int pm = std::clamp(value, 10, 100) * 10;
			if (row != LR_BOTTOM) backlight.set(Backlight::TOP, pm);
			if (row != LR_TOP) backlight.set(Backlight::BOTTOM, pm);
			return;
		}
		case LR_DIM: s.readDim = std::clamp(value, 0, 80) / 10 * 10; break;
		case LR_FILTER: lightFilter(s) = std::clamp(value, 0, DOC_FILTERS - 1); break;
		}
		p.setSettings(s);
	}

	int Ui::lightValue(int row, const Player& p) const
	{
		Settings s = p.settings;
		switch (row)
		{
		case LR_SCREENS: case LR_TOP: return std::max(10, (backlight.permille(Backlight::TOP) + 5) / 10);
		case LR_BOTTOM: return std::max(10, (backlight.permille(Backlight::BOTTOM) + 5) / 10);
		case LR_DIM: return s.readDim;
		case LR_FILTER: return lightFilter(s);
		}
		return 0;
	}

	std::string Ui::lightNote(const Player& p) const
	{
		Settings s = p.settings;
		int f = std::clamp(lightFilter(s), 0, DOC_FILTERS - 1), dim = s.readDim;
		std::string n = f ? LIGHT_FILTERS[f] : "";
		if (dim) n += (n.empty() ? "" : " \xC2\xB7 ") + std::string("\xE2\x88\x92") + std::to_string(dim) + "%";
		return n;
	}

	bool Ui::lightKey(Button b, Player& p)
	{
		auto rows = lightRows();
		int n = (int)rows.size();
		lightCursor = std::clamp(lightCursor, 0, n - 1);
		int row = rows[lightCursor];
		switch (b)
		{
		case Button::Up: lightCursor = (lightCursor + n - 1) % n; break;
		case Button::Down: lightCursor = (lightCursor + 1) % n; break;
		case Button::Left: case Button::Right: case Button::A:
		{
			if (row == LR_SAME) { lightSame(p); break; }
			if (b == Button::A) { lightOpen = false; break; }
			int dir = b == Button::Right ? 1 : -1, v = lightValue(row, p);
			if (row == LR_FILTER) lightSet(row, (v + dir + DOC_FILTERS) % DOC_FILTERS, p);
			else if (row == LR_DIM) lightSet(row, v + dir * 10, p);
			else lightSet(row, stepBrightness(v * 10, dir) / 10, p);
			break;
		}
		case Button::B: case Button::Start: lightOpen = false; break;
		default: break;
		}
		return false;
	}

	bool Ui::lightPointer(const Pointer& pt, int x, int y, Player& p)
	{
		auto rows = lightRows();
		if (pt.kind == Pointer::Up) { int was = lightDrag; lightDrag = -1; if (was >= 0) return false; }
		// a tap outside: closed
		if (!lightBox().hit(x, y))
		{
			if (pt.kind == Pointer::Up && lightDrag < 0) lightOpen = false;
			return false;
		}
		for (int i = 0; i < (int)rows.size(); i++)
		{
			int row = rows[i];
			if (row == LR_FILTER)
			{
				for (int k = 0; k < DOC_FILTERS; k++)
					if (lightChip(i, k).hit(x, y) && pt.kind == Pointer::Up) { lightCursor = i; lightSet(row, k, p); }
				continue;
			}
			if (row == LR_SAME)
			{
				if (pt.kind == Pointer::Up && lightRow(i).hit(x, y)) { lightSame(p); return false; }
				continue;
			}
			// a bar: a touch or a drag along it sets it (the drag kept to its row once begun)
			Rect r = lightRow(i), tr = lightTrack(i);
			bool inRow = r.hit(x, y);
			if (pt.kind == Pointer::Down && inRow) lightDrag = i;
			if ((pt.kind == Pointer::Down || pt.kind == Pointer::Move) && lightDrag == i)
			{
				lightCursor = i;
				double f = std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0);
				int v = row == LR_DIM ? (int)std::lround(f * 8) * 10 : 10 + (int)std::lround(f * 9) * 10;
				if (v != lightValue(row, p)) lightSet(row, v, p);
			}
		}
		return false;
	}

	void Ui::drawLight(const Player& p)
	{
		auto rows = lightRows();
		Rect box = lightBox();
		drawDialog(box, "Brightness");
		for (int i = 0; i < (int)rows.size(); i++)
		{
			int row = rows[i], v = lightValue(row, p);
			Rect r = lightRow(i);
			bool at = i == lightCursor;
			if (at) drawFocus(r);
			if (row == LR_SAME)
			{
				// a tick box, as the quick settings'
				int mid = r.y + r.h / 2;
				Rect box{r.x + 16, mid - 13, 26, 26};
				fillRound(box, at ? ACCENT : TEXT, R_FIELD);
				fillRound({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, at ? BUTTON : SURFACE, R_FIELD - 2);
				if (p.settings.sameBrightness) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, ACCENT);
				drawTextMid("Same brightness for both screens", 1, at ? ACCENT : TEXT, r.x + 56, mid, r.w - 66);
				continue;
			}
			const char* label = row == LR_SCREENS ? "Screens" : row == LR_TOP ? "Top screen" : row == LR_BOTTOM ? "Bottom screen"
				: row == LR_DIM ? "Extra dim" : "Filter";
			drawTextMid(label, 1, at ? ACCENT : TEXT, r.x + 16, r.y + 20, 300);
			if (row == LR_FILTER)
			{
				for (int k = 0; k < DOC_FILTERS; k++)
				{
					Rect c = lightChip(i, k);
					bool on = k == v;
					if (on) fillRound(c, ACCENT, 8);
					else drawBox(c, BUTTON, 8);
					drawTextMid(LIGHT_FILTERS[k], 0, on ? ON_ACCENT : TEXT, c.x + c.w / 2, c.y + c.h / 2, c.w - 8, 2);
				}
				continue;
			}
			// the value on the right; Extra dim says what it is for when it's off
			std::string val = row == LR_DIM ? (v ? std::to_string(v) + "% darker" : "Off  \xC2\xB7  for darker than the lowest") : std::to_string(v) + "%";
			drawTextMid(val, 0, row == LR_DIM && v ? ACCENT : DIM_ON, r.x + r.w - 16, r.y + 20, 0, 1);
			Rect tr = lightTrack(i);
			double f = row == LR_DIM ? lightShare(v, 0, 80) : lightShare(v, 10, 100);
			int w = (int)std::lround(tr.w * f);
			drawLevel(tr, w, TRACK, ACCENT);
			fillRound({tr.x + w - 5, tr.y - 4, 10, 20}, KNOB, R_KNOB);
		}
		drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Close"}});
	}
}
