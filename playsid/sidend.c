/* OpenCP Module Player
 * Detection of the end of a SID tune that starts again by itself (see sidend.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <string.h>
#include "sidend.h"

/* A frame is silent when neither side moves more than this (peak to peak, 16-bit
 * samples): the output of a SID is never exactly flat (filters, DC offset of the
 * 6581), so a small margin is needed. */
#define SILENT_PEAK_TO_PEAK 256

#define CONTROLS 9 /* control registers kept per frame: 3 voices on up to 3 chips */

static int chips;
static uint8_t start[SIDEND_COMPARE][CONTROLS];    /* the first frames of the tune */
static int start_frames;                           /* frames in start[]: recording begins with the first frame heard */
static uint8_t previous[SIDEND_COMPARE][CONTROLS]; /* the frames that followed the previous silence */
static int have_previous;
static uint8_t current[SIDEND_COMPARE][CONTROLS];  /* the frames after the current silence */
static int comparing;                              /* frames in current[], -1 when not comparing */
static int silent_frames;                          /* length of the current silence */

void sidend_reset (int n)
{
	chips = (n < 1) ? 1 : (n > 3) ? 3 : n;
	start_frames = 0;
	have_previous = 0;
	comparing = -1;
	silent_frames = 0;
}

static int is_silent (const int16_t *stereo, unsigned int samples)
{
	int16_t lmin = 32767, lmax = -32768, rmin = 32767, rmax = -32768;
	unsigned int i;
	for (i = 0; i < samples; i++)
	{
		int16_t l = stereo[i * 2], r = stereo[i * 2 + 1];
		if (l < lmin) lmin = l;
		if (l > lmax) lmax = l;
		if (r < rmin) rmin = r;
		if (r > rmax) rmax = r;
	}
	return !samples || (((int)lmax - lmin < SILENT_PEAK_TO_PEAK) && ((int)rmax - rmin < SILENT_PEAK_TO_PEAK));
}

static void controls (uint8_t dst[CONTROLS], const uint8_t regs[3][0x20])
{
	int c;
	memset (dst, 0, CONTROLS);
	for (c = 0; c < chips; c++)
	{
		dst[c * 3 + 0] = regs[c][0x04];
		dst[c * 3 + 1] = regs[c][0x0b];
		dst[c * 3 + 2] = regs[c][0x12];
	}
}

/* percent of equal control registers between a and b, the best of the shifts of b
 * from -SIDEND_SHIFT to +SIDEND_SHIFT frames */
static int similarity (uint8_t a[][CONTROLS], uint8_t b[][CONTROLS], int frames)
{
	int shift, best = 0;
	for (shift = -SIDEND_SHIFT; shift <= SIDEND_SHIFT; shift++)
	{
		int i, j, equal = 0, total = 0;
		for (i = 0; i < frames; i++)
		{
			int k = i + shift;
			if ((k < 0) || (k >= frames))
			{
				continue;
			}
			for (j = 0; j < chips * 3; j++)
			{
				equal += (a[i][j] == b[k][j]);
				total++;
			}
		}
		if (total && (equal * 100 / total > best))
		{
			best = equal * 100 / total;
		}
	}
	return best;
}

enum sidend_action_t sidend_frame (const int16_t *stereo, unsigned int samples, const uint8_t regs[3][0x20])
{
	int silent = is_silent (stereo, samples);

	if (comparing >= 0)
	{ /* after a silence: does the program play a part already heard? */
		controls (current[comparing++], regs);
		if (comparing < SIDEND_COMPARE)
		{
			return SIDEND_HOLD;
		}
		comparing = -1;
		silent_frames = silent ? 1 : 0;
		if ((similarity (current, start, SIDEND_COMPARE) >= SIDEND_SIMILAR) ||
		    (have_previous && (similarity (current, previous, SIDEND_COMPARE) >= SIDEND_SIMILAR)))
		{
			return SIDEND_RESTARTED;
		}
		memcpy (previous, current, sizeof (previous));
		have_previous = 1;
		return SIDEND_FLUSH;
	}

	/* the start of the tune: from its first frame that is heard */
	if ((start_frames < SIDEND_COMPARE) && (start_frames || !silent))
	{
		controls (start[start_frames++], regs);
	}

	if (silent)
	{
		silent_frames++;
		return SIDEND_PLAY;
	}
	if ((silent_frames >= SIDEND_MIN_SILENCE) && (start_frames == SIDEND_COMPARE))
	{ /* the sound comes back after a silence: compare the next frames */
		silent_frames = 0;
		comparing = 0;
		controls (current[comparing++], regs);
		return SIDEND_HOLD;
	}
	silent_frames = 0;
	return SIDEND_PLAY;
}
