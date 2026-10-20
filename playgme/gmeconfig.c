/* OpenCP Module Player
 *
 * GMEPlay configuration dialog, available as setup:/gmeconfig.dev
 * Edits the [gme] section of ocp.ini:
 *   trackerview, rowframes, defaultlength, loops, fade
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
#include "boot/psetting.h"
#include "filesel/filesystem.h"
#include "filesel/filesystem-drive.h"
#include "filesel/filesystem-file-dev.h"
#include "filesel/filesystem-setup.h"
#include "filesel/pfilesel.h"
#include "stuff/compat.h"
#include "stuff/err.h"
#include "stuff/poutput.h"
#include "gmeconfig.h"

#define ENTRIES 5

static int cfgTrackerView;
static int cfgRowFrames;
static int cfgDefaultLength;
static int cfgLoops;
static int cfgFade;

static const struct
{
	int min, max;
} limits[ENTRIES] = {{0, 1}, {1, 32}, {0, 1200}, {1, 10}, {0, 60}};

static int *value (int n)
{
	switch (n)
	{
		case 0: return &cfgTrackerView;
		case 1: return &cfgRowFrames;
		case 2: return &cfgDefaultLength;
		case 3: return &cfgLoops;
		default: return &cfgFade;
	}
}

static void DrawItems (const int lineno, int xpos, const int width, const char **list, const int listlength, const int selected, const int active, const struct DevInterfaceAPI_t *API)
{
	int i;
	int origxpos = xpos;
	for (i=0; i < listlength; i++)
	{
		int l = strlen (list[i]);
		if (selected == i)
		{
			API->console->DisplayPrintf (lineno, xpos, (active)?0x09:0x01, l + 2, "[%.*o%s%.*o]", (active)?0x0f:0x07, list[i], (active)?0x09:0x01);
		} else {
			API->console->DisplayPrintf (lineno, xpos, 0x00, l + 2, " %.*o%s%.0o ", (active)?0x07:0x08, list[i]);
		}
		xpos += l + 2;
	}
	API->console->Driver->DisplayChr (lineno, xpos, 0x07, ' ', width - xpos + origxpos);
}

static void DrawBar (const int lineno, int xpos, int width, int level, int minlevel, int maxlevel, const char *unit, const int active, const struct DevInterfaceAPI_t *API)
{
	char temp[24];
	int tw = width - 16;
	int pw = tw * (level - minlevel) / (maxlevel - minlevel);

	API->console->Driver->DisplayStr (lineno, xpos,          (active)?0x07:0x08, "[", 1);
	API->console->Driver->DisplayChr (lineno, xpos + 1,      (active)?0x09:0x08, '\xfe', pw);
	API->console->Driver->DisplayChr (lineno, xpos + 1 + pw, (active)?0x07:0x08, '\xfa', tw - pw);
	snprintf (temp, sizeof (temp), "] %5d %s", level, unit);
	API->console->Driver->DisplayStr (lineno, xpos + 1 + tw, (active)?0x0f:0x08, temp, width - tw - 1);
}

static void gmeConfigDraw (int EditPos, const struct DevInterfaceAPI_t *API)
{
	const char *offon[] = {"off", "on"};
	int mlWidth = 70, mlHeight = 18, mlTop, mlLeft;

	if (mlHeight > API->console->TextHeight)
	{
		mlHeight = API->console->TextHeight;
	}
	mlTop = (API->console->TextHeight - mlHeight) / 2;
	mlLeft = (API->console->TextWidth - mlWidth) / 2;

	API->console->DisplayFrame (mlTop++, mlLeft++, mlHeight, mlWidth, DIALOG_COLOR_FRAME, "Game Music Emulator (NSF, SPC, GBS, VGM...) configuration", 0, 2, 0);
	mlWidth -= 2;

	API->console->DisplayPrintf (mlTop++, mlLeft, 0x07, mlWidth, " Navigate with arrows and hit %.15o<ESC>%.7o to save and exit.");
	mlTop++; /* horizontal bar */

	API->console->DisplayPrintf (mlTop++, mlLeft, 0x07, mlWidth, " 1. Tracker view: NSF, HES, GBS, SPC, VGM, GYM and S98 (from the chip registers):");
	DrawItems (mlTop++, mlLeft + 3, mlWidth - 3, offon, 2, cfgTrackerView, EditPos==0, API);
	API->console->DisplayPrintf (mlTop++, mlLeft, 0x07, mlWidth, " 2. Tracker view, frames per row (60 frames = 1 second):");
	DrawBar (mlTop++, mlLeft + 4, mlWidth - 5, cfgRowFrames, 1, 32, "frames", EditPos==1, API);
	API->console->DisplayPrintf (mlTop++, mlLeft, 0x07, mlWidth, " 3. Default length, when the file gives none (0 = play forever):");
	DrawBar (mlTop++, mlLeft + 4, mlWidth - 5, cfgDefaultLength, 0, 1200, "seconds", EditPos==2, API);
	API->console->DisplayPrintf (mlTop++, mlLeft, 0x07, mlWidth, " 4. Loops, when the file gives intro/loop lengths:");
	DrawBar (mlTop++, mlLeft + 4, mlWidth - 5, cfgLoops, 1, 10, "times", EditPos==3, API);
	API->console->DisplayPrintf (mlTop++, mlLeft, 0x07, mlWidth, " 5. Fade-out, unless the file specifies one:");
	DrawBar (mlTop++, mlLeft + 4, mlWidth - 5, cfgFade, 0, 60, "seconds", EditPos==4, API);
	mlTop++;
	API->console->DisplayPrintf (mlTop++, mlLeft, 0x08, mlWidth, " Changes apply to the next file that is opened.");
}

