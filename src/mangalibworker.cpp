#include "mangalibworker.h"
#include <SDL.h>
#include <cstdlib>
#include <unordered_set>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bp
{
	namespace
	{
		constexpr Uint32 kBatchMs = 250;	// answers in batches: each one is a pass of the Ui's lists, not each file
		constexpr Uint32 kProgressMs = 200;
	}

	void MangaLibWorker::setCacheFile(const std::string& file)
	{
		std::lock_guard<std::mutex> g(m);
		cacheFile = file;
	}

	void MangaLibWorker::start()
	{
		if (started) return;
		started = true;
		thread = std::thread([this] { run(); });
		thread.detach();
	}

	void MangaLibWorker::scan(const std::vector<std::string>& roots, uint64_t gen)
	{
		std::lock_guard<std::mutex> g(m);
		if (stopping) return;
		scanRoots = roots;
		scanWanted = true;
		wantGen = gen;	// (a scan under way sees it and stops)
		start();
		cv.notify_one();
	}

	void MangaLibWorker::fill(const LocalSeries& s)
	{
		Ask a;
		a.series = s.path;
		for (const LocalChapter& c : s.chapters)
			if (c.archive && !c.known) a.files.push_back({c.path, c.size, c.mtime, {}});
		if (a.files.empty()) return;
		std::lock_guard<std::mutex> g(m);
		if (stopping) return;
		// the newest ask first: the series page just opened is the one looked at
		asks.push_front(std::move(a));
		start();
		cv.notify_one();
	}

	void MangaLibWorker::files(const std::vector<ChapterFileFound>& files)
	{
		if (files.empty()) return;
		std::lock_guard<std::mutex> g(m);
		if (stopping) return;
		asks.push_back({"", files});
		start();
		cv.notify_one();
	}

	bool MangaLibWorker::cached(const std::string& path, int64_t size, int64_t mtime, ChapterFileMeta& out) const
	{
		return loaded && metaCache.get(path, size, mtime, out);
	}

	std::vector<MangaLibWorker::Result> MangaLibWorker::take()
	{
		std::lock_guard<std::mutex> g(m);
		signalled = false;
		std::vector<Result> out;
		out.swap(results);
		return out;
	}

	void MangaLibWorker::stop()
	{
		std::lock_guard<std::mutex> g(m);
		stopping = true;
		asks.clear();
		scanWanted = false;
		cv.notify_one();
	}

	void MangaLibWorker::post(Result r)
	{
		bool signal = false;
		{
			std::lock_guard<std::mutex> g(m);
			if (stopping) return;
			results.push_back(std::move(r));
			signal = !signalled;
			signalled = true;
		}
		if (signal) ready();
	}

	void MangaLibWorker::ensureLoaded()
	{
		std::string want;
		{
			std::lock_guard<std::mutex> g(m);
			want = cacheFile;
		}
		if (want.empty())
		{
			loaded = true;	// (no state folder: the index lives in memory only, still read before main asks it)
			return;
		}
		if (want == loadedFile) return;
		// (a few MB on a huge card: read here, never on the main thread)
		Uint32 t0 = SDL_GetTicks();
		metaCache.load(want);
		loadedFile = want;
		loaded = true;
		fprintf(stderr, "manga: %zu chapter files remembered (%s, %u ms)\n", metaCache.size(), want.c_str(), SDL_GetTicks() - t0);
	}

	void MangaLibWorker::run()
	{
#ifdef __linux__
		// this thread only: playback and the panels go first (the series page waits on it, so not as low as durations)
		setpriority(PRIO_PROCESS, (pid_t)syscall(SYS_gettid), 5);
#endif
		for (;;)
		{
			std::vector<std::string> roots;
			uint64_t gen = 0;
			{
				std::unique_lock<std::mutex> g(m);
				cv.wait(g, [&] { return !stopping && (scanWanted || !asks.empty()); });
				if (scanWanted)
				{
					roots = scanRoots;
					gen = wantGen;
					scanWanted = false;
				}
			}
			ensureLoaded();
			if (!runAsks()) continue;
			if (gen) runScan(std::move(roots), gen);
		}
	}

	bool MangaLibWorker::runAsks()
	{
		for (;;)
		{
			Ask a;
			{
				std::lock_guard<std::mutex> g(m);
				if (stopping) return false;
				if (asks.empty()) return true;
				a = std::move(asks.front());
				asks.pop_front();
			}
			runAsk(a);
		}
	}

	void MangaLibWorker::runAsk(const Ask& a)
	{
		Uint32 t0 = SDL_GetTicks(), batchAt = t0;
		int opened = 0;
		Result r;
		r.kind = Result::Metas;
		for (const ChapterFileFound& f : a.files)
		{
			if (stopping) return;
			Found one{a.series, f};
			// read meanwhile (the scan's probe, an ask before this one): no second open
			if (!metaCache.get(f.path, f.size, f.mtime, one.file.meta))
			{
				one.file.meta = readChapterFile(f.path);
				metaCache.put(f.path, f.size, f.mtime, one.file.meta);
				opened++;
			}
			r.metas.push_back(std::move(one));
			if (SDL_GetTicks() - batchAt >= kBatchMs)
			{
				post(std::move(r));
				r = Result{};
				r.kind = Result::Metas;
				batchAt = SDL_GetTicks();
			}
		}
		if (!r.metas.empty()) post(std::move(r));
		metaCache.save();
		fprintf(stderr, "manga: %s: %zu chapter files read in %u ms (%d opened)\n", a.series.empty() ? "downloads" : a.series.c_str(),
			a.files.size(), SDL_GetTicks() - t0, opened);
	}

	void MangaLibWorker::runScan(std::vector<std::string> roots, uint64_t gen)
	{
		Uint32 t0 = SDL_GetTicks(), progressAt = t0;
		// tests: BP_MANGA_SLOW=ms a series listed, as a slow card would take (the "Reading your library" line's shot)
		static const int slow = getenv("BP_MANGA_SLOW") ? atoi(getenv("BP_MANGA_SLOW")) : 0;
		std::vector<LocalSeries> all;
		for (const std::string& root : roots)
		{
			size_t before = all.size();
			std::vector<LocalSeries> one = listMangaFolder(root, loaded ? &metaCache : nullptr, [&](size_t n) {
				if (slow > 0) SDL_Delay((Uint32)slow);
				if (SDL_GetTicks() - progressAt >= kProgressMs)
				{
					progressAt = SDL_GetTicks();
					Result p;
					p.gen = gen;
					p.count = before + n;
					post(std::move(p));
				}
				return !stale(gen);
			});
			if (stale(gen)) return;
			for (auto& s : one) all.push_back(std::move(s));
		}
		Uint32 listMs = SDL_GetTicks() - t0;
		int chapters = 0;
		std::unordered_set<std::string> seen;
		for (const LocalSeries& s : all)
		{
			chapters += (int)s.chapters.size();
			for (const LocalChapter& c : s.chapters)
				if (c.archive) seen.insert(c.path);
		}
		{
			Result l;
			l.kind = Result::Listed;
			l.gen = gen;
			l.series = all;
			l.listMs = listMs;
			l.seriesN = (int)all.size();
			l.chaptersN = chapters;
			post(std::move(l));
		}
		// each series' first chapter, where it isn't known: its cover's entry, and whether it's a download
		int opened = 0;
		Uint32 batchAt = SDL_GetTicks();
		Result r;
		r.kind = Result::Metas;
		r.gen = gen;
		for (LocalSeries& s : all)
		{
			// a series page opened meanwhile goes first
			if (!runAsks() || stale(gen)) return;
			if (s.chapters.empty() || s.chapters.front().known) continue;
			std::vector<ChapterFileFound> got;
			opened += fillMangaSeries(s, &metaCache, true, &got);
			for (auto& f : got) r.metas.push_back({s.path, std::move(f)});
			if (SDL_GetTicks() - batchAt >= kBatchMs && !r.metas.empty())
			{
				post(std::move(r));
				r = Result{};
				r.kind = Result::Metas;
				r.gen = gen;
				batchAt = SDL_GetTicks();
			}
		}
		// files gone from a card that is in leave the cache with the scan that saw them gone
		metaCache.prune(roots, seen);
		metaCache.save();
		r.scanDone = true;
		r.listMs = listMs;
		r.ms = SDL_GetTicks() - t0;
		r.seriesN = (int)all.size();
		r.chaptersN = chapters;
		r.opened = opened;
		post(std::move(r));
	}
}
