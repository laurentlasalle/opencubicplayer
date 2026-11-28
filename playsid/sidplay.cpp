/* OpenCP Module Player
 * copyright (c) 1994-'10 Niklas Beisert <nbeisert@physik.tu-muenchen.de>
 * copyright (c) 2011-'26 Stian Sebastian Skjelstad <stian.skjelstad@gmail.com>
 *
 * SIDPlay - SID file player based on Michael Schwendt's SIDPlay routines
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
 *
 * revision history: (please note changes here)
 *  -kb980717  Tammo Hinrichs <opencp@gmx.net>
 *    -first release
 *  -ss04????  Stian Skjelstad <stian@nixia.no>
 *    -ported the assembler to gcc
 *  -ss040908  Stian Skjelstad <stian@nixia.no>
 *    -made assembler optimize safe
 */

extern "C"
{
#include "sidplayfp-config/config.h"

/* sidplayfp compilation some defines set, that we need to remove again */
#ifdef DPACKAGE_NAME
#undef DPACKAGE_NAME
#endif

#ifdef VERSION
#undef VERSION
#endif

#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif

#include "../config.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "../types.h"
#include "boot/psetting.h"
#include "cpiface/cpiface.h"
#include "dev/mcp.h"
#include "dev/mixclip.h"
#include "dev/player.h"
#include "dev/ringbuffer.h"
#include "filesel/filesystem.h"
#include "stuff/err.h"
#include "stuff/imsrtns.h"
}

#include "sidplay.h"
#include "libsidplayfp-api.h"
#include "sidend.h"
#include "sidlength.h"

#define TARGET_ROW_BUFFERS 30 /* half a second, (if going 60 Herz), or 25 would be enough */
#define MAX_ROW_BUFFERS (TARGET_ROW_BUFFERS * 8) /* If pitch<->speed == 800%, we need up to 8x the amount of data */

static libsidplayfp::ConsolePlayer *mySidPlayer;
static SidTuneInfo const *mySidTuneInfo;
static uint32_t sid_clocks_per_row;
static uint32_t sid_samples_per_row /* row = virtual screen frame, set to 50 Hz, but it is not synced to the c64 virtual machine. Also happens to match the SID emulators max buffer-size, being 20ms */ ;

typedef struct
{
	uint8_t registers[3][0x20]; /* current register values, 3 SID chips */
	uint8_t volumes[3][3];

	uint8_t in_use;
} SidStatBuffer_t;

static SidStatBuffer_t last; /* current register values, 3 SID chips */

static SidStatBuffer_t SidStatBuffers[MAX_ROW_BUFFERS] = {{0}}; // half a second
static int SidStatBuffers_available = 0;
static int SidStatBuffers_target = 0;

static int16_t *sid_buf_stereo; /* stereo interleaved */
static int16_t *sid_buf_4x3[3]; /* 4-chan interleaved, 3 SID chips */

static struct ringbuffer_t *sid_buf_pos;
/*             tail              processing        head
 *  (free)      | already in devp | ready to stream |   (free)
 *
 *          As the tail catches up, we know data has been played, and we update our stats on the screen
 */

static uint32_t sidbuffpos;
static uint32_t sidbufrate;
static uint32_t sidRate; /* devp rate */

static uint64_t samples_committed;
static uint64_t samples_lastui;

static unsigned long voll,volr;
static int vol, bal;
static int pan;
static int srnd;

static int SidCount;

static int clipbusy=0;

static uint8_t sidMuted[3*3];

/* ---- end of the tune (Omni) ----
 * A SID tune plays forever. It ends:
 *  - at its length in the HVSC song length database (sidlength.h), with a fade-out;
 *  - otherwise when it loops after a silence (sidend.h), at the start of the silence;
 *  - otherwise after [libsidplayfp] defaultlength= seconds, with a fade-out.
 * In OCP's loop mode (Ctrl+L) it plays forever. */
struct frame_t
{
	int16_t *stereo;          /* stereo samples of one video frame */
	int16_t *raw[3];          /* 4 channels per sample (output and 3 voices) for each SID chip */
	unsigned int samples;
	SidStatBuffer_t stat;     /* registers at the end of the frame */
};
static struct frame_t frames[SIDEND_COMPARE];   /* [0] is the frame being made; the frames kept aside while sidend compares */
static int held;                                /* frames kept aside */
static int flushing = -1;                       /* next frame kept aside to give to the ringbuffer, -1 if none */
static unsigned int frame_capacity;             /* samples one frame can hold (PAL or NTSC) */
static int sid_doloop;                          /* OCP loop mode */
static int sid_ended;                           /* no more frames: the tune is over */
static int sid_loopdetect;                      /* [libsidplayfp] loopdetect= */
static int32_t sid_length_ms;                   /* -1: no length (forever, or until it loops) */
static int32_t sid_fade_ms;
static double sid_pos_ms;                       /* emulated time since the (sub)tune started */
static char sid_md5[33];                        /* of the file, for the song length database */
static struct ocpfilehandle_t *sid_file;
static struct cpifaceSessionAPI_t *sid_session;

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

