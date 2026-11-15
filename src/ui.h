#pragma once
#include "battery.h"
#include "browser.h"
#include "input.h"
#include "panel.h"
#include "player.h"
#include "touch.h"
#include "updater.h"
#include "backlight.h"
#include "volume.h"
#include "slider.h"
#include "netstore.h"
#include "networker.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <algorithm>
#include <array>
#include "keyboard.h"
#include "playlists.h"
#include "discovery.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

namespace bp
{
	class WebServer;
	class DurationWorker;

	// the on-screen keyboard's field for a repository's URL (Ui's Sources page; the form's are in ui_internal.h)
	inline constexpr int SRC_URL_FIELD = 62;
	// the on-screen keyboard's field for a source's search (apart from the form's, a preset's and a playlist's)
	inline constexpr int MANGA_SEARCH_FIELD = 63;	// (62: the repository URL)
	// the saved servers' form (ui_manga_servers.cpp): its four typed fields. 62 and 63 are the tabs' own.
	inline constexpr int MGSRV_NAME_FIELD = 64, MGSRV_URL_FIELD = 65, MGSRV_USER_FIELD = 66,
		MGSRV_PASS_FIELD = 67;
	// a source filter's text value (ui_manga_filters.cpp); 60-67 are the other fields
	inline constexpr int MANGA_FILTER_FIELD = 68;

	// The bottom panel: what is playing and where, the transport (previous, -5 s, play, +10 s, next), the choices
	// (audio, subtitles, aspect, speed, settings) and their pages. Laid out on 640x480 (the RG DS's panel) and scaled
	// to the panel it is on.
	class Ui
	{
	public:
		static constexpr int W = 640, H = 480;
		// a small picture: rows of cells, '#' drawn (ui.cpp's G_*)
		struct Glyph { const char* const* rows; int h; };

		// fontsDir: assets/fonts, the panel's font and the subtitle preview's
		Ui();
		bool init(SDL_Window* window, const std::string& fontsDir);
		~Ui();

		// true when the player should leave
		bool press(Button b, Player& p);
		// a button let go (keys and the pad; the scripted steps right after their press): only A cares, for its hold
		// while a page plays the file
		void release(Button b, Player& p);
		// a held button that repeats here (seeking, moving in a list, photo to photo)
		bool repeats(Button b, const Player& p) const;
		// called each turn of the loop: a flicked photo glides on
		void tick(Player& p);
		// something moves by itself (a flick, a playlist's rows scrolling under a dragged one): the loop should come
		// round often
		bool animating() const;
		// the header's playing mark moves (something plays behind the browser, the bottom panel lit): the loop comes
		// round a few times a second for its frames
		bool markMoving() const { return markAnim && panel.on(); }
		// the playlist edited in the browser, written now (its edits are written a second after they stop): before
		// quitting, or stopping what plays
		void flushPlaylist(Player& p);
		// a finger (or, on a computer, the mouse) on the bottom panel; true when the player should leave
		bool pointer(const Pointer& pt, Player& p);

		// the browser: the page shown with no video, opened at the last folder (Remember last folder) or Home
		// folder: where to open (a folder given on the command line), else the remembered one or Home
		void startBrowser(Player& p, const std::string& folder = "");
		// back to the browser from a video: the folder of the video last played, that video selected (whatever
		// Remember last folder says: that's for the next launch). from: what it played from (read before stop());
		// a playlist goes back to that playlist, the cursor on the item (index: the player's, in the order played)
		void showBrowser(Player& p, const std::string& lastPlayed, const PlaySource& from = {}, int index = -1);
		// the saved playlists' folder (<stateDir>/playlists); empty: there are none to have
		void setStateDir(const std::string& stateDir);
		// the row the browser's cursor is on (Home: the card or the tile it's on), for test runs
		std::string browseSelection() const;
		// the player shown (a pick, Now playing, SELECT, a photo...): where it was opened from is kept, for the way back
		void showPlayer();
		// what plays (a song or a video) has its screen as the bottom panel's base: Main, or a page or the menu over it.
		// False while it plays behind the browser (the player left with B, "<" or Back)
		bool playerShown(const Player& p) const { return p.playing() && !bgPlay; }
		// the player's own pages: over an open reader (the mini bar tapped while reading), the top panel shows what
		// plays, not the pages; B comes back to the reader, and its pages to the top panel
		bool topShowsPlayer() const
		{
			return page == Page::Main || page == Page::Tracks || page == Page::Chapters || page == Page::Info || page == Page::UpNext;
		}
		// the player's own Stop (Main's Stop, START › Playing › Stop, the mini bar's menu), once: main stops what plays
		bool takeStopPressed() { bool r = stopPressed; stopPressed = false; return r; }
		// the mini bar's tap over a photo: the photo closes and the player of what plays takes the screen (once; main
		// does it: only it can close the photo viewer)
		bool takePlayerOverPhoto() { bool r = playerOverPhoto; playerOverPhoto = false; return r; }
		// the player the mini bar works on: main's (songs and videos), set by main, also while a photo is shown. The
		// photo viewer is a Player of its own and a page of its own: the bar never plays the photo, only what goes
		// on under it. Without main's (a test, a caller that has none) it is the player it is given
		Player* mainPlayer = nullptr;
		// what plays stopped (any Stop, the end of the list): a page playing the file has nothing held here any more
		// (the page plays on: not told)
		void stopped() { playHoldAt = 0; }
		// Back from the player (B, "<", Back; its Stop; a photo closed): where it was opened from, as it was (the page,
		// the folder, the cursor and the scroll; Home, a playlist, START's menu...), what plays going on behind it. Never
		// opened from there (started with a file), or Show in folder asked for: the browser on lastPlayed (showBrowser)
		void backFromPlayer(Player& p, const std::string& lastPlayed, const PlaySource& from = {}, int index = -1);
		// opened behind the browser (a PlayRequest's stay): the browser stays the base page
		void playBehind() { bgPlay = true; }
		bool browsing() const { return page == Page::Browse; }
		std::string browseFolder() const;
		// a video picked in the browser, once: the caller opens it
		// the browser's rows as they are listed (label, then note; Home: the Recent cards, then the tiles), for test runs
		std::vector<std::string> browseLabels() const;
		// the Info or About page's lines as they are drawn, for test runs
		std::vector<std::string> pageLines() const
		{
			std::vector<std::string> out;
			for (auto& l : infoLines) out.push_back(l.header ? "[" + l.label + "]" : l.label + ": " + l.text);
			return out;
		}
		std::string takeOpenRequest() { std::string r; r.swap(openRequest); return r; }
		// a playlist picked in the browser (an item, Play, Shuffle play), once: the caller opens it (openPlaylist)
		// names: a network folder's listing (open's folderNames); stay: opened behind the browser, not on the player
		struct PlayRequest { std::vector<std::string> paths; int index = 0; PlaySource source; std::vector<std::string> names; bool stay = false; };
		bool takePlayRequest(PlayRequest& out)
		{
			if (playRequest.paths.empty()) return false;
			out = std::move(playRequest);
			playRequest = {};
			return true;
		}
		// ---- the top panel's page reader: its channel (the request, the commands, the status) and the views the PDF
		// viewer and the Manga reader share (the pages grid, fullscreen, the zoom), all in ui_reader.h / ui_reader.cpp ----
		// (public: main talks to it for the top panel's MangaReader)
		struct PageReader;
		std::unique_ptr<PageReader> pageReader;
		// ---- extensions and the manga reader: an area (Area), all of it in ui_manga.h, ui_manga*.cpp, ui_ext.cpp ----
		// Manage extensions, the Manga pages, and Home's Extensions drawer (a Browse folder: Browse draws, keys and
		// touches it through the area's methods). (public: main and the dashboard's routes reach its store, caches, workers)
		struct Manga;
		std::unique_ptr<Manga> manga;
		// ---- Files' document viewers: an area (Area, below), all of it in ui_docs.h / ui_docs.cpp ----
		// (public: main talks to it for the top panel's text viewer and the PDF viewer's pan area)
		struct Docs;
		std::unique_ptr<Docs> docs;
		// ---- the browser (Home, Files, Playlists, Network, All recent): an area (Area), ui_browse.h; its code in
		// ui_browse.cpp, ui_fileops.cpp, ui_forms.cpp ----
		// (public: the dashboard's routes reach its folder, rows and form)
		struct Browse;
		std::unique_ptr<Browse> browse;
		// the playlists: a plain struct the browser owns (browse->playlists), ui_playlists.h / ui_playlists.cpp
		struct Playlists;
		// Home and All recent: a plain struct the browser owns (browse->homeView), ui_home.h / ui_home.cpp
		struct Home;
		// a document from Files: opened in its viewer; false when the path isn't one
		bool openDocument(const std::string& path, Player& p);
		// started with no file: the video's Quit closes it (back to the browser)
		bool browserMode = false;
		// Settings' Quit Banana Player: out of the player, from the browser or a video
		bool quitRequested = false;
		// the built-in updater (main's), and "Restart now": out with Updater::RESTART_CODE (launch.sh starts again)
		Updater* updater = nullptr;
		bool restartRequested = false;
		// the saved network connections and the thread that lists their folders (main's)
		NetStore* netStore = nullptr;
		NetWorker* netWorker = nullptr;
		// a network folder's listing, from the worker (forwarded by main); dropped if the browser has moved on
		void netListed(NetListing* listing, Player& p);
		// the local file rows' durations, probed on main's worker (null: only the ones already known are shown), and
		// its answers (main forwards its event): into the rows' notes
		DurationWorker* durWorker = nullptr;
		void durationsReady(Player& p);
		// the on-screen keyboard is up: its caret blinks (the loop wakes often enough to draw it)
		bool typing() const { return kbOpen; }
		// Find on the network: discovery runs here, its hosts sent to hostFound (on the main thread: main forwards them)
		Discovery* discovery = nullptr;
		std::function<void(const FoundHost&)> onFound;
		void hostFound(const FoundHost& h, Player& p);
		// a network folder's file names, as last listed (none if it wasn't): where a file picked there finds its
		// subtitles, its cover and the files that play after it
		std::vector<std::string> netFolderNames(const std::string& dir) const;
		// F alone: quick settings over whatever's on the bottom panel (volume, brightness, Same for both screens, Quit);
		// F again, B or a tap outside closes them
		void toggleQuick(Player& p);
		bool quickShown() const { return quickOpen; }
		// a level from bananad (its keys: "volume" or "brightness", percent): the quick settings show it
		void levelChanged(const std::string& what, int percent) { if (what == "volume") volumePct = std::min(100, percent); }	// (the handheld's own: past 100 is the player's gain)

