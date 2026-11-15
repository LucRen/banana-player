// The Settings page (the groups' tiles, a group's rows, the subtitle style's preview): an area of Ui (Ui::Area, ui.h;
// docs/notes/ui-areas.md). Its keys, its touches, its drawing and the rows' stepping are here and in ui_settings.cpp.
// The manga reader's own settings page (Page::MangaSettings) is Manga's, but its rows are drawn, stepped and touched
// by this struct's settingsDown, drawSettingRow, settingHelp and changeSetting. Stayed Ui's: the group and row ids
// (SetGroup, STYLE_ROW0..MANGA_JAVA_ROW), the list's measures (SET_Y, SET_H...), setGroup (the open group: the
// way-back records of showPlayer, the dashboard and Browse keep it), the lift of a list's touch in Ui::pointer (one
// block for every list) and the Brightness rows' setBrightness (the quick settings' too).
#pragma once
#include "ui.h"

namespace bp
{
	struct Ui::SettingsPage : Ui::Area
	{
		explicit SettingsPage(Ui& u) : Area(u) {}

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Settings; }
		bool key(Button b, Player& p) override;
		// its downs come late, after the header's "<" and the menu's tabs, as they always did
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		bool pointerDownLate() const override { return true; }
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// ---- for the rest of Ui and Manga's settings page ----
		// B in START's menu on this page: out of the subtitle style back to its list, or out of a group to the tiles.
		// True: the key was taken (before the menu's own B closes it)
		bool menuBack(Player& p);
		std::vector<int> settingRows() const;	// the open group's rows, by id
		void changeSetting(int row, int dir, Player& p);
		std::string settingHelp(int row) const;	// what a setting does, under its rows
		// one settings row at y (at: the cursor's): its label, arrows and value, an action's or the note's own look.
		// Shared by Settings (draw) and the manga reader's own settings page (drawMangaSettings)
		void drawSettingRow(int row, int y, bool at, const Player& p);
		// a finger down on the settings' list (Settings', Manga's): its row, the touch begun
		bool settingsDown(int x, int y);
		void openGroup(int g);
		void stepGroup(int dir);	// to the settings group before (-1) or after (1), round
		// a settings group's tile (Settings opens on them: two to a row)
		Rect groupRect(int g) const { return {16 + g % 2 * 312, 70 + g / 2 * 86, 296, 78}; }
		bool styleFromSubs = false;	// the subtitle style opened from the Subtitles list: B goes back to it

	private:
		void drawPreview(const SubStyle& st, Rect box);
	};
}