static void SidStatBuffers_callback_from_sidbuf (void *arg, int samples_ago)
{
	SidStatBuffer_t *state = (SidStatBuffer_t *)arg;

	last = *state;

	state->in_use = 0;
	SidStatBuffers_available++;
}

static void free_frames (void)
{
	int i, c;
	for (i = 0; i < SIDEND_COMPARE; i++)
	{
		delete[] frames[i].stereo; frames[i].stereo = NULL;
		for (c = 0; c < 3; c++)
		{
			delete[] frames[i].raw[c]; frames[i].raw[c] = NULL;
		}
	}
	held = 0;
	flushing = -1;
}

/* emulates one video frame into f */
static void render_frame (struct frame_t *f)
{
	int j;
	std::vector<int16_t *> raw {f->raw[0], f->raw[1], f->raw[2]};
	f->samples = mySidPlayer->iterateaudio (f->stereo, sid_samples_per_row, sid_clocks_per_row, &raw) >> 1;
	for (j=0; j < SidCount; j++)
	{
		mySidPlayer->getSidStatus (j,
		                           f->stat.registers[j],
		                           f->stat.volumes[j][0],
		                           f->stat.volumes[j][1],
		                           f->stat.volumes[j][2]);
	}

	/* length of the tune: fade-out, then the end */
	if (!sid_doloop && (sid_length_ms > 0) && (sid_pos_ms >= sid_length_ms))
	{
		unsigned int i;
		double end = (double)sid_length_ms + sid_fade_ms;
		for (i = 0; i < f->samples; i++)
		{
			double t = sid_pos_ms + i * 1000.0 / sidRate;
			int32_t gain = (t >= end) ? 0 : (int32_t)((end - t) * 32768.0 / (sid_fade_ms ? sid_fade_ms : 1));
			f->stereo[i * 2 + 0] = (int16_t)((f->stereo[i * 2 + 0] * gain) >> 15);
			f->stereo[i * 2 + 1] = (int16_t)((f->stereo[i * 2 + 1] * gain) >> 15);
		}
		if (sid_pos_ms + f->samples * 1000.0 / sidRate >= end)
		{
			sid_ended = 1;
		}
	}
	sid_pos_ms += f->samples * 1000.0 / sidRate;
}

/* gives one frame to the ringbuffer, with its registers for the display; returns 0
 * if there is no room for it yet */
static int commit_frame (struct cpifaceSessionAPI_t *cpifaceSession, const struct frame_t *f)
{
	int pos1, pos2, length1, length2, i, c;
	unsigned int n1;

	cpifaceSession->ringbufferAPI->get_head_samples (sid_buf_pos, &pos1, &length1, &pos2, &length2);
	if ((unsigned int)(length1 + length2) < f->samples)
	{
		return 0;
	}
	for (i=0; i < MAX_ROW_BUFFERS; i++)
	{
		if (!SidStatBuffers[i].in_use)
		{
			break;
		}
	}
	if (i == MAX_ROW_BUFFERS)
	{
		return 0;
	}

	n1 = ((unsigned int)length1 < f->samples) ? (unsigned int)length1 : f->samples;
	memcpy (sid_buf_stereo + (pos1 << 1), f->stereo, n1 * 2 * sizeof (int16_t));
	memcpy (sid_buf_stereo + (pos2 << 1), f->stereo + n1 * 2, (f->samples - n1) * 2 * sizeof (int16_t));
	for (c=0; c < 3; c++)
	{
		memcpy (sid_buf_4x3[c] + (pos1 << 2), f->raw[c], n1 * 4 * sizeof (int16_t));
		memcpy (sid_buf_4x3[c] + (pos2 << 2), f->raw[c] + n1 * 4, (f->samples - n1) * 4 * sizeof (int16_t));
	}

	SidStatBuffers[i] = f->stat;
	SidStatBuffers[i].in_use = 1;
	cpifaceSession->ringbufferAPI->add_tail_callback_samples (sid_buf_pos, 0, SidStatBuffers_callback_from_sidbuf, SidStatBuffers + i);
	cpifaceSession->ringbufferAPI->head_add_samples (sid_buf_pos, f->samples);
	SidStatBuffers_available--;
	return 1;
}

