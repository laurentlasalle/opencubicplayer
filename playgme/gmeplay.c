/* OpenCP Module Player
 * copyright (c) 2024-'26 Stian Skjelstad <stian.skjelstad@gmail.com>
 *
 * Game Music Emulator Play playback routines
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
#include <gme/gme.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>
#include "types.h"
#include "cpiface/cpiface.h"
#include "dev/mcp.h"
#include "dev/player.h"
#include "dev/ringbuffer.h"
#include "filesel/filesystem.h"
#include "stuff/err.h"
#include "stuff/imsrtns.h"

#include "boot/psetting.h"
#include "gmeplay.h"
#include "nsfvis.h"

static Music_Emu *gmesession;

static int gmelooped;
static int doloop;

static unsigned long voll,volr;
static int bal;
static int vol;
static int pan;
static int srnd;

static int16_t *gmebuf;     /* the buffer */
static struct ringbuffer_t *gmebufpos = 0;
static uint32_t gmebuffpos; /* read fine-pos.. */
static uint32_t gmebufrate; /* re-sampling rate.. fixed point 0x10000 => 1.0 */
static int gmeactivetrack;
static struct gme_info_t *gmetrackinfo;

/* ocp.ini [gme] - how long a track plays before fading out and moving on */
static int gme_cfg_loops = 2;          /* loops=       times the loop section is played, when the file gives intro/loop lengths (VGM, ...) */
static int gme_cfg_defaultlength = 150; /* defaultlength= seconds, when the file gives no length (plain NSF, GBS, KSS, ...); 0 = forever */
static int gme_cfg_fade = 8;            /* fade=        seconds of fade-out, unless the file specifies one */
static long gme_cfg_rate = 44100;       /* output sample rate */

/* NSF tracker view: chip state per frame, synchronized with what is heard */
#define VIS_RING 16384                     /* frames kept (about 4.5 minutes) */
#define VIS_SLOTS 512
static int vis_enabled;                    /* collecting frames for this file */
static int vis_layout_done;
static struct nsfvis_layout_t vis_layout;
static struct nsfvis_ch_t *vis_ring;       /* [VIS_RING][vis_layout.count] */
static long vis_sample[VIS_RING];          /* output sample frame where each frame starts */
static long vis_first;                     /* absolute index of the oldest kept frame */
static long vis_count;                     /* number of kept frames */
static long vis_outpos;                    /* sample frames returned by gme_play() since the track started */
static volatile long vis_playpos;          /* sample frame currently heard */
static volatile int vis_generation;        /* incremented when a track (re)starts, stale callbacks are ignored */
static int vis_newdata;
static unsigned int vis_chips_header;      /* expansion chips from the file header, fallback for the layout */
static int vis_rowframes = 6;

/* Scout: a second, silent emulator of the same file and track that runs a few
 * seconds ahead of the audio. Its frames fill the rows that are not heard yet.
 * Emulation is deterministic, so they match what the main emulator will play.
 * The main emulator keeps producing frames only when the scout is unavailable. */
#define SCOUT_LEAD_SECONDS 4
#define SCOUT_CHUNK 1024                    /* sample frames per gme_play() call */
static Music_Emu *scoutsession;
enum { SCOUT_OFF, SCOUT_CATCHUP, SCOUT_SYNCED };
static int scout_state;                     /* SYNCED: the scout feeds the view; CATCHUP: replaying the track to reach the main emulator */
static long scout_outpos;                   /* sample frames played by the scout since its track started */
static long scout_offset;                   /* added to the scout stamps to get main emulator stamps */
static long scout_frames;                   /* frames read from the scout since its track started */
static long main_frames;                    /* frames read from the main emulator since the track started */
#define MAIN_STAMPS 4096
static long main_stamp[MAIN_STAMPS];        /* stamps of the last frames of the main emulator, by frame index */
static long vis_pushed;                     /* index of the last frame given to the view, -1 if none */
static int scout_tempo = 256;               /* tempo of both emulators (256 = normal) */
static long scout_tempo_changed;            /* vis_outpos of the last tempo change */
static int scout_failed;                    /* could not start the track, do not retry until the next one */
static const struct plrDevAPI_t *vis_plrDevAPI;

