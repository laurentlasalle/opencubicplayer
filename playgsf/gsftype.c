/* OpenCP Module Player
 * GSF file type detection and tag reading for the fileselector
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "boot/plinkman.h"
#include "filesel/filesystem.h"
#include "filesel/mdb.h"
#include "filesel/pfilesel.h"
#include "stuff/err.h"
#include "gsfload.h"
#include "gsfplay.h"
#include "gsftype.h"

#define MAX_TAG_AREA 50000 /* see the PSF specification */

static int gsfReadInfo (struct moduleinfostruct *m, struct ocpfilehandle_t *fp, const char *buf, size_t len, const struct mdbReadInfoAPI_t *API)
{
	struct gsf_tags_t tags;
	uint64_t tagofs, filesize;
	uint8_t *t = 0;
	size_t tlen = 0;

	if (!gsf_tags_offset ((const uint8_t *)buf, len, &tagofs))
	{
		return 0;
	}
	m->modtype.integer.i = MODULETYPE("GSF");

	/* only the tag area at the end of the file is read, not the program */
	if (fp && ((filesize = fp->filesize (fp)) > tagofs + 5))
	{
		tlen = filesize - tagofs;
		if (tlen > MAX_TAG_AREA + 5)
		{
			tlen = MAX_TAG_AREA + 5;
		}
		if ((t = malloc (tlen)))
		{
			fp->seek_set (fp, tagofs);
			if ((fp->read (fp, t, tlen) != (int)tlen) || memcmp (t, "[TAG]", 5))
			{
				free (t);
				t = 0;
			}
			fp->seek_set (fp, 0);
		}
	}
	gsf_parse_tags (t ? t + 5 : 0, t ? tlen - 5 : 0, &tags);
	free (t);

	snprintf (m->title,    sizeof (m->title),    "%s", tags.title);
	snprintf (m->composer, sizeof (m->composer), "%s", tags.artist);
	snprintf (m->album,    sizeof (m->album),    "%s", tags.game);
	snprintf (m->style,    sizeof (m->style),    "%s", tags.genre);
	snprintf (m->comment,  sizeof (m->comment),  "%s", tags.copyright[0] ? tags.copyright : tags.comment);
	if ((tags.year[0] >= '0') && (tags.year[0] <= '9'))
	{
		m->date = (uint32_t)atoi (tags.year) << 16;
	}
	if (tags.length_ms > 0)
	{
		uint32_t s = (tags.length_ms + ((tags.fade_ms > 0) ? tags.fade_ms : 0) + 500) / 1000;
		m->playtime = (s > 0xffff) ? 0xffff : (uint16_t)s;
	}
	return 1;
}

static const char *GSF_description[] =
{
	//                                                                          |
	"GSF files hold the music of Game Boy Advance games: the program of the game",
	"that plays the music, run by an emulated console. A MINIGSF file holds one",
	"track and needs its GSFLIB library, in the same directory. Playback using",
	"mGBA by Jeffrey Pfau.",
	NULL
};

static struct mdbreadinforegstruct gsfReadInfoReg = {"GSF", gsfReadInfo MDBREADINFOREGSTRUCT_TAIL};

OCP_INTERNAL int gsf_type_init (struct PluginInitAPI_t *API)
{
	struct moduletype mt;

	API->fsRegisterExt ("GSF");
	API->fsRegisterExt ("MINIGSF");
	mt.integer.i = MODULETYPE("GSF");
	API->fsTypeRegister (mt, GSF_description, "plOpenCP", &gsfPlayer);

	API->mdbRegisterReadInfo(&gsfReadInfoReg);

	return errOk;
}

OCP_INTERNAL void gsf_type_done (struct PluginCloseAPI_t *API)
{
	struct moduletype mt;

	mt.integer.i = MODULETYPE("GSF");
	API->fsTypeUnregister (mt);

	API->mdbUnregisterReadInfo(&gsfReadInfoReg);
}
