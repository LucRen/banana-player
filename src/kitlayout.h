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
		// n cells in `cols` columns, `gap` apart, in rows that share `area`'s height (filled left to right, top to
		// bottom): the Visualizer tab's style grid (docs/redesign, "music-viz-pick")
		std::vector<Box> gridCells(Box area, int n, int cols, int gap = GAP);
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
		// a sheet whose content is not rows, but a body of its own height: the song's Equalizer tab, whose bands take
		// the full height a sheet may have (docs/redesign "music-eq"). Clamped to the sheet's 226..470.
		ListSheet listSheetBody(int body, int header = LIST_HEAD);
		// the first row shown so that `cursor` is one of them, the scroll kept inside the list: what LEFT / RIGHT
		// paging and a tap leave behind
		int listScroll(int cursor, int scroll, int shown, int n);
		// the row under a finger at `y` with the list scrolled to `scroll`: a drag's lift puts the highlight there.
		// Outside the rows it is the nearest one (above them the first shown, below them the last), and always in 0..n-1
		int rowAt(int y, int rowsY, int scroll, int shown, int n);

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
		// foot): the kit's header draws the title and the crumb beside its back key, so the area starts under that strip
		// (54) at 64, and seven rows fit above the help
		constexpr int SET_TILE_H = 58, SET_TILE_GAP = GAP, SET_TILE_TOP = 64;
		constexpr int SET_ROW_H = 46, SET_ROWS = 7, SET_ROW_TOP = 64, SET_HELP_Y = 400;
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
			Box keys[4];	// Like, Playlist, List, Stop
			int chips = 0;
			Box chip[PLAYER_CHIPS_MAX];
			Box next;	// the Next line
			Box hints;
			Box lock;	// the lock's key, while touch is locked
		};
		PlayerLayout playerLayout(int chips);

		// ---- About's support QR, large, on the top panel ----
		// The picture About's Y (or a tap on its small code) puts up, in the panel's own pixels (the top drawable's
		// size, so the code lands on whole pixels): a white quiet zone, the code inside it and the address's line under
		// it. As large as fits with a margin: the quiet zone takes what the height leaves once the margin, the gap and
		// the line are out.
		struct QrPanel
		{
			Box quiet, code, line;	// the white box; the code in it (a pad all round); the address under it
		};
		QrPanel qrPanel(int w, int h);

		// a chip's value for a track: its language ("English"), or "Track 2" when the file names none. Two tracks of
		// the list in that language: what tells this one apart comes after it ("English · Commentary")
		std::string chipTrack(const Track& t, int n, const std::vector<Track>& all);
		// ASCII capitals, for a chip's and a line's small label ("SUBTITLES")
		std::string caps(std::string s);

		// ==== the kit's variants (docs/notes/2026-11-18-kit-variants.md): status cluster, mini bar, filter chips, form row,
		// alert / confirm / busy sheets, keyboard, volume bar, tiles, cards, empty state, notice row, select and clip bars ====

		// ---- the status cluster and header badges: items right to left, `gap` apart ----
		constexpr int BADGE_H = 20, BADGE_PAD = 5;
		struct StatusRow
		{
			std::vector<int> x;	// each item's left edge (in the order given: the rightmost first)
			int left = 0;	// the cluster's left edge
		};
		StatusRow statusRow(int right, const std::vector<int>& widths, int gap = 8);
		int badgeW(int textW, bool glyph);	// a badge: padding both sides, its mark (16) and a gap before the text

		// ---- the mini bar: 52 tall, 12 from the sides, 36 above the foot ----
		constexpr int MINI_H = 52, MINI_SIDE = 12, MINI_FOOT = 36, MINI_MARK_W = 56, MINI_MARK_H = 42;
		struct MiniBar
		{
			Box box, mark, line;	// the bar; the play mark (its own touch); the 3-tall progress line along its foot
			int kindX = 0, textX = 0, rightX = 0;	// the kind icon's centre; where the title starts; the right edge of what it holds
			int cy = 0;
		};
		MiniBar miniBar();

		// ---- filter chips used as filters: 40 tall in a 56-tall touch strip ----
		constexpr int CHIP_H = 40, CHIP_STRIP = 56, CHIP_GAP = 6;
		struct FilterChips
		{
			std::vector<Box> chip, hit;	// the chip as drawn; the touch area (56 tall, up to the next one)
		};
		FilterChips filterChips(Box area, const std::vector<int>& widths);
		int filterAt(const FilterChips& c, int x, int y);	// the chip a touch is on (-1 none)

		// ---- a form row: 46 tall, the label 150 wide, then the value; two keys at the foot ----
		constexpr int FORM_ROW = 46, FORM_LABEL = 150, FORM_KEY_H = 56;
		struct FormRow
		{
			Box row, label, value;
		};
		FormRow formRow(Box row);
		void formKeys(Box out[2]);	// Test (left) and Save (right)

		// ---- the alert, confirm and busy sheets (each a lay::Sheet with a body) ----
		struct AlertSheet
		{
			Sheet sheet;
			Box text, key;	// the sentence's lines; the one primary key, full width
		};
		AlertSheet alertSheet(int textLines);
		struct ConfirmSheet
		{
			Sheet sheet;
			Box text, safe, action, foot;	// foot: the "don't ask again" line (empty without one)
		};
		ConfirmSheet confirmSheet(int textLines, bool foot);
		struct BusySheet
		{
			Sheet sheet;
			Box file, bar, amounts, key;	// the file name line, the 12-tall bar, "x of y" and the time left, Cancel
		};
		BusySheet busySheet();

		// ---- the keyboard: a field, a special row, three rows of letters, the bottom row ----
		constexpr int KB_FIELD_H = 48, KB_KEY_W = 58, KB_KEY_H = 52, KB_GAP = 5, KB_BOTTOM_H = 56, KB_SPECIAL_H = 36;
		struct Keyboard
		{
			Box field, note;
			std::vector<Box> special;	// the field's own pieces (https://, .json), as wide as their labels say
			std::vector<std::vector<Box>> rows;	// the letters, each row centred
			std::vector<Box> bottom;	// layer keys, Space, backspace, Done
		};
		// specialW: each piece's width; counts: the keys in each row of letters; layers: how many layer keys open the bottom row
		Keyboard keyboard(const std::vector<int>& specialW, const std::vector<int>& counts, int layers);

		// ---- the volume bar: the 100 mark, and where each colour ends ----
		struct VolumeBar
		{
			Box bar, number;
			int tickX = -1;	// the 100 mark (-1: the range ends at 100: nothing to mark)
		};
		VolumeBar volumeBar(Box area, int max);
		int volumeX(Box bar, int value, int max);	// the x of `value` on the bar (0..max)
		// the colour's part: 0 to 100, 1 from 100 to 120, 2 past 120
		int volumeZone(int value);

		// ---- tiles with a summary: n in `cols` columns, `h` tall, 8 apart, in `area` ----
		Box tileCell(Box area, int n, int cols, int h, int i);

		// ---- the Recent card (112 x 100) and the Continue card (108 tall) ----
		constexpr int RECENT_W = 112, RECENT_H = 100, CONTINUE_H = 108;
		struct RecentCard
		{
			Box box, pic, title, note, line;
		};
		RecentCard recentCard(Box box);
		struct ContinueCard
		{
			Box box, pic, label, title, note, bar, key;
		};
		ContinueCard continueCard(Box box);

		// ---- the empty state: centred in the page; keys 200 x 56 ----
		struct EmptyState
		{
			Box icon, title, lines, key[2];
			int keys = 0;
		};
		EmptyState emptyState(Box page, int lineCount, int keys);

		// ---- the notice row: 52 tall (60 with a second line and a key), an optional 5-tall bar and key ----
		struct Notice
		{
			Box row, icon, text, second, bar, key;
			bool hasSecond = false, hasBar = false, hasKey = false;
		};
		Notice notice(Box area, bool second, bool bar, int keyW);	// area: x, y and width; its height is the row's

		// ---- the select bar and the clip bar, at the foot of the list above the hints ----
		constexpr int SELBAR_H = 56, CLIPBAR_H = 60, CLIP_KEY_H = 46;
		void selectBar(Box out[4]);	// Playlist, Copy, Move, Delete
		struct ClipBar
		{
			Box box, text, key[2];	// "... here" (SELECT), Cancel (X)
		};
		ClipBar clipBar();
	}
}
