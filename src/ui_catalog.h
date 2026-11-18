// The book catalogs (Page::Catalogs), the OPDS extension: saved OPDS 1.2 shelves, the folders inside them, and the
// books' files downloaded into <the card>/Books and opened in Files' own readers (EPUB in the text viewer, PDF and
// CBZ in the pages viewer). An area of Ui (Ui::Area, ui.h; docs/notes/ui-areas.md): its keys, its touches, its state
// and its drawing are here, not in ui.h.
//
// The network is the reader's own MangaNet (main's): its fetcher does the waiting on a thread, its server store keeps
// the addresses and the sealed passwords, and this page only builds the requests and reads the answers. A feed is
// OPDS 1.2 Atom (opds.h); navigation is a stack of feed URLs, paging follows the feed's own next link, and search asks
// the description the feed points at for its query template (OpenSearch).
#pragma once
#include "opds.h"
#include "mangaservers.h"
#include "ui.h"

namespace bp
{
	class MangaNet;

	struct Ui::Catalog : Ui::Area
	{
		explicit Catalog(Ui& u) : Area(u) {}

		// main's (the fetcher and the saved servers); null in the tests that build no Ui
		MangaNet* net = nullptr;

		// from Home's Extensions drawer: the catalogs' list, loaded from the store
		void open(Player& p);

		// ---- the area ----
		bool has(Page pg) const override { return pg == Page::Catalogs; }
		bool key(Button b, Player& p) override;
		bool pointer(const Pointer& pt, int x, int y, Player& p) override;
		void tick(Player& p) override;
		void draw(const Player& p) override;
		std::string signature(const Player& p) const override;

		// a field the on-screen keyboard finished (Ui::kbClose): typed into the form, or a search to run
		void kbDone(int field, const std::string& text, bool keep);

		enum View { V_SERVERS, V_FEED, V_FORM };
		enum FormRow { FR_NAME, FR_URL, FR_USER, FR_PASS, FR_TEST, FR_SAVE, FR_DELETE, FR_COUNT };

		// ---- the list of saved catalogs ----
		View view = V_SERVERS;
		std::vector<MangaServer> servers;
		int cursor = 0, scroll = 0;
		int pressed = -1;	// the row a finger went down on
		std::string note;	// a line under the title (a load's error, a hint, what a delete wants)

		// ---- the feed shown ----
		std::string serverId, serverName;
		std::string feedUrl, feedTitle;	// the feed on screen, and the address it came from
		std::vector<OpdsEntry> entries;
		std::string nextUrl;		// the feed's own next page, "" at the end
		std::string searchUrl;		// the OpenSearch description's address, "" when the catalog doesn't offer one
		std::vector<std::pair<std::string, std::string>> stack;	// the folders opened, back to the start
		std::string searchTemplate;	// the description's query, {searchTerms} in place
		bool loading = false;
		bool append = false;		// the answer adds a page to the list on screen
		uint64_t loadSerial = 0;
		uint64_t testSerial = 0;
		uint64_t descSerial = 0;
		bool testing = false;

		// ---- the form (a new catalog, or the one being edited) ----
		std::string formId, formName, formUrl, formUser, formPass;
		bool formPassTyped = false;
		bool deleteArmed = false;	// Delete needs its second press
		int formCursor = 0;

		// ---- a book coming down ----
		uint64_t dlSerial = 0;
		std::string dlTitle, dlPath;
		bool downloading = false;

		std::string searchDescUrl;	// the description being fetched for a search
		bool searchPending = false;

		// ---- the work ----
		void loadServers();
		void loadFeed(const std::string& url, const std::string& title, bool push, bool more);
		void openEntry(int row, Player& p);
		void startDownload(const OpdsEntry& e, Player& p);
		void startSearch(Player& p);
		void saveForm(Player& p);
		void startTest(Player& p);
		void deleteServer(Player& p);
		void newForm(const MangaServer* existing);
		int rowCount() const;
		int rowsShown() const { return 6; }
		Rect rowRect(int row) const { return {16, 88 + (row - scroll) * 56, 608, 52}; }
		Rect formRect(int row) const { return {16, 88 + row * 52, 608, 46}; }
		std::string formValue(int row) const;
	};
}
