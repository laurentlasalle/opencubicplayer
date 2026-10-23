/* OpenCP Module Player
 * S98 and VGM tracker view: reads the S98 or VGM command stream in parallel with
 * libvgm, keeps a copy of the sound chip registers and decodes them into tracker-like
 * channel information (see playgme/nsfvis.h), one frame every 1/60 s.
 *
 * Supported chips: YM2203 (OPN), YM2608 (OPNA), YM2612 (OPN2), YM2151 (OPM),
 * YM2149 / AY-3-8910 (PSG), SN76489, YM2413 (OPLL) and Konami SCC (K051649 /
 * K052539). Other chips (OPL, PCM...) are ignored.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../playgme/nsfvis.h"
#include "s98vis.h"

enum
{
	DEV_NONE = 0, DEV_YM2149 = 1, DEV_YM2203 = 2, DEV_YM2612 = 3, DEV_YM2608 = 4,
	DEV_YM2151 = 5, DEV_YM2413 = 6, DEV_AY8910 = 15, DEV_SN76489 = 16,
	DEV_SCC = 100 /* not an S98 device type: VGM only */
};

#define MAXDEV 8

/* channel kinds of the layout (src) */
enum { CH_FM, CH_SSG, CH_RHYTHM, CH_ADPCM, CH_OPM, CH_PSG_TONE, CH_PSG_NOISE, CH_OPLL, CH_SCC };

struct s98dev_t
{
	int type;
	uint32_t clock;
	uint8_t regs[2][256];
	uint8_t key[8];          /* FM / OPM operators keyed on, per channel */
	uint32_t trig;           /* channels (re)started during the frame */
	uint8_t rhythm_hits;     /* YM2608 rhythm keyed on during the frame */
	uint8_t adpcm_on;
	int psg_ch, psg_type;    /* SN76489 latch */
	uint16_t psg_period[3];
	uint8_t psg_noise, psg_vol[4];
	int plus;                /* SCC: K052539 (SCC+), 5 separate waveforms */
	int8_t scc_wave[5][32];
	uint16_t scc_period[5];
	uint8_t scc_vol[5], scc_key;
	uint32_t muteid;         /* device id given to libvgm for muting */
};

struct s98vis_t
{
	uint8_t *data;
	uint32_t len;
	double tick;             /* seconds per sync */
	uint32_t start, loop;
	int vgm;                 /* VGM file instead of S98 */
	int vgmdev[0x20][2];     /* VGM: device index of (our chip key, instance), -1 if none */
	int ndev;
	struct s98dev_t dev[MAXDEV];

	uint32_t pos;            /* parser */
	int ended;
	int looped;              /* went through the loop point at least once */
	double time;             /* song time of the parser, seconds */
	double next;             /* song time of the next frame */
	long frame;
	struct nsfvis_layout_t layout;
	uint32_t scan_used[2];
	int chdev[NSFVIS_MAXCH], chkind[NSFVIS_MAXCH], chidx[NSFVIS_MAXCH];
};