		// the bottom panel's backlight: off from its button, on again at the next touch
		bool screenOn() const { return panel.on(); }
		bool touchIsLocked() const { return touchLocked; }
		// START's menu's open tab ("playing", "settings", "about", "exit"), "" while it's closed: for test runs
		const char* menuTabName() const
		{
			static const char* names[] = {"settings", "about", "exit", "playing"};
			return inMenu ? names[menuTab] : "";
		}

		// The web dashboard (BP_WEB builds): main's server, null till it's first turned on, and its switch (start or
		// stop it, on the main thread). The routes reach the rest through WebPlayerGlue and WebPlaylistGlue
		// (src/web/api_*.cpp), on the main thread too
		WebServer* web = nullptr;
		std::function<void(bool)> webPower;
		// Page::Web open: a browser signing in needs no toast there (the page counts it)
		bool webPageShown() const { return page == Page::Web; }
		// a folder changed from the web (an upload, a delete): listed again if the browser shows it
		void folderChanged(const std::string& dir, Player& p);
		// the volume card shown by a change from the web: how long main keeps it up (as takeCardShown), once
		int takeLevelShown() { int ms = levelMs; levelMs = 0; return ms; }
		// the web's Stop: main closes what plays (back to the browser), once
		bool takeStopRequest() { bool r = stopRequest; stopRequest = false; return r; }
		// the handheld's volume, percent (-1 unknown): read once, then followed (bananad's keys, the quick settings, the web)
		int volumeLevel();
		// the volume as heard, 0 to Max volume: the handheld's (0-100), or past 100 the player's own gain over its 100%
		int volumeTotal(Player& p);
		// the volume set to v as heard (the web's bar, the volume keys past 100%): the handheld's to min(v, 100), the
		// gain to the rest; force: the handheld's set even if it seems at that already (bananad's own key press lowered
		// it). The first time past 120% in a run, a warning
		void setVolumeTotal(Player& p, int v, bool force = false);
		bool loudWarned = false;
		// the browser tab playing the sound: its own volume as it last said (POST /api/browser-volume), -1 not known.
		// The handheld's volume keys move it while the tab is the output; the handheld's own is left as it was
		int browserVol = -1;
		void keepHandheldVolume(int v) { if (v >= 0 && volume.present()) volume.set(v); if (v >= 0) volumePct = v; }
		// the playlists' store serial (-1 none): the web's lists follow it
		int playlistSerial() const;
		// Like (the built-in Liked playlist). likeable: something plays that can be liked (a song or video, not a photo)
		// and there's a state folder. likedNow: it is liked (cheap: a cached set). setLikedNow(want) / toggleLike set it
		// and return what it is now; false-able only by a failed write (osd says so). The pages that show Liked follow.
		bool likeable(const Player& p) const;
		bool likedNow(const Player& p) const;
		bool setLikedNow(Player& p, bool on);
		bool toggleLike(Player& p) { return setLikedNow(p, !likedNow(p)); }
		// the same for any path (the web's Like, a browser row): title/duration "" / -1 when not known
		// at, dropped: PlaylistStore::setLiked's (the web's Undo, and the one a full Liked lost)
		bool setLikedPath(Player& p, const std::string& path, const std::string& title, double duration, bool on, int at = 0,
			std::string* dropped = nullptr);
		bool plIsLiked(const std::string& file) const;
		// a page in a browser plays what plays here (the Session's output is that tab): the handheld holds it paused
		// a file job (copy, move, delete, a network copy of a book) is running: the idle sleep waits for it
		bool fileJobActive() const { return (bool)fileJob; }
		bool pageIsOut() const;
		// how this page can take a page's playback back, for the top panel's card: 1 hold A (the player), 2 elsewhere
		// (A does something else there: the card says the player can)
		int takeOverHint(const Player& p) const;
		// the top panel's level card (Volume / Brightness, as Player::showLevel has mpv draw it) as RGBA pixels, at the
		// panels' scale: for a top panel mpv doesn't draw (the manga reader's pages)
		// percent < 0: a small card of the label alone, without the bar (the reader's zoom)
		std::shared_ptr<const std::vector<uint8_t>> levelCard(const std::string& label, int percent, int& w, int& h);
		std::string outputName() const;	// where it plays now: "Handheld", or the page's name ("Chrome on Mac")
		std::string pageName() const;	// the output page's name, as it gave it ("Browser" unknown)
		// while a page plays the file, the paused copy here stays where it was (no seek per report: that decodes a frame):
		// the bar, times and chapter show the session's place (posNow); a seek made here is the session's Seek
		bool predicting(const Player& p) const;
		double shownPosition(const Player& p) const;
		int shownChapter(const Player& p) const;
		void noteSeek(const Player& p, double t);	// a seek made here meanwhile: the session's Seek (the page follows)
		// a page plays, and the handheld's A is its (Play / Pause of the session)
		bool pageDrives(const Player& p) const;
		// A (or the Play button) while a page drives: a short press plays or pauses the page (the session's Play /
		// Pause, the state wanted: a press seen twice can't undo itself); held PLAY_HOLD_MS it plays here (takeOver)
		static constexpr Uint32 PLAY_HOLD_MS = 600;
		Uint32 playHoldAt = 0;
		bool playHoldTouch = false;	// the hold is a finger's on the Play button (else A's)
		bool pagePlaying() const;	// the page plays (the session's wish)
		void toggleBrowser(Player& p);
		void takeOver(Player& p);
		// an audio track or subtitles (id 0: off) chosen, as the Audio & Subtitles page chooses one (and the web): the
		// language remembered as the settings say, and "Audio: <its label>" over the top panel. false: no such track
		bool chooseTrack(Player& p, TrackType type, int64_t id);

		// redraws when what it would show changed since the last frame
		void draw(const Player& p, bool force = false);
		// the bottom panel's pixels (RGBA, ABGR8888), drawn afresh: screenshots; the caller frees it
		SDL_Surface* capture(const Player& p);

