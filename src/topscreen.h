#pragma once
// The top panel: its window and GL context, and what main draws on it besides mpv (the manga reader, Files' text
// viewer, the frame taken from mpv under their pages, its screenshots). main.cpp keeps the loop and the panels'
// decisions; the panel's own state lives here so the loop reads it by name.
#include "reader.h"
#include "readerfetch.h"
#include "textreader.h"
#include <SDL.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

namespace bp
{
	class MangaNet;
	class Player;
	struct Settings;
	struct Ui;

	struct TopScreen
	{
		TopScreen(SDL_Window* win, SDL_GLContext ctx, std::string fonts);

		// a reader open, or About's support QR up: the Ui's own picture on the top panel; not while the player's own
		// pages show (they show what plays)
		bool onTop(const Ui& ui) const;
		// What plays under a photo or a reader's pages: a song's graph makes frames still (it carries the sound), and
		// mpv waits for each to be taken. They're taken here into a tiny target with rendering skipped (nothing drawn,
		// no GL time), so the photo or the comic isn't drawn again 30 times a second and the bottom panel keeps the GPU
		void drainUnder(Player& player, Player& photos, bool readerOnTop);
		// the top panel's pixels (RGBA, top row first), the frame drawn again for it: screenshots
		SDL_Surface* capture(Player& shown, bool readerOnTop, bool qr);
		// the level card (levelcard.h: one picture, the same on every screen) over a reader's pages, mpv's own being
		// under them; barTop: the bar's whole (levelTop), 0 for the label alone (the reader's zoom)
		void cardOnReader(const std::string& label, int percent, bool bar, int barTop, int ms);
		// the manga reader and Files' text viewer: what the bottom panel asked for, its commands, what they show back
		void syncReaders(Ui& ui, Player& player, MangaNet* mangaNet, const std::string& stateDir, Uint32 now, bool woke, bool haveUi,
			const std::function<void(const std::string&, int, int)>& showLevelCard, std::atomic<bool>& renderPending);
		// a frame on the top panel: a reader's pages, or mpv's picture (what plays, a photo, the icon)
		void draw(Player& player, Player& photos, Player& shown, const Ui& ui, Uint32 now, bool refresh, Uint32 levelUntil,
			std::atomic<bool>& renderPending);
		// Lite (VIS_LITE): the player draws its glow and bars itself, from the af tap's level, in place of mpv's
		// picture (the art is still mpv's, under it). Called only while a song with Lite plays.
		void drawLite(Player& shown, Player& photos, Uint32 now, bool refresh, std::atomic<bool>& renderPending);
		// the glow and bars into the current framebuffer (no swap); the loop and screenshots both use it. False when
		// nothing was drawn (quiet and fallen back, or no GL): the panel still shows the frame it had
		bool drawLiteFrame(Player& shown, Uint32 now, bool force);
		SDL_Window* top;
		SDL_GLContext gl;
		std::string fontsDir;
		unsigned sinkFbo = 0;	// the tiny target what plays is taken into under a photo or the pages
		// the manga reader: its pages on the top panel while a chapter is open (Ui asks for it: takeMangaRead)
		MangaReader reader;
		uint64_t zoomShownGen = 0;	// the chapter the zoom card follows (a new chapter opens at 100%: no card for that)
		// Files' text viewer (TXT, EPUB): its rows on the top panel while a document is open (Ui asks: takeTextRead)
		TextReader textReader;
		bool textReady = false;
		ReaderFetch readerFetch;	// a URL chapter's pages, kept coming while reader is open
		bool readerReady = false;
		Uint32 evReader = 0;	// a page decoded: the loop wakes and takes it
		bool topDark = false;	// the top panel cleared while there's no video
		int topDraws = 0;
		bool logNextTop = false;
		// Lite's drawing state: the bars' eased heights (immediate attack, 250 ms fall), the beat's glow (180 ms
		// fall), and when it last drew (redrawn at the visualizer-fps setting, never while paused or silent)
		static constexpr int LITE_BARS = 12;
		double liteBars[LITE_BARS] = {};
		double litePulse = 0;
		Uint32 liteAt = 0, liteTickAt = 0;
		uint64_t liteBeatsSeen = 0;
		int liteLoads = -1;
		bool liteDrew = false;
		// where Lite's frames go (in the log): whether the panel is drawing it now, how many frames were presented
		// and what the tap gave since the last 10 s line (the readings, their rate, the gain and the beats)
		bool liteOnPanel = false;
		uint64_t liteFrames = 0, liteFramesShown = 0, liteMetaShown = 0, liteReadingsShown = 0, liteBeatsShown = 0;
		Uint32 liteLogAt = 0;
		// About's support QR, large (Ui::aboutQrUp): the panel's own picture while it is up, composed at its pixels
		// (supportqr.h) and drawn 1:1 through its own renderer, so the code's edges stay the file's
		PageRenderer qrRender;
		bool qrReady = false, qrUp = false;
		int qrW = 0, qrH = 0;

	private:
		// the QR drawn 1:1 into the top window (the context made current here); false when it couldn't be composed
		bool drawQr(int w, int h);
		// the text viewer's look (Settings: the Text viewer's rows)
		TextReader::Style textStyle(const Settings& s) const;
		Uint32 slowLogged = 0;	// when a take under the pages was last logged (once per 5 s)
	};
}