static uint32_t le32 (const uint8_t *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void addch (struct s98vis_t *v, int dev, int kind, int idx, int linked, int bit, const char *name)
{
	int n = v->layout.count;
	if (n >= NSFVIS_MAXCH) return;
	/* voice: device << 20 | linked << 19 | mute mask bit (see s98lib_mute) */
	nsfvis_add_channel (&v->layout, (dev << 20) | (linked << 19) | (1 << bit), n, name);
	v->chdev[n] = dev;
	v->chkind[n] = kind;
	v->chidx[n] = idx;
}

static void build_layout (struct s98vis_t *v)
{
	int d, i;
	char name[10];
	memset (&v->layout, 0, sizeof (v->layout));
	for (d = 0; d < v->ndev; d++)
	{
		switch (v->dev[d].type)
		{
			case DEV_YM2203:
			case DEV_YM2608:
			case DEV_YM2612:
			{
				int nfm = (v->dev[d].type == DEV_YM2203) ? 3 : 6;
				for (i = 0; i < nfm; i++)
				{
					snprintf (name, sizeof (name), "FM %d", i + 1);
					addch (v, d, CH_FM, i, 0, i, name);
				}
				if (v->dev[d].type != DEV_YM2612)
				{
					for (i = 0; i < 3; i++)
					{
						snprintf (name, sizeof (name), "SSG %c", 'A' + i);
						addch (v, d, CH_SSG, i, 1, i, name);
					}
				}
				if (v->dev[d].type == DEV_YM2608)
				{
					addch (v, d, CH_RHYTHM, 0, 0, 6, "Rhythm");
					v->layout.voice[v->layout.count - 1] |= 0x3F << 6; /* the 6 drums */
					addch (v, d, CH_ADPCM, 0, 0, 12, "ADPCM");
				}
				break;
			}
			case DEV_YM2151:
				for (i = 0; i < 8; i++)
				{
					snprintf (name, sizeof (name), "FM %d", i + 1);
					addch (v, d, CH_OPM, i, 0, i, name);
				}
				break;
			case DEV_YM2149:
			case DEV_AY8910:
				for (i = 0; i < 3; i++)
				{
					snprintf (name, sizeof (name), "SSG %c", 'A' + i);
					addch (v, d, CH_SSG, i, 0, i, name);
				}
				break;
			case DEV_SN76489:
				for (i = 0; i < 3; i++)
				{
					snprintf (name, sizeof (name), "Square %d", i + 1);
					addch (v, d, CH_PSG_TONE, i, 0, i, name);
				}
				addch (v, d, CH_PSG_NOISE, 3, 0, 3, "Noise");
				break;
			case DEV_YM2413:
				for (i = 0; i < 9; i++)
				{
					snprintf (name, sizeof (name), "OPLL %d", i + 1);
					addch (v, d, CH_OPLL, i, 0, i, name);
				}
				break;
			case DEV_SCC:
				for (i = 0; i < 5; i++)
				{
					snprintf (name, sizeof (name), "SCC %d", i + 1);
					addch (v, d, CH_SCC, i, 0, i, name);
				}
				break;
		}
	}
}

static void decode (struct s98vis_t *v, struct nsfvis_ch_t *out);

static void scan_emit (void *arg, double t, const struct nsfvis_ch_t *ch)
{
	struct s98vis_t *v = arg;
	uint32_t *used = (uint32_t *)v->scan_used;
	int i;
	for (i = 0; i < v->layout.count; i++)
	{
		if (ch[i].active && (ch[i].kind == NSFVIS_DRUMS ? ch[i].trig : 1)) used[i / 32] |= 1u << (i % 32);
	}
}

/* reads the whole song once and keeps only the channels that are used */
static void hide_unused (struct s98vis_t *v)
{
	struct nsfvis_layout_t old = v->layout;
	int olddev[NSFVIS_MAXCH], oldkind[NSFVIS_MAXCH], oldidx[NSFVIS_MAXCH];
	double t = 0.0;
	int i, n = 0;
	memset (v->scan_used, 0, sizeof (v->scan_used));
	while (!v->ended && !v->looped && t < 3600.0)
	{
		t += 10.0;
		s98vis_run_until (v, t, scan_emit, v);
	}
	if (!v->scan_used[0] && !v->scan_used[1])
	{
		return; /* nothing found: keep everything */
	}
	memcpy (olddev, v->chdev, sizeof (olddev));
	memcpy (oldkind, v->chkind, sizeof (oldkind));
	memcpy (oldidx, v->chidx, sizeof (oldidx));
	for (i = 0; i < old.count; i++)
	{
		if ((v->scan_used[i / 32] >> (i % 32)) & 1)
		{
			v->layout.voice[n] = old.voice[i];
			v->layout.src[n] = n;
			memcpy (v->layout.name[n], old.name[i], sizeof (old.name[0]));
			v->chdev[n] = olddev[i];
			v->chkind[n] = oldkind[i];
			v->chidx[n] = oldidx[i];
			n++;
		}
	}
	v->layout.count = n;
	v->layout.wavecount = 0;
}

/* VGM header: the chips we can decode */
static void vgm_devices (struct s98vis_t *v, const uint8_t *data, uint32_t len)
{
	static const struct { uint8_t ofs; uint16_t minver; int type; uint8_t libvgm; } chips[] =
	{
		{0x0C, 0x100, DEV_SN76489, 0x00}, {0x10, 0x100, DEV_YM2413, 0x01}, {0x2C, 0x110, DEV_YM2612, 0x02},
		{0x30, 0x110, DEV_YM2151, 0x03},  {0x44, 0x151, DEV_YM2203, 0x06}, {0x48, 0x151, DEV_YM2608, 0x07},
		{0x74, 0x151, DEV_AY8910, 0x12},  {0x9C, 0x161, DEV_SCC, 0x19}
	};
	uint32_t version = le32 (data + 8), hdr = 0x40;
	unsigned int c;
	int inst;
	if ((version >= 0x150) && le32 (data + 0x34))
	{
		hdr = 0x34 + le32 (data + 0x34);
	}
	if (hdr > len) hdr = len;
	memset (v->vgmdev, 0xFF, sizeof (v->vgmdev));
	for (c = 0; c < sizeof (chips) / sizeof (chips[0]); c++)
	{
		uint32_t clock;
		if ((version < chips[c].minver) || (chips[c].ofs + 4u > hdr)) continue;
		clock = le32 (data + chips[c].ofs);
		if (!(clock & 0x3FFFFFFF)) continue;
		for (inst = 0; inst < ((clock & 0x40000000) ? 2 : 1); inst++)
		{
			struct s98dev_t *dv;
			if (v->ndev >= MAXDEV) return;
			dv = &v->dev[v->ndev];
			dv->type = chips[c].type;
			dv->clock = clock & 0x3FFFFFFF;
			dv->muteid = 0x80000000u | chips[c].libvgm | ((uint32_t)inst << 16);
			if (dv->type == DEV_SCC)
			{
				dv->plus = (clock & 0x80000000u) != 0;
			}
			if ((dv->type == DEV_AY8910) && (hdr >= 0x7A))
			{
				if ((data[0x78] & 0xF0) == 0x10) /* YM2149 family */
				{
					dv->type = DEV_YM2149;
					if (data[0x79] & 0x10) dv->clock /= 2; /* pin 26: clock divided by 2 */
				}
			}
			v->vgmdev[chips[c].libvgm][inst] = v->ndev;
			v->ndev++;
		}
	}
}

struct s98vis_t *s98vis_open (const uint8_t *data, uint32_t len)
{
	struct s98vis_t *v;
	uint32_t num, den;
	int i;

	if ((len >= 0x40) && !memcmp (data, "Vgm ", 4))
	{
		uint32_t version;
		v = calloc (1, sizeof (*v));
		if (!v) return 0;
		v->data = malloc (len);
		if (!v->data)
		{
			free (v);
			return 0;
		}
		memcpy (v->data, data, len);
		v->len = len;
		v->vgm = 1;
		version = le32 (data + 8);
		v->start = ((version >= 0x150) && le32 (data + 0x34)) ? 0x34 + le32 (data + 0x34) : 0x40;
		v->loop = le32 (data + 0x1C) ? 0x1C + le32 (data + 0x1C) : 0;
		vgm_devices (v, data, len);
		if (!v->ndev)
		{
			s98vis_close (v);
			return 0;
		}
		build_layout (v);
		s98vis_reset (v);
		hide_unused (v);
		s98vis_reset (v);
		return v;
	}
	if (len < 0x20 || memcmp (data, "S98", 3))
	{
		return 0;
	}
	v = calloc (1, sizeof (*v));
	if (!v) return 0;
	v->data = malloc (len);
	if (!v->data)
	{
		free (v);
		return 0;
	}
	memcpy (v->data, data, len);
	v->len = len;
	num = le32 (data + 4);
	den = le32 (data + 8);
	if (!num) num = 10;
	if (!den) den = 1000;
	v->tick = (double)num / den;
	v->start = le32 (data + 0x14);
	v->loop = le32 (data + 0x18);
	if (data[3] == '3')
	{
		uint32_t n = le32 (data + 0x1C);
		for (i = 0; i < (int)n && i < MAXDEV && 0x20 + 16 * (i + 1) <= (int)len; i++)
		{
			v->dev[i].type = le32 (data + 0x20 + 16 * i);
			v->dev[i].clock = le32 (data + 0x24 + 16 * i);
			v->dev[i].muteid = i;
		}
		v->ndev = i;
	}
	if (!v->ndev)
	{ /* S98 v1/v2, or no device list: one YM2608 */
		v->ndev = 1;
		v->dev[0].type = DEV_YM2608;
		v->dev[0].clock = 7987200;
		v->dev[0].muteid = 0;
	}
	build_layout (v);
	s98vis_reset (v);
	hide_unused (v);
	s98vis_reset (v);
	return v;
}

void s98vis_close (struct s98vis_t *v)
{
	if (v)
	{
		free (v->data);
		free (v);
	}
}

const struct nsfvis_layout_t *s98vis_layout (struct s98vis_t *v)
{
	return &v->layout;
}

uint32_t s98vis_muteid (struct s98vis_t *v, int dev)
{
	return ((dev >= 0) && (dev < v->ndev)) ? v->dev[dev].muteid : (uint32_t)dev;
}

int s98vis_devcount (struct s98vis_t *v)
{
	return v->ndev;
}

void s98vis_reset (struct s98vis_t *v)
{
	int d;
	for (d = 0; d < MAXDEV; d++)
	{
		struct s98dev_t *dv = &v->dev[d];
		int type = dv->type, plus = dv->plus;
		uint32_t clock = dv->clock, muteid = dv->muteid;
		memset (dv, 0, sizeof (*dv));
		dv->type = type;
		dv->clock = clock;
		dv->plus = plus;
		dv->muteid = muteid;
		dv->regs[0][0xB4] = dv->regs[0][0xB5] = dv->regs[0][0xB6] = 0xC0; /* OPN outputs on both sides */
		dv->regs[1][0xB4] = dv->regs[1][0xB5] = dv->regs[1][0xB6] = 0xC0;
		dv->regs[0][0x07] = 0xFF;                                         /* SSG: all off */
		dv->psg_vol[0] = dv->psg_vol[1] = dv->psg_vol[2] = dv->psg_vol[3] = 15;
		if (type == DEV_YM2151)
		{
			int c;
			for (c = 0; c < 8; c++) dv->regs[0][0x20 + c] = 0xC0;          /* OPM: both sides */
		}
	}
	v->pos = v->start;
	v->ended = (v->start >= v->len);
	v->looped = 0;
	v->time = 0.0;
	v->next = 0.0;
	v->frame = 0;
}

static void psg_vol (struct s98dev_t *dv, int ch, int val)
{
	int old = dv->psg_vol[ch];
	if (val < 15 && (old == 15 || old - val >= 4)) dv->trig |= 1u << ch;
	dv->psg_vol[ch] = val;
}

static void write_reg (struct s98dev_t *dv, int port, int addr, int data)
{
	uint8_t old = dv->regs[port][addr];
	dv->regs[port][addr] = data;
	switch (dv->type)
	{
		case DEV_YM2203:
		case DEV_YM2608:
		case DEV_YM2612:
			if (port == 0 && addr == 0x28)
			{
				int c = data & 7;
				if ((c & 3) != 3)
				{
					int key = (data >> 4) & 0x0F;
					c = (c & 3) + ((c & 4) ? 3 : 0);
					if (key && !dv->key[c]) dv->trig |= 1u << c;
					dv->key[c] = key;
				}
			} else if (port == 0 && addr >= 0x08 && addr <= 0x0A && dv->type != DEV_YM2612)
			{ /* SSG volume raised */
				int ch = addr - 0x08, nv = data & 0x1F, ov = old & 0x1F;
				if (nv && (nv & 0x10 || ov == 0 || nv >= ov + 4)) dv->trig |= 1u << (8 + ch);
			} else if (dv->type == DEV_YM2608 && port == 0 && addr == 0x10 && !(data & 0x80))
			{
				dv->rhythm_hits |= data & 0x3F;
			} else if (dv->type == DEV_YM2608 && port == 1 && addr == 0x00)
			{
				if ((data & 0x80) && !(old & 0x80)) dv->trig |= 1u << 16;
				dv->adpcm_on = (data & 0x80) != 0;
			}
			break;
		case DEV_YM2149:
		case DEV_AY8910:
			if (addr >= 0x08 && addr <= 0x0A)
			{
				int ch = addr - 0x08, nv = data & 0x1F, ov = old & 0x1F;
				if (nv && (nv & 0x10 || ov == 0 || nv >= ov + 4)) dv->trig |= 1u << (8 + ch);
			}
			break;
		case DEV_YM2151:
			if (addr == 0x08)
			{
				int c = data & 7, key = (data >> 3) & 0x0F;
				if (key && !dv->key[c]) dv->trig |= 1u << c;
				dv->key[c] = key;
			}
			break;
		case DEV_YM2413:
			if (addr >= 0x20 && addr <= 0x28 && (data & 0x10) && !(old & 0x10)) dv->trig |= 1u << (addr - 0x20);
			break;
	}
}

static void write_psg (struct s98dev_t *dv, int data)
{
	if (data & 0x80)
	{
		dv->psg_ch = (data >> 5) & 3;
		dv->psg_type = (data >> 4) & 1;
		if (dv->psg_type) psg_vol (dv, dv->psg_ch, data & 0x0F);
		else if (dv->psg_ch < 3) dv->psg_period[dv->psg_ch] = (dv->psg_period[dv->psg_ch] & 0x3F0) | (data & 0x0F);
		else dv->psg_noise = data & 7;
	} else if (dv->psg_type) psg_vol (dv, dv->psg_ch, data & 0x0F);
	else if (dv->psg_ch < 3) dv->psg_period[dv->psg_ch] = (dv->psg_period[dv->psg_ch] & 0x0F) | ((data & 0x3F) << 4);
	else dv->psg_noise = data & 7;
}

static void write_scc (struct s98dev_t *dv, int port, int addr, int data)
{
	switch (port)
	{
		case 0: /* waveform */
		case 4:
			if (dv->plus)
			{
				if (addr < 0xA0) dv->scc_wave[addr >> 5][addr & 31] = (int8_t)data;
			} else if (addr < 0x80)
			{
				dv->scc_wave[addr >> 5][addr & 31] = (int8_t)data;
				if (addr >= 0x60) dv->scc_wave[4][addr & 31] = (int8_t)data; /* channels 4 and 5 share it */
			}
			break;
		case 1: /* frequency */
			if (addr < 10)
			{
				int ch = addr >> 1;
				if (addr & 1) dv->scc_period[ch] = (dv->scc_period[ch] & 0x0FF) | ((data & 0x0F) << 8);
				else dv->scc_period[ch] = (dv->scc_period[ch] & 0xF00) | data;
			}
			break;
		case 2: /* volume */
			if (addr < 5)
			{
				int old = dv->scc_vol[addr], nv = data & 0x0F;
				if (nv && (old == 0 || nv >= old + 4)) dv->trig |= 1u << addr;
				dv->scc_vol[addr] = nv;
			}
			break;
		case 3: /* key on */
			dv->trig |= (data & ~dv->scc_key) & 0x1F;
			dv->scc_key = data & 0x1F;
			break;
	}
}

/* ---- decoding ---- */

static void fm_opn (struct s98vis_t *v, struct s98dev_t *dv, int ch, struct nsfvis_ch_t *c)
{
	static const uint8_t carriers[8] = {8, 8, 8, 8, 10, 14, 14, 15};
	static const uint8_t slot[4] = {0x0, 0x8, 0x4, 0xC};
	const uint8_t *r = dv->regs[ch / 3];
	int k = ch % 3, op, tl = 127, reg, pn = 0;
	int fnum = ((r[0xA4 + k] & 7) << 8) | r[0xA0 + k];
	int block = (r[0xA4 + k] >> 3) & 7;
	int alg = r[0xB0 + k] & 7;
	double prescale = (dv->type == DEV_YM2203) ? 72.0 : 144.0;
	uint8_t patch[29];
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
		if (reg == 0x40) continue;
		for (op = 0; op < 4; op++) patch[pn++] = r[reg + slot[op] + k];
	}
	patch[pn++] = r[0xB0 + k];
	c->ins = nsfvis_wave_id (&v->layout, patch, pn);
	c->vol = (tl >= 120) ? 0 : 15 - tl / 8; /* 6 dB per step */
	c->trig = (dv->trig >> ch) & 1;
	if (fnum)
	{
		c->note = nsfvis_note (fnum * pow (2.0, block - 1) * dv->clock / prescale / 1048576.0, &c->pitch);
	}
	c->active = dv->key[ch] && (dv->type == DEV_YM2203 || (r[0xB4 + k] & 0xC0)) && c->note >= 0;
}

