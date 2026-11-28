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
#include "gsfvis.h"
#include "../playgme/nsfvis.h"

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

/* Tracker view (shared with playgme, see playgme/nsftrak.c and gsfvis.h): the scout
 * runs a few seconds ahead of the audio. Emulated time t is mapped to output sample
 * frames with vis_out0 + (t - vis_t0) * rate / speed. */
#define VIS_RING 16384
#define VIS_SLOTS 512
#define VIS_LEAD_SECONDS 4
static struct gsfvis_t *vis;
static int vis_enabled;
static const struct nsfvis_layout_t *vis_layout;
static struct nsfvis_ch_t *vis_ring;
static long vis_sample[VIS_RING];
static double vis_time[VIS_RING];
static long vis_first, vis_count;
static long vis_outpos;                    /* sample frames rendered since the song (re)started */
static volatile long vis_playpos;          /* sample frames heard */
static volatile int vis_generation;
static int vis_newdata;
static int vis_rowframes = 6;
static double vis_t0, vis_speed = 1.0;
static long vis_out0;
static unsigned int vis_commit_target;
static const struct plrDevAPI_t *vis_plrDevAPI;
static struct vis_slot_t { long pos; int generation; int busy; } vis_slots[VIS_SLOTS];
static int vis_slot_next;
static uint8_t track_muted[GSFVIS_MAXTRACKS];

static long vis_stamp (double t)
{
	return vis_out0 + (long)((t - vis_t0) * gsfRate / vis_speed);
}

static void vis_emit (void *arg, double t, const struct nsfvis_ch_t *ch)
{
	long stamp = vis_stamp (t);
	long idx;
	if (vis_count == VIS_RING)
	{
		vis_first++;
		vis_count--;
	}
	idx = (vis_first + vis_count) % VIS_RING;
	memcpy (vis_ring + idx * vis_layout->count, ch, sizeof (*ch) * vis_layout->count);
	vis_sample[idx] = stamp;
	vis_time[idx] = t;
	vis_count++;
	vis_newdata = 1;
}

static void vis_song_restart (void)
{
	vis_generation++;
	vis_first = vis_count = 0;
	vis_outpos = 0;
	vis_playpos = 0;
	vis_t0 = 0.0;
	vis_out0 = 0;
	if (vis) gsfvis_reset (vis);
}

