/* OpenCP Module Player
 * GSF (Game Boy Advance Sound Format) playback routines, using mGBA by Jeffrey Pfau.
 * Based on the S98 and GME playback routines by Stian Skjelstad.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "types.h"
#include "boot/psetting.h"
#include "cpiface/cpiface.h"
#include "dev/mcp.h"
#include "dev/player.h"
#include "dev/ringbuffer.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "stuff/err.h"
#include "stuff/imsrtns.h"
#include "gsfemu.h"
#include "gsfload.h"
#include "gsfplay.h"

#define MAX_FILE (32 * 1024 * 1024 + 64 * 1024) /* largest GBA cartridge, compressed, with its tags */

static struct gsfemu_t *gsfsession;
static struct gsf_tags_t gsftags;
static size_t gsfimagesize;
static int gsfmultiboot;

static int gsflooped; /* bit 0: the track ended, bit 1: the ringbuffer is empty */
static int doloop;    /* OCP loop mode: play forever, without fading out */

/* Track length: the "length" and "fade" tags of the file, otherwise the [gme]
 * defaultlength= and fade= settings of ocp.ini, as for the other game music formats.
 * The time counted is the emulated time, so that a change of speed keeps the
 * length of the track in the music. */
static int32_t gsf_length_ms; /* -1 = forever */
static int32_t gsf_fade_ms;
static double gsf_pos_ms;
static unsigned int gsf_speed = 0x100;

static unsigned long voll,volr;
static int bal;
static int vol;
static int pan;
static int srnd;

static int16_t *gsfbuf;     /* the buffer */
static struct ringbuffer_t *gsfbufpos = 0;
static uint32_t gsfbuffpos; /* read fine-pos.. */
static uint32_t gsfbufrate; /* re-sampling rate.. fixed point 0x10000 => 1.0 */
static uint32_t gsfRate;

/* ---- libraries of MINIGSF files, read from the directory of the file ---- */

struct libsearch_t
{
	const struct dirdbAPI_t *dirdb;
	const char *name;
	struct ocpfile_t *found;
};

static void libsearch_file (void *token, struct ocpfile_t *file)
{
	struct libsearch_t *s = token;
	const char *name;
	if (s->found)
	{
		return;
	}
	s->dirdb->GetName_internalstr (file->dirdb_ref, &name);
	if (!strcasecmp (name, s->name))
	{
		file->ref (file);
		s->found = file;
	}
}

static void libsearch_dir (void *token, struct ocpdir_t *dir)
{
	(void)token; (void)dir;
}

struct libreader_t
{
	struct ocpfilehandle_t *file;
	struct cpifaceSessionAPI_t *cpifaceSession;
};

/* gsf_readfile_t: the file "name" in the directory (or archive) of the file played.
 * The name is first looked up as written, then without regard to case, since the
 * names in the tags do not always match the case of the files. */
static uint8_t *gsf_readlib (void *ctx, const char *name, size_t *size)
{
	struct libreader_t *r = ctx;
	struct ocpdir_t *dir = r->file->origin ? r->file->origin->parent : 0;
	struct ocpfile_t *f;
	struct ocpfilehandle_t *h;
	uint64_t len;
	uint8_t *data = 0;

	if (!dir || strchr (name, '/') || strchr (name, '\\'))
	{
		return 0;
	}
	f = ocpdir_readdir_file (dir, name, r->cpifaceSession->dirdb);
	if (!f)
	{
		struct libsearch_t s;
		ocpdirhandle_pt dh;
		s.dirdb = r->cpifaceSession->dirdb;
		s.name = name;
		s.found = 0;
		if ((dh = dir->readdir_start (dir, libsearch_file, libsearch_dir, &s)))
		{
			while (dir->readdir_iterate (dh) && !s.found)
			{
			}
			dir->readdir_cancel (dh);
		}
		f = s.found;
	}
	if (!f)
	{
		return 0;
	}
	h = f->open (f);
	f->unref (f);
	if (!h)
	{
		return 0;
	}
	len = h->filesize (h);
	if ((len >= 16) && (len <= MAX_FILE) && (data = malloc (len)))
	{
		h->seek_set (h, 0);
		if (h->read (h, data, len) != (int)len)
		{
			free (data);
			data = 0;
		}
		*size = len;
	}
	h->unref (h);
	return data;
}

