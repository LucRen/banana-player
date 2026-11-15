#include "ui.h"
#include "session_host.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#ifdef BP_WEB
#include "web/netif.h"
#include "web/server.h"
#include <qrcodegen.hpp>
#endif
#include "ui_internal.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_settings.h"
#include "ui_docs.h"
#include "ui_reader.h"
#include "ui_manga.h"
#include "ui_sound.h"
#include "ui_web.h"
#include "durworker.h"

namespace bp
{
	constexpr Ui::Rect Ui::BAR;

	bool Ui::init(SDL_Window* window, const std::string& dir)
	{
		fontsDir = dir;
		panel.init();
		// tests: a fake /sys/class/backlight and /data/banana/backlight
		if (const char* d = getenv("BP_BACKLIGHT_DIR")) backlight.sysDir = d;
		if (const char* f = getenv("BP_BACKLIGHT_STORE")) backlight.store = f;
		if (const char* v = getenv("BP_VOLUME_SCRIPT")) volume.script = v;
		ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
		if (!ren) ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
		if (!ren) return false;
		int ow, oh;
		SDL_GetRendererOutputSize(ren, &ow, &oh);
		sx = ow / (float)W;
		sy = oh / (float)H;
		// the text is drawn at the panel's own size rather than scaled up: Pixelify Sans stays sharp
		std::string path = fontsDir + "/PixelifySans-Regular.ttf";
		for (int i = 0; i < 3; i++)
		{
			fonts[i] = TTF_OpenFont(path.c_str(), (int)std::lround(SIZES[i] * sy));
			if (!fonts[i])
			{
				SDL_Log("font %s: %s", path.c_str(), TTF_GetError());
				return false;
			}
		}
		for (int i = 0; i < 3; i++)
		{
			int minx, maxx, miny, maxy, adv;
			capHeight[i] = TTF_GlyphMetrics(fonts[i], 'H', &minx, &maxx, &miny, &maxy, &adv) == 0 ? maxy : TTF_FontAscent(fonts[i]) * 3 / 4;
		}
		// the web dashboard's PIN, big (a font of its own: the three sizes are the page's text)
		pinFont = TTF_OpenFont(path.c_str(), (int)std::lround(56 * sy));
		static const char* FALLBACKS[] = {"fusion-pixel-12px-proportional-zh_hans.ttf", "NotoSansThai.ttf",
			"NotoSansArabic.ttf", "NotoSansHebrew.ttf", "NotoSansDevanagari.ttf"};
		for (const char* name : FALLBACKS)
		{
			std::array<TTF_Font*, 3> f = {};
			for (int i = 0; i < 3; i++)
			{
				// Fusion Pixel is a 12-pixel font: at whole multiples of 12 its pixels stay square
				int px = (int)std::lround(SIZES[i] * sy);
				if (name == FALLBACKS[0]) px = std::max(12, (int)std::lround(SIZES[i] * sy / 12.0) * 12);
				f[i] = TTF_OpenFont((fontsDir + "/" + name).c_str(), px);
			}
			if (f[0] && f[1] && f[2]) fallbacks.push_back(f);
			else for (auto* x : f) if (x) TTF_CloseFont(x);
		}
		return true;
	}

	// the areas: each made here and listed, which is all the wiring one needs (docs/notes/ui-areas.md)
	Ui::Ui()
	{
		pageReader = std::make_unique<PageReader>(*this);	// (not an area: no page of its own; Docs and Manga use it)
		docs = std::make_unique<Docs>(*this);
		areas.push_back(docs.get());
		sound = std::make_unique<Sound>(*this);
		areas.push_back(sound.get());
		webPage = std::make_unique<Web>(*this);
		areas.push_back(webPage.get());
		manga = std::make_unique<Manga>(*this);
		areas.push_back(manga.get());
		settingsPage = std::make_unique<SettingsPage>(*this);
		areas.push_back(settingsPage.get());
		browse = std::make_unique<Browse>(*this);
		areas.push_back(browse.get());
	}

	Ui::Area* Ui::area() const
	{
		for (Area* a : areas)
			if (a->has(page)) return a;
		return nullptr;
	}

	Ui::~Ui()
	{
		if (photoTex) SDL_DestroyTexture(photoTex);
		for (auto& c : cache) SDL_DestroyTexture(c.second);
		for (auto& c : manga->coverTex)
			if (c.second.tex) SDL_DestroyTexture(c.second.tex);
		for (auto* t : corners)
			if (t) SDL_DestroyTexture(t);
		for (auto* f : fonts)
			if (f) TTF_CloseFont(f);
		if (pinFont) TTF_CloseFont(pinFont);
		for (auto& f : fallbacks)
			for (auto* x : f) TTF_CloseFont(x);
		if (ren) SDL_DestroyRenderer(ren);
	}