static void gmeConfigRun (void **token, const struct DevInterfaceAPI_t *API)
{
	int esel = 0;
	int i;

	cfgTrackerView   = API->configAPI->GetProfileBool ("gme", "trackerview", 1, 1);
	cfgRowFrames     = API->configAPI->GetProfileInt  ("gme", "rowframes", 6, 10);
	cfgDefaultLength = API->configAPI->GetProfileInt  ("gme", "defaultlength", 150, 10);
	cfgLoops         = API->configAPI->GetProfileInt  ("gme", "loops", 2, 10);
	cfgFade          = API->configAPI->GetProfileInt  ("gme", "fade", 8, 10);
	for (i = 0; i < ENTRIES; i++)
	{
		int *v = value (i);
		if (*v < limits[i].min) *v = limits[i].min;
		if (*v > limits[i].max) *v = limits[i].max;
	}

	while (1)
	{
		API->fsDraw();
		gmeConfigDraw (esel, API);
		while (API->console->KeyboardHit())
		{
			int key = API->console->KeyboardGetChar();
			static uint32_t lastpress = 0;
			static int repeat;
			int *v = value (esel);

			if ((key != KEY_LEFT) && (key != KEY_RIGHT))
			{
				lastpress = 0;
				repeat = 1;
			} else {
				uint32_t newpress = clock_ms();
				if ((newpress-lastpress) > 250) /* 250 ms */
				{
					repeat = 1;
				} else if (repeat < 20)
				{
					repeat += 1;
				}
				lastpress = newpress;
			}
			if (esel != 2)
			{
				repeat = 1; /* only the length has a large range */
			}

			switch (key)
			{
				case KEY_LEFT:
					*v -= repeat;
					if (*v < limits[esel].min) *v = limits[esel].min;
					break;
				case KEY_RIGHT:
					*v += repeat;
					if (*v > limits[esel].max) *v = limits[esel].max;
					break;
				case KEY_HOME:
					*v = limits[esel].min;
					break;
				case KEY_END:
					*v = limits[esel].max;
					break;
				case KEY_DOWN:
					if (esel < (ENTRIES - 1))
					{
						esel++;
					}
					break;
				case KEY_UP:
					if (esel)
					{
						esel--;
					}
					break;
				case KEY_EXIT:
				case KEY_ESC:
				case _KEY_ENTER:
					goto superexit;
			}
		}
		API->console->FrameLock ();
	}

superexit:
	API->configAPI->SetProfileBool ("gme", "trackerview",   cfgTrackerView);
	API->configAPI->SetProfileInt  ("gme", "rowframes",     cfgRowFrames,     10);
	API->configAPI->SetProfileInt  ("gme", "defaultlength", cfgDefaultLength, 10);
	API->configAPI->SetProfileInt  ("gme", "loops",         cfgLoops,         10);
	API->configAPI->SetProfileInt  ("gme", "fade",          cfgFade,          10);
	API->configAPI->StoreConfig ();
}

static struct ocpfile_t *gmeconfig;

OCP_INTERNAL int gme_config_init (struct PluginInitAPI_t *API)
{
	gmeconfig = API->dev_file_create (
		API->dmSetup->basedir,
		"gmeconfig.dev",
		"Game Music Emulator Configuration",
		"",
		0, /* token */
		0, /* Init */
		gmeConfigRun,
		0, /* Close */
		0  /* Destructor */
	);

	API->filesystem_setup_register_file (gmeconfig);

	return errOk;
}

OCP_INTERNAL void gme_config_done (struct PluginCloseAPI_t *API)
{
	if (gmeconfig)
	{
		API->filesystem_setup_unregister_file (gmeconfig);
		gmeconfig->unref (gmeconfig);
		gmeconfig = 0;
	}
}