	private:
		// "at 12:03": where a video was left (a song: "")
		std::string resumeNote(const std::string& path, Player& p);
		friend struct WebPlayerGlue;
		friend class SessionHost;
		friend struct WebPlaylistGlue;
		enum class Page { Main, Tracks, Chapters, Settings, Browse, Info, About, Exit, Sound, Web, Playing, UpNext, Manga,
			Extensions, MangaSeries, MangaRead, MangaSettings, MangaSources, DocText, DocPages };
		struct Rect { int x, y, w, h; bool hit(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
		// An overlay: a dialog or a sheet over the page (ui_overlay.cpp; docs/notes/overlay-stack.md). While it is on
		// the stack it gets the keys and the touches (the topmost one only: it is modal), is drawn over the page (all
		// of them, the oldest first) and has its state in signature(): press, pointer, renderPage and signature ask
		// the stack, so a new dialog is wired in nowhere else. One is written as a struct nested in Ui (declared
		// here, defined in its own .cpp), which lets it use Ui's drawing and state through `ui`.
		struct Overlay
		{
			Ui& ui;
			bool done = false;	// closed: taken off the stack once the call that closed it is over
			explicit Overlay(Ui& u) : ui(u) {}
			virtual ~Overlay() = default;
			// what it is ("light", "alert"): one of a kind is open at a time, and signature() says which
			virtual const char* name() const = 0;
			// as Ui::press and Ui::pointer (x, y in the panel's 640x480 units): true leaves the player
			virtual bool key(Button b, Player& p) = 0;
			virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;
			virtual void draw(const Player& p) = 0;
			// everything draw() shows that Settings doesn't hold (the cursor, a level read from elsewhere): the
			// panel is drawn again only when this changes
			virtual std::string signature(const Player& p) const = 0;
			void close() { done = true; }
		};
		std::vector<std::unique_ptr<Overlay>> overlays;	// the stack: the last is on top
		void pushOverlay(std::unique_ptr<Overlay> o);
		Overlay* topOverlay() const;	// (null: none open)
		Overlay* findOverlay(const char* name) const;
		void dropClosedOverlays();
		bool overlayKey(Button b, Player& p);
		bool overlayPointer(const Pointer& pt, int x, int y, Player& p);
		void drawOverlays(const Player& p);
		std::string overlaySignature(const Player& p) const;
		// An area: a part of the app with pages of its own (the document viewers: Docs, ui_docs.h), its state and
		// its handlers in one struct instead of in Ui (docs/notes/ui-areas.md). While one of its pages is
		// shown, press, pointer, tick, renderPage, signature and animating ask it, so a feature inside an area is
		// written in the area's own header and .cpp, and a new area is wired into those six nowhere. One is written
		// as a struct nested in Ui (declared in ui.h by name only, defined in its own header), which lets it use
		// Ui's drawing and state through `ui`; the Player comes with each call, as for every Ui method.
		struct Area
		{
			Ui& ui;
			explicit Area(Ui& u) : ui(u) {}
			virtual ~Area() = default;
			virtual bool has(Page pg) const = 0;	// the page is one of its own
			// as Ui::press and Ui::pointer (x, y in the panel's 640x480 units): true leaves the player
			virtual bool key(Button b, Player& p) = 0;
			virtual bool pointer(const Pointer& pt, int x, int y, Player& p) = 0;
			virtual void tick(Player&) {}	// each turn of the loop while its page shows
			virtual void draw(const Player& p) = 0;
			// everything draw() shows that Settings doesn't hold: the panel is drawn again only when this changes
			virtual std::string signature(const Player& p) const = 0;
			virtual bool animating() const { return false; }	// something of its moves by itself (Ui::animating)
			// its touches come before the updater's pop-up and the mini bar (a page that always had them so); the rest after
			virtual bool pointerFirst() const { return false; }
			// its downs come late: after the header's "<", the empty page's button and the menu's tabs (its moves and lifts
			// still come where the others' do). Manga's pages took them so (asked per page: the reader's come early)
			virtual bool pointerDownLate() const { return false; }
		};
		std::vector<Area*> areas;	// (each owned by its own member, `docs`, `manga`...; listed in Ui's constructor, ui.cpp)
		Area* area() const;	// the one whose page is shown (null: a page that is still Ui's own)
		struct Row { int64_t id; std::string label; bool selected; std::string note = ""; };

		std::vector<Row> rows(const Player& p) const;
		// a list page's own keys (the tracks, Up next, the browser): UP/DOWN, a page with LEFT/RIGHT, A chooses, B home
		bool listKey(Button b, Player& p);
		// a finger down on a list page's rows: the row under it the cursor, the touch begun (the drag or the tap is
		// Ui::pointer's)
		bool listDown(int x, int y, Player& p);
		void openMenu(Page pg, const Player& p);
		// Tracks: one page, four tabs in this order, those that apply (tracksTabs): Chapters (a file with chapters; the
		// Chapters page, Page::Chapters), Subtitles, Audio and Picture (Fit, Fill, Stretch) (a video; Page::Tracks).
		// L/R (and Y/X) step between them. SELECT on a video opens the last one shown (openTracksPage); none shown yet:
		// Chapters where there are some, else Subtitles. A line of the player's opens its own tab
		enum TracksTab { TRACKS_CHAPTERS, TRACKS_SUBS, TRACKS_AUDIO, TRACKS_PICTURE };
		int tracksTab = TRACKS_SUBS;
		bool tracksTabUsed = false;	// a tab was shown this run (X opens it again)
		TrackType trackType() const { return tracksTab == TRACKS_AUDIO ? TrackType::Audio : TrackType::Sub; }
		std::vector<int> tracksTabs(const Player& p) const;
		void openTracks(int tab, const Player& p);
		void openTracksPage(const Player& p);
		void stepTracksTab(int dir, const Player& p);	// the tab before (-1) or after (+1), no wrap
		// the i-th of n tabs shown: three or fewer 152 wide, four share the room before the clock
		// the tabs share the room between the back key and the header's status (clock, Wi-Fi, battery): tabsRight, set
		// as the header is drawn (statusLeft), so a tap hits what was drawn
		int tabsRight = 630;
		Rect tracksTabRect(int i, int n) const
		{
			int room = tabsRight - 70, step = std::min(n > 3 ? 122 : 156, n > 0 ? room / n : room);
			return {70 + i * step, 10, step - 4, 44};
		}
		void drawTracksHeader(const Player& p);
		// the file's chapters (the Tracks page's Chapters tab): the cursor on the one playing
		void openChapters(const Player& p);
		void drawChapters(const Player& p);
		// Up next (Y on the player): the list that plays, in its own order (the Session's queue: Shuffle reorders
		// nothing shown), the one playing marked, scrolled to it; A plays a row as a pick from that list does (the
		// Session's Open: Shuffle and Loop stay as set)
		std::vector<std::string> upNextList(const Player& p, int& current) const;
		void openUpNext(const Player& p);
		void playUpNext(int i, Player& p);
		void drawUpNext(const Player& p);
		// the chapter line over the bar (the one playing), and the two track lines under the buttons: each opens its
		// page. Their ids are apart from buttonRect's grid and the photo's
		static constexpr int LINE1_BTN = 500, LINE2_BTN = 501, CHAPTER_BTN = 502;
		static constexpr int CHAPTER_Y = 126;
		bool chapterLineShown(const Player& p) const;
		void choose(int i, Player& p);
		// Settings opens on its groups (setGroup -1); A or a tap opens one, B goes back to them
		// (Web dashboard is a tile like the groups, but its own page: openGroup and stepGroup open Page::Web)
		enum SetGroup { GROUP_SUBS, GROUP_MUSIC, GROUP_PHOTOS, GROUP_REMEMBER, GROUP_FILES, GROUP_CONTROLS, GROUP_UPDATES, GROUP_WEB,
			GROUPS };
		// the page is an area (SettingsPage, ui_settings.h): the rows, changeSetting, openGroup, its drawing and keys.
		// Here stay: the groups' ids, the rows' ids and the list's measures (Manga's settings page and the tables use
		// them) and setGroup, the open group, which showPlayer's, the dashboard's and Browse's way-back records keep
		int setGroup = -1;
		struct SettingsPage;
		std::unique_ptr<SettingsPage> settingsPage;
		Page home(const Player& p) const { return playerShown(p) ? Page::Main : Page::Browse; }
		// the browser's four sections, opened from Home's tiles: Files (the cards), Playlists (the saved ones, then their
		// items), Network (the saved connections, then their folders) and Extensions (a placeholder). L/R step to the
		// one before or after (no wrap), at its top; the header's markers by touch. The section shown and where each
		// was left are the browser's (Browse::browseTab, tabDir)
		enum BrowseTab { TAB_LOCAL, TAB_PLAYLISTS, TAB_NETWORK, TAB_EXT, BROWSE_TABS };

		// the readers' Brightness sheet (ui_light.cpp): the screens' backlight, Extra dim, the reader's filter
		// (an overlay: LightSheet, pushed by openLight)
		struct LightSheet;
		void openLight();
		int& lightFilter(Settings& s) const;	// the reader's own filter: the text viewer's, the PDF/comic viewer's, Manga's
		// what the reader's Brightness button says while Extra dim or a filter is on ("Warm · 30%"); empty: neither
		std::string lightNote(const Player& p) const;
		bool showHidden = false;	// the setting, kept here for drawBrowseMenu (no Player there)
		bool mangaOn = false;		// the manga reader's switch, kept here for tileSub (no Player there)
		// a recent file played (as All recent's rows play one), from its start, shown in its folder (Files or Network)
		void openFile(const std::string& path, Player& p);
		void showInFolder(const std::string& path, Player& p);
		std::string stateDirPath;	// (setStateDir's) the manga store's, the readers' places and the playlists' folders are under it
		// ---- Files' own file operations (ui_fileops.cpp) ----
		// Copy / Move: the item is held (clip) while you go to the folder it goes in, and pasted there (SELECT, or the
		// bar's button); a copy, or a move to the other card, runs on a thread of its own with this panel showing
		// its progress until it ends (nothing else answers meanwhile but Cancel)
		enum { CLIP_NONE, CLIP_COPY, CLIP_MOVE };	// what the clip holds (Browse::clipOp), what a job does
		struct FileJob;
		std::shared_ptr<FileJob> fileJob;
		static void runFileJob(FileJob* j);	// on the job's thread
		std::string fileJobSignature() const;
		int fileJobPressed = -1;
		// Info (Files' Y menu, the manga reader's series menu): a sheet of what a file or folder is, any key closes it
		bool infoOpen = false;
		std::string infoTitle;
		std::vector<std::pair<std::string, std::string>> infoRows;
		void openFileInfo(const std::string& path, const std::string& title, std::vector<std::pair<std::string, std::string>> extra = {});
		void drawFileInfo();
	public:
		// a file or folder moved (Files' Rename or Move here, the dashboard's): everything kept of it follows it, it and
		// what's inside it — Recent and places to resume, playlists and Liked, a document's place and bookmarks, the
		// manga reader's series and their read marks (main thread: the dashboard's come through WebServer::call)
		void pathMoved(const std::string& from, const std::string& to, Player& p);
	private:
		void fileJobTick(Player& p);
		bool fileJobKey(Button b);
		bool fileJobPointer(const Pointer& pt, int x, int y);
		void drawFileJob();
		Rect fileJobCancelRect() const { return {200, 300, 240, 52}; }
		PlayRequest playRequest;
		// the select mode's uses (Playlists::selMode): Edit (a playlist's items), Add (files), Pick (a folder)
		enum SelMode { SEL_NONE, SEL_EDIT, SEL_ADD, SEL_PICK };
		std::string whereOf(const std::string& dir) const;	// "Videos / Shows", "MY-PC / Media / Movies"
		// the Main page's line under the title: what plays from (a folder, a network folder, a playlist)
		void drawSource(const Player& p);
		static constexpr int SOURCE_Y = 70;
		std::string ctxTitle, ctxNote;	// a playlist's menu: its title, and the line under it ("12 items", why it's missing)
		// a playlist's network items: their folders listed for their subtitles and covers (Player::netFolderWanted),
		// each once a play; the answers by serial
		std::set<std::string> sideAsked;
		std::map<int, std::string> sidePending;
		int sideSerial = 1 << 30;
		void dropTouch();
		// the browser's menu: X anywhere (or a finger held on a folder). On a card's folder Hide folder / Unhide
		// folder (a ".hidden" file in it); everywhere Refresh (the list read again: a file copied in meanwhile); Cancel
		enum CtxItem { CTX_HIDE, CTX_REFRESH, CTX_CANCEL, CTX_ADD, CTX_FIND, CTX_EDIT, CTX_TEST, CTX_DELETE,
			// a playlist's (Y on one, X in one) and its items'
			CTX_PL_NEW, CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_RENAME, CTX_PL_DELETE, CTX_PL_DEDUP, CTX_PL_MISSING, CTX_PL_HERE, CTX_PL_REMOVE,
			CTX_PL_SHOW, CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER, CTX_PL_SELECT, CTX_PL_MOVE,
			// Local and Network: X's Select files, a folder's Add to playlist...
			CTX_SELECT_FILES, CTX_ADD_TO,
			// a file's or folder's own on the cards (Files, ui_fileops.cpp): Copy..., Move..., Rename..., Delete
			CTX_F_COPY, CTX_F_MOVE, CTX_F_RENAME, CTX_F_DELETE, CTX_F_INFO,
			// the Network tab's X: the web dashboard's page
			CTX_WEB,
			// Liked's item's Add to playlist... (the one file)
			CTX_PL_ADD_ONE,
			// a recent file's (Home's Y): Resume, Play from the start, Show in folder, Like / Unlike, Add to playlist...,
			// Remove from Recent
			CTX_R_PLAY, CTX_R_START, CTX_R_SHOW, CTX_R_LIKE, CTX_R_ADD, CTX_R_REMOVE,
			// Home's X
			CTX_CLEAR_RECENT, CTX_HIDDEN, CTX_SETTINGS,
			// the mini bar's (a hold on it): Now playing, Play / Pause, Like, Add to playlist..., Show in folder, Stop
			CTX_NP_NOW, CTX_NP_PLAY, CTX_NP_LIKE, CTX_NP_ADD, CTX_NP_SHOW, CTX_NP_STOP };
		// how it was opened: X (the place's menu), Y (the row's), a finger held on a row (the row's, folders and
		// connections only)
		enum MenuHow { MENU_X, MENU_Y, MENU_HOLD };
		std::string ctxServer;		// a connection's menu: its id
		std::string ctxStatus;		// a connection's menu: Test's answer
		bool ctxArmed = false;		// Delete pressed once
		int ctxMissing = 0;		// a playlist's Remove missing, pressed once: how many it would take out
		bool ctxOpen = false;
		int ctxCursor = 0;
		std::vector<int> ctxItems;	// CtxItem
		std::string ctxPath;		// the folder Hide is for ("" none)
		std::string ctxFolder;		// the folder "Add to playlist..." is for (a card's or a server's)
		Uint32 holdAt = 0;	// a finger down on a row, for the hold that opens it
		void openBrowseMenu(int row, MenuHow how);
		void chooseBrowseMenu(int item, Player& p);
		void drawBrowseMenu(const Player& p);
		// the updater's pop-up over the file list: a release found (Update now, Later, Not until tomorrow), then the
		// download's progress (Hide), then Restart now / Later, or what went wrong (OK)
		bool popupClosed = false, popupFollow = false;
		int popupCursor = 0;
		bool popupShown(const Player& p) const;
		bool popupKey(Button b, Player& p);	// its keys (Browse's: the pop-up is over the file list)
		std::vector<const char*> popupItems() const;
		void choosePopup(int item, Player& p);
		void drawUpdatePopup();
		Rect popupRow(int item) const { return {136, 252 + item * 50, 368, 44}; }
		std::string updateStatus() const;	// Check now's value
		// the battery's icon (and percent, a bolt while charging), its right edge at right; read every 10 s
		BatteryState battery;
		Uint32 batteryAt = 0;
		void readPower();
		// minX: the clock only if it fits right of it (START's tabs)
		void drawBattery(int right, int y, const Player& p, int minX = 0);
		// the clock, left of the battery and the web's globe ("14:32", "2:32 PM"; "" off, or over a photo); where the
		// header's marks start (the battery's right edge at 616): a title or the section markers end before it
		std::string clockText(const Player& p) const;
		int statusLeft(const Player& p) const;
		static constexpr int BATTERY_W = 110;	// what a title leaves for it
		// the menu's box, in the middle of the panel, as tall as its items
		// (a connection's has a line under its title for Test's answer)
		// A long menu (a playlist's eight) steps its rows closer, 52, 48 or 44 apart, to fit above the footer; one that
		// still doesn't leaves its line out (a playlist's count: the page shows it)
		int ctxStep() const
		{
			int n = (int)ctxItems.size(), extra = ctxServer.empty() && ctxNote.empty() ? 0 : 34;
			for (int step : {52, 48})
				if (80 + extra + n * step <= 436) return step;
			return 44;
		}
		int ctxExtra() const
		{
			if (ctxServer.empty() && ctxNote.empty()) return 0;
			return 80 + 34 + (int)ctxItems.size() * ctxStep() <= 436 ? 34 : 0;
		}
		// Home's (a recent file's, Options) wider: their rows have a value on the right
		bool ctxWide() const { return !ctxItems.empty() && ctxItems[0] >= CTX_R_PLAY; }
		Rect ctxBox() const
		{
			int h = 80 + ctxExtra() + (int)ctxItems.size() * ctxStep();
			return ctxWide() ? Rect{90, (436 - h) / 2, 460, h} : Rect{112, (436 - h) / 2, 416, h};
		}
		Rect ctxRow(int item) const
		{
			Rect b = ctxBox();
			int inset = ctxWide() ? 8 : 16;
			return {b.x + inset, b.y + 62 + ctxExtra() + item * ctxStep(), b.w - 2 * inset, ctxStep() - 6};
		}


		// the on-screen keyboard, over the bottom panel: a form's field typed in
		bool kbOpen = false, kbSymbols = false;
		ShiftState kbShift = ShiftState::Off;
		int kbCursor = 0, kbField = 0, kbPressed = -1;
		Uint32 kbActiveAt = 0;	// the last key: the caret shows steadily for a moment after it, then blinks
		bool kbCaretShown() const;
		TextEdit kbEdit;
		std::vector<KeyDef> kbKeys() const;
		void openKeyboard(int field, const std::string& text);
		void kbPress(Button b);
		void kbPointer(const Pointer& pt, int x, int y);
		void kbKey(int i);
		void kbClose(bool keep);
		void drawKeyboard();
		// START's menu, over a video or the browser: tabs Settings, About, Exit (Recent is Home's); L/R or a tap
		// change tab, B or START close it
		// Playing (Like, Add to playlist..., Show in folder, Info) is first, while a song or video plays (menuTabs)
		enum Tab { TAB_SETTINGS, TAB_ABOUT, TAB_EXIT, TAB_PLAYING, TABS };
		std::vector<int> menuTabs(const Player& p) const;	// the tabs shown, in their order
		bool playingTabShown(const Player& p) const;
		Rect menuTabRect(int i, int n) const;	// the i-th of n tabs
		bool inMenu = false;
		int menuTab = TAB_SETTINGS;
		// the browser's folder before the menu, and its cursor: back to them after
		Page menuReturnPage = Page::Browse;	// the menu opened over Manage extensions: back to it
		std::string menuReturnDir;
		int menuReturnCursor = 0, menuReturnScroll = 0;
		void openMainMenu(Player& p, int tab = TAB_SETTINGS);
		void showTab(int tab, Player& p);
		void closeMenu(Player& p);
		Rect tabRect(int tab) const { return {72 + tab * 136, 10, 128, 44}; }
		void drawTabs(const Player& p);
		// the Playing tab: the file's card, then its rows (PLAY_ROW + i)
		// Now playing (while it plays behind the browser) first, Stop last
		enum PlayingRow { PR_LIKE, PR_ADD, PR_SHOW, PR_INFO, PR_NOW, PR_STOP, PR_PLAYON };
		std::vector<int> playingRows() const;
		void drawPlaying(const Player& p);
		bool playingKey(Button b, Player& p);
		bool playingChoose(int row, Player& p);	// true: leave (Show in folder closes what plays)
		Rect playingRowRect(int i) const
		{
			int n = (int)playingRows().size(), step = n > 6 ? 42 : n > 5 ? 46 : 54;	// (six or seven: closer, above the footer)
			return {16, 142 + i * step, 608, step - 4};
		}
		// Play on (the Playing tab's row, while a dashboard page that can play is open): where it plays, the handheld or
		// one of those pages, as a dialog over the tab. Picking one is the Session's SetOutput (SessionHost::sendTo)
		struct OutChoice { std::string tab, name; };	// tab "": the handheld
		std::vector<OutChoice> outChoices() const;	// the handheld first, then each page that can play (WebServer::playTabs)
		bool playOnShown() const;	// the row: a page that can play is open, or one is the output
		bool playOnOpen = false;
		int playOnCursor = 0;
		void openPlayOn();
		bool playOnKey(Button b, Player& p);
		bool playOnPointer(const Pointer& pt, int x, int y, Player& p);
		void playOnChoose(int i, Player& p);
		void drawPlayOn(const Player& p);
		Rect playOnBox() const { int n = (int)outChoices().size(); int h = 122 + std::min(n, PLAYON_SHOWN) * 60; return {70, std::max(16, (436 - h) / 2), 500, h}; }
		Rect playOnRow(int i) const { Rect b = playOnBox(); return {b.x + 20, b.y + 106 + (i - playOnScroll) * 60, b.w - 40, 54}; }
		static constexpr int PLAYON_SHOWN = 4, PLAYON_ROW = 2000;	// (above the picker's PICK_ROW + its rows)
		int playOnScroll = 0;
		// Show in folder from the Playing tab: what plays closed, then its folder shown (showBrowser)
		std::string showAfterLeave;
		bool showSourceAfterLeave = false;	// List (Y) on a playlist's song: the player left for that playlist's page
		// Like: toggled (the heart, START's Playing tab, L3), the heart's pop and the top panel's card
		void likeNow(Player& p);
		void likeCard(Player& p, bool liked, const std::string& title);
		Uint32 likePopAt = 0, addFlashUntil = 0;
		// the Playing tab's "In Road trip": the playlists the file is in, read again when they or the file change
		mutable std::string playingIn, playingInPath;
		mutable int playingInSerial = -1;
		// Main's header squares (a song or a video): Add to playlist, Like
		bool headerActions(const Player& p) const;
		void drawExit(const Player& p);
		bool clearHistory(Player& p);	// true when it cleared (the second time)
		bool act(int id, Player& p);	// a touch button let go of on it
		bool pressAnyway(Button b, Player& p);
		// music playing: X loop, Y chapters (none: nothing), SELECT the Display popup (a video: audio, subtitles,
		// picture size)
		bool musicKey(Button b, Player& p);
		// the music key's mode: MODE_SHUFFLE when Shuffle is on (with whichever Loop), else the Loop (LOOP_*)
		int playMode(const Player& p) const;
		std::string modeLabel(const Player& p) const;	// what the screen says as X changes it
		// Display, over a song's page (SELECT): what the top panel shows, and the visualizer's style; each change
		// applied at once (the player rebuilds the graph once the changes stop)
		bool displayOpen = false;
		int displayRow = 0;	// 0 Show, 1 Style
		// the style the arrows have moved to, applied once they rest (-1 none): see displayKey
		int displayPending = -1;
		Uint32 displayPendingAt = 0;
		static constexpr Uint32 DISPLAY_SETTLE_MS = 450;
		static constexpr int CHIP_STRIDE = 32;	// a chip's touch id: CHIP_BTN + row * CHIP_STRIDE + chip
		void displayApplyPending(Player& p);
		bool displayShown(const Player& p) const { return displayOpen && page == Page::Main && !inMenu && p.playingAudio(); }
		bool displayKey(Button b, Player& p);
		bool displayPointer(const Pointer& pt, int x, int y, Player& p);
		void drawDisplay(const Player& p);
		// Show's 4 chips share a row; Style's (one an offered visualizer, VIS_SHOWN) go 5 to a row, as many rows as
		// it takes (three for fifteen), the chips a little lower than Show's
		static int displayChips(int row) { return row ? VIS_SHOWN_COUNT : SHOWS; }
		static constexpr int DISPLAY_BOX_Y = 24, DISPLAY_BOX_H = 412, STYLE_COLS = 5, STYLE_ROWS = (VIS_SHOWN_COUNT + 4) / 5,
			STYLE_Y = 166, STYLE_STEP = 56, CHIP_H = 50;
		Rect displayChip(int row, int i) const
		{
			if (!row) return {48 + i * (130 + 8), 84, 130, CHIP_H};
			int w = (544 - (STYLE_COLS - 1) * 8) / STYLE_COLS;
			return {48 + (i % STYLE_COLS) * (w + 8), STYLE_Y + (i / STYLE_COLS) * STYLE_STEP, w, CHIP_H};
		}
		void displaySet(int row, int value, Player& p);
		// Sound (music's): two tabs, Equalizer and Effects: an area (Area), ui_sound.h / ui_sound.cpp
		struct Sound;
		std::unique_ptr<Sound> sound;
		// a photo: the pan area (its thumbnail, the part on the top panel boxed: a finger drags the photo as on a
		// phone, a flick glides, a double-tap zooms; no swipe to the next: Prev/Next do that) beside its buttons
		bool photoKey(Button b, Player& p);	// true: it was a photo's
		bool photoPointer(const Pointer& pt, int x, int y, Player& p);
		void photoZoomTo(double zoom, double panX, double panY, Player& p);
		void drawPhoto(const Player& p);
		Rect panRect(const Player& p) const { return {p.settings.panRight ? 288 : 16, 80, PAN_W, PAN_H}; }
		Rect photoBtn(int i, const Player& p) const
		{
			return gridCell({p.settings.panRight ? 16 : 368, 80, 120, 56}, 2, 8, 8, i);
		}
		Rect thumbRect(const Player& p) const;	// the photo, as shown, fitted in the pan area
		bool panTouch = false, panMoved = false;
		int panDownX = 0, panDownY = 0, lastX = 0, lastY = 0, tapX = 0, tapY = 0;
		double panStartX = 0, panStartY = 0, flickVX = 0, flickVY = 0;	// the flick's speed: units a millisecond
		Uint32 lastAt = 0, flickAt = 0, tapAt = 0;
		SDL_Texture* photoTex = nullptr;
		int photoTexId = -1;
		static constexpr int PAN_W = 336, PAN_H = 252, PHOTO_BTN = 20, PHOTO_BTNS = 8;
		Rect buttonRect(int id) const;
		std::string signature(const Player& p) const;

		void renderPage(const Player& p);	// the page into the renderer, not yet shown
		void drawMain(const Player& p);
		void drawLeave(const Player& p);	// Close (Quit): B twice, or a tap twice
		void drawTracks(const Player& p);
		// the pieces of a page: the header ("<", the title, the battery), a tab, the footer's hints (a button's name in
		// yellow, what it does), the cursor's row, a list's scroll bar, a page with nothing to list
		struct Hint { std::string key, verb; };
		void drawHeader(const std::string& title, const Player& p, int titleRight = 0);	// titleRight: the title ends before it
		void drawTab(Rect r, const char* name, bool active, int size = 1);
		void drawHints(const std::vector<Hint>& hints);
		void drawFooterText(const std::string& text, SDL_Color c);
		void drawFocus(Rect r);
		void drawScrollBar(int top, int height, int first, int shown, int total);
		// btn: the button's glyph (G_PLUS when none); sub: a line, or two apart by '\n'
		void drawEmpty(const Glyph& g, const std::string& title, const std::string& sub, const char* action, const Glyph* btn = nullptr);
		std::string elideLeft(const std::string& s, int size, int maxW) const;
		// a dialog over the page (the Options menu, the update, quick settings), and one of its rows
		void drawDialog(Rect box, const std::string& title);
		// note: a value on the row's right ("On", "Press twice")
		void drawDialogRow(Rect r, const Glyph* g, const std::string& label, bool at, bool pressed, SDL_Color ink,
			const std::string& note = "", SDL_Color noteInk = {0, 0, 0, 0});
		std::vector<std::string> wrapText(const std::string& text, int size, int maxW, int maxLines) const;
		// a button's face: a glyph and a label side by side, the hardware button's name under them (key: none)
		// Rows and grids of buttons: one function per row gives the rect, and drawing and touch both ask it.
		// rowCell: the i-th of n equal cells across `area`, `gap` apart (the sizes divide exactly in every row that
		// uses it: a remainder would be dropped at the right).
		static Rect rowCell(Rect area, int n, int gap, int i)
		{
			int w = (area.w - (n - 1) * gap) / n;
			return {area.x + i * (w + gap), area.y, w, area.h};
		}
		// gridCell: the i-th of a grid of `cols` columns, row by row, every cell the size of `first`
		static Rect gridCell(Rect first, int cols, int gapX, int gapY, int i)
		{
			return {first.x + i % cols * (first.w + gapX), first.y + i / cols * (first.h + gapY), first.w, first.h};
		}
		// a button's label, with its key under it when there is one (the label moves up to make room); the label
		// and key fit labelW/keyW units wide (0: the button less 6)
		void drawKeyedLabel(Rect b, const std::string& label, const std::string& key, bool off = false, int labelW = 0, int keyW = 0);
		void drawButton(Rect b, const Glyph* g, const std::string& label, const char* key, bool off, int size, bool lit = false);	// lit: the glyph and label in yellow (on, as the heart once liked)
		static constexpr int STATUS_Y = 100, LINE1_Y = 378, LINE2_Y = 403;	// the player's status line, its two track lines
		static constexpr int FOOT_Y = 458, SUB_Y = 70;	// the footer's line, the browser's "where" line (their middles)
		void drawInfo(const Player& p);
		// the Media info page's lines: a row's value wrapped to the width, read again each second while it's open
		struct InfoLine { std::string label, text; bool header; };
		std::vector<InfoLine> infoLines;
		Uint32 infoAt = 0;
		void readInfo(const Player& p);
		void setInfoLines(const std::vector<Player::InfoRow>& rows);	// wrapped to the page
		void openAbout(const Player& p);
		static constexpr int INFO_Y = 70, INFO_H = 26, INFO_SHOWN = 14, INFO_VALUE_X = 190;

		SDL_Texture* text(const std::string& s, int size, SDL_Color c);
		void drawText(const std::string& s, int size, SDL_Color c, int x, int y, int maxW = 0, int align = 0);
		// the same, the line's middle (its capitals') on cy: labels centred in their boxes at any panel size
		void drawTextMid(const std::string& s, int size, SDL_Color c, int x, int cy, int maxW = 0, int align = 0);
		void drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle);
		int capHeight[3] = {};	// in pixels
		void fill(Rect r, SDL_Color c);
		// rounded corners (theme.h's radii, in 640x480 units): corners a mask of ROUND_TL, _TR, _BL, _BR. SDL_Renderer
		// has none: a box is its middle and sides as rects, its corners copies of one anti-aliased quarter circle
		// (white, made once per radius in the panel's pixels, tinted and flipped)
		enum { ROUND_TL = 1, ROUND_TR = 2, ROUND_BL = 4, ROUND_BR = 8, ROUND_ALL = 15 };
		void fillRound(Rect r, SDL_Color c, int rad, int corners = ROUND_ALL);
		// the part of a rounded box between y0 and y1 (left of x1, when given): a line along its edge, its corners kept
		void fillRoundBand(Rect box, int rad, int y0, int y1, SDL_Color c, int x1 = -1);
		// a level bar: the track, the first w units filled (the accent when none given)
		void drawLevel(Rect track, int w, SDL_Color trackInk, SDL_Color fillInk = {0, 0, 0, 0});
		// a button, tab, tile, card or dialog: rounded, an edge inside its outline (the fill 30% towards the text, the
		// accent when focused) and a darker lip along its bottom
		void drawBox(Rect r, SDL_Color c, int rad, bool focus = false);
		void drawBox(Rect r, SDL_Color c, int rad, SDL_Color edge);
		void roundDev(SDL_Rect d, SDL_Color c, int R, int corners);	// in the panel's pixels
		SDL_Rect devRect(Rect r) const;
		int devRadius(int rad) const;
		SDL_Texture* corner(int R);
		std::vector<SDL_Texture*> corners;	// by radius in pixels
		// a glyph at (x, y), its cells cell units square, snapped to the panel's pixels; Mid: centred on (cx, cy)
		void drawGlyph(const Glyph& g, int x, int y, SDL_Color c, int cell = 2);
		void drawGlyphMid(const Glyph& g, int cx, int cy, SDL_Color c, int cell = 2);
		int glyphW(const Glyph& g, int cell = 2) const;