OCP_INTERNAL void sidIdler (struct cpifaceSessionAPI_t *cpifaceSession)
{
	while (1)
	{
		struct frame_t *f;
		enum sidend_action_t action;

		if (held && (flushing < 0) && (!sid_loopdetect || sid_doloop))
		{ /* the loop mode was turned on while frames were kept aside: play them */
			flushing = 0;
		}
		if (flushing >= 0)
		{ /* the frames kept aside after a silence that was part of the tune */
			if ((SidStatBuffers_available <= 0) || !commit_frame (cpifaceSession, frames + flushing))
			{
				break;
			}
			if (++flushing == held)
			{
				flushing = -1;
				held = 0;
			}
			continue;
		}
		if (sid_ended)
		{
			break;
		}
		/* about 0.5 s of sound is prepared ahead (SidStatBuffers_available); while
		 * sidend compares, the emulation goes on until it knows the answer */
		if (!held && (SidStatBuffers_available <= 0))
		{
			break;
		}

		f = frames + held;
		render_frame (f);

		action = (sid_loopdetect && !sid_doloop) ? sidend_frame (f->stereo, f->samples, f->stat.registers) : SIDEND_PLAY;
		switch (action)
		{
			case SIDEND_PLAY:
				if (!commit_frame (cpifaceSession, f))
				{ /* no room (speed far above 100%): the frame is kept for the next call */
					held = 1;
					flushing = 0;
					return;
				}
				break;
			case SIDEND_HOLD:
				held++;
				break;
			case SIDEND_FLUSH:
				held++;
				flushing = 0;
				break;
			case SIDEND_RESTARTED:
				held = 0;
				sid_ended = 1;
				cpifaceSession->cpiDebug (cpifaceSession, "[SID] the tune loops: end at %.1f s\n", (sid_pos_ms - SIDEND_COMPARE * 1000.0 * sid_clocks_per_row / mySidPlayer->getMainCpuSpeed ()) / 1000.0);
				break;
		}
	}
}

/* a (sub)tune starts: its length, and the end detection */
static void sid_track_start (void)
{
	int song = mySidTuneInfo->currentSong();
	int defaultlength, fade;
	const struct configAPI_t *cfg = sid_session->configAPI;

	held = 0;
	flushing = -1;
	sid_ended = 0;
	sid_pos_ms = 0.0;
	sidend_reset (SidCount);

	defaultlength = cfg->GetProfileInt ("libsidplayfp", "defaultlength", 300, 10);
	fade = cfg->GetProfileInt ("libsidplayfp", "fade", 3, 10);
	if (defaultlength < 0) defaultlength = 0;
	if (fade < 0) fade = 0;
	if (fade > 60) fade = 60;
	sid_fade_ms = fade * 1000;
	sid_length_ms = sidlength_lookup (sid_session, sid_file, sid_md5, song);
	if (sid_length_ms > 0)
	{
		sid_session->cpiDebug (sid_session, "[SID] song %d: %d.%03d s (song length database)\n", song, sid_length_ms / 1000, sid_length_ms % 1000);
	} else {
		sid_length_ms = defaultlength ? defaultlength * 1000 : -1;
	}
}

OCP_INTERNAL void sidSetLoop (int loop)
{
	sid_doloop = loop;
}

OCP_INTERNAL int sidIsEnded (struct cpifaceSessionAPI_t *cpifaceSession)
{
	/* the resampler (speed or pitch other than 100%) keeps the last 3 samples to
	 * interpolate with, and up to 256 at high speeds: those are never played */
	return sid_ended && (flushing < 0) && (cpifaceSession->ringbufferAPI->get_processing_available_samples (sid_buf_pos) <= 256);
}

OCP_INTERNAL int32_t sidGetLength (void)
{
	return sid_length_ms;
}

