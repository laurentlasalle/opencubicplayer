/* OpenCP Module Player
 * Volume normalization (Omni), see cpinorm.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "boot/psetting.h"
#include "cpiface/cpinorm.h"
#include "dev/player.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "stuff/file.h"

#define DATABASE_NAME  "CPLOUD.DAT"
#define DATABASE_MAGIC "OCP loudness 1\n"

#define BLOCK_SECONDS    0.4   /* the loudness is measured in blocks of 400 ms */
#define FIRST_SECONDS    3.0   /* sound measured before the gain of a new file is set */
#define GLIDE_SECONDS    1.5
#define SAVE_SECONDS     10.0  /* sound needed to save the loudness of a file */
#define ABSOLUTE_GATE   -60.0  /* dBFS: silent blocks */
#define RELATIVE_GATE   -10.0  /* dB below the mean: quiet blocks (fade-outs, breaks) */
#define MAX_BOOST        12.0  /* dB */
#define MAX_CUT         -12.0

/* blocks are counted by loudness in steps of 0.25 dB, from -70 to 0 dBFS */
#define BINS 280
#define BIN_LOW -70.0
#define BIN_STEP 0.25

struct entry_t
{
	char *path;
	uint64_t size;
	int32_t loudness; /* hundredths of dB */
	int32_t peak;     /* highest sample, 1 to 32768 */
};

static const struct plrDevAPI_t *device;
static struct plrDevAPI_t wrapped;

static struct entry_t *entries;
static int entrycount, entrysize, loaded;
static double target = -18.0;

/* the file being played */
static char *cur_path;
static uint64_t cur_size;
static int cur_known;           /* its loudness was in the database */
static uint32_t cur_rate;
static int16_t *cur_buf;        /* the buffer given by GetBuffer() */
static int32_t gain;            /* 0x10000 = 1.0 */
static int32_t gain_target, gain_step;
static long glide_left;         /* samples left in the glide */
static int gain_set;            /* the gain of a new file was set */
static double block_sum;
static unsigned int block_fill, block_length;
static double bin_sum[BINS];
static unsigned int bin_count[BINS];
static unsigned int heard_blocks; /* blocks above the absolute gate */
static int32_t peak;

static char *database_path (void)
{
	char *p = malloc (strlen (configAPI.DataHomePath) + strlen (DATABASE_NAME) + 1);
	if (p)
	{
		sprintf (p, "%s%s", configAPI.DataHomePath, DATABASE_NAME);
	}
	return p;
}

static void add_entry (const char *path, uint64_t size, int32_t loudness, int32_t pk)
{
	int i;
	for (i = 0; i < entrycount; i++)
	{
		if ((entries[i].size == size) && !strcmp (entries[i].path, path))
		{
			entries[i].loudness = loudness; /* a later line replaces an older one */
			entries[i].peak = pk;
			return;
		}
	}
	if (entrycount == entrysize)
	{
		struct entry_t *n = realloc (entries, (entrysize + 1024) * sizeof (*entries));
		if (!n)
		{
			return;
		}
		entries = n;
		entrysize += 1024;
	}
	if (!(entries[entrycount].path = strdup (path)))
	{
		return;
	}
	entries[entrycount].size = size;
	entries[entrycount].loudness = loudness;
	entries[entrycount].peak = pk;
	entrycount++;
}

/* lines: "<loudness in hundredths of dB> <highest sample> <file size> <path>" */
static void load_database (void)
{
	char *path = database_path ();
	struct osfile_t *f;
	uint64_t len;
	char *text, *line;

	loaded = 1;
	if (!path)
	{
		return;
	}
	f = osfile_open_readonly (path, 0);
	free (path);
	if (!f)
	{
		return;
	}
	len = osfile_getfilesize (f);
	if ((len < strlen (DATABASE_MAGIC)) || (len > 64 * 1024 * 1024) || !(text = malloc (len + 1)))
	{
		osfile_close (f);
		return;
	}
	if (osfile_read (f, text, len) != (int64_t)len)
	{
		free (text);
		osfile_close (f);
		return;
	}
	osfile_close (f);
	text[len] = 0;
	if (strncmp (text, DATABASE_MAGIC, strlen (DATABASE_MAGIC)))
	{
		free (text);
		return;
	}
	for (line = text + strlen (DATABASE_MAGIC); *line; )
	{
		char *end = strchr (line, '\n'), *p;
		long loudness, pk;
		unsigned long long size;
		if (end)
		{
			*end = 0;
		}
		loudness = strtol (line, &p, 10);
		if (*p == ' ')
		{
			pk = strtol (p + 1, &p, 10);
			if (*p == ' ')
			{
				size = strtoull (p + 1, &p, 10);
				if ((*p == ' ') && p[1] && (pk >= 1) && (pk <= 32768))
				{
					add_entry (p + 1, size, (int32_t)loudness, (int32_t)pk);
				}
			}
		}
		if (!end)
		{
			break;
		}
		line = end + 1;
	}
	free (text);
}

