#include "volumekey.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <initializer_list>
#include <poll.h>
#include <string>
#include <unistd.h>
#ifdef __linux__
#include <dirent.h>
#include <linux/input.h>
#include <SDL.h>
#include <sys/ioctl.h>
#endif

namespace bp
{
	static long long monoMs()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
	}

#ifdef __linux__
	// the device BananaOS calls the volume keys (its own, apart from the power key and the lid switch)
	static const char* const deviceName = "gpio-keys-volume";
	static constexpr long long holdMs = 600;	// a press this long is next / previous, not a volume step
	static constexpr long long stuckMs = 5000;	// down this long: stuck in a pocket, ignored till let go
	static constexpr long long beatLimitMs = 3000;	// a main loop silent this long (frozen): the grab goes

	// the first /dev/input/event* whose name is the volume keys' ("" none)
	static std::string findDevice()
	{
		DIR* d = opendir("/dev/input");
		if (!d) return "";
		std::string found;
		while (dirent* e = readdir(d))
		{
			if (strncmp(e->d_name, "event", 5)) continue;
			std::string path = std::string("/dev/input/") + e->d_name;
			int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;
			char name[256] = {};
			bool match = ioctl(fd, EVIOCGNAME(sizeof name), name) >= 0 && !strcmp(name, deviceName);
			close(fd);
			if (match)
			{
				found = path;
				break;
			}
		}
		closedir(d);
		return found;
	}

	// back to bananad: the grab dropped, the fd closed (the kernel would drop it anyway)
	static void release(int& fd, const std::string& why)
	{
		if (fd < 0) return;
		ioctl(fd, EVIOCGRAB, 0);
		close(fd);
		fd = -1;
		fprintf(stderr, "volume keys: released (%s)\n", why.c_str());
	}

	static void pushAction(uint32_t type, int what)
	{
		SDL_Event ev{};
		ev.type = type;
		ev.user.code = what;
		SDL_PushEvent(&ev);
	}

	// one volume key's press: when it went down, whether its one action was taken, and whether it is stuck
	struct KeyState
	{
		long long downAt = 0;
		bool acted = false, stuck = false;
	};

	static void down(KeyState& k, const char* name)
	{
		k.downAt = monoMs();
		k.acted = k.stuck = false;
		fprintf(stderr, "volume keys: %s down\n", name);
	}
