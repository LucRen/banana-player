// The readers' Brightness sheet (the text viewer, the PDF/comic viewer, the Manga reader): the screens' backlight as
// BananaOS sets it, Extra dim past its lowest (the pages' colours scaled down: for reading in the dark when 10% is
// still too bright), and the reader's own filter (warm, warmer, dim). Over the bottom panel; any reader opens it.
// An overlay (Ui::Overlay): openLight() pushes it, and the stack gives it the keys, the touches, its drawing and
// its part of signature().
#include "ui_internal.h"
#include "textreader.h"

namespace bp
{
	namespace
	{
		const char* LIGHT_FILTERS[DOC_FILTERS] = {"Off", "Warm", "Warmer", "Dim"};

		enum { LR_SCREENS, LR_TOP, LR_BOTTOM, LR_SAME, LR_DIM, LR_FILTER };

		// a row's height: a bar or the filters 70, the tick box 44 (6 between)
		int lightRowH(bool tickBox) { return tickBox ? 44 : 70; }

		// a row's value as 0..1 along its bar (screens 10-100%, Extra dim 0-80%)
		double lightShare(int value, int lo, int hi) { return std::clamp((value - lo) / (double)(hi - lo), 0.0, 1.0); }
	}

	int& Ui::lightFilter(Settings& s) const
	{
		// the reader's own: the text viewer's, the PDF/comic viewer's, the Manga reader's
		return page == Page::DocText ? s.txtFilter : page == Page::DocPages ? s.pdfFilter : s.mangaFilter;
	}

	std::string Ui::lightNote(const Player& p) const
	{
		Settings s = p.settings;
		int f = std::clamp(lightFilter(s), 0, DOC_FILTERS - 1), dim = s.readDim;
		std::string n = f ? LIGHT_FILTERS[f] : "";
		if (dim) n += (n.empty() ? "" : " \xC2\xB7 ") + std::string("\xE2\x88\x92") + std::to_string(dim) + "%";
		return n;
	}

	struct Ui::LightSheet : Ui::Overlay
	{
		int cursor = 0;	// the row the keys are on

		explicit LightSheet(Ui& u) : Overlay(u) {}
		const char* name() const override { return "light"; }

		std::vector<int> rows() const
		{
			std::vector<int> r;
			bool top = ui.backlight.present(Backlight::TOP), bottom = ui.backlight.present(Backlight::BOTTOM);
			if (top && bottom && ui.sameBrightness) r.push_back(LR_SCREENS);
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

		Rect box() const
		{
			int h = 92 - 6;
			for (int row : rows()) h += lightRowH(row == LR_SAME) + 6;
			return {40, (H - h) / 2 - 10, W - 80, h};
		}

		Rect rowRect(int i) const
		{
			Rect b = box();
			auto rs = rows();
			int y = b.y + 70;
			for (int j = 0; j < i && j < (int)rs.size(); j++) y += lightRowH(rs[j] == LR_SAME) + 6;
			return {b.x + 16, y, b.w - 32, i < (int)rs.size() ? lightRowH(rs[i] == LR_SAME) : 70};
		}

		Rect track(int i) const
		{
			Rect r = rowRect(i);
			return {r.x + 16, r.y + 44, r.w - 32, 12};
		}

		Rect chip(int i, int k) const
		{
			Rect r = rowRect(i);
			int w = (r.w - 32 - 3 * 8) / 4;
			return {r.x + 16 + k * (w + 8), r.y + 34, w, 30};
		}

		void same(Player& p)
		{
			// as the quick settings' box: one level for both (the bottom to the top's), or each its own; the cursor
			// stays on the box as the rows above it change
			Settings s = p.settings;
			s.sameBrightness = !s.sameBrightness;
			p.setSettings(s);
			ui.sameBrightness = s.sameBrightness;
			int pm = ui.backlight.permille(Backlight::TOP);
			if (ui.sameBrightness && pm >= 100) ui.backlight.set(Backlight::BOTTOM, pm);
			auto rs = rows();
			for (int i = 0; i < (int)rs.size(); i++)
				if (rs[i] == LR_SAME) cursor = i;
		}

		void set(int row, int value, Player& p)
		{
			Settings s = p.settings;
			switch (row)
			{
			case LR_SCREENS: case LR_TOP: case LR_BOTTOM:
			{
				int pm = std::clamp(value, 10, 100) * 10;
				if (row != LR_BOTTOM) ui.backlight.set(Backlight::TOP, pm);
				if (row != LR_TOP) ui.backlight.set(Backlight::BOTTOM, pm);
				return;
			}
			case LR_DIM: s.readDim = std::clamp(value, 0, 80) / 10 * 10; break;
			case LR_FILTER: ui.lightFilter(s) = std::clamp(value, 0, DOC_FILTERS - 1); break;
			}
			p.setSettings(s);
		}

		int value(int row, const Player& p) const
		{
			Settings s = p.settings;
			switch (row)
			{
			case LR_SCREENS: case LR_TOP: return std::max(10, (ui.backlight.permille(Backlight::TOP) + 5) / 10);
			case LR_BOTTOM: return std::max(10, (ui.backlight.permille(Backlight::BOTTOM) + 5) / 10);
			case LR_DIM: return s.readDim;
			case LR_FILTER: return ui.lightFilter(s);
			}
			return 0;
		}

		bool key(Button b, Player& p) override
		{
			auto rs = rows();
			int n = (int)rs.size();
			cursor = std::clamp(cursor, 0, n - 1);
			int row = rs[cursor];
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::Left: case Button::Right: case Button::A:
			{
				if (row == LR_SAME) { same(p); break; }
				if (b == Button::A) { close(); break; }
				int dir = b == Button::Right ? 1 : -1, v = value(row, p);
				if (row == LR_FILTER) set(row, (v + dir + DOC_FILTERS) % DOC_FILTERS, p);
				else if (row == LR_DIM) set(row, v + dir * 10, p);
				else set(row, stepBrightness(v * 10, dir) / 10, p);
				break;
			}
			case Button::B: case Button::Start: close(); break;
			default: break;
			}
			return false;
		}

		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			auto rs = rows();
			// a tap outside: closed (a bar's finger lifting out there never comes here: the slider has it)
			if (!box().hit(x, y))
			{
				if (pt.kind == Pointer::Up) close();
				return false;
			}
			for (int i = 0; i < (int)rs.size(); i++)
			{
				int row = rs[i];
				if (row == LR_FILTER)
				{
					for (int k = 0; k < DOC_FILTERS; k++)
						if (chip(i, k).hit(x, y) && pt.kind == Pointer::Up) { cursor = i; set(row, k, p); }
					continue;
				}
				if (row == LR_SAME)
				{
					if (pt.kind == Pointer::Up && rowRect(i).hit(x, y)) { same(p); return false; }
					continue;
				}
				// a bar: a touch anywhere in its row takes it, in tenths (the screens 10-100%, Extra dim 0-80%)
				if (pt.kind == Pointer::Down && rowRect(i).hit(x, y))
				{
					cursor = i;
					SliderRange range = row == LR_DIM ? SliderRange{0, 80, 10} : SliderRange{10, 100, 10};
					ui.slideGrab(SL_LIGHT, i, {track(i), range, false, 0, (double)value(row, p)}, x, y, p, [this, row](double v, bool, Player& p) {
						if ((int)v != value(row, p)) set(row, (int)v, p);
					}, this);
					return false;
				}
			}
			return false;
		}

