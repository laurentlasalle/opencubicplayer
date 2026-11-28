/* OpenCP Module Player
 * vgmstream playback routines: audio streams of games (GENH, ADX, BRSTM, HCA and
 * hundreds of other formats). Based on the WAV, S98 and GME playback routines by
 * Stian Skjelstad.
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
#include <libvgmstream.h>
#include <libvgmstream_streamfile.h>
#include "types.h"
#include "boot/psetting.h"
#include "cpiface/cpiface.h"
#include "dev/mcp.h"
#include "dev/player.h"
#include "dev/ringbuffer.h"
#include "filesel/filesystem.h"
#include "stuff/err.h"
#include "stuff/imsrtns.h"
#include "vgmsplay.h"

static libvgmstream_t *vgms;              /* the decoder, one stream (subsong) open at a time */
static struct ocpfilehandle_t *vgmsfile;  /* kept to open other subsongs */
static const struct dirdbAPI_t *vgmsdirdb;
static libvgmstream_config_t vgmscfg;
static int vgmssubsong;                   /* 1..vgmssubsongs */
static int vgmssubsongs;
static int vgmschannels;                  /* channels of the file */

static int vgmslooped; /* bit 0: the stream ended, bit 1: the ringbuffer is empty */
static int doloop;     /* OCP loop mode: start the stream again when it ends */

static unsigned long voll,volr;
static int bal;
static int vol;
static int pan;
static int srnd;

static int16_t *vgmsbuf;     /* the buffer, stereo */
static struct ringbuffer_t *vgmsbufpos = 0;
static uint32_t vgmsbuffpos; /* read fine-pos.. */
static uint32_t vgmsbufrate; /* re-sampling rate.. fixed point 0x10000 => 1.0 */
static uint32_t vgmsRate;    /* sample rate of the sound device */
static uint32_t vgmsfilerate;/* sample rate of the stream */
static uint16_t vgmsspeed = 256;
static int16_t *vgmsmono;    /* decoding buffer for mono streams */
static int vgmsplaying;      /* the sound device was started (plrDevAPI->Play) */

#define DECODE_SLICE 1024    /* samples decoded at a time */

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

static void vgmsUpdateRate (void)
{
	vgmsbufrate = imuldiv (256 * vgmsspeed, vgmsfilerate, vgmsRate);
}

/* fills the ringbuffer with decoded samples (stereo, at the rate of the stream) */
static void vgmsIdler (struct cpifaceSessionAPI_t *cpifaceSession)
{
	int pos1, pos2;
	int length1, length2;

	cpifaceSession->ringbufferAPI->get_head_samples (vgmsbufpos, &pos1, &length1, &pos2, &length2);

	while (length1)
	{
		int16_t *p = vgmsbuf + (pos1 << 1);
		if (vgms->decoder->done)
		{
			if (!doloop)
			{
				vgmslooped |= 1;
				return;
			}
			libvgmstream_reset (vgms); /* loop mode: the stream starts again */
		}
		if (length1 > DECODE_SLICE)
		{
			length1 = DECODE_SLICE;
		}
		if (vgms->format->channels == 1)
		{ /* mono: decoded aside, then given to both sides */
			int i;
			if (libvgmstream_fill (vgms, vgmsmono, length1) < 0)
			{
				vgmslooped |= 1;
				return;
			}
			for (i = 0; i < length1; i++)
			{
				p[i * 2] = p[i * 2 + 1] = vgmsmono[i];
			}
		} else if (libvgmstream_fill (vgms, p, length1) < 0)
		{ /* stereo (more channels are mixed down to stereo by vgmstream) */
			vgmslooped |= 1;
			return;
		}

		cpifaceSession->ringbufferAPI->head_add_samples (vgmsbufpos, length1);
		cpifaceSession->ringbufferAPI->get_head_samples (vgmsbufpos, &pos1, &length1, &pos2, &length2);
	}
}