static struct vis_slot_t
{
	long pos;
	int generation;
	int busy;
} vis_slots[VIS_SLOTS];
static int vis_slot_next;

static void vis_track_reset (void)
{
	main_frames = 0;
	vis_pushed = -1;
	scout_tempo_changed = 0;
	vis_generation++;
	vis_first = 0;
	vis_count = 0;
	vis_outpos = 0;
	vis_playpos = 0;
	vis_generation++;
}

static void vis_push (gme_nsf_frame_t *f)
{
	long idx;
	if (!vis_layout_done)
	{
		if (!f->system && !f->chips) f->chips = vis_chips_header;
		nsfvis_layout (&vis_layout, f);
		vis_ring = calloc ((size_t)VIS_RING * vis_layout.count, sizeof (struct nsfvis_ch_t));
		if (!vis_ring)
		{
			vis_enabled = 0;
			return;
		}
		vis_layout_done = 1;
	}
	/* frames emulated while libgme skipped the initial silence are stamped too late:
	 * drop them when the timeline goes backwards (they are silent anyway) */
	while (vis_count && (vis_sample[(vis_first + vis_count - 1) % VIS_RING] >= f->sample))
	{
		vis_count--;
	}
	if (vis_count == VIS_RING)
	{
		vis_first++;
		vis_count--;
	}
	idx = (vis_first + vis_count) % VIS_RING;
	nsfvis_decode (&vis_layout, f, vis_ring + idx * vis_layout.count);
	vis_sample[idx] = f->sample;
	vis_count++;
	vis_newdata = 1;
}

/* Frames are numbered from the start of the track, the same way for both emulators
 * (the k-th frame holds the same registers whatever the tempo). A frame is given to
 * the view once: by the scout when it is synchronized (it is ahead), else by the
 * main emulator. */
static void scout_read (void)
{
	gme_nsf_frame_t tmp[64];
	int n, i;
	while ((scout_state != SCOUT_OFF) && ((n = gme_vis_read (scoutsession, tmp, 64)) > 0))
	{
		for (i = 0; i < n; i++)
		{
			long idx = scout_frames++;
			if (scout_state == SCOUT_CATCHUP)
			{
				if ((idx + 1 < main_frames) || (idx >= main_frames))
				{
					continue; /* not there yet (or main emulator has no frame yet) */
				}
				/* reached the last frame of the main emulator: align the stamps */
				scout_offset = main_stamp[idx % MAIN_STAMPS] - tmp[i].sample;
				scout_state = SCOUT_SYNCED;
				continue;
			}
			if (idx > vis_pushed)
			{
				tmp[i].sample += scout_offset;
				vis_push (&tmp[i]);
				vis_pushed = idx;
			}
		}
	}
}

static void vis_pull (void)
{
	gme_nsf_frame_t tmp[64];
	int n, i;

	if (!vis_enabled || !gmesession)
	{
		return;
	}
	while ((n = gme_vis_read (gmesession, tmp, 64)) > 0)
	{
		for (i = 0; i < n; i++)
		{
			long idx = main_frames++;
			main_stamp[idx % MAIN_STAMPS] = tmp[i].sample;
			if (idx > vis_pushed) /* not given by the scout (behind, stopped, or its track ended) */
			{
				vis_push (&tmp[i]);
				vis_pushed = idx;
			}
		}
	}
	scout_read ();
}

static void vis_callback_from_devp (void *arg, int samples_ago)
{
	struct vis_slot_t *slot = arg;
	if (slot->generation == vis_generation)
	{
		vis_playpos = slot->pos;
	}
	slot->busy = 0;
}

static unsigned int vis_commit_target; /* device samples that will be committed by the ongoing tail_consume_samples() */

