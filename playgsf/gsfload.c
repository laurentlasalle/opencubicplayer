/* OpenCP Module Player
 * GSF (Game Boy Advance Sound Format) loader, see gsfload.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include "types.h"
#include "gsfload.h"

#define PSF_VERSION_GSF 0x22
#define MAX_IMAGE   (32 * 1024 * 1024) /* largest GBA cartridge */
#define MAX_PROGRAM (MAX_IMAGE + 12)
#define MAX_DEPTH   10                 /* libraries that name libraries */
#define MAX_TAGS    50000              /* the PSF specification limits the tag area */

static uint32_t le32 (const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* header check; gives where the compressed program and the tags are */
static int psf_parse (const uint8_t *data, size_t len, const uint8_t **program, size_t *programlen, const uint8_t **tags, size_t *tagslen)
{
	uint64_t reserved, compressed, tagofs;
	if ((len < 16) || memcmp (data, "PSF", 3) || (data[3] != PSF_VERSION_GSF))
	{
		return 0;
	}
	reserved = le32 (data + 4);
	compressed = le32 (data + 8);
	if (16 + reserved + compressed > len)
	{
		return 0;
	}
	*program = data + 16 + reserved;
	*programlen = compressed;
	tagofs = 16 + reserved + compressed;
	if ((tagofs + 5 <= len) && !memcmp (data + tagofs, "[TAG]", 5))
	{
		*tags = data + tagofs + 5;
		*tagslen = len - tagofs - 5;
		if (*tagslen > MAX_TAGS)
		{
			*tagslen = MAX_TAGS;
		}
	} else {
		*tags = 0;
		*tagslen = 0;
	}
	return 1;
}

/* calls found() for each key=value line; keys are lower-cased, and spaces (and
 * control characters) around keys and values are removed */
static void tags_foreach (const uint8_t *t, size_t len, void (*found)(void *ctx, const char *key, const char *value), void *ctx)
{
	size_t i = 0;
	while (i < len)
	{
		char key[64], value[512];
		size_t start = i, eq = 0, end, a, b, n;
		while ((i < len) && (t[i] != 0x0a))
		{
			if (!eq && (t[i] == '='))
			{
				eq = i;
			}
			i++;
		}
		end = i;
		i++; /* skip the line feed */
		if (!eq)
		{
			continue;
		}
		for (a = start; (a < eq) && (t[a] <= 0x20); a++);
		for (b = eq; (b > a) && (t[b - 1] <= 0x20); b--);
		n = b - a;
		if (!n || (n >= sizeof (key)))
		{
			continue;
		}
		for (n = 0; a < b; a++)
		{
			key[n++] = tolower (t[a]);
		}
		key[n] = 0;
		for (a = eq + 1; (a < end) && (t[a] <= 0x20); a++);
		for (b = end; (b > a) && (t[b - 1] <= 0x20); b--);
		n = b - a;
		if (n >= sizeof (value))
		{
			n = sizeof (value) - 1;
		}
		memcpy (value, t + a, n);
		value[n] = 0;
		found (ctx, key, value);
	}
}

/* copies a tag value; a key given on several lines makes a value of several lines */
static void tag_set (char *dst, size_t dstlen, const char *value)
{
	size_t used = strlen (dst);
	if (used)
	{
		snprintf (dst + used, dstlen - used, "\n%s", value);
	} else {
		snprintf (dst, dstlen, "%s", value);
	}
}

int32_t gsf_parse_time (const char *s)
{
	int64_t total = 0, part = 0;
	int digits = 0, fraction = -1;
	for (; *s; s++)
	{
		if ((*s >= '0') && (*s <= '9'))
		{
			if (fraction >= 0)
			{
				if (fraction < 3)
				{
					part = part * 10 + (*s - '0');
					fraction++;
				}
			} else {
				part = part * 10 + (*s - '0');
				if (part > 1000000)
				{
					return -1;
				}
			}
			digits++;
		} else if ((*s == ':') && (fraction < 0))
		{
			total = (total + part) * 60;
			part = 0;
		} else if (((*s == '.') || (*s == ',')) && (fraction < 0))
		{
			total = (total + part) * 1000;
			part = 0;
			fraction = 0;
		} else if (*s > 0x20)
		{
			return -1;
		}
	}
	if (!digits)
	{
		return -1;
	}
	if (fraction < 0)
	{
		total = (total + part) * 1000;
	} else {
		while (fraction++ < 3)
		{
			part *= 10; /* ".5" is 500 ms */
		}
		total += part;
	}
	return (total > 0x7fffffff) ? -1 : (int32_t)total;
}

static void tags_found (void *ctx, const char *key, const char *value)
{
	struct gsf_tags_t *tags = ctx;
	if      (!strcmp (key, "title"))     tag_set (tags->title,     sizeof (tags->title),     value);
	else if (!strcmp (key, "artist"))    tag_set (tags->artist,    sizeof (tags->artist),    value);
	else if (!strcmp (key, "game"))      tag_set (tags->game,      sizeof (tags->game),      value);
	else if (!strcmp (key, "year"))      tag_set (tags->year,      sizeof (tags->year),      value);
	else if (!strcmp (key, "genre"))     tag_set (tags->genre,     sizeof (tags->genre),     value);
	else if (!strcmp (key, "copyright")) tag_set (tags->copyright, sizeof (tags->copyright), value);
	else if (!strcmp (key, "comment"))   tag_set (tags->comment,   sizeof (tags->comment),   value);
	else if (!strcmp (key, "gsfby"))     tag_set (tags->gsfby,     sizeof (tags->gsfby),     value);
	else if (!strcmp (key, "_lib"))      snprintf (tags->lib, sizeof (tags->lib), "%s", value);
	else if (!strcmp (key, "length"))    tags->length_ms = gsf_parse_time (value);
	else if (!strcmp (key, "fade"))      tags->fade_ms = gsf_parse_time (value);
}

/* Tags are UTF-8 when the file says "utf8=1"; older files use a local character set,
 * taken as Latin-1 unless the text is valid UTF-8. */
static int valid_utf8 (const unsigned char *s)
{
	while (*s)
	{
		int n = (*s < 0x80) ? 0 : ((*s & 0xe0) == 0xc0) ? 1 : ((*s & 0xf0) == 0xe0) ? 2 : ((*s & 0xf8) == 0xf0) ? 3 : -1;
		if (n < 0)
		{
			return 0;
		}
		for (s++; n; n--, s++)
		{
			if ((*s & 0xc0) != 0x80)
			{
				return 0;
			}
		}
	}
	return 1;
}

static void latin1_to_utf8 (char *str, size_t size)
{
	char tmp[512];
	size_t used = 0;
	const unsigned char *s;
	for (s = (const unsigned char *)str; *s && (used + 3 < sizeof (tmp)); s++)
	{
		if (*s < 0x80)
		{
			tmp[used++] = *s;
		} else {
			tmp[used++] = 0xc0 | (*s >> 6);
			tmp[used++] = 0x80 | (*s & 0x3f);
		}
	}
	tmp[used] = 0;
	snprintf (str, size, "%s", tmp);
	while (used && !valid_utf8 ((const unsigned char *)str))
	{ /* cut back to a whole character */
		str[strlen (str) - 1] = 0;
		used--;
	}
}

static void tags_charset (struct gsf_tags_t *tags)
{
	char *fields[] = {tags->title, tags->artist, tags->game, tags->year, tags->genre, tags->copyright, tags->comment, tags->gsfby};
	size_t sizes[] = {sizeof (tags->title), sizeof (tags->artist), sizeof (tags->game), sizeof (tags->year), sizeof (tags->genre), sizeof (tags->copyright), sizeof (tags->comment), sizeof (tags->gsfby)};
	size_t i;
	for (i = 0; i < sizeof (fields) / sizeof (fields[0]); i++)
	{
		if (!valid_utf8 ((const unsigned char *)fields[i]))
		{
			latin1_to_utf8 (fields[i], sizes[i]);
		}
	}
}

int gsf_parse_tags (const uint8_t *t, size_t tlen, struct gsf_tags_t *tags)
{
	memset (tags, 0, sizeof (*tags));
	tags->length_ms = -1;
	tags->fade_ms = -1;
	if (t)
	{
		tags_foreach (t, (tlen > MAX_TAGS) ? MAX_TAGS : tlen, tags_found, tags);
		tags_charset (tags);
	}
	return 1;
}

int gsf_read_tags (const uint8_t *data, size_t len, struct gsf_tags_t *tags)
{
	const uint8_t *program, *t;
	size_t programlen, tlen;
	if (!psf_parse (data, len, &program, &programlen, &t, &tlen))
	{
		gsf_parse_tags (0, 0, tags);
		return 0;
	}
	return gsf_parse_tags (t, tlen, tags);
}

int gsf_tags_offset (const uint8_t *header, size_t len, uint64_t *offset)
{
	if ((len < 16) || memcmp (header, "PSF", 3) || (header[3] != PSF_VERSION_GSF))
	{
		return 0;
	}
	*offset = 16 + (uint64_t)le32 (header + 4) + le32 (header + 8);
	return 1;
}

/* the decompressed program: 12 bytes of header, then the data */
static uint8_t *inflate_program (const uint8_t *src, size_t srclen, size_t *outlen)
{
	z_stream zs;
	size_t size = 64 * 1024, used = 0;
	uint8_t *out = malloc (size);
	int r;
	if (!out)
	{
		return 0;
	}
	memset (&zs, 0, sizeof (zs));
	if (inflateInit (&zs) != Z_OK)
	{
		free (out);
		return 0;
	}
	zs.next_in = (Bytef *)src;
	zs.avail_in = srclen;
	do
	{
		if (used == size)
		{
			uint8_t *t;
			if (size >= MAX_PROGRAM)
			{
				break;
			}
			size = (size * 2 > MAX_PROGRAM) ? MAX_PROGRAM : size * 2;
			t = realloc (out, size);
			if (!t)
			{
				break;
			}
			out = t;
		}
		zs.next_out = out + used;
		zs.avail_out = size - used;
		r = inflate (&zs, Z_NO_FLUSH);
		used = size - zs.avail_out;
	} while (r == Z_OK);
	inflateEnd (&zs);
	if (r != Z_STREAM_END)
	{
		free (out);
		return 0;
	}
	*outlen = used;
	return out;
}

struct loader_t
{
	gsf_readfile_t readfile;
	void *ctx;
	struct gsf_image_t *image;
	int entry_set;
	char *missing;
	size_t missinglen;
	size_t capacity;
};

/* writes one decompressed program into the image */
static int load_program (struct loader_t *l, const uint8_t *p, size_t len)
{
	uint32_t entry, offset, size;
	if (len < 12)
	{
		return GSF_ERR_FORMAT;
	}
	entry = le32 (p);
	offset = le32 (p + 4) & 0x1ffffff; /* the address without the region (0x08 or 0x02) */
	size = le32 (p + 8);
	if (size > len - 12)
	{
		size = len - 12;
	}
	if ((uint64_t)offset + size > MAX_IMAGE)
	{
		return GSF_ERR_FORMAT;
	}
	if (!l->entry_set)
	{
		l->image->entry = entry;
		l->image->multiboot = (entry >> 24) == 0x02;
		l->entry_set = 1;
	}
	if (offset + size > l->capacity)
	{
		size_t c = l->capacity ? l->capacity : 1024 * 1024;
		uint8_t *t;
		while (c < offset + size)
		{
			c *= 2;
		}
		t = realloc (l->image->data, c);
		if (!t)
		{
			return GSF_ERR_MEMORY;
		}
		memset (t + l->capacity, 0, c - l->capacity);
		l->image->data = t;
		l->capacity = c;
	}
	memcpy (l->image->data + offset, p + 12, size);
	if (offset + size > l->image->size)
	{
		l->image->size = offset + size;
	}
	return 0;
}

struct libnames_t
{
	char names[10][128]; /* [0] = _lib, [1] = _lib2, and so on */
};

static void libnames_found (void *ctx, const char *key, const char *value)
{
	struct libnames_t *n = ctx;
	if (!strcmp (key, "_lib"))
	{
		snprintf (n->names[0], sizeof (n->names[0]), "%s", value);
	} else if (!strncmp (key, "_lib", 4) && (key[4] >= '2') && (key[4] <= '9') && !key[5])
	{
		snprintf (n->names[key[4] - '1'], sizeof (n->names[0]), "%s", value);
	}
}

static int load_file (struct loader_t *l, const uint8_t *data, size_t len, int depth);

static int load_library (struct loader_t *l, const char *name, int depth)
{
	size_t len;
	uint8_t *data;
	int r;
	if (depth >= MAX_DEPTH)
	{
		return GSF_ERR_LIBRARY;
	}
	data = l->readfile (l->ctx, name, &len);
	if (!data)
	{
		if (l->missing)
		{
			snprintf (l->missing, l->missinglen, "%s", name);
		}
		return GSF_ERR_LIBRARY;
	}
	r = load_file (l, data, len, depth + 1);
	free (data);
	if (r == GSF_ERR_FORMAT)
	{
		if (l->missing)
		{
			snprintf (l->missing, l->missinglen, "%s", name);
		}
		r = GSF_ERR_LIBRARY;
	}
	return r;
}

static int load_file (struct loader_t *l, const uint8_t *data, size_t len, int depth)
{
	const uint8_t *program, *t;
	size_t programlen, tlen, plen;
	struct libnames_t libs;
	uint8_t *p;
	int r, i;

	if (!psf_parse (data, len, &program, &programlen, &t, &tlen))
	{
		return GSF_ERR_FORMAT;
	}
	memset (&libs, 0, sizeof (libs));
	tags_foreach (t, tlen, libnames_found, &libs);

	if (libs.names[0][0] && ((r = load_library (l, libs.names[0], depth))))
	{
		return r;
	}
	if (programlen)
	{
		if (!(p = inflate_program (program, programlen, &plen)))
		{
			return GSF_ERR_FORMAT;
		}
		r = load_program (l, p, plen);
		free (p);
		if (r)
		{
			return r;
		}
	}
	for (i = 1; i < 10; i++)
	{
		if (libs.names[i][0] && ((r = load_library (l, libs.names[i], depth))))
		{
			return r;
		}
	}
	return 0;
}

int gsf_load (const uint8_t *data, size_t len, gsf_readfile_t readfile, void *ctx, struct gsf_image_t *image, char *missing, size_t missinglen)
{
	struct loader_t l;
	int r;
	memset (image, 0, sizeof (*image));
	memset (&l, 0, sizeof (l));
	l.readfile = readfile;
	l.ctx = ctx;
	l.image = image;
	l.missing = missing;
	l.missinglen = missinglen;
	if (missing && missinglen)
	{
		missing[0] = 0;
	}
	r = load_file (&l, data, len, 0);
	if (!r && !image->size)
	{
		r = GSF_ERR_FORMAT;
	}
	if (r)
	{
		free (image->data);
		memset (image, 0, sizeof (*image));
	}
	return r;
}
