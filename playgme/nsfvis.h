#ifndef _NSFVIS_H
#define _NSFVIS_H 1

/* Decodes sound chip register snapshots (gme_nsf_frame_t / gme_vis_frame_t) of
 * NSF, HES, GBS, VGM/GYM and SPC files into tracker-like per channel information: note, volume,
 * "instrument" (duty / mode / patch / waveform number). */

#include <stdint.h>

#define NSFVIS_MAXCH 32

enum nsfvis_kind
{
	NSFVIS_TONE  = 0, /* pitched channel, note valid */
	NSFVIS_NOISE = 1, /* aux = period index 0..15 */
	NSFVIS_DPCM  = 2, /* aux = sample address register */
	NSFVIS_PCM   = 3, /* raw PCM output, no pitch */
	NSFVIS_DRUMS = 4  /* rhythm section, aux = bitmask of the drums started (bit 0 bass, 1 snare, 2 top cymbal, 3 hi-hat, 4 tom, 5 rim shot) */
};

struct nsfvis_ch_t
{
	int16_t note;    /* 12*(octave+1)+semitone (A-4 = 69), -1 if none */
	uint8_t vol;     /* 0..15 */
	uint8_t ins;     /* duty / noise mode / VRC7 patch, 0xFF = none */
	uint8_t active;  /* audible */
	uint8_t trig;    /* restarted during the frame */
	uint8_t aux;
	uint8_t kind;
	int16_t pitch;   /* exact pitch in cents (A-4 = 6900), tone channels only */
};

struct nsfvis_layout_t
{
	int  count;                      /* number of displayed channels */
	int  voice [NSFVIS_MAXCH];       /* GME voice index (for muting), -1 if none */
	char name  [NSFVIS_MAXCH][10];   /* short label */
	int  src   [NSFVIS_MAXCH];       /* internal decoder id */
	int  wavecount;                  /* HES / GBS: waveforms seen so far, numbered as "instruments" */
	uint32_t wavesig [255];
};

/* SPC: sound RAM of the emulator (samples), used to find the pitch of each sample */
void nsfvis_set_ram (const unsigned char *ram);

/* helpers for other decoders (plays98) */
void nsfvis_add_channel (struct nsfvis_layout_t *l, int voice, int src, const char *name);
int  nsfvis_note (double freq, int16_t *cents); /* note number (A-4 = 69), -1 if out of range; *cents = exact pitch */
int  nsfvis_wave_id (struct nsfvis_layout_t *l, const unsigned char *data, int len); /* "instrument" number of a waveform / patch */

struct gme_nsf_frame_t;

/* build the channel list from the chips used by the file (and the N163 channel count) */
void nsfvis_layout (struct nsfvis_layout_t *l, const struct gme_nsf_frame_t *f);

/* decode one frame into out[layout->count] */
void nsfvis_decode (struct nsfvis_layout_t *l, const struct gme_nsf_frame_t *f, struct nsfvis_ch_t *out);



/* One tracker cell: what happened on a channel during a row of frames */
struct nsfvis_cell_t
{
	uint8_t has_note;  /* note / retrigger started in this row */
	uint8_t noteoff;   /* channel went silent in this row (and no new note) */
	uint8_t has_vol;
	uint8_t has_ins;
	int16_t note;
	uint8_t kind;
	uint8_t aux;
	uint8_t vol;
	uint8_t ins;
	uint8_t slide;     /* pitch movement during the row: 0 none, 1 up, 2 down, 3 vibrato */
};

/* prev: state of the frame just before the row (NULL if unknown), frames: n consecutive frames */
void nsfvis_cell (const struct nsfvis_ch_t *prev, const struct nsfvis_ch_t *const *frames, int n, struct nsfvis_cell_t *cell);
#endif