static void ssg (struct s98dev_t *dv, int i, struct nsfvis_ch_t *c)
{
	const uint8_t *r = dv->regs[0];
	int period = r[2 * i] | ((r[2 * i + 1] & 0x0F) << 8);
	int volr = r[8 + i];
	int tone = !((r[7] >> i) & 1), noise = !((r[7] >> (i + 3)) & 1);
	double clock = dv->clock;
	if (dv->type == DEV_YM2203) clock /= 2;       /* SSG part runs at the master clock / 2 */
	else if (dv->type == DEV_YM2608) clock /= 4;  /* and / 4 on the YM2608 */
	else if (dv->type == DEV_YM2149) clock /= 1;
	c->vol = (volr & 0x10) ? 15 : (volr & 0x0F);
	c->ins = (volr & 0x10) ? 1 : 0;               /* 01 = hardware envelope */
	c->trig = (dv->trig >> (8 + i)) & 1;
	if (tone)
	{
		c->note = nsfvis_note (clock / (16.0 * (period ? period : 1)), &c->pitch);
		c->active = c->vol && c->note >= 0;
	} else if (noise)
	{
		c->kind = NSFVIS_NOISE;
		c->aux = (r[6] & 0x1F) >> 1; /* period: shown as 15 - aux, higher = brighter */
		c->active = c->vol != 0;
	}
}

