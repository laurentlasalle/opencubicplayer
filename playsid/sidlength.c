/* OpenCP Module Player
 * Lengths of the SID tunes from the HVSC song length database (see sidlength.h)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "../config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../types.h"
#include "boot/psetting.h"
#include "cpiface/cpiface.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "md5.inc.c"
#include "sidlength.h"

#define MAX_DATABASE (64 * 1024 * 1024)

static char *database;          /* the text of Songlengths.md5, 0-terminated */
static uint32_t database_ref = DIRDB_NOPARENT; /* the file it was read from (dirdb reference) */

void sidlength_md5 (const uint8_t *data, size_t len, char md5[33])
{
	MD5_CTX ctx;
	MD5Init (&ctx);
	MD5Update (&ctx, (uint8_t *)data, len);
	MD5Final (md5, &ctx);
	md5[32] = 0;
}

/* ---- finding the database ---- */

struct search_t
{
	const struct dirdbAPI_t *dirdb;
	const char *name;
	struct ocpfile_t *file;
	struct ocpdir_t *dir;
};

static void search_file (void *token, struct ocpfile_t *file)
{
	struct search_t *s = token;
	const char *name;
	if (s->file || s->dir)
	{
		return;
	}
	s->dirdb->GetName_internalstr (file->dirdb_ref, &name);
	if (!strcasecmp (name, s->name))
	{
		file->ref (file);
		s->file = file;
	}
}

static void search_dir (void *token, struct ocpdir_t *dir)
{
	struct search_t *s = token;
	const char *name;
	if (s->file || s->dir)
	{
		return;
	}
	s->dirdb->GetName_internalstr (dir->dirdb_ref, &name);
	if (!strcasecmp (name, s->name))
	{
		dir->ref (dir);
		s->dir = dir;
	}
}

/* the entry "name" of dir, without regard to case (the HVSC writes DOCUMENTS and
 * Songlengths.md5, but copies of it do not always keep the case) */
static void find_entry (struct ocpdir_t *dir, const char *name, const struct dirdbAPI_t *dirdb, struct ocpfile_t **file, struct ocpdir_t **subdir)
{
	struct search_t s;
	ocpdirhandle_pt h;
	s.dirdb = dirdb;
	s.name = name;
	s.file = 0;
	s.dir = 0;
	if ((h = dir->readdir_start (dir, search_file, search_dir, &s)))
	{
		while (dir->readdir_iterate (h) && !s.file && !s.dir)
		{
		}
		dir->readdir_cancel (h);
	}
	if (file)
	{
		*file = s.file;
	} else if (s.file)
	{
		s.file->unref (s.file);
	}
	if (subdir)
	{
		*subdir = s.dir;
	} else if (s.dir)
	{
		s.dir->unref (s.dir);
	}
}

/* DOCUMENTS/Songlengths.md5 in the directory of the file or one above it */
static struct ocpfile_t *find_in_collection (struct ocpfilehandle_t *file, const struct dirdbAPI_t *dirdb)
{
	struct ocpdir_t *dir = file->origin ? file->origin->parent : 0;
	while (dir)
	{
		struct ocpdir_t *documents = 0;
		struct ocpfile_t *found = 0;
		find_entry (dir, "DOCUMENTS", dirdb, 0, &documents);
		if (documents)
		{
			find_entry (documents, "Songlengths.md5", dirdb, &found, 0);
			documents->unref (documents);
			if (found)
			{
				return found;
			}
		}
		dir = dir->parent; /* parents are kept alive by their children, no reference needed */
	}
	return 0;
}

static char *read_ocpfile (struct ocpfile_t *f)
{
	struct ocpfilehandle_t *h = f->open (f);
	uint64_t len;
	char *text = 0;
	if (!h)
	{
		return 0;
	}
	len = h->filesize (h);
	if ((len > 0) && (len <= MAX_DATABASE) && (text = malloc (len + 1)))
	{
		h->seek_set (h, 0);
		if (h->read (h, text, len) != (int)len)
		{
			free (text);
			text = 0;
		} else {
			text[len] = 0;
		}
	}
	h->unref (h);
	return text;
}

/* the path given in ocp.ini, read with the C library (as the C64 ROM files are) */
static char *read_path (struct cpifaceSessionAPI_t *cpifaceSession, uint32_t ref)
{
	char *path = 0, *text = 0;
	FILE *f;
	long len;
#ifdef _WIN32
	uint16_t *wpath;
	cpifaceSession->dirdb->GetFullname_malloc (ref, &path, DIRDB_FULLNAME_DRIVE | DIRDB_FULLNAME_BACKSLASH);
	if (!path)
	{
		return 0;
	}
	wpath = cpifaceSession->utf8_to_utf16_LFN (path, 0);
	f = wpath ? _wfopen ((wchar_t *)wpath, L"rb") : 0;
	free (wpath);
#else
	cpifaceSession->dirdb->GetFullname_malloc (ref, &path, DIRDB_FULLNAME_NODRIVE);
	if (!path)
	{
		return 0;
	}
	f = fopen (path, "rb");
#endif
	free (path);
	if (!f)
	{
		return 0;
	}
	if (!fseek (f, 0, SEEK_END) && ((len = ftell (f)) > 0) && (len <= MAX_DATABASE) && !fseek (f, 0, SEEK_SET) && (text = malloc (len + 1)))
	{
		if (fread (text, 1, len, f) != (size_t)len)
		{
			free (text);
			text = 0;
		} else {
			text[len] = 0;
		}
	}
	fclose (f);
	return text;
}

