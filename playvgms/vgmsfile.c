/* OpenCP Module Player
 * vgmstream I/O ("streamfile") over OCP files, see vgmsplay.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <libvgmstream.h>
#include <libvgmstream_streamfile.h>
#include "types.h"
#include "filesel/dirdb.h"
#include "filesel/filesystem.h"
#include "vgmsplay.h"

/* One open file. vgmstream reads at any offset, so every read seeks first: two
 * streamfiles on the same OCP handle (vgmstream reopens files freely) stay correct. */
struct vgmsfile_t
{
	struct ocpfilehandle_t *handle;
	const struct dirdbAPI_t *dirdb;
	char name[256];  /* file name only: vgmstream looks at the extension and opens companions next to it */
};

static int vf_read (void *user_data, uint8_t *dst, int64_t offset, int length)
{
	struct vgmsfile_t *f = user_data;
	int r;
	if ((offset < 0) || (length <= 0) || f->handle->seek_set (f->handle, offset))
	{
		return 0;
	}
	r = f->handle->read (f->handle, dst, length);
	return (r > 0) ? r : 0;
}

static int64_t vf_get_size (void *user_data)
{
	struct vgmsfile_t *f = user_data;
	return f->handle->filesize (f->handle);
}

static const char *vf_get_name (void *user_data)
{
	struct vgmsfile_t *f = user_data;
	return f->name;
}

static struct libstreamfile_t *vf_open (void *user_data, const char *filename);

static void vf_close (struct libstreamfile_t *libsf)
{
	struct vgmsfile_t *f = libsf->user_data;
	f->handle->unref (f->handle);
	free (f);
	free (libsf);
}

/* an unbuffered streamfile owning a reference to the handle */
static struct libstreamfile_t *vf_new (struct ocpfilehandle_t *handle, const char *name, const struct dirdbAPI_t *dirdb)
{
	struct libstreamfile_t *sf = calloc (1, sizeof (*sf));
	struct vgmsfile_t *f = calloc (1, sizeof (*f));
	if (!sf || !f)
	{
		free (sf);
		free (f);
		return 0;
	}
	f->handle = handle;
	f->dirdb = dirdb;
	snprintf (f->name, sizeof (f->name), "%s", name);
	handle->ref (handle);
	sf->user_data = f;
	sf->read = vf_read;
	sf->get_size = vf_get_size;
	sf->get_name = vf_get_name;
	sf->open = vf_open;
	sf->close = vf_close;
	return sf;
}

/* ---- companion files ---- */

struct search_t
{
	const struct dirdbAPI_t *dirdb;
	const char *name;
	struct ocpfile_t *found;
};

static void search_file (void *token, struct ocpfile_t *file)
{
	struct search_t *s = token;
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

static void search_dir (void *token, struct ocpdir_t *dir)
{
	(void)token; (void)dir;
}

/* The file "name" next to f (same directory or archive): first as written, then
 * without regard to case, since games and rips do not keep the case of names. */
static struct ocpfile_t *find_sibling (struct vgmsfile_t *f, const char *name)
{
	struct ocpdir_t *dir = f->handle->origin ? f->handle->origin->parent : 0;
	struct ocpfile_t *found;
	struct search_t s;
	ocpdirhandle_pt dh;
	if (!dir)
	{
		return 0;
	}
	if ((found = ocpdir_readdir_file (dir, name, f->dirdb)))
	{
		return found;
	}
	s.dirdb = f->dirdb;
	s.name = name;
	s.found = 0;
	if ((dh = dir->readdir_start (dir, search_file, search_dir, &s)))
	{
		while (dir->readdir_iterate (dh) && !s.found)
		{
		}
		dir->readdir_cancel (dh);
	}
	return s.found;
}

/* vgmstream asks for "filename", a path built from the name of the current file:
 * only its last part is used, looked up next to the current file */
static struct libstreamfile_t *vf_open (void *user_data, const char *filename)
{
	struct vgmsfile_t *f = user_data;
	const char *base = filename, *p;
	struct ocpfile_t *file;
	struct ocpfilehandle_t *handle;
	struct libstreamfile_t *sf;

	for (p = filename; *p; p++)
	{
		if ((*p == '/') || (*p == '\\'))
		{
			base = p + 1;
		}
	}
	if (!*base)
	{
		return 0;
	}
	if (!strcmp (base, f->name))
	{ /* the same file again */
		sf = vf_new (f->handle, f->name, f->dirdb);
	} else {
		if (!(file = find_sibling (f, base)))
		{
			return 0;
		}
		handle = file->open (file);
		file->unref (file);
		if (!handle)
		{
			return 0;
		}
		sf = vf_new (handle, base, f->dirdb);
		handle->unref (handle); /* vf_new holds its own reference */
	}
	return sf; /* vgmstream wraps it in its own buffer (it opens through the buffered streamfile) */
}

OCP_INTERNAL struct libstreamfile_t *vgms_streamfile (struct ocpfilehandle_t *file, const struct dirdbAPI_t *dirdb)
{
	const char *name;
	struct libstreamfile_t *sf;
	dirdb->GetName_internalstr (file->dirdb_ref, &name);
	if (!(sf = vf_new (file, name, dirdb)))
	{
		return 0;
	}
	/* vgmstream reads in small pieces at many offsets: the buffer matters. On failure,
	 * libstreamfile_open_buffered() may already have closed sf, so it is not closed here. */
	return libstreamfile_open_buffered (sf);
}
