// The bottom panel's sliders: one finger, one slider. A touch in a slider's hit area grabs it (slideGrab) and the
// value jumps to the finger; from then on Ui::pointer hands every move and the lift to the slider (slideFollow),
// whatever is under the finger, so a finger that drifts off the row still holds it and nothing else reacts. The
// slider tells its target through the setter it was grabbed with: every change while held (or, for a costly
// target, one each `everyMs` at most), and the last value once more as the finger lifts (last = true).
// The arithmetic is slider.h's. docs/notes/sliders.md: how to add one, and how a held one must look.
#include "ui_internal.h"

namespace bp
{
	void Ui::slideGrab(int id, int sub, const SlideSpec& spec, int x, int y, Player& p, SlideSet set, const Overlay* owner)
	{
		slide = Slide();
		slide.on = true;
		slide.id = id;
		slide.sub = sub;
		slide.spec = spec;
		slide.set = std::move(set);
		slide.owner = owner;
		slide.page = page;
		slide.at0 = slide.at = spec.upright ? y : x;
		slide.value = spec.upright ? sliderValueAt(y, spec.track.y, spec.track.h, spec.range, true)
			: sliderValueAt(x, spec.track.x, spec.track.w, spec.range);
		slide.pace.start(SDL_GetTicks(), spec.everyMs);
		slideLive(p);
	}

	bool Ui::slideStale() const
	{
		// what the slider was on has gone from under the finger (a page left from the web, a dialog come up over it,
		// its own sheet closed): the setter isn't called again
		return slide.page != page || topOverlay() != slide.owner;
	}

	void Ui::slideLive(Player& p)
	{
		if (!slide.on) return;
		if (slideStale()) { slideDrop(); return; }
		if (slide.pace.due(slide.value, SDL_GetTicks())) slide.set(slide.value, false, p);
	}

	bool Ui::slideWaiting() const
	{
		// a paced slider whose finger rests on a value its target hasn't heard: tick() sends it when it's due
		return slide.on && slide.spec.everyMs && (!slide.pace.any || slide.pace.sent != slide.value);
	}

	bool Ui::slideFollow(const Pointer& pt, int x, int y, Player& p)
	{
		if (slideStale()) { slideDrop(); return false; }
		// along the track only (a finger wandering off the row across it hasn't moved the slider), and less the
		// slack: a fingertip's shift as it lifts isn't a move
		const SlideSpec& s = slide.spec;
		slide.at = sliderFollow(slide.at, s.upright ? y : x, SLIDE_SLACK);
		if (std::abs(slide.at - slide.at0) >= DRAG_SLOP) slide.moved = true;
		slide.value = s.upright ? sliderValueAt(slide.at, s.track.y, s.track.h, s.range, true)
			: sliderValueAt(slide.at, s.track.x, s.track.w, s.range);
		if (pt.kind == Pointer::Up) slideEnd(p);
		else slideLive(p);
		return false;
	}

	void Ui::slideEnd(Player& p)
	{
		if (!slide.on) return;
		// let go first: the setter sees no slider held (and may grab, close or open what it likes)
		slide.on = false;
		SlideSet set = std::move(slide.set);
		slide.set = nullptr;
		if (!slideStale() && set) set(slide.value, true, p);
	}

	void Ui::slideDrop()
	{
		slide.on = false;
		slide.set = nullptr;
	}

	void Ui::drawSlider(Rect track, double share, bool held, const std::string& tag)
	{
		// the track, its fill to the value, and the knob there (8 taller than the track)
		int w = (int)std::lround(track.w * share);
		drawLevel(track, w, TRACK, ACCENT);
		Rect knob{track.x + w - 5, track.y - 4, 10, track.h + 8};
		if (!held) { fillRound(knob, KNOB, R_KNOB); return; }
		drawSlideKnob(knob);
		if (!tag.empty()) drawSlideTag(track, share, tag);
	}

	void Ui::drawSlideKnob(Rect k)
	{
		// held: grown by 3 all round, a dark rim keeping it clear of the fill and of what it passes over
		fillRound({k.x - 5, k.y - 5, k.w + 10, k.h + 10}, BG, R_KNOB + 3);
		fillRound({k.x - 3, k.y - 3, k.w + 6, k.h + 6}, KNOB, R_KNOB + 2);
	}

	void Ui::drawSlideTag(Rect track, double share, const std::string& text)
	{
		// the value over the knob, far enough up to clear the fingertip on it: an accent chip, kept inside the panel
		int tw = (int)std::lround(textWidth(text, 0) / sx) + 20, h = 26;
		int cx = track.x + (int)std::lround(track.w * share);
		Rect r{std::clamp(cx - tw / 2, 4, W - 4 - tw), track.y - 28 - h, tw, h};
		fillRound({r.x - 2, r.y - 2, r.w + 4, r.h + 4}, BG, 10);
		fillRound(r, ACCENT, 8);
		drawTextMid(text, 0, ON_ACCENT, r.x + r.w / 2, r.y + r.h / 2, 0, 2);
	}
}
