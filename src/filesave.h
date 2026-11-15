#pragma once
#include <string>

// Whole-file saves. Both write `path + ".tmp"` and rename it over `path`, so a reader (or a crash of this process)
// finds the old file or the new one, never half of one. They differ in what a power cut does:
//
//   writeFileAtomic   no sync. For what is rebuilt when lost (caches, thumbnails, the crash log). Cheap.
//   writeFileDurable  fsync of the file before the rename and of the folder after it. For everything the user would
//                     miss (settings, places, playlists, servers, keys, progress). On exFAT a power cut after an
//                     unsynced rename can leave the name pointing at clusters never written; an fsync on an SD
//                     card can take tens of ms, so call it from a loop that runs it once a second at most
//                     (Player::writeFile coalesces for the main thread).
//
// Both return false on failure and leave no .tmp behind.
namespace bp
{
	bool writeFileAtomic(const std::string& path, const std::string& text);
	bool writeFileDurable(const std::string& path, const std::string& text, unsigned mode = 0644);	// mode: 0600 for sealed secrets
}
