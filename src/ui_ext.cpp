#include "ui.h"
#include "manganet.h"
#include "pagedecode.h"
#include "ui_internal.h"
#include "ui_manga.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_home.h"

namespace bp
{
	// ---- the Extensions drawer, Manage extensions and the manga reader's page ----

	void Ui::Manga::openManga(Player& p)
	{
		ui.page = Page::Manga;
		ui.cursor = ui.scroll = 0;
		extPressed = ui.pressedBtn = -1;
		mangaOpen(p);	// the store loaded, the card scanned, on Library
		// most sources need the manga engine: offered at each entry while it isn't on the card (Not now is for this
		// time only). A failed download with nothing kept counts as not there
		mengSheet = false;
		if (mangaNet && mangaNet->started())
		{
			JavaExtHost& h = mangaNet->javaExt();
			auto st = h.state();
			mengSheet = st == JavaExtHost::State::NotInstalled || (st == JavaExtHost::State::Failed && h.installedBytes() == 0);
			// an install on the old runtime (HotSpot): the lighter one offered the same way, each entry till it's taken
			auto up = h.upgrade();
			mengUpgrade = !mengSheet && h.runtimeOld() && (up == JavaExtHost::Upgrade::None || up == JavaExtHost::Upgrade::Failed);
			if (mengUpgrade) mengSheet = true;
			mengCursor = 0;
		}
	}

	void Ui::Manga::openExtensions()
	{
		ui.page = Page::Extensions;
		ui.cursor = ui.scroll = 0;
		extPressed = ui.pressedBtn = -1;
	}

	void Ui::Manga::openExtensionsDrawer(Player& p)
	{
		extSheet = false;
		ui.page = Page::Browse;
		ui.inMenu = false;
		ui.browse->setSection(TAB_EXT);
		ui.browse->enterFolder(EXTDIR, p);
		ui.cursor = ui.scroll = 0;
		extPressed = ui.pressedBtn = -1;
	}

	void Ui::Manga::extToggleManga(Player& p)
	{
		Settings s = p.settings;
		s.mangaOn = !s.mangaOn;
		// the note about repositories is shown once, the first time it's turned on, wherever that was done
		bool first = s.mangaOn && !s.mangaIntroShown;
		if (first) s.mangaIntroShown = true;
		p.setSettings(s);
		ui.mangaOn = s.mangaOn;
		if (!s.mangaOn) { extSheet = false; return; }
		if (!first) return;
		extSheet = true;
		extSheetCursor = 0;
		ui.page = Page::Extensions;
		ui.cursor = ui.scroll = 0;
	}

	void Ui::Manga::extSheetChoose(int i, Player& p)
	{
		extSheet = false;
		if (i == 0) openManga(p);
	}

	bool Ui::Manga::extDrawerKey(Button b, Player& p)
	{
		if (b == Button::Start) { openExtensions(); return true; }
		if (b != Button::A) return false;
		// what's on (the manga reader); with none on, its button's page
		if (p.settings.mangaOn) openManga(p);
		else openExtensions();
		return true;
	}

	bool Ui::Manga::extKey(Button b, Player& p)
	{
		if (extSheet && ui.page == Page::Extensions)
		{
			if (b == Button::Left) extSheetCursor = 0;
			else if (b == Button::Right) extSheetCursor = 1;
			else if (b == Button::A) extSheetChoose(extSheetCursor, p);
			else if (b == Button::B) extSheet = false;
			return false;
		}
		// the reader is an app of its own: B at its top goes back to the drawer
		if (ui.page == Page::Manga) return mangaKey(b, p);
		// (the mini bar shown here says SELECT goes to what plays)
		if (b == Button::Select && ui.nowPlayable(p)) { ui.goNowPlaying(p); return false; }
		switch (b)
		{
		case Button::A: extToggleManga(p); break;
		case Button::Start: ui.openMainMenu(p); break;
		case Button::B: openExtensionsDrawer(p); break;
		default: break;
		}
		return false;
	}