/* fired while the chunk holding the tagged sample is consumed; it is committed to the device right after */
static void vis_callback_from_ringbuffer (void *arg, int samples_ago)
{
	struct vis_slot_t *slot = arg;
	int samples_until = (int)vis_commit_target - (gmebufrate ? (int)((int64_t)samples_ago * 65536 / gmebufrate) : samples_ago);
	if (samples_until < 0)
	{
		samples_until = 0;
	}
	if (vis_plrDevAPI && vis_plrDevAPI->OnBufferCallback)
	{
		vis_plrDevAPI->OnBufferCallback (-samples_until, vis_callback_from_devp, slot);
	} else {
		vis_callback_from_devp (arg, 0);
	}
}

OCP_INTERNAL int gmeVisActive (void)
{
	return vis_enabled && vis_layout_done;
}

OCP_INTERNAL const struct nsfvis_layout_t *gmeVisLayout (void)
{
	return &vis_layout;
}

OCP_INTERNAL int gmeVisRowFrames (void)
{
	return vis_rowframes;
}

OCP_INTERNAL int gmeVisTakeNewData (void)
{
	int r = vis_newdata;
	vis_newdata = 0;
	return r;
}

/* absolute index of the frame currently heard, -1 if none */
OCP_INTERNAL long gmeVisCurrentFrame (void)
{
	long lo = vis_first, hi = vis_first + vis_count - 1, best = -1;
	long pos = vis_playpos;
	while (lo <= hi)
	{
		long mid = lo + (hi - lo) / 2;
		if (vis_sample[mid % VIS_RING] <= pos)
		{
			best = mid;
			lo = mid + 1;
		} else {
			hi = mid - 1;
		}
	}
	return best;
}

OCP_INTERNAL long gmeVisFrameSample (long frame)
{
	if (frame < vis_first || frame >= vis_first + vis_count)
	{
		return -1;
	}
	return vis_sample[frame % VIS_RING];
}

OCP_INTERNAL long gmeVisLastFrame (void)
{
	return vis_first + vis_count - 1;
}

/* returns the decoded channels of an absolute frame index, or NULL if not kept */
OCP_INTERNAL const struct nsfvis_ch_t *gmeVisFrame (long frame)
{
	if (!vis_ring || frame < vis_first || frame >= vis_first + vis_count)
	{
		return 0;
	}
	return vis_ring + (frame % VIS_RING) * vis_layout.count;
}

/* restarts the scout on the current track at the current tempo; it replays the track
 * silently until it reaches the main emulator (SCOUT_CATCHUP), then goes ahead */
static void scout_restart (int synced)
{
	if (!scoutsession || !vis_enabled)
	{
		return;
	}
	gme_vis_enable (scoutsession, 0);
	gme_set_tempo (scoutsession, (double)scout_tempo / 256.0);
	scout_outpos = 0;
	scout_frames = 0;
	scout_offset = 0;
	scout_state = SCOUT_OFF;
	if (gme_start_track (scoutsession, gmeactivetrack) || !gme_vis_enable (scoutsession, 1))
	{
		scout_failed = 1;
		return;
	}
	scout_failed = 0;
	scout_state = synced ? SCOUT_SYNCED : SCOUT_CATCHUP;
}

/* lets the scout run up to SCOUT_LEAD_SECONDS ahead of the audio, at most half a second
 * of emulation per call (more while catching up, the audio is not waiting for it) */
static void scout_run (void)
{
	static short buf[SCOUT_CHUNK * 2];
	long budget;

	if (!scoutsession || !vis_enabled)
	{
		return;
	}
	if ((scout_state == SCOUT_OFF) && !scout_failed && (vis_outpos - scout_tempo_changed > gme_cfg_rate / 2))
	{ /* tempo stable for half a second (e.g. end of a pause fade): resynchronize */
		scout_restart (0);
	}
	if (scout_state == SCOUT_OFF)
	{
		return;
	}
	budget = (scout_state == SCOUT_CATCHUP) ? gme_cfg_rate : gme_cfg_rate / 2;
	while ((budget > 0) && !gme_track_ended (scoutsession) && (scout_state != SCOUT_OFF))
	{
		if ((scout_state == SCOUT_SYNCED) && (scout_outpos + scout_offset >= vis_outpos + (long)SCOUT_LEAD_SECONDS * gme_cfg_rate))
		{
			break;
		}
		if (gme_play (scoutsession, SCOUT_CHUNK * 2, buf))
		{
			scout_state = SCOUT_OFF;
			break;
		}
		scout_outpos += SCOUT_CHUNK;
		budget -= SCOUT_CHUNK;
		scout_read (); /* keep the libgme frame ring from overflowing while catching up */
	}
}