	bool Ui::press(Button b, Player& p)
	{
		// a key while a finger holds a slider: the slider is let go where it is (the key may close what it's on)
		slideEnd(p);
		// an overlay (the readers' Brightness sheet, a message's dialog) takes the keys while it shows
		if (topOverlay()) return overlayKey(b, p);
		// the browser-check dialog takes the keys while it shows: A or B closes it
		if (manga->mangaErrOpen)
		{
			if (b == Button::A || b == Button::B) manga->mangaErrOpen = false;
			return false;
		}
		sameBrightness = p.settings.sameBrightness;
		mangaOn = p.settings.mangaOn;
		webInputAt = SDL_GetTicks();
		// dark (Screen off, or off by itself): over what plays, the playback keys work blind and leave it dark (play
		// and pause, seek, next and previous, chapters, speed, like: issue #3, watching with the bottom screen off);
		// any other button, or anywhere else (a list: A there would open whatever the cursor is on), brings the
		// panel back and does nothing else, as a touch does. F + START still quits, in main
		if (!panel.on())
		{
			bool playKey = b == Button::A || b == Button::Left || b == Button::Right || b == Button::L || b == Button::R ||
						   b == Button::L2 || b == Button::R2 || b == Button::Up || b == Button::Down || b == Button::L3;
			bool overPlayer = page == Page::Main && !inMenu && !quickOpen && !kbOpen && p.playing() && !p.playingPhoto();
			if (!(playKey && overPlayer))
			{
				panel.set(true);
				return false;
			}
		}
		// a copy or move running: only its Cancel answers (ui_fileops.cpp)
		if (fileJob) return fileJobKey(b);
		// Info's sheet: any key closes it
		if (infoOpen) { infoOpen = false; return false; }
		if (quickOpen) return quickKey(b, p);
		if (kbOpen)
		{
			kbPress(b);
			if (sound->eqNameSubmit) sound->nameDone(p);
			if (browse->playlists->plNameSubmit) browse->playlists->plNameDone(p);
			if (browse->fileNameSubmit) browse->fileRenameDone(p);
			return false;
		}
		// Add to... over what plays (or the Playing tab); the browser's is taken below, in its turn
		if (browse->playlists->pickerOpen && page != Page::Browse)
		{
			browse->playlists->pickerKey(b, p);
			return false;
		}
		// Play on, over START › Playing
		playOnOpen = playOnOpen && inMenu && page == Page::Playing && playingTabShown(p);
		if (playOnOpen)
		{
			playOnKey(b, p);
			return false;
		}
		displayOpen = displayOpen && displayShown(p);	// (the song gone under it: closed)
		if (!displayOpen) displayPending = -1;
		if (displayOpen) return displayKey(b, p);
		// a menu (the Options one, a row's hold, the mini bar's) takes the buttons first, on whatever page it is over
		// (the mini bar's opens on pages whose own keys would otherwise eat them)
		if (ctxOpen)
		{
			switch (b)
			{
			case Button::Up: ctxCursor = (ctxCursor + (int)ctxItems.size() - 1) % (int)ctxItems.size(); ctxArmed = false; break;
			case Button::Down: ctxCursor = (ctxCursor + 1) % (int)ctxItems.size(); ctxArmed = false; break;
			case Button::A: chooseBrowseMenu(ctxCursor, p); break;
			case Button::B: case Button::X: case Button::Y: ctxOpen = false; break;
			default: break;
			}
			return false;
		}
		// a page the output waits for a tap there to start the sound (the top panel says so): B plays here instead
		if (b == Button::B && !inMenu && (page == Page::Main || page == Page::Browse) && SessionHost::get().pageWaits() &&
			!ctxOpen && !browse->playlists->pickerOpen && !popupShown(p) && browse->playlists->selMode == SEL_NONE && !browse->playlists->addPending.serial)
		{
			takeOver(p);
			return false;
		}
		// Button hotkeys off: the buttons do nothing over a playing video (touch does it all); the pages opened from
		// there by touch still take them
		if (page == Page::Main && !p.settings.buttonHotkeys) return false;
		// L2/R2: the chapter before or after, over what plays and its two pages (the Chapters page's mark follows).
		// Not in START's menu, not over a photo (its keys are its own), and nothing with Button hotkeys off: they
		// have no touch button to stand for
		// (the manga pages have their own: the reader's chapter before or after, the series page's Download)
		if ((b == Button::L2 || b == Button::R2) && page != Page::Manga && page != Page::MangaSeries && page != Page::MangaRead &&
			page != Page::DocText && page != Page::DocPages)
		{
			if (p.settings.buttonHotkeys && !inMenu && p.playing() && !p.playingPhoto() && p.error.empty() &&
				(page == Page::Main || page == Page::Tracks || page == Page::Chapters))
			{
				bool pred = predicting(p);
				int t = p.stepChapter(b == Button::L2 ? -1 : 1, pred ? shownPosition(p) : -1);
				if (pred && t >= 0 && t < (int)p.chapters.size()) noteSeek(p, p.chapters[t].time);
				else if (pred && t == CHAPTER_FILE_START) noteSeek(p, 0);
			}
			return false;
		}
		if (inMenu)
		{
			if (b == Button::L || b == Button::R)
			{
				// round, in the order shown
				auto tabs = menuTabs(p);
				int n = (int)tabs.size(), at = 0;
				for (int i = 0; i < n; i++)
					if (tabs[i] == menuTab) at = i;
				showTab(tabs[(at + (b == Button::R ? 1 : n - 1)) % n], p);
				return false;
			}
			// B in the subtitle style opened from the Subtitles list, or in a settings group: back (SettingsPage)
			if (b == Button::B && page == Page::Settings && settingsPage->menuBack(p)) return false;
			if (b == Button::Start || b == Button::B) { closeMenu(p); return false; }
			if (page == Page::Exit)
			{
				if (b == Button::A) quitRequested = true;
				return false;
			}
			// the tab's page takes the rest (Recent's list, the settings' rows, About's scrolling)
		}
		// Files' Copy / Move held: SELECT pastes in this folder, X lets go
		if (browse->clipKey(b, p)) return false;
		// an area's page (Files' document viewers): its own keys
		if (Area* a = area()) return a->key(b, p);
		if (page == Page::Playing) return playingKey(b, p);
		if (page == Page::Info || page == Page::About)
		{
			int maxScroll = std::max(0, (int)infoLines.size() - INFO_SHOWN);
			switch (b)
			{
			case Button::Up: scroll = std::max(0, scroll - 1); break;
			case Button::Down: scroll = std::min(maxScroll, scroll + 1); break;
			case Button::Left: scroll = std::max(0, scroll - INFO_SHOWN); break;
			case Button::Right: scroll = std::min(maxScroll, scroll + INFO_SHOWN); break;
			case Button::B: case Button::A:
				// About as the menu's tab: nothing to go back to (B closes the menu)
				if (page == Page::About && inMenu) break;
				if (page == Page::About) { page = Page::Settings; setGroup = -1; cursor = scroll = 0; }
				else page = home(p);
				break;
			default: break;
			}
			return false;
		}
		if (page == Page::Tracks || (page == Page::Chapters && !p.chapters.empty()))
		{
			// the tabs: L the one before, R the one after, as the browser's sections go; Y and X as well (the west and
			// east buttons), X having opened the page
			if (b == Button::Y || b == Button::L) { stepTracksTab(-1, p); return false; }
			if (b == Button::X || b == Button::R) { stepTracksTab(1, p); return false; }
		}
		if (page == Page::Chapters && p.chapters.empty())
		{
			// (the list can also empty under it: the next file has none)
			if (b == Button::B) page = home(p);
			return false;
		}
		if (page != Page::Main) return listKey(b, p);
		if (p.playingPhoto() && photoKey(b, p)) return false;
		// the network dropped it: A tries again, from where it stopped (the session reloads it there)
		if (!p.error.empty() && p.errorNet && b == Button::A)
		{
			fprintf(stderr, "net: trying %s again\n", p.currentPath().c_str());
			SessionHost::get().playLive(true);	// (live: its reload runs now, as Next's does)
			return false;
		}
		// a file that didn't open: nothing to pause, choose or speed up (Prev, Next, Up next, the menu and leaving still go)
		if (!p.error.empty() && (b == Button::A || b == Button::X || b == Button::Select || b == Button::Up || b == Button::Down))
			return false;
		switch (b)
		{
		case Button::A:
			// a page plays it: the page's play/pause as A lets go (release()), here only once held (tick())
			if (pageDrives(p)) { if (!playHoldAt) { playHoldAt = SDL_GetTicks(); playHoldTouch = false; } break; }
			// the page is the output but can't hear (its tab gone a moment): play here
			if (pageIsOut()) { takeOver(p); break; }
			SessionHost::get().play(p.paused);	// (mpv answers the flip later: paused is still the old state)
			p.togglePause();
			break;
		// out of the player at once: what plays goes on behind the browser (nothing is lost, so a pocket's press costs
		// nothing); a photo closes
		case Button::B: return true;
		// the play mode (a song or a video)
		case Button::X: musicKey(b, p); break;
		// List: a playlist's song opens that playlist's own page (its edits, its order), what plays going on behind;
		// a folder, Recent or a selection: Up next
		case Button::Y:
			if (p.source().kind == SourceKind::Playlist && !p.source().id.empty() && !p.playingPhoto())
			{
				showSourceAfterLeave = true;
				return true;
			}
			openUpNext(p);
			break;
		// the menu opens on what plays (Like, Add to playlist...): Settings is a tab along
		case Button::Start: openMainMenu(p, playingTabShown(p) ? TAB_PLAYING : TAB_SETTINGS); break;
		// the stick pressed in (the RG DS Plus): Like, at once (over a photo it turns it, above)
		case Button::L3: likeNow(p); break;
		// a song: the Display popup; a video: Tracks (Chapters, Subtitles, Audio, Picture)
		case Button::Select: if (!musicKey(b, p)) openTracksPage(p); break;
		// (a page playing it: from where it should be by now; the jump is passed on to the page)
		case Button::Left:
		case Button::Right:
			if (predicting(p))
			{
				double t = std::clamp(shownPosition(p) + (b == Button::Left ? -BACK_S : FORWARD_S), 0.0, p.duration);
				p.seekFraction(t / p.duration);
				noteSeek(p, t);
			}
			else p.seekRelative(b == Button::Left ? -BACK_S : FORWARD_S);
			break;
		case Button::L:
		case Button::R:
			// the session's Step (Previous RESTART_S in restarts; a burst counts every press; a page playing it moves on
			// there, the copy here following held). What the session doesn't have (it disagrees: logged), as before
			if (SessionHost::get().stepsHere(p)) { SessionHost::get().step(b == Button::R ? 1 : -1); break; }
			if (b == Button::L ? !p.back() : !p.next()) p.osd(b == Button::L ? "This is the first one" : "This is the last one");
			break;
		case Button::Up: case Button::Down:
		{
			double s = stepSpeed(p.speed, b == Button::Up ? 1 : -1);
			p.setSpeed(s);
			p.osd("Speed " + speedLabel(s));
			break;
		}
		default: break;
		}
		return false;
	}

