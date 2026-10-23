#ifndef _PLAYGME_VGMCHIPS_H
#define _PLAYGME_VGMCHIPS_H 1

/* Which player gets a VGM file, from the chip clocks of its header (shared by playgme
 * and plays98 so both agree):
 *  - VGM_CHIPS_SEGA:  only SN76489, YM2413 and YM2612, the chips Game Music Emu emulates
 *                     (with the tracker view)
 *  - VGM_CHIPS_OPL:   only YM3812, YM3526 and YMF262, played by AdPlug (as before)
 *  - VGM_CHIPS_OTHER: anything else (AY-3-8910, SCC, YM2151, YM2608, arcade PCM...),
 *                     played by libvgm (plays98) */

#include <stddef.h>
#include <stdint.h>

#define VGM_CHIPS_NONE  0
#define VGM_CHIPS_SEGA  1
#define VGM_CHIPS_OPL   2
#define VGM_CHIPS_OTHER 3

static inline uint32_t vgm_chips_le32 (const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline int vgm_chips_class (const uint8_t *b, size_t len)
{
	static const uint8_t sega[]  = {0x0C, 0x10, 0x2C};
	static const uint8_t opl[]   = {0x50, 0x54, 0x5C};
	static const uint8_t other[] = {0x30, 0x38, 0x40, 0x44, 0x48, 0x4C, 0x58, 0x60, 0x64, 0x68, 0x6C,
	                                0x70, 0x74, 0x80, 0x84, 0x88, 0x8C, 0x90, 0x98, 0x9C, 0xA0, 0xA4,
	                                0xA8, 0xAC, 0xB0, 0xB4, 0xB8, 0xC0, 0xC4, 0xC8, 0xCC, 0xD0, 0xD8,
	                                0xDC, 0xE0};
	uint32_t version, hdr = 0x40;
	int s = 0, o = 0, x = 0;
	size_t i;

	if ((len < 0x40) || (b[0] != 'V') || (b[1] != 'g') || (b[2] != 'm') || (b[3] != ' '))
	{
		return VGM_CHIPS_NONE;
	}
	version = vgm_chips_le32 (b + 0x08);
	if ((version >= 0x150) && vgm_chips_le32 (b + 0x34))
	{
		hdr = 0x34 + vgm_chips_le32 (b + 0x34);
	}
	if (hdr > len)
	{
		hdr = (uint32_t)len;
	}
#define VGM_CLOCK(ofs) ((((ofs) + 4) <= hdr) && (vgm_chips_le32 (b + (ofs)) & 0x3FFFFFFF))
	for (i = 0; i < sizeof (sega); i++)
	{
		if (((sega[i] < 0x2C) || (version >= 0x110)) && VGM_CLOCK (sega[i])) s = 1;
	}
	for (i = 0; i < sizeof (opl); i++)
	{
		if ((version >= 0x151) && VGM_CLOCK (opl[i])) o = 1;
	}
	for (i = 0; i < sizeof (other); i++)
	{
		if (((other[i] != 0x30) || (version >= 0x110)) && (other[i] < 0x34 || version >= 0x151) && VGM_CLOCK (other[i])) x = 1;
	}
#undef VGM_CLOCK
	if (s && !o && !x) return VGM_CHIPS_SEGA;
	if (o && !s && !x) return VGM_CHIPS_OPL;
	if (s || o || x)   return VGM_CHIPS_OTHER;
	return VGM_CHIPS_NONE;
}

#endif