/* ---- playback ---- */

#define PANPROC \
do { \
	float _rs = rs, _ls = ls; \
	if(pan==-64) \
	{ \
		float t=_ls; \
		_ls = _rs; \
		_rs = t; \
	} else if(pan==64) \
	{ \
	} else if(pan==0) \
		_rs=_ls=(_rs+_ls) / 2.0; \
	else if(pan<0) \
	{ \
		_ls = _ls / (-pan/-64.0+2.0) + _rs*(64.0+pan)/128.0; \
		_rs = _rs / (-pan/-64.0+2.0) + _ls*(64.0+pan)/128.0; \
	} else if(pan<64) \
	{ \
		_ls = _ls / (pan/-64.0+2.0) + _rs*(64.0-pan)/128.0; \
		_rs = _rs / (pan/-64.0+2.0) + _ls*(64.0-pan)/128.0; \
	} \
	rs = _rs * volr / 256.0; \
	ls = _ls * voll / 256.0; \
	if (srnd) \
	{ \
		ls ^= 0xffff; \
	} \
} while(0)

/* volume of the fade-out at the current position, 0x10000 = full */
static uint32_t gsf_fade_gain (void)
{
	double end;
	if (doloop || (gsf_length_ms < 0) || (gsf_pos_ms < gsf_length_ms))
	{
		return 0x10000;
	}
	end = (double)gsf_length_ms + gsf_fade_ms;
	if (gsf_pos_ms >= end)
	{
		return 0;
	}
	return (uint32_t)((end - gsf_pos_ms) * 0x10000 / gsf_fade_ms);
}

static int gsf_finished (void)
{
	return !doloop && (gsf_length_ms >= 0) && (gsf_pos_ms >= (double)gsf_length_ms + gsf_fade_ms);
}

/* fills the ringbuffer, a slice at a time so that the fade-out stays smooth */
static void gsfIdler (struct cpifaceSessionAPI_t *cpifaceSession)
{
	int pos1, pos2;
	int length1, length2;

	cpifaceSession->ringbufferAPI->get_head_samples (gsfbufpos, &pos1, &length1, &pos2, &length2);

	while (length1)
	{
		int16_t *p = gsfbuf + (pos1 << 1);
		uint32_t gain;
		if (gsf_finished ())
		{
			gsflooped |= 1;
			return;
		}
		if (length1 > 256)
		{
			length1 = 256;
		}
		gsfemu_render (gsfsession, p, length1);
		if ((gain = gsf_fade_gain ()) != 0x10000)
		{
			int i;
			for (i = 0; i < (length1 << 1); i++)
			{
				p[i] = (int16_t)(((int32_t)p[i] * (int32_t)(gain >> 1)) >> 15);
			}
		}
		gsf_pos_ms += (double)length1 * 1000.0 * gsf_speed / 0x100 / gsfRate;

		cpifaceSession->ringbufferAPI->head_add_samples (gsfbufpos, length1);
		cpifaceSession->ringbufferAPI->get_head_samples (gsfbufpos, &pos1, &length1, &pos2, &length2);
	}
}