/* makes sure the database for this file is in memory; returns 0 if there is none */
static int load_database (struct cpifaceSessionAPI_t *cpifaceSession, struct ocpfilehandle_t *file)
{
	const struct dirdbAPI_t *dirdb = cpifaceSession->dirdb;
	struct ocpfile_t *f = find_in_collection (file, dirdb);
	if (f)
	{
		if (f->dirdb_ref != database_ref)
		{
			free (database);
			database = read_ocpfile (f);
			database_ref = database ? f->dirdb_ref : DIRDB_NOPARENT;
		}
		f->unref (f);
		return database != 0;
	} else {
		const char *path = cpifaceSession->configAPI->GetProfileString ("libsidplayfp", "songlengths", "");
		uint32_t ref;
		if (!path[0])
		{
			return 0;
		}
#ifdef _WIN32
		ref = dirdb->ResolvePathWithBaseAndRef (cpifaceSession->configAPI->DataHomeDir->dirdb_ref, path, DIRDB_RESOLVE_DRIVE | DIRDB_RESOLVE_TILDE_HOME | DIRDB_RESOLVE_WINDOWS_SLASH, dirdb_use_file);
#else
		ref = dirdb->ResolvePathWithBaseAndRef (cpifaceSession->configAPI->DataHomeDir->dirdb_ref, path, DIRDB_RESOLVE_DRIVE | DIRDB_RESOLVE_TILDE_HOME, dirdb_use_file);
#endif
		if (ref == DIRDB_NOPARENT)
		{
			return 0;
		}
		if (ref != database_ref)
		{
			free (database);
			database = read_path (cpifaceSession, ref);
			database_ref = database ? ref : DIRDB_NOPARENT;
		}
		dirdb->Unref (ref, dirdb_use_file);
		return database != 0;
	}
}

/* "m:ss", "m:ss.mmm" (or "h:mm:ss") in milliseconds, -1 if not valid; *end is set
 * after the text read */
static int32_t parse_length (const char *s, const char **end)
{
	int32_t total = 0, part = 0, ms = 0;
	int digits = 0, msdigits = -1;
	for (; *s; s++)
	{
		if ((*s >= '0') && (*s <= '9'))
		{
			if (msdigits >= 0)
			{
				if (msdigits < 3)
				{
					ms = ms * 10 + (*s - '0');
					msdigits++;
				}
			} else if (part < 100000)
			{
				part = part * 10 + (*s - '0');
				digits++;
			}
		} else if ((*s == ':') && (msdigits < 0))
		{
			total = (total + part) * 60;
			part = 0;
		} else if ((*s == '.') && (msdigits < 0))
		{
			msdigits = 0;
		} else if ((*s == '(') && digits)
		{ /* older databases add flags such as "(G)" after a length: ignored */
			while (*s && (*s != ')') && (*s != ' ') && (*s != '\n'))
			{
				s++;
			}
			if (*s != ')')
			{
				break;
			}
		} else {
			break;
		}
	}
	*end = s;
	if (!digits)
	{
		return -1;
	}
	while ((msdigits >= 0) && (msdigits < 3))
	{
		ms *= 10; /* ".5" is 500 ms */
		msdigits++;
	}
	return (total + part) * 1000 + ms;
}

int32_t sidlength_lookup (struct cpifaceSessionAPI_t *cpifaceSession, struct ocpfilehandle_t *file, const char *md5, int song)
{
	char key[40];
	const char *p;
	int i;
	if ((song < 1) || !load_database (cpifaceSession, file))
	{
		return -1;
	}
	snprintf (key, sizeof (key), "\n%s=", md5);
	p = strstr (database, key);
	if (!p)
	{
		if (strncmp (database, key + 1, 33))
		{ /* not on the first line either */
			return -1;
		}
		p = database + 33;
	} else {
		p += 34;
	}
	for (i = 1; ; i++)
	{
		int32_t length;
		const char *end;
		while ((*p == ' ') || (*p == '\t'))
		{
			p++;
		}
		length = parse_length (p, &end);
		if (length < 0)
		{
			return -1; /* no length for this subsong */
		}
		if (i == song)
		{
			return length;
		}
		p = end;
	}
}

void sidlength_done (void)
{
	free (database);
	database = 0;
	database_ref = DIRDB_NOPARENT;
}
