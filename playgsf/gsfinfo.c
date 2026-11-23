/* OpenCP Module Player
 * GSF info view: the tags of the file and how it is played. Based on the GME info
 * view by Stian Skjelstad.
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
#include "gsfemu.h"
#include "gsfplay.h"

static int gsfInfoActive;

static int gsfInfoFirstLine;
static int gsfInfoHeight;
static int gsfInfoWidth;
static int gsfInfoDesiredHeight;
static int gsfInfoScroll;

#define COLTITLE1 0x01
#define COLTITLE1H 0x09

static void gsfInfoSetWin (struct cpifaceSessionAPI_t *cpifaceSession, int _ignore, int wid, int ypos, int hgt)
{
	gsfInfoFirstLine=ypos;
	gsfInfoHeight=hgt;
	gsfInfoWidth=wid;
}

static int gsfInfoGetWin (struct cpifaceSessionAPI_t *cpifaceSession, struct cpitextmodequerystruct *q)
{
	if (!gsfInfoActive)
		return 0;
	q->hgtmin=6;
	q->hgtmax = gsfInfoDesiredHeight = 22;
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
	int y = *line - gsfInfoScroll;
	(*line)++;
	if ((y < 1) || (y >= gsfInfoHeight))
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
	cpifaceSession->console->DisplayPrintf (gsfInfoFirstLine + y, 0, 0x07, gsfInfoWidth, "\xb3%.11o %12s: %.5o%*S%.7o\xb3", label, gsfInfoWidth - 17, flat);
}

static void box_border (struct cpifaceSessionAPI_t *cpifaceSession, int *line, const char *fmt)
{
	int y = *line - gsfInfoScroll;
	(*line)++;
	if ((y >= 1) && (y < gsfInfoHeight))
	{
		cpifaceSession->console->DisplayPrintf (gsfInfoFirstLine + y, 0, 0x07, gsfInfoWidth, fmt, gsfInfoWidth - 2);
	}
}

static void format_time (char *dst, size_t len, int32_t ms)
{
	snprintf (dst, len, "%d:%02d.%03d", ms / 60000, (ms % 60000) / 1000, ms % 1000);
}

static void gsfInfoDraw (struct cpifaceSessionAPI_t *cpifaceSession, int focus)
{
	struct gsfinfo_t info;
	const struct gsf_tags_t *t;
	char text[128], text2[64];
	int line = 1;

	gsfGetInfo (&info);
	t = info.tags;

	while (gsfInfoScroll && ((gsfInfoScroll + gsfInfoHeight) > gsfInfoDesiredHeight))
	{
		gsfInfoScroll--;
	}

	cpifaceSession->console->Driver->DisplayStr (gsfInfoFirstLine, 0, focus?COLTITLE1H:COLTITLE1, "GSF info view - page up/dn to scroll", gsfInfoWidth);

	box_border (cpifaceSession, &line, "\xda%*C\xc4\xbf");
	snprintf (text, sizeof (text), "mGBA %s (Game Boy Advance)", gsfemu_version ());
	box_line (cpifaceSession, &line, "Emulator", text);
	box_border (cpifaceSession, &line, "\xc3%*C\xc4\xb4");

	if (t->game[0])      box_line (cpifaceSession, &line, "Game",      t->game);
	if (t->title[0])     box_line (cpifaceSession, &line, "Title",     t->title);
	if (t->artist[0])    box_line (cpifaceSession, &line, "Artist",    t->artist);
	if (t->year[0])      box_line (cpifaceSession, &line, "Year",      t->year);
	if (t->genre[0])     box_line (cpifaceSession, &line, "Genre",     t->genre);
	if (t->copyright[0]) box_line (cpifaceSession, &line, "Copyright", t->copyright);
	if (t->gsfby[0])     box_line (cpifaceSession, &line, "Ripped by",  t->gsfby);
	if (t->comment[0])   box_line (cpifaceSession, &line, "Comment",   t->comment);

	if (info.length_ms < 0)
	{
		snprintf (text, sizeof (text), "forever (no length in the file, [gme] defaultlength=0)");
	} else {
		format_time (text2, sizeof (text2), info.length_ms);
		snprintf (text, sizeof (text), "%s%s", text2, (t->length_ms > 0) ? "" : " (no length in the file: [gme] defaultlength)");
	}
	box_line (cpifaceSession, &line, "Length", text);
	format_time (text2, sizeof (text2), info.fade_ms);
	snprintf (text, sizeof (text), "%s%s", text2, (t->fade_ms >= 0) ? "" : " ([gme] fade)");
	box_line (cpifaceSession, &line, "Fade-out", text);

	box_border (cpifaceSession, &line, "\xc3%*C\xc4\xb4");
	box_line (cpifaceSession, &line, "Filename", gsf_filename ? gsf_filename : "");
	if (t->lib[0])
	{
		box_line (cpifaceSession, &line, "Library", t->lib);
	}
	snprintf (text, sizeof (text), "%s, %u KB", info.multiboot ? "multiboot (work RAM)" : "cartridge ROM", (unsigned int)((info.imagesize + 1023) / 1024));
	box_line (cpifaceSession, &line, "Program", text);
	box_border (cpifaceSession, &line, "\xc0%*C\xc4\xd9");

	gsfInfoDesiredHeight = line;
	line -= gsfInfoScroll;
	while (line < gsfInfoHeight)
	{
		cpifaceSession->console->Driver->DisplayVoid (gsfInfoFirstLine + line, 0, gsfInfoWidth);
		line++;
	}
}

static int gsfInfoIProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('i', "Enable GSF info viewer");
			cpifaceSession->KeyHelp ('I', "Enable GSF info viewer");
			break;
		case 'i': case 'I':
			gsfInfoActive=1;
			cpifaceSession->cpiTextSetMode (cpifaceSession, "gsfinfo");
			return 1;
		case 'x': case 'X':
			gsfInfoActive=1;
			break;
		case KEY_ALT_X:
			gsfInfoActive=0;
			break;
	}
	return 0;
}

static int gsfInfoAProcessKey (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t key)
{
	switch (key)
	{
		case 'i': case 'I':
			gsfInfoActive=!gsfInfoActive;
			cpifaceSession->cpiTextRecalc (cpifaceSession);
			break;

		case KEY_ALT_K:
			cpifaceSession->KeyHelp ('i',       "Disable GSF info viewer");
			cpifaceSession->KeyHelp ('I',       "Disable GSF info viewer");
			cpifaceSession->KeyHelp (KEY_PPAGE, "Scroll GSF info viewer up");
			cpifaceSession->KeyHelp (KEY_NPAGE, "Scroll GSF info viewer down");
			cpifaceSession->KeyHelp (KEY_HOME,  "Scroll GSF info viewer to the top");
			cpifaceSession->KeyHelp (KEY_END,   "Scroll GSF info viewer to the bottom");
			return 0;

		case KEY_PPAGE:
			if (gsfInfoScroll)
			{
				gsfInfoScroll--;
			}
			break;
		case KEY_NPAGE:
			gsfInfoScroll++;
			break;
		case KEY_HOME:
			gsfInfoScroll=0;
			break;
		case KEY_END:
			gsfInfoScroll=gsfInfoDesiredHeight - gsfInfoHeight;
			break;
		default:
			return 0;
	}
	return 1;
}

static int gsfInfoEvent (struct cpifaceSessionAPI_t *cpifaceSession, int ev)
{
	switch (ev)
	{
		case cpievInitAll:
			return 1;
		case cpievInit:
			gsfInfoActive=1;
			break;
		case cpievDone:
			break;
	}
	return 1;
}

static struct cpitextmoderegstruct cpiGsfInfo = {"gsfinfo", gsfInfoGetWin, gsfInfoSetWin, gsfInfoDraw, gsfInfoIProcessKey, gsfInfoAProcessKey, gsfInfoEvent CPITEXTMODEREGSTRUCT_TAIL};

OCP_INTERNAL void gsfInfoInit (struct cpifaceSessionAPI_t *cpifaceSession)
{
	cpifaceSession->cpiTextRegisterMode (cpifaceSession, &cpiGsfInfo);
}

OCP_INTERNAL void gsfInfoDone (struct cpifaceSessionAPI_t *cpifaceSession)
{
	cpifaceSession->cpiTextUnregisterMode (cpifaceSession, &cpiGsfInfo);
}