OCP_INTERNAL void gsfIdle (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->InPause || (gsflooped == 3))
	{
		cpifaceSession->plrDevAPI->Pause (1);
	} else {
		void *targetbuf;
		unsigned int targetlength; /* in samples */

		cpifaceSession->plrDevAPI->Pause (0);

		cpifaceSession->plrDevAPI->GetBuffer (&targetbuf, &targetlength);

		if (targetlength)
		{
			int16_t *t = targetbuf;
			unsigned int accumulated_target = 0;
			unsigned int accumulated_source = 0;
			int pos1, length1, pos2, length2;

			gsfIdler (cpifaceSession);

			/* how much data is available.. we are using a ringbuffer, so we might receive two fragments */
			cpifaceSession->ringbufferAPI->get_tail_samples (gsfbufpos, &pos1, &length1, &pos2, &length2);

			if (gsfbufrate==0x10000)
			{
				if (targetlength>(length1+length2))
				{
					targetlength=(length1+length2); // limiting targetlength here, saves us from doing this per sample later
					gsflooped |= 2;
				} else {
					gsflooped &= ~2;
				}

				// limit source to not overrun target buffer
				if (length1 > targetlength)
				{
					length1 = targetlength;
					length2 = 0;
				} else if ((length1 + length2) > targetlength)
				{
					length2 = targetlength - length1;
				}

				accumulated_source = accumulated_target = length1 + length2;

				while (length1)
				{
					while (length1)
					{
						int16_t rs, ls;

						rs = gsfbuf[(pos1<<1) + 0];
						ls = gsfbuf[(pos1<<1) + 1];

						PANPROC;

						*(t++) = rs;
						*(t++) = ls;

						pos1++;
						length1--;

						//accumulated_target++;
					}
					length1 = length2;
					length2 = 0;
					pos1 = pos2;
					pos2 = 0;
				}
				//accumulated_source = accumulated_target;
			} else {
				/* We are going to perform cubic interpolation of rate conversion... this bit is tricky */
				gsflooped &= ~2;

				while (targetlength && length1)
				{
					while (targetlength && length1)
					{
						uint32_t wpm1, wp0, wp1, wp2;
						int32_t rc0, rc1, rc2, rc3, rvm1,rv1,rv2;
						int32_t lc0, lc1, lc2, lc3, lvm1,lv1,lv2;
						unsigned int progress;
						int16_t rs, ls;

						/* will the interpolation overflow? */
						if ((length1+length2) <= 3)
						{
							gsflooped |= 2;
							break;
						}
						/* will we overflow the wavebuf if we advance? */
						if ((length1+length2) < ((gsfbufrate+gsfbuffpos)>>16))
						{
							gsflooped |= 2;
							break;
						}

						switch (length1) /* if we are close to the wrap between buffer segment 1 and 2, len1 will grow down to a small number */
						{
							case 1:  wpm1 = pos1; wp0 = pos2;     wp1 = pos2 + 1; wp2 = pos2 + 2; break;
							case 2:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos2;     wp2 = pos2 + 1; break;
							case 3:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos2;     break;
							default: wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos1 + 3; break;
						}

						rvm1 = (uint16_t)gsfbuf[(wpm1<<1)+0]^0x8000; /* we temporary need data to be unsigned - hence the ^0x8000 */
						lvm1 = (uint16_t)gsfbuf[(wpm1<<1)+1]^0x8000;
						 rc0 = (uint16_t)gsfbuf[(wp0 <<1)+0]^0x8000;
						 lc0 = (uint16_t)gsfbuf[(wp0 <<1)+1]^0x8000;
						 rv1 = (uint16_t)gsfbuf[(wp1 <<1)+0]^0x8000;
						 lv1 = (uint16_t)gsfbuf[(wp1 <<1)+1]^0x8000;
						 rv2 = (uint16_t)gsfbuf[(wp2 <<1)+0]^0x8000;
						 lv2 = (uint16_t)gsfbuf[(wp2 <<1)+1]^0x8000;

						rc1 = rv1-rvm1;
						rc2 = 2*rvm1-2*rc0+rv1-rv2;
						rc3 = rc0-rvm1-rv1+rv2;
						rc3 =  imulshr16(rc3,gsfbuffpos);
						rc3 += rc2;
						rc3 =  imulshr16(rc3,gsfbuffpos);
						rc3 += rc1;
						rc3 =  imulshr16(rc3,gsfbuffpos);
						rc3 += rc0;
						if (rc3<0)
							rc3=0;
						if (rc3>65535)
							rc3=65535;

						lc1 = lv1-lvm1;
						lc2 = 2*lvm1-2*lc0+lv1-lv2;
						lc3 = lc0-lvm1-lv1+lv2;
						lc3 =  imulshr16(lc3,gsfbuffpos);
						lc3 += lc2;
						lc3 =  imulshr16(lc3,gsfbuffpos);
						lc3 += lc1;
						lc3 =  imulshr16(lc3,gsfbuffpos);
						lc3 += lc0;
						if (lc3<0)
							lc3=0;
						if (lc3>65535)
							lc3=65535;

						rs = rc3 ^ 0x8000;
						ls = lc3 ^ 0x8000;

						PANPROC;

						*(t++) = rs;
						*(t++) = ls;

						gsfbuffpos+=gsfbufrate;
						progress = gsfbuffpos>>16;
						gsfbuffpos &= 0xffff;
						accumulated_source+=progress;
						pos1+=progress;
						length1-=progress;
						targetlength--;
						if (length1 < 0)
						{
							length2 += length1;
							length1 = 0;
						}

						accumulated_target++;
					} /* while (targetlength && length1) */
					length1 = length2;
					length2 = 0;
					pos1 = pos2;
					pos2 = 0;
				} /* while (targetlength && length1) */
			} /* if (gsfbufrate==0x10000) */
			cpifaceSession->ringbufferAPI->tail_consume_samples (gsfbufpos, accumulated_source);
			cpifaceSession->plrDevAPI->CommitBuffer (accumulated_target);
		} /* if (targetlength) */
	}

	cpifaceSession->plrDevAPI->Idle();

}



