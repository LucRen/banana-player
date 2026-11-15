#include "ui.h"
#include "ui_internal.h"
#include "ui_manga.h"
#include "manganet.h"

namespace bp
{
	// six rows: a seventh would run under the help line (Settings' groups never have seven)
	static constexpr int MSET_SHOWN = 6;

	static std::string javaMb(int64_t bytes)
	{
		return std::to_string((bytes + 500000) / 1000000) + " MB";
	}

	// ---- the manga reader's settings: a page of the app's own, not a group of Banana Player's Settings ----

	std::vector<int> Ui::Manga::mangaSettingRows() const
	{
		// (In Recent on Home, manga-recent, is kept in settings.conf and not shown: the reader stays off Home)
		auto rows = pageRows(SG_MANGA, {{80, MANGA_CLEAR_ROW}, {90, MANGA_JAVA_ROW}, {130, EXT_NOTE}});
		return rows;
	}

	void Ui::Manga::openMangaSettings()
	{
		// where it came from, and on the app the tab it was on (and Browse's list): B goes back to both. From the
		// reader, the reader stays open behind it
		mangaSettingsFrom = ui.page;
		mangaSettingsTab = mangaTab;
		mangaSettingsList = mBrowseList;
		ui.page = Page::MangaSettings;
		ui.cursor = ui.scroll = 0;
		ui.pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
		mangaJavaAt = 0;
	}

	void Ui::Manga::closeMangaSettings()
	{
		ui.page = mangaSettingsFrom;
		mangaTab = mangaSettingsTab;
		mBrowseList = mangaSettingsList;
		ui.cursor = ui.scroll = 0;
		ui.pressedBtn = mangaPressed = -1;
		mangaCacheAt = 0;
		mangaJavaAt = 0;
	}

	bool Ui::Manga::mangaSettingsKey(Button b, Player& p)
	{
		// (the mini bar shown here says SELECT goes to what plays, as on the app's other pages)
		if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
		auto rows = mangaSettingRows();
		int n = (int)rows.size();
		ui.cursor = std::min(ui.cursor, n - 1);
		switch (b)
		{
		case Button::Up: ui.cursor = (ui.cursor + n - 1) % n; break;
		case Button::Down: ui.cursor = (ui.cursor + 1) % n; break;
		case Button::Left: ui.changeSetting(rows[ui.cursor], -1, p); break;
		case Button::Right: case Button::A: ui.changeSetting(rows[ui.cursor], 1, p); break;
		case Button::B: closeMangaSettings(); return false;
		default: break;
		}
		if (ui.cursor < ui.scroll) ui.scroll = ui.cursor;
		if (ui.cursor >= ui.scroll + MSET_SHOWN) ui.scroll = ui.cursor - MSET_SHOWN + 1;
		return false;
	}

	uint64_t Ui::Manga::mangaJavaSerial() const
	{
		// (ui.cpp's signature has no MangaNet's definition: the serial it follows is asked for here)
		return mangaNet ? mangaNet->javaExt().serial() : 0;
	}

	std::string Ui::Manga::mangaJavaValue() const
	{
		if (!mangaNet) return "Not installed \xC2\xB7 241 MB";
		JavaExtHost& h = mangaNet->javaExt();
		switch (h.state())
		{
		case JavaExtHost::State::NotInstalled:
			return "Not installed \xC2\xB7 " + javaMb(JavaExtPins::JRE_BYTES + JavaExtPins::SERVER_BYTES);
		case JavaExtHost::State::Downloading:
		{
			int64_t done = 0, total = 0;
			h.progress(done, total);
			int pct = total > 0 ? (int)(done * 100 / total) : 0;
			return "Downloading " + std::to_string(pct) + " %";
		}
		case JavaExtHost::State::Installed:
		{
			int64_t bytes = h.installedBytes();
			return bytes > 0 ? "Installed \xC2\xB7 " + javaMb(bytes) : "Installed";
		}
		case JavaExtHost::State::Starting: return "Starting\xE2\x80\xA6";
		case JavaExtHost::State::Running: return "Running";
		case JavaExtHost::State::Failed: return h.error().empty() ? "Failed" : "Failed: " + h.error();
		}
		return "";
	}

	void Ui::Manga::mangaJavaAct()
	{
		if (!mangaNet) return;
		JavaExtHost& h = mangaNet->javaExt();
		switch (h.state())
		{
		case JavaExtHost::State::NotInstalled:
			h.install();
			mangaJavaAt = 0;
			break;
		case JavaExtHost::State::Downloading:
			h.cancelInstall();
			mangaJavaAt = 0;
			break;
		case JavaExtHost::State::Installed:
		{
			// as Clear cache is: the first press says what the second does
			Uint32 now = SDL_GetTicks();
			if (mangaJavaAt && now - mangaJavaAt < QUIT_WINDOW_MS)
			{
				h.remove();
				mangaJavaAt = 0;
			}
			else mangaJavaAt = now;
			break;
		}
		case JavaExtHost::State::Failed:
			// a process that ended: start it again; a download or unpack that failed: try the install again
			if (h.installedBytes() > 0) h.want();
			else h.install();
			mangaJavaAt = 0;
			break;
		default: break;
		}
	}

	void Ui::Manga::drawMangaSettings(const Player& p)
	{
		ui.drawHeader("Manga settings", p);
		auto rows = mangaSettingRows();
		int n = (int)rows.size();
		if (ui.cursor >= n) ui.cursor = n - 1;
		if (ui.scroll > n - MSET_SHOWN) ui.scroll = std::max(0, n - MSET_SHOWN);
		// the rows as Settings draws its group's: the note last
		for (int i = ui.scroll; i < n && i < ui.scroll + MSET_SHOWN; i++)
			ui.drawSettingRow(rows[i], SET_Y + (i - ui.scroll) * SET_H, i == ui.cursor, p);
		// what the row under the cursor does, in up to three lines above the footer
		std::string help = ui.cursor >= 0 && ui.cursor < n ? ui.settingHelp(rows[ui.cursor]) : "";
		auto lines = ui.wrapText(help, 0, 592, 3);
		// a third line would reach into the footer: the block starts one step higher
		int helpY = HELP_Y - (lines.size() > 2 ? 22 : 0);
		for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], 0, DIM, 24, helpY + (int)i * 22, 592);
		std::vector<Hint> hints = {{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"B", "Back"}};
		if (mangaNet && ui.cursor >= 0 && ui.cursor < n && rows[ui.cursor] == MANGA_JAVA_ROW)
		{
			const char* verb = nullptr;
			switch (mangaNet->javaExt().state())
			{
			case JavaExtHost::State::NotInstalled: verb = "Install"; break;
			case JavaExtHost::State::Downloading: verb = "Cancel"; break;
			case JavaExtHost::State::Installed: verb = mangaJavaAt ? "Remove again" : "Remove"; break;
			case JavaExtHost::State::Failed: verb = "Try again"; break;
			default: break;
			}
			if (verb) hints.insert(hints.begin() + 2, Hint{"A", verb});
		}
		ui.drawHints(hints);
	}
}
