/* OpenCP Module Player
 * copyright (c) 2024-'26 Stian Skjelstad <stian.skjelstad@gmail.com>
 *
 * S98 (NEC PC-88/PC-98, Sharp X1/X68000 register dumps) playback routines,
 * using libvgm by Valley Bell. Based on the GME playback routines.
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
#include <unistd.h>
#include "types.h"
#include "cpiface/cpiface.h"
#include "dev/mcp.h"
#include "dev/player.h"
#include "dev/ringbuffer.h"
#include "filesel/filesystem.h"
#include "stuff/err.h"
#include "stuff/imsrtns.h"

#include "boot/psetting.h"
#include "s98lib.h"
#include "s98play.h"

static struct s98lib_t *s98session;

static int s98looped;
static int doloop = -1; /* -1 = not set yet */

static unsigned long voll,volr;
static int bal;
static int vol;
static int pan;
static int srnd;

static int16_t *s98buf;     /* the buffer */
static struct ringbuffer_t *s98bufpos = 0;
static uint32_t s98buffpos; /* read fine-pos.. */
static uint32_t s98bufrate; /* re-sampling rate.. fixed point 0x10000 => 1.0 */
static struct s98lib_info_t s98info;

/* ocp.ini [s98] */
static int s98_cfg_loops = 2; /* loops= times the loop section is played before fading out */
static int s98_cfg_fade = 8;  /* fade=  seconds of fade-out */

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

static void s98Idler (struct cpifaceSessionAPI_t *cpifaceSession)
{
	int pos1, pos2;
	int length1, length2;

	cpifaceSession->ringbufferAPI->get_head_samples (s98bufpos, &pos1, &length1, &pos2, &length2);

	while (length1)
	{
		if (s98lib_finished (s98session))
		{
			if (doloop)
			{
				s98lib_restart (s98session);
			} else {
				s98looped |= 1;
				return;
			}
		}
		s98lib_render (s98session, s98buf + (pos1<<1), length1);

		cpifaceSession->ringbufferAPI->head_add_samples (s98bufpos, length1);
		cpifaceSession->ringbufferAPI->get_head_samples (s98bufpos, &pos1, &length1, &pos2, &length2);
	}
}

OCP_INTERNAL void s98Idle (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->InPause || (s98looped == 3))
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

			s98Idler (cpifaceSession);

			/* how much data is available.. we are using a ringbuffer, so we might receive two fragments */
			cpifaceSession->ringbufferAPI->get_tail_samples (s98bufpos, &pos1, &length1, &pos2, &length2);

			if (s98bufrate==0x10000)
			{
				if (targetlength>(length1+length2))
				{
					targetlength=(length1+length2); // limiting targetlength here, saves us from doing this per sample later
					s98looped |= 2;
				} else {
					s98looped &= ~2;
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

						rs = s98buf[(pos1<<1) + 0];
						ls = s98buf[(pos1<<1) + 1];

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
				s98looped &= ~2;

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
							s98looped |= 2;
							break;
						}
						/* will we overflow the wavebuf if we advance? */
						if ((length1+length2) < ((s98bufrate+s98buffpos)>>16))
						{
							s98looped |= 2;
							break;
						}

						switch (length1) /* if we are close to the wrap between buffer segment 1 and 2, len1 will grow down to a small number */
						{
							case 1:  wpm1 = pos1; wp0 = pos2;     wp1 = pos2 + 1; wp2 = pos2 + 2; break;
							case 2:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos2;     wp2 = pos2 + 1; break;
							case 3:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos2;     break;
							default: wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos1 + 3; break;
						}

						rvm1 = (uint16_t)s98buf[(wpm1<<1)+0]^0x8000; /* we temporary need data to be unsigned - hence the ^0x8000 */
						lvm1 = (uint16_t)s98buf[(wpm1<<1)+1]^0x8000;
						 rc0 = (uint16_t)s98buf[(wp0 <<1)+0]^0x8000;
						 lc0 = (uint16_t)s98buf[(wp0 <<1)+1]^0x8000;
						 rv1 = (uint16_t)s98buf[(wp1 <<1)+0]^0x8000;
						 lv1 = (uint16_t)s98buf[(wp1 <<1)+1]^0x8000;
						 rv2 = (uint16_t)s98buf[(wp2 <<1)+0]^0x8000;
						 lv2 = (uint16_t)s98buf[(wp2 <<1)+1]^0x8000;

						rc1 = rv1-rvm1;
						rc2 = 2*rvm1-2*rc0+rv1-rv2;
						rc3 = rc0-rvm1-rv1+rv2;
						rc3 =  imulshr16(rc3,s98buffpos);
						rc3 += rc2;
						rc3 =  imulshr16(rc3,s98buffpos);
						rc3 += rc1;
						rc3 =  imulshr16(rc3,s98buffpos);
						rc3 += rc0;
						if (rc3<0)
							rc3=0;
						if (rc3>65535)
							rc3=65535;

						lc1 = lv1-lvm1;
						lc2 = 2*lvm1-2*lc0+lv1-lv2;
						lc3 = lc0-lvm1-lv1+lv2;
						lc3 =  imulshr16(lc3,s98buffpos);
						lc3 += lc2;
						lc3 =  imulshr16(lc3,s98buffpos);
						lc3 += lc1;
						lc3 =  imulshr16(lc3,s98buffpos);
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

						s98buffpos+=s98bufrate;
						progress = s98buffpos>>16;
						s98buffpos &= 0xffff;
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
			} /* if (s98bufrate==0x10000) */
			cpifaceSession->ringbufferAPI->tail_consume_samples (s98bufpos, accumulated_source);
			cpifaceSession->plrDevAPI->CommitBuffer (accumulated_target);
		} /* if (targetlength) */
	}

	cpifaceSession->plrDevAPI->Idle();

}