static void gsfSetSpeed (uint16_t sp)
{
	if (sp < 16)
		sp = 16;
	gsf_speed = sp;
	gsfemu_set_speed (gsfsession, sp);
}

static void gsfSetPitch (uint16_t sp)
{
	if (sp < 4)
		sp = 4;
	gsfbufrate = 256 * sp;
}

static void gsfSetVolume (void)
{
	volr = voll = vol * 4;
	if (bal < 0)
	{
		voll = (voll * (64 + bal)) >> 6;
	} else {
		volr = (volr * (64 - bal)) >> 6;
	}
}

static void gsfSet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt, int val)
{
	switch (opt)
	{
		case mcpMasterSpeed:
			gsfSetSpeed(val);
			break;
		case mcpMasterPitch:
			gsfSetPitch(val);
			break;
		case mcpMasterSurround:
			srnd=val;
			break;
		case mcpMasterPanning:
			pan=val;
			gsfSetVolume();
			break;
		case mcpMasterVolume:
			vol=val;
			gsfSetVolume();
			break;
		case mcpMasterBalance:
			bal=val;
			gsfSetVolume();
			break;
	}
}

static int gsfGet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt)
{
	return 0;
}

OCP_INTERNAL void gsfSetMute (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int mute)
{
	cpifaceSession->MuteChannel[ch] = mute;
	if (gsfsession)
	{
		gsfemu_mute (gsfsession, ch, mute);
	}
}

static const char *gsf_load_error (int r)
{
	switch (r)
	{
		case GSF_ERR_LIBRARY: return "library missing or invalid";
		case GSF_ERR_MEMORY:  return "out of memory";
		default:              return "not a valid GSF file";
	}
}