OCP_INTERNAL void sidIdle(struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (clipbusy++)
	{
		clipbusy--;
		return;
	}

	if (cpifaceSession->InPause /*|| (sid_looped == 3)*/)
	{
		cpifaceSession->plrDevAPI->Pause (1);
	} else {
		void *targetbuf;
		unsigned int targetlength; /* in samples */

		cpifaceSession->plrDevAPI->Pause (0);

		cpifaceSession->plrDevAPI->GetBuffer (&targetbuf, &targetlength);

		if (targetlength)
		{
			int16_t *t = (int16_t *)targetbuf;
			unsigned int accumulated_target = 0;
			unsigned int accumulated_source = 0;
			int pos1, length1, pos2, length2;

			sidIdler (cpifaceSession);

			/* how much data is available.. we are using a ringbuffer, so we might receive two fragments */
			/* We are using processing, not tail */
			cpifaceSession->ringbufferAPI->get_processing_samples (sid_buf_pos, &pos1, &length1, &pos2, &length2);

			if (sidbufrate == 0x00010000)
			{
				if (targetlength>(length1+length2))
				{
					targetlength=(length1+length2);
					//sid_looped |= 2;
				} else {
					//sid_looped &= ~2;
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

						rs = sid_buf_stereo[(pos1<<1) + 0];
						ls = sid_buf_stereo[(pos1<<1) + 1];

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
				// sid_looped &= ~2;

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
							//sid_looped |= 2;
							break;
						}
						/* will we overflow the wavebuf if we advance? */
						if ((length1+length2) < ((sidbufrate+sidbuffpos)>>16))
						{
							//sid_looped |= 2;
							break;
						}

						switch (length1) /* if we are close to the wrap between buffer segment 1 and 2, len1 will grow down to a small number */
						{
							case 1:  wpm1 = pos1; wp0 = pos2;     wp1 = pos2 + 1; wp2 = pos2 + 2; break;
							case 2:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos2;     wp2 = pos2 + 1; break;
							case 3:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos2;     break;
							default: wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos1 + 3; break;
						}

						rvm1 = (uint16_t)sid_buf_stereo[(wpm1<<1)+0]^0x8000; /* we temporary need data to be unsigned - hence the ^0x8000 */
						lvm1 = (uint16_t)sid_buf_stereo[(wpm1<<1)+1]^0x8000;
						 rc0 = (uint16_t)sid_buf_stereo[(wp0 <<1)+0]^0x8000;
						 lc0 = (uint16_t)sid_buf_stereo[(wp0 <<1)+1]^0x8000;
						 rv1 = (uint16_t)sid_buf_stereo[(wp1 <<1)+0]^0x8000;
						 lv1 = (uint16_t)sid_buf_stereo[(wp1 <<1)+1]^0x8000;
						 rv2 = (uint16_t)sid_buf_stereo[(wp2 <<1)+0]^0x8000;
						 lv2 = (uint16_t)sid_buf_stereo[(wp2 <<1)+1]^0x8000;

						rc1 = rv1-rvm1;
						rc2 = 2*rvm1-2*rc0+rv1-rv2;
						rc3 = rc0-rvm1-rv1+rv2;
						rc3 =  imulshr16(rc3,sidbuffpos);
						rc3 += rc2;
						rc3 =  imulshr16(rc3,sidbuffpos);
						rc3 += rc1;
						rc3 =  imulshr16(rc3,sidbuffpos);
						rc3 += rc0;
						if (rc3<0)
							rc3=0;
						if (rc3>65535)
							rc3=65535;

						lc1 = lv1-lvm1;
						lc2 = 2*lvm1-2*lc0+lv1-lv2;
						lc3 = lc0-lvm1-lv1+lv2;
						lc3 =  imulshr16(lc3,sidbuffpos);
						lc3 += lc2;
						lc3 =  imulshr16(lc3,sidbuffpos);
						lc3 += lc1;
						lc3 =  imulshr16(lc3,sidbuffpos);
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
						sidbuffpos+=sidbufrate;
						progress = sidbuffpos>>16;
						sidbuffpos &= 0xffff;
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
			} /* if (sidbufrate==0x10000) */
			/* We are using processing instead of tail here */
			cpifaceSession->ringbufferAPI->processing_consume_samples (sid_buf_pos, accumulated_source);
			cpifaceSession->plrDevAPI->CommitBuffer (accumulated_target);
			samples_committed += accumulated_source;
		} /* if (targetlength) */

		{
			uint64_t delay = cpifaceSession->plrDevAPI->Idle();
			uint64_t new_ui = samples_committed - delay;
			if (new_ui > samples_lastui)
			{
				cpifaceSession->ringbufferAPI->tail_consume_samples (sid_buf_pos, new_ui - samples_lastui);
				samples_lastui = new_ui;
			}
		}
	}

	clipbusy--;
}

OCP_INTERNAL int sidNumberOfChips (void)
{
	return SidCount;
}

OCP_INTERNAL int sidNumberOfComments (void)
{
	return mySidTuneInfo->numberOfCommentStrings();
}

OCP_INTERNAL int sidNumberOfInfos(void)
{
	return mySidTuneInfo->numberOfInfoStrings();
}

OCP_INTERNAL const char *sidInfoString (int i)
{
	return mySidTuneInfo->infoString(i);
}

OCP_INTERNAL const char *sidCommentString (int i)
{
	return mySidTuneInfo->commentString(i);
}

OCP_INTERNAL const char *sidFormatString (void)
{
	return mySidTuneInfo->formatString();
}

OCP_INTERNAL const char *sidROMDescKernal (void)
{
	return mySidPlayer->kernalDesc();
}

OCP_INTERNAL const char *sidROMDescBasic (void)
{
	return mySidPlayer->basicDesc();
}

OCP_INTERNAL const char *sidROMDescChargen (void)
{
	return mySidPlayer->chargenDesc();
}

OCP_INTERNAL const float sidGetCPUSpeed (void)
{
	return mySidPlayer->getMainCpuSpeed();
}

OCP_INTERNAL const char *sidGetVICIIModelString (void)
{
	return libsidplayfp::VICIImodel_ToString(mySidPlayer->getVICIImodel());
}

OCP_INTERNAL const char *sidGetCIAModelString (void)
{
	return mySidPlayer->getCIAmodel();
}

OCP_INTERNAL const char *sidChipModel (int i)
{
	return libsidplayfp::sidModel_ToString(mySidPlayer->getSIDmodel(i));
}

OCP_INTERNAL uint16_t sidChipAddr (int i)
{
	return mySidPlayer->getSIDaddr(i);
}

OCP_INTERNAL const char *sidTuneStatusString (void)
{
	return mySidPlayer->getTuneStatusString();
}