		SDL_Renderer* ren = nullptr;
		TTF_Font* fonts[3] = {};	// small, normal, large
		// for text Pixelify Sans has no glyphs for, at the same sizes: Fusion Pixel (Chinese, Japanese, Korean,
		// Vietnamese, Greek), then Noto Sans Thai, Arabic, Hebrew, Devanagari
		std::vector<std::array<TTF_Font*, 3>> fallbacks;
		// the text in pieces, each with a font that has its characters: one piece in one font when one font has
		// them all (no mix of looks in a name), else a font a run of characters (an Arabic name's ".mkv")
		std::vector<std::pair<TTF_Font*, std::string>> runs(const std::string& s, int size) const;
		std::vector<std::pair<TTF_Font*, std::string>> shapeRuns(const std::string& s, int size) const;
		int textWidth(const std::string& s, int size) const;
		std::unordered_map<std::string, SDL_Texture*> cache;
		// measured widths and shortened ("...") strings, by size and text: a long line is shortened once, not
		// re-measured a character at a time on every redraw (the handheld's cores made lists lag)
		mutable std::unordered_map<std::string, int> widthCache;
		std::unordered_map<std::string, std::string> elideCache;
		std::string drawn, fontsDir;

		Page page = Page::Main;
		int cursor = 0, scroll = 0;
		// ---- sliders (ui_slider.cpp, slider.h; docs/notes/sliders.md) ----
		// Every bar a finger moves is one of these: a touch in its hit area grabs it (slideGrab) and the value jumps
		// to the finger; pointer() then gives every move and the lift to it, wherever the finger goes, until it
		// lifts. `set` hears the value while held (each change; one per everyMs at most when that's given: a seek, a
		// page) and once more with last = true as the finger lifts. A key pressed meanwhile ends it as a lift does.
		enum { SL_SEEK = 1, SL_QUICK, SL_DOC, SL_MANGA, SL_EQ, SL_LIGHT };
		struct SlideSpec
		{
			Rect track;	// the finger's place is read along this (its width; its height when upright)
			SliderRange range;
			bool upright = false;
			Uint32 everyMs = 0;	// a costly target: told this often at most while held (0: every change)
			double now = 0;	// its value as the finger lands: a slide moves it on from here, by the finger's travel
		};
		using SlideSet = std::function<void(double v, bool last, Player& p)>;
		struct Slide
		{
			bool on = false, moved = false;	// held; the finger has travelled along the track (not a tap)
			int id = 0, sub = 0;	// which slider (SL_*), and which of its kind (a row, a band)
			SlideSpec spec;
			SlideSet set;
			double value = 0;	// where the finger has it
			int at0 = 0, at = 0;	// along the track: where the finger landed; where the slider is (sliderFollow)
			Page page = Page::Main;	// the page and the overlay it was grabbed on: gone, the slider is let go (slideStale)
			const Overlay* owner = nullptr;
			SliderPace pace;
		} slide;
		void slideGrab(int id, int sub, const SlideSpec& spec, int x, int y, Player& p, SlideSet set, const Overlay* owner = nullptr);
		// held: a finger on it that has slid along it (a touch alone changes nothing and shows nothing yet)
		bool slideHeld(int id) const { return slide.on && slide.moved && slide.id == id; }
		bool slideHeld(int id, int sub) const { return slide.on && slide.moved && slide.id == id && slide.sub == sub; }
		bool slideFollow(const Pointer& pt, int x, int y, Player& p);	// pointer(): a move or the lift while one is held
		void slideLive(Player& p);	// the value to the target, if it's due (tick() too: a finger at rest)
		bool slideWaiting() const;
		bool slideStale() const;
		void slideEnd(Player& p);	// let go, the last value given
		void slideDrop();	// let go, nothing given
		// How a slider looks, the same for all (docs/notes/sliders.md): at rest the track, the accent to `share`
		// (0..1) of it and the knob there; held, the knob grown (drawSlideKnob) so the finger knows it has it, and the
		// value said where the finger doesn't hide it: a tag over the knob (drawSlideTag), unless the row says the
		// value itself above or beside the bar (then that is lit instead). Lifted, the resting look again.
		void drawSlider(Rect track, double share, bool held = false, const std::string& tag = "");
		void drawSlideKnob(Rect knob);	// a resting knob's box, drawn as held: 3 bigger all round, a dark rim
		void drawSlideTag(Rect track, double share, const std::string& text);
		// the share a slider is drawn at: where the finger has it while held, else `rest` (what its target says)
		double slideShare(int id, int sub, double rest) const
		{
			return slideHeld(id, sub) ? sliderShare(slide.value, slide.spec.range) : rest;
		}
		// the finger may shift this far along the track without moving the slider (landing, lifting)
		static constexpr int SLIDE_SLACK = 6;
		// let go, the bar stays where the finger left it until playback gets there (the exact seek takes a moment, a buffering one longer;
		// and until then the position is still the old one: the bar flicked back and forth); -1 none
		double seekHeld = -1;
		Uint32 seekHeldAt = 0;
		BottomPanel panel;
		// Lock: over a video, touch does nothing but Screen off and a 1 s hold on the lock (its release); the
		// buttons go on as Button hotkeys says
		bool touchLocked = false;
		Uint32 lockHoldAt = 0, lockNoticeAt = 0;
		bool swallowTouch = false;	// the touch that woke the panel: it presses nothing
		int pressedBtn = -1;		// a touch button under the finger: it acts when the finger lifts on it
		bool listTouch = false, listScrolled = false;	// a finger on a track list: a tap chooses, a drag scrolls
		int touchStartY = 0, touchX = 0, scrollStart = 0;
		Uint32 clearArmed = 0;		// Clear play history chosen once: again within 2 s clears it

