#ifndef _VGMSPLAY_H
#define _VGMSPLAY_H 1

#include <stdint.h>

struct cpifaceSessionAPI_t;
struct ocpfilehandle_t;
struct ocpdir_t;
struct dirdbAPI_t;

struct vgmsinfo_t
{
	int subsong;             /* 1..subsongs */
	int subsongs;            /* 1 for files without subsongs */
	int channels;            /* of the file, before any downmix to stereo */
	int rate;                /* sample rate of the file */
	int64_t samples;         /* samples of the stream (one pass, without loops) */
	int64_t play_samples;    /* samples played: loops and fade-out included */
	int64_t position;        /* sample being decoded */
	int loop;                /* the file has loop points */
	int64_t loop_start, loop_end;
	int bitrate;             /* bits per second, 0 if unknown */
	const char *codec;       /* e.g. "PlayStation 4-bit ADPCM" */
	const char *layout;      /* e.g. "interleave" */
	const char *meta;        /* the container format, e.g. "GENH generic header" */
	const char *name;        /* name of the stream inside the file, may be empty */
};

OCP_INTERNAL int vgmsOpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void vgmsClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void vgmsIdle (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void vgmsSetLoop (int loop);
OCP_INTERNAL int vgmsIsLooped (void);
OCP_INTERNAL int vgmsStartSubsong (struct cpifaceSessionAPI_t *cpifaceSession, int subsong); /* 1..subsongs */
OCP_INTERNAL void vgmsGetInfo (struct vgmsinfo_t *info);

/* vgmstream I/O over an OCP file (vgmsfile.c); companion files (headers, other
 * channels) are looked up in the same directory or archive */
struct libstreamfile_t;
OCP_INTERNAL struct libstreamfile_t *vgms_streamfile (struct ocpfilehandle_t *file, const struct dirdbAPI_t *dirdb);

/* info view (vgmsinfo.c) */
OCP_INTERNAL void vgmsInfoInit (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void vgmsInfoDone (struct cpifaceSessionAPI_t *cpifaceSession);

extern OCP_INTERNAL const char *vgms_filename;
extern OCP_INTERNAL const struct cpifaceplayerstruct vgmsPlayer;

#endif