OCP_INTERNAL const char *sidTuneInfoClockSpeedString (void)
{
	return libsidplayfp::tuneInfo_clockSpeed_toString(mySidPlayer->getTuneInfoClockSpeed());
}

static void sidSetPitch (uint32_t sp)
{
	if (sp > 0x00080000) sp = 0x00080000;
	if (!sp) sp = 0x1;
	sidbufrate = sp;

	int SidStatBuffers_newtarget = sidbufrate * TARGET_ROW_BUFFERS / 0x10000;
	if (SidStatBuffers_newtarget < 2)
	{
		SidStatBuffers_newtarget = 2;
	}
	if (SidStatBuffers_newtarget > MAX_ROW_BUFFERS)
	{
		SidStatBuffers_newtarget = MAX_ROW_BUFFERS;
	}

#ifdef PLAYSID_DEBUG
	fprintf (stderr, "sidSetPitch: sp=0x%08x   old_avail=%d old_target=%d    ", sp, SidStatBuffers_available, SidStatBuffers_target);
#endif

	SidStatBuffers_available += (SidStatBuffers_newtarget - SidStatBuffers_target);
	SidStatBuffers_target = SidStatBuffers_newtarget;

#ifdef PLAYSID_DEBUG
	fprintf (stderr, "new_avail=%d new_target=%d\n", SidStatBuffers_available, SidStatBuffers_target);
#endif
}

static void sidSetVolume (void)
{
	volr = voll = vol * 4;
	if (bal < 0)
		voll = (voll * (64 + bal)) >> 6;
	else
		volr = (volr * (64 - bal)) >> 6;
}

static void sidSet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt, int val)
{
	switch (opt)
	{
		case mcpMasterSpeed:
#warning TODO SetSpeed...
			//sidSetSpeed(val);
			break;
		case mcpMasterPitch:
			sidSetPitch(val<<8);
			break;
		case mcpMasterSurround:
			srnd=val;
			break;
		case mcpMasterPanning:
			pan=val;
			sidSetVolume();
			break;
		case mcpMasterVolume:
			vol=val;
			sidSetVolume();
			break;
		case mcpMasterBalance:
			bal=val;
			sidSetVolume();
			break;
#warning FILTER TODO
	}
}

static int sidGet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt)
{
	return 0;
}

OCP_INTERNAL void sidStartSong (uint8_t sng)
{
	if (!mySidPlayer)
	{
		return;
	}
	if (sng<1)
		sng=1;
	if (sng>mySidTuneInfo->songs())
		sng=mySidTuneInfo->songs();
	clipbusy++;
	mySidPlayer->selecttrack (sng);
	sid_track_start ();
	clipbusy--;
}

/* key R (Omni): switches between PAL (50 Hz) and NTSC (60 Hz) for the file being
 * played, and starts the current subsong again (the C64 must be reset). The next
 * files use the setting of ocp.ini again. */
OCP_INTERNAL void sidToggleVideoStandard (void)
{
	if (!mySidPlayer)
	{
		return;
	}
	clipbusy++;
	mySidPlayer->setVideoStandard (sidGetVideo () ? false : true);
	sid_clocks_per_row = mySidPlayer->GetVICIICyclesPerFrame();
	sid_samples_per_row = (unsigned long long) sidRate * mySidPlayer->GetVICIICyclesPerFrame() / mySidPlayer->getMainCpuSpeed();
	sid_track_start ();
	clipbusy--;
}

OCP_INTERNAL uint8_t sidGetSong ()
{
	if (!mySidPlayer)
	{
		return 0;
	}
	return mySidTuneInfo->currentSong();
}

OCP_INTERNAL uint8_t sidGetSongs (void)
{
	if (!mySidPlayer)
	{
		return 0;
	}
	return mySidTuneInfo->songs();
}

OCP_INTERNAL char sidGetVideo (void)
{
	if (!mySidPlayer)
	{
		return 0;
	}
	switch (mySidPlayer->c64Model())
	{
		default:
		case libsidplayfp::c64::model_t::PAL_B:      ///< PAL C64
		case libsidplayfp::c64::model_t::PAL_N:      ///< C64 Drean
		case libsidplayfp::c64::model_t::PAL_M:      ///< C64 Brasil
			return 1; /* PAL */
		case libsidplayfp::c64::model_t::NTSC_M:     ///< NTSC C64
		case libsidplayfp::c64::model_t::OLD_NTSC_M: ///< Old NTSC C64
			return 0; /* NTSC */
	}
}

OCP_INTERNAL void sidMute (struct cpifaceSessionAPI_t *cpifaceSession, int i, int m)
{
	cpifaceSession->MuteChannel[i] = m;
	sidMuted[i] = m;
	mySidPlayer->mute(i, m);
}

