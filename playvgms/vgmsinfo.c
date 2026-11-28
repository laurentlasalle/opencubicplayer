/* OpenCP Module Player
 * vgmstream info view: the format of the stream. Based on the GME info view by
 * Stian Skjelstad.
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
#include "cpiface/cpiface.h"
#include "stuff/poutput.h"
#include "vgmsplay.h"

static int vgmsInfoActive;

static int vgmsInfoFirstLine;
static int vgmsInfoHeight;
static int vgmsInfoWidth;
static int vgmsInfoDesiredHeight;
static int vgmsInfoScroll;

#define COLTITLE1 0x01
#define COLTITLE1H 0x09

static void vgmsInfoSetWin (struct cpifaceSessionAPI_t *cpifaceSession, int _ignore, int wid, int ypos, int hgt)
{
	vgmsInfoFirstLine=ypos;
	vgmsInfoHeight=hgt;
	vgmsInfoWidth=wid;
}

static int vgmsInfoGetWin (struct cpifaceSessionAPI_t *cpifaceSession, struct cpitextmodequerystruct *q)
{
	if (!vgmsInfoActive)
		return 0;
	q->hgtmin=6;
	q->hgtmax = vgmsInfoDesiredHeight = 22;
	q->xmode=1;
	q->size=2;
	q->top=0;
	q->killprio=64;
	q->viewprio=160;
	return 1;
}

/* one line of the box; line counts from the top of the view, before scrolling */
static void box_line (struct cpifaceSessionAPI_t *cpifaceSession, int *line, const char *label, const char *value)
{
	char flat[512];
	size_t i;
	int y = *line - vgmsInfoScroll;
	(*line)++;
	if ((y < 1) || (y >= vgmsInfoHeight))
	{
		return;
	}
	/* a tag given on several lines is shown on one, the lines separated by / */
	snprintf (flat, sizeof (flat), "%s", value);
	for (i = 0; flat[i]; i++)
	{
		if (flat[i] == '\n')
		{
			flat[i] = '/';
		}
	}
	cpifaceSession->console->DisplayPrintf (vgmsInfoFirstLine + y, 0, 0x07, vgmsInfoWidth, "\xb3%.11o %12s: %.5o%*S%.7o\xb3", label, vgmsInfoWidth - 17, flat);
}

static void box_border (struct cpifaceSessionAPI_t *cpifaceSession, int *line, const char *fmt)
{
	int y = *line - vgmsInfoScroll;
	(*line)++;
	if ((y >= 1) && (y < vgmsInfoHeight))
	{
		cpifaceSession->console->DisplayPrintf (vgmsInfoFirstLine + y, 0, 0x07, vgmsInfoWidth, fmt, vgmsInfoWidth - 2);
	}
}

static void format_time (char *dst, size_t len, int64_t samples, int rate)
{
	int64_t ms = (rate > 0) ? samples * 1000 / rate : 0;
	snprintf (dst, len, "%d:%02d.%03d", (int)(ms / 60000), (int)((ms % 60000) / 1000), (int)(ms % 1000));
}

