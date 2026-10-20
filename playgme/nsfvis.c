/* OpenCP Module Player
 * NSF register snapshot decoder for the tracker-like view
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <gme/gme.h>
#include "nsfvis.h"

enum
{
	SRC_P1, SRC_P2, SRC_TRI, SRC_NOISE, SRC_DPCM,
	SRC_VRC6_P1, SRC_VRC6_P2, SRC_VRC6_SAW,
	SRC_N163_0, /* .. SRC_N163_0 + 7, index = N163 channel number 0 (=$78) .. 7 (=$40) */
	SRC_5B_0 = SRC_N163_0 + 8, SRC_5B_1, SRC_5B_2,
	SRC_FDS,
	SRC_MMC5_P1, SRC_MMC5_P2, SRC_MMC5_PCM,
	SRC_VRC7_0, /* .. +5 */
	SRC_HES_0 = SRC_VRC7_0 + 6, /* .. +5 */
	SRC_GB_SQ1 = SRC_HES_0 + 6, SRC_GB_SQ2, SRC_GB_WAVE, SRC_GB_NOISE,
	SRC_FM_0, /* .. +5, YM2612 */
	SRC_PSG_0 = SRC_FM_0 + 6, /* .. +2 tone, +3 noise, SN76489 */
	SRC_OPLL_0 = SRC_PSG_0 + 4, /* .. +8, YM2413 */
	SRC_SPC_0 = SRC_OPLL_0 + 9 /* .. +7 */
};

/* waveform -> "instrument" number, in order of appearance (1, 2, 3...) */
static int wave_id (struct nsfvis_layout_t *l, const unsigned char *data, int len)
{
	uint32_t h = 2166136261u;
	int i;
	for (i = 0; i < len; i++)
	{
		h = (h ^ data[i]) * 16777619u;
	}
	for (i = 0; i < l->wavecount; i++)
	{
		if (l->wavesig[i] == h)
		{
			return i + 1;
		}
	}
	if (l->wavecount < (int)(sizeof (l->wavesig) / sizeof (l->wavesig[0])))
	{
		l->wavesig[l->wavecount++] = h;
		return l->wavecount;
	}
	return 0xFF;
}

static int last_cents; /* exact pitch of the last freq_to_note() call, in cents (A-4 = 6900) */

static int freq_to_note (double f)
{
	double n;
	if (f < 8.0 || f > 20000.0)
	{
		return -1;
	}
	n = 69.0 + 12.0 * log2 (f / 440.0);
	last_cents = (int)floor (n * 100.0 + 0.5);
	n = floor (n + 0.5);
	if (n < 12) return 12;
	if (n > 119) return 119;
	return (int)n;
}

static int n163_channels (const struct gme_nsf_frame_t *f)
{
	return ((f->n163[0x7F] >> 4) & 7) + 1;
}

static void add (struct nsfvis_layout_t *l, int voice, int src, const char *name)
{
	if (l->count >= NSFVIS_MAXCH)
	{
		return;
	}
	l->voice[l->count] = voice;
	l->src[l->count] = src;
	snprintf (l->name[l->count], sizeof (l->name[0]), "%s", name);
	l->count++;
}

