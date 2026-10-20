/* OpenCP Module Player
 *
 * GMEPlay: tracker-like pattern view for NSF/NSFe, built from sound chip
 * register snapshots taken once per frame (see nsfvis.c).
 *
 * Each pattern holds 64 rows, each row covers "rowframes" frames ([gme] rowframes
 * in ocp.ini, 6 by default). A row shows the note / retrigger that started during
 * these frames, the "instrument" (duty cycle, noise mode, VRC7 patch...) and the volume.
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
#include "gmeplay.h"
#include "nsfvis.h"

#define COLNOTE  0x0F
#define COLNOISE 0x0B
#define COLDPCM  0x0D
#define COLOFF   0x08
#define COLINS   0x07
#define COLVOL   0x09
#define COLFX    0x02

#define ROWS 64

static int curPat;
static int curRow;
static int curCh;

static int  cacheValid;
static int  cachePat, cacheRow, cacheCh;
static struct nsfvis_cell_t cacheCell;
static int  cacheHas;

static long lastPos = -1;


static int trak_getcurpos (struct cpifaceSessionAPI_t *cpifaceSession)
{
	long cur = gmeVisCurrentFrame ();
	long rowidx;
	int pat;

	if (cur < 0)
	{
		cur = 0;
	}
	rowidx = cur / gmeVisRowFrames ();
	pat = rowidx / ROWS;
	if (pat >= GME_TRAK_PATTERNS)
	{
		pat = GME_TRAK_PATTERNS - 1;
	}
	if (gmeVisTakeNewData () || (rowidx != lastPos))
	{
		/* the prepared pattern is cached by the track viewer, new frames must be rendered */
		cpifaceSession->SelectedChannelChanged = 1;
		cacheValid = 0;
		lastPos = rowidx;
	}
	return (pat << 8) | (rowidx % ROWS);
}

static int trak_getpatlen (struct cpifaceSessionAPI_t *cpifaceSession, int n)
{
	return ROWS;
}

static const char *short_name (const char *n, char *buf)
{
	if (!strcmp (n, "Pulse 1")) return "P1";
	if (!strcmp (n, "Pulse 2")) return "P2";
	if (!strcmp (n, "Triangle")) return "Tri";
	if (!strcmp (n, "Noise")) return "Noi";
	if (!strcmp (n, "DPCM")) return "DMC";
	if (!strncmp (n, "VRC6 ", 5)) { snprintf (buf, 8, "V6%s", n + 5); return buf; }
	if (!strncmp (n, "N163 ", 5)) { snprintf (buf, 8, "N%s", n + 5); return buf; }
	if (!strncmp (n, "5B ", 3)) { snprintf (buf, 8, "5B%s", n + 3); return buf; }
	if (!strncmp (n, "MMC5 ", 5)) { snprintf (buf, 8, "M5%s", n + 5); return buf; }
	if (!strncmp (n, "VRC7 ", 5)) { snprintf (buf, 8, "V7%s", n + 5); return buf; }
	if (!strncmp (n, "Wave ", 5)) { snprintf (buf, 8, "W%s", n + 5); return buf; }
	if (!strncmp (n, "Multi ", 6)) { snprintf (buf, 8, "M%s", n + 6); return buf; }
	if (!strncmp (n, "Square ", 7)) { snprintf (buf, 8, "Sq%s", n + 7); return buf; }
	if (!strcmp (n, "Wave")) return "Wav";
	if (!strncmp (n, "FM ", 3)) { snprintf (buf, 8, "FM%s", n + 3); return buf; }
	if (!strncmp (n, "OPLL ", 5)) { snprintf (buf, 8, "OL%s", n + 5); return buf; }
	if (!strncmp (n, "Voice ", 6)) { snprintf (buf, 8, "V%s", n + 6); return buf; }
	if (!strncmp (n, "SSG ", 4)) { snprintf (buf, 8, "SG%s", n + 4); return buf; }
	if (!strcmp (n, "Rhythm")) return "Rhy";
	if (!strcmp (n, "ADPCM")) return "PCM";
	return n;
}

static const char *trak_getpatname (struct cpifaceSessionAPI_t *cpifaceSession, int n)
{
	return 0; /* channel names are shown in the column header, see trak_getchanname() */
}

static const char *trak_getchanname (struct cpifaceSessionAPI_t *cpifaceSession, int ch)
{
	const struct nsfvis_layout_t *l = gmeVisLayout ();
	static char buf[8];
	if (ch < 0 || ch >= l->count)
	{
		return 0;
	}
	return short_name (l->name[ch], buf);
}

static void trak_seektrack (struct cpifaceSessionAPI_t *cpifaceSession, int n, int c)
{
	curPat = n;
	curCh = c;
	curRow = -1;
}

static int trak_startrow (struct cpifaceSessionAPI_t *cpifaceSession)
{
	long f0;
	curRow++;
	if (curRow >= ROWS)
	{
		return -1;
	}
	f0 = ((long)curPat * ROWS + curRow) * gmeVisRowFrames ();
	if (f0 > gmeVisLastFrame ())
	{
		return -1; /* not emulated yet */
	}
	return curRow;
}

