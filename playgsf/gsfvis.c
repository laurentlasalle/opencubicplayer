/* OpenCP Module Player
 * Tracker view of the GSF files that use the MP2K sound engine (see gsfvis.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * The layout of the engine structures comes from the decompilations of GBA games
 * that use it (for example the m4a library of pret/pokeemerald):
 *
 *  SoundInfo (pointer at 0x03007FF0)
 *    +0   u32 ident        0x68736D53 ("Smsh"), +1 while the engine works on it
 *    +6   u8  maxChans     Direct Sound voices used (up to 12)
 *    +28  CgbChannel *     the 4 Game Boy channels (64 bytes each)
 *    +80  SoundChannel[12] the Direct Sound voices (64 bytes each)
 *  SoundChannel and CgbChannel (the start is the same)
 *    +0   u8  statusFlags  0x80 start, 0x40 release, 0x03 envelope phase (3 attack)
 *    +1   u8  type         CgbChannel: 1 square 1, 2 square 2, 3 wave, 4 noise
 *    +9   u8  envelopeVolume (0..255 Direct Sound, 0..15 Game Boy)
 *    +17  u8  midiKey      the note (MIDI numbers, 60 = C-4)
 *    +36  wav *            the sample (or the duty / waveform of a Game Boy channel)
 *    +44  MusicPlayerTrack * the track that plays this voice
 *  MusicPlayerInfo (one per player: music, sound effects and others)
 *    +0   SongHeader *     the song (its first byte is its number of tracks)
 *    +8   u8  trackCount   tracks allocated to the player
 *    +44  MusicPlayerTrack * its tracks (80 bytes each)
 *    +52  u32 ident        0x68736D53
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "../playgme/nsfvis.h"
#include "gsfemu.h"
#include "gsfvis.h"

#define MP2K_POINTER   0x03007FF0
#define MP2K_IDENT     0x68736D53
#define SOUNDINFO_MAXCHANS 6
#define SOUNDINFO_CGB      28
#define SOUNDINFO_CHANS    0x50
#define CHAN_SIZE          64
#define CHAN_STATUS        0
#define CHAN_TYPE          1
#define CHAN_ENV           9
#define CHAN_KEY           17
#define CHAN_WAV           36
#define CHAN_TRACK         44
#define PLAYER_SONG        0
#define PLAYER_IDENT       52

#define DS_VOICES 12
#define GB_VOICES 4

/* a GBA video frame is 280896 cycles of the 16.78 MHz processor */
#define FRAME_SECONDS (280896.0 / 16777216.0)

/* the engine and the song are looked for during the first seconds */
#define PROBE_FRAMES       (60 * 15) /* until a note is heard */
#define PROBE_AFTER_NOTE   (60 * 3)  /* then, to see which player plays the most */

struct voice_state_t
{
	uint8_t active;
	uint8_t status;
	uint8_t key;
};

struct gsfvis_t
{
	struct gsfemu_t *scout;
	struct nsfvis_layout_t layout;
	uint32_t player;   /* MusicPlayerInfo of the song */
	uint32_t tracks;   /* its MusicPlayerTrack array */
	int trackcount;    /* columns of tracks */
	long frame;        /* frames run since the start */
	struct voice_state_t ds[DS_VOICES], gb[GB_VOICES];
	struct nsfvis_ch_t ch[GSFVIS_MAXTRACKS + GB_VOICES];
};

static uint32_t soundinfo (struct gsfemu_t *e)
{
	uint32_t si = gsfemu_read32 (e, MP2K_POINTER);
	if (((si >> 24) != 0x02) && ((si >> 24) != 0x03))
	{
		return 0;
	}
	if ((gsfemu_read32 (e, si) - MP2K_IDENT) > 8) /* the ident, or ident + 1 while the engine works */
	{
		return 0;
	}
	return si;
}

static int is_ram (uint32_t p)
{
	return ((p >= 0x02000000) && (p < 0x02040000)) || ((p >= 0x03000000) && (p < 0x03008000));
}

/* the player that owns a track, from all the players found in the work RAM */
struct player_t
{
	uint32_t info, tracks;
	int count;
	int voices; /* voices seen during the probe */
};

static int find_players (struct gsfemu_t *e, uint32_t si, struct player_t *players, int max)
{
	static const uint32_t ranges[2][2] = {{0x02000000, 0x02040000}, {0x03000000, 0x03008000}};
	int r, n = 0;
	for (r = 0; r < 2; r++)
	{
		uint32_t a;
		for (a = ranges[r][0] + PLAYER_IDENT; (a < ranges[r][1]) && (n < max); a += 4)
		{
			uint32_t info, tracks;
			int count;
			if (gsfemu_read32 (e, a) != MP2K_IDENT)
			{
				continue;
			}
			info = a - PLAYER_IDENT;
			if (info == si)
			{
				continue;
			}
			tracks = gsfemu_read32 (e, info + MP2K_PLAYER_TRACKS);
			count = gsfemu_read8 (e, info + MP2K_PLAYER_TRACKCOUNT);
			if (!is_ram (tracks) || (count < 1) || (count > GSFVIS_MAXTRACKS))
			{
				continue;
			}
			players[n].info = info;
			players[n].tracks = tracks;
			players[n].count = count;
			players[n].voices = 0;
			n++;
		}
	}
	return n;
}

