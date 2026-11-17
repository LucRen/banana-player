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

		// ---- tabs, and the List sheet (Y on the player: Up next / Chapters) ----
		constexpr int TAB_H = 44, TAB_GAP = 6, TAB_PAD = 13, TAB_CAP = 19;
		struct Tabs
		{
			Box area;
			Box left, right;	// the key caps at the ends ("L", "R")
			std::vector<Box> tab;
		};
		// a strip 44 tall: the left cap, each tab as wide as `widths` says, 6 apart, the right cap. The caps are
		// their own boxes (19 wide, TAB_H tall, centred in the strip)
		Tabs tabs(Box area, const std::vector<int>& widths, int gap = TAB_GAP);
		// the List sheet: the grab mark, a header (the tabs and the two controls), `rows` rows 46 tall, the hint bar
		constexpr int LIST_HEAD = 54, LIST_ROW = 46, LIST_ROWS = 7;
		struct ListSheet
		{
			int rows = 0, header = LIST_HEAD;
			Box box, grab, head, hints;
			int rowsY = 0;
			Box row(int i) const;
		};
		ListSheet listSheet(int rows, int header = LIST_HEAD);
		// the first row shown so that `cursor` is one of them, the scroll kept inside the list: what LEFT / RIGHT
		// paging and a tap leave behind
		int listScroll(int cursor, int scroll, int shown, int n);

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

		// ---- the Settings page (docs/redesign: settings, settings-group) ----
		// its groups' tiles, two to a row (58 tall, 8 apart), and a group's rows (46 tall, the focused row's help at the
		// foot): the kit's header (a player layout's back, title and crumb strip) is drawn above them, so the area
		// starts under the title at 100
		constexpr int SET_TILE_H = 58, SET_TILE_GAP = GAP, SET_TILE_TOP = 100;
		constexpr int SET_ROW_H = 46, SET_ROWS = 6, SET_ROW_TOP = 100, SET_HELP_Y = 400;
		// the i-th tile of n, in `cols` columns; the rows share `area`'s height evenly
		Box settingsTile(Box area, int n, int cols, int i);
		struct SettingsRows
		{
			Box area;
			Box row(int i) const;
			Box help;	// the focused row's line(s) above the hint bar
		};
		SettingsRows settingsRows(int shown = SET_ROWS);

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
		// where the D-pad's focus is: 0 the playing keys (nothing lit: LEFT / RIGHT seek), 2 the Next line. The chips
		// are touch only, so the D-pad never lands on them
		struct PlayerFocus
		{
			int zone = 0;
			bool operator==(const PlayerFocus& o) const { return zone == o.zone; }
		};
		// DOWN steps onto the Next line; UP comes back; LEFT / RIGHT are not the focus's
		PlayerFocus playerFocusMove(PlayerFocus f, Dir d);

		// a chip's value for a track: its language ("English"), or "Track 2" when the file names none. Two tracks of
		// the list in that language: what tells this one apart comes after it ("English · Commentary")
		std::string chipTrack(const Track& t, int n, const std::vector<Track>& all);
		// ASCII capitals, for a chip's and a line's small label ("SUBTITLES")
		std::string caps(std::string s);
	}
}