/* computes the cell of the current row/channel, returns 0 if no data */
static int getcell (void)
{
	const struct nsfvis_ch_t *frames[32];
	const struct nsfvis_ch_t *prev;
	int rf = gmeVisRowFrames ();
	long f0 = ((long)curPat * ROWS + curRow) * rf;
	int i, n = 0;

	if (curCh < 0)
	{
		return 0;
	}
	if (cacheValid && (cachePat == curPat) && (cacheRow == curRow) && (cacheCh == curCh))
	{
		return cacheHas;
	}
	cacheValid = 1;
	cachePat = curPat;
	cacheRow = curRow;
	cacheCh = curCh;
	cacheHas = 0;

	if (rf > 32)
	{
		rf = 32;
	}
	for (i = 0; i < rf; i++)
	{
		const struct nsfvis_ch_t *f = gmeVisFrame (f0 + i);
		if (!f)
		{
			break;
		}
		frames[n++] = f + curCh;
	}
	if (!n)
	{
		return 0;
	}
	prev = gmeVisFrame (f0 - 1);
	if (prev)
	{
		prev += curCh;
	}
	nsfvis_cell (prev, frames, n, &cacheCell);
	cacheHas = 1;
	return 1;
}

static int trak_getnote (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t *bp, int small)
{
	const struct nsfvis_cell_t *c = &cacheCell;
	char tmp[4];

	if (!getcell ())
	{
		return 0;
	}
	if (c->has_note)
	{
		if (c->kind == NSFVIS_NOISE)
		{
			int p = 15 - (c->aux & 15);
			switch (small)
			{
				case 0: snprintf (tmp, sizeof (tmp), "%X-#", p); break;
				case 1: snprintf (tmp, sizeof (tmp), "%X#", p); break;
				default: snprintf (tmp, sizeof (tmp), "%X", p); break;
			}
			cpifaceSession->console->WriteString (bp, 0, COLNOISE, tmp, 3 - small);
		} else if (c->kind == NSFVIS_DPCM)
		{
			switch (small)
			{
				case 0:
					cpifaceSession->console->WriteString (bp, 0, COLDPCM, "S", 1);
					cpifaceSession->console->WriteNum (bp, 1, COLDPCM, c->aux, 16, 2, 0);
					break;
				case 1:
					cpifaceSession->console->WriteNum (bp, 0, COLDPCM, c->aux, 16, 2, 0);
					break;
				default:
					cpifaceSession->console->WriteString (bp, 0, COLDPCM, "S", 1);
					break;
			}
		} else if (c->kind == NSFVIS_DRUMS)
		{ /* up to 3 letters: Bass drum, Snare, Hi-hat, top Cymbal, Tom, Rim shot */
			static const char order[6] = {0, 1, 3, 2, 4, 5};
			static const char letter[6] = {'B', 'S', 'C', 'H', 'T', 'R'};
			char t[4] = "   ";
			int k, m = 0;
			for (k = 0; k < 6 && m < 3 - small; k++)
			{
				if ((c->aux >> order[k]) & 1) t[m++] = letter[(int)order[k]];
			}
			cpifaceSession->console->WriteString (bp, 0, COLNOISE, t, 3 - small);
		} else if (c->kind == NSFVIS_PCM || c->note < 0)
		{
			cpifaceSession->console->WriteString (bp, 0, COLDPCM, "***", 3 - small);
		} else {
			int note = c->note;
			if (note > 119) note = 119;
			switch (small)
			{
				case 0:
					cpifaceSession->console->WriteString (bp, 0, COLNOTE, &"CCDDEFFGGAAB"[note % 12], 1);
					cpifaceSession->console->WriteString (bp, 1, COLNOTE, &"-#-#--#-#-#-"[note % 12], 1);
					cpifaceSession->console->WriteString (bp, 2, COLNOTE, &"-0123456789" [note / 12], 1);
					break;
				case 1:
					cpifaceSession->console->WriteString (bp, 0, COLNOTE, &"cCdDefFgGaAb"[note % 12], 1);
					cpifaceSession->console->WriteString (bp, 1, COLNOTE, &"-0123456789" [note / 12], 1);
					break;
				default:
					cpifaceSession->console->WriteString (bp, 0, COLNOTE, &"cCdDefFgGaAb"[note % 12], 1);
					break;
			}
		}
		return 1;
	}
	if (c->noteoff)
	{
		cpifaceSession->console->WriteString (bp, 0, COLOFF, "---", 3 - small);
		return 1;
	}
	return 0;
}

static int trak_getins (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t *bp)
{
	if (!getcell () || !cacheCell.has_ins)
	{
		return 0;
	}
	cpifaceSession->console->WriteNum (bp, 0, COLINS, cacheCell.ins, 16, 2, 0);
	return 1;
}

static int trak_getvol (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t *bp)
{
	if (!getcell () || !cacheCell.has_vol)
	{
		return 0;
	}
	cpifaceSession->console->WriteNum (bp, 0, COLVOL, cacheCell.vol, 16, 2, 0);
	return 1;
}

static int trak_getpan (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t *bp)
{
	return 0;
}

static void trak_getfx (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t *bp, int n)
{
	if (!n || !getcell () || !cacheCell.slide)
	{
		return;
	}
	/* CP437: 0x18 up arrow, 0x19 down arrow, 0xF7 approximately (vibrato) */
	cpifaceSession->console->WriteString (bp, 0, COLFX, (cacheCell.slide == 1) ? "\x18" : (cacheCell.slide == 2) ? "\x19" : "\xf7", 1);
}

static void trak_getgcmd (struct cpifaceSessionAPI_t *cpifaceSession, uint16_t *bp, int n)
{
}

OCP_INTERNAL const struct cpitrakdisplaystruct gmeTrakDisplay =
{
	trak_getcurpos,
	trak_getpatlen,
	trak_getpatname,
	trak_seektrack,
	trak_startrow,
	trak_getnote,
	trak_getins,
	trak_getvol,
	trak_getpan,
	trak_getfx,
	trak_getgcmd,
	trak_getchanname
};
