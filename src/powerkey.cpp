#include "powerkey.h"
#include "lidsleep_host.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
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
	// the device BananaOS calls the power key (bananad finds it by name, never by number)
	static const char* const deviceName = "rk805 pwrkey";
	static constexpr long long holdMs = 2000;	// a press held this long sleeps instead of playing
	static constexpr long long beatLimitMs = 3000;	// a main loop silent this long (frozen): the grab goes

	// the first /dev/input/event* whose name is the power key's ("" none)
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
		fprintf(stderr, "power: released (%s)\n", why.c_str());
	}

	static void pushPress(uint32_t type)
	{
		SDL_Event ev{};
		ev.type = type;
		ev.user.code = PowerKey::Press;
		SDL_PushEvent(&ev);
	}
#endif

	void PowerKey::tick(bool want, const char* why)
	{
		if (!want && why) wantWhy = why;
		wantGrab = want;
		beat = monoMs();
	}

	bool PowerKey::start(uint32_t event)
	{
		eventType = event;
#ifdef __linux__
		if (pipe(stopPipe) < 0)
		{
			fprintf(stderr, "power: can't start: %s\n", strerror(errno));
			return false;
		}
		thread = std::thread([this] { run(); });
		return true;
#else
		return false;	// a Mac: the power key is the OS's (a stub that does nothing)
#endif
	}

	void PowerKey::stop()
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

	void PowerKey::run()
	{
#ifdef __linux__
		int fd = -1;
		bool refused = false;	// no device, or a grab that failed: off for this stretch
		bool slept = false;	// a hold's sleep: off until it wakes (bananad needs the key to wake)
		long long pressedAt = 0;	// the power key down since (0: up)
		bool held = false;	// its 2 s reached for this press
		long long lastLoop = monoMs();
		for (;;)
		{
			if (quitting.load()) break;
			long long now = monoMs();
			// a gap in this thread's own clock and in main's beat: the process was stopped (the sleep a hold asked
			// for) and is back. The grab can go on again; it was let go first because a held fd would have hidden
			// the wake key. A stall of this thread alone leaves main's beat fresh, so it isn't taken for a wake
			if (slept && now - lastLoop > 1000 && now - beat.load() > 1000)
			{
				slept = false;
				fprintf(stderr, "power: back from the sleep: the key can be taken again\n");
			}
			lastLoop = now;
			bool want = wantGrab.load();
			if (!want)
			{
				// the lid opened, playback stopped or the file ended, the setting went off
				release(fd, wantWhy.load());
				refused = slept = held = false;
				pressedAt = 0;
			}
			else if (now - beat.load() > beatLimitMs)
			{
				// the main loop stopped coming round: a frozen player must not keep the key from bananad
				release(fd, "the player stopped answering");
				pressedAt = 0;
				held = false;
			}
			else if (fd < 0 && !refused && !slept)
			{
				std::string path = findDevice();
				if (path.empty())
				{
					fprintf(stderr, "power: no %s in /dev/input: the power key stays as it is\n", deviceName);
					refused = true;
				}
				else if ((fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)) < 0)
				{
					fprintf(stderr, "power: can't open %s: %s\n", path.c_str(), strerror(errno));
					refused = true;
				}
				else if (ioctl(fd, EVIOCGRAB, 1) < 0)
				{
					int e = errno;
					// EBUSY: bananad holds it exclusively (and has no setting for it): leave it alone, quietly
					fprintf(stderr, "power: can't take %s: %s%s\n", path.c_str(), strerror(e),
						e == EBUSY ? " (bananad has it: the power key stays as it is)" : "");
					close(fd);
					fd = -1;
					refused = true;
				}
				else
					fprintf(stderr, "power: %s taken: lid shut, Keep playing, something loaded; press is play/pause, a 2 s hold sleeps\n",
						path.c_str());
			}
			// its events, or the 2 s of a press held (the timeout wakes in time for that)
			int timeout = 100;
			if (fd >= 0 && pressedAt && !held)
			{
				long long left = holdMs - (monoMs() - pressedAt);
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
			bool holdNow = false;
			if (fd >= 0 && fds[1].revents)
			{
				input_event ev[16];
				ssize_t r;
				while ((r = read(fd, ev, sizeof ev)) > 0)
					for (ssize_t i = 0; i < r / (ssize_t)sizeof *ev; i++)
					{
						if (ev[i].type != EV_KEY || ev[i].code != KEY_POWER) continue;
						if (ev[i].value == 1)
						{
							pressedAt = monoMs();
							held = false;
						}
						else if (ev[i].value == 0 && pressedAt && !held)
						{
							// let go: play/pause if that was before 2 s, through the session as A does it; at 2 s
							// or after, the hold (its release can come in the same read as the deadline)
							long long took = monoMs() - pressedAt;
							pressedAt = 0;
							if (took >= holdMs) holdNow = true;
							// the conditions held until now (not a press that came with the lid already open)
							else if (wantGrab.load()) pushPress(eventType);
						}
					}
				if (r < 0 && errno != EAGAIN && errno != EINTR)
				{
					// the device went away: not tried again for this stretch
					ioctl(fd, EVIOCGRAB, 0);
					close(fd);
					fd = -1;
					refused = true;
					pressedAt = 0;
					fprintf(stderr, "power: the device went away\n");
				}
			}
			if (fd >= 0 && !holdNow && pressedAt && !held && monoMs() - pressedAt >= holdMs) holdNow = true;
			if (fd >= 0 && holdNow)
			{
				// held: the sleep the power key would bring. The grab goes first, so bananad reads the key again
				// while the handheld sleeps and the same key wakes it
				held = true;
				pressedAt = 0;
				fprintf(stderr, "power: hold: asking bananad to sleep\n");
				release(fd, "the 2 s hold: the sleep asked for");
				if (!askBananadSleep()) fprintf(stderr, "power: can't ask bananad to sleep\n");
				slept = true;
			}
		}
		release(fd, "the player quits");
#endif
	}
}