OCP_INTERNAL void vgmsIdle (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->InPause || (vgmslooped == 3))
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

			vgmsIdler (cpifaceSession);

			/* how much data is available.. we are using a ringbuffer, so we might receive two fragments */
			cpifaceSession->ringbufferAPI->get_tail_samples (vgmsbufpos, &pos1, &length1, &pos2, &length2);

			if (vgmsbufrate==0x10000)
			{
				if (targetlength>(length1+length2))
				{
					targetlength=(length1+length2); // limiting targetlength here, saves us from doing this per sample later
					vgmslooped |= 2;
				} else {
					vgmslooped &= ~2;
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

						rs = vgmsbuf[(pos1<<1) + 0];
						ls = vgmsbuf[(pos1<<1) + 1];

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
				vgmslooped &= ~2;

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
							vgmslooped |= 2;
							break;
						}
						/* will we overflow the wavebuf if we advance? */
						if ((length1+length2) < ((vgmsbufrate+vgmsbuffpos)>>16))
						{
							vgmslooped |= 2;
							break;
						}

						switch (length1) /* if we are close to the wrap between buffer segment 1 and 2, len1 will grow down to a small number */
						{
							case 1:  wpm1 = pos1; wp0 = pos2;     wp1 = pos2 + 1; wp2 = pos2 + 2; break;
							case 2:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos2;     wp2 = pos2 + 1; break;
							case 3:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos2;     break;
							default: wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos1 + 3; break;
						}

						rvm1 = (uint16_t)vgmsbuf[(wpm1<<1)+0]^0x8000; /* we temporary need data to be unsigned - hence the ^0x8000 */
						lvm1 = (uint16_t)vgmsbuf[(wpm1<<1)+1]^0x8000;
						 rc0 = (uint16_t)vgmsbuf[(wp0 <<1)+0]^0x8000;
						 lc0 = (uint16_t)vgmsbuf[(wp0 <<1)+1]^0x8000;
						 rv1 = (uint16_t)vgmsbuf[(wp1 <<1)+0]^0x8000;
						 lv1 = (uint16_t)vgmsbuf[(wp1 <<1)+1]^0x8000;
						 rv2 = (uint16_t)vgmsbuf[(wp2 <<1)+0]^0x8000;
						 lv2 = (uint16_t)vgmsbuf[(wp2 <<1)+1]^0x8000;

						rc1 = rv1-rvm1;
						rc2 = 2*rvm1-2*rc0+rv1-rv2;
						rc3 = rc0-rvm1-rv1+rv2;
						rc3 =  imulshr16(rc3,vgmsbuffpos);
						rc3 += rc2;
						rc3 =  imulshr16(rc3,vgmsbuffpos);
						rc3 += rc1;
						rc3 =  imulshr16(rc3,vgmsbuffpos);
						rc3 += rc0;
						if (rc3<0)
							rc3=0;
						if (rc3>65535)
							rc3=65535;

						lc1 = lv1-lvm1;
						lc2 = 2*lvm1-2*lc0+lv1-lv2;
						lc3 = lc0-lvm1-lv1+lv2;
						lc3 =  imulshr16(lc3,vgmsbuffpos);
						lc3 += lc2;
						lc3 =  imulshr16(lc3,vgmsbuffpos);
						lc3 += lc1;
						lc3 =  imulshr16(lc3,vgmsbuffpos);
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

						vgmsbuffpos+=vgmsbufrate;
						progress = vgmsbuffpos>>16;
						vgmsbuffpos &= 0xffff;
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
			} /* if (vgmsbufrate==0x10000) */
			cpifaceSession->ringbufferAPI->tail_consume_samples (vgmsbufpos, accumulated_source);
			cpifaceSession->plrDevAPI->CommitBuffer (accumulated_target);
		} /* if (targetlength) */
	}

	cpifaceSession->plrDevAPI->Idle();

}



static void vgmsSetSpeed (uint16_t sp)
{
	if (sp < 4)
		sp = 4;
	vgmsspeed = sp;
	vgmsUpdateRate ();
}

static void vgmsSetVolume (void)
{
	volr = voll = vol * 4;
	if (bal < 0)
	{
		voll = (voll * (64 + bal)) >> 6;
	} else {
		volr = (volr * (64 - bal)) >> 6;
	}
}

static void vgmsSet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt, int val)
{
	switch (opt)
	{
		case mcpMasterSpeed: /* recorded sound: the speed changes the pitch too */
			vgmsSetSpeed(val);
			break;
		case mcpMasterPitch:
			break;
		case mcpMasterSurround:
			srnd=val;
			break;
		case mcpMasterPanning:
			pan=val;
			vgmsSetVolume();
			break;
		case mcpMasterVolume:
			vol=val;
			vgmsSetVolume();
			break;
		case mcpMasterBalance:
			bal=val;
			vgmsSetVolume();
			break;
	}
}

static int vgmsGet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt)
{
	return 0;
}

static void vgms_log (int level, const char *str)
{
	(void)level; (void)str; /* vgmstream reports format problems on stdout by default */
}

/* opens a subsong (1..N, or 0 for the default one) of the file */
static int vgms_open_subsong (int subsong)
{
	struct libstreamfile_t *sf = vgms_streamfile (vgmsfile, vgmsdirdb);
	int r;
	if (!sf)
	{
		return -1;
	}
	libvgmstream_setup (vgms, &vgmscfg);
	r = libvgmstream_open_stream (vgms, sf, subsong);
	libstreamfile_close (sf); /* vgmstream keeps its own references to the file */
	if (r < 0)
	{
		return -1;
	}
	vgmssubsongs = (vgms->format->subsong_count > 0) ? vgms->format->subsong_count : 1;
	vgmssubsong = (vgms->format->subsong_index > 0) ? vgms->format->subsong_index : 1;
	vgmschannels = vgms->format->input_channels ? vgms->format->input_channels : vgms->format->channels;
	vgmsfilerate = vgms->format->sample_rate;
	vgmslooped = 0;
	return 0;
}

