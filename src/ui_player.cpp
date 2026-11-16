// The player screen (a video, a song), rebuilt on the redesign's component kit (ui_player.h says what it is and what
// stayed Ui's; docs/redesign/src/screens.js "video", "music", "video-chips", "video-more", "play-on", "lock").
#include "ui_player.h"
#include "ui_browse.h"
#include "ui_playlists.h"
#include "ui_sound.h"
#include "session_host.h"
#include <cmath>

namespace bp
{
	namespace
	{
		const char* const LOCK_NOTE = "Touch locked: hold the lock 1 s to unlock";
		// its key only where there's an L3 (the RG DS Plus, the 1024x768 one)
		bool hasL3() { return sx > 1.2f; }
	}

	bool Ui::touchIsLocked() const { return playerPage->touchLocked; }

	// ---- what plays next, and the lines ----

	bool Ui::PlayerPage::chapterLineShown(const Player& p) const
	{
		// before the first chapter starts there's none to name (and nothing to tap)
		return p.playing() && !p.playingPhoto() && !p.loading() && p.error.empty() && !p.chapters.empty() &&
			ui.shownChapter(p) >= 0 && ui.shownChapter(p) < (int)p.chapters.size();
	}

	Ui::PlayerPage::NextUp Ui::PlayerPage::nextUp(const Player& p) const
	{
		NextUp out;
		if (!p.playing() || p.playingPhoto()) return out;
		// the Session's list when it has this file (its Next is what the keys do), else the player's own, in order
		const Session& s = SessionHost::get().session();
		if (s.index >= 0 && s.index < (int)s.queue.size() && s.queue[s.index] == p.currentPath())
		{
			int to = nextPlace(s);
			if (to == NEXT_ROUND)
			{
				out.any = out.round = true;
				return out;
			}
			if (to < 0) return out;
			out.any = true;
			out.path = s.queue[to];
			out.note = std::to_string(to + 1) + " of " + std::to_string(s.queue.size()) + (s.shuffle ? " \xC2\xB7 shuffled" : "");
			return out;
		}
		if (p.index() + 1 >= p.count()) return out;
		out.any = true;
		out.path = p.paths()[p.index() + 1];
		out.note = std::to_string(p.index() + 2) + " of " + std::to_string(p.count());
		return out;
	}

	bool Ui::PlayerPage::listAct(Player& p)
	{
		// List: a playlist's song opens that playlist's own page (its edits, its order), what plays going on behind;
		// a folder, Recent or a selection: Up next
		if (p.source().kind == SourceKind::Playlist && !p.source().id.empty() && !p.playingPhoto())
		{
			ui.showSourceAfterLeave = true;
			focus = {};
			return true;
		}
		ui.openUpNext(p);
		return false;
	}

	std::string Ui::PlayerPage::crumb(const Player& p, const Glyph*& mark) const
	{
		mark = nullptr;
		const PlaySource& src = p.source();
		if (src.id.empty()) return "";
		if (src.kind == SourceKind::Playlist)
		{
			// the playlist (Liked: its heart); an item on a server says which
			mark = ui.plIsLiked(src.id) ? &G_HEART : &G_PLAYLIST;
			std::string out = "Playlist \xC2\xB7 " + src.title;
			NetPath np;
			if (isNetPath(p.currentPath()))
			{
				const NetServer* sv = ui.netStore && parseNetPath(p.currentPath(), np) ? ui.netStore->find(np.server) : nullptr;
				out += std::string(" \xC2\xB7 ") + (sv ? serverName(*sv) : "Network");
			}
			return out;
		}
		// the folder, as the browser says it
		mark = src.kind == SourceKind::NetFolder ? &G_NETWORK : &G_CARD;
		return ui.whereOf(src.id);
	}

	// ---- the chips ----

	std::vector<Ui::PlayerPage::ChipDef> Ui::PlayerPage::chips(const Player& p) const
	{
		std::vector<ChipDef> out;
		auto add = [&](ChipKind kind, const Glyph* g, const char* label, const std::string& value, bool away) {
			ChipDef d;
			d.kind = kind;
			d.look.glyph = g;
			d.look.label = label;
			d.look.value = value;
			d.look.away = away;
			d.look.off = !p.error.empty();	// a file that didn't open: nothing to choose or speed up
			out.push_back(d);
		};
		auto track = [&](TrackType type) {
			const Track* t = p.selected(type);
			if (!t) return std::string("Off");
			auto list = p.tracksOf(type);
			int n = 1;
			for (size_t i = 0; i < list.size(); i++)
				if (list[i].id == t->id) n = (int)i + 1;
			return lay::chipTrack(*t, n, list);
		};
		const Settings& st = p.settings;
		if (!p.playingAudio())
		{
			add(C_SUBS, &G_CC, "Subtitles", track(TrackType::Sub), false);
			add(C_AUDIO, &G_SPEAKER, "Audio", track(TrackType::Audio), false);
			add(C_PICTURE, &G_PICTURE, "Picture", aspectName(p.aspect), p.aspect != AspectMode::Fit);
		}
		else
		{
			// what changes the sound is yellow, so an EQ or effect left on can't go unnoticed
			bool fx = st.soundFx != SoundFx::None;
			add(C_SOUND, &G_EQ, "Sound", soundSummary(st), st.eqOn || fx);
			// the Show mode in words; a choice, not a state away from normal: never yellow
			int show = std::clamp(st.musicShow, 0, SHOWS - 1);
			std::string style = visualizerLabel(st.visualizer);
			add(C_TOP, showGlyph(show, st.visualizer), "Top screen",
				show == SHOW_ART ? "Album art" : show == SHOW_BOTH ? "Art + " + style : show == SHOW_VIS ? style : "Title only", false);
		}
		add(C_SPEED, &G_SPEED, "Speed", speedLabel(p.speed), std::fabs(p.speed - 1) > 1e-6);
		// the play mode, as X's round had it: the Loop (Off, All, One), or Shuffle with whichever Loop the web set
		if (st.shuffle) add(C_MODE, &G_SHUFFLE, "Shuffle", st.loop == LOOP_ALL ? "On" : st.loop == LOOP_ONE ? "One" : "Once", true);
		else add(C_MODE, &G_LOOP, "Repeat", LOOP_SHORT[std::clamp(st.loop, 0, LOOPS - 1)], st.loop != LOOP_OFF);
		return out;
	}

