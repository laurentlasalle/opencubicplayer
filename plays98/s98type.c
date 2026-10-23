/* OpenCP Module Player
 * S98 file type detection and tag reading for the fileselector
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <ctype.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "boot/plinkman.h"
#include "filesel/filesystem.h"
#include "filesel/mdb.h"
#include "filesel/pfilesel.h"
#include "stuff/err.h"
#include "s98type.h"
#include "../playgme/vgmchips.h"

static uint32_t le32 (const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Shift-JIS (CP932) or UTF-8 text into a zero-terminated UTF-8 field */
static void s98_text (const char *src, size_t srclen, char *dst, size_t dstlen, int is_utf8, const struct mdbReadInfoAPI_t *API)
{
	iconv_t ic;
	char *in = (char *)src;
	char *out = dst;
	size_t inleft = srclen, outleft = dstlen - 1;

	while (srclen && ((src[srclen-1] == '\r') || (src[srclen-1] == ' ')))
	{
		srclen--;
	}
	inleft = srclen;

	if (is_utf8)
	{
		size_t n = (srclen < dstlen - 1) ? srclen : dstlen - 1;
		memcpy (dst, src, n);
		dst[n] = 0;
		return;
	}
	ic = iconv_open ("UTF-8", "CP932");
	if (ic == (iconv_t)-1)
	{
		API->cp437_f_to_utf8_z (src, srclen, dst, dstlen);
		return;
	}
	while (inleft && outleft)
	{
		if (iconv (ic, (void *)&in, &inleft, &out, &outleft) == (size_t)-1)
		{
			if (!inleft || !outleft)
			{
				break;
			}
			/* skip one invalid byte */
			in++;
			inleft--;
		}
	}
	*out = 0;
	iconv_close (ic);
}

static const char *s98_devname (uint32_t type)
{
	switch (type)
	{
		case 1:  return "YM2149";
		case 2:  return "YM2203";
		case 3:  return "YM2612";
		case 4:  return "YM2608";
		case 5:  return "YM2151";
		case 6:  return "YM2413";
		case 7:  return "YM3526";
		case 8:  return "YM3812";
		case 9:  return "YMF262";
		case 15: return "AY-3-8910";
		case 16: return "SN76489";
		default: return 0;
	}
}

static void s98_parse_v3_tags (struct moduleinfostruct *m, const uint8_t *p, size_t len, const struct mdbReadInfoAPI_t *API)
{
	int utf8 = 0;
	size_t i = 0;

	if ((len >= 3) && (p[0] == 0xef) && (p[1] == 0xbb) && (p[2] == 0xbf))
	{
		utf8 = 1;
		i = 3;
	}
	while (i < len && p[i])
	{
		size_t start = i, eq = 0;
		while (i < len && p[i] && (p[i] != 0x0a))
		{
			if (!eq && (p[i] == '='))
			{
				eq = i;
			}
			i++;
		}
		if (eq)
		{
			const char *key = (const char *)p + start;
			size_t keylen = eq - start;
			const char *val = (const char *)p + eq + 1;
			size_t vallen = i - eq - 1;
#define KEY(k) ((keylen == strlen (k)) && !strncasecmp (key, k, keylen))
			if (KEY ("title"))
			{
				s98_text (val, vallen, m->title, sizeof (m->title), utf8, API);
			} else if (KEY ("artist"))
			{
				s98_text (val, vallen, m->composer, sizeof (m->composer), utf8, API);
			} else if (KEY ("game"))
			{
				s98_text (val, vallen, m->album, sizeof (m->album), utf8, API);
			} else if (KEY ("comment") && !m->comment[0])
			{
				s98_text (val, vallen, m->comment, sizeof (m->comment), utf8, API);
			} else if (KEY ("copyright") && !m->comment[0])
			{
				s98_text (val, vallen, m->comment, sizeof (m->comment), utf8, API);
			} else if (KEY ("year") && (vallen >= 4))
			{
				m->date = (uint32_t)atoi (val) << 16;
			}
#undef KEY
		}
		if (i < len && p[i] == 0x0a)
		{
			i++;
		}
	}
}