static void gmeApplyLength (void)
{
	long length = -1;
	long fade;

	if (!gmesession)
	{
		return;
	}
	if (gmetrackinfo)
	{
		if (gmetrackinfo->length > 0)
		{
			length = gmetrackinfo->length;
		} else if (gmetrackinfo->loop_length > 0)
		{
			length = ((gmetrackinfo->intro_length > 0) ? gmetrackinfo->intro_length : 0) + (long)gme_cfg_loops * gmetrackinfo->loop_length;
		}
	}
	if (length <= 0)
	{
		length = (long)gme_cfg_defaultlength * 1000;
	}
	if (length <= 0)
	{
		return; /* play forever (until silence is detected) */
	}
	fade = (gmetrackinfo && (gmetrackinfo->fade_length > 0)) ? gmetrackinfo->fade_length : (long)gme_cfg_fade * 1000;
	gme_set_fade_msecs (gmesession, length, fade);
	if (scoutsession)
	{
		gme_set_fade_msecs (scoutsession, length, fade); /* the scout stops where the track stops */
	}
}

/* start a sub-song, refresh its info and apply the length/fade limit */
static const char *gmeStartTrackInternal (int track)
{
	const char *result;

	vis_track_reset ();
	if ((result = gme_start_track (gmesession, gmeactivetrack = track)))
	{
		return result;
	}
	scout_restart (1); /* both emulators start together: same stamps */
	if (gmetrackinfo)
	{
		gme_free_info (gmetrackinfo);
		gmetrackinfo = 0;
	}
	if ((result = gme_track_info (gmesession, &gmetrackinfo, gmeactivetrack)))
	{
		fprintf (stderr, "[GME] gme_track_info(): %s\n", result);
		gmetrackinfo = 0;
	}
	gmeApplyLength ();
	return 0;
}

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

static void gmeIdler (struct cpifaceSessionAPI_t *cpifaceSession)
{
	int pos1, pos2;
	int length1, length2;

	cpifaceSession->ringbufferAPI->get_head_samples (gmebufpos, &pos1, &length1, &pos2, &length2);

	while (length1)
	{
		const char *result;
		if (gme_track_ended (gmesession))
		{
			if (gmeactivetrack + 1 >= gme_track_count (gmesession))
			{
				if (doloop)
				{
					gmeStartTrackInternal (0);
				} else {
					gmelooped |= 1;
				}
				return;
			} else {
				if ((result = gmeStartTrackInternal (gmeactivetrack + 1)))
				{
					fprintf (stderr, "[GME] gme_start_track(): %s\n", result);
					return;
				}
			}
		}
		result = gme_play (gmesession, length1 << 1, gmebuf + (pos1<<1));
		if (result)
		{
			fprintf (stderr, "[GME] gme_play(): %s\n", result);
			break;
		}

		cpifaceSession->ringbufferAPI->head_add_samples (gmebufpos, length1);
		vis_outpos += length1;
		if (vis_enabled)
		{
			struct vis_slot_t *slot = &vis_slots[vis_slot_next];
			scout_run ();
			vis_pull ();
			/* never wait on a busy slot: a callback lost in a freed ringbuffer would freeze the view for good.
			 * If an old callback for this slot still fires, the generation check below ignores it. */
			{
				vis_slot_next = (vis_slot_next + 1) % VIS_SLOTS;
				slot->busy = 1;
				slot->pos = vis_outpos;
				slot->generation = vis_generation;
				cpifaceSession->ringbufferAPI->add_tail_callback_samples (gmebufpos, 1, vis_callback_from_ringbuffer, slot);
			}
		}
		cpifaceSession->ringbufferAPI->get_head_samples (gmebufpos, &pos1, &length1, &pos2, &length2);
	}
}