	void Ui::PlayerPage::chipAct(ChipKind kind, Player& p)
	{
		if (!p.error.empty()) return;
		switch (kind)
		{
		// a video's: its tab of Tracks
		case C_SUBS: ui.openTracks(TRACKS_SUBS, p); break;
		case C_AUDIO: ui.openTracks(TRACKS_AUDIO, p); break;
		case C_PICTURE: ui.openTracks(TRACKS_PICTURE, p); break;
		// a song's: the Sound page, the Display popup
		case C_SOUND: ui.sound->open(p, false); break;
		case C_TOP: ui.musicKey(Button::Select, p); break;
		case C_SPEED:
		{
			// round its choices (until View has its Speed tab: docs/redesign, "video-speed")
			double s = stepSpeed(p.speed, 1, true);
			p.setSpeed(s);
			p.osd("Speed " + speedLabel(s));
			break;
		}
		// In order, Loop all, Loop one, Shuffle, round (until List has its switches: "video-list")
		case C_MODE: ui.musicKey(Button::X, p); break;
		}
	}

	// ---- touch ----

	Ui::Rect Ui::PlayerPage::rect(int id, const lay::PlayerLayout& l) const
	{
		if (id == T_BACK) return l.backHit;
		if (id == T_MORE) return l.moreHit;
		if (id == T_LINE) return l.lineHit;
		if (id >= T_PREV && id <= T_NEXT) return l.transport[id - T_PREV];
		if (id >= T_LIKE && id <= T_VIEW) return l.keys[id - T_LIKE];
		if (id >= T_CHIP0 && id < T_CHIP0 + l.chips) return l.chip[id - T_CHIP0];
		if (id == T_NEXTLINE) return l.next;
		return {0, 0, 0, 0};
	}

	bool Ui::PlayerPage::usable(int id, const Player& p) const
	{
		bool err = !p.error.empty();
		switch (id)
		{
		case T_LINE: return chapterLineShown(p);
		// a file that didn't open: only what leads elsewhere (the next file, the list, More), and Like / Add
		case T_REW: case T_FF: return !err && p.duration > 0;
		case T_PLAY: return !err || p.errorNet;	// (the network dropped it: Retry)
		case T_VIEW: return !err;
		// Like and Add work on a file that didn't open too (it's still a file)
		case T_LIKE: case T_ADD: return ui.headerActions(p);
		case T_NEXT:
		{
			// (only In order has no Next at the last one: Shuffle starts a new round, a Loop goes back to the first)
			const Session& ss = SessionHost::get().session();
			return ss.index >= 0 && p.currentPath() == ss.queue[ss.index] ? hasNext(ss)
				: p.settings.shuffle || p.settings.loop != LOOP_OFF || p.index() + 1 < p.count();
		}
		default: return id >= T_CHIP0 && id < T_CHIP_END ? !err : true;
		}
	}