/* GD3 tag string (UTF-16LE, zero-terminated) into UTF-8; returns the next string */
static const uint8_t *vgm_gd3_str (const uint8_t *p, const uint8_t *end, char *dst, size_t dstlen)
{
	size_t used = 0;
	while (p + 1 < end)
	{
		uint32_t c = p[0] | (p[1] << 8);
		p += 2;
		if (!c)
		{
			break;
		}
		if ((c >= 0xD800) && (c < 0xDC00) && (p + 1 < end))
		{
			uint32_t c2 = p[0] | (p[1] << 8);
			if ((c2 >= 0xDC00) && (c2 < 0xE000))
			{
				c = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
				p += 2;
			}
		}
		if (c < 0x80)
		{
			if (used + 1 < dstlen) dst[used++] = c;
		} else if (c < 0x800)
		{
			if (used + 2 < dstlen) { dst[used++] = 0xC0 | (c >> 6); dst[used++] = 0x80 | (c & 0x3F); }
		} else if (c < 0x10000)
		{
			if (used + 3 < dstlen) { dst[used++] = 0xE0 | (c >> 12); dst[used++] = 0x80 | ((c >> 6) & 0x3F); dst[used++] = 0x80 | (c & 0x3F); }
		} else {
			if (used + 4 < dstlen) { dst[used++] = 0xF0 | (c >> 18); dst[used++] = 0x80 | ((c >> 12) & 0x3F); dst[used++] = 0x80 | ((c >> 6) & 0x3F); dst[used++] = 0x80 | (c & 0x3F); }
		}
	}
	if (dstlen)
	{
		dst[used] = 0;
	}
	return p;
}

/* VGM files with chips that Game Music Emu does not emulate (see playgme/vgmchips.h) */
static int vgmReadInfo (struct moduleinfostruct *m, struct ocpfilehandle_t *fp, const char *buf, size_t len, const struct mdbReadInfoAPI_t *API)
{
	const uint8_t *h = (const uint8_t *)buf;
	uint32_t gd3, samples;

	if (vgm_chips_class (h, len) != VGM_CHIPS_OTHER)
	{
		return 0;
	}
	m->modtype.integer.i = MODULETYPE("VGMX");

	samples = le32 (h + 0x18); /* 44100 Hz */
	m->playtime = (samples / 44100 > 0xffff) ? 0xffff : (uint16_t)(samples / 44100);

	gd3 = le32 (h + 0x14);
	if (fp && gd3 && ((uint64_t)gd3 + 0x14 + 12 < fp->filesize (fp)))
	{
		uint8_t head[12];
		fp->seek_set (fp, gd3 + 0x14);
		if ((fp->read (fp, head, 12) == 12) && !memcmp (head, "Gd3 ", 4))
		{
			uint32_t size = le32 (head + 8);
			uint8_t *t;
			if ((size < 65536) && (t = malloc (size)))
			{
				if (fp->read (fp, t, size) == size)
				{
					/* track, track (jp), game, game (jp), system, system (jp), author, author (jp), date, ripper, notes */
					char f[11][128];
					const uint8_t *p = t;
					int i;
					for (i = 0; i < 11; i++)
					{
						p = vgm_gd3_str (p, t + size, f[i], sizeof (f[i]));
					}
					snprintf (m->title,    sizeof (m->title),    "%s", f[0][0] ? f[0] : f[1]);
					snprintf (m->album,    sizeof (m->album),    "%s", f[2][0] ? f[2] : f[3]);
					snprintf (m->comment,  sizeof (m->comment),  "%s", f[4][0] ? f[4] : f[5]);
					snprintf (m->composer, sizeof (m->composer), "%s", f[6][0] ? f[6] : f[7]);
					if (isdigit ((unsigned char)f[8][0]) && isdigit ((unsigned char)f[8][1]) && isdigit ((unsigned char)f[8][2]) && isdigit ((unsigned char)f[8][3]))
					{
						m->date = (uint32_t)atoi (f[8]) << 16;
					}
				}
				free (t);
			}
		}
		fp->seek_set (fp, 0);
	}
	return 1;
}