/* runs the scout up to VIS_LEAD_SECONDS ahead of what is rendered */
static void vis_advance (void)
{
	if (vis_enabled)
	{
		gsfvis_run_until (vis, vis_t0 + (double)(vis_outpos + (long)VIS_LEAD_SECONDS * gsfRate - vis_out0) * vis_speed / gsfRate, vis_emit, 0);
	}
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

static void vis_callback_from_ringbuffer (void *arg, int samples_ago)
{
	struct vis_slot_t *slot = arg;
	int samples_until = (int)vis_commit_target - (gsfbufrate ? (int)((int64_t)samples_ago * 65536 / gsfbufrate) : samples_ago);
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

/* a change of speed: the frames the scout has run ahead get new positions */
static void vis_set_speed (double speed)
{
	long i;
	if (!vis_enabled || (speed == vis_speed))
	{
		vis_speed = speed;
		return;
	}
	vis_t0 = vis_t0 + (double)(vis_outpos - vis_out0) * vis_speed / gsfRate;
	vis_out0 = vis_outpos;
	vis_speed = speed;
	for (i = vis_first; i < vis_first + vis_count; i++)
	{
		long idx = i % VIS_RING;
		if (vis_time[idx] > vis_t0)
		{
			vis_sample[idx] = vis_stamp (vis_time[idx]);
		}
	}
}

/* interface used by playgme/nsftrak.c */
OCP_INTERNAL int gmeVisActive (void) { return vis_enabled; }
OCP_INTERNAL const struct nsfvis_layout_t *gmeVisLayout (void) { return vis_layout; }
OCP_INTERNAL int gmeVisRowFrames (void) { return vis_rowframes; }
OCP_INTERNAL int gmeVisTakeNewData (void) { int r = vis_newdata; vis_newdata = 0; return r; }
OCP_INTERNAL long gmeVisLastFrame (void) { return vis_first + vis_count - 1; }
OCP_INTERNAL long gmeVisCurrentFrame (void)
{
	long lo = vis_first, hi = vis_first + vis_count - 1, best = -1, pos = vis_playpos;
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
OCP_INTERNAL const struct nsfvis_ch_t *gmeVisFrame (long frame)
{
	if (!vis_ring || frame < vis_first || frame >= vis_first + vis_count)
	{
		return 0;
	}
	return vis_ring + (frame % VIS_RING) * vis_layout->count;
}

OCP_INTERNAL int gsfChannelCount (void)
{
	return vis_enabled ? vis_layout->count : GSFEMU_CHANNELS;
}

/* A track is muted through the volume that the program gives to it (volX, 64 =
 * full), which the sequence itself never changes: the engine is asked to compute
 * the volume of its voices again. This is done after each video frame of the
 * emulator that is heard, since the engine sets volX again when a song starts.
 * track_muted[]: 0 heard, 1 muted, 2 to be heard again. */
static void mute_tracks (void *arg, struct gsfemu_t *e)
{
	uint32_t tracks = gsfemu_read32 (e, gsfvis_player (vis) + MP2K_PLAYER_TRACKS);
	int i, busy = 0;
	for (i = 0; i < gsfvis_tracks (vis); i++)
	{
		uint32_t t = tracks + i * MP2K_TRACK_SIZE;
		uint8_t volx;
		if (!track_muted[i])
		{
			continue;
		}
		volx = gsfemu_read8 (e, t + MP2K_TRACK_VOLX);
		if ((track_muted[i] == 1) ? (volx != 0) : (volx == 0))
		{
			gsfemu_write8 (e, t + MP2K_TRACK_VOLX, (track_muted[i] == 1) ? 0 : 64);
			gsfemu_write8 (e, t + MP2K_TRACK_FLAGS, gsfemu_read8 (e, t + MP2K_TRACK_FLAGS) | 0x03);
		}
		if (track_muted[i] == 2)
		{
			track_muted[i] = 0;
		} else {
			busy = 1;
		}
	}
	if (!busy)
	{
		gsfemu_set_frame_hook (e, 0, 0); /* nothing is muted any more */
	}
}

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
		vis_outpos += length1;
		if (vis_enabled)
		{
			struct vis_slot_t *slot = &vis_slots[vis_slot_next];
			vis_advance ();
			vis_slot_next = (vis_slot_next + 1) % VIS_SLOTS;
			slot->busy = 1;
			slot->pos = vis_outpos;
			slot->generation = vis_generation;
			cpifaceSession->ringbufferAPI->add_tail_callback_samples (gsfbufpos, 1, vis_callback_from_ringbuffer, slot);
		}
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
			vis_commit_target = accumulated_target;
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
	vis_set_speed ((double)sp / 256.0);
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

/* without the tracker view, the channels are those of the Game Boy Advance (see
 * gsfemu.h); with it, the tracks of the song, then the 4 Game Boy channels */
OCP_INTERNAL void gsfSetMute (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int mute)
{
	cpifaceSession->MuteChannel[ch] = mute;
	if (!gsfsession)
	{
		return;
	}
	if (!vis_enabled)
	{
		gsfemu_mute (gsfsession, ch, mute);
	} else if (vis_layout->voice[ch] >= 0x100)
	{
		int t = vis_layout->voice[ch] - 0x100;
		track_muted[t] = mute ? 1 : (track_muted[t] ? 2 : 0);
		gsfemu_set_frame_hook (gsfsession, mute_tracks, 0); /* removes itself when nothing is muted */
	} else {
		gsfemu_mute (gsfsession, vis_layout->voice[ch], mute);
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
	if (!gsfsession)
	{
		free (image.data);
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
		free (image.data);
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		gsfemu_close (gsfsession);
		gsfsession = 0;
		return errAllocMem;
	}

	gsfbufpos = cpifaceSession->ringbufferAPI->new_samples (RINGBUFFER_FLAGS_STEREO | RINGBUFFER_FLAGS_16BIT | RINGBUFFER_FLAGS_SIGNED, gsfbuflen);
	if (!gsfbufpos)
	{
		free (image.data);
		cpifaceSession->plrDevAPI->Stop (cpifaceSession);
		free (gsfbuf);
		gsfbuf = 0;
		gsfemu_close (gsfsession);
		gsfsession = 0;
		return errAllocMem;
	}
	gsfbuffpos=0;

	/* the tracker view: a scout, if the program uses the MP2K engine */
	vis_plrDevAPI = cpifaceSession->plrDevAPI;
	vis_speed = 1.0;
	vis_enabled = 0;
	memset (vis_slots, 0, sizeof (vis_slots));
	memset (track_muted, 0, sizeof (track_muted));
	vis_slot_next = 0;
	vis_rowframes = cpifaceSession->configAPI->GetProfileInt ("gme", "rowframes", 6, 10);
	if (vis_rowframes < 1) vis_rowframes = 1;
	if (vis_rowframes > 32) vis_rowframes = 32;
	vis = cpifaceSession->configAPI->GetProfileBool ("gme", "trackerview", 1, 1) ? gsfvis_open (image.data, image.size, image.multiboot) : 0;
	if (vis)
	{
		vis_layout = gsfvis_layout (vis);
		vis_ring = calloc ((size_t)VIS_RING * vis_layout->count, sizeof (struct nsfvis_ch_t));
		vis_enabled = vis_ring != 0;
		cpifaceSession->cpiDebug (cpifaceSession, "[GSF] MP2K sound engine, %d tracks: tracker view\n", gsfvis_tracks (vis));
	}
	free (image.data);
	vis_song_restart ();
	vis_advance ();

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

	vis_enabled = 0;
	vis_generation++;
	free (vis_ring);
	vis_ring = 0;
	vis_count = 0;
	gsfvis_close (vis);
	vis = 0;

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
		vis_song_restart ();
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