void nsfvis_layout (struct nsfvis_layout_t *l, const struct gme_nsf_frame_t *f)
{
	int v = 5; /* GME voice order: 2A03, VRC6 (saw first), N163 (8), 5B (3), FDS, MMC5 (3), VRC7 (6) */
	int i;

	memset (l, 0, sizeof (*l));
	if (f->system == 1) /* HES: GME voices "Wave 1-4", "Multi 1-2" */
	{
		static const char *names[6] = {"Wave 1", "Wave 2", "Wave 3", "Wave 4", "Multi 1", "Multi 2"};
		for (i = 0; i < 6; i++)
		{
			add (l, i, SRC_HES_0 + i, names[i]);
		}
		return;
	}
	if (f->system == 3) /* VGM / GYM; GME voices: "FM 1-6", "PCM", "PSG" (all four PSG channels), or the 4 PSG channels without FM */
	{
		int fm = (f->vgm_chips & 6) != 0;
		if (f->vgm_chips & 4)
		{
			for (i = 0; i < 6; i++)
			{
				char name[10];
				snprintf (name, sizeof (name), "FM %d", i + 1);
				add (l, i, SRC_FM_0 + i, name);
			}
		} else if (f->vgm_chips & 2)
		{
			for (i = 0; i < 9; i++)
			{
				char name[10];
				snprintf (name, sizeof (name), "OPLL %d", i + 1);
				add (l, (i < 6) ? i : -1, SRC_OPLL_0 + i, name);
			}
		}
		if (f->vgm_chips & 1)
		{
			add (l, fm ? 7 : 0, SRC_PSG_0 + 0, "Square 1");
			add (l, fm ? 7 : 1, SRC_PSG_0 + 1, "Square 2");
			add (l, fm ? 7 : 2, SRC_PSG_0 + 2, "Square 3");
			add (l, fm ? 7 : 3, SRC_PSG_0 + 3, "Noise");
		}
		return;
	}
	if (f->system == 4) /* SPC: GME voices "DSP 1-8" */
	{
		for (i = 0; i < 8; i++)
		{
			char name[10];
			snprintf (name, sizeof (name), "Voice %d", i + 1);
			add (l, i, SRC_SPC_0 + i, name);
		}
		return;
	}
	if (f->system == 2) /* GBS */
	{
		add (l, 0, SRC_GB_SQ1,   "Square 1");
		add (l, 1, SRC_GB_SQ2,   "Square 2");
		add (l, 2, SRC_GB_WAVE,  "Wave");
		add (l, 3, SRC_GB_NOISE, "Noise");
		return;
	}
	add (l, 0, SRC_P1,    "Pulse 1");
	add (l, 1, SRC_P2,    "Pulse 2");
	add (l, 2, SRC_TRI,   "Triangle");
	add (l, 3, SRC_NOISE, "Noise");
	add (l, 4, SRC_DPCM,  "DPCM");

	if (f->chips & 0x01)
	{
		add (l, v + 1, SRC_VRC6_P1,  "VRC6 P1");
		add (l, v + 2, SRC_VRC6_P2,  "VRC6 P2");
		add (l, v + 0, SRC_VRC6_SAW, "VRC6 Saw");
		v += 3;
	}
	if (f->chips & 0x10)
	{
		int n = n163_channels (f);
		for (i = 0; i < n; i++)
		{
			char name[10];
			snprintf (name, sizeof (name), "N163 %d", i + 1);
			add (l, v + 7 - i, SRC_N163_0 + i, name); /* GME "Wave k" = registers $40 + 8*(k-1), N163 channel 1 is $78 */
		}
		v += 8;
	}
	if (f->chips & 0x20)
	{
		add (l, v + 0, SRC_5B_0, "5B A");
		add (l, v + 1, SRC_5B_1, "5B B");
		add (l, v + 2, SRC_5B_2, "5B C");
		v += 3;
	}
	if (f->chips & 0x04)
	{
		add (l, v, SRC_FDS, "FDS");
		v += 1;
	}
	if (f->chips & 0x08)
	{
		add (l, v + 0, SRC_MMC5_P1,  "MMC5 P1");
		add (l, v + 1, SRC_MMC5_P2,  "MMC5 P2");
		add (l, v + 2, SRC_MMC5_PCM, "MMC5 PCM");
		v += 3;
	}
	if (f->chips & 0x02)
	{
		for (i = 0; i < 6; i++)
		{
			char name[10];
			snprintf (name, sizeof (name), "VRC7 %d", i + 1);
			add (l, v + i, SRC_VRC7_0 + i, name);
		}
		v += 6;
	}
}

/* ---- SPC: pitch of the samples ---- */

static const unsigned char *spc_ram;

static struct
{
	int valid;
	unsigned start, loop;
	double f0;   /* fundamental at pitch $1000 (32 kHz playback), 0 if not pitched */
} spc_sample[256];

void nsfvis_set_ram (const unsigned char *ram)
{
	spc_ram = ram;
	memset (spc_sample, 0, sizeof (spc_sample));
}

/* decodes BRR from addr until the end block (at most max samples); returns the sample count,
 * *loopstart = index of the first sample of the loop block, -1 if the sample does not loop */
