/* OpenCP Module Player
 * Game Boy Advance emulation for the GSF player, using mGBA (see gsfemu.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <mgba/core/blip_buf.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/version.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/gba.h>
#include <mgba-util/vfs.h>
/* the OCP headers come after the mGBA ones, which include system headers of their own */
#include "config.h"
#include "types.h"
#include "gsfemu.h"

/* mGBA stops adding samples to its buffer when it holds this many; one emulated
 * frame gives about rate / 60 samples, and the buffer is emptied after each frame */
#define AUDIO_BUFFER 8192

struct gsfemu_t
{
	struct mCore *core;
	blip_t *left, *right;
	uint32_t rate;
	unsigned int speed;
};

static void quiet_log (struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
	(void)logger; (void)category; (void)level; (void)format; (void)args;
}

static struct mLogger quiet_logger = { .log = quiet_log, .filter = 0 };

static void set_rates (struct gsfemu_t *e)
{
	double out = (double)e->rate * 0x100 / e->speed; /* fewer samples per emulated second = faster */
	blip_set_rates (e->left,  e->core->frequency (e->core), out);
	blip_set_rates (e->right, e->core->frequency (e->core), out);
}

struct gsfemu_t *gsfemu_open (const uint8_t *image, size_t size, int multiboot, uint32_t rate)
{
	struct gsfemu_t *e = calloc (1, sizeof (*e));
	struct VFile *vf;
	if (!e)
	{
		return 0;
	}
	mLogSetDefaultLogger (&quiet_logger);
	e->core = GBACoreCreate ();
	if (!e->core || !e->core->init (e->core))
	{
		free (e->core); /* init failed: nothing else was allocated */
		free (e);
		return 0;
	}
	mCoreInitConfig (e->core, 0);
	e->core->opts.useBios = false;   /* mGBA emulates the BIOS calls itself */
	e->core->opts.skipBios = true;   /* start the program directly */

	vf = VFileMemChunk (image, size); /* mGBA keeps and frees this copy */
	if (!vf)
	{
		gsfemu_close (e);
		return 0;
	}
	/* The entry point of the GSF file tells whether the program runs from the work RAM
	 * (multiboot, without a cartridge) or from the cartridge. core->loadROM() is not
	 * used: it guesses multiboot from the size and the first instructions, and small
	 * GSF programs can look like multiboot programs. */
	if (!(multiboot ? GBALoadMB (e->core->board, vf) : GBALoadROM (e->core->board, vf)))
	{
		vf->close (vf);
		gsfemu_close (e);
		return 0;
	}

	e->core->setAudioBufferSize (e->core, AUDIO_BUFFER);
	e->left = e->core->getAudioChannel (e->core, 0);
	e->right = e->core->getAudioChannel (e->core, 1);
	e->rate = rate;
	e->speed = 0x100;
	e->core->reset (e->core);
	set_rates (e);
	return e;
}

void gsfemu_close (struct gsfemu_t *e)
{
	if (!e)
	{
		return;
	}
	if (e->core)
	{
		mCoreConfigDeinit (&e->core->config);
		e->core->deinit (e->core);
	}
	free (e);
}

void gsfemu_restart (struct gsfemu_t *e)
{
	e->core->reset (e->core);
	blip_clear (e->left);
	blip_clear (e->right);
	set_rates (e);
}

void gsfemu_render (struct gsfemu_t *e, int16_t *buf, unsigned int frames)
{
	while (frames)
	{
		int avail = blip_samples_avail (e->left);
		if (!avail)
		{
			e->core->runFrame (e->core);
			continue;
		}
		if ((unsigned int)avail > frames)
		{
			avail = frames;
		}
		blip_read_samples (e->left,  buf,     avail, 1); /* 1 = stereo: every other sample */
		blip_read_samples (e->right, buf + 1, avail, 1);
		buf += avail * 2;
		frames -= avail;
	}
}

void gsfemu_set_speed (struct gsfemu_t *e, unsigned int speed)
{
	if (speed < 16)
	{
		speed = 16;
	}
	e->speed = speed;
	set_rates (e);
}

void gsfemu_mute (struct gsfemu_t *e, int channel, int mute)
{
	if ((channel >= 0) && (channel < GSFEMU_CHANNELS))
	{
		e->core->enableAudioChannel (e->core, channel, !mute);
	}
}

const char *gsfemu_version (void)
{
	return projectVersion;
}