static void opm (struct s98vis_t *v, struct s98dev_t *dv, int ch, struct nsfvis_ch_t *c)
{
	static const uint8_t carriers[8] = {8, 8, 8, 8, 12, 14, 14, 15}; /* bit0 M1, bit1 M2, bit2 C1, bit3 C2 */
	static const int semis[16] = {1, 2, 3, 3, 4, 5, 6, 6, 7, 8, 9, 9, 10, 11, 12, 12}; /* KC note -> semitones above C */
	const uint8_t *r = dv->regs[0];
	int kc = r[0x28 + ch], kf = r[0x30 + ch] >> 2, con = r[0x20 + ch] & 7, op, tl = 127, pn = 0, reg;
	uint8_t patch[30];
	double midi;
	for (op = 0; op < 4; op++)
	{
		if ((carriers[con] >> op) & 1)
		{
			int t = r[0x60 + 8 * op + ch] & 0x7F;
			if (t < tl) tl = t;
		}
	}
	for (reg = 0x40; reg <= 0xE0; reg += 0x20)
	{
		if (reg == 0x60) continue;
		for (op = 0; op < 4; op++) patch[pn++] = r[reg + 8 * op + ch];
	}
	patch[pn++] = r[0x20 + ch] & 0x3F;
	c->ins = nsfvis_wave_id (&v->layout, patch, pn);
	c->vol = (tl >= 120) ? 0 : 15 - tl / 8; /* 6 dB per step */
	c->trig = (dv->trig >> ch) & 1;
	/* KC $4A = A-4 (440 Hz) with a 3.579545 MHz clock */
	midi = 12.0 * (((kc >> 4) & 7) + 1) + semis[kc & 15] + kf / 64.0;
	if (dv->clock) midi += 12.0 * log2 (dv->clock / 3579545.0);
	c->note = nsfvis_note (440.0 * pow (2.0, (midi - 69.0) / 12.0), &c->pitch);
	c->active = dv->key[ch] && (r[0x20 + ch] & 0xC0) && c->note >= 0;
}

