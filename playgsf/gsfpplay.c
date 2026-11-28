/* OpenCP Module Player
 * GSF player interface (mGBA). Based on the GME player interface by Stian Skjelstad.
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
#include "gsfemu.h"
#include "gsfplay.h"
#include "gsftype.h"
#include "../playgme/gmeplay.h"

OCP_INTERNAL const char *gsf_filename;
static struct ocpfilehandle_t *gsf_file;

static void gsfCloseFile (struct cpifaceSessionAPI_t *cpifaceSession)
{
	gsfInfoDone (cpifaceSession);
	if (gsf_file)
	{
		gsf_file->unref (gsf_file);
		gsf_file = 0;
		gsf_filename = 0;
	}
	gsfClosePlayer (cpifaceSession);
}

static int gsfLooped (struct cpifaceSessionAPI_t *cpifaceSession, int LoopMod)
{
	gsfSetLoop (LoopMod);
	gsfIdle (cpifaceSession);
	return (!LoopMod) && gsfIsLooped();
}

static void gsfDrawGStrings (struct cpifaceSessionAPI_t *cpifaceSession)
{
	cpifaceSession->drawHelperAPI->GStringsSongXofY (cpifaceSession, 1, 1);
}

static int gsfProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('p', "Start/stop pause with fade");
			cpifaceSession->KeyHelp ('P', "Start/stop pause with fade");
			cpifaceSession->KeyHelp (KEY_CTRL_HOME, "Restart song");
			cpifaceSession->KeyHelp (KEY_CTRL_P, "Start/stop pause");
			return 0;
		case 'p': case 'P':
			cpifaceSession->TogglePauseFade (cpifaceSession);
			break;
		case KEY_CTRL_P:
			cpifaceSession->TogglePause (cpifaceSession);
			break;
		case KEY_CTRL_HOME:
			gsfRestart ();
			cpifaceSession->ResetSongTimer (cpifaceSession);
			break;
		default:
			return 0;
	}
	return 1;
}

static int gsfOpenFile (struct cpifaceSessionAPI_t *cpifaceSession, struct moduleinfostruct *info, struct ocpfilehandle_t *file)
{
	int retval;

	if (!file)
		return -1;

	cpifaceSession->dirdb->GetName_internalstr (file->dirdb_ref, &gsf_filename);
	cpifaceSession->cpiDebug (cpifaceSession, "[GSF] loading %s\n", gsf_filename);

	cpifaceSession->IsEnd = gsfLooped;
	cpifaceSession->ProcessKey = gsfProcessKey;
	cpifaceSession->DrawGStrings = gsfDrawGStrings;

	if ((retval = gsfOpenPlayer(file, cpifaceSession)))
	{
		return retval;
	}

	gsfInfoInit (cpifaceSession);

	/* the keys 1 to 0 mute the channels: the sound channels of the Game Boy Advance
	 * (see gsfemu.h), or, with the tracker view, the tracks of the song then the 4
	 * Game Boy channels (see gsfvis.h) */
	cpifaceSession->LogicalChannelCount = gsfChannelCount ();
	cpifaceSession->SetMuteChannel = gsfSetMute;
	if (gmeVisActive ())
	{
		cpifaceSession->TrackSetup2 (cpifaceSession, &gmeTrakDisplay, GME_TRAK_PATTERNS, gsfChannelCount ());
		cpifaceSession->TrackStretch (cpifaceSession, 1);
	}

	cpifaceSession->InPause = 0;

	gsf_file = file;
	file->ref (file);

	return errOk;
}

static int gsfPluginInit (struct PluginInitAPI_t *API)
{
	return gsf_type_init (API);
}

static void gsfPluginClose (struct PluginCloseAPI_t *API)
{
	gsf_type_done (API);
}

OCP_INTERNAL const struct cpifaceplayerstruct gsfPlayer = {"[GSF plugin, mGBA]", gsfOpenFile, gsfCloseFile};
#ifdef _WIN32
/* the plugin links mGBA statically and is linked with --exclude-all-symbols, so export this one explicitly */
__declspec(dllexport)
#endif
DLLEXTINFO_PLAYBACK_PREFIX struct linkinfostruct dllextinfo = {.name = "playgsf", .desc = "OpenCP GSF Player (mGBA by Jeffrey Pfau)", .ver = DLLVERSION, .sortindex = 95, .PluginInit = gsfPluginInit, .PluginClose = gsfPluginClose};
