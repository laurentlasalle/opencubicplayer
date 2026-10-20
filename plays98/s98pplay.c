/* OpenCP Module Player
 * S98 player interface (libvgm). Based on the GME player interface by Stian Skjelstad.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "s98play.h"
#include "../playgme/gmeplay.h"
#include "s98type.h"
#include "boot/plinkman.h"
#include "cpiface/cpiface.h"
#include "dev/player.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "filesel/mdb.h"
#include "filesel/pfilesel.h"
#include "stuff/compat.h"
#include "stuff/err.h"
#include "stuff/poutput.h"
#include "stuff/sets.h"

static const char *s98_filename;
static struct ocpfilehandle_t *s98_file;

static void s98CloseFile (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (s98_file)
	{
		s98_file->unref (s98_file);
		s98_file = 0;
		s98_filename = 0;
	}
	s98ClosePlayer (cpifaceSession);
}

static int s98Looped (struct cpifaceSessionAPI_t *cpifaceSession, int LoopMod)
{
	s98SetLoop (LoopMod);
	s98Idle (cpifaceSession);
	return (!LoopMod) && s98IsLooped();
}

static void s98DrawGStrings (struct cpifaceSessionAPI_t *cpifaceSession)
{
	cpifaceSession->drawHelperAPI->GStringsSongXofY (cpifaceSession, 1, 1);
}

static int s98ProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('p', "Start/stop pause with fade");
			cpifaceSession->KeyHelp ('P', "Start/stop pause with fade");
			cpifaceSession->KeyHelp (KEY_CTRL_HOME, "Restart Song");
			cpifaceSession->KeyHelp (KEY_CTRL_P, "Start/stop pause");
			return 0;
		case 'p': case 'P':
			cpifaceSession->TogglePauseFade (cpifaceSession);
			break;
		case KEY_CTRL_P:
			cpifaceSession->TogglePause (cpifaceSession);
			break;
		case KEY_CTRL_HOME:
			s98Restart ();
			cpifaceSession->ResetSongTimer (cpifaceSession);
			break;
		default:
			return 0;
	}
	return 1;
}

static int s98OpenFile (struct cpifaceSessionAPI_t *cpifaceSession, struct moduleinfostruct *info, struct ocpfilehandle_t *file)
{
	int retval;

	if (!file)
		return -1;

	cpifaceSession->dirdb->GetName_internalstr (file->dirdb_ref, &s98_filename);
	cpifaceSession->cpiDebug (cpifaceSession, "[S98] loading %s...\n", s98_filename);

	cpifaceSession->IsEnd = s98Looped;
	cpifaceSession->ProcessKey = s98ProcessKey;
	cpifaceSession->DrawGStrings = s98DrawGStrings;

	if ((retval = s98OpenPlayer(file, cpifaceSession)))
	{
		return retval;
	}

	cpifaceSession->LogicalChannelCount = s98ChannelCount ();
	cpifaceSession->SetMuteChannel = s98SetMute;
	if (s98ChannelCount ())
	{
		cpifaceSession->TrackSetup2 (cpifaceSession, &gmeTrakDisplay, GME_TRAK_PATTERNS, s98ChannelCount ());
		cpifaceSession->TrackStretch (cpifaceSession, 1);
	}

	cpifaceSession->InPause = 0;

	s98_file = file;
	file->ref (file);

	return errOk;
}

static int s98PluginInit (struct PluginInitAPI_t *API)
{
	return s98_type_init (API);
}

static void s98PluginClose (struct PluginCloseAPI_t *API)
{
	s98_type_done (API);
}

OCP_INTERNAL const struct cpifaceplayerstruct s98Player = {"[S98 plugin, libvgm]", s98OpenFile, s98CloseFile};
#ifdef _WIN32
/* the plugin links libvgm statically and is linked with --exclude-all-symbols, so export this one explicitly */
__declspec(dllexport)
#endif
DLLEXTINFO_PLAYBACK_PREFIX struct linkinfostruct dllextinfo = {.name = "plays98", .desc = "OpenCP S98 Player (libvgm by Valley Bell)", .ver = DLLVERSION, .sortindex = 95, .PluginInit = s98PluginInit, .PluginClose = s98PluginClose};
