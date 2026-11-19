// The card's Manga folders read as series and chapters: see mangalocal.h. One pass of opendir/lstat per level, the
// reading order from manga_names. Listing opens no chapter file: what an archive holds (its page count, its first
// page, its ComicInfo.xml) is opened only by fillMangaSeries and remembered in a ChapterMetaCache, because opening
// every archive of a big card (a PDF through pdfium most of all) took minutes (docs/notes/2026-11-16-manga-library-scan.md).
#include "mangalocal.h"
#include "pagebook.h"
#include "comicinfo.h"
#include "filesave.h"
#include "manga_names.h"
#include "playlist.h"	// naturalLess, the order sortPages is built on
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// the design's caps (docs/design/manga.md, section 2, as given to this scanner)
		constexpr size_t maxSeries = 2000, maxChapters = 5000;

		std::string join(const std::string& dir, const std::string& name)
		{
			return dir == "/" ? "/" + name : dir + "/" + name;
		}

		// strip a place's trailing slashes once, so join reads well
		std::string trimTrailing(const std::string& p)
		{
			std::string s = p;
			while (s.size() > 1 && s.back() == '/') s.pop_back();
			return s;
		}

		char lower(char c)
		{
			return (char)std::tolower((unsigned char)c);
		}

		// hidden entries and macOS's metadata are not manga, in listings and in archives alike
		bool hidden(const std::string& name)
		{
			return !name.empty() && (name[0] == '.' || name == "__MACOSX");
		}

		// the lowercased extension without the dot, "" when there is none
		std::string extOf(const std::string& name)
		{
			size_t dot = name.rfind('.');
			if (dot == std::string::npos || dot == 0) return "";
			std::string e = name.substr(dot + 1);
			for (char& c : e) c = lower(c);
			return e;
		}

		// the one-file chapters this scanner can open (pagebook.h): CBZ (and a .zip renamed), CBR, PDF
		bool isArchiveName(const std::string& name)
		{
			// (not an EPUB, a comic's or a novel's: books are read in Files; the manga reader keeps to comics)
			return isPageBookName(name) && extOf(name) != "epub";
		}

		std::string stripArchiveExt(const std::string& name)
		{
			return stripBookExt(name);
		}

		std::string baseName(const std::string& path)
		{
			size_t slash = path.rfind('/');
			return slash == std::string::npos ? path : path.substr(slash + 1);
		}

		struct Archive
		{
			std::string name;
			int64_t size = 0, mtime = 0;	// from the listing's own lstat: the cache's key costs no second stat
		};

		struct Listing
		{
			bool ok = false;
			std::vector<std::string> images;	// natural order: the first is a chapter's page one and its cover
			std::vector<std::string> dirs;
			std::vector<Archive> archives;
		};

		// one level of a folder. lstat and nothing else: a symlink is neither a directory we walk into nor a file we
		// read, so a card's self-linked folder can't send the scan round in circles.
		Listing listOne(const std::string& dir)
		{
			Listing l;
			DIR* d = opendir(dir.c_str());
			if (!d) return l;
			l.ok = true;
			while (dirent* e = readdir(d))
			{
				std::string n = e->d_name;
				if (hidden(n)) continue;
				struct stat st;
				if (lstat(join(dir, n).c_str(), &st) != 0) continue;
				if (S_ISDIR(st.st_mode)) l.dirs.push_back(n);
				else if (!S_ISREG(st.st_mode)) continue;
				else if (isPageImage(n)) l.images.push_back(n);
				else if (isArchiveName(n)) l.archives.push_back({n, (int64_t)st.st_size, (int64_t)st.st_mtime});
			}
			closedir(d);
			sortPages(l.images);
			return l;
		}

		// ComicInfo's Number is text ("10.5", "Extra"): a double when the whole of it is one, else nothing
		double numberFromText(const std::string& s)
		{
			const char* p = s.c_str();
			char* end = nullptr;
			double v = std::strtod(p, &end);
			return (end != p && *end == '\0') ? v : -1;
		}

		// volOfParent: a volume folder's number, for a chapter whose own name says nothing about volumes
		LocalChapter makeFolder(const std::string& path, const std::string& name, const Listing& l, double volOfParent)
		{
			LocalChapter c;
			c.title = name;
			c.path = path;
			c.number = chapterNumber(name);
			c.volume = volumeNumber(name);
			if (c.volume < 0) c.volume = volOfParent;
			c.pages = (int)l.images.size();	// never empty: a folder chapter is a chapter because of its images
			if (!l.images.empty()) c.cover = join(path, l.images[0]);
			return c;
		}

		// an archive from its name alone; what it holds from the cache when the file is the one cached
		LocalChapter makeArchive(const std::string& path, const Archive& a, double volOfParent, const ChapterMetaCache* cache)
		{
			LocalChapter c;
			c.title = stripArchiveExt(baseName(path));
			c.path = path;
			c.archive = true;
			c.known = false;
			c.size = a.size;
			c.mtime = a.mtime;
			c.number = chapterNumber(c.title);
			c.volume = volumeNumber(c.title);
			if (c.volume < 0) c.volume = volOfParent;
			c.cover = path + "#";	// its first page, by position: CoverWorker opens the file for it
			ChapterFileMeta m;
			if (cache && cache->get(path, a.size, a.mtime, m)) applyChapterMeta(c, m);
			return c;
		}

		// the chapters of a series folder: its chapter subfolders, its archives, and one level down through folders
		// that hold neither but whose own children do — the volume folders of docs/design/manga.md, section 2
		void collectChapters(const std::string& dir, const Listing& l, std::vector<LocalChapter>& out, const ChapterMetaCache* cache)
		{
			for (const std::string& n : l.dirs)
			{
				std::string sub = join(dir, n);
				Listing s = listOne(sub);
				if (!s.ok) continue;
				if (!s.images.empty())
				{
					out.push_back(makeFolder(sub, n, s, -1));
					if (out.size() >= maxChapters) return;
					continue;
				}
				double vol = volumeNumber(n);
				for (const std::string& g : s.dirs)
				{
					std::string vg = join(sub, g);
					Listing v = listOne(vg);
					if (v.ok && !v.images.empty()) out.push_back(makeFolder(vg, g, v, vol));
					if (out.size() >= maxChapters) return;
				}
				for (const Archive& a : s.archives)
				{
					out.push_back(makeArchive(join(sub, a.name), a, vol, cache));
					if (out.size() >= maxChapters) return;
				}
			}
			for (const Archive& a : l.archives)
			{
				out.push_back(makeArchive(join(dir, a.name), a, -1, cache));
				if (out.size() >= maxChapters) return;
			}
		}

		// naturalLess has no tie rule of its own for equal names, and two chapters of one folder can't share a path
		bool natLess(const std::string& a, const std::string& b, const std::string& pa, const std::string& pb)
		{
			if (naturalLess(a, b)) return true;
			return !naturalLess(b, a) && naturalLess(pa, pb);
		}

		LocalSeries makeSeries(const std::string& path, const std::string& name, std::vector<LocalChapter>& chs)
		{
			LocalSeries s;
			s.path = path;
			s.name = name;
			s.chapters = std::move(chs);
			finishSeries(s);
			return s;
		}

		// a series folder in root, listed (false: nothing in it to read)
		bool listDir(const std::string& path, const std::string& n, const ChapterMetaCache* cache, LocalSeries& out)
		{
			Listing l = listOne(path);
			if (!l.ok) return false;
			std::vector<LocalChapter> chs;
			collectChapters(path, l, chs, cache);
			// the folder's own images make it a chapter only when nothing inside makes it a series (then they are
			// ignored, as the design says: series first)
			if (chs.empty() && !l.images.empty()) chs.push_back(makeFolder(path, n, l, -1));
			if (chs.empty()) return false;	// empty, or no manga to read in it
			out = makeSeries(path, n, chs);
			return true;
		}

		LocalSeries listArchive(const std::string& path, const Archive& a, const ChapterMetaCache* cache)
		{
			std::vector<LocalChapter> chs;
			chs.push_back(makeArchive(path, a, -1, cache));
			return makeSeries(path, stripArchiveExt(a.name), chs);
		}

		// ---- the cache file's line: tab-separated, escaped, and its own checksum last ----

		void escapeTo(std::string& out, const std::string& s)
		{
			for (char c : s)
			{
				if (c == '\\') out += "\\\\";
				else if (c == '\t') out += "\\t";
				else if (c == '\n') out += "\\n";
				else if (c == '\r') out += "\\r";
				else out += c;
			}
		}

		std::string unescape(const std::string& s)
		{
			std::string out;
			out.reserve(s.size());
			for (size_t i = 0; i < s.size(); i++)
			{
				if (s[i] != '\\' || i + 1 == s.size()) { out += s[i]; continue; }
				char n = s[++i];
				out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
			}
			return out;
		}

		uint32_t fnv32(const char* p, size_t n)
		{
			uint32_t h = 2166136261u;
			for (size_t i = 0; i < n; i++)
			{
				h ^= (unsigned char)p[i];
				h *= 16777619u;
			}
			return h;
		}

		bool toInt(const std::string& s, int64_t& v)
		{
			if (s.empty()) return false;
			char* end = nullptr;
			long long x = std::strtoll(s.c_str(), &end, 10);
			if (*end != '\0') return false;
			v = (int64_t)x;
			return true;
		}

		const char* const CACHE_HEAD = "# bp manga files 1";
		// before the checksum; a line written before BPChapter was kept has one fewer (its file had none to keep)
		constexpr size_t CACHE_FIELDS = 14;
	}

	ChapterFileMeta readChapterFile(const std::string& path)
	{
		ChapterFileMeta m;
		std::string err;
		auto z = openPageBook(path, err);
		if (!z) return m;	// unreadable: pages stays -1, and there is no metadata to read
		const std::vector<std::string>& p = z->pages();
		m.pages = (int)p.size();
		if (!p.empty()) m.firstPage = p[0];
		// ComicInfo.xml as the downloads write it: one small entry, read once and remembered
		std::string xml;
		ComicInfo ci;
		if (z->comicInfo(xml) && parseComicInfo(xml, ci))
		{
			m.comicInfo = true;
			m.ciNumber = ci.number;
			m.ciVolume = ci.volume;
			m.ciPageCount = ci.pageCount;
			m.ciSeries = ci.series;
			m.ciSeriesKey = ci.seriesKey;
			m.ciSourceId = ci.sourceId;
			m.ciChapterId = ci.chapterId;
			m.ciTitle = ci.title;
		}
		return m;
	}

	void applyChapterMeta(LocalChapter& c, const ChapterFileMeta& m)
	{
		c.known = true;
		c.pages = m.pages;
		c.cover = m.firstPage.empty() ? "" : c.path + "#" + m.firstPage;
		if (!m.comicInfo) return;
		// only to fill what the name doesn't carry (the name's number wins)
		if (c.number < 0) c.number = numberFromText(m.ciNumber);
		if (c.volume < 0 && m.ciVolume >= 0) c.volume = m.ciVolume;
		c.ciSeries = m.ciSeries;
		c.ciSeriesKey = m.ciSeriesKey;
	}

	void finishSeries(LocalSeries& s)
	{
		auto& chs = s.chapters;
		bool allNumbered = !chs.empty();
		for (const LocalChapter& c : chs)
			if (c.number < 0) { allNumbered = false; break; }
		if (allNumbered)
			std::stable_sort(chs.begin(), chs.end(), [](const LocalChapter& a, const LocalChapter& b) {
				if (a.volume != b.volume) return a.volume < b.volume;
				if (a.number != b.number) return a.number < b.number;
				return natLess(a.title, b.title, a.path, b.path);
			});
		else
			std::stable_sort(chs.begin(), chs.end(), [](const LocalChapter& a, const LocalChapter& b) {
				return natLess(a.title, b.title, a.path, b.path);
			});
		// the title: the folder's name unless every chapter is an archive whose ComicInfo agrees on one Series (an
		// archive not opened yet has no Series, so the name stands until the whole series is known)
		s.title = s.name;
		bool agrees = !chs.empty();
		for (const LocalChapter& c : chs)
			if (!c.archive || c.ciSeries != chs.front().ciSeries || c.ciSeries.empty()) { agrees = false; break; }
		if (agrees) s.title = chs.front().ciSeries;
		s.cover = chs.empty() ? "" : chs.front().cover;
	}

	void sortSeries(std::vector<LocalSeries>& series)
	{
		std::stable_sort(series.begin(), series.end(), [](const LocalSeries& a, const LocalSeries& b) {
			return natLess(a.title, b.title, a.path, b.path);
		});
	}

	bool seriesKnown(const LocalSeries& s)
	{
		for (const LocalChapter& c : s.chapters)
			if (!c.known) return false;
		return true;
	}

	std::vector<LocalSeries> listMangaFolder(const std::string& root, const ChapterMetaCache* cache,
		const std::function<bool(size_t)>& onSeries)
	{
		std::vector<LocalSeries> out;
		if (root.empty()) return out;
		std::string base = trimTrailing(root);
		Listing top = listOne(base);
		if (!top.ok) return out;
		for (const std::string& n : top.dirs)
		{
			if (out.size() >= maxSeries) break;
			LocalSeries s;
			if (!listDir(join(base, n), n, cache, s)) continue;
			out.push_back(std::move(s));
			if (onSeries && !onSeries(out.size())) return out;
		}
		for (const Archive& a : top.archives)
		{
			if (out.size() >= maxSeries) break;
			out.push_back(listArchive(join(base, a.name), a, cache));
			if (onSeries && !onSeries(out.size())) return out;
		}
		sortSeries(out);
		return out;
	}

	bool listMangaSeries(const std::string& root, const std::string& path, const ChapterMetaCache* cache, LocalSeries& out)
	{
		std::string base = trimTrailing(root), name = baseName(path);
		if (base.empty() || name.empty() || hidden(name) || path != join(base, name)) return false;
		struct stat st;
		if (lstat(path.c_str(), &st) != 0) return false;
		if (S_ISDIR(st.st_mode)) return listDir(path, name, cache, out);
		if (!S_ISREG(st.st_mode) || !isArchiveName(name)) return false;
		out = listArchive(path, {name, (int64_t)st.st_size, (int64_t)st.st_mtime}, cache);
		return true;
	}

	int fillMangaSeries(LocalSeries& s, ChapterMetaCache* cache, bool frontOnly, std::vector<ChapterFileFound>* opened)
	{
		int n = 0;
		auto open = [&](LocalChapter& c) {
			ChapterFileMeta m = readChapterFile(c.path);
			if (cache) cache->put(c.path, c.size, c.mtime, m);
			if (opened) opened->push_back({c.path, c.size, c.mtime, m});
			applyChapterMeta(c, m);
			n++;
		};
		if (frontOnly)
		{
			// the first chapter may change as numbers come from ComicInfo: a few rounds, never the whole series
			for (int round = 0; round < 8 && !s.chapters.empty() && !s.chapters.front().known; round++)
			{
				open(s.chapters.front());
				finishSeries(s);
			}
			return n;
		}
		for (LocalChapter& c : s.chapters)
			if (!c.known) open(c);
		finishSeries(s);
		return n;
	}

	std::vector<LocalSeries> scanMangaFolder(const std::string& root)
	{
		// the listing, then every archive opened: the same series, chapters and order the scan always gave
		std::vector<LocalSeries> out = listMangaFolder(root, nullptr);
		for (LocalSeries& s : out) fillMangaSeries(s, nullptr, false);
		sortSeries(out);
		return out;
	}

	// ---- ChapterMetaCache ----

	bool ChapterMetaCache::load(const std::string& path)
	{
		std::string text;
		{
			std::ifstream f(path, std::ios::binary);
			if (f)
			{
				std::ostringstream ss;
				ss << f.rdbuf();
				text = ss.str();
			}
		}
		{
			std::lock_guard<std::mutex> g(m);
			file = path;
		}
		if (text.empty()) return false;
		parse(text);
		std::lock_guard<std::mutex> g(m);
		dirty = false;
		return true;
	}

	bool ChapterMetaCache::save()
	{
		std::string text, to;
		{
			std::lock_guard<std::mutex> g(m);
			if (!dirty || file.empty()) return true;
			text = textLocked();
			to = file;
			dirty = false;
		}
		// (no fsync: this is a cache, and a file lost is only read again)
		if (writeFileAtomic(to, text)) return true;
		std::lock_guard<std::mutex> g(m);
		dirty = true;
		return false;
	}

	bool ChapterMetaCache::get(const std::string& path, int64_t size, int64_t mtime, ChapterFileMeta& out) const
	{
		std::lock_guard<std::mutex> g(m);
		auto it = map.find(path);
		if (it == map.end() || it->second.size != size || it->second.mtime != mtime) return false;
		out = it->second.meta;
		return true;
	}

	void ChapterMetaCache::put(const std::string& path, int64_t size, int64_t mtime, const ChapterFileMeta& meta)
	{
		std::lock_guard<std::mutex> g(m);
		Entry& e = map[path];
		e.size = size;
		e.mtime = mtime;
		e.meta = meta;
		dirty = true;
	}

	void ChapterMetaCache::prune(const std::vector<std::string>& roots, const std::unordered_set<std::string>& seen)
	{
		std::lock_guard<std::mutex> g(m);
		for (auto it = map.begin(); it != map.end();)
		{
			bool under = false;
			for (const std::string& r : roots)
			{
				std::string base = trimTrailing(r);
				if (it->first.size() > base.size() + 1 && it->first.compare(0, base.size(), base) == 0 && it->first[base.size()] == '/')
				{
					under = true;
					break;
				}
			}
			if (under && !seen.count(it->first))
			{
				it = map.erase(it);
				dirty = true;
			}
			else ++it;
		}
	}

	size_t ChapterMetaCache::size() const
	{
		std::lock_guard<std::mutex> g(m);
		return map.size();
	}

	std::string ChapterMetaCache::text() const
	{
		std::lock_guard<std::mutex> g(m);
		return textLocked();
	}

	std::string ChapterMetaCache::textLocked() const
	{
		std::string out = std::string(CACHE_HEAD) + "\n";
		for (const auto& kv : map)
		{
			const ChapterFileMeta& c = kv.second.meta;
			std::string line;
			escapeTo(line, kv.first);
			line += "\t" + std::to_string(kv.second.size) + "\t" + std::to_string(kv.second.mtime) + "\t" + std::to_string(c.pages) + "\t";
			escapeTo(line, c.firstPage);
			line += c.comicInfo ? "\t1\t" : "\t0\t";
			escapeTo(line, c.ciNumber);
			line += "\t" + std::to_string(c.ciVolume) + "\t" + std::to_string(c.ciPageCount);
			for (const std::string* f : {&c.ciSeries, &c.ciSeriesKey, &c.ciSourceId, &c.ciTitle, &c.ciChapterId})
			{
				line += "\t";
				escapeTo(line, *f);
			}
			char sum[16];
			snprintf(sum, sizeof sum, "\t%08x", fnv32(line.data(), line.size()));
			out += line + sum + "\n";
		}
		return out;
	}

	void ChapterMetaCache::parse(const std::string& text)
	{
		std::unordered_map<std::string, Entry> got;
		size_t at = 0;
		while (at < text.size())
		{
			size_t nl = text.find('\n', at);
			// the last line without its newline is a write cut short: its checksum decides, like any other line's
			std::string line = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
			at = nl == std::string::npos ? text.size() : nl + 1;
			if (line.empty() || line[0] == '#') continue;
			size_t tab = line.rfind('\t');
			if (tab == std::string::npos || line.size() - tab - 1 != 8) continue;
			char sum[16];
			snprintf(sum, sizeof sum, "%08x", fnv32(line.data(), tab));
			if (line.compare(tab + 1, 8, sum) != 0) continue;	// torn or garbled: that file is read again
			std::vector<std::string> f;
			size_t s = 0;
			for (;;)
			{
				size_t t = line.find('\t', s);
				if (t == std::string::npos || t >= tab) { f.push_back(line.substr(s, tab - s)); break; }
				f.push_back(line.substr(s, t - s));
				s = t + 1;
			}
			if (f.size() != CACHE_FIELDS && f.size() != CACHE_FIELDS - 1) continue;
			Entry e;
			int64_t pages = 0, vol = 0, pc = 0;
			if (!toInt(f[1], e.size) || !toInt(f[2], e.mtime) || !toInt(f[3], pages) || !toInt(f[7], vol) || !toInt(f[8], pc)) continue;
			std::string path = unescape(f[0]);
			if (path.empty() || path[0] != '/') continue;
			e.meta.pages = (int)pages;
			e.meta.firstPage = unescape(f[4]);
			e.meta.comicInfo = f[5] == "1";
			e.meta.ciNumber = unescape(f[6]);
			e.meta.ciVolume = (int)vol;
			e.meta.ciPageCount = (int)pc;
			e.meta.ciSeries = unescape(f[9]);
			e.meta.ciSeriesKey = unescape(f[10]);
			e.meta.ciSourceId = unescape(f[11]);
			e.meta.ciTitle = unescape(f[12]);
			if (f.size() > 13) e.meta.ciChapterId = unescape(f[13]);
			got[path] = std::move(e);
		}
		std::lock_guard<std::mutex> g(m);
		for (auto& kv : got) map[kv.first] = std::move(kv.second);
		dirty = true;
	}

	std::vector<std::string> mangaRoots(const std::vector<std::string>& places)
	{
		std::vector<std::string> out;
		for (const std::string& p : places)
		{
			if (p.empty() || p[0] != '/') continue;	// the places are absolute card paths
			std::string dir = trimTrailing(p);
			DIR* d = opendir(dir.c_str());
			if (!d) continue;
			const char* want[3] = {"Manga", "manga", "MANGA"};	// preference, not a set: the first hit that exists wins
			int best = 3;
			while (dirent* e = readdir(d))
			{
				for (int k = 0; k < best; k++)
				{
					if (std::string(e->d_name) != want[k]) continue;
					struct stat st;
					if (lstat(join(dir, want[k]).c_str(), &st) == 0 && S_ISDIR(st.st_mode)) best = k;
					break;
				}
			}
			closedir(d);
			if (best >= 3) continue;
			std::string path = join(dir, want[best]);
			if (std::find(out.begin(), out.end(), path) == out.end()) out.push_back(path);
		}
		return out;
	}
}