static void opll (struct s98dev_t *dv, int ch, struct nsfvis_ch_t *c)
{
	const uint8_t *r = dv->regs[0];
	int fnum = r[0x10 + ch] | ((r[0x20 + ch] & 1) << 8);
	int block = (r[0x20 + ch] >> 1) & 7;
	c->ins = r[0x30 + ch] >> 4;
	c->vol = 15 - (r[0x30 + ch] & 15);
	c->trig = (dv->trig >> ch) & 1;
	if (fnum)
	{
		c->note = nsfvis_note (fnum * (double)dv->clock / 72.0 / (double)(1 << (19 - block)), &c->pitch);
	}
	c->active = (r[0x20 + ch] & 0x10) && c->note >= 0;
	if (ch >= 6 && (r[0x0E] & 0x20))
	{
		c->kind = NSFVIS_PCM;
		c->note = -1;
		c->ins = 0xFF;
		c->active = (r[0x0E] & 0x1F) != 0;
	}
}

static void scc (struct s98vis_t *v, struct s98dev_t *dv, int i, struct nsfvis_ch_t *c)
{
	int p = dv->scc_period[i];
	c->vol = dv->scc_vol[i];
	c->ins = nsfvis_wave_id (&v->layout, (const unsigned char *)dv->scc_wave[i], 32);
	c->trig = (dv->trig >> i) & 1;
	if (p > 8)
	{
		c->note = nsfvis_note ((double)dv->clock / (16.0 * (p + 1)), &c->pitch);
	}
	c->active = ((dv->scc_key >> i) & 1) && c->vol && c->note >= 0;
}