		void draw(const Player& p) override
		{
			auto rs = rows();
			ui.drawDialog(box(), "Brightness");
			for (int i = 0; i < (int)rs.size(); i++)
			{
				int row = rs[i], v = value(row, p);
				Rect r = rowRect(i);
				bool at = i == cursor;
				if (at) ui.drawFocus(r);
				if (row == LR_SAME)
				{
					// a tick box, as the quick settings'
					int mid = r.y + r.h / 2;
					Rect tick{r.x + 16, mid - 13, 26, 26};
					ui.fillRound(tick, at ? ACCENT : TEXT, R_FIELD);
					ui.fillRound({tick.x + 2, tick.y + 2, tick.w - 4, tick.h - 4}, at ? BUTTON : SURFACE, R_FIELD - 2);
					if (p.settings.sameBrightness) ui.drawGlyphMid(G_CHECK, tick.x + tick.w / 2, tick.y + tick.h / 2, ACCENT);
					ui.drawTextMid("Same brightness for both screens", 1, at ? ACCENT : TEXT, r.x + 56, mid, r.w - 66);
					continue;
				}
				const char* label = row == LR_SCREENS ? "Screens" : row == LR_TOP ? "Top screen" : row == LR_BOTTOM ? "Bottom screen"
					: row == LR_DIM ? "Extra dim" : "Filter";
				ui.drawTextMid(label, 1, at ? ACCENT : TEXT, r.x + 16, r.y + 20, 300);
				if (row == LR_FILTER)
				{
					for (int k = 0; k < DOC_FILTERS; k++)
					{
						Rect c = chip(i, k);
						bool on = k == v;
						if (on) ui.fillRound(c, ACCENT, 8);
						else ui.drawBox(c, BUTTON, 8);
						ui.drawTextMid(LIGHT_FILTERS[k], 0, on ? ON_ACCENT : TEXT, c.x + c.w / 2, c.y + c.h / 2, c.w - 8, 2);
					}
					continue;
				}
				// the value on the right; Extra dim says what it is for when it's off
				std::string val = row == LR_DIM ? (v ? std::to_string(v) + "% darker" : "Off  \xC2\xB7  for darker than the lowest") : std::to_string(v) + "%";
				// (a bar under a finger: its value, over the bar on the right, lit; the knob grown)
				bool held = ui.slideHeld(SL_LIGHT, i);
				ui.drawTextMid(val, 0, held || (row == LR_DIM && v) ? ACCENT : DIM_ON, r.x + r.w - 16, r.y + 20, 0, 1);
				ui.drawSlider(track(i), row == LR_DIM ? lightShare(v, 0, 80) : lightShare(v, 10, 100), held);
			}
			ui.drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Close"}});
		}

		std::string signature(const Player&) const override
		{
			// the screens' levels aren't settings (Extra dim, the filter and the tick box are, and signature() has
			// those): the backlight's own
			return std::to_string(cursor) + " " + std::to_string(ui.backlight.permille(Backlight::TOP)) + " " +
				std::to_string(ui.backlight.permille(Backlight::BOTTOM));
		}
	};

	void Ui::openLight()
	{
		pushOverlay(std::make_unique<LightSheet>(*this));
	}
}