static int brr_decode (unsigned addr, unsigned loop, short *out, int max, int *loopstart)
{
	int n = 0, p1 = 0, p2 = 0;
	*loopstart = -1;
	while (n + 16 <= max)
	{
		int hdr = spc_ram[addr & 0xFFFF];
		int range = hdr >> 4, filter = (hdr >> 2) & 3, i;
		if ((addr & 0xFFFF) == (loop & 0xFFFF) && *loopstart < 0)
		{
			*loopstart = n;
		}
		for (i = 0; i < 16; i++)
		{
			int b = spc_ram[(addr + 1 + i / 2) & 0xFFFF];
			int s = (i & 1) ? (b & 0x0F) : (b >> 4);
			if (s >= 8) s -= 16;
			s = (range <= 12) ? ((s << range) >> 1) : ((s < 0) ? -2048 : 0);
			switch (filter)
			{
				case 1: s += p1 + ((-p1) >> 4); break;
				case 2: s += (p1 << 1) + ((-((p1 << 1) + p1)) >> 5) - p2 + (p2 >> 4); break;
				case 3: s += (p1 << 1) + ((-(p1 + (p1 << 2) + (p1 << 3))) >> 6) - p2 + (((p2 << 1) + p2) >> 4); break;
			}
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			s = (short)(s << 1) >> 1;
			out[n++] = s;
			p2 = p1; p1 = s;
		}
		if (hdr & 1)
		{
			if (!(hdr & 2))
			{
				*loopstart = -1;
			}
			break;
		}
		addr += 9;
	}
	return n;
}

/* fundamental period (in samples) by normalized autocorrelation, 0 if not periodic enough.
 * Takes the shortest lag whose correlation is close to the best one, to avoid octave errors. */
static double find_period (const short *x, int n)
{
	static double corr[2049];
	int lag, maxlag = n / 2;
	double bestc = 0.0;
	if (maxlag > 2048) maxlag = 2048;
	if (n < 32) return 0.0;
	for (lag = 4; lag <= maxlag; lag++)
	{
		double a = 0, e1 = 0, e2 = 0;
		int i, m = n - lag;
		for (i = 0; i < m; i++)
		{
			a += (double)x[i] * x[i + lag];
			e1 += (double)x[i] * x[i];
			e2 += (double)x[i + lag] * x[i + lag];
		}
		corr[lag] = (e1 > 0 && e2 > 0) ? a / sqrt (e1 * e2) : 0.0;
		if (corr[lag] > bestc) bestc = corr[lag];
	}
	if (bestc < 0.85)
	{
		return 0.0;
	}
	for (lag = 5; lag < maxlag; lag++)
	{
		if ((corr[lag] >= bestc - 0.04) && (corr[lag] >= corr[lag - 1]) && (corr[lag] >= corr[lag + 1]))
		{ /* refine with a parabola through the peak */
			double a = corr[lag - 1], b = corr[lag], c = corr[lag + 1], d = a - 2 * b + c;
			return lag + ((d != 0.0) ? 0.5 * (a - c) / d : 0.0);
		}
	}
	return 0.0;
}

static double spc_sample_f0 (const struct gme_nsf_frame_t *f, int srcn)
{
	static short buf[16384];
	unsigned dir, start, loop;
	int n, loopstart;
	double period;

	if (!spc_ram)
	{
		return 0.0;
	}
	dir = f->spc_dsp[0x5D] * 0x100;
	start = spc_ram[(dir + srcn * 4) & 0xFFFF] | (spc_ram[(dir + srcn * 4 + 1) & 0xFFFF] << 8);
	loop = spc_ram[(dir + srcn * 4 + 2) & 0xFFFF] | (spc_ram[(dir + srcn * 4 + 3) & 0xFFFF] << 8);
	if (spc_sample[srcn].valid && spc_sample[srcn].start == start && spc_sample[srcn].loop == loop)
	{
		return spc_sample[srcn].f0;
	}
	n = brr_decode (start, loop, buf, sizeof (buf) / sizeof (buf[0]), &loopstart);
	if (loopstart >= 0 && n - loopstart >= 16 && n - loopstart <= 512)
	{ /* short loop (16 ms or less): a few cycles of a waveform, repeat it to measure */
		int len = n - loopstart, i;
		static short tmp[4096];
		int m = 0;
		while (m + len <= (int)(sizeof (tmp) / sizeof (tmp[0])) && m < 2048)
		{
			for (i = 0; i < len; i++) tmp[m++] = buf[loopstart + i];
		}
		period = find_period (tmp, m);
		if (!period) period = len;
	} else if (loopstart >= 0 && n - loopstart > 512)
	{ /* long loop: must be periodic by itself (a looped noise is not) */
		period = find_period (buf + loopstart, n - loopstart);
	} else {
		period = 0.0; /* one-shot sample: drums and effects, shown as a sample number */
	}
	if (period && (period < 6.0))
	{
		period = 0.0; /* above 5 kHz: noise-like (cymbals...) */
	}
	spc_sample[srcn].valid = 1;
	spc_sample[srcn].start = start;
	spc_sample[srcn].loop = loop;
	spc_sample[srcn].f0 = period ? 32000.0 / period : 0.0;
	return spc_sample[srcn].f0;
}

