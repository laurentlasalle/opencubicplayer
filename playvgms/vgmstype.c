/* OpenCP Module Player
 * vgmstream file types: registration of the extensions and detection for the
 * fileselector
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
#include <strings.h>
#include <libvgmstream.h>
#include <libvgmstream_streamfile.h>
#include "types.h"
#include "boot/plinkman.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "filesel/mdb.h"
#include "filesel/pfilesel.h"
#include "stuff/err.h"
#include "vgmsplay.h"
#include "vgmstype.h"

#define MAX_EXT 7 /* longest file extension the fileselector accepts (MAX_MODEXT_LENGTH) */

/* Extensions that vgmstream knows but that stay with the players OCP already has
 * (and the very generic ones vgmstream itself calls "common", see
 * libvgmstream_get_common_extensions()). vgmstream only gets the files of the
 * other extensions, so it never takes a WAV, an MP3, a VGM or a module from them. */
static const char *const not_ours[] =
{
	"669", "ahx", "ams", "ay", "cda", "dmf", "far", "flac", "gbs", "gsf", "hes", "imf",
	"it", "kss", "mad", "mid", "midi", "minigsf", "mod", "mtm", "mus", "mxm", "nsf",
	"nsfe", "nst", "oga", "ogg", "okt", "opus", "psid", "ptm", "rac", "rad", "rmi",
	"rsid", "s3m", "s98", "sap", "sid", "sng", "spc", "stm", "ult", "vgm", "vgz",
	"wav", "wave", "wv", "xm", "xms", "ym"
};

static char (*exts)[MAX_EXT + 1]; /* the extensions given to vgmstream, sorted, lower case */
static int extcount;

static int ext_cmp (const void *a, const void *b)
{
	return strcmp (a, b);
}

static int is_not_ours (const char *ext)
{
	int i, n;
	const char **common = libvgmstream_get_common_extensions (&n);
	for (i = 0; i < (int)(sizeof (not_ours) / sizeof (not_ours[0])); i++)
	{
		if (!strcasecmp (ext, not_ours[i]))
		{
			return 1;
		}
	}
	for (i = 0; common && (i < n); i++)
	{
		if (!strcasecmp (ext, common[i]))
		{
			return 1;
		}
	}
	/* AdPlug takes the extensions 0 to 99 (BAM files of RPG games are stored as .1, .2 and so on) */
	if ((ext[0] >= '0') && (ext[0] <= '9') && (!ext[1] || ((ext[1] >= '0') && (ext[1] <= '9') && !ext[2])))
	{
		return 1;
	}
	return 0;
}

/* the extension can be registered in OCP: letters, digits, - and _ */
static int is_valid (const char *ext)
{
	int i;
	for (i = 0; ext[i]; i++)
	{
		char c = ext[i];
		if ((i >= MAX_EXT) || !(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '-') || (c == '_')))
		{
			return 0;
		}
	}
	return i > 0;
}

static int is_vgms_ext (const char *filename)
{
	const char *dot = strrchr (filename, '.');
	char ext[MAX_EXT + 1];
	int i;
	if (!dot || !dot[1] || (strlen (dot + 1) > MAX_EXT))
	{
		return 0;
	}
	for (i = 0; dot[1 + i]; i++)
	{
		ext[i] = ((dot[1 + i] >= 'A') && (dot[1 + i] <= 'Z')) ? dot[1 + i] + 32 : dot[1 + i];
	}
	ext[i] = 0;
	return bsearch (ext, exts, extcount, sizeof (exts[0]), ext_cmp) != 0;
}

static int vgmsReadInfo (struct moduleinfostruct *m, struct ocpfilehandle_t *fp, const char *buf, size_t len, const struct mdbReadInfoAPI_t *API)
{
	const char *filename;
	struct libstreamfile_t *sf;
	libvgmstream_t *v;
	libvgmstream_config_t cfg;

	if (!fp)
	{
		return 0;
	}
	API->dirdb->GetName_internalstr (fp->dirdb_ref, &filename);
	if (!is_vgms_ext (filename))
	{
		return 0;
	}
	if (!(sf = vgms_streamfile (fp, API->dirdb)))
	{
		return 0;
	}
	memset (&cfg, 0, sizeof (cfg));
	cfg.loop_count = 2; /* the playtime shown in the fileselector: the default [gme] loops=2 and fade=8 */
	cfg.fade_time = 8;
	v = libvgmstream_create (sf, 0, &cfg);
	libstreamfile_close (sf);
	fp->seek_set (fp, 0);
	if (!v)
	{
		return 0;
	}

	m->modtype.integer.i = MODULETYPE("VGMS");
	if (v->format->stream_name[0])
	{
		snprintf (m->title, sizeof (m->title), "%s", v->format->stream_name);
	}
	snprintf (m->comment, sizeof (m->comment), "%s, %s", v->format->meta_name, v->format->codec_name);
	m->channels = (v->format->input_channels ? v->format->input_channels : v->format->channels);
	if (v->format->sample_rate > 0)
	{
		int64_t s = v->format->play_samples / v->format->sample_rate;
		m->playtime = (s > 0xffff) ? 0xffff : (uint16_t)s;
	}
	libvgmstream_free (v);
	return 1;
}

static const char *VGMS_description[] =
{
	//                                                                          |
	"Audio streams of games (GENH, ADX, BRSTM, HCA, XA and hundreds of other",
	"formats), recorded sound in the formats of the consoles. Played by",
	"vgmstream, with the loops of the files (see [gme] loops= and fade= in",
	"ocp.ini).",
	NULL
};

static struct mdbreadinforegstruct vgmsReadInfoReg = {"vgmstream", vgmsReadInfo MDBREADINFOREGSTRUCT_TAIL};

OCP_INTERNAL int vgms_type_init (struct PluginInitAPI_t *API)
{
	struct moduletype mt;
	const char **list;
	int n, i;

	list = libvgmstream_get_extensions (&n);
	exts = calloc (n > 0 ? n : 1, sizeof (exts[0]));
	if (!exts)
	{
		return errAllocMem;
	}
	for (i = 0; list && (i < n); i++)
	{
		char upper[MAX_EXT + 1];
		int j;
		if (!is_valid (list[i]) || is_not_ours (list[i]))
		{
			continue;
		}
		for (j = 0; list[i][j]; j++)
		{
			exts[extcount][j] = ((list[i][j] >= 'A') && (list[i][j] <= 'Z')) ? list[i][j] + 32 : list[i][j];
			upper[j] = ((list[i][j] >= 'a') && (list[i][j] <= 'z')) ? list[i][j] - 32 : list[i][j];
		}
		exts[extcount][j] = 0;
		upper[j] = 0;
		extcount++;
		API->fsRegisterExt (upper);
	}
	qsort (exts, extcount, sizeof (exts[0]), ext_cmp);

	mt.integer.i = MODULETYPE("VGMS");
	API->fsTypeRegister (mt, VGMS_description, "plOpenCP", &vgmsPlayer);

	API->mdbRegisterReadInfo(&vgmsReadInfoReg);

	return errOk;
}

OCP_INTERNAL void vgms_type_done (struct PluginCloseAPI_t *API)
{
	struct moduletype mt;

	mt.integer.i = MODULETYPE("VGMS");
	API->fsTypeUnregister (mt);

	API->mdbUnregisterReadInfo(&vgmsReadInfoReg);

	free (exts);
	exts = 0;
	extcount = 0;
}