	bool Ui::popupKey(Button b, Player& p)
	{
		int n = (int)popupItems().size();
		popupCursor = std::min(popupCursor, n - 1);
		switch (b)
		{
		case Button::Up: popupCursor = (popupCursor + n - 1) % n; break;
		case Button::Down: popupCursor = (popupCursor + 1) % n; break;
		case Button::A: choosePopup(popupCursor, p); break;
		case Button::B: choosePopup(n > 1 ? 1 : 0, p); break;	// Later (or Hide, OK)
		default: break;
		}
		return false;
	}

	bool Ui::listKey(Button b, Player& p)
	{
		// (the browser's Up row is row -1: first; n counts it)
		int first = browse->listFirst(), n = (int)rows(p).size() - first;
		cursor = std::min(cursor, n - 1 + first);
		switch (b)
		{
		case Button::Up: cursor = (cursor - first + n - 1) % n + first; break;
		case Button::Down: cursor = (cursor - first + 1) % n + first; break;
		// a page at a time (the browser): LEFT/RIGHT, and L/R as BananaOS's lists jump
		case Button::Left: cursor = std::max(first, cursor - ROWS_SHOWN); break;
		case Button::Right: cursor = std::min(n - 1 + first, cursor + ROWS_SHOWN); break;
		case Button::A: choose(cursor, p); break;
		case Button::B: page = home(p); break;
		default: break;
		}
		if (cursor < scroll) scroll = cursor;
		if (cursor >= scroll + ROWS_SHOWN) scroll = cursor - ROWS_SHOWN + 1;
		return false;
	}