static void decode (struct s98vis_t *v, struct nsfvis_ch_t *out)
{
	int i;
	for (i = 0; i < v->layout.count; i++)
	{
		struct s98dev_t *dv = &v->dev[v->chdev[i]];
		struct nsfvis_ch_t *c = out + i;
		memset (c, 0, sizeof (*c));
		c->note = -1;
		c->ins = 0xFF;
		c->kind = NSFVIS_TONE;
		switch (v->chkind[i])
		{
			case CH_FM: fm_opn (v, dv, v->chidx[i], c); break;
			case CH_SSG: ssg (dv, v->chidx[i], c); break;
			case CH_RHYTHM:
				c->kind = NSFVIS_DRUMS;
				c->aux = dv->rhythm_hits;
				c->trig = dv->rhythm_hits != 0;
				c->vol = (dv->regs[0][0x11] & 0x3F) >> 2;
				c->active = 1;
				break;
			case CH_ADPCM:
				c->kind = NSFVIS_PCM;
				c->vol = dv->regs[1][0x0B] >> 4;
				c->trig = (dv->trig >> 16) & 1;
				c->active = dv->adpcm_on;
				break;
			case CH_OPM: opm (v, dv, v->chidx[i], c); break;
			case CH_PSG_TONE:
			{
				int k = v->chidx[i], p = dv->psg_period[k] ? dv->psg_period[k] : 1024;
				c->vol = 15 - dv->psg_vol[k];
				c->trig = (dv->trig >> k) & 1;
				c->note = nsfvis_note ((double)dv->clock / (32.0 * p), &c->pitch);
				c->active = c->vol && c->note >= 0;
				break;
			}
			case CH_PSG_NOISE:
				c->kind = NSFVIS_NOISE;
				c->vol = 15 - dv->psg_vol[3];
				c->aux = (dv->psg_noise & 3) * 4;
				c->ins = (dv->psg_noise >> 2) & 1;
				c->trig = (dv->trig >> 3) & 1;
				c->active = c->vol != 0;
				break;
			case CH_OPLL: opll (dv, v->chidx[i], c); break;
			case CH_SCC: scc (v, dv, v->chidx[i], c); break;
		}
		if (c->kind != NSFVIS_TONE) c->pitch = 0;
		if (!c->active && c->kind != NSFVIS_DRUMS) c->vol = 0;
	}
}