	bool Ui::PlayerPage::act(int id, Player& p)
	{
		switch (id)
		{
		// "<" over what plays leaves at once (it plays on behind the browser)
		case T_BACK: focus = {}; return true;
		case T_MORE: openMoreSheet(); return false;
		case T_LINE: ui.openTracks(TRACKS_CHAPTERS, p); return false;
		// a key on the screen: what its button does, whether the buttons themselves are on or not
		case T_PREV: return ui.pressAnyway(Button::L, p);
		case T_REW: return ui.pressAnyway(Button::Left, p);
		case T_PLAY: return ui.pressAnyway(Button::A, p);
		case T_FF: return ui.pressAnyway(Button::Right, p);
		case T_NEXT: return ui.pressAnyway(Button::R, p);
		case T_LIKE: ui.likeNow(p); return false;
		case T_ADD:
			if (p.playing()) ui.browse->playlists->openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p);
			return false;
		case T_LIST: case T_NEXTLINE: return listAct(p);
		case T_VIEW: return ui.pressAnyway(Button::Select, p);
		default: break;
		}
		if (id >= T_CHIP0 && id < T_CHIP_END)
		{
			auto list = chips(p);
			if (id - T_CHIP0 < (int)list.size()) chipAct(list[id - T_CHIP0].kind, p);
		}
		return false;
	}

	bool Ui::PlayerPage::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (p.playingPhoto())
		{
			// the photo viewer's (Ui's: its moves and lifts are taken before this, by its pan and its buttons)
			if (pt.kind != Pointer::Down) return false;
			if (ui.buttonRect(BACK_ICON).hit(x, y)) { ui.pressedBtn = BACK_ICON; return false; }
			return ui.photoDown(x, y, p);
		}
		lay::PlayerLayout l = lay::playerLayout((int)chips(p).size());
		if (pt.kind == Pointer::Down)
		{
			focus = {};	// (the D-pad's: a touch does not show it)
			pressed = T_NONE;
			// the header's keys, then the chapter line (its touch area reaches up over the title), then the bar
			for (int id : {T_BACK, T_MORE, T_LINE})
				if (pressed == T_NONE && rect(id, l).hit(x, y) && usable(id, p)) pressed = id;
			if (pressed != T_NONE) return false;
			if (l.barHit.hit(x, y) && p.duration > 0 && p.error.empty())
			{
				// a trackpad's scrub: a slide moves the place on by the finger's travel, a tap jumps there as it lifts
				// (drawBar shows slide.value while it's held). The picture follows too, keyframes only and no more often
				// than the decoder keeps up with (SCRUB_MS); lifted, the exact seek to where it was left, and the bar
				// held there until playback arrives (seekHeld)
				double pos = ui.shownPosition(p);
				ui.slideGrab(SL_SEEK, 0, {l.bar, {0, 1, 0}, false, SCRUB_MS, seekHeld >= 0 ? seekHeld : pos >= 0 ? std::clamp(pos / p.duration, 0.0, 1.0) : 0}, x, y, p,
					[this, scrubs = 0](double f, bool last, Player& p) mutable {
						if (!last)
						{
							scrubs++;
							p.scrub(f);
							return;
						}
						fprintf(stderr, "seek bar: let go at %.4f after %d scrubs\n", f, scrubs);
						p.seekFraction(f);
						ui.noteSeek(p, f * p.duration);
						seekHeld = f;
						seekHeldAt = SDL_GetTicks();
					});
				return false;
			}
			for (int id = T_PREV; id < T_COUNT; id++)
				if (rect(id, l).hit(x, y) && usable(id, p)) pressed = id;
			// Play while a page plays it: held, it plays here (Ui::tick times it); let go sooner, the page's play / pause
			if (pressed == T_PLAY && p.error.empty() && ui.pageDrives(p))
			{
				ui.playHoldAt = SDL_GetTicks();
				ui.playHoldTouch = true;
			}
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		int id = pressed;
		pressed = T_NONE;
		if (id == T_NONE) return false;
		if (id == T_PLAY && ((ui.playHoldTouch && ui.playHoldAt) || ui.pageDrives(p)))
		{
			ui.playHoldAt = 0;
			if (rect(id, l).hit(x, y) && ui.pageDrives(p)) ui.toggleBrowser(p);
			return false;
		}
		// let go off the key: nothing (a finger that slid away changed its mind)
		return rect(id, l).hit(x, y) && usable(id, p) && act(id, p);
	}

	// ---- Lock ----

	void Ui::PlayerPage::lock(Player& p)
	{
		touchLocked = true;
		lockHoldAt = 0;
		p.osd(LOCK_NOTE);
	}

	void Ui::PlayerPage::unlock()
	{
		touchLocked = false;
		lockHoldAt = 0;
	}

	bool Ui::PlayerPage::lockPointer(const Pointer& pt, int x, int y, Player& p)
	{
		// a hold on the lock (lockTimer times it) releases it; nothing else answers a finger
		Rect key = lay::playerLayout(0).lock;
		if (pt.kind == Pointer::Down && key.hit(x, y)) lockHoldAt = SDL_GetTicks();
		else if (pt.kind == Pointer::Up) lockHoldAt = 0;	// let go too soon: still locked
		else if (pt.kind == Pointer::Down && SDL_GetTicks() - lockNoticeAt > 3000)
		{
			lockNoticeAt = SDL_GetTicks();
			p.osd(LOCK_NOTE);
		}
		return false;
	}

	void Ui::PlayerPage::lockTimer()
	{
		// the lock held long enough: released (the finger still down presses nothing)
		if (touchLocked && lockHoldAt && SDL_GetTicks() - lockHoldAt >= UNLOCK_HOLD_MS)
		{
			unlock();
			ui.swallowTouch = true;
		}
		if (!touchLocked) lockHoldAt = 0;
	}

	// ---- keys ----

	bool Ui::PlayerPage::key(Button b, Player& p)
	{
		if (p.playingPhoto())
		{
			focus = {};
			if (ui.photoKey(b, p)) return false;
		}
		// dark: the playback keys work blind, so LEFT / RIGHT seek whatever was lit when it went dark
		if (!ui.panel.on()) focus = {};
		bool err = !p.error.empty();
		// the network dropped it: A tries again, from where it stopped (the session reloads it there)
		if (err && p.errorNet && b == Button::A)
		{
			fprintf(stderr, "net: trying %s again\n", p.currentPath().c_str());
			SessionHost::get().playLive(true);	// (live: its reload runs now, as Next's does)
			return false;
		}
		// a file that didn't open: nothing to pause or choose (Prev, Next, the list, More, the menu and leaving still go)
		if (err && (b == Button::A || b == Button::Select)) return false;
		if (!p.playingPhoto())
		{
			// the D-pad's focus: DOWN onto the chips, then the Next line; UP comes back. With nothing lit LEFT / RIGHT
			// seek, as ever (UP / DOWN used to change the speed: the easiest keys to hit by mistake in a film)
			auto list = chips(p);
			lay::Dir dir = b == Button::Up ? lay::Dir::Up : b == Button::Down ? lay::Dir::Down : b == Button::Left ? lay::Dir::Left : lay::Dir::Right;
			bool arrow = b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right;
			if (b == Button::Up || b == Button::Down || (arrow && focus.zone))
			{
				focus = lay::playerFocusMove(focus, dir, (int)list.size(), true);
				return false;
			}
			if (focus.zone && b == Button::B) { focus = {}; return false; }
			if (focus.zone && b == Button::A)
			{
				if (focus.zone == 2) return listAct(p);
				if (focus.chip < (int)list.size()) chipAct(list[focus.chip].kind, p);
				return false;
			}
		}
		switch (b)
		{
		case Button::A:
			// a page plays it: the page's play/pause as A lets go (Ui::release), here only once held (Ui::tick)
			if (ui.pageDrives(p)) { if (!ui.playHoldAt) { ui.playHoldAt = SDL_GetTicks(); ui.playHoldTouch = false; } break; }
			// the page is the output but can't hear (its tab gone a moment): play here
			if (ui.pageIsOut()) { ui.takeOver(p); break; }
			SessionHost::get().play(p.paused);	// (mpv answers the flip later: paused is still the old state)
			p.togglePause();
			break;
		// out of the player at once: what plays goes on behind the browser (nothing is lost, so a pocket's press costs
		// nothing); a photo closes
		case Button::B: return true;
		// More: Lock, Screen off, Info, Stop; Like, Add to playlist, Show in folder, Play on
		case Button::X: openMoreSheet(); break;
		case Button::Y: return listAct(p);
		// the menu opens on what plays (Like, Add to playlist...): Settings is a tab along
		case Button::Start: ui.openMainMenu(p, ui.playingTabShown(p) ? TAB_PLAYING : TAB_SETTINGS); break;
		// the stick pressed in (the RG DS Plus): Like, at once (over a photo it turns it, above)
		case Button::L3: ui.likeNow(p); break;
		// a song: the Display popup; a video: Tracks (Chapters, Subtitles, Audio, Picture)
		case Button::Select: if (!ui.musicKey(b, p)) ui.openTracksPage(p); break;
		// (a page playing it: from where it should be by now; the jump is passed on to the page)
		case Button::Left:
		case Button::Right:
			if (err) break;
			if (ui.predicting(p))
			{
				double t = std::clamp(ui.shownPosition(p) + (b == Button::Left ? -BACK_S : FORWARD_S), 0.0, p.duration);
				p.seekFraction(t / p.duration);
				ui.noteSeek(p, t);
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
		default: break;
		}
		return false;
	}

	void Ui::PlayerPage::tick(Player& p)
	{
		// the bar held at a released drag: let go once playback is there, or after 6 s whatever happens (a network file
		// buffers a while)
		if (seekHeld >= 0 && !ui.slideHeld(SL_SEEK) &&
			(p.duration <= 0 || std::fabs(ui.shownPosition(p) - seekHeld * p.duration) < 1.0 || SDL_GetTicks() - seekHeldAt > 6000))
			seekHeld = -1;
		if (listPulseAt && SDL_GetTicks() - listPulseAt >= LIST_PULSE_MS) listPulseAt = 0;
	}

	// ---- X: More, and Play on ----

	Ui::MoreSpec Ui::PlayerPage::moreSpec(const Player& p) const
	{
		MoreSpec m;
		// (nothing plays any more, or the player was left under the sheet: an empty list, and the sheet closes)
		if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) return m;
		Ui* u = &ui;
		PlayerPage* self = const_cast<PlayerPage*>(this);
		std::string path = p.currentPath();
		bool audio = p.playingAudio(), actions = ui.headerActions(p), liked = ui.likedNow(p);
		m.glyph = audio ? &G_NOTE : &G_FILM;
		m.title = !p.title.empty() ? p.title : entryTitle(path);
		m.note = audio && !p.artist().empty() ? p.artist() + (p.album().empty() ? "" : " \xC2\xB7 " + p.album())
			: ui.whereOf(isNetPath(path) ? netParent(path) : parentFolder(path));
		auto item = [](const Glyph* g, const std::string& label, std::function<bool(Player&)> act) {
			MoreItem it;
			it.glyph = g;
			it.label = label;
			it.act = std::move(act);
			return it;
		};
		// the tiles: what was the player's bottom row
		m.tiles.push_back(item(&G_LOCK, touchLocked ? "Unlock touch" : "Lock touch", [self](Player& p) {
			if (self->touchLocked) self->unlock();
			else self->lock(p);
			return false;
		}));
		m.tiles.push_back(item(&G_MOON, "Screen off", [u](Player&) { u->panel.set(false); return false; }));
		m.tiles.push_back(item(&G_INFO, "Info", [u](Player&) {
			u->page = Page::Info;
			u->cursor = u->scroll = 0;
			u->infoAt = 0;	// read at once
			return false;
		}));
		// (main stops it, the place kept: B only leaves the player, it plays on)
		m.tiles.push_back(item(&G_STOP, "Stop", [u](Player&) { u->stopPressed = true; return false; }));
		m.tiles.back().danger = true;
		// the rows: what START's Playing tab has
		MoreItem like = item(liked ? &G_HEART : &G_HEART_O, liked ? "Liked" : "Like", [u](Player& p) { u->likeNow(p); return false; });
		like.key = hasL3() ? "L3" : "";
		like.lit = liked;
		like.off = !actions;
		like.stay = true;	// (the row says what it is now)
		m.rows.push_back(like);
		MoreItem add = item(&G_PLUS, "Add to playlist...", [u](Player& p) {
			if (p.playing()) u->browse->playlists->openAddOne(p.currentPath(), p.title, p.loaded ? p.duration : -1, p);
			return false;
		});
		add.off = !actions;
		m.rows.push_back(add);
		// its folder: the player left first (it plays on behind the browser), then the folder (showBrowser)
		m.rows.push_back(item(&G_FOLDER, "Show in folder", [u](Player& p) { u->showAfterLeave = p.currentPath(); return true; }));
		// while a dashboard page that can play is open, or one plays it
		if (ui.playOnShown())
		{
			m.rows.push_back(item(&G_WEB, "Play on...", [self](Player&) { self->openPlayOn(); return false; }));
			m.rows.back().note = ui.outputName();
		}
		m.cols = 2;
		return m;
	}

	void Ui::PlayerPage::openMoreSheet()
	{
		pressed = T_NONE;
		ui.openMore([this](const Player& p) { return moreSpec(p); });
	}

	// Play on: where it plays (the Session's output), the handheld or a dashboard page that can play. A sheet of rows;
	// the list is read each time (a page that closes leaves it)
	struct Ui::PlayOnSheet : Ui::Overlay
	{
		static constexpr int SHOWN = 6;
		int cursor = 0, scroll = 0, pressed = -1;

		explicit PlayOnSheet(Ui& u) : Overlay(u)
		{
			// on the first that isn't where it plays (the one wanted, most of the time)
			auto list = ui.outChoices();
			std::string cur = here();
			for (int i = 0; i < (int)list.size(); i++)
				if (list[i].tab != cur) { cursor = i; break; }
			fit((int)list.size());
		}
		const char* name() const override { return "playon"; }
		std::string here() const
		{
			SessionHost& sh = SessionHost::get();
			return sh.pageIsOutput() ? sh.session().out.tab : "";
		}
		void fit(int n)
		{
			cursor = std::clamp(cursor, 0, std::max(0, n - 1));
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + SHOWN) scroll = cursor - SHOWN + 1;
			scroll = std::clamp(scroll, 0, std::max(0, n - SHOWN));
		}
		lay::Sheet layout(int n) const { return lay::sheet({0, std::min(n, SHOWN), 1, 0}); }

		void choose(int i, Player& p)
		{
			auto list = ui.outChoices();
			close();
			if (i < 0 || i >= (int)list.size()) return;
			ui.playOnSend(list[i], p);	// (where it plays already: just closed)
		}
		bool key(Button b, Player& p) override
		{
			int n = (int)ui.outChoices().size();
			fit(n);
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::A: choose(cursor, p); break;
			case Button::B: case Button::X: close(); break;
			default: break;
			}
			fit(n);
			return false;
		}
		bool pointer(const Pointer& pt, int x, int y, Player& p) override
		{
			if (ui.touchIsLocked()) return false;
			int n = (int)ui.outChoices().size();
			fit(n);
			lay::Sheet s = layout(n);
			if (pt.kind == Pointer::Down)
			{
				pressed = -1;
				if (!s.box.hit(x, y)) { close(); ui.swallowTouch = true; return false; }
				for (int i = 0; i < s.spec.rows; i++)
					if (s.row(i).hit(x, y)) pressed = scroll + i;
				return false;
			}
			if (pt.kind != Pointer::Up) return false;
			int i = pressed;
			pressed = -1;
			if (i >= scroll && i < n && s.row(i - scroll).hit(x, y)) choose(i, p);
			return false;
		}
		void draw(const Player& p) override
		{
			auto list = ui.outChoices();
			int n = (int)list.size();
			// (what played has gone under it: closed)
			if (!p.playing() || p.playingPhoto() || ui.page != Page::Main) { close(); return; }
			fit(n);
			lay::Sheet s = layout(n);
			Kit& k = *ui.kit;
			std::string title = !p.title.empty() ? p.title : entryTitle(p.currentPath());
			double pos = ui.shownPosition(p);
			k.sheet(s, {&G_WEB, "Play on", title + (pos >= 0 ? " \xC2\xB7 from " + formatTime(pos) : "")});
			SessionHost& sh = SessionHost::get();
			std::string cur = here();
			for (int i = 0; i < s.spec.rows; i++)
			{
				const OutChoice& c = list[scroll + i];
				bool hh = c.tab.empty(), at = c.tab == cur;
				Kit::Row row;
				row.glyph = hh ? &G_CARD : &G_LAPTOP;
				row.name = c.name;
				row.focused = scroll + i == cursor;
				row.pressed = scroll + i == pressed;
				// where it plays now says so; the handheld says what comes with it
				if (at) row.pill = !hh && sh.pageWaits() ? "Waiting for a tap there" : (hh ? !p.paused : sh.session().playing) ? "Playing now" : hh ? "Paused here" : "Paused there";
				else if (hh) row.note = "Equalizer, effects, visualizer";
				k.row(s.row(i), row);
			}
			ui.drawScrollBar(s.rowsY, s.spec.rows * lay::SHEET_ROW, scroll, SHOWN, n);
			k.hints({{"UP/DOWN", "Move"}, {"A", "Play there"}, {"B", "Back"}});
		}
		std::string signature(const Player& p) const override
		{
			std::string sig = std::to_string(cursor) + "," + std::to_string(scroll) + "," + std::to_string(pressed) + here() + "|" + std::to_string(p.paused);
			for (auto& c : ui.outChoices()) sig += "," + c.name;
			return sig;
		}
	};

	void Ui::PlayerPage::openPlayOn() { ui.pushOverlay(std::make_unique<PlayOnSheet>(ui)); }

	// ---- drawing ----

	std::vector<Ui::Hint> Ui::PlayerPage::hintList(const Player& p, bool chapters) const
	{
		if (!p.settings.buttonHotkeys) return {{"", "Button hotkeys are off: the keys on this screen do it all"}};
		if (focus.zone == 1) return {{"LEFT/RIGHT", "Chip"}, {"A", "Change"}, {"UP/B", "Back to playing"}, {"DOWN", "Next line"}};
		if (focus.zone == 2) return {{"A", "List"}, {"UP", "Chips"}, {"B", "Back to playing"}};
		if (ui.pageDrives(p))
			return {{"A", ui.pagePlaying() ? "Pause there" : "Play there"}, {"Hold A", "Play here"}, {"L/R", "Step"}, {"X", "More"}, {"B", "Back"}};
		std::vector<Hint> out{{"LEFT/RIGHT", "Seek"}, {"DOWN", "Chips"}};
		if (chapters) out.push_back({"L2/R2", "Chapter"});
		out.push_back({"X", "More"});
		out.push_back({"B", "Back, keeps playing"});
		return out;
	}

	void Ui::PlayerPage::drawBar(const Player& p, const lay::PlayerLayout& l)
	{
		Kit& k = *ui.kit;
		// (while dragged, and just after until playback catches up: where the finger is or left it)
		double pos = ui.shownPosition(p);
		bool dragging = ui.slideHeld(SL_SEEK), held = dragging || seekHeld >= 0;
		double heldFrac = dragging ? ui.slide.value : seekHeld;
		double frac = held ? heldFrac : p.duration > 0 && pos >= 0 ? std::clamp(pos / p.duration, 0.0, 1.0) : 0;
		std::vector<double> starts;
		for (auto& c : p.chapters) starts.push_back(c.time);
		// under a finger: the knob grown, the time there over it (the chapter's name goes under the bar)
		k.progress(l.bar, frac, lay::barMarks(l.bar, p.duration, starts), dragging,
			dragging ? formatTime(frac * p.duration) + " / " + formatTime(p.duration) : "");
		int cy = l.times.y + l.times.h / 2, mid = l.times.x + l.times.w / 2;
		ui.drawTextMid(formatTime(held ? heldFrac * p.duration : pos), F_SMALL, TEXT, l.times.x, cy);
		ui.drawTextMid(formatTime(p.duration), F_SMALL, DIM, l.times.x + l.times.w, cy, 0, 1);
		// how long is left, at the speed it plays (half at 2x)
		double left = timeLeft(held ? heldFrac * p.duration : pos, p.duration, p.speed);
		// dragging the bar of a file with chapters: the chapter under the finger instead ("3 · The Dragon")
		int under = -1;
		if (dragging && p.duration > 0)
			for (int i = 0; i < (int)p.chapters.size(); i++)
				if (p.chapters[i].time <= ui.slide.value * p.duration) under = i;
		if (under >= 0)
			ui.drawTextMid(std::to_string(under + 1) + " \xC2\xB7 " + chapterLabel(p.chapters[under], under + 1), F_I12, TEXT, mid, cy, 320, 2);
		else if (left >= 0) ui.drawTextMid(formatTime(left) + " left", F_I12, DIM, mid, cy, 0, 2);
	}

	void Ui::PlayerPage::draw(const Player& p)
	{
		if (listPulseArm)
		{
			listPulseArm = false;
			// (once per playlist: the first of its songs opened; another row of it, or the player opened again, not)
			bool pl = p.playing() && !p.playingPhoto() && p.source().kind == SourceKind::Playlist;
			if (pl && p.source().id != listPulsedFor)
			{
				listPulseAt = SDL_GetTicks();
				fprintf(stderr, "ui: the List key glows (a playlist's first song)\n");
			}
			if (p.playing() && !p.playingPhoto()) listPulsedFor = pl ? p.source().id : std::string();
		}
		if (p.playingPhoto()) { ui.drawPhoto(p); return; }

		Kit& k = *ui.kit;
		auto chipList = chips(p);
		lay::PlayerLayout l = lay::playerLayout((int)chipList.size());
		bool err = !p.error.empty(), hotkeys = p.settings.buttonHotkeys, audio = p.playingAudio();
		auto capOf = [&](const char* key) { return std::string(hotkeys ? key : ""); };
		if (ui.roots.empty()) ui.roots = browseRoots();	// (started with a file: the browser never read them)

		// ---- the header: back, what it plays from, its place in the list, the status marks, More ----
		k.iconKey(l.back, G_BACK, pressed == T_BACK, 3);
		k.iconKey(l.more, G_MORE, pressed == T_MORE);
		const int headY = l.back.y + l.back.h / 2;
		int statusRight = l.more.x - 10, x = l.back.x + l.back.w + 10;
		ui.drawBattery(statusRight, headY - 8, p, x + 120);
		int right = statusRight - (616 - ui.statusLeft(p)) - 10;	// (statusLeft: the marks' left edge when they end at 616)
		if (p.count() > 1)
		{
			std::string count = std::to_string(p.index() + 1) + " / " + std::to_string(p.count());
			ui.drawTextMid(count, F_I12, DIM, right, headY, 0, 1);
			right -= k.textW(count, F_I12) + 12;
		}
		const Glyph* mark = nullptr;
		std::string from = crumb(p, mark);
		if (mark)
		{
			ui.drawGlyphMid(*mark, x + 10, headY, DIM);
			x += 20 + 6;
		}
		// (a folder's path keeps its end, a playlist's name its start)
		if (right > x) ui.drawTextMid(p.source().kind == SourceKind::Playlist ? from : ui.elideLeft(from, F_I12, right - x), F_I12, DIM, x, headY, right - x);

		// ---- the title, and how it plays on its right ----
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		const Glyph* stMark = nullptr;
		std::string state;
		SDL_Color stInk = ACCENT;
		if (err)
		{
			stMark = &G_ALERT;
			state = !p.errorNet ? "Can't play this file" : p.errorNetFound ? "Lost the connection" : "Can't reach it";
			stInk = DANGER;
		}
		else if (p.loading())
		{
			state = "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.');
			stInk = DIM_ON;
		}
		else if (ui.predicting(p))
		{
			// a page plays it: where is the first thing to know
			stMark = &G_WEB;
			state = (SessionHost::get().pageWaits() ? "Waiting for " : ui.pagePlaying() ? "In " : "Paused in ") + ui.pageName();
		}
		else if (p.paused)
		{
			stMark = &G_PAUSE;
			state = "Paused";
			stInk = DIM_ON;
		}
		else
		{
			stMark = &G_PLAY;
			state = "Playing";
		}
		if (!hotkeys) state += " \xC2\xB7 buttons off";
		int titleY = l.title.y + l.title.h / 2, stW = std::min(300, k.textW(state, F_P16)), titleRight = l.title.x + l.title.w;
		ui.drawTextMid(state, F_P16, stInk, titleRight, titleY, 300, 1);
		if (stMark)
		{
			int gw = ui.glyphW(*stMark);
			ui.drawGlyphMid(*stMark, titleRight - stW - 7 - gw / 2, titleY, stInk);
			stW += 7 + gw;
		}
		ui.drawTextMid(!p.title.empty() ? p.title : err ? name : "Opening...", F_P28, TEXT, l.title.x, titleY, l.title.w - stW - 14);

		// ---- the line under it, then the bar (a file that didn't open: what went wrong, in their place) ----
		bool chapterLine = chapterLineShown(p);
		if (err)
		{
			auto lines = ui.wrapText(p.errorNet ? p.error + ". A tries again from where it stopped" : p.error, F_I14, l.line.w, 3);
			for (size_t i = 0; i < lines.size(); i++) ui.drawTextMid(lines[i], F_I14, TEXT, l.line.x, l.line.y + 16 + (int)i * 22, l.line.w);
		}
		else
		{
			if (chapterLine)
			{
				// the chapter playing: the count first, so a long name can be cut without losing the place. A tap opens the list
				int c = ui.shownChapter(p), n = (int)p.chapters.size();
				Kit::Line line;
				line.glyph = &G_CHAPTERS;
				line.text = chapterUntitled(p.chapters[c].title) ? "Chapter " + std::to_string(c + 1) + " of " + std::to_string(n)
					: std::to_string(c + 1) + " / " + std::to_string(n) + " \xC2\xB7 " + p.chapters[c].title;
				line.keys = capOf("L2/R2");
				line.pressed = pressed == T_LINE;
				line.font = F_I14;
				k.line(l.line, line);
			}
			else if (audio && (!p.artist().empty() || !p.album().empty()))
			{
				// a song: who it's by, and the album
				int cy = l.line.y + l.line.h / 2, tx = l.line.x + 26;
				ui.drawGlyphMid(G_NOTE, l.line.x + 10, cy, DIM_ON);
				if (!p.artist().empty())
				{
					ui.drawTextMid(p.artist(), F_P14, TEXT, tx, cy, 300);
					tx += std::min(300, k.textW(p.artist(), F_P14)) + 8;
				}
				if (!p.album().empty())
					ui.drawTextMid((p.artist().empty() ? "" : "\xC2\xB7  ") + p.album(), F_I12, DIM_ON, tx, cy, l.line.x + l.line.w - tx);
			}
			drawBar(p, l);
		}

		// ---- the transport: what is pressed all the time ----
		{
			Uint32 now = SDL_GetTicks();
			bool drives = ui.pageDrives(p) && !err;
			// a page plays it: the page's play and pause (a hold plays here)
			bool still = err || (ui.pageDrives(p) ? !ui.pagePlaying() : p.paused);
			Kit::Transport t[5];
			t[0].glyph = &G_PREV; t[0].label = "Prev"; t[0].key = capOf("L");	// never dimmed: it can always go back to the start
			t[1].glyph = &G_REW; t[1].label = "5 s"; t[1].key = capOf("LEFT");
			t[2].glyph = still ? &G_PLAY_BIG : &G_PAUSE_BIG; t[2].label = still ? "Play" : "Pause"; t[2].key = capOf("A");
			t[2].primary = true;
			if (err && p.errorNet) { t[2].glyph = &G_REFRESH; t[2].label = "Retry"; }
			if (drives)
			{
				// the hold that plays it here, the key filling as it goes
				t[2].third = ui.playHoldAt ? "Keep holding..." : "Hold: play it here";
				if (ui.playHoldAt) t[2].hold = std::min(1.0, (now - ui.playHoldAt) / (double)PLAY_HOLD_MS);
			}
			t[3].glyph = &G_FF; t[3].label = "10 s"; t[3].key = capOf("RIGHT");
			t[4].glyph = &G_NEXT; t[4].label = "Next"; t[4].key = capOf("R");
			for (int i = 0; i < 5; i++)
			{
				t[i].off = !usable(T_PREV + i, p);
				t[i].pressed = pressed == T_PREV + i && !t[i].off;
				k.transport(l.transport[i], t[i]);
			}
		}

		// ---- Like, Playlist, List, View ----
		{
			Uint32 now = SDL_GetTicks();
			bool liked = ui.likedNow(p), added = ui.addFlashUntil && now < ui.addFlashUntil;
			Kit::Key keys[4];
			keys[0].glyph = liked ? &G_HEART : &G_HEART_O; keys[0].label = liked ? "Liked" : "Like"; keys[0].key = hasL3() ? capOf("L3") : "";
			keys[0].lit = liked;
			// Add to playlist: no button of its own; a check for a moment once it's added
			keys[1].glyph = added ? &G_CHECK : &G_PLUS; keys[1].label = "Playlist"; keys[1].lit = added;
			keys[2].glyph = &G_PLAYLIST; keys[2].label = "List"; keys[2].key = capOf("Y");
			if (listPulseAt)
			{
				// two soft yellow swells (sin²)
				double sw = std::sin(M_PI * (now - listPulseAt) / (double)(LIST_PULSE_MS / 2));
				keys[2].glow = sw * sw;
			}
			keys[3].glyph = &G_EYE; keys[3].label = "View"; keys[3].key = capOf("SELECT");
			for (int i = 0; i < 4; i++)
			{
				keys[i].off = !usable(T_LIKE + i, p);
				keys[i].pressed = pressed == T_LIKE + i && !keys[i].off;
				k.key(l.keys[i], keys[i]);
			}
		}

		// ---- the chips: what is set sometimes, each saying its state ----
		for (int i = 0; i < l.chips; i++)
		{
			Kit::Chip c = chipList[i].look;
			c.focused = focus.zone == 1 && focus.chip == i;
			c.pressed = pressed == T_CHIP0 + i;
			k.chip(l.chip[i], c);
		}

		// ---- the Next line: what plays after this, before the list is opened ----
		{
			NextUp nx = nextUp(p);
			Kit::Line line;
			line.label = "Next";
			line.pressed = pressed == T_NEXTLINE;
			line.focused = focus.zone == 2;
			if (nx.round)
			{
				line.glyph = &G_SHUFFLE;
				line.text = "A new shuffled round";
			}
			else if (nx.any)
			{
				line.glyph = isAudioFile(nx.path) ? &G_NOTE : &G_FILM;
				line.text = entryTitle(nx.path);
				line.note = nx.note;
			}
			else
			{
				line.text = "Nothing after this";
				line.note = "End of the list";
				line.dim = true;
			}
			k.line(l.next, line);
		}

		k.hints(hintList(p, !p.chapters.empty() && !err));

		// locked: the controls under a veil, one large lock to hold
		if (touchLocked) k.lock(l.lock, lockHoldAt ? std::max(0.02, std::min(1.0, (SDL_GetTicks() - lockHoldAt) / (double)UNLOCK_HOLD_MS)) : 0);
	}

	std::string Ui::PlayerPage::signature(const Player& p) const
	{
		// what draw() shows that Ui's own signature doesn't hold: the finger and the focus, the lock, what comes next,
		// the song's tags (they arrive after its title)
		if (p.playingPhoto()) return "";
		NextUp nx = nextUp(p);
		std::string sig = "|pp" + std::to_string(pressed) + "," + std::to_string(focus.zone) + "," + std::to_string(focus.chip) + "," +
			std::to_string(touchLocked) + "," + std::to_string(seekHeld >= 0) + "|" + nx.path + "|" + nx.note + (nx.round ? "r" : "") + (nx.any ? "a" : "") +
			"|" + p.artist() + "|" + p.album();
		if (touchLocked && lockHoldAt) sig += "|hold" + std::to_string((SDL_GetTicks() - lockHoldAt) / 50);
		if (listPulseAt) sig += "|pulse" + std::to_string((SDL_GetTicks() - listPulseAt) / 33);
		return sig;
	}
}