static int s98ReadInfo (struct moduleinfostruct *m, struct ocpfilehandle_t *fp, const char *buf, size_t len, const struct mdbReadInfoAPI_t *API)
{
	const uint8_t *h = (const uint8_t *)buf;
	uint8_t *data;
	uint64_t filesize;
	uint32_t tagofs, dumpofs, num, den, devcount;

	if ((len >= 4) && !memcmp (buf, "Vgm ", 4))
	{
		return vgmReadInfo (m, fp, buf, len, API);
	}
	if ((len < 0x20) || memcmp (buf, "S98", 3) || (buf[3] < '0') || (buf[3] > '3'))
	{
		return 0;
	}
	m->modtype.integer.i = MODULETYPE("S98");

	if (!fp)
	{
		return 1;
	}
	filesize = fp->filesize (fp);
	if ((filesize < 0x20) || (filesize > 32*1024*1024))
	{
		return 1;
	}
	data = malloc (filesize);
	if (!data)
	{
		return 1;
	}
	fp->seek_set (fp, 0);
	if (fp->read (fp, data, filesize) != filesize)
	{
		free (data);
		return 1;
	}

	num = le32 (h + 0x04); if (!num) num = 10;
	den = le32 (h + 0x08); if (!den) den = 1000;
	tagofs = le32 (h + 0x10);
	dumpofs = le32 (h + 0x14);
	devcount = (buf[3] == '3') ? le32 (h + 0x1c) : 0;

	/* tags */
	if (tagofs && (tagofs < filesize))
	{
		const uint8_t *t = data + tagofs;
		size_t tlen = filesize - tagofs;
		if ((tlen >= 5) && !memcmp (t, "[S98]", 5))
		{
			s98_parse_v3_tags (m, t + 5, tlen - 5, API);
		} else {
			size_t n = 0;
			while ((n < tlen) && t[n]) n++;
			s98_text ((const char *)t, n, m->title, sizeof (m->title), 0, API);
		}
	}

	/* sound chips, shown in the comment field when there is no comment tag */
	if (!m->comment[0])
	{
		if (!devcount)
		{
			snprintf (m->comment, sizeof (m->comment), "YM2608 (OPNA)");
		} else {
			uint32_t i;
			size_t used = 0;
			for (i = 0; (i < devcount) && (0x20 + i * 16 + 4 <= filesize); i++)
			{
				const char *n = s98_devname (le32 (data + 0x20 + i * 16));
				if (n && (used + strlen (n) + 3 < sizeof (m->comment)))
				{
					used += snprintf (m->comment + used, sizeof (m->comment) - used, "%s%s", used ? ", " : "", n);
				}
			}
		}
	}

	/* length of one pass */
	if (dumpofs && (dumpofs < filesize))
	{
		uint64_t syncs = 0;
		uint64_t p = dumpofs;
		while (p < filesize)
		{
			uint8_t c = data[p++];
			if (c == 0xff)
			{
				syncs++;
			} else if (c == 0xfe)
			{
				uint64_t v = 0;
				int shift = 0;
				uint8_t b;
				do
				{
					if (p >= filesize) break;
					b = data[p++];
					v |= (uint64_t)(b & 0x7f) << shift;
					shift += 7;
				} while ((b & 0x80) && (shift < 35));
				syncs += v + 2;
			} else if (c == 0xfd)
			{
				break;
			} else if (c < 0x80)
			{
				p += 2;
			} else {
				break; /* unknown command */
			}
		}
		{
			uint64_t secs = syncs * num / den;
			m->playtime = (secs > 0xffff) ? 0xffff : (uint16_t)secs;
		}
	}

	free (data);
	return 1;
}

static const char *S98_description[] =
{
	//                                                                          |
	"S98 files are register dumps from Japanese home computers, mainly NEC PC-88",
	"and PC-98, also Sharp X1 and X68000. They usually use Yamaha FM sound ICs:",
	"YM2203 (OPN), YM2608 (OPNA) with its SSG part, YM2151 (OPM)... Playback",
	"using libvgm by Valley Bell.",
	NULL
};

static const char *VGMX_description[] =
{
	//                                                                          |
	"VGM files for sound chips that Game Music Emu does not emulate: MSX (AY-3-",
	"8910, Konami SCC), PC-88/PC-98 (YM2203, YM2608), X68000 and arcade boards",
	"(YM2151, PCM chips)... Playback using libvgm by Valley Bell. VGM files for",
	"Sega consoles keep using Game Music Emu (with the tracker view).",
	NULL
};

static struct mdbreadinforegstruct s98ReadInfoReg = {"S98", s98ReadInfo MDBREADINFOREGSTRUCT_TAIL};

OCP_INTERNAL int s98_type_init (struct PluginInitAPI_t *API)
{
	struct moduletype mt;

	API->fsRegisterExt ("S98");
	mt.integer.i = MODULETYPE("S98");
	API->fsTypeRegister (mt, S98_description, "plOpenCP", &s98Player);
	mt.integer.i = MODULETYPE("VGMX");
	API->fsTypeRegister (mt, VGMX_description, "plOpenCP", &s98Player);

	API->mdbRegisterReadInfo(&s98ReadInfoReg);

	return errOk;
}

OCP_INTERNAL void s98_type_done (struct PluginCloseAPI_t *API)
{
	struct moduletype mt;

	mt.integer.i = MODULETYPE("S98");
	API->fsTypeUnregister (mt);
	mt.integer.i = MODULETYPE("VGMX");
	API->fsTypeUnregister (mt);

	API->mdbUnregisterReadInfo(&s98ReadInfoReg);
}
