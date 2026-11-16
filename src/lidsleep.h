#pragma once
#include <string>
#include <vector>

namespace bp
{
	// "When the lid closes: Keep playing" (docs/notes/2026-11-16-lid-and-unplug.md). With BananaOS's `lid_sleep = yes`,
	// bananad suspends at once when the lid shuts, whatever runs. So while something plays, the player turns that line
	// of /data/banana/bananad.conf to `no` and puts it back after, keeping every other byte of the file. These are the
	// pure parts: reading the conf as bananad does, the rewrite, the note that says how to undo it, and the undo.
	// packaging/launch.sh's lid_restore does the same undo in shell (after a crash): keep the two in step.

	// `lid_sleep` from a conf's text, read as bananad's load_conf reads it: a line whose first byte is '#' or that has no
	// '=' is skipped, key and value trimmed, "yes" or "1" is true, anything else false; the last line wins.
	// found false: no such line
	bool lidSleepIn(const std::string& conf, bool& found);
	// what bananad does: on by default, then /etc/banana/bananad.conf, then /data/banana/bananad.conf
	bool lidSleepEffective(const std::string& etcConf, const std::string& dataConf);

	// what the player changed, kept in the state folder as "lid-restore" until it's undone
	struct LidNote
	{
		std::string mode;	// "changed" (a line rewritten), "added" (a line appended), "created" (the file was absent)
		std::string conf;	// the file's path
		std::string was;	// changed: the line as it was
		std::string wrote;	// the line the player wrote
		bool nl = false;	// added: the file didn't end in a newline, so one went in before the line
	};
	std::string lidNoteText(const LidNote& n);
	bool parseLidNote(const std::string& text, LidNote& n);

	// the override's text with the effective lid_sleep line made `lid_sleep = no` (the last one rewritten, or one
	// appended); exists: whether the file is there. The note says how to undo it (its conf is left for the caller)
	std::string lidSleepOff(const std::string& data, bool exists, LidNote& note);

	enum class LidRestore
	{
		Write,	// write `out` over the file
		Remove,	// the player made the file and nothing else is in it: remove it
		Skip,	// the line no longer says what the player wrote (BananaOS's Settings changed it, or the file went): leave it
	};
	LidRestore lidRestore(const std::string& data, bool exists, const LidNote& note, std::string& out);

	// the USB sound cards' numbers in /proc/asound/cards (" 1 [Device ]: USB-Audio - USB Audio Device")
	std::vector<int> usbSoundCards(const std::string& procCards);
}
