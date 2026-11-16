#include "sleepwatch.h"
#include "lidsleep.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <string>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <cstring>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

namespace bp
{
	// written by the SIGCONT handler (a write is async-signal-safe), read by the watch's thread
	static int contPipe[2] = {-1, -1};

	static void onCont(int)
	{
		int saved = errno;
		char c = 'c';
		if (write(contPipe[1], &c, 1) < 0) {}
		errno = saved;
	}

	static void cloexecNonblock(int fd)
	{
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	}

#ifdef __linux__
	static bool hasBit(const unsigned long* bits, int bit)
	{
		return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1;
	}

	// the input devices with a lid or headphone switch: the hall sensor's gpio-keys and the codec's jack on the RG DS
	// (found by what they can report, as bananad finds them, not by name)
	static std::vector<int> openSwitches()
	{
		std::vector<int> out;
		DIR* d = opendir("/dev/input");
		if (!d) return out;
		while (dirent* e = readdir(d))
		{
			if (strncmp(e->d_name, "event", 5)) continue;
			std::string path = std::string("/dev/input/") + e->d_name;
			int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;
			unsigned long sw[SW_MAX / (8 * sizeof(long)) + 1] = {};
			ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw);
			if (hasBit(sw, SW_LID) || hasBit(sw, SW_HEADPHONE_INSERT)) out.push_back(fd);
			else close(fd);
		}
		closedir(d);
		return out;
	}

	// whether any of the devices can report the switch
	static bool switchKnown(const std::vector<int>& fds, int bit)
	{
		for (int fd : fds)
		{
			unsigned long sw[SW_MAX / (8 * sizeof(long)) + 1] = {};
			if (ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw) >= 0 && hasBit(sw, bit)) return true;
		}
		return false;
	}

	// a switch's state now (EVIOCGSW), on any of the devices
	static bool switchOn(const std::vector<int>& fds, int bit)
	{
		for (int fd : fds)
		{
			unsigned long sw[SW_MAX / (8 * sizeof(long)) + 1] = {};
			if (ioctl(fd, EVIOCGSW(sizeof sw), sw) >= 0 && hasBit(sw, bit)) return true;
		}
		return false;
	}