		std::string openRequest;
		std::vector<BrowseRoot> roots;
		// the cards looked at again every few seconds: one put in or taken out shows at once (rootsSerial: main hands
		// the dashboard its places again)
		Uint32 rootsCheckedAt = 0;
		int rootsChanged = 0;
		void rootsCheck(Player& p);
	public:
		int rootsSerial() const { return rootsChanged; }
		const std::vector<BrowseRoot>& places() const { return roots; }
	private:

		static constexpr Rect BAR{24, 150, 592, 24};
		static constexpr int BTN_W = 108, BTN_H = 60, BTN_STEP = 121, ROW1_Y = 228, ROW2_Y = 300;
		static constexpr int ROW_Y = 84, ROW_H = 48, ROWS_SHOWN = 7;	// 7 rows end at 420, above the hints (and the mini bar)
		// the settings' rows by id: the subtitle style's, and the actions and pages that have their own code (the rest are
		// the settings table's: SET_ROW0 + the index in settingDefs(), ui_internal.h). Shown a group at a time
		// (settingRows), after a list of the groups. The manga reader's rows are its own page's (Page::MangaSettings):
		// EXT_NOTE is its note, no value
		static constexpr int STYLE_ROW0 = 0, CLEAR_ROW = STYLE_KEYS, CHECK_ROW = CLEAR_ROW + 1, INSTALL_ROW = CHECK_ROW + 1,
			TOP_LIGHT_ROW = INSTALL_ROW + 1, BOTTOM_LIGHT_ROW = TOP_LIGHT_ROW + 1, SOUND_ROW = BOTTOM_LIGHT_ROW + 1,
			EXT_NOTE = SOUND_ROW + 1, MANGA_CLEAR_ROW = EXT_NOTE + 1, MANGA_JAVA_ROW = MANGA_CLEAR_ROW + 1;
		// the panels' backlights (BananaOS's own levels: changed here as its menu changes them)
		Backlight backlight;
		bool sameBrightness = true;	// the setting's, kept here for settingRows (no Player there)
		Volume volume;
		int volumePct = -1;	// as read when the quick settings opened, then as set there
		bool quickOpen = false;
		int quickCursor = 0;
		enum QuickRow { Q_VOLUME, Q_BRIGHT, Q_BOTTOM, Q_SAME, Q_WEB, Q_INCOGNITO, Q_QUIT };
		int quickSub = 0;	// Web dashboard's row: 0 its switch, 1 New PIN (LEFT/RIGHT)
		std::vector<int> quickRows() const;
		bool quickKey(Button b, Player& p);
		bool quickPointer(const Pointer& pt, int x, int y, Player& p);
		void quickAdjust(int row, int dir, Player& p);
		void quickSlide(int row, int x, int y, Player& p);	// a finger on the row's bar: grabbed
		void quickChoose(int row, Player& p);
		void drawQuick(const Player& p);
		// rows 62 apart, closer (56) when that many don't fit above the footer
		// the rows' spacing: roomy while they fit above the hints, closer as rows come (seven at most: both panels'
		// brightness apart, the dashboard, incognito mode and quit)
		int quickStep() const
		{
			int n = (int)quickRows().size();
			return 74 + n * 62 <= 436 ? 62 : 74 + n * 56 <= 436 ? 56 : (436 - 74) / n;
		}
		Rect quickPanel() const { int h = 74 + (int)quickRows().size() * quickStep(); return {40, (436 - h) / 2, 560, h}; }
		Rect quickRowRect(int i) const { return {56, quickPanel().y + 56 + i * quickStep(), 528, std::min(54, quickStep() - 2)}; }
		Rect quickPinBtn(int i) const { Rect r = quickRowRect(i); return {r.x + r.w - 164, r.y + (r.h - 48) / 2, 160, 48}; }
		Rect quickMinus(int i) const { Rect r = quickRowRect(i); return {r.x + 236, r.y + (r.h - 48) / 2, 48, 48}; }
		Rect quickBar(int i) const { Rect r = quickRowRect(i); return {r.x + 296, r.y + r.h / 2 - 10, 164, 20}; }
		Rect quickPlus(int i) const { Rect r = quickRowRect(i); return {r.x + 472, r.y + (r.h - 48) / 2, 48, 48}; }
		void setBrightness(Backlight::Panel panel, int permille, Player& p);
		// (6 rows end at 368: the help's two lines at 400 stay clear of them; a longer group scrolls)
		static constexpr int SET_Y = 92, SET_H = 46, SET_SHOWN = 6, HELP_Y = 400;
		static constexpr int64_t STYLE_ROW = -2;	// the subtitle menu's last row: the settings page
		static constexpr int QUIT_BTN = 10, SCREEN_BTN = 12, INFO_BTN = 13, LOCK_BTN = 14, EXIT_BTN = 15,
			BACK_ICON = 16,	// "<" at the top left of every page: B
			EMPTY_BTN = 17,	// an empty page's button (no connections: Find on the network)
			GROUP_PREV = 18, GROUP_NEXT = 19;	// a settings group's neighbours, either side of its title (Y, X)
		// the web dashboard's page: New PIN and Turn off (its Turn on / Try again is EMPTY_BTN)
		static constexpr int WEB_NEWPIN = 540, WEB_OFF = 541;
		// the select mode's bar (and Move's), the busy dialog's Cancel, the Add to... picker's rows (PICK_ROW + i)
		static constexpr int SEL_ALL = 530, SEL_MOVE = 531, SEL_ACT = 532, SEL_CANCEL = 533, MOVE_CANCEL = 534, MOVE_DONE = 535,
			BUSY_CANCEL = 536, PICK_ROW = 900;
		// Main's header squares, the Playing tab's rows (PLAY_ROW + i)
		static constexpr int ADD_BTN = 503, LIKE_BTN = 504, PLAY_ROW = 550;
		static constexpr int PLAY_GRID = 2;	// the grid's Play/Pause (A's place)
		// Sound's touch buttons, apart from the grid's and the photo's; its dialogs' rows from DLG_ROW, the Display
		// popup's chips from CHIP_BTN (row * CHIP_STRIDE + chip, up to 664)
		static constexpr int SOUND_TAB0 = 510, SOUND_TAB1 = 511, EQ_SWITCH = 520, EQ_PREV = 521, EQ_NEXT = 522, EQ_NAME_ZONE = 523,
			EQ_RESET = 524, EQ_SAVE = 525, EQ_OPTIONS = 526, DLG_ROW = 700, CHIP_BTN = 600, FX_ROW = 800;
		void drawBackIcon();
		// the browser's rows' marks: what each row is
		enum RowIcon { ICON_NONE, ICON_RECENT, ICON_PLACE, ICON_FOLDER, ICON_VIDEO, ICON_SONG, ICON_PHOTO, ICON_DOC, ICON_BOOK, ICON_FILE, ICON_PDF, ICON_EPUB, ICON_COMIC, ICON_NETWORK, ICON_REFRESH,
			ICON_TYPE, ICON_TEST, ICON_SAVE, ICON_PLAYLIST, ICON_LIKED, ICON_NEW };
		RowIcon rowIcon(int row) const;
		void drawRowIcon(RowIcon icon, int x, int y, SDL_Color c);
		// every page has it but Home (nowhere to go back to; B there asks to quit, START has Quit)
		bool backIconShown(const Player&) const;
		static constexpr Uint32 UNLOCK_HOLD_MS = 1000;	// buttonRect's ids beside the grid's 0..9
		static constexpr int SET_ARROW_X = 372;	// a settings row's left arrow: a tap left of the value's middle steps back