OCP_INTERNAL int gsfOpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession)
{
	enum plrRequestFormat format;
	struct gsf_image_t image;
	struct libreader_t reader;
	char missing[128];
	unsigned char *data;
	uint64_t datalen;
	uint32_t gsfbuflen;
	int r, cfg_length, cfg_fade;

	assert (!gsfsession);

	if (!cpifaceSession->plrDevAPI)
	{
		return errPlay;
	}

	datalen = file->filesize(file);
	if ((datalen < 16) || (datalen > MAX_FILE))
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[GSF] File too small or too big\n");
		return errFormStruc;
	}
	data = malloc (datalen);
	if (!data)
	{
		return errAllocMem;
	}
	file->seek_set (file, 0);
	if (file->read (file, data, datalen) != datalen)
	{
		free (data);
		return errFileRead;
	}

	gsf_read_tags (data, datalen, &gsftags);
	reader.file = file;
	reader.cpifaceSession = cpifaceSession;
	r = gsf_load (data, datalen, gsf_readlib, &reader, &image, missing, sizeof (missing));
	free (data);
	data = 0;
	if (r)
	{
		if (missing[0])
		{
			cpifaceSession->cpiDebug (cpifaceSession, "[GSF] %s: %s\n", missing, gsf_load_error (r));
		} else {
			cpifaceSession->cpiDebug (cpifaceSession, "[GSF] %s\n", gsf_load_error (r));
		}
		return (r == GSF_ERR_MEMORY) ? errAllocMem : errFormStruc;
	}
	gsfimagesize = image.size;
	gsfmultiboot = image.multiboot;

	/* the same settings as for the other game music formats (NSF, GBS and others) */
	cfg_length = cpifaceSession->configAPI->GetProfileInt ("gme", "defaultlength", 150, 10);
	cfg_fade   = cpifaceSession->configAPI->GetProfileInt ("gme", "fade", 8, 10);
	if (cfg_length < 0) cfg_length = 0;
	if (cfg_length > 36000) cfg_length = 36000;
	if (cfg_fade < 0) cfg_fade = 0;
	if (cfg_fade > 60) cfg_fade = 60;
	if (gsftags.length_ms > 0)
	{
		gsf_length_ms = gsftags.length_ms;
	} else {
		gsf_length_ms = cfg_length ? cfg_length * 1000 : -1;
	}
	gsf_fade_ms = (gsftags.fade_ms >= 0) ? gsftags.fade_ms : cfg_fade * 1000;

	gsfRate=0;
	format=PLR_STEREO_16BIT_SIGNED;
	if (!cpifaceSession->plrDevAPI->Play (&gsfRate, &format, file, cpifaceSession))
	{
		free (image.data);
		return errPlay;
	}

	gsfsession = gsfemu_open (image.data, image.size, image.multiboot, gsfRate);
	free (image.data);
	if (!gsfsession)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[GSF] mGBA failed to load the program\n");
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		return errFormMiss;
	}

	gsfbuflen = gsfRate >> 4;
	gsflooped = 0;
	gsf_pos_ms = 0.0;
	gsf_speed = 0x100;
	gsfbuf = malloc(gsfbuflen <<  2 /* stereo + 16bit */);
	if (!gsfbuf)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		gsfemu_close (gsfsession);
		gsfsession = 0;
		return errAllocMem;
	}

	gsfbufpos = cpifaceSession->ringbufferAPI->new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED, gsfbuflen);
	if (!gsfbufpos)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		free (gsfbuf);
		gsfbuf = 0;
		gsfemu_close (gsfsession);
		gsfsession = 0;
		return errAllocMem;
	}
	gsfbuffpos=0;

	cpifaceSession->mcpSet = gsfSet;
	cpifaceSession->mcpGet = gsfGet;

	cpifaceSession->Normalize (cpifaceSession, mcpNormalizeCanSpeedPitchUnlock);

	return errOk;
}

OCP_INTERNAL void gsfClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->plrDevAPI)
	{
		cpifaceSession->plrDevAPI->Stop(cpifaceSession);
	}

	if (gsfsession)
	{
		gsfemu_close (gsfsession);
		gsfsession = 0;
	}

	if (gsfbufpos)
	{
		cpifaceSession->ringbufferAPI->free (gsfbufpos);
		gsfbufpos = 0;
	}

	free(gsfbuf);
	gsfbuf = 0;
}

OCP_INTERNAL void gsfSetLoop (int s)
{
	doloop = !!s;
}

OCP_INTERNAL int gsfIsLooped (void)
{
	return gsflooped == 3;
}

OCP_INTERNAL void gsfRestart (void)
{
	if (gsfsession)
	{
		gsfemu_restart (gsfsession);
		gsf_pos_ms = 0.0;
		gsflooped = 0;
	}
}

OCP_INTERNAL void gsfGetInfo (struct gsfinfo_t *info)
{
	info->tags = &gsftags;
	info->length_ms = gsf_length_ms;
	info->fade_ms = gsf_fade_ms;
	info->position_ms = gsf_pos_ms;
	info->imagesize = gsfimagesize;
	info->multiboot = gsfmultiboot;
}