static void vgmsInfoDraw (struct cpifaceSessionAPI_t *cpifaceSession, int focus)
{
	struct vgmsinfo_t info;
	char text[160], t1[32], t2[32];
	int line = 1;

	vgmsGetInfo (&info);

	while (vgmsInfoScroll && ((vgmsInfoScroll + vgmsInfoHeight) > vgmsInfoDesiredHeight))
	{
		vgmsInfoScroll--;
	}

	cpifaceSession->console->Driver->DisplayStr (vgmsInfoFirstLine, 0, focus?COLTITLE1H:COLTITLE1, "vgmstream info view - page up/dn to scroll", vgmsInfoWidth);

	box_border (cpifaceSession, &line, "\xda%*C\xc4\xbf");
	box_line (cpifaceSession, &line, "Format", info.meta);
	box_line (cpifaceSession, &line, "Codec", info.codec);
	box_line (cpifaceSession, &line, "Layout", info.layout);
	if (info.name && info.name[0])
	{
		box_line (cpifaceSession, &line, "Stream", info.name);
	}
	if (info.subsongs > 1)
	{
		snprintf (text, sizeof (text), "%d of %d (keys < and >)", info.subsong, info.subsongs);
		box_line (cpifaceSession, &line, "Subsong", text);
	}
	box_border (cpifaceSession, &line, "\xc3%*C\xc4\xb4");

	snprintf (text, sizeof (text), "%d%s, %d Hz", info.channels, (info.channels > 2) ? " (mixed down to stereo)" : "", info.rate);
	box_line (cpifaceSession, &line, "Channels", text);
	if (info.bitrate > 0)
	{
		snprintf (text, sizeof (text), "%d kbit/s", (info.bitrate + 500) / 1000);
		box_line (cpifaceSession, &line, "Bitrate", text);
	}
	format_time (t1, sizeof (t1), info.samples, info.rate);
	box_line (cpifaceSession, &line, "Length", t1);
	if (info.loop)
	{
		format_time (t1, sizeof (t1), info.loop_start, info.rate);
		format_time (t2, sizeof (t2), info.loop_end, info.rate);
		snprintf (text, sizeof (text), "%s to %s", t1, t2);
	} else {
		snprintf (text, sizeof (text), "none");
	}
	box_line (cpifaceSession, &line, "Loop", text);
	format_time (t1, sizeof (t1), info.play_samples, info.rate);
	snprintf (text, sizeof (text), "%s%s", t1, info.loop ? " (loops and fade-out: [gme] loops= and fade=)" : "");
	box_line (cpifaceSession, &line, "Played", text);

	box_border (cpifaceSession, &line, "\xc3%*C\xc4\xb4");
	box_line (cpifaceSession, &line, "Filename", vgms_filename ? vgms_filename : "");
	box_border (cpifaceSession, &line, "\xc0%*C\xc4\xd9");

	vgmsInfoDesiredHeight = line;
	line -= vgmsInfoScroll;
	while (line < vgmsInfoHeight)
	{
		cpifaceSession->console->Driver->DisplayVoid (vgmsInfoFirstLine + line, 0, vgmsInfoWidth);
		line++;
	}
}

static int vgmsInfoIProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('i', "Enable vgmstream info viewer");
			cpifaceSession->KeyHelp ('I', "Enable vgmstream info viewer");
			break;
		case 'i': case 'I':
			vgmsInfoActive=1;
			cpifaceSession->cpiTextSetMode (cpifaceSession, "vgmsinfo");
			return 1;
		case 'x': case 'X':
			vgmsInfoActive=1;
			break;
		case KEY_ALT_X:
			vgmsInfoActive=0;
			break;
	}
	return 0;
}

static int vgmsInfoAProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case 'i': case 'I':
			vgmsInfoActive=!vgmsInfoActive;
			cpifaceSession->cpiTextRecalc (cpifaceSession);
			break;

		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('i',       "Disable vgmstream info viewer");
			cpifaceSession->KeyHelp ('I',       "Disable vgmstream info viewer");
			cpifaceSession->KeyHelp (KEY_PPAGE, "Scroll vgmstream info viewer up");
			cpifaceSession->KeyHelp (KEY_NPAGE, "Scroll vgmstream info viewer down");
			cpifaceSession->KeyHelp (KEY_HOME,  "Scroll vgmstream info viewer to the top");
			cpifaceSession->KeyHelp (KEY_END,   "Scroll vgmstream info viewer to the bottom");
			return 0;

		case KEY_PPAGE:
			if (vgmsInfoScroll)
			{
				vgmsInfoScroll--;
			}
			break;
		case KEY_NPAGE:
			vgmsInfoScroll++;
			break;
		case KEY_HOME:
			vgmsInfoScroll=0;
			break;
		case KEY_END:
			vgmsInfoScroll=vgmsInfoDesiredHeight - vgmsInfoHeight;
			break;
		default:
			return 0;
	}
	return 1;
}

static int vgmsInfoEvent (struct cpifaceSessionAPI_t *cpifaceSession, int ev)
{
	switch (ev)
	{
		case cpievInitAll:
			return 1;
		case cpievInit:
			vgmsInfoActive=1;
			break;
		case cpievDone:
			break;
	}
	return 1;
}

static struct cpitextmoderegstruct cpiVgmsInfo = {"vgmsinfo", vgmsInfoGetWin, vgmsInfoSetWin, vgmsInfoDraw, vgmsInfoIProcessKey, vgmsInfoAProcessKey, vgmsInfoEvent CPITEXTMODEREGSTRUCT_TAIL};

OCP_INTERNAL void vgmsInfoInit (struct cpifaceSessionAPI_t *cpifaceSession)
{
	cpifaceSession->cpiTextRegisterMode (cpifaceSession, &cpiVgmsInfo);
}

OCP_INTERNAL void vgmsInfoDone (struct cpifaceSessionAPI_t *cpifaceSession)
{
	cpifaceSession->cpiTextUnregisterMode (cpifaceSession, &cpiVgmsInfo);
}
