#include "writequeue.h"
#include "filesave.h"
#include <chrono>
#include <utility>

namespace bp
{
	void DurableWriter::write(std::vector<FileWrite> files)
	{
		if (files.empty()) return;
		{
			std::lock_guard<std::mutex> g(m_);
			if (stopping_) return;
			slot_.push(std::move(files));
			if (!started_)
			{
				started_ = true;
				// detached at once: the object is never destroyed, and a std::thread that outlives it must never be
				// joined on main's path
				thread_ = std::thread([this] { run(); });
				thread_.detach();
			}
		}
		cv_.notify_one();
	}

	void DurableWriter::run()
	{
		std::vector<FileWrite> files;
		for (;;)
		{
			{
				std::unique_lock<std::mutex> g(m_);
				cv_.wait(g, [&] { return stopping_ || slot_.full(); });
				if (!slot_.full()) return;	// (stopping with nothing left to write)
				slot_.take(files);
				busy_ = true;
			}
			for (auto& f : files)
				writeFileDurable(f.path, f.text, f.mode);
			files.clear();
			{
				std::lock_guard<std::mutex> g(m_);
				busy_ = false;
				if (stopping_ && !slot_.full()) { cv_.notify_all(); return; }
			}
			cv_.notify_all();
		}
	}

	bool DurableWriter::flush(unsigned timeoutMs)
	{
		std::unique_lock<std::mutex> g(m_);
		if (!started_) return true;
		return cv_.wait_for(g, std::chrono::milliseconds(timeoutMs), [&] { return !busy_ && !slot_.full(); });
	}

	void DurableWriter::stop()
	{
		{
			std::lock_guard<std::mutex> g(m_);
			stopping_ = true;
		}
		cv_.notify_one();
	}
}