	void Ui::tick(Player& p)
	{
		// the Display popup's arrows have rested on a style: it's applied now (displayKey)
		if (displayPending >= 0 && SDL_GetTicks() - displayPendingAt >= DISPLAY_SETTLE_MS) displayApplyPending(p);
		// the bar held at a released drag: let go once playback is there, or after 6 s whatever happens (a network file buffers a while)
		if (seekHeld >= 0 && !slideHeld(SL_SEEK) &&
			(p.duration <= 0 || std::fabs(shownPosition(p) - seekHeld * p.duration) < 1.0 || SDL_GetTicks() - seekHeldAt > 6000))
			seekHeld = -1;
		// a held slider whose target is told at a pace: a finger resting on a new value, sent when it's due
		slideLive(p);
		manga->mangaVideoGuard(p);
		if (Area* a = area()) a->tick(p);
		fileJobTick(p);
		rootsCheck(p);
		manga->mangaEngineWatch(p);	// the engine's download sheet: closed when done
		// the rows' durations: none probed while a file opens; the rows on screen first
		if (durWorker)
		{
			durWorker->setBusy(p.loading());
			if (page == Page::Browse && scroll != browse->durLo)
			{
				browse->durLo = scroll;
				durWorker->focus(scroll, scroll + ROWS_SHOWN);
			}
		}
		// a playlist's edits, written once they've stopped for a second
		if (browse->playlists->plDirty && SDL_GetTicks() - browse->playlists->plDirtyAt >= 1000) browse->playlists->plFlush(p);
		// the dashboard starts downloads and asks for a series' chapters: their worker answers are taken here too,
		// since no manga page may be drawn on the handheld while the browser is the one using the reader
		if (p.settings.mangaOn && manga->mangaNet)
		{
			manga->mangaBrowsePoll(p);
			manga->mangaDlAskPump();
			manga->mangaNotesTick(p);
		}
		// the web's equalizer: what mpv keeps, and settings.conf, once its sliders stop for a second
		if (eqWebDirtyAt && SDL_GetTicks() - eqWebDirtyAt >= 1000)
		{
			eqWebDirtyAt = 0;
			p.commitEq();
		}
		// A or the Play button held while a page plays the file: here instead, once, as the hold reaches its length (not
		// as it lets go). Gone off the player (B, the menu), or the page gone: nothing
		// (the mini bar's play mark holds the same way, on any page that has the bar: what it plays is main's player)
		bool holdHere = (page == Page::Main && !inMenu) || (playHoldMini && miniBarShown(p));
		if (playHoldAt && (!holdHere || !pageDrives(barPlayer(p)))) playHoldAt = 0;
		if (!playHoldAt) playHoldMini = false;
		if (playHoldAt && SDL_GetTicks() - playHoldAt >= PLAY_HOLD_MS)
		{
			playHoldAt = 0;
			playHoldMini = false;
			if (playHoldTouch) { pressedBtn = -1; swallowTouch = true; }	// the finger's lift presses nothing
			takeOver(barPlayer(p));
		}
		// a finger held on the mini bar: its menu (the lift that follows chooses nothing)
		if (miniHoldAt && (pressedBtn == MINI_BAR || pressedBtn == MINI_PP) && SDL_GetTicks() - miniHoldAt >= 600)
		{
			miniHoldAt = 0;
			pressedBtn = -1;
			swallowTouch = true;
			if (miniBarShown(p)) openNowMenu(p);
		}
		if (landFlashUntil && SDL_GetTicks() >= landFlashUntil) landFlashUntil = 0;
		if (listPulseAt && SDL_GetTicks() - listPulseAt >= LIST_PULSE_MS) listPulseAt = 0;
		// Home's Recent: listed again when what plays changes (the next song started by itself: its card comes first)
		if (page == Page::Browse && !inMenu && browse->atHome() && !homeTouch && !ctxOpen && p.currentPath() != homeListedFor)
		{
			recentKeepOrder = true;	// (its progress anew, its order as shown)
			browse->enterFolder(HOMEDIR, p);
		}
		// the hand-off from a browser: its position, once the file it opened knows its length (5 s at most)
		if (!seekPath.empty())
		{
			// (a play request still to be opened by main: not yet the file; a photo up: this is the photo viewer, the seek
			// waits for the player)
			if (!playRequest.paths.empty() || p.playingPhoto()) {}
			else if (SDL_GetTicks() > seekUntil || p.currentPath() != seekPath || !p.error.empty()) seekPath.clear();
			else if (p.loaded && p.duration > 0)
			{
				p.seekFraction(std::clamp(seekTo / p.duration, 0.0, 1.0));
				seekPath.clear();
			}
		}
		// Screen off after (Settings): a song or video playing, nothing touched or pressed for that long, nothing open
		// that waits for an answer: the bottom panel dark, as its Screen off button does (a touch or a page's button
		// brings it back). Paused, it stays: someone is about to act. Never while a picture is looked at, whatever
		// plays meanwhile: a photo (panned here) or a comic in the manga reader (its pages are dragged here, and a
		// reader reads longer than any timeout without touching)
		// (a song's own timer, and a video's: Never by default, its controls are what one reaches for)
		int offSecs = screenOffSeconds(p.playingAudio() ? p.settings.screenOffAfter : p.settings.screenOffVideo);
		bool viewing = p.playingPhoto() || pageReader->readerStatus.open;
		if (offSecs > 0 && panel.on() && p.playing() && !viewing && !p.paused && !p.loading() && p.error.empty() &&
			!kbOpen && !quickOpen && !popupShown(p) && !slide.on && SDL_GetTicks() - webInputAt >= (Uint32)offSecs * 1000)
			panel.set(false);
		// a dragged row near the list's top or bottom: the list scrolls under it, a row each 150 ms
		if (browse->playlists->plDrag.on && page == Page::Browse && isPlDir(browse->browseDir) && SDL_GetTicks() - browse->playlists->plDrag.scrollAt >= 150)
		{
			int n = (int)browse->browseRows.size();
			int was = scroll;
			if (browse->playlists->plDrag.y < ROW_Y + 24) scroll = std::max(0, scroll - 1);
			else if (browse->playlists->plDrag.y > ROW_Y + ROWS_SHOWN * ROW_H - 24) scroll = std::min(std::max(0, n - ROWS_SHOWN), scroll + 1);
			if (scroll != was) browse->playlists->plDrag.scrollAt = SDL_GetTicks();
		}
		// a playlist's network item: its folder listed for its subtitles and cover (once a play; netListed answers)
		if (std::string dir = p.netFolderWanted(); !dir.empty() && !sideAsked.count(dir))
		{
			sideAsked.insert(dir);
			NetPath np;
			const NetServer* sv = netStore && parseNetPath(dir, np) ? netStore->find(np.server) : nullptr;
			if (sv && netWorker)
			{
				sidePending[++sideSerial] = dir;
				netWorker->list(sideSerial, *sv, netStore->password(*sv), dir);
			}
			else p.netFolderListed(dir, {}, false);
		}
		stripStep();
		if (likePopAt && SDL_GetTicks() - likePopAt >= 200) likePopAt = 0;
		if (addFlashUntil && SDL_GetTicks() >= addFlashUntil) addFlashUntil = 0;
		if (!flickAt) return;
		if (!p.playingPhoto() || page != Page::Main || panTouch)
		{
			flickAt = 0;
			return;
		}
		Uint32 now = SDL_GetTicks(), dt = now - flickAt;
		if (!dt) return;
		flickAt = now;
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		double x0 = v.panX, y0 = v.panY;
		Rect tr = thumbRect(p);
		if (p.settings.dragBox) p.setPhotoView(v.zoom, x0 - flickVX * dt / tr.w, y0 - flickVY * dt / tr.h);
		else p.setPhotoView(v.zoom, x0 + flickVX * dt / PAN_W / sc.x, y0 + flickVY * dt / PAN_H / sc.y);
		// slowing, and stopped at an edge
		double decay = std::exp(-(double)dt / 300);
		flickVX = p.photo().panX == x0 ? 0 : flickVX * decay;
		flickVY = p.photo().panY == y0 ? 0 : flickVY * decay;
		if (std::hypot(flickVX, flickVY) < 0.02) flickAt = 0;
	}