	bool Ui::Manga::extPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind == Pointer::Down)
		{
			extPressed = -1;
			if (extSheet && ui.page == Page::Extensions)
			{
				for (int i = 0; i < 2; i++)
					if (extSheetBtn(i).hit(x, y)) extPressed = EXT_OPEN + i;
				return false;
			}
			if (ui.page == Page::Manga) return mangaPointer(pt, x, y, p);
			if (ui.page == Page::Browse)
			{
				if (!p.settings.mangaOn) { if (extManageRect().hit(x, y)) extPressed = EXT_MANAGE; return false; }
				for (int i = 0; i < EXT_DRAWER_TILES; i++)
					if (extTileRect(i).hit(x, y)) { extCursor = i; extPressed = EXT_TILE; }
				return false;
			}
			if (extRowRect().hit(x, y)) extPressed = EXT_ROW;
			if (extChipRect().hit(x, y)) extPressed = EXT_CHIP;
			return false;
		}
		if (pt.kind != Pointer::Up || extPressed < 0) return false;
		int id = extPressed;
		extPressed = -1;
		Rect r = id == EXT_TILE ? extTileRect(extCursor) : id == EXT_MANAGE ? extManageRect() : id == EXT_ROW ? extRowRect()
			: id == EXT_CHIP ? extChipRect() : id == EXT_GEAR ? extGearRect() : extSheetBtn(id - EXT_OPEN);
		// a finger that slid away changed its mind
		return r.hit(x, y) && extAct(id, p);
	}

	void Ui::Manga::openExtSettings()
	{
		// the manga reader's own page (not Banana Player's Settings): B comes back here, as it was
		openMangaSettings();
	}

	bool Ui::Manga::extAct(int id, Player& p)
	{
		if (id == EXT_OPEN || id == EXT_DONE) { extSheetChoose(id - EXT_OPEN, p); return false; }
		if (id == EXT_TILE) openManga(p);
		else if (id == EXT_MANAGE) openExtensions();
		else if (id == EXT_ROW || id == EXT_CHIP) extToggleManga(p);
		else if (id == EXT_GEAR) openExtSettings();
		return false;
	}

	void Ui::Manga::drawExtensionsDrawer(const Player& p)
	{
		// the section header (its markers, "<") is drawn before this: the drawer is the section's top
		if (!p.settings.mangaOn)
		{
			// none on: what extensions are, and the way to turn one on
			ui.drawGlyphMid(G_BLOCKS, W / 2, 160, DIM, 6);
			ui.drawTextMid("No extensions are on", 1, TEXT, W / 2, 206, 560, 2);
			ui.drawTextMid("Extensions add to Banana Player, like a manga reader.", 0, DIM, W / 2, 244, 560, 2);
			ui.drawTextMid("Nothing of one shows or runs until you turn it on.", 0, DIM, W / 2, 264, 560, 2);
			Rect b = extManageRect();
			ui.drawBox(b, extPressed == EXT_MANAGE ? PRESSED : BUTTON, R_BUTTON, true);
			ui.drawTextMid("Manage extensions", 1, ACCENT, b.x + b.w / 2, b.y + b.h / 2, b.w - 16, 2);
			ui.drawHints({{"START", "Manage extensions"}, {"B", "Home"}, {"L/R", "Sections"}});
			return;
		}
		// what's on: one tile each, its book, its name and what it does
		ui.drawTextMid(std::to_string(EXT_DRAWER_TILES) + " on", 0, DIM, 16, 64);
		for (int i = 0; i < EXT_DRAWER_TILES; i++)
		{
			Rect t = extTileRect(i);
			bool at = extCursor == i;
			ui.drawBox(t, extPressed == EXT_TILE ? PRESSED : at ? BUTTON : SURFACE, R_TILE, at);
			// (the name at the small size, as the mock has it: the large one doesn't fit a tile)
			drawMangaIcon({t.x + t.w / 2 - 38, 102, 76, 76});
			ui.drawTextMid("Manga reader", 0, at ? ACCENT : TEXT, t.x + t.w / 2, 196, t.w - 16, 2);
			ui.drawTextMid("Comics and manga", 0, at ? DIM_ON : DIM, t.x + t.w / 2, 218, t.w - 16, 2);
		}
		ui.drawTextMid("Turn more on in Manage extensions (START).", 0, DIM, W / 2, 270, 560, 2);
		ui.drawHints({{"A", "Open"}, {"START", "Manage extensions"}, {"B", "Home"}, {"L/R", "Sections"}});
	}

	void Ui::Manga::drawExtensions(const Player& p)
	{
		ui.drawHeader("Manage extensions", p);
		ui.drawTextMid(std::to_string(EXT_DRAWER_TILES) + " extension", 0, DIM, 16, 64);
		// the extension's row: its mark, its name and what it does, its switch, its settings
		Rect row = extRowRect();
		ui.drawFocus(row);
		drawMangaIcon({30, 100, 46, 46});
		ui.drawTextMid("Manga reader", 1, ACCENT, 92, 110, 360);
		ui.drawTextMid("Comics and manga from files and servers", 0, DIM, 92, 136, 360);
		bool on = p.settings.mangaOn;
		Rect chip = extChipRect();
		ui.fillRound(chip, on ? ACCENT : BUTTON, R_CHIP);
		ui.drawTextMid(on ? "On" : "Off", 0, on ? ON_ACCENT : TEXT, chip.x + chip.w / 2, chip.y + chip.h / 2, chip.w - 8, 2);
		// (on and off only: the extension's settings are its own, START inside it)
		ui.drawTextMid("More extensions come with later versions.", 0, DIM, W / 2, 190, 560, 2);
		if (extSheet)
		{
			drawExtSheet(p);
			return;
		}
		ui.drawHints({{"A", "On / Off"}, {"B", "Back"}});
	}

	void Ui::Manga::drawExtSheet(const Player& p)
	{
		(void)p;
		// over the page, dimmed: the note about repositories, once, the first time the reader is turned on
		ui.fill({0, 0, W, H}, sdl(theme::BG, (Uint8)(255 * theme::VEIL_SHEET / 100)));	// (the veil: BG, as a sheet's)
		ui.drawBox(extSheetBox(), SURFACE, R_DIALOG);
		drawMangaIcon({88, 108, 40, 40});
		ui.drawTextMid("Manga reader is on", 1, TEXT, 136, 128, 420);
		ui.drawTextMid("It reads from your files and servers, and from", 0, TEXT, 92, 174, 456);
		ui.drawTextMid("sources in repositories you add.", 0, TEXT, 92, 196, 456);
		ui.drawTextMid("Repositories are added by you. Banana Player", 0, DIM, 92, 232, 456);
		ui.drawTextMid("doesn\xE2\x80\x99t provide or check their content.", 0, DIM, 92, 254, 456);
		for (int i = 0; i < 2; i++)
		{
			Rect b = extSheetBtn(i);
			bool at = extSheetCursor == i;
			ui.drawBox(b, extPressed == EXT_OPEN + i ? PRESSED : at ? ACCENT : BUTTON, R_BUTTON, at);
			ui.drawTextMid(i ? "Done" : "Open Manga", 1, at ? ON_ACCENT : TEXT, b.x + b.w / 2, b.y + b.h / 2, b.w - 16, 2);
		}
		ui.drawHints({{"A", "Choose"}, {"LEFT/RIGHT", "Move"}, {"B", "Done"}});
	}

	void Ui::Manga::drawMangaIcon(Rect r)
	{
		// the manga reader's picture (assets/icons/manga.png beside the fonts), decoded at the size it's drawn (smooth,
		// as drawn; again when a place of another size asks)
		SDL_Rect d = ui.devRect(r);
		int s = std::min(d.w, d.h);
		if (s != mangaIconPx)
		{
			if (mangaIconTex) SDL_DestroyTexture(mangaIconTex);
			mangaIconTex = nullptr;
			mangaIconPx = s;
			PageImage img = decodePageFile(ui.fontsDir + "/../icons/manga.png", s);
			if (img.ok())
			{
				mangaIconTex = SDL_CreateTexture(ui.ren, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, img.w, img.h);
				if (mangaIconTex)
				{
					SDL_UpdateTexture(mangaIconTex, nullptr, img.rgba.data(), img.w * 4);
					SDL_SetTextureBlendMode(mangaIconTex, SDL_BLENDMODE_BLEND);
				}
			}
		}
		SDL_Rect c{d.x + (d.w - s) / 2, d.y + (d.h - s) / 2, s, s};
		if (mangaIconTex) SDL_RenderCopy(ui.ren, mangaIconTex, nullptr, &c);
		else ui.drawGlyphMid(G_BOOK, r.x + r.w / 2, r.y + r.h / 2, ACCENT, 3);	// (no picture: the book, as before)
	}
}
