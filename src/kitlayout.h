#pragma once
// The redesign kit's layout maths (docs/redesign/components.md), with no window: where each piece of a row, a
// sheet or the player screen goes on the bottom panel's 640x480, and how a cursor moves over them. The drawing
// (src/ui_kit.h) and the touches ask the same functions, so a tap hits what was drawn. Tests: tests/core_test.cpp.
#include <string>
#include <vector>

namespace bp
{
	struct Track;

	namespace lay
	{
		// a box on the panel, in its 640x480 units (Ui::Rect is this)
		struct Box
		{
			int x, y, w, h;
			constexpr bool hit(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
		};

		constexpr int PANEL_W = 640, PANEL_H = 480;
		constexpr int HINTS_H = 30;	// the hint bar along the foot (a rule above it)
		constexpr int MARGIN = 16, GAP = 8;	// a page's side margin; between the keys of a row

		// the i-th of n cells across `area`, `gap` apart. Widths that don't divide: the spare units go one each to the
		// last cells (five across 608: 115, 115, 115, 115, 116), so the row always ends on the area's right edge
		Box cell(Box area, int n, int gap, int i);
		// the transport row: five keys, the middle one (play) `mid` wide, the four others sharing the rest equally
		Box transportCell(Box area, int i, int gap = GAP, int mid = 176);

		// ---- the progress bar ----
		// the knob at `share` (0..1) of the track: `w` wide, `over` taller than the track above and below
		Box barKnob(Box track, double share, int w = 8, int over = 5);
		// where each chapter starts: the x of its notch. None at the very start, none closer than minGap to the last
		// one drawn, and none at all past `most` chapters (they would be a comb)
		std::vector<int> barMarks(Box track, double duration, const std::vector<double>& starts, int minGap = 6, size_t most = 100);

		// items `gap` apart, centred between left and right: each one's x. Too wide: the gap shrinks to minGap, then
		// the row starts at `left` (its end is cut by whoever draws it)
		std::vector<int> spread(const std::vector<int>& widths, int gap, int left, int right, int minGap = 6);

		// ---- the bottom sheet (the More sheet: tiles, then rows in one or two columns) ----
		constexpr int SHEET_HEAD = 54, SHEET_PAD = 14, SHEET_ROW = 46, SHEET_TILE = 60, SHEET_MIN = 226, SHEET_MAX = 470;
		struct SheetSpec
		{
			int tiles = 0;	// a first row of keys, icon over label (four across)
			int rows = 0;	// list rows under them
			int cols = 1;	// the rows' columns (filled row by row)
			int body = 0;	// other content's height, between the header and the tiles
		};
		struct Sheet
		{
			SheetSpec spec;
			Box box, grab, head, hints;
			int bodyY = 0, tilesY = 0, rowsY = 0;
			Box tile(int i) const;
			Box row(int i) const;
			int count() const { return spec.tiles + spec.rows; }
			// the cursor's item: the tiles first, then the rows
			Box item(int i) const { return i < spec.tiles ? tile(i) : row(i - spec.tiles); }
		};
		// full width, from the foot, as tall as its content (SHEET_MIN at least, SHEET_MAX at most)
		Sheet sheet(const SheetSpec& s);
		enum class Dir { Up, Down, Left, Right };
		// the cursor moved over the tiles and the rows (two columns: LEFT / RIGHT cross). No wrap: at an edge it stays
		int sheetMove(const SheetSpec& s, int cursor, Dir d);

		// ---- the player screen ----
		constexpr int PLAYER_CHIPS_MAX = 5;
		struct PlayerLayout
		{
			Box back, backHit, more, moreHit;	// the header's keys; their touch areas reach the panel's edges
			Box title;	// the title, and how it plays on its right
			Box line, lineHit;	// the chapter line (a key), or a song's artist and album
			Box bar, barHit, times;
			Box transport[5];	// Prev, back 5 s, Play, forward 10 s, Next
			Box keys[4];	// Like, Playlist, List, View
			int chips = 0;
			Box chip[PLAYER_CHIPS_MAX];
			Box next;	// the Next line
			Box hints;
			Box lock;	// the lock's key, while touch is locked
		};
		PlayerLayout playerLayout(int chips);
		// where the D-pad's focus is: 0 the playing keys (nothing lit: LEFT / RIGHT seek), 1 a chip, 2 the Next line
		struct PlayerFocus
		{
			int zone = 0, chip = 0;
			bool operator==(const PlayerFocus& o) const { return zone == o.zone && chip == o.chip; }
		};
		// DOWN steps onto the chips, then the Next line (when shown); UP comes back; LEFT / RIGHT choose a chip (they
		// are not the focus's while nothing is lit, nor on the Next line)
		PlayerFocus playerFocusMove(PlayerFocus f, Dir d, int chips, bool next);

		// a chip's value for a track: its language ("English"), or "Track 2" when the file names none. Two tracks of
		// the list in that language: what tells this one apart comes after it ("English · Commentary")
		std::string chipTrack(const Track& t, int n, const std::vector<Track>& all);
		// ASCII capitals, for a chip's and a line's small label ("SUBTITLES")
		std::string caps(std::string s);
	}
}