#endif

	static std::string runDir()
	{
		const char* e = getenv("BP_RUN_DIR");
		return e && *e ? e : "/run/banana";
	}

	static std::string slurp(const char* path)
	{
		std::string out;
		if (FILE* f = fopen(path, "r"))
		{
			char buf[1024];
			size_t n;
			while ((n = fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
			fclose(f);
		}
		return out;
	}

	// the Bluetooth headphones and the USB sound cards: what's there now against what was (first: only taken)
	void SleepWatch::lookAtSound(bool first)
	{
		// connected: the bluetooth script's bt/audio, or BananaOS's route still through bluez-alsa. Both are files written
		// again from time to time (bt/audio replaced, the route renamed over), and a look between the two writes saw the
		// headphones gone while they played on: gone only when neither says so at two looks in a row (a second apart)
		struct stat st;
		bool bt = stat((runDir() + "/bt/audio").c_str(), &st) == 0 ||
				  slurp((runDir() + "/asound.conf").c_str()).find("bluealsa") != std::string::npos;
		btGone = bt ? 0 : btGone + 1;
		if (!first && btIn && btGone == 2 && onUnplug) onUnplug("Bluetooth headphones disconnected");
		if (bt) btIn = true;
		else if (btGone >= 2) btIn = false;
		const char* cardsPath = getenv("BP_ASOUND_CARDS");
		std::vector<int> usb = usbSoundCards(slurp(cardsPath ? cardsPath : "/proc/asound/cards"));
		if (!first && onUnplug)
			for (int c : usbCards)
				if (std::find(usb.begin(), usb.end(), c) == usb.end())
				{
					onUnplug("USB sound card unplugged");
					break;
				}
		usbCards = usb;
	}

	bool SleepWatch::start(std::function<void(bool)> cb, std::function<void()> awake, std::function<void(const std::string&)> unplug)
	{
		onSleep = std::move(cb);
		onAwake = std::move(awake);
		onUnplug = std::move(unplug);
		if (pipe(stopPipe) < 0) return false;
		if (contPipe[0] < 0)
		{
			if (pipe(contPipe) < 0) return false;
			cloexecNonblock(contPipe[0]);
			cloexecNonblock(contPipe[1]);
		}
		struct sigaction sa = {};
		sa.sa_handler = onCont;
		sa.sa_flags = SA_RESTART;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGCONT, &sa, nullptr);
#ifdef __linux__
		swFds = openSwitches();
		if (!switchKnown(swFds, SW_LID)) fprintf(stderr, "no lid switch found: only SIGCONT pauses after a sleep\n");
		if (!switchKnown(swFds, SW_HEADPHONE_INSERT)) fprintf(stderr, "no headphone switch found: pulling them out doesn't pause\n");
		lidShut = switchOn(swFds, SW_LID);
		jackIn = switchOn(swFds, SW_HEADPHONE_INSERT);
#endif
		lookAtSound(true);
		thread = std::thread([this] { run(); });
		return true;
	}

	void SleepWatch::stop()
	{
		if (!thread.joinable()) return;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		signal(SIGCONT, SIG_DFL);
		close(stopPipe[0]);
		close(stopPipe[1]);
		for (int fd : swFds) close(fd);
		swFds.clear();
	}

	static long long monoMs()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
	}

	void SleepWatch::run()
	{
		// the lid closed and no SIGCONT since: when it's taken as never stopped (monoMs; 0 none)
		long long awakeAt = 0;
		long long soundAt = monoMs() + 1000;	// the next look at Bluetooth and USB
		for (;;)
		{
			std::vector<pollfd> fds = {{stopPipe[0], POLLIN, 0}, {contPipe[0], POLLIN, 0}};
			for (int fd : swFds) fds.push_back({fd, POLLIN, 0});
			long long next = soundAt;
			if (awakeAt && awakeAt < next) next = awakeAt;
			int timeout = (int)std::max(0LL, next - monoMs());
			int n = poll(fds.data(), fds.size(), timeout);
			if (n < 0)
			{
				if (errno == EINTR) continue;	// SIGCONT itself lands here too; its byte is in the pipe
				return;
			}
			if (n > 0 && fds[0].revents) return;
			bool lid = false, cont = false, jackOut = false;
			if (n > 0 && fds[1].revents)
			{
				char buf[16];
				while (read(contPipe[0], buf, sizeof buf) > 0) {}
				cont = true;
			}
#ifdef __linux__
			for (size_t k = 2; n > 0 && k < fds.size(); k++)
			{
				if (!fds[k].revents) continue;
				int fd = fds[k].fd;
				input_event ev[16];
				ssize_t r;
				while ((r = read(fd, ev, sizeof ev)) > 0)
					for (size_t i = 0; i < r / sizeof *ev; i++)
					{
						if (ev[i].type != EV_SW) continue;
						if (ev[i].code == SW_LID)
						{
							lidShut = ev[i].value != 0;
							if (ev[i].value == 1) lid = true;
						}
						else if (ev[i].code == SW_HEADPHONE_INSERT)
						{
							if (jackIn && ev[i].value == 0) jackOut = true;
							jackIn = ev[i].value != 0;
						}
					}
				if (r < 0 && errno != EAGAIN)
				{
					close(fd);	// the device went away: SIGCONT still covers the sleeps
					swFds.erase(std::find(swFds.begin(), swFds.end(), fd));
				}
			}
#endif
			if (jackOut && onUnplug) onUnplug("headphones out");
			if ((lid || cont) && onSleep) onSleep(lid && !cont);
			if (cont)
			{
				// back from a stop (a timer of the lid's with it: it was a sleep)
				awakeAt = 0;
				if (onAwake) onAwake();
#ifdef __linux__
				// the lid and the jack may have moved while stopped, their events read before the stop or not at all
				lidShut = switchOn(swFds, SW_LID);
				jackIn = switchOn(swFds, SW_HEADPHONE_INSERT);
#endif
				// what went while asleep isn't an unplug to pause for (the wake pauses anyway): taken as it is now
				lookAtSound(true);
				soundAt = monoMs() + 1000;
			}
			else if (lid) awakeAt = monoMs() + 1500;
			else if (awakeAt && monoMs() >= awakeAt)
			{
				// never stopped: closing the lid only darkened the panels (lid sleep off)
				awakeAt = 0;
				if (onAwake) onAwake();
			}
			if (monoMs() >= soundAt)
			{
				lookAtSound(false);
				soundAt = monoMs() + 1000;
			}
		}
	}
}
