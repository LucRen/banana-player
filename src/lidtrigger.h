#pragma once

namespace bp
{
	// What a lid close does when incognito is in charge of the lid and sleep (main's SleepWatch callback and its
	// main-thread follow-up). Pure so the rule has a test of its own: only a real open -> closed transition (the
	// switch, read by SleepWatch, not a SIGCONT, a busy loop or a stale event) with incognito on *at that moment*
	// and a setting other than Nothing is a trigger. Everything else is NotInCharge: the lid does what it would
	// with incognito off, and nothing is closed.
	enum class IncognitoLidAction
	{
		NotInCharge,	// no trigger at all (not a lid close, incognito off, or Nothing)
		CloseAll,
		Quit,
	};
	// mode: Settings::LidIncognito (0 Nothing, 1 Close all, 2 Quit)
	IncognitoLidAction incognitoLidAction(bool lidJustClosed, bool incognitoOn, int mode);
}