static void s98SetSpeed (uint16_t sp)
{
	if (sp < 4)
		sp = 4;
	s98lib_setspeed (s98session, (double)sp / 256.0);
}

static void s98SetPitch (uint16_t sp)
{
	if (sp < 4)
		sp = 4;
	s98bufrate = 256 * sp;
}

static void s98SetVolume (void)
{
	volr = voll = vol * 4;
	if (bal < 0)
	{
		voll = (voll * (64 + bal)) >> 6;
	} else {
		volr = (volr * (64 - bal)) >> 6;
	}
}

static void s98Set (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt, int val)
{
	switch (opt)
	{
		case mcpMasterSpeed:
			s98SetSpeed(val);
			break;
		case mcpMasterPitch:
			s98SetPitch(val);
			break;
		case mcpMasterSurround:
			srnd=val;
			break;
		case mcpMasterPanning:
			pan=val;
			s98SetVolume();
			break;
		case mcpMasterVolume:
			vol=val;
			s98SetVolume();
			break;
		case mcpMasterBalance:
			bal=val;
			s98SetVolume();
			break;
	}
}

static int s98Get (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt)
{
	return 0;
}

OCP_INTERNAL int s98OpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession)
{
	uint32_t s98Rate;
	enum plrRequestFormat format;
	unsigned char *data;
	uint64_t datalen;
	uint32_t s98buflen;

	assert (!s98session);

	if (!cpifaceSession->plrDevAPI)
	{
		return errPlay;
	}

	datalen = file->filesize(file);
	if ((datalen < 0x20) || (datalen > 32*1024*1024))
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[S98] File too small or too big\n");
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

	s98_cfg_loops = cpifaceSession->configAPI->GetProfileInt ("s98", "loops", 2, 10);
	s98_cfg_fade  = cpifaceSession->configAPI->GetProfileInt ("s98", "fade",  8, 10);
	if (s98_cfg_loops < 1) s98_cfg_loops = 1;
	if (s98_cfg_loops > 100) s98_cfg_loops = 100;
	if (s98_cfg_fade < 0) s98_cfg_fade = 0;
	if (s98_cfg_fade > 60) s98_cfg_fade = 60;

	s98Rate=0;
	format=PLR_STEREO_16BIT_SIGNED;
	if (!cpifaceSession->plrDevAPI->Play (&s98Rate, &format, file, cpifaceSession))
	{
		free (data);
		return errPlay;
	}

	s98buflen = s98Rate >> 4;

	s98session = s98lib_open (data, datalen, s98Rate, s98buflen);
	free (data);
	data = 0;
	if (!s98session)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[S98] libvgm failed to load the file\n");
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		return errFormMiss;
	}
	doloop = -1;
	s98lib_setloop (s98session, s98_cfg_loops, s98_cfg_fade);
	s98lib_getinfo (s98session, &s98info);

	s98looped = 0;
	s98buf = malloc(s98buflen <<  2 /* stereo + 16bit */);
	if (!s98buf)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		s98lib_close (s98session);
		s98session = 0;
		return errAllocMem;
	}

	s98bufpos = cpifaceSession->ringbufferAPI->new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED, s98buflen);
	if (!s98bufpos)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);

		free (s98buf);
		s98buf = 0;

		s98lib_close (s98session);
		s98session = 0;

		return errAllocMem;
	}
	s98buffpos=0;

	cpifaceSession->mcpSet = s98Set;
	cpifaceSession->mcpGet = s98Get;

	cpifaceSession->Normalize (cpifaceSession, mcpNormalizeCanSpeedPitchUnlock);

	return errOk;
}

OCP_INTERNAL void s98ClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->plrDevAPI)
	{
		cpifaceSession->plrDevAPI->Stop(cpifaceSession);
	}

	if (s98session)
	{
		s98lib_close (s98session);
		s98session = 0;
	}

	if (s98bufpos)
	{
		cpifaceSession->ringbufferAPI->free (s98bufpos);
		s98bufpos = 0;
	}

	free(s98buf);
	s98buf = 0;
}

OCP_INTERNAL void s98SetLoop (int s)
{
	s = !!s;
	if (s98session && (s != doloop))
	{
		/* OCP loop mode: loop forever without fading, otherwise the configured loops + fade */
		s98lib_setloop (s98session, s ? 0 : s98_cfg_loops, s98_cfg_fade);
	}
	doloop = s;
}

OCP_INTERNAL int s98IsLooped (void)
{
	return s98looped == 3;
}

OCP_INTERNAL void s98Restart (void)
{
	if (s98session)
	{
		s98lib_restart (s98session);
		s98looped = 0;
	}
}

OCP_INTERNAL void s98GetInfo (struct s98info_t *info)
{
	info->length     = s98info.length;
	info->looplength = s98info.looplength;
	info->devices    = s98info.devicenames;
	info->position   = s98session ? s98lib_curtime (s98session) : 0.0;
}
