#include "ui_catalog.h"
#include "mangadownloader.h"
#include "manganet.h"
#include "mangaremote.h"
#include "ui_internal.h"
#include "ui_manga.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// a file's bytes as a row says them: "812 B", "1.4 MB"; "" when the feed gave no length
		std::string bytesText(long long n)
		{
			if (n < 0) return "";
			char buf[32];
			if (n < 1024) snprintf(buf, sizeof buf, "%lld B", n);
			else if (n < 1024LL * 1024) snprintf(buf, sizeof buf, "%.0f KB", n / 1024.0);
			else snprintf(buf, sizeof buf, "%.1f MB", n / (1024.0 * 1024.0));
			return buf;
		}

		// the query as an OpenSearch template wants it: everything but an unreserved character escaped
		std::string urlEncode(const std::string& s)
		{
			static const char* hex = "0123456789ABCDEF";
			std::string o;
			for (unsigned char c : s)
			{
				if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
				else
				{
					o += '%';
					o += hex[c >> 4];
					o += hex[c & 15];
				}
			}
			return o;
		}

		void subAll(std::string& s, const std::string& from, const std::string& to)
		{
			if (from.empty()) return;
			size_t p = 0;
			while ((p = s.find(from, p)) != std::string::npos)
			{
				s.replace(p, from.size(), to);
				p += to.size();
			}
		}

		// mkdir -p: the card may not have a Books folder until the first book comes
		bool makeDirs(const std::string& path)
		{
			if (path.empty()) return false;
			std::string at;
			for (size_t i = 0; i < path.size(); i++)
			{
				at += path[i];
				if (path[i] != '/' && i + 1 < path.size()) continue;
				if (at == "/" || at.empty()) continue;
				if (mkdir(at.c_str(), 0777) != 0 && errno != EEXIST) return false;
			}
			return true;
		}

		bool existsFile(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
		}

		// a book's place on the card: its title, its format's extension, and " (2)" when that name is taken
		std::string bookPath(const std::string& root, const std::string& title, const std::string& ext)
		{
			std::string name = MangaDownloader::safeName(title);
			if (name.empty()) name = "Book";
			std::string path = root + "/" + name + "." + ext;
			for (int n = 2; existsFile(path) && n < 100; n++)
				path = root + "/" + name + " (" + std::to_string(n) + ")." + ext;
			return path;
		}

		// the answer's own words for why it failed, as the reader's pages say them
		std::string answerError(const MangaFetch::Result& r)
		{
			if (!r.error.empty()) return "The server didn\xE2\x80\x99t answer";
			if (r.status == 401 || r.status == 403) return "The server refused the sign-in";
			if (r.status == 404) return "Nothing answered at that address";
			if (r.status == 0) return "The server didn\xE2\x80\x99t answer";
			return "The server answered HTTP " + std::to_string(r.status);
		}
	}

	void Ui::Catalog::open(Player& p)
	{
		(void)p;
		ui.page = Page::Catalogs;
		ui.cursor = ui.scroll = 0;
		view = V_SERVERS;
		cursor = scroll = pressed = 0;
		pressed = -1;
		note.clear();
		downloading = false;
		dlSerial = 0;
		loading = false;
		loadSerial = testSerial = descSerial = 0;
		entries.clear();
		stack.clear();
		feedUrl.clear();
		feedTitle.clear();
		nextUrl.clear();
		searchUrl.clear();
		searchTemplate.clear();
		if (!net)
		{
			note = "No network side in this build";
			return;
		}
		net->start();
		loadServers();
	}

	void Ui::Catalog::loadServers()
	{
		servers.clear();
		if (net)
			for (const MangaServer& s : net->servers().list())
				if (s.kind == "opds") servers.push_back(s);
		view = V_SERVERS;
		if (cursor >= (int)servers.size()) cursor = std::max(0, (int)servers.size() - 1);
		scroll = 0;
	}

	// ---- the feed ----

	void Ui::Catalog::loadFeed(const std::string& url, const std::string& title, bool push, bool more)
	{
		if (!net || !net->started())
		{
			note = "The network side is off";
			return;
		}
		const MangaServer* s = net->servers().find(serverId);
		if (!s)
		{
			note = "This catalog isn\xE2\x80\x99t saved";
			return;
		}
		if (!more)
		{
			if (push && !feedUrl.empty()) stack.push_back({feedUrl, feedTitle});
			feedUrl = url;
			feedTitle = title;
			entries.clear();
			nextUrl.clear();
			searchUrl.clear();
			searchTemplate.clear();
			cursor = scroll = 0;
		}
		append = more;
		note.clear();
		if (loadSerial) net->fetchApi().cancel(loadSerial);
		RemoteRequest rq = remoteAuth(*s, net->servers().password(serverId), url);
		for (auto& h : rq.headers)
			if (h.first == "Accept") h.second = "application/atom+xml";
		loading = true;
		loadSerial = net->fetchApi().get(rq.url, rq.headers, 8 << 20);
		if (!loadSerial)
		{
			loading = false;
			note = "The fetcher isn\xE2\x80\x99t running";
			return;
		}
		view = V_FEED;
	}

	void Ui::Catalog::openEntry(int row, Player& p)
	{
		if (view != V_FEED) return;
		if (row < 0) return;
		if (row >= (int)entries.size())
		{
			// the feed's own next page
			if (!nextUrl.empty() && !loading)
			{
				std::string url = nextUrl;
				loadFeed(url, feedTitle, false, true);
			}
			return;
		}
		const OpdsEntry& e = entries[row];
		std::string nav = e.nav();
		if (!nav.empty())
		{
			loadFeed(opdsResolve(feedUrl, nav), e.title, true, false);
			return;
		}
		OpdsAcquisition a = opdsAcquire(e);
		if (!a.href.empty())
		{
			if (!a.supported)
			{
				std::string kind = opdsFormatName(a.type);
				ui.alert("Can\xE2\x80\x99t open this book",
					kind.empty() ? "This catalog offers a file Banana Player can\xE2\x80\x99t read."
					             : "This catalog offers " + kind + ", which Banana Player can\xE2\x80\x99t read.");
				return;
			}
			startDownload(e, p);
			return;
		}
		if (!e.pse().empty()) ui.alert("A comic, not a book", "Page-streamed comics read in the Manga reader.");
		else ui.alert("Nothing to open", "This entry carries no file.");
	}

	void Ui::Catalog::startDownload(const OpdsEntry& e, Player& p)
	{
		(void)p;
		if (!net || !net->started()) { note = "The network side is off"; return; }
		const MangaServer* s = net->servers().find(serverId);
		if (!s) { note = "This catalog isn\xE2\x80\x99t saved"; return; }
		OpdsAcquisition a = opdsAcquire(e);
		if (!a.supported) return;
		std::string ext = opdsFormatExtension(a.type);
		if (ext.empty()) return;
		if (net->booksRoot().empty()) { note = "There\xE2\x80\x99s no card to put books on"; return; }
		if (!makeDirs(net->booksRoot()))
		{
			note = "The Books folder couldn\xE2\x80\x99t be made";
			return;
		}
		std::string path = bookPath(net->booksRoot(), e.title, ext);
		RemoteRequest rq = remoteAuth(*s, net->servers().password(serverId), opdsResolve(feedUrl, a.href));
		for (auto& h : rq.headers)
			if (h.first == "Accept") h.second = "*/*";
		dlSerial = net->fetchApi().get(rq.url, rq.headers, 1LL << 30, path);
		if (!dlSerial)
		{
			note = "The fetcher isn\xE2\x80\x99t running";
			return;
		}
		dlTitle = e.title;
		dlPath = path;
		downloading = true;
		view = V_FEED;
	}

	void Ui::Catalog::startSearch(Player& p)
	{
		(void)p;
		if (searchUrl.empty())
		{
			note = "This catalog doesn\xE2\x80\x99t offer search";
			return;
		}
		if (!net || !net->started()) return;
		const MangaServer* s = net->servers().find(serverId);
		if (!s) return;
		RemoteRequest rq = remoteAuth(*s, net->servers().password(serverId), searchUrl);
		for (auto& h : rq.headers)
			if (h.first == "Accept") h.second = "application/opensearchdescription+xml, application/xml;q=0.9, */*;q=0.1";
		if (descSerial) net->fetchApi().cancel(descSerial);
		descSerial = net->fetchApi().get(rq.url, rq.headers, 1 << 20);
		searchDescUrl = rq.url;
		searchPending = descSerial != 0;
		note = searchPending ? "Looking for the search form..." : "";
	}

	// ---- the form ----

	void Ui::Catalog::newForm(const MangaServer* existing)
	{
		formCursor = 0;
		if (existing)
		{
			formId = existing->id;
			formName = existing->name;
			formUrl = existing->url;
			formUser = existing->user;
			formPass.clear();
			formPassTyped = false;
		}
		else
		{
			formId.clear();
			formName.clear();
			formUrl = "https://";
			formUser.clear();
			formPass.clear();
			formPassTyped = false;
		}
		note.clear();
		deleteArmed = false;
		view = V_FORM;
	}

	std::string Ui::Catalog::formValue(int row) const
	{
		switch (row)
		{
		case FR_NAME: return formName.empty() ? "Name (Calibre, Gutenberg...)" : formName;
		case FR_URL: return formUrl;
		case FR_USER: return formUser.empty() ? "None" : formUser;
		case FR_PASS: return formPassTyped ? (formPass.empty() ? "None" : std::string(std::min<size_t>(formPass.size(), 12), '*'))
		                                 : !formId.empty() && net && net->servers().find(formId) && net->servers().find(formId)->hasPassword()
		                                     ? "Saved" : "None";
		case FR_TEST: return testing ? "Testing..." : "The address, the sign-in and a feed";
		case FR_SAVE: return note;
		case FR_DELETE: return formId.empty() ? "" : "Delete this catalog";
		default: return "";
		}
	}

	void Ui::Catalog::saveForm(Player& p)
	{
		(void)p;
		if (!net || !net->started()) { note = "The network side is off"; return; }
		MangaServer s;
		s.kind = "opds";
		s.name = trimmed(formName);
		s.url = withoutUrlCredentials(trimmed(formUrl));
		s.user = trimmed(formUser);
		s.id = formId;
		if (!isMangaServerUrl(s.url))
		{
			note = "That address isn\xE2\x80\x99t an http(s) URL";
			formCursor = FR_URL;
			return;
		}
		MangaServerStore& store = net->servers();
		std::string id;
		if (formId.empty())
		{
			id = store.add(s);
			if (id.empty()) { note = "A name is needed (up to 40 characters)"; formCursor = FR_NAME; return; }
			formId = id;
		}
		else
		{
			if (!store.update(s)) { note = "A name is needed (up to 40 characters)"; formCursor = FR_NAME; return; }
			id = formId;
		}
		if (formPassTyped && !store.setPassword(id, formPass)) { note = "The password couldn\xE2\x80\x99t be sealed"; return; }
		store.save();
		loadServers();
		for (int i = 0; i < (int)servers.size(); i++)
			if (servers[i].id == id) cursor = i;
		scroll = std::max(0, cursor - rowsShown() + 1);
		note = "Saved";
	}

	void Ui::Catalog::startTest(Player& p)
	{
		(void)p;
		if (!net || !net->started()) { note = "The network side is off"; return; }
		MangaServer s;
		s.kind = "opds";
		s.name = trimmed(formName);
		s.url = withoutUrlCredentials(trimmed(formUrl));
		s.user = trimmed(formUser);
		s.id = formId;
		if (!isMangaServerUrl(s.url)) { note = "That address isn\xE2\x80\x99t an http(s) URL"; return; }
		std::string pw = formPassTyped ? formPass : formId.empty() ? std::string() : net->servers().password(formId);
		RemoteRequest rq = remoteAuth(s, pw, s.url);
		for (auto& h : rq.headers)
			if (h.first == "Accept") h.second = "application/atom+xml";
		if (testSerial) net->fetchApi().cancel(testSerial);
		testSerial = net->fetchApi().get(rq.url, rq.headers, 8 << 20);
		testing = testSerial != 0;
		note = testing ? "Testing..." : "";
	}

	void Ui::Catalog::deleteServer(Player& p)
	{
		(void)p;
		if (formId.empty() || !net) return;
		// twice: the first press only says what the second does (a catalog is an address, an account and a password)
		if (!deleteArmed)
		{
			deleteArmed = true;
			note = "Press A again to delete it";
			return;
		}
		net->servers().remove(formId);
		net->servers().save();
		deleteArmed = false;
		note = "Deleted";
		loadServers();
	}

	// ---- the keys ----

	bool Ui::Catalog::key(Button b, Player& p)
	{
		if (!p.settings.catalogsOn)
		{
			ui.manga->openExtensionsDrawer(p);
			return false;
		}
		if (downloading)
		{
			if (b == Button::B)
			{
				if (net && dlSerial) net->fetchApi().cancel(dlSerial);
				dlSerial = 0;
				downloading = false;
				note = "Download stopped";
			}
			return false;
		}
		if (view == V_FORM)
		{
			if (b != Button::A) deleteArmed = false;	// Delete's second press is for Delete alone
			switch (b)
			{
			case Button::Up: formCursor = (formCursor + FR_COUNT - 1) % FR_COUNT; break;
			case Button::Down: formCursor = (formCursor + 1) % FR_COUNT; break;
			case Button::A:
				if (formCursor != FR_DELETE) deleteArmed = false;
				switch (formCursor)
				{
				case FR_NAME: ui.openKeyboard(CATALOG_NAME_FIELD, formName); break;
				case FR_URL: ui.openKeyboard(CATALOG_URL_FIELD, formUrl); break;
				case FR_USER: ui.openKeyboard(CATALOG_USER_FIELD, formUser); break;
				case FR_PASS: ui.openKeyboard(CATALOG_PASS_FIELD, formPassTyped ? formPass : ""); break;
				case FR_TEST: startTest(p); break;
				case FR_SAVE: saveForm(p); break;
				case FR_DELETE: deleteServer(p); break;
				default: break;
				}
				break;
			case Button::B: view = V_SERVERS; note.clear(); break;
			case Button::Start: ui.openMainMenu(p); break;
			default: break;
			}
			return false;
		}
		if (view == V_FEED)
		{
			int n = rowCount();
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % std::max(1, n); break;
			case Button::Down: cursor = (cursor + 1) % std::max(1, n); break;
			case Button::Left: cursor = std::max(0, cursor - rowsShown()); break;
			case Button::Right: cursor = std::min(std::max(0, n - 1), cursor + rowsShown()); break;
			case Button::A: openEntry(cursor, p); break;
			case Button::B:
				if (!stack.empty())
				{
					auto prev = stack.back();
					stack.pop_back();
					loadFeed(prev.first, prev.second, false, false);
				}
				else
				{
					feedUrl.clear();
					feedTitle.clear();
					loadServers();
				}
				break;
			case Button::Y: startSearch(p); break;
			case Button::X:
				if (!feedUrl.empty()) loadFeed(feedUrl, feedTitle, false, false);
				break;
			case Button::Start: ui.openMainMenu(p); break;
			default: break;
			}
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + rowsShown()) scroll = cursor - rowsShown() + 1;
			return false;
		}
		// the saved catalogs
		int n = (int)servers.size();
		switch (b)
		{
		case Button::Up:
			if (n) cursor = (cursor + n - 1) % n;
			break;
		case Button::Down:
			if (n) cursor = (cursor + 1) % n;
			break;
		case Button::A:
			if (cursor >= 0 && cursor < n)
			{
				serverId = servers[cursor].id;
				serverName = servers[cursor].name;
				feedUrl.clear();
				feedTitle.clear();
				entries.clear();
				stack.clear();
				loadFeed(servers[cursor].url, servers[cursor].name, false, false);
			}
			break;
		case Button::Y: newForm(nullptr); break;
		case Button::X:
			if (cursor >= 0 && cursor < n) newForm(&servers[cursor]);
			break;
		case Button::B: ui.manga->openExtensionsDrawer(p); break;
		case Button::Start: ui.openMainMenu(p); break;
		default: break;
		}
		if (cursor < scroll) scroll = cursor;
		if (cursor >= scroll + rowsShown()) scroll = cursor - rowsShown() + 1;
		return false;
	}

	bool Ui::Catalog::pointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind != Pointer::Up) return false;
		if (downloading) return false;
		if (view == V_FORM)
		{
			if (x < 16 || x > 624 || y < 88) return false;
			int row = (y - 88) / 52;
			if (row >= FR_COUNT) return false;
			if (row != FR_DELETE) deleteArmed = false;
			formCursor = row;
			switch (row)
			{
			case FR_NAME: ui.openKeyboard(CATALOG_NAME_FIELD, formName); break;
			case FR_URL: ui.openKeyboard(CATALOG_URL_FIELD, formUrl); break;
			case FR_USER: ui.openKeyboard(CATALOG_USER_FIELD, formUser); break;
			case FR_PASS: ui.openKeyboard(CATALOG_PASS_FIELD, formPassTyped ? formPass : ""); break;
			case FR_TEST: startTest(p); break;
			case FR_SAVE: saveForm(p); break;
			case FR_DELETE: deleteServer(p); break;
			default: break;
			}
			return false;
		}
		if (y < 88 || x < 16 || x > 624) return false;
		int row = scroll + (y - 88) / 56;
		if (row >= rowCount()) return false;
		cursor = row;
		if (view == V_FEED) openEntry(row, p);
		else if (row < (int)servers.size())
		{
			serverId = servers[row].id;
			serverName = servers[row].name;
			feedUrl.clear();
			feedTitle.clear();
			entries.clear();
			stack.clear();
			loadFeed(servers[row].url, servers[row].name, false, false);
		}
		return false;
	}

	int Ui::Catalog::rowCount() const
	{
		if (view == V_SERVERS) return (int)servers.size();
		int n = (int)entries.size();
		if (!nextUrl.empty()) n++;
		return n;
	}

	// ---- the answers ----

	void Ui::Catalog::tick(Player& p)
	{
		if (!net || !net->started()) return;
		for (MangaFetch::Result& r : net->takeFetched())
		{
			if (testSerial && r.id == testSerial)
			{
				testSerial = 0;
				testing = false;
				if (!r.error.empty() || r.status < 200 || r.status >= 300) note = answerError(r);
				else
				{
					OpdsFeed f;
					std::string body(r.body.begin(), r.body.end());
					if (!parseOpds(body, f)) note = "The server answered, but not like a catalog";
					else note = "Connected \xC2\xB7 " + std::to_string(f.entries.size()) +
						(f.entries.size() == 1 ? " entry" : " entries");
				}
				continue;
			}
			if (descSerial && r.id == descSerial)
			{
				descSerial = 0;
				searchPending = false;
				if (!r.error.empty() || r.status < 200 || r.status >= 300) { note = answerError(r); continue; }
				std::string body(r.body.begin(), r.body.end());
				std::string base = r.finalUrl.empty() ? searchDescUrl : r.finalUrl;
				searchTemplate = opdsSearchTemplate(body, base);
				if (searchTemplate.empty()) note = "This catalog\xE2\x80\x99s search form couldn\xE2\x80\x99t be read";
				else { note.clear(); ui.openKeyboard(CATALOG_SEARCH_FIELD, ""); }
				continue;
			}
			if (dlSerial && r.id == dlSerial)
			{
				dlSerial = 0;
				downloading = false;
				if (!r.error.empty() || r.status < 200 || r.status >= 300)
				{
					note = "The book didn\xE2\x80\x99t come down: " + answerError(r);
					continue;
				}
				note = "Saved to Books";
				if (!ui.openDocument(dlPath, p)) ui.alert("Saved, but can\xE2\x80\x99t open it", "The file is in Books beside the other card folders.");
				continue;
			}
			if (loadSerial && r.id == loadSerial)
			{
				loadSerial = 0;
				loading = false;
				if (!r.error.empty() || r.status < 200 || r.status >= 300)
				{
					note = answerError(r);
					append = false;
					continue;
				}
				std::string body(r.body.begin(), r.body.end());
				std::string base = r.finalUrl.empty() ? feedUrl : r.finalUrl;
				OpdsFeed f;
				if (!parseOpds(body, f))
				{
					note = "The server answered, but not like a catalog";
					append = false;
					continue;
				}
				if (append) entries.insert(entries.end(), f.entries.begin(), f.entries.end());
				else entries = f.entries;
				append = false;
				note.clear();
				nextUrl = f.next.empty() ? "" : opdsResolve(base, f.next);
				searchUrl = f.search.empty() ? "" : opdsResolve(base, f.search);
				if (!f.title.empty() && feedTitle.empty()) feedTitle = f.title;
				if (cursor >= rowCount()) cursor = std::max(0, rowCount() - 1);
				// the catalog's own start feed names itself: the title the rows say comes from the store
				continue;
			}
		}
	}

	// ---- the drawing ----

	void Ui::Catalog::draw(const Player& p)
	{
		if (view == V_FORM)
		{
			ui.drawHeader(formId.empty() ? "Add catalog" : "Edit catalog", p);
			ui.drawTextMid("OPDS 1.2 catalog \xC2\xB7 a Calibre library, Gutenberg, Standard Ebooks...", 0, DIM, 16, 64, 608, 0);
			for (int i = 0; i < FR_COUNT; i++)
			{
				Rect r = formRect(i);
				bool at = formCursor == i;
				ui.drawBox(r, at ? BUTTON : SURFACE, R_FIELD, at);
				const char* label = i == FR_NAME ? "Name" : i == FR_URL ? "Address" : i == FR_USER ? "User"
					: i == FR_PASS ? "Password" : i == FR_TEST ? "Test" : i == FR_SAVE ? "Save" : "Delete";
				ui.drawTextMid(label, 0, at ? ACCENT : TEXT, r.x + 16, r.y + r.h / 2, 140);
				std::string v = formValue(i);
				ui.drawTextMid(v, 0, at ? TEXT : DIM, r.x + 170, r.y + r.h / 2, r.w - 190, 0);
			}
			ui.drawHints({{"A", "Change"}, {"B", "Back"}, {"START", "Menu"}});
			return;
		}
		ui.drawHeader(view == V_FEED ? (feedTitle.empty() ? serverName : feedTitle) : "Catalogs", p);
		int n = rowCount();
		std::string line;
		if (!note.empty()) line = note;
		else if (loading) line = "Loading...";
		else if (view == V_SERVERS) line = "OPDS \xC2\xB7 " + std::to_string(n) + (n == 1 ? " catalog" : " catalogs");
		else line = serverName + " \xC2\xB7 " + std::to_string((int)entries.size()) + ((int)entries.size() == 1 ? " entry" : " entries");
		ui.drawTextMid(line, 0, note.empty() ? DIM : TEXT, 16, 64, 608, 0);

		for (int i = scroll; i < n && i < scroll + rowsShown(); i++)
		{
			Rect r = rowRect(i);
			bool at = cursor == i;
			if (at) ui.drawFocus(r);
			if (view == V_SERVERS)
			{
				const MangaServer& s = servers[i];
				ui.drawTextMid(s.name, 1, at ? ACCENT : TEXT, 32, r.y + 18, 500);
				ui.drawTextMid(s.user.empty() ? s.url : s.url + " \xC2\xB7 as " + s.user, 0, DIM, 32, r.y + 36, 540);
			}
			else if (i < (int)entries.size())
			{
				const OpdsEntry& e = entries[i];
				std::string nav = e.nav();
				OpdsAcquisition a = opdsAcquire(e);
				std::string right;
				if (!nav.empty()) right = "Folder";
				else if (!a.href.empty())
				{
					std::string fmt = opdsFormatName(a.type);
					right = fmt.empty() ? "File" : fmt;
					std::string b = bytesText(a.length);
					if (!b.empty()) right += " \xC2\xB7 " + b;
					if (!a.supported) right += " \xC2\xB7 can\xE2\x80\x99t open";
				}
				else if (!e.pse().empty()) right = "Comic";
				ui.drawTextMid(e.title, 1, at ? ACCENT : TEXT, 32, r.y + 18, 420);
				std::string sub = !e.author.empty() ? e.author : nav.empty() ? "" : "Folder";
				if (!sub.empty()) ui.drawTextMid(sub, 0, DIM, 32, r.y + 36, 420);
				if (!right.empty()) ui.drawTextMid(right, 0, at ? DIM_ON : DIM, 612, r.y + 26, 190, 1);
			}
			else
				ui.drawTextMid("More...", 1, at ? ACCENT : TEXT, 32, r.y + 26, 400);
		}
		ui.drawScrollBar(88, rowsShown() * 56 - 4, scroll, rowsShown(), n);
		if (n == 0 && !loading && note.empty())
		{
			ui.drawTextMid(view == V_SERVERS ? "No catalogs yet" : "Nothing here", 1, TEXT, W / 2, 200, 560, 2);
			ui.drawTextMid(view == V_SERVERS ? "Y adds one: its OPDS address, and a name for it." : "This feed has no books or folders.",
				0, DIM, W / 2, 228, 560, 2);
		}
		if (downloading)
		{
			ui.drawBox({60, 190, 520, 90}, SURFACE, R_DIALOG, true);
			ui.drawTextMid("Downloading", 1, TEXT, 300, 216, 460, 2);
			ui.drawTextMid(dlTitle, 0, ACCENT, 300, 240, 460, 2);
			ui.drawHints({{"B", "Stop"}});
			return;
		}
		if (view == V_SERVERS)
			ui.drawHints({{"A", "Open"}, {"Y", "Add"}, {"X", "Edit"}, {"B", "Back"}, {"START", "Menu"}});
		else
			ui.drawHints({{"A", "Open"}, {"B", "Back"}, {"Y", "Search"}, {"X", "Reload"}, {"START", "Menu"}});
	}

	std::string Ui::Catalog::signature(const Player& p) const
	{
		(void)p;
		char buf[256];
		snprintf(buf, sizeof buf, "|cat%d %d,%d %d %d %d %d %d %s|%s|%s|%s %d %d",
			(int)view, cursor, scroll, (int)servers.size(), (int)entries.size(), loading, downloading, (int)stack.size(),
			note.c_str(), feedTitle.c_str(), nextUrl.c_str(), (searchUrl.empty() ? "" : "s"), formCursor,
			(int)(formPassTyped + 2 * testing + 4 * searchPending));
		return buf;
	}

	// ---- the keyboard's answers ----

	void Ui::Catalog::kbDone(int field, const std::string& text, bool keep)
	{
		switch (field)
		{
		case CATALOG_NAME_FIELD: formName = text; break;
		case CATALOG_URL_FIELD: formUrl = text; break;
		case CATALOG_USER_FIELD: formUser = text; break;
		case CATALOG_PASS_FIELD:
			if (keep)
			{
				formPass = text;
				formPassTyped = true;
			}
			break;
		case CATALOG_SEARCH_FIELD:
			if (!keep || text.empty()) break;
			if (searchTemplate.empty()) break;
			{
				std::string url = searchTemplate;
				std::string q = urlEncode(text);
				subAll(url, "{searchTerms}", q);
				subAll(url, "%7BsearchTerms%7D", q);
				subAll(url, "%7bsearchTerms%7d", q);
				loadFeed(url, "Search: " + text, true, false);
			}
			break;
		default: break;
		}
	}
}
