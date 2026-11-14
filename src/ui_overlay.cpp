// The overlay stack: the dialogs and sheets that show over a page and take its input (Ui::Overlay in ui.h). One
// that's open gets the keys and the touches (the topmost only), is drawn over the page (all of them, the oldest
// first) and has its state in signature(), from here: a new dialog is one class and the line that pushes it.
// (docs/notes/overlay-stack.md: how to add one, and how the older dialogs would move over.)
// Also here: the message dialog (Ui::alert), the smallest overlay there is.
#include "ui_internal.h"

namespace bp
{
	Ui::Overlay* Ui::topOverlay() const
	{
		for (auto it = overlays.rbegin(); it != overlays.rend(); ++it)
			if (!(*it)->done) return it->get();
		return nullptr;
	}

	Ui::Overlay* Ui::findOverlay(const char* name) const
	{
		for (auto& o : overlays)
			if (!o->done && !strcmp(o->name(), name)) return o.get();
		return nullptr;
	}

	void Ui::pushOverlay(std::unique_ptr<Overlay> o)
	{
		// one of a kind: opened again, the new one takes the old one's place, on top. The old one is only marked (it
		// may be the one whose key() is running: an overlay that opens its own kind again)
		if (Overlay* old = findOverlay(o->name())) old->done = true;
		overlays.push_back(std::move(o));
	}

	void Ui::dropClosedOverlays()
	{
		overlays.erase(std::remove_if(overlays.begin(), overlays.end(), [](const std::unique_ptr<Overlay>& o) { return o->done; }),
			overlays.end());
	}

	bool Ui::overlayKey(Button b, Player& p)
	{
		// (the pointer is kept: key() may push another, which moves the vector's own storage, not the overlays)
		Overlay* top = topOverlay();
		bool leave = top->key(b, p);
		dropClosedOverlays();
		return leave;
	}

	bool Ui::overlayPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Overlay* top = topOverlay();
		bool leave = top->pointer(pt, x, y, p);
		dropClosedOverlays();
		return leave;
	}

	void Ui::drawOverlays(const Player& p)
	{
		for (auto& o : overlays)
			if (!o->done) o->draw(p);
	}

	std::string Ui::overlaySignature(const Player& p) const
	{
		// each one's name and state, in the stack's order (the order is drawn too)
		std::string s = "|ov";
		for (auto& o : overlays)
			if (!o->done) s += std::string("|") + o->name() + ":" + o->signature(p);
		return s;
	}

	// A message that needs reading: its title, up to four lines, OK. A or B or a tap anywhere closes it (nothing
	// else, so a press meant for the page isn't lost on it).
	struct Ui::AlertBox : Ui::Overlay
	{
		std::string title, text;

		AlertBox(Ui& u, const std::string& title, const std::string& text) : Overlay(u), title(title), text(text) {}
		const char* name() const override { return "alert"; }

		Rect box() const
		{
			// as tall as its words
			int lines = text.empty() ? 0 : (int)ui.wrapText(text, 0, 448 - 56, 4).size();
			int h = 150 + lines * 24;
			return {96, (H - h) / 2 - 10, 448, h};
		}

		Rect okRect() const
		{
			Rect b = box();
			return {W / 2 - 80, b.y + b.h - 66, 160, 46};
		}

		bool key(Button b, Player&) override
		{
			if (b == Button::A || b == Button::B) close();
			return false;
		}

		bool pointer(const Pointer& pt, int, int, Player&) override
		{
			if (pt.kind == Pointer::Up) close();	// OK, or a tap anywhere
			return false;
		}

		void draw(const Player&) override
		{
			Rect b = box();
			ui.drawDialog(b, "");
			ui.drawGlyphMid(G_ALERT, b.x + 40, b.y + 38, ACCENT);
			ui.drawTextMid(title, 1, TEXT, b.x + 64, b.y + 38, b.w - 88);
			auto lines = ui.wrapText(text, 0, b.w - 56, 4);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, DIM_ON, b.x + 28, b.y + 80 + (int)i * 24, b.w - 56);
			Rect ok = okRect();
			ui.drawBox(ok, ACCENT, R_BUTTON);
			ui.drawTextMid("OK", 1, ON_ACCENT, ok.x + ok.w / 2, ok.y + ok.h / 2, ok.w - 16, 2);
			ui.drawHints({{"A", "OK"}, {"B", "Close"}});
		}

		std::string signature(const Player&) const override { return title + "\n" + text; }
	};

	void Ui::alert(const std::string& title, const std::string& text)
	{
		pushOverlay(std::make_unique<AlertBox>(*this, title, text));
		fprintf(stderr, "ui: said \"%s\"%s%s\n", title.c_str(), text.empty() ? "" : ": ", text.c_str());
	}
}
