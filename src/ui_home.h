// Home, the browser's first page (browseDir HOMEDIR), and All recent (RECENT): the Recent strip and its cards, the
// sections' tiles, the focus and the strip's easing, a finger on a card or a tile, B's quit dialog, a recent file's
// menu and Home's Options, the Recent list itself and All recent's tabs. A plain struct owned by the browser's area
// (Ui::Browse::homeView, ui_browse.h; docs/notes/ui-areas.md): Home is a folder of Page::Browse, not a page, so
// Browse's key and pointer hand it Home's keys and touches (homeKey, homePointer) and drawTracks draws it (drawHome).
// Stayed Ui's: what plays a recent file or shows it in its folder (openFile, showInFolder: the dashboard and the
// Playing tab use them too), the mini bar and the playing file's chip (every page's; their code is in ui_home.cpp),
// the settings mirrored for drawing (showHidden, mangaOn) and the Home touches Ui::pointer takes before the area's
// point (the quit dialog over every finger, a card's drag in progress) and Ui::tick's (the strip's easing, Recent
// listed again when what plays changes), which call in here where they always were.
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::Home
	{
		Ui& ui;
		explicit Home(Ui& u) : ui(u) {}

		// Home (browseDir HOMEDIR), the browser's first page: the Recent strip (a card a recent file, then All recent)
		// over the four sections' tiles, two by two. The focus is on the strip or the tiles; UP/DOWN go between them
		// (back to the card or the column left), LEFT/RIGHT along. The focused card is kept in the strip's second
		// slot, the strip easing there over 120 ms (stripT0: tick moves it)
		struct RecentItem
		{
			std::string path, name, sub, where, at;	// at: where it was left ("0:27"), "" from the start
			const Glyph* kind;
			double frac;	// the bar: how far into it (played through: 1)
			bool net;
		};
		std::vector<RecentItem> recent;	// as listed with Home (or All recent)
		void listRecent(Player& p);
		// Home's Recent (and All recent) listed again where it's being looked at (the next song started, the web removed
		// one): in the order it was shown, so nothing moves under the cursor; a newly played one comes when it's
		// entered again (each entry lists it newest first)
		bool recentKeepOrder = false;

		// All recent's tabs: 0 everything, 1 videos, 2 music, 3 books and documents (L2/R2, or a tap on them); the counts
		// as last listed, and where each tab was drawn (for a tap)
		int recentTab = 0, recentCounts[4] = {0, 0, 0, 0};
		Rect recentTabRect[4];
		bool drawRecentTabs(int x, int mid, int right, bool counts = true);	// false (nothing drawn): they don't fit there
		bool recentTabsUp = false;	// drawn on the title's line (else on the line under it)
		void stepRecentTab(int dir, Player& p);
		void recentInOrder(const std::vector<std::string>& before);
		// the history changed from elsewhere (the web): Home or All recent listed again, the focus kept
		void recentEdited(Player& p);
		enum HomeZone { HOME_STRIP, HOME_GRID };
		int homeZone = HOME_STRIP, homeStrip = 0, homeGrid = 0, homeCol = 0;
		bool homeFresh = true;	// not yet shown this run: the focus starts on the newest card (Files with none)
		static constexpr int CARD_W = 240, STRIDE = 250, CARD_Y = 84, CARD_H = 104, STRIP_TOP = 80, STRIP_H = 116;
		// the strip: the newest few (the rest are in All recent, its last card)
		static constexpr int HOME_RECENT_MAX = 5;
		int stripCards() const { return std::min((int)recent.size(), HOME_RECENT_MAX); }
		int stripCount() const { return stripCards() + 1; }	// (+ All recent)
		int stripMax() const { return std::max(0, 16 + stripCount() * STRIDE - 10 + 16 - W); }
		int stripTarget(int i) const { return std::clamp((i - 1) * STRIDE, 0, stripMax()); }
		double stripOff = 0, stripFrom = 0, stripTo = 0;
		Uint32 stripT0 = 0;
		void setStrip(int i, bool animate = true);
		void stripStep();	// the eased scroll, a frame
		Rect tileRect(int g) const;
		std::string tileSub(int g) const;
		// the tiles' lines, made again when the playlists or the connections change
		mutable std::string tileSubs[BROWSE_TABS];
		mutable int tileSubsKey = -1;
		bool homeKey(Button b, Player& p);	// true: Quit chosen in B's dialog, leave
		void goHome(int zone, int idx, Player& p);	// back to Home, the focus there
		void homeOpen(Player& p);	// A: the card's file, All recent, a tile's section
		// a finger on Home: on a card (a tap plays, a hold opens its menu, a drag scrolls the strip) or a tile
		bool homeTouch = false, homeMoved = false;
		int homeDownX = 0, homePress = -1;	// homePress: a card's index, or HOME_TILE + a tile's
		double homeDownOff = 0;
		Uint32 homeHoldAt = 0;
		static constexpr int HOME_TILE = 1000;
		bool homePointer(const Pointer& pt, int x, int y, Player& p);
		// B on Home with nothing playing asks first; the dialog is Home's, over the page (drawHome)
		bool quitOpen = false;
		int quitCursor = 1;	// Cancel first
		// laid out as Quick settings (F) is: its panel, its rows, its red Quit button
		static constexpr Rect QUIT_BOX{40, 119, 560, 198};
		Rect quitRow(int i) const { return {56, QUIT_BOX.y + 56 + i * 62, 528, 54}; }
		void drawQuitDialog(bool quitPlaying);	// (playing: it says quitting stops it)
		void drawHome(const Player& p);

		// Y on a card (or on All recent's row, a hold on a card): Resume from / Play, Play from the start, Show in
		// folder, Add to playlist..., Remove from Recent; X on Home (and All recent): Clear Recent, Show hidden folders,
		// Settings
		std::string ctxRecent;	// the file the menu is for
		void openRecentMenu(const std::string& path);
		void openHomeOptions();

		std::string homeListedFor;	// what played as Home's Recent was listed (it's listed again when that changes)

	};
}
