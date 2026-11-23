#ifndef _GSFLOAD_H
#define _GSFLOAD_H 1

/* GSF (Game Boy Advance Sound Format) loader.
 *
 * A GSF file is a PSF file (version 0x22): a header, a zlib-compressed program and
 * optional "[TAG]" text lines (key=value). The program starts with three 32-bit
 * values (entry point, load address, size) followed by the data to load into the
 * cartridge ROM (address 0x08000000) or, for multiboot programs, into the external
 * work RAM (address 0x02000000).
 *
 * A MINIGSF file holds only what differs between the tracks of a game (usually the
 * track number) and names the library holding the rest in its "_lib" tag (a GSFLIB
 * file, in the same directory). Loading order, as in the PSF specification: the
 * "_lib" library first (recursively), then the file itself, then "_lib2", "_lib3"
 * and so on, each one written over the previous ones. The entry point is the one
 * of the first program loaded. */

#include <stddef.h>
#include <stdint.h>

struct gsf_tags_t
{
	char title[128];
	char artist[128];
	char game[128];
	char year[32];
	char genre[64];
	char copyright[128];
	char comment[256];
	char gsfby[128];      /* who ripped the music */
	char lib[128];        /* name of the "_lib" library, empty for a stand-alone GSF */
	int32_t length_ms;    /* -1 when the file gives no length */
	int32_t fade_ms;      /* -1 when the file gives no fade */
};

struct gsf_image_t
{
	uint8_t *data;        /* ROM (or multiboot) image, starting at the base address; free() it */
	size_t size;
	uint32_t entry;       /* 0x08000000 for a ROM, 0x02000000 for a multiboot program */
	int multiboot;
};

/* reads a file next to the one being loaded (for "_lib" tags); returns malloc()ed
 * data, or NULL if the file is missing */
typedef uint8_t *(*gsf_readfile_t) (void *ctx, const char *name, size_t *size);

/* the tags of a GSF/MINIGSF file (only its own tags, no file is read); returns 0 if
 * the data is not a GSF file */
OCP_INTERNAL int gsf_read_tags (const uint8_t *data, size_t len, struct gsf_tags_t *tags);

/* the tags of a "[TAG]" area (t points after "[TAG]"); t can be NULL (no tags) */
OCP_INTERNAL int gsf_parse_tags (const uint8_t *t, size_t tlen, struct gsf_tags_t *tags);

/* from the first 16 bytes of a file: where "[TAG]" would start; returns 0 if the
 * header is not the one of a GSF file */
OCP_INTERNAL int gsf_tags_offset (const uint8_t *header, size_t len, uint64_t *offset);

/* builds the program image of a GSF/MINIGSF file and its libraries; returns 0 on
 * success, or a negative value (see the GSF_ERR_* values) */
#define GSF_ERR_FORMAT   -1  /* not a valid GSF file */
#define GSF_ERR_LIBRARY  -2  /* a library named by _lib is missing or invalid */
#define GSF_ERR_MEMORY   -3
OCP_INTERNAL int gsf_load (const uint8_t *data, size_t len, gsf_readfile_t readfile, void *ctx, struct gsf_image_t *image, char *missing, size_t missinglen);

/* "m:ss", "h:mm:ss", "s.mmm" or "m:ss,mmm" into milliseconds, -1 if not valid */
OCP_INTERNAL int32_t gsf_parse_time (const char *s);

#endif