#endif

	// main, each pass: while want, the keys are ours; a main loop that stopped beating (3 s) lets them go by itself
	void VolumeKey::tick(bool want, const char* why)
	{
		if (!want && why) wantWhy = why;
		wantGrab = want;
		beat = monoMs();
	}

	bool VolumeKey::start(uint32_t event)
	{
		eventType = event;
#ifdef __linux__
		if (pipe(stopPipe) < 0)
		{
			fprintf(stderr, "volume keys: can't start: %s\n", strerror(errno));
			return false;
		}
		thread = std::thread([this] { run(); });
		return true;
#else
		return false;	// a Mac: the volume keys are the OS's (a stub that does nothing)
#endif
	}

	void VolumeKey::stop()
	{
		if (!thread.joinable()) return;
		quitting = true;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		close(stopPipe[0]);
		close(stopPipe[1]);
		stopPipe[0] = stopPipe[1] = -1;
	}

	void VolumeKey::run()
	{
#ifdef __linux__
		int fd = -1;
		bool refused = false;
		KeyState up, downk;
		for (;;)
		{
			if (quitting.load()) break;
			long long now = monoMs();
			bool want = wantGrab.load();
			if (!want)
			{
				release(fd, wantWhy.load());
				refused = false;
				up = downk = KeyState();
			}
			else if (now - beat.load() > beatLimitMs)
			{
				// the main loop stopped coming round: a frozen player must not keep the keys from bananad
				release(fd, "the player stopped answering");
				up = downk = KeyState();
			}
			else if (fd < 0 && !refused)
			{
				std::string path = findDevice();
				if (path.empty())
				{
					fprintf(stderr, "volume keys: no %s in /dev/input: the keys stay BananaOS's\n", deviceName);
					refused = true;
				}
				else if ((fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
				{
					fprintf(stderr, "volume keys: can't open %s: %s\n", path.c_str(), strerror(errno));
					refused = true;
				}
				else if (ioctl(fd, EVIOCGRAB, 1) < 0)
				{
					int e = errno;
					fprintf(stderr, "volume keys: can't take %s: %s%s\n", path.c_str(), strerror(e),
						e == EBUSY ? " (bananad has it: the keys stay BananaOS's)" : "");
					close(fd);
					fd = -1;
					refused = true;
				}
				else
					fprintf(stderr, "volume keys: %s taken: lid shut, Keep playing, something loaded; a press is one volume step, a 0.6 s hold next / previous\n",
						path.c_str());
			}
			int timeout = 100;
			// time a hold's 0.6 s (and a stuck key's 5 s) without waiting for another event
			for (KeyState* k : {&up, &downk})
			{
				if (fd < 0 || !k->downAt || k->acted) continue;
				long long left = holdMs - (now - k->downAt);
				if (left < timeout) timeout = left < 0 ? 0 : (int)left;
			}
			pollfd fds[2] = {{stopPipe[0], POLLIN, 0}, {fd, POLLIN, 0}};
			int n = poll(fds, fd >= 0 ? 2 : 1, timeout);
			if (n < 0)
			{
				if (errno == EINTR) continue;
				break;
			}
			if (fds[0].revents) break;
			if (fd >= 0 && fds[1].revents)
			{
				input_event ev[16];
				ssize_t r;
				while ((r = read(fd, ev, sizeof ev)) > 0)
					for (ssize_t i = 0; i < r / (ssize_t)sizeof *ev; i++)
					{
						if (ev[i].type != EV_KEY) continue;
						KeyState* k = ev[i].code == KEY_VOLUMEUP ? &up : ev[i].code == KEY_VOLUMEDOWN ? &downk : nullptr;
						if (!k) continue;
						bool isUp = ev[i].code == KEY_VOLUMEUP;
						if (ev[i].value == 1)
						{
							// (a second down without a release: the press starts again, as a key would)
							down(*k, isUp ? "Volume +" : "Volume -");
						}
						else if (ev[i].value == 0 && k->downAt)
						{
							long long took = monoMs() - k->downAt;
							k->downAt = 0;
							if (k->stuck) fprintf(stderr, "volume keys: %s let go (it was stuck)\n", isUp ? "Volume +" : "Volume -");
							else if (!k->acted && took < holdMs)
							{
								// a press: one volume step, now that the key is let go
								fprintf(stderr, "volume keys: %s press: one volume step (%lld ms)\n", isUp ? "Volume +" : "Volume -", took);
								pushAction(eventType, isUp ? VolumeUp : VolumeDown);
							}
							k->acted = k->stuck = false;
						}
					}
				if (r < 0 && errno != EAGAIN && errno != EINTR)
				{
					// the device went away: not tried again for this stretch
					ioctl(fd, EVIOCGRAB, 0);
					close(fd);
					fd = -1;
					refused = true;
					up = downk = KeyState();
					fprintf(stderr, "volume keys: the device went away\n");
				}
			}
			now = monoMs();
			for (KeyState* k : {&up, &downk})
			{
				if (fd < 0 || !k->downAt || k->acted) continue;
				bool isUp = k == &up;
				long long held = now - k->downAt;
				if (held >= stuckMs)
				{
					// a key held this long was in a pocket (or the grab found it down): ignored till let go
					k->acted = k->stuck = true;
					fprintf(stderr, "volume keys: %s down %lld ms: stuck, ignored until let go\n", isUp ? "Volume +" : "Volume -", held);
				}
				else if (held >= holdMs)
				{
					// a hold: next / previous, once, and the volume is not touched
					k->acted = true;
					fprintf(stderr, "volume keys: %s held %lld ms: %s\n", isUp ? "Volume +" : "Volume -", held, isUp ? "next" : "previous");
					pushAction(eventType, isUp ? Next : Previous);
				}
			}
		}
		release(fd, "the player quits");
#else
		(void)0;
#endif
	}
}