static struct s98dev_t *vgm_dev (struct s98vis_t *v, int libvgm, int inst)
{
	int d = v->vgmdev[libvgm & 0x1F][inst & 1];
	return (d >= 0) ? &v->dev[d] : 0;
}

/* one VGM command (VGM specification 1.71) */
static void vgm_step (struct s98vis_t *v)
{
	const uint8_t *p;
	int cmd, n;
	struct s98dev_t *dv;
	if (v->pos >= v->len)
	{
		v->ended = 1;
		return;
	}
	p = v->data + v->pos;
	cmd = p[0];
	/* length of the command, operands included */
	if (cmd >= 0x30 && cmd <= 0x3F) n = 2;
	else if (cmd >= 0x40 && cmd <= 0x4E) n = 3;
	else if (cmd == 0x4F || cmd == 0x50) n = 2;
	else if (cmd >= 0x51 && cmd <= 0x5F) n = 3;
	else if (cmd == 0x61) n = 3;
	else if (cmd == 0x62 || cmd == 0x63 || cmd == 0x66) n = 1;
	else if (cmd == 0x67) n = (v->pos + 7 <= v->len) ? 7 + (int)(le32 (p + 3) & 0x7FFFFFFF) : 7;
	else if (cmd == 0x68) n = 12;
	else if (cmd >= 0x70 && cmd <= 0x8F) n = 1;
	else if (cmd == 0x90 || cmd == 0x91 || cmd == 0x95) n = 5;
	else if (cmd == 0x92) n = 6;
	else if (cmd == 0x93) n = 11;
	else if (cmd == 0x94) n = 2;
	else if (cmd >= 0xA0 && cmd <= 0xBF) n = 3;
	else if (cmd >= 0xC0 && cmd <= 0xDF) n = 4;
	else if (cmd >= 0xE0) n = 5;
	else
	{
		v->ended = 1; /* unknown command */
		return;
	}
	if ((uint64_t)v->pos + n > v->len)
	{
		v->ended = 1;
		return;
	}
	v->pos += n;
	switch (cmd)
	{
		case 0x61: v->time += (p[1] | (p[2] << 8)) / 44100.0; break;
		case 0x62: v->time += 735 / 44100.0; break;
		case 0x63: v->time += 882 / 44100.0; break;
		case 0x66:
			if (v->loop && v->loop >= v->start && v->loop < v->len)
			{
				v->pos = v->loop;
				v->looped = 1;
			} else {
				v->ended = 1;
			}
			break;
		case 0x50: case 0x30: if ((dv = vgm_dev (v, 0x00, cmd == 0x30))) write_psg (dv, p[1]); break;
		case 0x51: case 0xA1: if ((dv = vgm_dev (v, 0x01, cmd == 0xA1))) write_reg (dv, 0, p[1], p[2]); break;
		case 0x52: case 0xA2: if ((dv = vgm_dev (v, 0x02, cmd == 0xA2))) write_reg (dv, 0, p[1], p[2]); break;
		case 0x53: case 0xA3: if ((dv = vgm_dev (v, 0x02, cmd == 0xA3))) write_reg (dv, 1, p[1], p[2]); break;
		case 0x54: case 0xA4: if ((dv = vgm_dev (v, 0x03, cmd == 0xA4))) write_reg (dv, 0, p[1], p[2]); break;
		case 0x55: case 0xA5: if ((dv = vgm_dev (v, 0x06, cmd == 0xA5))) write_reg (dv, 0, p[1], p[2]); break;
		case 0x56: case 0xA6: if ((dv = vgm_dev (v, 0x07, cmd == 0xA6))) write_reg (dv, 0, p[1], p[2]); break;
		case 0x57: case 0xA7: if ((dv = vgm_dev (v, 0x07, cmd == 0xA7))) write_reg (dv, 1, p[1], p[2]); break;
		case 0xA0: if ((dv = vgm_dev (v, 0x12, p[1] >> 7))) write_reg (dv, 0, p[1] & 0x7F, p[2]); break;
		case 0xD2: if ((dv = vgm_dev (v, 0x19, p[1] >> 7))) write_scc (dv, p[1] & 0x7F, p[2], p[3]); break;
		default:
			if (cmd >= 0x70 && cmd <= 0x7F) v->time += (cmd - 0x6F) / 44100.0;
			else if (cmd >= 0x80 && cmd <= 0x8F) v->time += (cmd - 0x80) / 44100.0;
			break;
	}
}

