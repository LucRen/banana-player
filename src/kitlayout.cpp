#include "kitlayout.h"
#include "tracks.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	namespace lay
	{
		Box cell(Box area, int n, int gap, int i)
		{
			if (n <= 0) return {area.x, area.y, 0, area.h};
			int room = area.w - (n - 1) * gap, w = room / n, spare = room - w * n;
			// the spare units widen the last `spare` cells by one each
			int wider = std::max(0, i - (n - spare));
			return {area.x + i * (w + gap) + wider, area.y, w + (i >= n - spare ? 1 : 0), area.h};
		}

		Box transportCell(Box area, int i, int gap, int mid)
		{
			int side = (area.w - mid - 4 * gap) / 4;
			// (what four equal sides leave over goes to the play key, so the row ends on the area's edge)
			int play = area.w - 4 * gap - 4 * side;
			int x = area.x;
			for (int k = 0; k < i; k++) x += (k == 2 ? play : side) + gap;
			return {x, area.y, i == 2 ? play : side, area.h};
		}

		Box barKnob(Box track, double share, int w, int over)
		{
			int at = track.x + (int)std::lround(track.w * std::clamp(share, 0.0, 1.0));
			return {at - w / 2, track.y - over, w, track.h + 2 * over};
		}

		std::vector<int> barMarks(Box track, double duration, const std::vector<double>& starts, int minGap, size_t most)
		{
			std::vector<int> out;
			if (duration <= 0 || starts.size() > most) return out;
			int last = track.x - 1000;
			for (double t : starts)
			{
				if (t <= 0 || t >= duration) continue;
				int x = track.x + (int)(track.w * (t / duration));
				if (x - last < minGap) continue;
				out.push_back(x);
				last = x;
			}
			return out;
		}

		std::vector<int> spread(const std::vector<int>& widths, int gap, int left, int right, int minGap)
		{
			std::vector<int> xs;
			int n = (int)widths.size(), total = 0;
			for (int w : widths) total += w;
			if (n > 1 && total + gap * (n - 1) > right - left) gap = std::max(minGap, (right - left - total) / (n - 1));
			int all = total + gap * std::max(0, n - 1);
			int x = std::max(left, left + (right - left - all) / 2);
			for (int w : widths)
			{
				xs.push_back(x);
				x += w + gap;
			}
			return xs;
		}

		// ---- tabs, and the List sheet ----

		Tabs tabs(Box area, const std::vector<int>& widths, int gap)
		{
			Tabs out;
			out.area = area;
			// the strip 44 tall in the header it is given (54), its caps 19 tall in the strip's middle
			int y = area.y + (area.h - TAB_H) / 2, cy = y + (TAB_H - TAB_CAP) / 2;
			out.left = {area.x, cy, TAB_CAP, TAB_CAP};
			int x = area.x + TAB_CAP + (widths.empty() ? 0 : gap);
			for (int w : widths)
			{
				out.tab.push_back({x, y, w, TAB_H});
				x += w + gap;
			}
			out.right = {widths.empty() ? area.x + TAB_CAP + gap : x, cy, TAB_CAP, TAB_CAP};
			return out;
		}

		ListSheet listSheet(int rows, int header)
		{
			ListSheet out;
			out.rows = std::max(0, rows);
			out.header = std::max(0, header);
			// the grab mark's 11, the header, the rows, 8 clear of the hint bar
			int h = 11 + out.header + out.rows * LIST_ROW + 8 + HINTS_H;
			out.box = {0, PANEL_H - h, PANEL_W, h};
			out.grab = {PANEL_W / 2 - 22, out.box.y + 7, 44, 4};
			out.head = {SHEET_PAD, out.box.y + 11, PANEL_W - 2 * SHEET_PAD, out.header};
			out.rowsY = out.head.y + out.header;
			out.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
			return out;
		}

		Box ListSheet::row(int i) const { return {SHEET_PAD, rowsY + i * LIST_ROW, PANEL_W - 2 * SHEET_PAD, LIST_ROW}; }

		int listScroll(int cursor, int scroll, int shown, int n)
		{
			if (n <= 0 || shown <= 0) return 0;
			cursor = std::clamp(cursor, 0, n - 1);
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + shown) scroll = cursor - shown + 1;
			return std::clamp(scroll, 0, std::max(0, n - shown));
		}

		Sheet sheet(const SheetSpec& s)
		{
			Sheet out;
			out.spec = s;
			out.spec.cols = std::max(1, s.cols);
			int lines = (s.rows + out.spec.cols - 1) / out.spec.cols;
			// the grab mark's 11, the header, the content, 8 clear of the hint bar
			int h = 11 + SHEET_HEAD + s.body + (s.tiles ? SHEET_TILE + GAP : 0) + lines * SHEET_ROW + 8 + HINTS_H;
			h = std::clamp(h, SHEET_MIN, SHEET_MAX);
			out.box = {0, PANEL_H - h, PANEL_W, h};
			out.grab = {PANEL_W / 2 - 22, out.box.y + 7, 44, 4};
			out.head = {SHEET_PAD, out.box.y + 11, PANEL_W - 2 * SHEET_PAD, SHEET_HEAD};
			out.bodyY = out.head.y + SHEET_HEAD;
			out.tilesY = out.bodyY + s.body;
			out.rowsY = out.tilesY + (s.tiles ? SHEET_TILE + GAP : 0);
			out.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
			return out;
		}

		Box Sheet::tile(int i) const
		{
			return cell({SHEET_PAD, tilesY, PANEL_W - 2 * SHEET_PAD, SHEET_TILE}, std::max(1, spec.tiles), GAP, i);
		}

		Box Sheet::row(int i) const
		{
			Box c = cell({SHEET_PAD, rowsY, PANEL_W - 2 * SHEET_PAD, SHEET_ROW}, spec.cols, SHEET_PAD, i % spec.cols);
			c.y += i / spec.cols * SHEET_ROW;
			return c;
		}

		int sheetMove(const SheetSpec& s, int cursor, Dir d)
		{
			int cols = std::max(1, s.cols), n = s.tiles + s.rows;
			if (n <= 0) return 0;
			cursor = std::clamp(cursor, 0, n - 1);
			if (cursor < s.tiles)
			{
				if (d == Dir::Left) return std::max(0, cursor - 1);
				if (d == Dir::Right) return std::min(s.tiles - 1, cursor + 1);
				if (d == Dir::Up || !s.rows) return cursor;
				// down: the column under the tile (four tiles over two columns: the left two, the right two)
				int col = std::min(cols - 1, cursor * cols / s.tiles);
				return s.tiles + std::min(s.rows - 1, col);
			}
			int r = cursor - s.tiles, col = r % cols, line = r / cols;
			switch (d)
			{
			case Dir::Left: return col > 0 ? cursor - 1 : cursor;
			case Dir::Right: return col + 1 < cols && r + 1 < s.rows ? cursor + 1 : cursor;
			case Dir::Down: return r + cols < s.rows ? cursor + cols : cursor;
			default:
				if (line > 0) return cursor - cols;
				// up from the first line: the first tile over that column
				return s.tiles ? std::min(s.tiles - 1, (col * s.tiles + cols - 1) / cols) : cursor;
			}
		}

		PlayerLayout playerLayout(int chips)
		{
			PlayerLayout l;
			Box page{MARGIN, 0, PANEL_W - 2 * MARGIN, 0};
			l.back = {10, 3, 56, 48};
			l.backHit = {0, 0, 70, 56};
			l.more = {PANEL_W - 10 - 56, 3, 56, 48};
			l.moreHit = {PANEL_W - 70, 0, 70, 56};
			l.title = {page.x, 58, page.w, 34};
			l.line = {page.x, 92, page.w, 40};
			l.lineHit = {page.x, 76, page.w, 56};	// (a key 40 tall: its touch area takes the title's lower half)
			l.bar = {page.x, 138, page.w, 14};
			l.barHit = {page.x - 8, 132, page.w + 16, 52};	// the whole row, down to the times
			l.times = {page.x, 160, page.w, 20};
			for (int i = 0; i < 5; i++) l.transport[i] = transportCell({page.x, 188, page.w, 74}, i);
			for (int i = 0; i < 4; i++) l.keys[i] = cell({page.x, 272, page.w, 56}, 4, GAP, i);
			l.chips = std::clamp(chips, 0, PLAYER_CHIPS_MAX);
			for (int i = 0; i < l.chips; i++) l.chip[i] = cell({page.x, 338, page.w, 52}, l.chips, GAP, i);
			l.next = {page.x, 398, page.w, 50};
			l.hints = {0, PANEL_H - HINTS_H, PANEL_W, HINTS_H};
			l.lock = {PANEL_W / 2 - 60, 150, 120, 120};
			return l;
		}

		PlayerFocus playerFocusMove(PlayerFocus f, Dir d)
		{
			if (d == Dir::Down) f.zone = 2;
			else if (d == Dir::Up) f.zone = 0;
			return f;
		}

		std::string chipTrack(const Track& t, int n, const std::vector<Track>& all)
		{
			std::string lang = languageName(t.lang);
			if (lang.empty()) return "Track " + std::to_string(n);
			int same = 0;
			for (auto& o : all)
				if (o.type == t.type && languageName(o.lang) == lang) same++;
			if (same < 2) return lang;
			// what tells it apart: its own title, else that it's forced, else its format
			std::string more = !t.title.empty() && t.title != lang && t.title != t.lang ? t.title : t.forced ? "Forced" : codecName(t.codec);
			return more.empty() ? lang : lang + " \xC2\xB7 " + more;
		}

		std::string caps(std::string s)
		{
			for (auto& c : s)
				if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
			return s;
		}
	}
}
