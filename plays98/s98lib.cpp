/* OpenCP Module Player
 * S98 playback through libvgm (https://github.com/ValleyBell/libvgm) - thin C wrapper
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <vector>
#include <vgm/player/playera.hpp>
#include <vgm/player/s98player.hpp>
#include <vgm/utils/DataLoader.h>
#include <vgm/utils/MemoryLoader.h>
#include <vgm/emu/SoundEmu.h>
#include "s98lib.h"

struct s98lib_t
{
	PlayerA player;
	DATA_LOADER *loader;
	uint8_t *data;
	uint32_t rate;
	int loops;
	int fade;
};

extern "C" struct s98lib_t *s98lib_open (const uint8_t *data, uint32_t len, uint32_t rate, uint32_t buflen)
{
	s98lib_t *s = new s98lib_t;
	s->rate = rate;
	s->loops = 2;
	s->fade = 8;
	s->data = (uint8_t *)malloc (len); /* the loader keeps a pointer to the data, so keep our own copy */
	if (!s->data)
	{
		delete s;
		return 0;
	}
	memcpy (s->data, data, len);

	s->player.RegisterPlayerEngine (new S98Player);
	if (s->player.SetOutputSettings (rate, 2, 16, buflen))
	{
		free (s->data);
		delete s;
		return 0;
	}
	{
		PlayerA::Config c = s->player.GetConfiguration ();
		c.masterVol = 0x10000;
		c.loopCount = s->loops;
		c.fadeSmpls = rate * s->fade;
		c.endSilenceSmpls = rate / 2;
		c.pbSpeed = 1.0;
		s->player.SetConfiguration (c);
	}
	s->loader = MemoryLoader_Init (s->data, len);
	if (!s->loader)
	{
		free (s->data);
		delete s;
		return 0;
	}
	DataLoader_SetPreloadBytes (s->loader, 0x100);
	if (DataLoader_Load (s->loader) || s->player.LoadFile (s->loader))
	{
		DataLoader_Deinit (s->loader);
		free (s->data);
		delete s;
		return 0;
	}
	if (s->player.Start ())
	{
		s->player.UnloadFile ();
		DataLoader_Deinit (s->loader);
		free (s->data);
		delete s;
		return 0;
	}
	return s;
}

extern "C" void s98lib_close (struct s98lib_t *s)
{
	if (!s)
	{
		return;
	}
	s->player.Stop ();
	s->player.UnloadFile ();
	s->player.UnregisterAllPlayers (); /* deletes the registered S98Player */
	DataLoader_Deinit (s->loader);
	free (s->data);
	delete s;
}

extern "C" uint32_t s98lib_render (struct s98lib_t *s, int16_t *buf, uint32_t frames)
{
	uint32_t done = 0;
	while (done < frames)
	{
		uint32_t got = s->player.Render ((frames - done) * 4, buf + done * 2) / 4;
		if (!got)
		{
			break;
		}
		done += got;
	}
	if (done < frames)
	{
		memset (buf + done * 2, 0, (frames - done) * 4);
	}
	return done;
}

extern "C" int s98lib_finished (struct s98lib_t *s)
{
	return !!(s->player.GetState () & PLAYSTATE_FIN);
}

extern "C" void s98lib_setloop (struct s98lib_t *s, int loops, int fade_seconds)
{
	PlayerA::Config c = s->player.GetConfiguration ();
	s->loops = loops;
	s->fade = fade_seconds;
	c.loopCount = loops;
	c.fadeSmpls = s->rate * fade_seconds;
	s->player.SetConfiguration (c);
}

extern "C" void s98lib_restart (struct s98lib_t *s)
{
	s->player.Stop ();
	s->player.Start ();
}

extern "C" void s98lib_setspeed (struct s98lib_t *s, double speed)
{
	s->player.SetPlaybackSpeed (speed);
}

extern "C" double s98lib_curtime (struct s98lib_t *s)
{
	return s->player.GetCurTime (PLAYTIME_LOOP_INCL | PLAYTIME_TIME_FILE);
}

extern "C" void s98lib_getinfo (struct s98lib_t *s, struct s98lib_info_t *info)
{
	PlayerBase *pb = s->player.GetPlayer ();
	memset (info, 0, sizeof (*info));
	if (!pb)
	{
		return;
	}
	info->length = pb->Tick2Second (pb->GetTotalTicks ());
	info->looplength = pb->GetLoopTicks () ? pb->Tick2Second (pb->GetLoopTicks ()) : 0.0;
	{
		std::vector<PLR_DEV_INFO> dv;
		pb->GetSongDeviceInfo (dv);
		info->devices = (int)dv.size ();
		for (size_t i = 0; i < dv.size (); i++)
		{
			const char *n = SndEmu_GetDevName (dv[i].type, 0x01, dv[i].devCfg);
			if (!n) n = "?";
			if (strlen (info->devicenames) + strlen (n) + 3 < sizeof (info->devicenames))
			{
				if (i) strcat (info->devicenames, ", ");
				strcat (info->devicenames, n);
			}
		}
	}
}
