#ifndef _GSFPLAY_H
#define _GSFPLAY_H 1

#include "gsfload.h"

struct cpifaceSessionAPI_t;
struct ocpfilehandle_t;

struct gsfinfo_t
{
	const struct gsf_tags_t *tags;
	int32_t length_ms;   /* length used for playback, -1 = forever */
	int32_t fade_ms;
	double position_ms;  /* emulated time since the track started */
	size_t imagesize;
	int multiboot;
};

OCP_INTERNAL int gsfOpenPlayer (struct ocpfilehandle_t *file, struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gsfClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gsfIdle (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gsfSetLoop (int loop);
OCP_INTERNAL int gsfIsLooped (void);
OCP_INTERNAL void gsfRestart (void);
OCP_INTERNAL void gsfGetInfo (struct gsfinfo_t *info);
OCP_INTERNAL void gsfSetMute (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int mute);

/* info view (gsfinfo.c) */
OCP_INTERNAL void gsfInfoInit (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gsfInfoDone (struct cpifaceSessionAPI_t *cpifaceSession);

extern OCP_INTERNAL const char *gsf_filename;
extern OCP_INTERNAL const struct cpifaceplayerstruct gsfPlayer;

#endif