		// ---- the web dashboard ----
		// Page::Web is an area (Area), ui_web.h / ui_web.cpp; what every page shows of it (the header's globe, the quick
		// settings' row, the switch, the address and the PIN) stays Ui's, below
		struct Web;
		std::unique_ptr<Web> webPage;
		void openWeb();	// the page, from Home's menu, a settings group, the globe, the quick settings
		// its states as the page shows them
		enum WebView { WEB_OFF_VIEW, WEB_ON_VIEW, WEB_NOWIFI_VIEW, WEB_FAILED_VIEW };
		int webView(const Player& p) const;
		void webTurn(bool on, Player& p);	// the switch: kept in the settings, the server started or stopped
		int webClients() const;
		std::string webPin() const;
		std::string webAddress() const;	// "192.168.1.23:8080" ("" no Wi-Fi, or not running)
		std::string webIp;				// the device's address, read when the page opens and every 10 s while it shows
		Uint32 webIpAt = 0, webInputAt = 0, pinArmed = 0;
		void readWebIp();
		TTF_Font* pinFont = nullptr;	// Pixelify Sans at 56: the PIN's characters
		// the header's indicator (left of the battery), as last drawn: a tap opens the page
		Rect webInd{0, 0, 0, 0};
		bool webIndDown = false;
		void drawWebIndicator(int right, int cy, const Player& p);
		bool webIndicatorShown(const Player& p) const;
		std::string webSig(const Player& p) const;	// for signature()
		int levelMs = 0;
		bool stopRequest = false;

