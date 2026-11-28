/* OpenCP Module Player
 * vgmstream player interface. Based on the GME player interface by Stian Skjelstad.
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
#include "stuff/err.h"
#include "stuff/poutput.h"
#include "vgmsplay.h"
#include "vgmstype.h"

OCP_INTERNAL const char *vgms_filename;

static void vgmsCloseFile (struct cpifaceSessionAPI_t *cpifaceSession)
{
	vgmsInfoDone (cpifaceSession);
	vgmsClosePlayer (cpifaceSession);
	vgms_filename = 0;
}

static int vgmsLooped (struct cpifaceSessionAPI_t *cpifaceSession, int LoopMod)
{
	vgmsSetLoop (LoopMod);
	vgmsIdle (cpifaceSession);
	return (!LoopMod) && vgmsIsLooped();
}

static void vgmsDrawGStrings (struct cpifaceSessionAPI_t *cpifaceSession)
{
	struct vgmsinfo_t info;
	vgmsGetInfo (&info);
	cpifaceSession->drawHelperAPI->GStringsSongXofY (cpifaceSession, info.subsong, info.subsongs);
}

/* subsongs are separate streams of the same file (e.g. the songs of a sound bank) */
static void vgmsSubsong (struct cpifaceSessionAPI_t *cpifaceSession, int delta)
{
	struct vgmsinfo_t info;
	vgmsGetInfo (&info);
	if (!vgmsStartSubsong (cpifaceSession, info.subsong + delta))
	{
		cpifaceSession->ResetSongTimer (cpifaceSession);
	}
}

static int vgmsProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('p', "Start/stop pause with fade");
			cpifaceSession->KeyHelp ('P', "Start/stop pause with fade");
			cpifaceSession->KeyHelp (KEY_CTRL_P, "Start/stop pause");
			cpifaceSession->KeyHelp (KEY_CTRL_HOME, "Restart song");
			cpifaceSession->KeyHelp ('<', "Jump to previous subsong");
			cpifaceSession->KeyHelp (KEY_CTRL_LEFT, "Jump to previous subsong");
			cpifaceSession->KeyHelp ('>', "Jump to next subsong");
			cpifaceSession->KeyHelp (KEY_CTRL_RIGHT, "Jump to next subsong");
			return 0;
		case 'p': case 'P':
			cpifaceSession->TogglePauseFade (cpifaceSession);
			break;
		case KEY_CTRL_P:
			cpifaceSession->TogglePause (cpifaceSession);
			break;
		case KEY_CTRL_HOME:
			vgmsSubsong (cpifaceSession, 0);
			break;
		case '<':
		case KEY_CTRL_LEFT:
			vgmsSubsong (cpifaceSession, -1);
			break;
		case '>':
		case KEY_CTRL_RIGHT:
			vgmsSubsong (cpifaceSession, 1);
			break;
		default:
			return 0;
	}
	return 1;
}

static int vgmsOpenFile (struct cpifaceSessionAPI_t *cpifaceSession, struct moduleinfostruct *info, struct ocpfilehandle_t *file)
{
	int retval;

	if (!file)
		return -1;

	cpifaceSession->dirdb->GetName_internalstr (file->dirdb_ref, &vgms_filename);
	cpifaceSession->cpiDebug (cpifaceSession, "[vgmstream] loading %s\n", vgms_filename);

	cpifaceSession->IsEnd = vgmsLooped;
	cpifaceSession->ProcessKey = vgmsProcessKey;
	cpifaceSession->DrawGStrings = vgmsDrawGStrings;

	if ((retval = vgmsOpenPlayer(file, cpifaceSession)))
	{
		return retval;
	}

	vgmsInfoInit (cpifaceSession);
	cpifaceSession->InPause = 0;

	return errOk;
}

static int vgmsPluginInit (struct PluginInitAPI_t *API)
{
	return vgms_type_init (API);
}

static void vgmsPluginClose (struct PluginCloseAPI_t *API)
{
	vgms_type_done (API);
}

OCP_INTERNAL const struct cpifaceplayerstruct vgmsPlayer = {"[vgmstream plugin]", vgmsOpenFile, vgmsCloseFile};
#ifdef _WIN32
/* the plugin links vgmstream statically and is linked with --exclude-all-symbols, so export this one explicitly */
__declspec(dllexport)
#endif
DLLEXTINFO_PLAYBACK_PREFIX struct linkinfostruct dllextinfo = {.name = "playvgms", .desc = "OpenCP vgmstream Player (game audio streams)", .ver = DLLVERSION, .sortindex = 95, .PluginInit = vgmsPluginInit, .PluginClose = vgmsPluginClose};