static void count_voices (struct gsfemu_t *e, uint32_t si, struct player_t *players, int n)
{
	int i, j, max = gsfemu_read8 (e, si + SOUNDINFO_MAXCHANS);
	uint32_t cgb = gsfemu_read32 (e, si + SOUNDINFO_CGB);
	if (max > DS_VOICES) max = DS_VOICES;
	for (i = 0; i < max + GB_VOICES; i++)
	{
		uint32_t c, track;
		if (i < max)
		{
			c = si + SOUNDINFO_CHANS + i * CHAN_SIZE;
		} else if (is_ram (cgb))
		{
			c = cgb + (i - max) * CHAN_SIZE;
		} else {
			break;
		}
		if (!(gsfemu_read8 (e, c + CHAN_STATUS) & 0xC7))
		{
			continue;
		}
		track = gsfemu_read32 (e, c + CHAN_TRACK);
		for (j = 0; j < n; j++)
		{
			if ((track >= players[j].tracks) && (track < players[j].tracks + players[j].count * MP2K_TRACK_SIZE))
			{
				players[j].voices++;
				break;
			}
		}
	}
}

struct gsfvis_t *gsfvis_open (const uint8_t *image, size_t size, int multiboot)
{
	struct gsfvis_t *v = calloc (1, sizeof (*v));
	struct player_t players[32];
	int nplayers = 0, f, heard = -1, i, best = -1;
	uint32_t si = 0, song;
	char name[10];

	if (!v)
	{
		return 0;
	}
	v->scout = gsfemu_open (image, size, multiboot, 44100);
	if (!v->scout)
	{
		free (v);
		return 0;
	}

	/* probe: the engine, the first note, then the player that plays the most */
	for (f = 0; f < PROBE_FRAMES; f++)
	{
		gsfemu_frame (v->scout);
		if (!(si = soundinfo (v->scout)))
		{
			continue;
		}
		if (heard < 0)
		{
			int max = gsfemu_read8 (v->scout, si + SOUNDINFO_MAXCHANS);
			for (i = 0; (i < max) && (i < DS_VOICES); i++)
			{
				if (gsfemu_read8 (v->scout, si + SOUNDINFO_CHANS + i * CHAN_SIZE + CHAN_STATUS) & 0xC7)
				{
					break;
				}
			}
			if ((i == max) || (i == DS_VOICES))
			{
				uint32_t cgb = gsfemu_read32 (v->scout, si + SOUNDINFO_CGB);
				for (i = 0; (i < GB_VOICES) && is_ram (cgb); i++)
				{
					if (gsfemu_read8 (v->scout, cgb + i * CHAN_SIZE + CHAN_STATUS) & 0xC7)
					{
						break;
					}
				}
				if (!is_ram (cgb) || (i == GB_VOICES))
				{
					continue;
				}
			}
			heard = f;
			nplayers = find_players (v->scout, si, players, 32);
			if (!nplayers)
			{
				break;
			}
		}
		count_voices (v->scout, si, players, nplayers);
		if (f >= heard + PROBE_AFTER_NOTE)
		{
			break;
		}
	}
	for (i = 0; i < nplayers; i++)
	{
		if (players[i].voices && ((best < 0) || (players[i].voices > players[best].voices)))
		{
			best = i;
		}
	}
	if (best < 0)
	{
		gsfvis_close (v);
		return 0;
	}

	v->player = players[best].info;
	v->tracks = players[best].tracks;
	v->trackcount = players[best].count;
	/* the song may use fewer tracks than the player has */
	song = gsfemu_read32 (v->scout, v->player + PLAYER_SONG);
	if ((song >> 24) == 0x08 || (song >> 24) == 0x09 || is_ram (song))
	{
		int n = gsfemu_read8 (v->scout, song);
		if ((n >= 1) && (n <= v->trackcount))
		{
			v->trackcount = n;
		}
	}

	for (i = 0; i < v->trackcount; i++)
	{
		snprintf (name, sizeof (name), "Track %d", (i + 1) % 100);
		nsfvis_add_channel (&v->layout, 0x100 + i, i, name);
	}
	nsfvis_add_channel (&v->layout, 0, 0x100, "Square 1");
	nsfvis_add_channel (&v->layout, 1, 0x101, "Square 2");
	nsfvis_add_channel (&v->layout, 2, 0x102, "Wave");
	nsfvis_add_channel (&v->layout, 3, 0x103, "Noise");

	gsfvis_reset (v);
	return v;
}

void gsfvis_close (struct gsfvis_t *v)
{
	if (!v)
	{
		return;
	}
	gsfemu_close (v->scout);
	free (v);
}