static void decode_one (struct nsfvis_layout_t *l, int src, const struct gme_nsf_frame_t *f, struct nsfvis_ch_t *c)
{
	const double cpu = f->pal ? 1662607.125 : 1789772.72727;

	memset (c, 0, sizeof (*c));
	c->note = -1;
	c->ins = 0xFF;
	c->kind = NSFVIS_TONE;
	last_cents = 0;

	switch (src)
	{
		case SRC_P1:
		case SRC_P2:
		{
			int i = src - SRC_P1;
			const unsigned char *r = f->apu + 4 * i;
			int period = r[2] | ((r[3] & 7) << 8);
			c->vol = f->apu_vol[i];
			c->active = (f->apu_active >> i) & 1;
			c->trig = (f->apu_trig >> i) & 1;
			c->ins = r[0] >> 6;
			if (period >= 8)
			{
				c->note = freq_to_note (cpu / (16.0 * (period + 1)));
			}
			break;
		}
		case SRC_TRI:
		{
			int period = f->apu[0x0A] | ((f->apu[0x0B] & 7) << 8);
			c->vol = f->apu_vol[2];
			c->active = (f->apu_active >> 2) & 1;
			c->trig = (f->apu_trig >> 2) & 1;
			c->note = freq_to_note (cpu / (32.0 * (period + 1)));
			break;
		}
		case SRC_NOISE:
			c->kind = NSFVIS_NOISE;
			c->vol = f->apu_vol[3];
			c->active = (f->apu_active >> 3) & 1;
			c->trig = (f->apu_trig >> 3) & 1;
			c->aux = f->apu[0x0E] & 0x0F;
			c->ins = (f->apu[0x0E] >> 7) & 1;
			break;
		case SRC_DPCM:
			c->kind = NSFVIS_DPCM;
			c->vol = f->apu_vol[4] >> 3; /* dac 0..127 >> 3 = 0..15 */
			c->active = (f->apu_active >> 4) & 1;
			c->trig = (f->apu_trig >> 4) & 1;
			c->aux = f->apu[0x12];
			c->ins = f->apu[0x10] & 0x0F; /* rate */
			break;

		case SRC_VRC6_P1:
		case SRC_VRC6_P2:
		{
			const unsigned char *r = f->vrc6[src - SRC_VRC6_P1];
			int period = r[1] | ((r[2] & 0x0F) << 8);
			c->vol = r[0] & 0x0F;
			c->ins = (r[0] >> 4) & 7;
			c->active = (r[2] & 0x80) && c->vol;
			c->trig = (f->exp_trig >> (src - SRC_VRC6_P1)) & 1;
			c->note = freq_to_note (cpu / (16.0 * (period + 1)));
			break;
		}
		case SRC_VRC6_SAW:
		{
			const unsigned char *r = f->vrc6[2];
			int period = r[1] | ((r[2] & 0x0F) << 8);
			int rate = r[0] & 0x3F;
			c->vol = (rate >= 42) ? 15 : (rate * 15 + 20) / 42;
			c->active = (r[2] & 0x80) && rate;
			c->trig = (f->exp_trig >> 2) & 1;
			c->note = freq_to_note (cpu / (14.0 * (period + 1)));
			break;
		}

		case SRC_5B_0:
		case SRC_5B_1:
		case SRC_5B_2:
		{
			int ch = src - SRC_5B_0;
			int period = f->fme7[2 * ch] | ((f->fme7[2 * ch + 1] & 0x0F) << 8);
			int tone = !(f->fme7[7] & (1 << ch));
			int noise = !(f->fme7[7] & (8 << ch));
			c->vol = f->fme7[8 + ch] & 0x0F;
			if (f->fme7[8 + ch] & 0x10) c->vol = 15; /* envelope mode */
			c->active = c->vol && (tone || noise);
			c->trig = (f->exp_trig >> (20 + ch)) & 1;
			if (tone)
			{
				c->note = freq_to_note (cpu / (32.0 * (period ? period : 1)));
			} else {
				c->kind = NSFVIS_NOISE;
				c->aux = f->fme7[6] & 0x0F;
			}
			break;
		}

		case SRC_FDS:
		{
			int freq = f->fds[2] | ((f->fds[3] & 0x0F) << 8);
			int vol = f->fds[0] & 0x3F;
			if (vol > 32) vol = 32;
			c->vol = (vol * 15 + 16) / 32;
			c->active = !(f->fds[3] & 0x80) && freq && vol;
			c->note = freq_to_note (cpu * freq / 4194304.0);
			break;
		}

		case SRC_MMC5_P1:
		case SRC_MMC5_P2:
		{
			int i = src - SRC_MMC5_P1;
			const unsigned char *r = f->mmc5 + 4 * i;
			int period = r[2] | ((r[3] & 7) << 8);
			c->vol = r[0] & 0x0F;
			c->ins = r[0] >> 6;
			c->active = ((f->mmc5[0x15] >> i) & 1) && c->vol && period >= 8;
			c->trig = (f->exp_trig >> (3 + i)) & 1;
			if (period >= 8)
			{
				c->note = freq_to_note (cpu / (16.0 * (period + 1)));
			}
			break;
		}
		case SRC_MMC5_PCM:
			c->kind = NSFVIS_PCM;
			c->vol = f->mmc5[0x11] >> 4;
			c->active = f->mmc5[0x11] != 0;
			break;

		default:
			if ((src >= SRC_N163_0) && (src < SRC_N163_0 + 8))
			{
				int n = n163_channels (f);
				int k = src - SRC_N163_0;
				int b = 0x78 - 8 * k;
				long freq = f->n163[b] | (f->n163[b + 2] << 8) | ((long)(f->n163[b + 4] & 3) << 16);
				int len = 256 - (f->n163[b + 4] & 0xFC);
				c->vol = f->n163[b + 7] & 0x0F;
				c->active = (k < n) && c->vol && freq;
				c->ins = f->n163[b + 6] >> 2; /* wave position / 4 */
				if (freq)
				{
					c->note = freq_to_note ((double)freq * cpu / (15.0 * 65536.0 * n * len));
				}
			} else if ((src >= SRC_VRC7_0) && (src < SRC_VRC7_0 + 6))
			{
				int ch = src - SRC_VRC7_0;
				int fnum = f->vrc7[0x10 + ch] | ((f->vrc7[0x20 + ch] & 1) << 8);
				int block = (f->vrc7[0x20 + ch] >> 1) & 7;
				c->active = (f->vrc7[0x20 + ch] & 0x10) != 0;
				c->trig = (f->exp_trig >> (6 + ch)) & 1;
				c->ins = f->vrc7[0x30 + ch] >> 4;
				c->vol = 15 - (f->vrc7[0x30 + ch] & 0x0F);
				if (fnum)
				{
					c->note = freq_to_note (fnum * 49715.9 / (double)(1 << (19 - block)));
				}
			}
			break;

		case SRC_HES_0 + 0: case SRC_HES_0 + 1: case SRC_HES_0 + 2:
		case SRC_HES_0 + 3: case SRC_HES_0 + 4: case SRC_HES_0 + 5:
		{
			int i = src - SRC_HES_0;
			int ctl = f->hes_control[i];
			int bal = f->hes_balance[i];
			int heard = (ctl & 0x80) && (ctl & 0x1F) && bal && f->hes_main_balance;
			c->vol = ctl & 0x1F; /* 0-31 */
			c->trig = (f->hes_trig >> i) & 1;
			if (ctl & 0x40)
			{ /* DDA: samples written directly by the CPU */
				c->kind = NSFVIS_PCM;
				c->active = heard;
			} else if ((i >= 4) && (f->hes_noise[i] & 0x80))
			{
				c->kind = NSFVIS_NOISE;
				c->aux = 15 - ((f->hes_noise[i] & 0x1F) >> 1); /* displayed as 15 - aux: higher = brighter */
				c->active = heard;
			} else {
				int period = f->hes_period[i] ? f->hes_period[i] : 0x1000;
				c->ins = wave_id (l, f->hes_wave[i], 32);
				c->note = freq_to_note (3579545.0 / (32.0 * period));
				c->active = heard && (c->note >= 0);
			}
			break;
		}

		case SRC_GB_SQ1:
		case SRC_GB_SQ2:
		{
			int i = src - SRC_GB_SQ1;
			const unsigned char *r = f->gb_regs + 5 * i;
			int x = r[3] | ((r[4] & 7) << 8);
			int on = (f->gb_regs[0x16] & 0x80) && (f->gb_regs[0x15] & (0x11 << i));
			c->ins = r[1] >> 6;
			c->vol = f->gb_vol[i] & 15;
			c->trig = (f->gb_trig >> i) & 1;
			c->note = freq_to_note (131072.0 / (2048 - x));
			c->active = on && ((f->gb_enabled >> i) & 1) && c->vol && (c->note >= 0);
			break;
		}

		case SRC_GB_WAVE:
		{
			const unsigned char *r = f->gb_regs + 10;
			int x = r[3] | ((r[4] & 7) << 8);
			int on = (f->gb_regs[0x16] & 0x80) && (f->gb_regs[0x15] & 0x44) && (r[0] & 0x80);
			static const unsigned char level[4] = {0, 15, 7, 3};
			c->vol = level[(r[2] >> 5) & 3];
			c->ins = wave_id (l, f->gb_regs + 0x20, 16);
			c->trig = (f->gb_trig >> 2) & 1;
			c->note = freq_to_note (65536.0 / (2048 - x));
			c->active = on && ((f->gb_enabled >> 2) & 1) && c->vol && (c->note >= 0);
			break;
		}

		case SRC_GB_NOISE:
		{
			const unsigned char *r = f->gb_regs + 15;
			int on = (f->gb_regs[0x16] & 0x80) && (f->gb_regs[0x15] & 0x88);
			c->kind = NSFVIS_NOISE;
			c->aux = r[3] >> 4;          /* clock shift, displayed as 15 - shift: higher = brighter */
			c->ins = (r[3] >> 3) & 1;    /* 1 = short (7 bit) mode */
			c->vol = f->gb_vol[3] & 15;
			c->trig = (f->gb_trig >> 3) & 1;
			c->active = on && ((f->gb_enabled >> 3) & 1) && c->vol;
			break;
		}

		case SRC_FM_0 + 0: case SRC_FM_0 + 1: case SRC_FM_0 + 2:
		case SRC_FM_0 + 3: case SRC_FM_0 + 4: case SRC_FM_0 + 5:
		{
			static const unsigned char carriers[8] = {8, 8, 8, 8, 10, 14, 14, 15}; /* bit0 op1, bit1 op2, bit2 op3, bit3 op4 */
			static const unsigned char slot[4] = {0x0, 0x8, 0x4, 0xC};          /* register offset of op1, op2, op3, op4 */
			int ch = src - SRC_FM_0, k = ch % 3, op, tl = 127, alg, fnum, block;
			const unsigned char *r = f->ym2612[ch / 3];
			unsigned char patch[29];
			int pn = 0, reg;
			fnum = ((r[0xA4 + k] & 7) << 8) | r[0xA0 + k];
			block = (r[0xA4 + k] >> 3) & 7;
			alg = r[0xB0 + k] & 7;
			for (op = 0; op < 4; op++)
			{
				if ((carriers[alg] >> op) & 1)
				{
					int t = r[0x40 + slot[op] + k] & 0x7F;
					if (t < tl) tl = t;
				}
			}
			for (reg = 0x30; reg <= 0x90; reg += 0x10)
			{
				if (reg == 0x40) continue; /* total level changes with the volume */
				for (op = 0; op < 4; op++) patch[pn++] = r[reg + slot[op] + k];
			}
			patch[pn++] = r[0xB0 + k];
			c->ins = wave_id (l, patch, pn);
			c->vol = (tl >= 120) ? 0 : 15 - tl / 8; /* 6 dB per step */
			c->trig = (f->ym2612_trig >> ch) & 1;
			if (fnum)
			{
				c->note = freq_to_note (fnum * pow (2.0, block - 1) * (double)f->ym2612_clock / 144.0 / 1048576.0);
			}
			c->active = f->ym2612_key[ch] && (r[0xB4 + k] & 0xC0) && (c->note >= 0);
			if (ch == 5 && f->ym2612_dac)
			{
				c->kind = NSFVIS_PCM;
				c->note = -1;
				c->ins = 0xFF;
				c->vol = 15;
				c->active = f->ym2612_dac_writes > 0;
			}
			break;
		}

		case SRC_PSG_0 + 0: case SRC_PSG_0 + 1: case SRC_PSG_0 + 2:
		{
			int i = src - SRC_PSG_0;
			int p = f->psg_period[i] ? f->psg_period[i] : 1024;
			c->vol = 15 - (f->psg_vol[i] & 15);
			c->trig = (f->psg_trig >> i) & 1;
			c->note = freq_to_note ((double)f->psg_clock / (32.0 * p));
			c->active = c->vol && (c->note >= 0);
			break;
		}

		case SRC_PSG_0 + 3:
			c->kind = NSFVIS_NOISE;
			c->vol = 15 - (f->psg_vol[3] & 15);
			c->aux = (f->psg_noise & 3) * 4;          /* displayed as 15 - aux: higher = brighter */
			c->ins = (f->psg_noise >> 2) & 1;         /* 1 = white noise */
			c->trig = (f->psg_trig >> 3) & 1;
			c->active = c->vol != 0;
			break;

		case SRC_OPLL_0 + 0: case SRC_OPLL_0 + 1: case SRC_OPLL_0 + 2:
		case SRC_OPLL_0 + 3: case SRC_OPLL_0 + 4: case SRC_OPLL_0 + 5:
		case SRC_OPLL_0 + 6: case SRC_OPLL_0 + 7: case SRC_OPLL_0 + 8:
		{
			int ch = src - SRC_OPLL_0;
			int fnum = f->ym2413[0x10 + ch] | ((f->ym2413[0x20 + ch] & 1) << 8);
			int block = (f->ym2413[0x20 + ch] >> 1) & 7;
			c->ins = f->ym2413[0x30 + ch] >> 4;
			c->vol = 15 - (f->ym2413[0x30 + ch] & 15);
			c->trig = (f->ym2413_trig >> ch) & 1;
			if (fnum)
			{
				c->note = freq_to_note (fnum * (double)f->ym2413_clock / 72.0 / (double)(1 << (19 - block)));
			}
			c->active = (f->ym2413[0x20 + ch] & 0x10) && (c->note >= 0);
			if (ch >= 6 && (f->ym2413[0x0E] & 0x20))
			{ /* rhythm mode: channels 7-9 play the drums */
				c->kind = NSFVIS_PCM;
				c->note = -1;
				c->ins = 0xFF;
				c->active = (f->ym2413[0x0E] & 0x1F) != 0;
			}
			break;
		}

		case SRC_SPC_0 + 0: case SRC_SPC_0 + 1: case SRC_SPC_0 + 2: case SRC_SPC_0 + 3:
		case SRC_SPC_0 + 4: case SRC_SPC_0 + 5: case SRC_SPC_0 + 6: case SRC_SPC_0 + 7:
		{
			int i = src - SRC_SPC_0;
			const unsigned char *r = f->spc_dsp + 16 * i;
			int pitch = (r[2] | (r[3] << 8)) & 0x3FFF;
			int envx = r[8] & 0x7F;
			int srcn = r[4];
			double f0;
			c->vol = envx >> 3;
			c->trig = (f->spc_kon >> i) & 1;
			c->active = envx && (r[0] || r[1]) && !(f->spc_dsp[0x6C] & 0x40);
			if ((f->spc_dsp[0x3D] >> i) & 1)
			{
				c->kind = NSFVIS_NOISE;
				c->aux = 15 - ((f->spc_dsp[0x6C] & 0x1F) >> 1);
			} else if ((f0 = spc_sample_f0 (f, srcn)) > 0.0)
			{
				c->ins = srcn;
				c->note = freq_to_note (f0 * pitch / 4096.0);
				c->active = c->active && (c->note >= 0);
			} else {
				c->kind = NSFVIS_DPCM; /* unpitched sample (drums...): shown as "Sxx" */
				c->aux = srcn;
			}
			break;
		}
	}
	if (!c->active)
	{
		c->vol = (c->kind == NSFVIS_DPCM) ? c->vol : 0;
	}
}