static void save_entry (const char *path, uint64_t size, int32_t loudness, int32_t pk)
{
	char *dbpath = database_path ();
	struct osfile_t *f;
	char head[64];
	if (!dbpath)
	{
		return;
	}
	f = osfile_open_readwrite (dbpath, 0, 0);
	free (dbpath);
	if (!f)
	{
		return;
	}
	if (!osfile_getfilesize (f))
	{
		osfile_write (f, DATABASE_MAGIC, strlen (DATABASE_MAGIC));
	} else {
		osfile_setpos (f, osfile_getfilesize (f));
	}
	snprintf (head, sizeof (head), "%ld %ld %llu ", (long)loudness, (long)pk, (unsigned long long)size);
	osfile_write (f, head, strlen (head));
	osfile_write (f, path, strlen (path));
	osfile_write (f, "\n", 1);
	osfile_close (f);
	add_entry (path, size, loudness, pk);
}

/* the loudness of what was measured, in dBFS; returns 0 if there was no sound */
static int measured (double *loudness)
{
	double sum = 0.0, gate;
	unsigned int count = 0;
	int i, first;
	for (i = 0; i < BINS; i++)
	{
		sum += bin_sum[i];
		count += bin_count[i];
	}
	if (!count)
	{
		return 0;
	}
	gate = 10.0 * log10 (sum / count) + RELATIVE_GATE;
	first = (int)((gate - BIN_LOW) / BIN_STEP);
	if (first < 0) first = 0;
	sum = 0.0;
	count = 0;
	for (i = first; i < BINS; i++)
	{
		sum += bin_sum[i];
		count += bin_count[i];
	}
	if (!count)
	{
		return 0;
	}
	*loudness = 10.0 * log10 (sum / count);
	return 1;
}

/* a boost never takes the highest sample measured above full scale */
static int32_t gain_for (double loudness, int32_t pk)
{
	double db = target - loudness, g;
	if (db > MAX_BOOST) db = MAX_BOOST;
	if (db < MAX_CUT) db = MAX_CUT;
	g = pow (10.0, db / 20.0);
	if ((g > 1.0) && (pk > 0) && (g * pk > 32767.0))
	{
		g = 32767.0 / pk;
		if (g < 1.0) g = 1.0;
	}
	return (int32_t)(g * 65536.0);
}

static void end_block (void)
{
	double ms = block_sum / block_fill;
	block_sum = 0.0;
	block_fill = 0;
	if (ms > 0.0)
	{
		double db = 10.0 * log10 (ms);
		if (db >= ABSOLUTE_GATE)
		{
			int bin = (int)((db - BIN_LOW) / BIN_STEP);
			if (bin < 0) bin = 0;
			if (bin >= BINS) bin = BINS - 1;
			bin_sum[bin] += ms;
			bin_count[bin]++;
			heard_blocks++;
		}
	}
	if (!gain_set && (heard_blocks * BLOCK_SECONDS >= FIRST_SECONDS))
	{
		double loudness;
		gain_set = 1;
		if (measured (&loudness))
		{
			long n = (long)(GLIDE_SECONDS * cur_rate);
			gain_target = gain_for (loudness, peak);
			gain_step = (gain_target - gain) / n;
			glide_left = gain_step ? n : 0;
			if (!glide_left)
			{
				gain = gain_target;
			}
		}
	}
}