/*extern ubyte filterType;*/
OCP_INTERNAL void sidGetChanInfo (int i, sidChanInfo &ci)
{
	int sid = i / 3;
	int ch = i % 3;
	ci.freq=         last.registers[sid][ch*0x07+0x00] |
                        (last.registers[sid][ch*0x07+0x01]<<8);
	ci.pulse=        last.registers[sid][ch*0x07+0x02] |
                       ((last.registers[sid][ch*0x07+0x03] & 0x0f)<<8);
	ci.wave=         last.registers[sid][ch*0x07+0x04];
	ci.ad=           last.registers[sid][ch*0x07+0x05];
	ci.sr=           last.registers[sid][ch*0x07+0x06];
	ci.filtenabled = last.registers[sid][0x17] & (1<<ch);
	ci.filttype    = last.registers[sid][0x18];

	unsigned int leftvol, rightvol;
	leftvol = rightvol = last.volumes[sid][ch];

	switch (SidCount)
	{ /* mirror sidplayfp-git/libsidplayfp/src/mixer.h layout */
		default:
		case 1:
			break;
		case 2:
			if (sid) { leftvol = 0; } else { rightvol = 0; }
			break;
		case 3:
			switch (sid)
			{
				case 0:
					 leftvol = (leftvol * 150) >> 8;
					rightvol = 0;
					break;
				case 1:
					 leftvol = ( leftvol * 106) >> 8;
					rightvol = (rightvol * 106) >> 8;
					break;
				case 2:
					 leftvol = 0;
					rightvol = (rightvol * 150) >> 8;
					break;
			}
			break;
	}

	long pulsemul;
	switch (ci.wave & 0xf0)
	{
		case 0x10:
			leftvol*=192;
			rightvol*=192;
			break;
		case 0x20:
			leftvol*=224;
			rightvol*=224;
			break;
		case 0x30:
			leftvol*=208;
			rightvol*=208;
			break;
		case 0x40:
			pulsemul=2*(ci.pulse>>4);
			if (ci.pulse & 0x800)
				pulsemul=511-pulsemul;
			leftvol*=pulsemul;
			rightvol*=pulsemul;
			break;
		case 0x50:
			pulsemul=255-(ci.pulse>>4);
			leftvol*=pulsemul;
			rightvol*=pulsemul;
			break;
		case 0x60:
			pulsemul=255-(ci.pulse>>4);
			leftvol*=pulsemul;
			rightvol*=pulsemul;
			break;
		case 0x70:
			leftvol*=224;
			rightvol*=224;
			break;
		case 0x80:
			leftvol*=240;
			rightvol*=240;
			break;
		default:
			leftvol=ci.rightvol=0;
	}
	ci.leftvol=leftvol>>8;
	ci.rightvol=rightvol>>8;
}

static inline int16_t scale (int16_t src1)
{
	int32_t src2 = (((int32_t)src1) * 2) + 8285;
	if (src2 > INT16_MAX) return INT16_MAX;
	if (src2 < INT16_MIN) return INT16_MIN;
	return (int16_t)src2;
}

OCP_INTERNAL int sidGetLChanSample (struct cpifaceSessionAPI_t *cpifaceSession, unsigned int i, int16_t *s, unsigned int len, uint32_t rate, int opt)
{
	int sid = i / 3;
	int ch = (i % 3) + 1;

	int stereo = (opt&mcpGetSampleStereo)?1:0;
	uint32_t step = imuldiv(0x00010000, sidRate, (signed)rate);
	int16_t *src;
	int pos1, pos2;
	int length1, length2;
	uint32_t posf = 0;

	cpifaceSession->ringbufferAPI->get_tail_samples (sid_buf_pos, &pos1, &length1, &pos2, &length2);

	src = sid_buf_4x3[sid] + pos1 * 4 + ch;

	if (!length1)
	{
		return !!sidMuted[ch];
	}

	while (len)
	{
		if (stereo)
		{
			*(s++) = scale(*src);
			*(s++) = scale(*src);
		} else {
			*(s++) = scale(*src);
		}
		len--;

		posf += step;

		while (posf >= 0x00010000)
		{
			posf -= 0x00010000;

			src += 4;
			length1--;

			if (!length1)
			{
				length1 = length2;
				length2 = 0;
				pos1 = pos2;
				pos2 = -1;
				src = sid_buf_4x3[sid] + pos1 * 4 + ch;
			}
			if (!length1)
			{
				memset (s, 0, len<<(stereo + 1));
				return !!sidMuted[ch];
			}
		}
	}

	return !!sidMuted[ch];
}

