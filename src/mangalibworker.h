// The Manga app's "On this handheld" library read off the main thread (docs/notes/2026-11-16-manga-library-scan.md).
// A huge card took minutes on the main thread when every chapter file was opened at each open of the app; here:
//
//   scan   the Manga folders listed by name (no chapter file opened: mangalocal.h's listMangaFolder), what each
//          archive holds taken from the ChapterMetaCache when unchanged; the listing is answered at once, then each
//          series' first chapter is opened if still unknown (one file a series: its cover's entry, its title's and a
//          download's ComicInfo), answered in batches; the cache pruned and saved.
//   fill   one series' unknown chapter files opened (the series page is open: their numbers order the list).
//   files  the Downloads tab's chapter files opened (their ComicInfo says which are downloads).
//
// fill and files go before the rest of a scan's probes. One thread, made on the heap and never destroyed (like
// CoverWorker: it may be inside pdfium at exit). It never touches Ui or Player: answers go to a mutex-guarded list
// the main thread takes, and ready() (on the worker's thread) tells the loop there are some.
#pragma once
#include "mangalocal.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	class MangaLibWorker
	{
	public:
		// one chapter file's contents, for the series (its path; "" for a files ask) it was asked for
		struct Found
		{
			std::string series;
			ChapterFileFound file;
		};
		struct Result
		{
			enum Kind { Progress, Listed, Metas } kind = Progress;
			uint64_t gen = 0;		// the scan's (Progress, Listed, a scan's Metas); 0 for fill and files
			size_t count = 0;		// Progress: series listed so far
			std::vector<LocalSeries> series;	// Listed: every Manga folder's, in the roots' order
			std::vector<Found> metas;			// Metas
			bool scanDone = false;	// the scan's last answer: the numbers below are its whole
			unsigned listMs = 0, ms = 0;
			int seriesN = 0, chaptersN = 0, opened = 0;
		};

		explicit MangaLibWorker(std::function<void()> ready) : ready(std::move(ready)) {}
		// <state>/manga/files.tsv: read on the worker's thread before its first job
		void setCacheFile(const std::string& file);
		// the roots read again; a scan under way is dropped (its answers keep their own gen)
		void scan(const std::vector<std::string>& roots, uint64_t gen);
		// the series' chapters not known yet
		void fill(const LocalSeries& s);
		// these chapter files (path, size, mtime as listed)
		void files(const std::vector<ChapterFileFound>& files);
		// main thread: what the cache holds for a file as listed (false: not read yet, or the cache isn't loaded)
		bool cached(const std::string& path, int64_t size, int64_t mtime, ChapterFileMeta& out) const;
		const ChapterMetaCache* cache() const { return loaded ? &metaCache : nullptr; }
		std::vector<Result> take();
		void stop();

	private:
		struct Ask
		{
			std::string series;		// a fill's; "" a files ask
			std::vector<ChapterFileFound> files;
		};
		void start();	// under lock
		void run();
		void ensureLoaded();
		void runScan(std::vector<std::string> roots, uint64_t gen);
		bool runAsks();	// the fills and files waiting; false when stopping
		void runAsk(const Ask& a);
		void post(Result r);
		bool stale(uint64_t gen) const { return wantGen.load() != gen || stopping.load(); }

		std::function<void()> ready;
		std::thread thread;
		mutable std::mutex m;
		std::condition_variable cv;
		std::string cacheFile, loadedFile;
		ChapterMetaCache metaCache;
		std::atomic<bool> loaded{false};
		std::deque<Ask> asks;
		std::vector<std::string> scanRoots;
		bool scanWanted = false;
		std::atomic<uint64_t> wantGen{0};
		std::vector<Result> results;
		bool signalled = false, started = false;
		std::atomic<bool> stopping{false};
	};
}
