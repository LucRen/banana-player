#include "lidtrigger.h"
#include "settings.h"

namespace bp
{
	IncognitoLidAction incognitoLidAction(bool lidJustClosed, bool incognitoOn, int mode)
	{
		// a wake (SIGCONT), a duplicate event or main's follow-up after the lid opened again is not a close
		if (!lidJustClosed) return IncognitoLidAction::NotInCharge;
		// the setting is read only while incognito is on: it is never saved, and an off incognito means nothing
		// closes, whatever the row last said
		if (!incognitoOn) return IncognitoLidAction::NotInCharge;
		if (mode == LID_INCOGNITO_CLOSE_ALL) return IncognitoLidAction::CloseAll;
		if (mode == LID_INCOGNITO_QUIT) return IncognitoLidAction::Quit;
		return IncognitoLidAction::NotInCharge;	// Nothing
	}
}