void nsfvis_decode (struct nsfvis_layout_t *l, const struct gme_nsf_frame_t *f, struct nsfvis_ch_t *out)
{
	int i;
	for (i = 0; i < l->count; i++)
	{
		decode_one (l, l->src[i], f, out + i);
		out[i].pitch = ((out[i].kind == NSFVIS_TONE) && (out[i].note >= 0)) ? last_cents : 0;
	}
}

void nsfvis_add_channel (struct nsfvis_layout_t *l, int voice, int src, const char *name)
{
	add (l, voice, src, name);
}

int nsfvis_note (double freq, int16_t *cents)
{
	int n;
	last_cents = 0;
	n = freq_to_note (freq);
	if (cents) *cents = (n >= 0) ? last_cents : 0;
	return n;
}

int nsfvis_wave_id (struct nsfvis_layout_t *l, const unsigned char *data, int len)
{
	return wave_id (l, data, len);
}

void nsfvis_cell (const struct nsfvis_ch_t *prev, const struct nsfvis_ch_t *const *frames, int n, struct nsfvis_cell_t *cell)
{
	static const struct nsfvis_ch_t silent = { -1, 0, 0xFF, 0, 0, 0, 0 };
	const struct nsfvis_ch_t *p = prev ? prev : &silent;
	int i;
	int havebase = 0, base = 0, lo = 0, hi = 0, last = 0;

	memset (cell, 0, sizeof (*cell));
	if (p->active && (p->kind == NSFVIS_TONE) && (p->note >= 0))
	{
		havebase = 1;
		base = lo = hi = last = p->pitch;
	}
	for (i = 0; i < n; i++)
	{
		const struct nsfvis_ch_t *c = frames[i];
		int newnote = 0;

		if (c->active)
		{
			switch (c->kind)
			{
				/* a pitch jump of more than 70 cents within one frame is a new note; slower movements (vibrato,
				 * portamento) are shown with the slide indicator instead of repeating notes */
				case NSFVIS_TONE:  newnote = c->trig || !p->active || (p->note < 0) || (c->pitch - p->pitch > 70) || (p->pitch - c->pitch > 70); break;
				case NSFVIS_NOISE: newnote = c->trig || !p->active || (c->aux != p->aux); break;
				case NSFVIS_DPCM:  newnote = c->trig; break;
				case NSFVIS_DRUMS: newnote = c->trig; break;
				case NSFVIS_PCM:   newnote = !p->active; break;
			}
		} else if ((c->kind == NSFVIS_DPCM) || (c->kind == NSFVIS_DRUMS))
		{
			newnote = c->trig;
		}

		if (c->active && (c->kind == NSFVIS_TONE) && (c->note >= 0))
		{
			if (!havebase || (newnote && c->trig))
			{
				havebase = 1;
				base = lo = hi = c->pitch;
			}
			if (c->pitch < lo) lo = c->pitch;
			if (c->pitch > hi) hi = c->pitch;
			last = c->pitch;
		}

		if (newnote && !cell->has_note)
		{
			cell->has_note = 1;
			cell->noteoff = 0;
			cell->note = c->note;
			cell->kind = c->kind;
			cell->aux = c->aux;
			if (!cell->has_vol && c->kind != NSFVIS_DPCM)
			{
				cell->has_vol = 1;
				cell->vol = c->vol;
			}
			if (!cell->has_ins && c->ins != 0xFF)
			{
				cell->has_ins = 1;
				cell->ins = c->ins;
			}
		} else if (!c->active && p->active && !cell->has_note && !cell->noteoff && c->kind != NSFVIS_DPCM)
		{
			cell->noteoff = 1;
		}

		if (c->active && !cell->has_vol && (c->vol != p->vol) && c->kind != NSFVIS_DPCM)
		{
			cell->has_vol = 1;
			cell->vol = c->vol;
		}
		if (c->active && !cell->has_ins && (c->ins != p->ins) && (c->ins != 0xFF))
		{
			cell->has_ins = 1;
			cell->ins = c->ins;
		}
		p = c;
	}

	if (havebase)
	{
		const int threshold = 30; /* cents */
		if (((hi - base) > threshold) && ((base - lo) > threshold))
		{
			cell->slide = 3;
		} else if ((last - base) > threshold)
		{
			cell->slide = 1;
		} else if ((base - last) > threshold)
		{
			cell->slide = 2;
		}
	}
}