OCP_INTERNAL int sidGetPChanSample (struct cpifaceSessionAPI_t *cpifaceSession, unsigned int i, int16_t *s, unsigned int len, uint32_t rate, int opt)
{
	int sid = i / 4;
	int ch = i % 4;

	int stereo = (opt&mcpGetSampleStereo)?1:0;
	uint32_t step = imuldiv(0x00010000, sidRate, (signed)rate);
	int16_t *src;
	int pos1, pos2;
	int length1, length2;
	uint32_t posf = 0;

	cpifaceSession->ringbufferAPI->get_tail_samples (sid_buf_pos, &pos1, &length1, &pos2, &length2);

	src = sid_buf_4x3[sid] + pos1 * 4 + ch;

	if (!length1)
	{
		return !!sidMuted[ch];
	}

	while (len)
	{
		if (ch)
		{
			if (stereo)
			{
				*(s++) = scale(*src);
				*(s++) = scale(*src);
			} else {
				*(s++) = scale(*src);
			}
		} else {
			if (stereo)
			{
				*(s++) = *src;
				*(s++) = *src;
			} else {
				*(s++) = *src;
			}
		}

		len--;

		posf += step;

		while (posf >= 0x00010000)
		{
			posf -= 0x00010000;

			src += 4;
			length1--;

			if (!length1)
			{
				length1 = length2;
				length2 = 0;
				pos1 = pos2;
				pos2 = -1;
				src = sid_buf_4x3[sid] + pos1 * 4 + ch;
			}
			if (!length1)
			{
				memset (s, 0, len<<(stereo + 1));
				return !!sidMuted[ch];
			}
		}
	}
	return !!sidMuted[ch];
}

OCP_INTERNAL void sidSetFilter(bool enable)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetFilter (enable);
}

OCP_INTERNAL void sidSetFilterCurve6581 (double v)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetFilterCurve6581 (v);
}

OCP_INTERNAL void sidSetEnableOld6581caps (bool enable)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetEnableOld6581caps (enable);
}

OCP_INTERNAL void sidSetFilterRange6581 (double v)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetFilterRange6581 (v);
}

OCP_INTERNAL void sidSetWaveOffset6581 (double v)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetWaveOffset6581 (v);
}

OCP_INTERNAL void sidSetFilterCurve8580 (double v)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetFilterCurve8580 (v);
}

OCP_INTERNAL void sidSetCombinedWaveformsStrength (int CWF)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->SetCombinedWaveformsStrength (CWF);
}

OCP_INTERNAL void sidSetDACLeakageLevel (double v)
{
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->sidSetDACLeakageLevel (v);
}

OCP_INTERNAL void sidSetDCBlockRes (double v)
{
#warning remove-me
fprintf(stderr, "sidSetDCBlockRes(%lf), mySidPlayer=%p\n", v, mySidPlayer);
	if (!mySidPlayer)
	{
		return;
	}
	mySidPlayer->sidSetDCBlockRes (v);
}

OCP_INTERNAL int sidOpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession)
{
	int retval;
	enum plrRequestFormat format=PLR_STEREO_16BIT_SIGNED;

	if (!cpifaceSession->plrDevAPI)
	{
		return errPlay;
	}

	samples_committed = 0;
	samples_lastui = 0;

	const int length = file->filesize (file);
	if (!length)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[SID] File is way too small\n");
		return errFormStruc;
	}
	if (length > 1024*1024)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[SID] File is way too big\n");
		return errFormStruc;
	}

	unsigned char *buf=new unsigned char[length];
	if (!buf)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[SID] new() #1 failed\n");
		return errAllocMem;
	}

	if (file->read (file, buf, length) != length)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[SID] read failed #1\n");
		retval = errFileRead;
		goto error_out_buf;
	}

	sidRate=0;
	if (!cpifaceSession->plrDevAPI->Play (&sidRate, &format, file, cpifaceSession))
	{
		retval = errPlay;
		goto error_out_buf;
	}

	mySidPlayer = new libsidplayfp::ConsolePlayer(sidRate, cpifaceSession);
	if (!mySidPlayer->load (buf, length))
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[SID] loading file failed\n");
		retval = errFormStruc;
		goto error_out_mySidPlay;
	}
	sidlength_md5 ((const uint8_t *)buf, length, sid_md5);
	delete [] buf; buf = 0;
	mySidTuneInfo = mySidPlayer->getInfo();

	SidCount = mySidPlayer->getSidCount();
	if (!mySidTuneInfo)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[SID] retrieve info from file failed\n");
		retval = errFormStruc;
		goto error_out_mySidPlay;
	}

	memset(sidMuted, 0, sizeof (sidMuted));

	sid_clocks_per_row = mySidPlayer->GetVICIICyclesPerFrame();
	sid_samples_per_row = (unsigned long long) sidRate * mySidPlayer->GetVICIICyclesPerFrame() / mySidPlayer->getMainCpuSpeed();
	/* libsidplayfp will perform fewer samples than sid_samples_per_row when doing sid_clocks_per_row clock cycles..... go figure. If it was larger we would need to compensate */

#ifdef PLAYSID_DEBUG
	fprintf (stderr, "GetVICIICyclesPerFrame()=%u\n", (unsigned int) mySidPlayer->GetVICIICyclesPerFrame());
	fprintf (stderr, "mySidPlayer->getMainCpuSpeed()=%u\n", (unsigned int) mySidPlayer->getMainCpuSpeed());
	fprintf (stderr, "sidRate=%u\n", (unsigned int)sidRate);
	fprintf (stderr, "sid_samples_per_row=%u\n", (unsigned int)sid_samples_per_row);
