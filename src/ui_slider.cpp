// The bottom panel's sliders: one finger, one slider, as a trackpad scrubs. A touch in a slider's hit area takes it
// (slideGrab) but changes nothing; a slide moves the value on from where it was by the finger's travel along the
// track (the knob needn't be under the finger); a tap, lifted without sliding, puts it where the finger was. From
// the touch on, Ui::pointer hands every move and the lift to the slider (slideFollow), whatever is under the
// finger, so a finger that drifts off the row still holds it and nothing else reacts. The
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
		// a touch alone: nothing yet (the value as it is, the target not told)
		slide.value = sliderSnap(spec.now, spec.range);
		slide.pace.start(SDL_GetTicks(), spec.everyMs);
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
		if (!slide.moved) return;	// (a touch that hasn't slid tells nothing)
		if (slide.pace.due(slide.value, SDL_GetTicks())) slide.set(slide.value, false, p);
	}

	bool Ui::slideWaiting() const
	{
		// a paced slider whose finger rests on a value its target hasn't heard: tick() sends it when it's due
		return slide.on && slide.moved && slide.spec.everyMs && (!slide.pace.any || slide.pace.sent != slide.value);
	}

	bool Ui::slideFollow(const Pointer& pt, int x, int y, Player& p)
	{
		if (slideStale()) { slideDrop(); return false; }
		// along the track only (a finger wandering off the row across it hasn't moved the slider), and less the
		// slack: a fingertip's shift as it lifts isn't a move
		const SlideSpec& s = slide.spec;
		int finger = s.upright ? y : x;
		slide.at = sliderFollow(slide.at, finger, SLIDE_SLACK);
		if (std::abs(slide.at - slide.at0) >= DRAG_SLOP) slide.moved = true;
		if (slide.moved)
			slide.value = sliderMoveBy(s.now, slide.at - slide.at0, s.upright ? s.track.h : s.track.w, s.range, s.upright);
		else if (pt.kind == Pointer::Up)
		{
			// a tap: the value where the finger was (the knob goes there)
			slide.value = s.upright ? sliderValueAt(finger, s.track.y, s.track.h, s.range, true)
				: sliderValueAt(finger, s.track.x, s.track.w, s.range);
			slide.moved = slide.value != sliderSnap(s.now, s.range);
		}
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
		// (a touch lifted where the value already was, or let go by a key before sliding: nothing to tell)
		if (!slideStale() && set && slide.moved) set(slide.value, true, p);
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