OCP_INTERNAL void gmeIdle (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->InPause || (gmelooped == 3))
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

			gmeIdler (cpifaceSession);

			/* how much data is available.. we are using a ringbuffer, so we might receive two fragments */
			cpifaceSession->ringbufferAPI->get_tail_samples (gmebufpos, &pos1, &length1, &pos2, &length2);

			if (gmebufrate==0x10000)
			{
				if (targetlength>(length1+length2))
				{
					targetlength=(length1+length2); // limiting targetlength here, saves us from doing this per sample later
					gmelooped |= 2;
				} else {
					gmelooped &= ~2;
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

						rs = gmebuf[(pos1<<1) + 0];
						ls = gmebuf[(pos1<<1) + 1];

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
				gmelooped &= ~2;

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
							gmelooped |= 2;
							break;
						}
						/* will we overflow the wavebuf if we advance? */
						if ((length1+length2) < ((gmebufrate+gmebuffpos)>>16))
						{
							gmelooped |= 2;
							break;
						}

						switch (length1) /* if we are close to the wrap between buffer segment 1 and 2, len1 will grow down to a small number */
						{
							case 1:  wpm1 = pos1; wp0 = pos2;     wp1 = pos2 + 1; wp2 = pos2 + 2; break;
							case 2:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos2;     wp2 = pos2 + 1; break;
							case 3:  wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos2;     break;
							default: wpm1 = pos1; wp0 = pos1 + 1; wp1 = pos1 + 2; wp2 = pos1 + 3; break;
						}

						rvm1 = (uint16_t)gmebuf[(wpm1<<1)+0]^0x8000; /* we temporary need data to be unsigned - hence the ^0x8000 */
						lvm1 = (uint16_t)gmebuf[(wpm1<<1)+1]^0x8000;
						 rc0 = (uint16_t)gmebuf[(wp0 <<1)+0]^0x8000;
						 lc0 = (uint16_t)gmebuf[(wp0 <<1)+1]^0x8000;
						 rv1 = (uint16_t)gmebuf[(wp1 <<1)+0]^0x8000;
						 lv1 = (uint16_t)gmebuf[(wp1 <<1)+1]^0x8000;
						 rv2 = (uint16_t)gmebuf[(wp2 <<1)+0]^0x8000;
						 lv2 = (uint16_t)gmebuf[(wp2 <<1)+1]^0x8000;

						rc1 = rv1-rvm1;
						rc2 = 2*rvm1-2*rc0+rv1-rv2;
						rc3 = rc0-rvm1-rv1+rv2;
						rc3 =  imulshr16(rc3,gmebuffpos);
						rc3 += rc2;
						rc3 =  imulshr16(rc3,gmebuffpos);
						rc3 += rc1;
						rc3 =  imulshr16(rc3,gmebuffpos);
						rc3 += rc0;
						if (rc3<0)
							rc3=0;
						if (rc3>65535)
							rc3=65535;

						lc1 = lv1-lvm1;
						lc2 = 2*lvm1-2*lc0+lv1-lv2;
						lc3 = lc0-lvm1-lv1+lv2;
						lc3 =  imulshr16(lc3,gmebuffpos);
						lc3 += lc2;
						lc3 =  imulshr16(lc3,gmebuffpos);
						lc3 += lc1;
						lc3 =  imulshr16(lc3,gmebuffpos);
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

						gmebuffpos+=gmebufrate;
						progress = gmebuffpos>>16;
						gmebuffpos &= 0xffff;
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
			} /* if (gmebufrate==0x10000) */
			vis_commit_target = accumulated_target;
			cpifaceSession->ringbufferAPI->tail_consume_samples (gmebufpos, accumulated_source);
			cpifaceSession->plrDevAPI->CommitBuffer (accumulated_target);
		} /* if (targetlength) */
	}

	cpifaceSession->plrDevAPI->Idle();

}



