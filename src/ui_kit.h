// The redesign's component kit (docs/redesign/components.md): the pieces the redesigned screens are drawn from, each
// written once. A component is a function of Ui::Kit that takes a Rect and a small struct of inputs and draws; it
// keeps nothing, so the screen's state stays the screen's (its area's struct), and the Rect it is given is the one
// the screen's touches test (a row or a sheet's Rects come from src/kitlayout.h, which has no window and is tested
// in tests/core_test.cpp). Built on Ui's primitives (fillRound, drawBox, drawTextMid, the glyphs of ui_glyphs.h) and
// theme.h's tokens; type sizes are ui_internal.h's F_* (the mock-ups' Pixelify Sans and Inter sizes).
// How to build a screen with it: docs/notes/2026-11-16-redesign-kit.md.
//
//   caps / capsW   key caps: a button's name in a yellow outline ("A", "L2/R2", "LEFT/RIGHT" as arrows)
//   hints          the hint bar along the foot: caps, then what they do
//   key            a key (button): icon, label and cap side by side (56 tall), the cap under them (60 and taller),
//                  or icon over label (a sheet's tile); normal, primary (yellow), danger (red ink), focused, off
//   transport      a transport key (74 tall): icon over label-with-cap; the play key's hold fill and third line
//   iconKey        a key that is only a mark (the header's back and More)
//   chip           a state chip: icon, a small label in capitals, the value under it; yellow with a dot when the
//                  value is away from normal
//   line           a line key: a full-width key that reads as a line (the chapter line, the Next line)
//   progress       the 14-tall bar with a knob and chapter marks (a slider: its knob grows and shows a tag when held)
//   bar            a progress bar that isn't touched (3, 6 or 12 tall)
//   sheet          a bottom sheet's body: the veil, the box, the grab mark, the header (lay::sheet gives its Rects)
//   row            a list row: lead icon, name, pill, note, and a cap, a chevron or a switch at its end
//   pill, toggle   a mark on a row; a switch (never the touch target: its row is)
//   lock           the lock over a screen whose touch is locked
//   flat           a box with an edge and no lip (SURFACE pieces: chips, lines)
// Unavailable ("off") is one rule everywhere: the piece at theme::OFF percent, nothing else changed.
#pragma once
#include "ui_internal.h"
#include <functional>

namespace bp
{
	struct Ui::Kit
	{
		Ui& ui;
		explicit Kit(Ui& u) : ui(u) {}

		int textW(const std::string& s, int font) const;	// in 640x480 units
		static SDL_Color off(SDL_Color c, bool isOff = true);	// c as an unavailable piece shows it
		static SDL_Color edgeOf(SDL_Color fill);
		void flat(Rect r, SDL_Color fill, int rad, SDL_Color edge);

		// ---- key caps and the hint bar ----
		// keys: one name or several apart by '/' ("L2/R2"); LEFT, RIGHT, UP, DOWN are drawn as arrows. ground: what
		// the cap is drawn on (its inside is that colour). Returns the width drawn
		int capsW(const std::string& keys) const;
		int caps(const std::string& keys, int x, int cy, SDL_Color ink = ACCENT, SDL_Color ground = BG);
		void hints(const std::vector<Hint>& list);	// (Ui::Hint: key, verb)

		// ---- keys ----
		enum class Tone { Normal, Primary, Danger };
		struct Key
		{
			const Glyph* glyph = nullptr;
			std::string label, key;	// key: the button that does the same ("" none shown)
			Tone tone = Tone::Normal;
			bool pressed = false, focused = false, off = false;
			bool lit = false;	// the icon and label in yellow (on: the heart once liked)
			bool stack = false;	// icon over label (a sheet's tile)
			double glow = 0;	// 0..1: a soft yellow swell over the key (it asks to be noticed)
		};
		void key(Rect r, const Key& k);
		struct Transport
		{
			const Glyph* glyph = nullptr;
			std::string label, key;
			bool primary = false, pressed = false, off = false;
			double hold = -1;	// 0..1: held this far (a darker fill growing left to right); -1: not held
			std::string third;	// a third line ("Hold: play it here")
		};
		void transport(Rect r, const Transport& t);
		void iconKey(Rect r, const Glyph& g, bool pressed, int cell = 2);

		// ---- state chips and line keys ----
		struct Chip
		{
			const Glyph* glyph = nullptr;
			std::string label, value;
			bool away = false;	// the value isn't the normal one: yellow, with a dot
			bool off = false, focused = false, pressed = false;
		};
		void chip(Rect r, const Chip& c);
		struct Line
		{
			std::string label;	// a small label in capitals before the text ("NEXT")
			const Glyph* glyph = nullptr;
			std::string text, note, keys;	// note: dim, on the right; keys: caps before the chevron
			bool chevron = true, pressed = false, focused = false;
			bool dim = false;	// the text dim (nothing to name)
			int font = F_P16;
		};
		void line(Rect r, const Line& l);

		// ---- bars ----
		// marks: lay::barMarks; held: under a finger (Ui::slideHeld), tag: the value over the knob then
		void progress(Rect track, double share, const std::vector<int>& marks, bool held = false, const std::string& tag = "");
		void bar(Rect r, double share);

		// ---- sheets and rows ----
		struct SheetHead
		{
			const Glyph* glyph = nullptr;
			std::string title, note;
			bool danger = false;
		};
		void sheet(const lay::Sheet& s, const SheetHead& h);
		enum class Pill { None, Live, Fact, Bad };	// yellow: live or wants attention; neutral: a fact; red: failed
		enum class Trail { None, Chevron, SwitchOff, SwitchOn };
		struct Row
		{
			const Glyph* glyph = nullptr;
			std::string name, note, key, pill;
			Pill tone = Pill::Live;
			Trail trail = Trail::None;
			bool focused = false, pressed = false, off = false, danger = false, lit = false;
			int font = F_SMALL;	// the name's (a sheet's two-column rows: F_P16)
			SDL_Color under = SURFACE;	// what the row lies on: a sheet (a page: BG)
		};
		void row(Rect r, const Row& row);
		int pill(int right, int cy, const std::string& text, Pill tone = Pill::Live);	// its width
		void toggle(int x, int cy, bool on);	// 40 x 22, its left edge at x

		// ---- the lock over a screen ----
		void lock(Rect key, double held);	// held 0..1: the hold that unlocks, filling
	};

	// ---- the More sheet's list (Ui::openMore; ui_more.cpp) ----
	struct Ui::MoreItem
	{
		const Glyph* glyph = nullptr;
		std::string label, note;
		std::string key;	// a row's cap ("L3": that button chooses it while the sheet is open)
		std::function<bool(Player&)> act;	// what choosing it does; true: leave the player (as Ui::press says)
		bool danger = false, off = false, lit = false;
		bool stay = false;	// the sheet stays open (the row says what it is now: Like)
	};
	struct Ui::MoreSpec
	{
		const Glyph* glyph = nullptr;
		std::string title, note;
		std::vector<MoreItem> tiles, rows;	// a first row of keys (four across), then the list
		int cols = 1;	// the list's columns
	};
}