		// ---- every page's loading mark and message dialog (the Manga pages use them most; the area is ui_manga.h) ----
		// the one loading mark: a ring of dots turning, a line under it, in the middle of what's loading (a page's
		// list, a chapter). Drawn this frame: the loop comes round for its next step (animating, signature)
		mutable bool loaderShown = false;
		void drawLoader(int cx, int cy, const std::string& text);
		// A message that needs reading (what can't be done, what went wrong): a dialog on this panel with OK, A or B
		// or a tap closes it. Short confirmations ("Copied", "Speed 1.5x") stay the top screen's notice (Player::osd)
		// (an overlay: AlertBox in ui_overlay.cpp; a second message takes the first one's place)
		struct AlertBox;
	public:
		void alert(const std::string& title, const std::string& text = "");
	private:

		// ---- playing behind the browser ----
		// the player left (B, "<", Back) while a song or video plays on: the browser is the base page; showPlayer ends it
		bool bgPlay = false;
		bool stopPressed = false;	// the player's own Stop (takeStopPressed)
		bool playerOverPhoto = false;	// the mini bar's tap over a photo (takePlayerOverPhoto)
		// the player shown (showPlayer) and not left since; back: where it was opened from (backFromPlayer)
		bool onPlayer = true;
		struct PlayerBack
		{
			bool set = false;
			Page page = Page::Browse;
			bool inMenu = false;
			int menuTab = 0, setGroup = -1, cursor = 0, scroll = 0;
			std::string menuReturnDir;
			int menuReturnCursor = 0, menuReturnScroll = 0;
			std::string browseDir;	// the folder or playlist page it was opened from (a playlist's play moves the list's)
			int browseTab = 0;
		} playerBack;
		// back to what plays (SELECT on a browse page, the bar, Now playing, B at Home): the player. False: nothing plays
		bool goNowPlaying(Player& p);
		bool nowPlayable(const Player& p) const { return p.playing() && !p.playingPhoto(); }
		// the player the mini bar works on: main's (see mainPlayer above); without it, p
		Player& barPlayer(Player& p) const { return mainPlayer ? *mainPlayer : p; }
		const Player& barPlayer(const Player& p) const { return mainPlayer ? *mainPlayer : p; }
		// the mini bar (Now playing) along the bottom of every page but the player's own (the song or video player
		// page, its tracks, its queue, its Info): a tap the player, its play mark play / pause (a page playing the
		// file: the page's, held: here), a hold its menu. The comic reader and the photo viewer are pages of their
		// own, not the player: it shows there too
		// SELECT here goes back to what plays (the bar's right end names it), but where SELECT is the page's own key
		// (a photo's Fit/Fill, Sound's On/Off, the reader's fullscreen) the bar doesn't name it
		bool selectToPlayer() const { return page != Page::Main && page != Page::Sound && page != Page::MangaRead; }
		static constexpr Rect MINI_BOX{16, 422, 608, 34};
		static constexpr int MINI_PP = 560, MINI_BAR = 561;
		bool miniBarShown(const Player& p) const;
		bool miniUp = false;	// shown in the last frame drawn (tileRect, the footer's line)
		void drawMiniBar(const Player& p);
		void miniToggle(Player& p);	// its play mark: play / pause what plays (a page's too)
		void openNowMenu(const Player& p);
		Uint32 miniHoldAt = 0;
		bool playHoldMini = false;	// the Play hold (a page plays the file) is on the mini bar's play mark
		int footY() const { return miniUp ? 468 : FOOT_Y; }
		// the playing file's row (and Recent card): a chip ("Playing", "Paused", "In browser", "Stopped"), set each frame
		std::string chipPath, chipPlDir;	// chipPlDir: a playlist's page (only there, of the playlists: the one playing)
		int chipKind = 0;	// 0 none, 1 playing, 2 paused, 3 in a browser
		void setChip(const Player& p);
		int drawChip(int right, int mid, int kind, bool card = false);	// its width
		int rowChip(int i) const;	// the chip of the browser's row i (0 none)
		// back from the player: the playing file's row (and the bar's edge) lit a moment
		Uint32 landFlashUntil = 0;
		// the player opened on a playlist's first song: its List key (the way to the playlist) glows a subtle yellow
		// twice
		bool listPulseArm = false;
		std::string listPulsedFor;	// the playlist it glowed for (cleared when something else plays)
		Uint32 listPulseAt = 0;
		static constexpr Uint32 LIST_PULSE_MS = 1400;
		bool markAnim = false;	// drawBattery drew the playing mark moving
		int markW(const Player& p) const;	// its room in the header (0 none)
		// Incognito mode (Quick settings' Incognito mode): nothing played goes in Recent (Player::incognito) and
		// reading adds nothing to the manga History (places, read marks, resume points, the library and downloads go
		// on as ever). Never saved: off at every start. Its mark sits left of the clock while it's on
		bool incognito = false;
		int incognitoW() const { return incognito ? 18 + 10 : 0; }
		bool markLive(const Player& p) const;	// it moves: what plays plays (a page's: as the page does)
		void drawMark(int right, int cy, const Player& p);
		// the equalizer changed from the web (a slider's run): written (commitEq) a second after the last change
		Uint32 eqWebDirtyAt = 0;
		// a seek waiting for its file to open (the hand-off from a browser: at that position): path, seconds
		std::string seekPath;
		double seekTo = -1;
		Uint32 seekUntil = 0;
		// the paths a web selection plays (Up next lists them)
		std::vector<std::string> webSelection;
	};
}
