#pragma once
// Whole files written off the main thread. writeFileDurable's fsync on an SD card is tens of ms (filesave.h), so a
// file rewritten as the user reads or plays must not pay it on the thread that draws the panels.
//
// The main thread copies what to write (a path and a small text) into WriteSlot: one slot, the latest push wins. A
// write already in flight finishes, then the thread takes whatever is in the slot, so a burst of saves costs one
// write and the card always ends with the newest state. DurableWriter owns the thread; like the other workers it is
// made on the heap and never destroyed (its thread may be inside an fsync at exit), and it never touches Player or Ui.
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bp
{
	// One file: a path, its bytes, and the mode a new file gets (0600 for sealed secrets)
	struct FileWrite
	{
		std::string path, text;
		unsigned mode = 0644;
	};

	// The slot itself: pure (no thread, no clock), so the latest-wins rule is tested on its own.
	class WriteSlot
	{
	public:
		// what to write; replaces anything pushed before and not taken yet
		void push(std::vector<FileWrite> files)
		{
			files_ = std::move(files);
			full_ = true;
		}
		// the files in the slot, or false when there are none
		bool take(std::vector<FileWrite>& out)
		{
			if (!full_) return false;
			out = std::move(files_);
			files_.clear();
			full_ = false;
			return true;
		}
		bool full() const { return full_; }

	private:
		std::vector<FileWrite> files_;
		bool full_ = false;
	};

	// One thread, one write at a time, the newest state last. The thread starts on the first write and waits for ever
	// after stop(). Never destroyed while a write could be in flight (no join in a destructor on the main path).
	class DurableWriter
	{
	public:
		// main thread: copy the files into the slot (the write itself happens on the thread)
		void write(std::vector<FileWrite> files);
		// wait until nothing is queued or being written, at most timeoutMs; false on timeout
		bool flush(unsigned timeoutMs);
		// no more writes: the thread finishes one in flight, then waits for ever
		void stop();

	private:
		void run();
		std::thread thread_;
		std::mutex m_;
		std::condition_variable cv_;
		WriteSlot slot_;
		bool started_ = false, busy_ = false, stopping_ = false;
	};
}