#endif

	/* key R switches between PAL and NTSC while the file plays: the buffers are made
	 * for the longest video frame, the one of PAL (19656 cycles at 985248 Hz) */
	frame_capacity = (unsigned long long) sidRate * 19656 / 985248 + 16;
	if (frame_capacity < sid_samples_per_row + 16)
	{
		frame_capacity = sid_samples_per_row + 16;
	}

	sid_buf_stereo = new int16_t [MAX_ROW_BUFFERS * 2 * frame_capacity](); /* 2 for stereo, ()=initialize the array to zero, ensure valgrind is happy */
	sid_buf_4x3[0] = new int16_t [MAX_ROW_BUFFERS * 4 * frame_capacity](); /* 4 for 1 output and 3 internal channels, First SID IC */
	sid_buf_4x3[1] = new int16_t [MAX_ROW_BUFFERS * 4 * frame_capacity](); /*                                         Second SID IC */
	sid_buf_4x3[2] = new int16_t [MAX_ROW_BUFFERS * 4 * frame_capacity](); /*                                         Third SID IC */
	if ((!sid_buf_4x3[0]) || (!sid_buf_4x3[1]) || (!sid_buf_4x3[2]))
	{
		retval = errAllocMem;
		goto error_out_sid_buffers;
	}
	{
		int i, c;
		for (i = 0; i < SIDEND_COMPARE; i++)
		{
			frames[i].stereo = new int16_t [2 * frame_capacity]();
			for (c = 0; c < 3; c++)
			{
				frames[i].raw[c] = new int16_t [4 * frame_capacity]();
			}
		}
	}

	sid_buf_pos = cpifaceSession->ringbufferAPI->new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED | RINGBUFFER_FLAGS_PROCESS, MAX_ROW_BUFFERS * frame_capacity);
	if (!sid_buf_pos)
	{
		retval = errAllocMem;
		goto error_out_sid_buffers;
	}

	memset (SidStatBuffers, 0, sizeof (SidStatBuffers));
	SidStatBuffers_target = TARGET_ROW_BUFFERS;
	SidStatBuffers_available = SidStatBuffers_target;

	sidbuffpos = 0x00000000;
	sidbufrate = 0x00010000;

	// construct song message
	{
		int i,j;
		const int msgLen=50;
		static const char* msg[msgLen];
		for(i=0; i<msgLen; i++)
			msg[i]=0;
		i=0;
		for(j=0; j<mySidTuneInfo->numberOfInfoStrings() && i<msgLen; j++)
			msg[i++]=mySidTuneInfo->infoString(j);
		for(j=0; j<mySidTuneInfo->numberOfCommentStrings() && i<msgLen; j++)
			msg[i++]=mySidTuneInfo->commentString(j);
		if(i<msgLen)
			msg[i++]=mySidTuneInfo->formatString();
		cpifaceSession->UseMessage((char **)msg);
	}

	sid_session = cpifaceSession;
	sid_file = file;
	file->ref (file);
	sid_doloop = 0;
	sid_loopdetect = cpifaceSession->configAPI->GetProfileBool ("libsidplayfp", "loopdetect", 1, 1);
	sid_track_start ();

	cpifaceSession->mcpSet = sidSet;
	cpifaceSession->mcpGet = sidGet;
	cpifaceSession->Normalize (cpifaceSession, mcpNormalizeDefaultPlayP);

	return errOk;

	//cpifaceSession->ringbufferAPI->free (sid_buf_pos); sid_buf_pos = 0;
error_out_sid_buffers:
	free_frames ();
	delete[] sid_buf_stereo; sid_buf_stereo = NULL;
	delete[] sid_buf_4x3[0]; sid_buf_4x3[0] = NULL;
	delete[] sid_buf_4x3[1]; sid_buf_4x3[1] = NULL;
	delete[] sid_buf_4x3[2]; sid_buf_4x3[2] = NULL;
error_out_mySidPlay:
	cpifaceSession->plrDevAPI->Stop (cpifaceSession);
	delete mySidPlayer; mySidPlayer = NULL;
error_out_buf:
	if (buf) delete [] buf;
	return retval;
}

OCP_INTERNAL void sidClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->plrDevAPI)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
	}

	if (sid_buf_pos)
	{
		cpifaceSession->ringbufferAPI->free (sid_buf_pos);
		sid_buf_pos = 0;
	}

	delete mySidPlayer;      mySidPlayer = NULL;
	                         mySidTuneInfo = NULL;
	free_frames ();
	if (sid_file)
	{
		sid_file->unref (sid_file);
		sid_file = 0;
	}
	delete[] sid_buf_stereo; sid_buf_stereo = NULL;
	delete[] sid_buf_4x3[0]; sid_buf_4x3[0] = NULL;
	delete[] sid_buf_4x3[1]; sid_buf_4x3[1] = NULL;
	delete[] sid_buf_4x3[2]; sid_buf_4x3[2] = NULL;
}
