#include "filesave.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <cstdio>
#include <fstream>
#include <unistd.h>

namespace bp
{
	bool writeFileAtomic(const std::string& path, const std::string& text)
	{
		std::string tmp = path + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
			if (!f) return false;
			f.write(text.data(), std::streamsize(text.size()));
			f.flush();
			if (!f) { unlink(tmp.c_str()); return false; }
		}
		if (rename(tmp.c_str(), path.c_str()) != 0)
		{
			unlink(tmp.c_str());
			return false;
		}
		return true;
	}

	bool writeFileDurable(const std::string& path, const std::string& text, unsigned mode)
	{
		// as writeFileAtomic, and on the card before it returns: the file's bytes synced before the rename, the folder
		// after it, so a reset (a battery pulled, a crash of the whole handheld) finds the old file or the new one whole
		std::string tmp = path + ".tmp";
		int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
		if (fd < 0) return false;
		::fchmod(fd, mode);	// (a .tmp left by a cut keeps the mode it had)
		size_t off = 0;
		while (off < text.size())
		{
			ssize_t n = ::write(fd, text.data() + off, text.size() - off);
			if (n <= 0) { ::close(fd); unlink(tmp.c_str()); return false; }
			off += (size_t)n;
		}
		bool ok = ::fsync(fd) == 0;
		if (::close(fd) != 0) ok = false;
		if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
		{
			unlink(tmp.c_str());
			return false;
		}
		size_t slash = path.rfind('/');
		std::string dir = slash == std::string::npos ? "." : slash == 0 ? "/" : path.substr(0, slash);
		int dfd = ::open(dir.c_str(), O_RDONLY);
		if (dfd >= 0)
		{
			::fsync(dfd);
			::close(dfd);
		}
		return true;
	}
}