	bool Ui::act(int id, Player& p)
	{
		// "<" over what plays closes it at once (the place kept); B and Close ask twice, a pocket's press being one
		if (id == BACK_ICON && page == Page::Main && p.playing()) return true;
		// elsewhere (a page over what plays, START's menu): B, back a step
		if (id == BACK_ICON && (inMenu || (page != Page::Browse && page != Page::Main))) return pressAnyway(Button::B, p);
		// Main's Stop (over a photo: Close); "<" elsewhere: B
		if (id == QUIT_BTN)
		{
			if (p.playingPhoto()) return true;
			stopPressed = true;
			return false;
		}
		// adding files: the header's key is a ✕ that cancels at once (B goes up a folder there)
		if (id == BACK_ICON && page == Page::Browse && (browse->playlists->selMode == SEL_ADD || browse->playlists->selMode == SEL_PICK)) { browse->playlists->selBack(p, true); return false; }
		if (id == BACK_ICON) return pressAnyway(Button::B, p);
		// the mini bar: the player, or its play mark
		if (id == MINI_BAR) { goNowPlaying(p); return false; }
		if (id == MINI_PP) { miniToggle(p); return false; }
		if (id == EMPTY_BTN) { browse->doEmptyAction(p); return false; }
		// the select mode's bar: its keys' (Cancel leaves from any folder)
		if (id == SEL_ALL) { browse->playlists->selKey(Button::Select, p); return false; }
		if (id == SEL_MOVE) { browse->playlists->selKey(Button::Y, p); return false; }
		// (the act itself, not its key: adding, X is Cancel and Y adds)
		if (id == SEL_ACT) { browse->playlists->removeArmed = browse->playlists->removeArmed && browse->playlists->selMode == SEL_EDIT; browse->playlists->dropArmed = 0; browse->playlists->selAct(p); return false; }
		// (Cancel shows X's key, so it asks twice as X does; the header's ✕ leaves at once)
		if (id == SEL_CANCEL) { browse->playlists->selBack(p, false); return false; }
		if (id == MOVE_CANCEL || id == MOVE_DONE) { browse->playlists->moveEnd(id == MOVE_DONE, p); return false; }
		if (id == BUSY_CANCEL) { browse->playlists->cancelAdd(p); return false; }
		if (id >= manga->EXT_TILE && id <= manga->EXT_DONE) return manga->extAct(id, p);
		if (id == LIKE_BTN) { likeNow(p); return false; }
		if (id == ADD_BTN)
		{
			if (p.playing()) browse->playlists->openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p);
			return false;
		}
		if (id >= PLAY_ROW && id < PLAY_ROW + 7) return playingChoose(id - PLAY_ROW, p);
		// a song's line under the buttons: the Sound page
		if (page == Page::Main && p.playingAudio() && id == LINE1_BTN)
		{
			sound->open(p, false);
			return false;
		}
		// a video's lines and the chapter line: their tab of Tracks
		if (id == LINE1_BTN || id == LINE2_BTN) { openTracks(id == LINE1_BTN ? TRACKS_AUDIO : TRACKS_SUBS, p); return false; }
		if (id == CHAPTER_BTN) { openTracks(TRACKS_CHAPTERS, p); return false; }
		if (id == GROUP_PREV || id == GROUP_NEXT) { settingsPage->stepGroup(id == GROUP_NEXT ? 1 : -1); return false; }
		if (id == SCREEN_BTN) { panel.set(false); return false; }
		if (id == EXIT_BTN) { quitRequested = true; return false; }
		if (id == LOCK_BTN)
		{
			touchLocked = true;
			p.osd("Touch locked: hold the lock 1 s to unlock");
			return false;
		}
		if (id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS)
		{
			static const Button PHOTO_KEYS[PHOTO_BTNS] = {Button::L, Button::R, Button::Down, Button::Up, Button::L3,
				Button::Select, Button::None, Button::Start};
			Button b = PHOTO_KEYS[id - PHOTO_BTN];
			if (b != Button::None) return pressAnyway(b, p);
			id = INFO_BTN;
		}
		if (id == INFO_BTN)
		{
			page = Page::Info;
			cursor = scroll = 0;
			infoAt = 0;	// read at once
			return false;
		}
		// the grid's Like and Add (L3's place and the one with no key)
		if (page == Page::Main && (id == 1 || id == 3)) return act(id == 1 ? LIKE_BTN : ADD_BTN, p);
		Button b = GRID[id / 5][id % 5].button;
		if (b != Button::Up) return pressAnyway(b, p);
		if (!p.error.empty()) return false;
		// a tap on the speed goes round its choices
		double s = stepSpeed(p.speed, 1, true);
		p.setSpeed(s);
		p.osd("Speed " + speedLabel(s));
		return false;
	}

	bool Ui::pointer(const Pointer& pt, Player& p)
	{
		int x = (int)(pt.x * W), y = (int)(pt.y * H);
		webInputAt = SDL_GetTicks();
		// a list's finger whose lift never came (a dialog took it): a new touch starts afresh
		if (pt.kind == Pointer::Down) pageReader->mlist = PageReader::MangaListDrag();
		// a slider under the finger: it keeps the finger until it lifts, whatever it passes over (a new touch with
		// one still held: its lift was lost, it's let go where it was)
		if (slide.on)
		{
			if (pt.kind != Pointer::Down) return slideFollow(pt, x, y, p);
			slideEnd(p);
		}
		if (topOverlay()) return overlayPointer(pt, x, y, p);
		if (manga->mangaErrOpen)
		{
			// OK, or a tap anywhere: closed
			if (pt.kind == Pointer::Up) manga->mangaErrOpen = false;
			return false;
		}
		mangaOn = p.settings.mangaOn;
		if (fileJob) return fileJobPointer(pt, x, y);
		if (infoOpen) { if (pt.kind == Pointer::Up) infoOpen = false; return false; }
		if (browse->clipPointer(pt, x, y, p)) return false;
		// All recent's tabs: a tap on one shows that kind
		if (page == Page::Browse && browse->browseDir == RECENT && !inMenu && !ctxOpen && p.settings.keepHistory && recentCounts[0] && y < ROW_Y)
		{
			for (int i = 0; i < 4; i++)
				if (recentTabRect[i].hit(x, y))
				{
					if (pt.kind == Pointer::Up && i != recentTab && (i == 0 || recentCounts[i]))
					{
						recentTab = i;
						browse->enterFolder(RECENT, p);
						cursor = browse->firstCursor();
						scroll = 0;
						browse->fitList();
					}
					return false;
				}
		}
		if (kbOpen && panel.on())
		{
			kbPointer(pt, x, y);
			if (sound->eqNameSubmit) sound->nameDone(p);
			if (browse->playlists->plNameSubmit) browse->playlists->plNameDone(p);
			if (browse->fileNameSubmit) browse->fileRenameDone(p);
			return false;
		}
		// dark: a touch anywhere brings the panel back, and does nothing else (the finger can't see what's there)
		if (!panel.on())
		{
			if (pt.kind == Pointer::Down)
			{
				panel.set(true);
				swallowTouch = true;
			}
			return false;
		}
		if (swallowTouch)
		{
			if (pt.kind == Pointer::Up) swallowTouch = false;
			return false;
		}
		if (quickOpen) return quickPointer(pt, x, y, p);
		// Add to... over what plays (or the Playing tab) as over the browser
		if (browse->playlists->pickerOpen) return browse->playlists->pickerPointer(pt, x, y, p);
		playOnOpen = playOnOpen && inMenu && page == Page::Playing && playingTabShown(p);
		if (playOnOpen) return playOnPointer(pt, x, y, p);
		displayOpen = displayOpen && displayShown(p);
		if (displayOpen) return displayPointer(pt, x, y, p);
		// Home's quit dialog takes every finger: a row chooses as it lifts, outside it cancels
		if (page == Page::Browse && !inMenu && browse->atHome() && quitOpen && !popupShown(p)) return homePointer(pt, x, y, p);
		// the header's web dashboard mark: its page, as the finger lifts on it
		if (webIndDown || (pt.kind == Pointer::Down && webInd.w && webInd.hit(x, y) && !ctxOpen && !browse->playlists->pickerOpen && !popupShown(p)))
		{
			if (pt.kind == Pointer::Down) webIndDown = true;
			else if (pt.kind == Pointer::Up)
			{
				webIndDown = false;
				if (webInd.hit(x, y) && page != Page::Web) openWeb();
			}
			return false;
		}
		if (ctxOpen)
		{
			// the folder's menu: a row tapped (as the finger lifts), anywhere else closes it
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < (int)ctxItems.size(); i++)
					if (ctxRow(i).hit(x, y)) pressedBtn = 100 + i;
				if (pressedBtn < 0) ctxOpen = false;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 100)
			{
				int item = pressedBtn - 100;
				pressedBtn = -1;
				if (item != ctxCursor) ctxArmed = false;
				ctxCursor = item;
				if (ctxRow(item).hit(x, y)) chooseBrowseMenu(item, p);
			}
			return false;
		}
		if (Area* a = area(); a && a->pointerFirst()) return a->pointer(pt, x, y, p);
		// locked, over a video: Screen off goes through; a hold on the lock (draw() times it) releases it; nothing else
		if (touchLocked && p.playing() && !p.playingPhoto() && page == Page::Main && pressedBtn != SCREEN_BTN)
		{
			if (pt.kind == Pointer::Down && buttonRect(LOCK_BTN).hit(x, y)) lockHoldAt = SDL_GetTicks();
			else if (pt.kind == Pointer::Down && buttonRect(SCREEN_BTN).hit(x, y)) pressedBtn = SCREEN_BTN;
			else if (pt.kind == Pointer::Up) lockHoldAt = 0;	// let go too soon: still locked
			else if (pt.kind == Pointer::Down && SDL_GetTicks() - lockNoticeAt > 3000)
			{
				lockNoticeAt = SDL_GetTicks();
				p.osd("Touch locked: hold the lock 1 s to unlock");
			}
			return false;
		}

		if (popupShown(p))
		{
			// the updater's pop-up: a row tapped (as the finger lifts); elsewhere, nothing (it's a question)
			int n = (int)popupItems().size();
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < n; i++)
					if (popupRow(i).hit(x, y)) pressedBtn = 200 + i;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 200)
			{
				int item = pressedBtn - 200;
				pressedBtn = -1;
				if (item < n && popupRow(item).hit(x, y)) choosePopup(item, p);
			}
			return false;
		}
		// a network folder read before it's added: its dialog's Cancel, nothing else
		if (page == Page::Browse && browse->playlists->addPending.serial)
		{
			if (pt.kind == Pointer::Down) pressedBtn = buttonRect(BUSY_CANCEL).hit(x, y) ? BUSY_CANCEL : -1;
			else if (pt.kind == Pointer::Up && pressedBtn == BUSY_CANCEL)
			{
				pressedBtn = -1;
				if (buttonRect(BUSY_CANCEL).hit(x, y)) browse->playlists->cancelAdd(p);
			}
			return false;
		}
		if (page == Page::Browse && browse->playlists->pickerOpen) return browse->playlists->pickerPointer(pt, x, y, p);
		// a playlist's row on the finger by its grip: the landing line follows, lifted it's dropped there
		if (browse->playlists->plDrag.on)
		{
			browse->playlists->plDrag.y = std::clamp(y, 0, H - 1);
			if (pt.kind == Pointer::Up) browse->playlists->dragEnd(p);
			return false;
		}
		if (panTouch) return photoPointer(pt, x, y, p);
		if (homeTouch) return homePointer(pt, x, y, p);
		if (listTouch)
		{
			// a list (the tracks, the browser, the settings, the manga's): a drag scrolls it, a tap acts as the finger
			// lifts
			bool set = page == Page::Settings || page == Page::MangaSettings, groups = page == Page::Settings && setGroup < 0;
			bool info = page == Page::Info || page == Page::About;
			auto srows = set && !groups ? (page == Page::MangaSettings ? manga->mangaSettingRows() : settingsPage->settingRows()) : std::vector<int>();
			int n = groups ? GROUPS : set ? (int)srows.size() : info ? (int)infoLines.size() : (int)rows(p).size();
			int rowH = groups ? 84 : set ? SET_H : info ? INFO_H : ROW_H, shown = set ? SET_SHOWN : info ? INFO_SHOWN : ROWS_SHOWN;
			if (pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
			{
				listScrolled = true;
				holdAt = 0;
				int rowsMoved = (int)std::lround((touchStartY - y) / (double)rowH);
				int first = set || info ? 0 : browse->listFirst();
				scroll = std::clamp(scrollStart + rowsMoved, first, std::max(first, n - shown));
				cursor = std::clamp(cursor, scroll, scroll + shown - 1);
			}
			else if (pt.kind == Pointer::Up)
			{
				listTouch = false;
				holdAt = 0;
				if (listScrolled || info) return false;	// the info page only scrolls
				// a setting: the left of its value (its arrow) steps back, the rest of the row on
				int dir = touchX >= SET_ARROW_X - 12 && touchX < 490 ? -1 : 1;
				if (groups) settingsPage->openGroup(cursor);
				else if (set && page == Page::Settings && srows[cursor] == SOUND_ROW) sound->open(p, true);	// a page, not a value
				else if (set) settingsPage->changeSetting(srows[cursor], dir, p);
				else choose(cursor, p);
			}
			return false;
		}
		// a finger that went down on one of the manga app's lists: its moves scroll the list and its lift is the tap
		// (the down came through mangaPointer, after the sheets, the header and the mini bar had their turn)
		if (pageReader->mlist.on && pt.kind != Pointer::Down) return pageReader->mangaListPointer(pt, x, y);
		if (pressedBtn >= 0)
		{
			if (pt.kind != Pointer::Up) return false;
			int id = pressedBtn;
			pressedBtn = -1;
			miniHoldAt = 0;
			// the mini bar's play mark while a page plays the file: let go before the hold took it over here, the
			// page's play / pause
			if (id == MINI_PP && playHoldMini)
			{
				bool on = playHoldAt != 0;
				playHoldAt = 0;
				playHoldMini = false;
				if (on && buttonRect(id).hit(x, y) && pageDrives(barPlayer(p))) toggleBrowser(barPlayer(p));
				return false;
			}
			if (id == PLAY_GRID && page == Page::Main && !inMenu && ((playHoldTouch && playHoldAt) || pageDrives(p)))
			{
				playHoldAt = 0;
				if (buttonRect(id).hit(x, y) && pageDrives(p)) toggleBrowser(p);
				return false;
			}
			// let go off the button: nothing (a finger that slid away changed its mind)
			Rect r = id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS ? photoBtn(id - PHOTO_BTN, p) : buttonRect(id);
			return r.hit(x, y) && act(id, p);
		}
		// the mini bar: acts as the finger lifts (a hold: its menu, tick()); its play mark while a page plays the file
		// holds as Main's Play does (here instead). Its down is taken here for every page, the reader's included
		if (pt.kind == Pointer::Down && miniBarShown(p) && MINI_BOX.hit(x, y))
		{
			Player& bp = barPlayer(p);
			pressedBtn = x < MINI_BOX.x + 44 ? MINI_PP : MINI_BAR;
			miniHoldAt = SDL_GetTicks();
			// in fullscreen the bar is the controls sheet's: a touch on it shows the sheet 4 s more
			if (page == Page::MangaRead && pageReader->readerStatus.fullscreen) manga->mangaCtrlUntil = SDL_GetTicks() + manga->MG_CTRL_MS;
			if (pressedBtn == MINI_PP && pageDrives(bp))
			{
				miniHoldAt = 0;
				playHoldAt = SDL_GetTicks();
				playHoldTouch = true;
				playHoldMini = true;
			}
			return false;
		}
		// an area's page (the readers, the Manga pages): its own touches; the mini bar's taken above. An area whose
		// downs come late (Manga's pages but the reader) has them below, after "<"
		if (Area* a = area(); a && !(pt.kind == Pointer::Down && a->pointerDownLate())) return a->pointer(pt, x, y, p);
		if (pt.kind != Pointer::Down) return false;
		if (inMenu && y < 60)
		{
			// the tab bar: a tab, or "<" (it closes the menu as the finger lifts)
			if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
			auto tabs = menuTabs(p);
			for (int t = 0; t < (int)tabs.size(); t++)
				if (menuTabRect(t, (int)tabs.size()).hit(x, y)) showTab(tabs[t], p);
			return false;
		}
		if (page == Page::Exit)
		{
			if (buttonRect(EXIT_BTN).hit(x, y)) pressedBtn = EXIT_BTN;
			return false;
		}
		if (page == Page::Playing)
		{
			// a row acts as the finger lifts on it
			auto rows = playingRows();
			for (int i = 0; i < (int)rows.size(); i++)
				if (buttonRect(PLAY_ROW + i).hit(x, y)) { cursor = i; pressedBtn = PLAY_ROW + i; }
			return false;
		}

		// "<": B (up a folder; twice, out of what plays or, from the first page, out of the player)
		if (backIconShown(p) && buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		if (browse->emptyAction() && buttonRect(EMPTY_BTN).hit(x, y)) { pressedBtn = EMPTY_BTN; return false; }
		// the Extensions drawer's and the browser's (Browse::pointer), and an area whose downs come late (the Manga pages):
		// their own taps (the mini bar's, the header's and "<" taken above)
		if (Area* a = area(); a && a->pointerDownLate()) return a->pointer(pt, x, y, p);
		if (page == Page::Info || page == Page::About)
		{
			if (y < INFO_Y) return false;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			return false;
		}
		if ((page == Page::Tracks || page == Page::Chapters) && y < 60)
		{
			// a tab switches as the finger lands (as the browser's do)
			auto tabs = tracksTabs(p);
			int cur = page == Page::Chapters ? TRACKS_CHAPTERS : tracksTab;
			for (int i = 0; i < (int)tabs.size(); i++)
				if (tracksTabRect(i, (int)tabs.size()).hit(x, y) && tabs[i] != cur) openTracks(tabs[i], p);
			return false;
		}
		if (page != Page::Main) return listDown(x, y, p);
		if (p.playingPhoto())
		{
			if (panRect(p).hit(x, y))
			{
				panTouch = true;
				panMoved = false;
				flickAt = 0;
				flickVX = flickVY = 0;
				panDownX = lastX = x;
				panDownY = lastY = y;
				lastAt = SDL_GetTicks();
				panStartX = p.photo().panX;
				panStartY = p.photo().panY;
				return false;
			}
			for (int i = 0; i < PHOTO_BTNS; i++)
				if (photoBtn(i, p).hit(x, y)) pressedBtn = PHOTO_BTN + i;
			if (buttonRect(QUIT_BTN).hit(x, y)) pressedBtn = QUIT_BTN;
			return false;
		}
		// the chapter line first: the bar's room around it reaches up over its lower part
		if (chapterLineShown(p) && buttonRect(CHAPTER_BTN).hit(x, y)) { pressedBtn = CHAPTER_BTN; return false; }
		// a video's track lines: each opens its tab
		if (!p.playingAudio() && p.error.empty())
			for (int id : {LINE1_BTN, LINE2_BTN})
				if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		// a song's: its first line opens Sound
		if (p.playingAudio() && p.error.empty() && buttonRect(LINE1_BTN).hit(x, y)) { pressedBtn = LINE1_BTN; return false; }
		// the bar is easier to hit with some room around it
		if (Rect{BAR.x - 8, BAR.y - 20, BAR.w + 16, BAR.h + 40}.hit(x, y) && p.duration > 0)
		{
			// a trackpad's scrub: a slide moves the place on by the finger's travel, a tap jumps there as it lifts
			// (drawMain shows slide.value while it's held). The picture
			// follows too, keyframes only and no more often than the decoder keeps up with (SCRUB_MS); lifted, the
			// exact seek to where it was left, and the bar held there until playback arrives (seekHeld)
			slideGrab(SL_SEEK, 0, {BAR, {0, 1, 0}, false, SCRUB_MS, seekHeld >= 0 ? seekHeld : shownPosition(p) >= 0 ? std::clamp(shownPosition(p) / p.duration, 0.0, 1.0) : 0}, x, y, p, [this, scrubs = 0](double f, bool last, Player& p) mutable {
				if (!last)
				{
					scrubs++;
					p.scrub(f);
					return;
				}
				fprintf(stderr, "seek bar: let go at %.4f after %d scrubs\n", f, scrubs);
				p.seekFraction(f);
				noteSeek(p, f * p.duration);
				seekHeld = f;
				seekHeldAt = SDL_GetTicks();
			});
			return false;
		}
		for (int id : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, QUIT_BTN, SCREEN_BTN, INFO_BTN, LOCK_BTN})
			if (buttonRect(id).hit(x, y)) pressedBtn = id;
		// Play while a page plays it: held, it plays here (tick() times it); let go sooner, the page's play/pause
		if (pressedBtn == PLAY_GRID && page == Page::Main && !inMenu && p.error.empty() && pageDrives(p))
		{
			playHoldAt = SDL_GetTicks();
			playHoldTouch = true;
		}
		return false;
	}

	bool Ui::listDown(int x, int y, Player& p)
	{
		if (y < ROW_Y || y >= ROW_Y + ROWS_SHOWN * ROW_H) return false;
		int i = scroll + (y - ROW_Y) / ROW_H;
		if (i >= (int)rows(p).size() || i < browse->listFirst()) return false;
		cursor = i;
		listTouch = true;
		listScrolled = false;
		touchStartY = y;
		scrollStart = scroll;
		holdAt = page == Page::Browse && !inMenu && browse->playlists->selMode == SEL_NONE && browse->holdRow(i) ? SDL_GetTicks() : 0;
		return false;
	}

	std::string Ui::signature(const Player& p) const
	{
		const Track* a = p.selected(TrackType::Audio);
		const Track* s = p.selected(TrackType::Sub);
		char buf[256];
		snprintf(buf, sizeof buf, "%d %d %d %d %ld %ld %d %d %lld %lld %zu %d %d %.2f %d %d %d %d|", (int)page, cursor, scroll,
			p.paused + setGroup * 2 + 2, (long)std::floor(shownPosition(p)) * 2 + (predicting(p) && pagePlaying()), (long)p.duration, (int)p.aspect, p.loaded, a ? (long long)a->id : 0LL,
			s ? (long long)s->id : 0LL, p.tracks.size(), slideHeld(SL_SEEK) + 2 * (seekHeld >= 0), slideHeld(SL_SEEK) ? (int)(slide.value * 1000) : 0, p.speed,
			p.index(), p.count(), pressedBtn + touchLocked * 100 + (lockHoldAt != 0) * 1000, listTouch * 1000 + browse->browseVersion + (clearArmed != 0) * 100000 + (int)p.history().items().size() * 1000000);
		std::string photo;	// (the browser's section and its listing: Browse::signature)
		// a slider under a finger: which, and where the finger has it (what draws differently while held reads these)
		if (slide.on) photo += "|sl" + std::to_string(slide.id) + "." + std::to_string(slide.sub) + ":" + std::to_string(slide.value);
		if (touchLocked && lockHoldAt) photo += "|hold" + std::to_string((SDL_GetTicks() - lockHoldAt) / 100);
		if (playHoldAt) photo += "|phold" + std::to_string((SDL_GetTicks() - playHoldAt) / 50);
		if (pageDrives(p)) photo += pagePlaying() ? "|pageplay" : "|pagepause";
		if (p.playingPhoto())
		{
			const Player::PhotoView& v = p.photo();
			char pb[160];
			snprintf(pb, sizeof pb, "|photo %.4f %.4f %.4f %d %d %d %d", v.zoom, v.panX, v.panY, v.rotate, v.fill, p.thumbId, p.photoWidth());
			photo = pb;
		}
		// the backlights' levels while their rows show (the keys change them too)
		std::string upd = quickOpen ? "|quick" + std::to_string(quickCursor) + " " + std::to_string(volumePct) + " " + std::to_string(pressedBtn) +
			" " + std::to_string(backlight.permille(Backlight::TOP)) + "," + std::to_string(backlight.permille(Backlight::BOTTOM)) : "";
		if (updater)
			upd += "|upd" + std::to_string((int)updater->state()) + " " + std::to_string(updater->progress()) + " " + updater->available() +
				" " + std::to_string(popupShown(p)) + std::to_string(popupCursor) + std::to_string(pressedBtn);
		// the chapters: the list, the one playing (the Chapters page's progress goes by the position, above), the tab
		photo += "|ch" + std::to_string(p.chapterSerial) + " " + std::to_string(p.chapters.size()) + " " +
			std::to_string(shownChapter(p)) + " t" + std::to_string(tracksTab);
		// Up next: the list and the one playing (the Session's version moves with both)
		if (page == Page::UpNext) photo += "|un" + std::to_string(SessionHost::get().version()) + " " + std::to_string((int)p.paths().size());
		// the Display popup over a song's page (the Sound page's own terms are its area's signature)
		if (displayOpen)
			photo += "|dsp" + std::to_string(displayShown(p) ? displayRow * 100 + displayPending + 1 : -1) + " " + std::to_string(p.playingAudio());
		// the dots of "Loading..." go round
		if (p.loading() || p.thumbPending()) photo += "|loading" + std::to_string(SDL_GetTicks() / 400 % 4);
		// Playlists: the store, items found missing, a menu's title and line, a name refused
		photo += "|pl" + std::to_string(browse->playlists->plStore ? browse->playlists->plStore->serial() : -1) + " " + std::to_string(browse->playlists->plMissingSerial) + ctxTitle + "|" + ctxNote + "|" + browse->playlists->plNameNote;
		// selecting: the mode and where it adds, the ticks, Move's block, a dragged row's landing (the finger in steps
		// of 8), the picker, the arms, a folder being read
		if (browse->playlists->selMode != SEL_NONE || browse->playlists->pickerOpen || browse->playlists->addPending.serial)
		{
			char sb[200];
			snprintf(sb, sizeof sb, "|sel%d %d %d %d %d,%d %d %d,%d %d %d %d %d %d %d", browse->playlists->selMode, browse->playlists->selCount(), browse->playlists->selSerial, browse->playlists->moving,
				browse->playlists->plSel.empty() ? -1 : *browse->playlists->plSel.begin(), (int)browse->playlists->plSel.size(), browse->playlists->plDrag.on, browse->playlists->plDrag.on ? browse->playlists->dragSlot() : -1, browse->playlists->plDrag.y / 8, browse->playlists->pickerOpen,
				browse->playlists->pickerCursor, browse->playlists->pickerScroll, browse->playlists->removeArmed, browse->playlists->dropArmed != 0, browse->playlists->addPending.serial);
			photo += sb + browse->playlists->selDest;
		}
		// Main: what it plays from, and whether the file is a server's (a playlist's item says where it comes from)
		if (p.playing() && page == Page::Main)
		{
			const PlaySource& src = p.source();
			photo += "|src" + std::to_string((int)src.kind) + (src.shuffled ? "s" : "") + (isNetPath(p.currentPath()) ? "n" : "") +
				(p.netWaiting() ? "w" : "") + src.title + "|" + src.id;
		}
		photo += webSig(p);
		// where it plays (the Playing tab's Play on row, the picker, the status line), and the pages it could
		{
			SessionHost& sh = SessionHost::get();
			// (the play mode's Shuffle: the music key, the status line)
			photo += "|out" + outputName() + (sh.pageWaits() ? "w" : "") + (sh.session().shuffle ? "S" : "") + " " + std::to_string(playOnOpen) + std::to_string(playOnCursor) +
				" " + std::to_string(playOnScroll);
			if (page == Page::Playing || playOnOpen)
				for (auto& c : outChoices()) photo += "," + c.name;
		}
		// Like: the heart's pop, the Add square's check, the picker's one file and its question
		{
			Uint32 now = SDL_GetTicks();
			photo += "|like" + std::to_string(likePopAt && now - likePopAt < 110) + std::to_string(addFlashUntil && now < addFlashUntil) +
				std::to_string(browse->playlists->pickerArmed) + browse->playlists->pickerOne + "|" + std::to_string(likedNow(p));
		}
		// the clock: its minute
		if (!clockText(p).empty()) photo += "|clk" + std::to_string((long long)time(nullptr) / 60);
		if (incognito) photo += "|incognito";	// its mark beside the clock, its row in Manga settings
		// behind the browser: the mini bar (what it shows, its press, its flash), the rows' chip, the header's mark (its
		// frame while it moves), what was stopped for a photo
		{
			char bb[160];
			snprintf(bb, sizeof bb, "|bg%d %d %d %d %d %d %d %d %.0f", bgPlay, miniBarShown(p), chipKind, landFlashUntil != 0 ? 1 : listPulseAt ? 2 + (int)((SDL_GetTicks() - listPulseAt) / 33) : 0, playHoldMini,
				markW(p), markW(p) && markLive(p) && panel.on() ? (int)(SDL_GetTicks() / 166) : -1, p.loading(), 0.0);
			photo += bb + chipPath + "|" + p.currentPath();
		}
		// the mini bar over a photo: it shows main's player, whose title, place and state are not the photo viewer's
		if (miniBarShown(p) && &barPlayer(p) != &p)
		{
			const Player& bp = barPlayer(p);
			char nb[240];
			snprintf(nb, sizeof nb, "|np%d %d %d %ld %ld %d %d", bp.paused, (int)bp.loading(), bp.error.empty() ? 0 : 1,
				(long)std::floor(shownPosition(bp)), (long)bp.duration, (int)bp.playingAudio(), (int)bp.playing());
			photo += std::string(nb) + bp.title + "|" + bp.currentPath();
		}
		// Home: the focus, the strip's scroll, a finger on a card or a tile, the tiles' lines (the connections)
		if (browse->atHome())
		{
			char hb[120];
			snprintf(hb, sizeof hb, "|home%d %d %d %ld %d %zu %d", homeZone, homeStrip, homeGrid, std::lround(stripOff), homePress,
				recent.size(), netStore ? netStore->version() : -1);
			photo += hb;
			// B's quit dialog over it, and the row it has the focus on
			photo += std::string("|quit") + std::to_string(quitOpen) + std::to_string(quitCursor);
		}
		// Extensions: the drawer's tile and the note's focus, the switch, a finger's button down on them (the area's
		// pages add it in their signature; here, the drawer and any other page while Browse is on it)
		if (browse->browseDir == EXTDIR && area() != manga.get()) photo += manga->extSignature();
		// Files' copy or move: how far, and the file on its way; what's held for Copy / Move
		photo += fileJobSignature();
		if (infoOpen) photo += "|info" + infoTitle + std::to_string(infoRows.size());
		if (Area* a = area()) photo += a->signature(p);
		return buf + photo + overlaySignature(p) + "|me" + std::to_string((int)manga->mangaErrOpen) + "|ld" + (loaderShown ? std::to_string(SDL_GetTicks() / 110) : std::string()) + "|xn" + manga->extNoticeText() + "|" + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (inMenu ? "|menu" + std::to_string(menuTab) : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) + ctxStatus + (ctxArmed ? "!" : "") : "") + upd
			+ (kbOpen ? "|kb" + sound->eqNameNote + std::to_string(kbCursor) + "," + std::to_string(kbPressed) + "," + std::to_string((int)kbShift) + (kbSymbols ? "s" : "") + std::to_string(kbEdit.caret) + kbEdit.text + (kbCaretShown() ? "|" : "") : "");
	}
}
