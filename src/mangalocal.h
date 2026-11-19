#pragma once
// "On this handheld" (docs/design/manga.md, sections 2 and 5.1): what series and chapters one Manga folder on the
// card holds. Directory reads with opendir and lstat, no threads and no window: the caller (the library worker,
// mangalibworker.h) decides when to scan. symlinks are never followed into directories (lstat), so a loop on a card
// can't hang or repeat the scan.
//
// Two speeds (docs/notes/2026-11-16-manga-library-scan.md): listMangaFolder reads names only and opens no chapter
// file, taking what an archive holds (its pages, first page, ComicInfo) from a ChapterMetaCache when the file is
// unchanged; fillMangaSeries opens what is still unknown. scanMangaFolder is both at once, as it always was.

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bp
{
	// one chapter: a folder of images or a .cbz/.zip archive. path: the folder or the archive file; title: its name,
	// without the extension for an archive. number/volume: chapterNumber/volumeNumber of that name, or for an archive
	// its ComicInfo.xml's Number/Volume when the name carries none; -1 is not known. pages: the images of a folder or
	// the archive's page count, -1 when it could not be read.
	//
	// An archive listed without being opened has known false: pages -1, its number and volume from its name only, cover
	// "archive.cbz#" (its first page, whatever that is called: CoverWorker opens it) and no ComicInfo yet.
	struct LocalChapter
	{
		std::string title;
		std::string path;
		bool archive = false;
		double number = -1;
		double volume = -1;
		int pages = -1;
		std::string ciSeriesKey;	// a download's: the source series it came from ("src:…", from its ComicInfo)
		std::string ciSeries;		// its ComicInfo's Series ("" none)
		std::string cover;			// its first page: a picture's path, "archive.cbz#entry", or "archive.cbz#" (not read)
		bool known = true;			// a folder's always; an archive's once opened or found unchanged in the cache
		int64_t size = 0, mtime = 0;	// an archive's, as listed: the cache's key
	};

	// one series: its chapters in reading order. cover: the first chapter's first page — a path for a folder chapter,
	// "archive.cbz#entry" for an archive, "" when none could be read. title: the folder or file name, unless every
	// chapter is an archive whose ComicInfo.xml says the same Series (a downloaded chapter links back that way).
	struct LocalSeries
	{
		std::string name;	// the folder's name, or the file's without its extension: the title ComicInfo doesn't give
		std::string title;
		std::string path;
		std::string cover;
		std::vector<LocalChapter> chapters;
	};

	// what one Manga folder holds (root; "" and a folder that isn't there give nothing). A folder of images directly
	// in root is a one-chapter series named after the folder; a folder whose children are chapter folders and/or
	// .cbz/.zip files is a series of those chapters; an archive directly in root is a one-chapter series named after
	// the file. Nested deeper is ignored except one level of volume folders ("Vol 1/Chapter 3" is chapters, the volume
	// from the volume folder's name). Hidden entries and __MACOSX are skipped; a folder of images that also holds
	// chapter folders reads as a series, its own images ignored. At most 2000 series and 5000 chapters per series,
	// because a messy card should not build a list nobody can scroll.
	// Every archive is opened (its page count, first page and ComicInfo), so this is slow on a big card: the tests'
	// scan. The app lists with listMangaFolder and fills lazily.
	std::vector<LocalSeries> scanMangaFolder(const std::string& root);

	// the Manga folders of the Files places (each a card root like /data or /run/sd2): the child folder named Manga,
	// manga or MANGA, in the places' order, without duplicates. The name is matched as readdir writes it, so a place
	// on a case-insensitive file system gives back the case it really has.
	std::vector<std::string> mangaRoots(const std::vector<std::string>& places);

	// What opening one chapter file tells: its pages (-1 unreadable), its first page's entry, its ComicInfo.xml's
	// fields when it has one that parses (comicInfo). Remembered per file in a ChapterMetaCache.
	struct ChapterFileMeta
	{
		int pages = -1;
		std::string firstPage;
		bool comicInfo = false;
		std::string ciNumber, ciSeries, ciSeriesKey, ciSourceId, ciTitle;
		std::string ciChapterId;	// a download's BPChapter: the source chapter it is a copy of ("" in one downloaded before 0.7.34)
		int ciVolume = -1, ciPageCount = -1;
	};
	ChapterFileMeta readChapterFile(const std::string& path);	// opens it (openPageBook): slow, never on the main thread
	void applyChapterMeta(LocalChapter& c, const ChapterFileMeta& m);	// known true; fills what the name doesn't carry
	// the chapters in reading order, the title (ComicInfo's Series when every chapter is a known archive that agrees,
	// else name) and the cover (the first chapter's), from what is known now: run again after applyChapterMeta
	void finishSeries(LocalSeries& s);
	void sortSeries(std::vector<LocalSeries>& series);	// natural order of title, ties on path
	bool seriesKnown(const LocalSeries& s);	// every chapter known: its order and title are final

	// What each chapter file held, keyed by path and checked against its size and mtime, so a file opened once is
	// never opened again until it changes. A plain text file in the state folder (one line per file, each with its
	// own checksum: a torn or garbled line, as an SD card leaves, is skipped and that file read again), written beside
	// and renamed over. Thread safe: the library worker fills it, the main thread reads it.
	class ChapterMetaCache
	{
	public:
		bool load(const std::string& file);		// remembers the file; false when it isn't there (an empty cache)
		bool save();							// when changed since; tmp + rename
		bool get(const std::string& path, int64_t size, int64_t mtime, ChapterFileMeta& out) const;
		void put(const std::string& path, int64_t size, int64_t mtime, const ChapterFileMeta& meta);
		// the files under one of roots that a whole scan didn't see (deleted, renamed) leave; the rest stay (a card
		// that is out keeps its files' entries for when it is back)
		void prune(const std::vector<std::string>& roots, const std::unordered_set<std::string>& seen);
		size_t size() const;
		std::string text() const;				// the file's contents (save writes it)
		void parse(const std::string& text);	// what load reads; bad lines skipped

	private:
		struct Entry
		{
			int64_t size = 0, mtime = 0;
			ChapterFileMeta meta;
		};
		std::string textLocked() const;
		mutable std::mutex m;
		std::string file;
		std::unordered_map<std::string, Entry> map;
		bool dirty = false;
	};

	// The two-speed scan. listMangaFolder: the series and chapters of one Manga folder as scanMangaFolder finds them,
	// but no chapter file opened: an archive is known only when cache (may be null) has it unchanged. onSeries, when
	// given, hears the count found so far and stops the listing by answering false (a newer scan wants the thread).
	std::vector<LocalSeries> listMangaFolder(const std::string& root, const ChapterMetaCache* cache,
		const std::function<bool(size_t)>& onSeries = nullptr);
	// one entry of root (a series folder, or an archive at its top) listed the same way; false when it holds no manga
	bool listMangaSeries(const std::string& root, const std::string& path, const ChapterMetaCache* cache, LocalSeries& out);

	// a chapter file opened by fillMangaSeries, as it was listed and what it held
	struct ChapterFileFound
	{
		std::string path;
		int64_t size = 0, mtime = 0;
		ChapterFileMeta meta;
	};
	// the series' unknown archives opened (frontOnly: only until its first chapter is known: all its cover, title and
	// a download's link need), each into cache when given, then finishSeries. Each opened one also goes to opened.
	// Returns how many files were opened.
	int fillMangaSeries(LocalSeries& s, ChapterMetaCache* cache, bool frontOnly, std::vector<ChapterFileFound>* opened = nullptr);
}