static void process (int16_t *buf, unsigned int samples)
{
	unsigned int i;
	if (!cur_known)
	{ /* measure the sound as the file gives it */
		for (i = 0; i < samples; i++)
		{
			int32_t al = buf[i * 2] < 0 ? -buf[i * 2] : buf[i * 2];
			int32_t ar = buf[i * 2 + 1] < 0 ? -buf[i * 2 + 1] : buf[i * 2 + 1];
			double l = buf[i * 2] / 32768.0, r = buf[i * 2 + 1] / 32768.0;
			if (al > peak) peak = al;
			if (ar > peak) peak = ar;
			block_sum += (l * l + r * r) * 0.5;
			if (++block_fill == block_length)
			{
				end_block ();
			}
		}
	}
	if ((gain == 0x10000) && !glide_left)
	{
		return;
	}
	for (i = 0; i < samples * 2; i += 2)
	{
		int32_t l = (int32_t)(((int64_t)buf[i] * gain) >> 16);
		int32_t r = (int32_t)(((int64_t)buf[i + 1] * gain) >> 16);
		buf[i]     = (l > 32767) ? 32767 : (l < -32768) ? -32768 : l;
		buf[i + 1] = (r > 32767) ? 32767 : (r < -32768) ? -32768 : r;
		if (glide_left)
		{
			gain += gain_step;
			if (!--glide_left)
			{
				gain = gain_target;
			}
		}
	}
}

static int norm_Play (uint32_t *rate, enum plrRequestFormat *format, struct ocpfilehandle_t *source_file, struct cpifaceSessionAPI_t *cpifaceSession)
{
	int i, r = device->Play (rate, format, source_file, cpifaceSession);

	free (cur_path);
	cur_path = 0;
	cur_known = 0;
	cur_buf = 0;
	gain = 0x10000;
	glide_left = 0;
	gain_set = 0;
	block_sum = 0.0;
	block_fill = 0;
	heard_blocks = 0;
	peak = 0;
	memset (bin_sum, 0, sizeof (bin_sum));
	memset (bin_count, 0, sizeof (bin_count));
	if (!r)
	{
		return r;
	}
	cur_rate = *rate ? *rate : 44100;
	block_length = (unsigned int)(BLOCK_SECONDS * cur_rate);

	if (source_file)
	{
		dirdbGetFullname_malloc (source_file->dirdb_ref, &cur_path, DIRDB_FULLNAME_DRIVE);
		if (cur_path && strchr (cur_path, '\n'))
		{
			free (cur_path);
			cur_path = 0;
		}
		cur_size = source_file->filesize (source_file);
	}
	if (cur_path)
	{
		if (!loaded)
		{
			load_database ();
		}
		for (i = 0; i < entrycount; i++)
		{
			if ((entries[i].size == cur_size) && !strcmp (entries[i].path, cur_path))
			{
				cur_known = 1;
				gain = gain_for (entries[i].loudness / 100.0, entries[i].peak);
				break;
			}
		}
	}
	return r;
}

static void norm_GetBuffer (void **buf, unsigned int *samples)
{
	device->GetBuffer (buf, samples);
	cur_buf = *buf;
}

static void norm_CommitBuffer (unsigned int samples)
{
	if (cur_buf && samples)
	{
		process (cur_buf, samples);
	}
	cur_buf = 0;
	device->CommitBuffer (samples);
}

static void norm_Stop (struct cpifaceSessionAPI_t *cpifaceSession)
{
	double loudness;
	if (block_fill)
	{
		end_block ();
	}
	if (cur_path && !cur_known && (heard_blocks * BLOCK_SECONDS >= SAVE_SECONDS) && measured (&loudness))
	{
		save_entry (cur_path, cur_size, (int32_t)lrint (loudness * 100.0), peak ? peak : 1);
	}
	free (cur_path);
	cur_path = 0;
	cur_known = 0;
	device->Stop (cpifaceSession);
}

const struct plrDevAPI_t *cpiNormalizeWrap (const struct plrDevAPI_t *dev)
{
	if (!dev || !configAPI.GetProfileBool ("sound", "normalize", 1, 1))
	{
		return dev;
	}
	target = configAPI.GetProfileInt ("sound", "normalizetarget", -18, 10);
	if (target < -40.0) target = -40.0;
	if (target > -6.0) target = -6.0;
	device = dev;
	wrapped = *dev;
	wrapped.Play = norm_Play;
	wrapped.GetBuffer = norm_GetBuffer;
	wrapped.CommitBuffer = norm_CommitBuffer;
	wrapped.Stop = norm_Stop;
	return &wrapped;
}