OCP_INTERNAL int vgmsOpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession)
{
	enum plrRequestFormat format;
	uint32_t buflen;
	int loops, fade;

	assert (!vgms);

	if (!cpifaceSession->plrDevAPI)
	{
		return errPlay;
	}

	libvgmstream_set_log (LIBVGMSTREAM_LOG_LEVEL_NONE, vgms_log);

	/* the loop settings of the other game music formats (see ocp.ini, [gme]) */
	loops = cpifaceSession->configAPI->GetProfileInt ("gme", "loops", 2, 10);
	fade = cpifaceSession->configAPI->GetProfileInt ("gme", "fade", 8, 10);
	if (loops < 1) loops = 1;
	if (loops > 10) loops = 10;
	if (fade < 0) fade = 0;
	if (fade > 60) fade = 60;
	memset (&vgmscfg, 0, sizeof (vgmscfg));
	vgmscfg.loop_count = loops;
	vgmscfg.fade_time = fade;
	vgmscfg.force_sfmt = LIBVGMSTREAM_SFMT_PCM16;
	vgmscfg.auto_downmix_channels = 2; /* OCP plays stereo */

	if (!(vgms = libvgmstream_init ()))
	{
		return errAllocMem;
	}
	vgmsfile = file;
	file->ref (file);
	vgmsdirdb = cpifaceSession->dirdb;
	if (vgms_open_subsong (0))
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[vgmstream] format not recognized\n");
		vgmsClosePlayer (cpifaceSession);
		return errFormStruc;
	}

	vgmsRate=0;
	format=PLR_STEREO_16BIT_SIGNED;
	if (!cpifaceSession->plrDevAPI->Play (&vgmsRate, &format, file, cpifaceSession))
	{
		vgmsClosePlayer (cpifaceSession);
		return errPlay;
	}
	vgmsplaying = 1;
	vgmsspeed = 256;
	vgmsUpdateRate ();

	/* about 1/8 s of the stream (at least 1/16 s at the device rate) */
	buflen = vgmsfilerate >> 3;
	if (buflen < (vgmsRate >> 4))
	{
		buflen = vgmsRate >> 4;
	}
	vgmsbuf = malloc (buflen << 2 /* stereo + 16bit */);
	vgmsmono = malloc (DECODE_SLICE * sizeof (int16_t));
	vgmsbufpos = cpifaceSession->ringbufferAPI->new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED, buflen);
	if (!vgmsbuf || !vgmsmono || !vgmsbufpos)
	{
		vgmsClosePlayer (cpifaceSession);
		return errAllocMem;
	}
	vgmsbuffpos=0;

	cpifaceSession->mcpSet = vgmsSet;
	cpifaceSession->mcpGet = vgmsGet;

	cpifaceSession->Normalize (cpifaceSession, mcpNormalizeDefaultPlayP);

	return errOk;
}

OCP_INTERNAL void vgmsClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (vgmsplaying)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		vgmsplaying = 0;
	}
	if (vgmsbufpos)
	{
		cpifaceSession->ringbufferAPI->free (vgmsbufpos);
		vgmsbufpos = 0;
	}
	if (vgms)
	{
		libvgmstream_free (vgms);
		vgms = 0;
	}
	if (vgmsfile)
	{
		vgmsfile->unref (vgmsfile);
		vgmsfile = 0;
	}
	free (vgmsbuf);
	vgmsbuf = 0;
	free (vgmsmono);
	vgmsmono = 0;
}

OCP_INTERNAL void vgmsSetLoop (int s)
{
	doloop = !!s;
}

OCP_INTERNAL int vgmsIsLooped (void)
{
	return vgmslooped == 3;
}

OCP_INTERNAL int vgmsStartSubsong (struct cpifaceSessionAPI_t *cpifaceSession, int subsong)
{
	if (!vgms || (subsong < 1) || (subsong > vgmssubsongs))
	{
		return -1;
	}
	if (vgms_open_subsong (subsong))
	{
		return -1;
	}
	vgmsUpdateRate (); /* subsongs can have different sample rates */
	return 0;
}

OCP_INTERNAL void vgmsGetInfo (struct vgmsinfo_t *info)
{
	const libvgmstream_format_t *f = vgms->format;
	info->subsong = vgmssubsong;
	info->subsongs = vgmssubsongs;
	info->channels = vgmschannels;
	info->rate = f->sample_rate;
	info->samples = f->stream_samples;
	info->play_samples = f->play_samples;
	info->position = libvgmstream_get_play_position (vgms);
	info->loop = f->loop_flag;
	info->loop_start = f->loop_start;
	info->loop_end = f->loop_end;
	info->bitrate = f->stream_bitrate;
	info->codec = f->codec_name;
	info->layout = f->layout_name;
	info->meta = f->meta_name;
	info->name = f->stream_name;
}