void gsfvis_reset (struct gsfvis_t *v)
{
	gsfemu_restart (v->scout);
	v->frame = 0;
	memset (v->ds, 0, sizeof (v->ds));
	memset (v->gb, 0, sizeof (v->gb));
}

const struct nsfvis_layout_t *gsfvis_layout (struct gsfvis_t *v)
{
	return &v->layout;
}

int gsfvis_tracks (struct gsfvis_t *v)
{
	return v->trackcount;
}

uint32_t gsfvis_player (struct gsfvis_t *v)
{
	return v->player;
}

/* reads one voice; returns its track number, -1 if it is silent or not of the song */
static int read_voice (struct gsfvis_t *v, uint32_t c, struct voice_state_t *state, int gb, struct nsfvis_ch_t *out, int *score)
{
	uint8_t status = gsfemu_read8 (v->scout, c + CHAN_STATUS);
	uint8_t key = gsfemu_read8 (v->scout, c + CHAN_KEY);
	uint8_t env = gsfemu_read8 (v->scout, c + CHAN_ENV);
	uint32_t track, wav;
	unsigned char sig[5];
	int active = (status & 0xC7) != 0;
	int trig;

	trig = active && ((status & 0x80) ||                             /* started, not yet seen by the mixer */
	                  !state->active ||                              /* a new voice */
	                  (key != state->key) ||                         /* another note */
	                  (((status & 3) == 3) && ((state->status & 3) != 3))); /* the envelope starts again */
	state->active = active;
	state->status = status;
	state->key = key;
	if (!active)
	{
		return -1;
	}

	out->active = 1;
	out->trig = trig;
	out->kind = NSFVIS_TONE;
	out->note = key;
	out->pitch = key * 100;
	out->vol = gb ? (env & 15) : (env >> 4);
	wav = gsfemu_read32 (v->scout, c + CHAN_WAV);
	sig[0] = gsfemu_read8 (v->scout, c + CHAN_TYPE);
	sig[1] = wav;
	sig[2] = wav >> 8;
	sig[3] = wav >> 16;
	sig[4] = wav >> 24;
	out->ins = nsfvis_wave_id (&v->layout, sig, 5);
	out->aux = 0;
	*score = (trig ? 0x1000 : 0) + (out->vol << 7) + key; /* newest, then loudest, then highest */

	track = gsfemu_read32 (v->scout, c + CHAN_TRACK);
	if ((track < v->tracks) || ((track - v->tracks) % MP2K_TRACK_SIZE) || ((int)((track - v->tracks) / MP2K_TRACK_SIZE) >= v->trackcount))
	{
		return -2; /* a voice of another player (sound effects) */
	}
	return (track - v->tracks) / MP2K_TRACK_SIZE;
}

static void decode_frame (struct gsfvis_t *v)
{
	int scores[GSFVIS_MAXTRACKS];
	int i, max;
	uint32_t si = soundinfo (v->scout), cgb;
	struct nsfvis_ch_t *gbcols = v->ch + v->trackcount;

	for (i = 0; i < v->trackcount + GB_VOICES; i++)
	{
		v->ch[i].active = 0;
		v->ch[i].trig = 0;
		v->ch[i].kind = NSFVIS_TONE;
		v->ch[i].note = -1;
		v->ch[i].ins = 0xFF;
		v->ch[i].vol = 0;
	}
	memset (scores, 0, sizeof (scores));
	if (!si)
	{
		return;
	}
	max = gsfemu_read8 (v->scout, si + SOUNDINFO_MAXCHANS);
	if (max > DS_VOICES) max = DS_VOICES;
	cgb = gsfemu_read32 (v->scout, si + SOUNDINFO_CGB);

	for (i = 0; i < max + GB_VOICES; i++)
	{
		struct nsfvis_ch_t voice;
		int score, t, gb = i >= max;
		uint32_t c;
		if (!gb)
		{
			c = si + SOUNDINFO_CHANS + i * CHAN_SIZE;
		} else if (is_ram (cgb))
		{
			c = cgb + (i - max) * CHAN_SIZE;
		} else {
			break;
		}
		memset (&voice, 0, sizeof (voice));
		t = read_voice (v, c, gb ? v->gb + (i - max) : v->ds + i, gb, &voice, &score);
		if (t == -1)
		{
			continue;
		}
		if (gb)
		{
			gbcols[i - max] = voice;
		}
		if (t >= 0)
		{ /* the track shows its main voice; a new note on any of its voices is a new note */
			int trig = v->ch[t].trig || voice.trig;
			if (!v->ch[t].active || (score > scores[t]))
			{
				v->ch[t] = voice;
				scores[t] = score;
			}
			v->ch[t].trig = trig;
		}
	}
}

void gsfvis_run_until (struct gsfvis_t *v, double t, void (*emit)(void *arg, double time, const struct nsfvis_ch_t *ch), void *arg)
{
	while ((v->frame + 1) * FRAME_SECONDS <= t)
	{
		gsfemu_frame (v->scout);
		v->frame++;
		if (emit)
		{
			decode_frame (v);
			emit (arg, v->frame * FRAME_SECONDS, v->ch);
		}
	}
}