static void gmeSetSpeed (uint16_t sp)
{
	if (sp < 4)
		sp = 4;
	gme_set_tempo (gmesession, (double)sp / 256.0f);
	if (sp != scout_tempo)
	{ /* the scout is ahead and cannot follow; main emulator frames until the tempo is stable again */
		scout_tempo = sp;
		scout_tempo_changed = vis_outpos;
		if (scout_state != SCOUT_OFF)
		{
			scout_state = SCOUT_OFF;
			gme_vis_enable (scoutsession, 0);
		}
	}
}

static void gmeSetPitch (uint16_t sp)
{
	if (sp < 4)
		sp = 4;
	gmebufrate = 256 * sp;
}

static void gmeSetVolume (void)
{
	volr = voll = vol * 4;
	if (bal < 0)
	{
		voll = (voll * (64 + bal)) >> 6;
	} else {
		volr = (volr * (64 - bal)) >> 6;
	}
}

static void gmeSet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt, int val)
{
	switch (opt)
	{
		case mcpMasterSpeed:
			gmeSetSpeed(val);
			break;
		case mcpMasterPitch:
			gmeSetPitch(val);
			break;
		case mcpMasterSurround:
			srnd=val;
			break;
		case mcpMasterPanning:
			pan=val;
			gmeSetVolume();
			break;
		case mcpMasterVolume:
			vol=val;
			gmeSetVolume();
			break;
		case mcpMasterBalance:
			bal=val;
			gmeSetVolume();
			break;
	}
}

static int gmeGet (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int opt)
{
	return 0;
}