/* Reads the command stream up to song time t (seconds, loops included), producing
 * one frame every 1/60 s: emit (arg, song time of the frame, decoded channels). */
void s98vis_run_until (struct s98vis_t *v, double t, void (*emit)(void *arg, double time, const struct nsfvis_ch_t *ch), void *arg)
{
	struct nsfvis_ch_t ch[NSFVIS_MAXCH];
	while (1)
	{
		/* frames due before the next command */
		while (v->next <= v->time && v->next <= t)
		{
			int d;
			decode (v, ch);
			if (emit) emit (arg, v->next, ch);
			for (d = 0; d < v->ndev; d++)
			{
				v->dev[d].trig = 0;
				v->dev[d].rhythm_hits = 0;
			}
			v->frame++;
			v->next = v->frame / 60.0;
		}
		if (v->time > t || v->ended)
		{
			if (v->ended)
			{ /* nothing more is written: keep producing frames of the last state */
				while (v->next <= t)
				{
					decode (v, ch);
					if (emit) emit (arg, v->next, ch);
					v->frame++;
					v->next = v->frame / 60.0;
				}
			}
			return;
		}
		if (v->vgm)
		{
			vgm_step (v);
		} else {
			int cmd;
			if (v->pos >= v->len)
			{
				v->ended = 1;
				continue;
			}
			cmd = v->data[v->pos++];
			if (cmd == 0xFF)
			{
				v->time += v->tick;
			} else if (cmd == 0xFE)
			{
				uint32_t n = 0;
				int shift = 0;
				while (v->pos < v->len)
				{
					int b = v->data[v->pos++];
					n |= (uint32_t)(b & 0x7F) << shift;
					shift += 7;
					if (!(b & 0x80) || shift > 28) break;
				}
				v->time += (n + 2) * v->tick;
			} else if (cmd == 0xFD)
			{
				if (v->loop && v->loop < v->len && v->loop >= v->start)
				{
					v->pos = v->loop;
					v->looped = 1;
				}
				else v->ended = 1;
			} else if (cmd < 0x80)
			{
				int d = cmd >> 1, port = cmd & 1;
				if (v->pos + 2 > v->len)
				{
					v->ended = 1;
					continue;
				}
				if (d < v->ndev)
				{
					if (v->dev[d].type == DEV_SN76489) write_psg (&v->dev[d], v->data[v->pos + 1]);
					else write_reg (&v->dev[d], port, v->data[v->pos], v->data[v->pos + 1]);
				}
				v->pos += 2;
			} else {
				v->ended = 1; /* unknown command */
			}
		}
	}
}