OCP_INTERNAL int gmeOpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession)
{
	uint32_t gmeRate;
	enum plrRequestFormat format;
	unsigned char *data;
	uint64_t datalen;
	const char *result;
	uint32_t gmebuflen;

	assert (!gmesession);

	if (!cpifaceSession->plrDevAPI)
	{
		return errPlay;
	}

	datalen = file->filesize(file);
	if (datalen > 2*1024*1024)
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[GME] File too big\n");
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

	if ((datalen > 428) &&
	    (!memcmp (data, "GYMX", 4)) &&
	    (data[424] || data[425] || data[426] || data[427]))
	{
		/* stock libgme does not support compressed GYM files */
		uLong unpackedsize =  (uint_fast32_t)(data[424])        |
		                     ((uint_fast32_t)(data[425]) <<  8) |
		                     ((uint_fast32_t)(data[426]) << 16) |
		                     ((uint_fast32_t)(data[427]) << 24);
		if (unpackedsize < 2*1024*1024)
		{
			uint8_t *newdata = calloc (428 + unpackedsize, 1);
			if (!newdata)
			{
				return errAllocMem;
			}
			memcpy (newdata, data, 424); /* copy header except packed size */
			if (uncompress (newdata + 428, &unpackedsize, data + 428, datalen - 428) == Z_OK)
			{
				cpifaceSession->cpiDebug (cpifaceSession, "[GME] Predecompressed GYMX file, %" PRIu64" bytes decompressed into %lu\n", datalen - 428, unpackedsize);
				free (data);
				data = newdata;
				datalen = 428 + unpackedsize;
			}
		}
	}

	gmeRate=0;
	format=PLR_STEREO_16BIT_SIGNED;
	if (!cpifaceSession->plrDevAPI->Play (&gmeRate, &format, file, cpifaceSession))
	{
		free (data);
		return errPlay;
	}
	vis_chips_header = 0;
	if ((datalen > 0x80) && !memcmp (data, "NESM\x1a", 5))
	{
		vis_chips_header = data[0x7b];
	} else if ((datalen > 12) && !memcmp (data, "NSFE", 4))
	{
		uint64_t o = 4;
		while (o + 8 <= datalen)
		{
			uint32_t len = data[o] | (data[o+1] << 8) | (data[o+2] << 16) | ((uint32_t)data[o+3] << 24);
			if (!memcmp (data + o + 4, "INFO", 4))
			{
				if ((len >= 8) && (o + 8 + 8 <= datalen))
				{
					vis_chips_header = data[o + 8 + 7];
				}
				break;
			}
			o += 8 + (uint64_t)len;
		}
	}
	if ((result = gme_open_data (data, datalen, &gmesession, gmeRate)))
	{
		cpifaceSession->cpiDebug (cpifaceSession, "[GME]: %s\n", result);
		free (data);
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		gmesession = 0;
		return errFormMiss;
	}
	gme_cfg_rate = gmeRate;
	scout_state = SCOUT_OFF;
	scout_tempo = 256;
	scout_tempo_changed = 0;
	scoutsession = 0;
	if (cpifaceSession->configAPI->GetProfileBool ("gme", "trackerview", 1, 1) && gme_open_data (data, datalen, &scoutsession, gmeRate))
	{
		scoutsession = 0;
	}
	free (data);
	data = 0;

	gme_cfg_loops         = cpifaceSession->configAPI->GetProfileInt ("gme", "loops",         2,   10);
	gme_cfg_defaultlength = cpifaceSession->configAPI->GetProfileInt ("gme", "defaultlength", 150, 10);
	gme_cfg_fade          = cpifaceSession->configAPI->GetProfileInt ("gme", "fade",          8,   10);
	if (gme_cfg_loops < 1) gme_cfg_loops = 1;
	if (gme_cfg_defaultlength < 0) gme_cfg_defaultlength = 0;
	if (gme_cfg_defaultlength > 36000) gme_cfg_defaultlength = 36000;
	if (gme_cfg_fade < 0) gme_cfg_fade = 0;
	if (gme_cfg_fade > 60) gme_cfg_fade = 60;

	vis_rowframes = cpifaceSession->configAPI->GetProfileInt ("gme", "rowframes", 6, 10);
	if (vis_rowframes < 1) vis_rowframes = 1;
	if (vis_rowframes > 32) vis_rowframes = 32;
	vis_enabled = 0;
	vis_layout_done = 0;
	vis_plrDevAPI = cpifaceSession->plrDevAPI;
	vis_generation++;
	memset (vis_slots, 0, sizeof (vis_slots));
	vis_slot_next = 0;
	if (cpifaceSession->configAPI->GetProfileBool ("gme", "trackerview", 1, 1))
	{
		vis_enabled = gme_vis_enable (gmesession, 1);
	}
	if (scoutsession && !vis_enabled)
	{
		gme_delete (scoutsession);
		scoutsession = 0;
	}

	if ((result = gmeStartTrackInternal (0)))
	{
		vis_enabled = 0;
		cpifaceSession->cpiDebug (cpifaceSession, "[GME]: %s\n", result);
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		gme_delete (gmesession);
		gmesession = 0;
		return errFormMiss;
	}

	if (vis_enabled)
	{
		scout_run (); /* the first frames give the channel layout */
		vis_pull ();
		if (!vis_layout_done)
		{
			gme_nsf_frame_t empty;
			memset (&empty, 0, sizeof (empty));
			empty.chips = vis_chips_header;
			empty.n163[0x7F] = 0x70; /* unknown: 8 channels */
			if (gme_type (gmesession) == gme_hes_type) empty.system = 1;
			if (gme_type (gmesession) == gme_gbs_type) empty.system = 2;
			nsfvis_layout (&vis_layout, &empty);
			vis_ring = calloc ((size_t)VIS_RING * vis_layout.count, sizeof (struct nsfvis_ch_t));
			vis_layout_done = vis_ring != 0;
			vis_enabled = vis_layout_done;
		}
	}

	gmelooped = 0;
	gmebuflen = gmeRate >> 4;
	gmebuf = malloc(gmebuflen <<  2 /* stereo + 16bit */);
	if (!gmebuf)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		gme_delete (gmesession);
		gmesession = 0;
		return errAllocMem;
	}

	gmebufpos = cpifaceSession->ringbufferAPI->new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED, gmebuflen);
	if (!gmebufpos)
	{
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);

		free (gmebuf);
		gmebuf = 0;

		gme_delete (gmesession);
		gmesession = 0;

		return errAllocMem;
	}
	gmebuffpos=0;

	cpifaceSession->mcpSet = gmeSet;
	cpifaceSession->mcpGet = gmeGet;

	cpifaceSession->Normalize (cpifaceSession, mcpNormalizeCanSpeedPitchUnlock);

	return errOk;
}

void gmeClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession)
{
	if (cpifaceSession->plrDevAPI)
	{
		cpifaceSession->plrDevAPI->Stop(cpifaceSession);
	}

	if (gmetrackinfo)
	{
		gme_free_info (gmetrackinfo);
		gmetrackinfo = 0;
	}

	vis_enabled = 0;
	vis_layout_done = 0;
	vis_generation++;
	free (vis_ring);
	vis_ring = 0;
	vis_count = 0;

	scout_state = SCOUT_OFF;
	if (scoutsession)
	{
		gme_delete (scoutsession);
		scoutsession = 0;
	}

	if (gmesession)
	{
		gme_delete (gmesession);
		gmesession = 0;
	}

	if (gmebufpos)
	{
		cpifaceSession->ringbufferAPI->free (gmebufpos);
		gmebufpos = 0;
	}

	free(gmebuf);
	gmebuf = 0;
}

OCP_INTERNAL int gmeChannelCount (void)
{
	int n;
	if (!gmesession)
	{
		return 0;
	}
	if (gmeVisActive ())
	{
		return vis_layout.count;
	}
	n = gme_voice_count (gmesession);
	return (n > 64) ? 64 : n;
}

OCP_INTERNAL void gmeSetMute (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int mute)
{
	int voice = ch;
	cpifaceSession->MuteChannel[ch] = mute;
	if (!gmesession)
	{
		return;
	}
	if (gmeVisActive ())
	{
		voice = (ch < vis_layout.count) ? vis_layout.voice[ch] : -1;
	}
	if (voice >= 0 && voice < gme_voice_count (gmesession))
	{
		gme_mute_voice (gmesession, voice, mute);
	}
}

OCP_INTERNAL void gmeSetLoop (int s)
{
	doloop = s;
}

OCP_INTERNAL void gmeStartSong (struct cpifaceSessionAPI_t *cpifaceSession, int song)
{
	if (gmesession && (song >= 0) && (song < gme_track_count (gmesession)))
	{
		const char *result;
		if ((result = gmeStartTrackInternal (song)))
		{
			fprintf (stderr, "[GME] gme_start_track(): %s\n", result);
		}
	}
}

OCP_INTERNAL int gmeIsLooped (void)
{
	return gmelooped == 3;
}

OCP_INTERNAL void gmeGetInfo (struct gmeinfo *info)
{
	info->track = gmeactivetrack;
	info->numtracks   = gme_track_count (gmesession);
	info->system      = (gmetrackinfo && gmetrackinfo->system   ) ? gmetrackinfo->system    : "";
	info->game        = (gmetrackinfo && gmetrackinfo->game     ) ? gmetrackinfo->game      : "";
	info->song        = (gmetrackinfo && gmetrackinfo->song     ) ? gmetrackinfo->song      : "";
	info->author      = (gmetrackinfo && gmetrackinfo->author   ) ? gmetrackinfo->author    : "";
	info->copyright   = (gmetrackinfo && gmetrackinfo->copyright) ? gmetrackinfo->copyright : "";
	info->comment     = (gmetrackinfo && gmetrackinfo->comment  ) ? gmetrackinfo->comment   : "";
	info->dumper      = (gmetrackinfo && gmetrackinfo->dumper   ) ? gmetrackinfo->dumper    : "";
	info->length      = gmetrackinfo ? gmetrackinfo->length : -1;
	info->introlength = gmetrackinfo ? gmetrackinfo->intro_length : -1;
	info->looplength  = gmetrackinfo ? gmetrackinfo->loop_length : -1;
	info->playlength  = gmetrackinfo ? gmetrackinfo->play_length : -1;
}
