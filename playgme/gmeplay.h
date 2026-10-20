#ifndef _GMEPLAY_H
#define _GMEPLAY_H

struct moduletype;
struct gmeinfo
{
	int track;
	int numtracks;
	const char* system;
	const char* game;
	const char* song;
	const char* author;
	const char* copyright;
	const char* comment;
	const char* dumper;
        int length;
	int introlength;
	int looplength;
	int playlength;
};


OCP_INTERNAL extern struct moduletype gme_mt;
OCP_INTERNAL extern const char *gme_filename;

struct ocpfilehandle_t;
struct cpifaceSessionAPI_t;

OCP_INTERNAL int gmeOpenPlayer (struct ocpfilehandle_t *, struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gmeClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gmeIdle (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gmeSetLoop (int s);
OCP_INTERNAL int gmeIsLooped (void);
OCP_INTERNAL void gmeGetInfo (struct gmeinfo *);
OCP_INTERNAL void gmeStartSong (struct cpifaceSessionAPI_t *cpifaceSession, int song);

OCP_INTERNAL int gmeChannelCount (void);
OCP_INTERNAL void gmeSetMute (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int mute);

/* NSF tracker view (frames of chip state, see nsfvis.h) */
struct nsfvis_layout_t;
struct nsfvis_ch_t;
OCP_INTERNAL int gmeVisActive (void);
OCP_INTERNAL const struct nsfvis_layout_t *gmeVisLayout (void);
OCP_INTERNAL int gmeVisRowFrames (void);
OCP_INTERNAL int gmeVisTakeNewData (void);
OCP_INTERNAL long gmeVisCurrentFrame (void);
OCP_INTERNAL long gmeVisLastFrame (void);
OCP_INTERNAL long gmeVisFrameSample (long frame);
OCP_INTERNAL const struct nsfvis_ch_t *gmeVisFrame (long frame);

OCP_INTERNAL extern const struct cpitrakdisplaystruct gmeTrakDisplay;
#define GME_TRAK_PATTERNS 4096

OCP_INTERNAL void gmeInfoInit (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void gmeInfoDone (struct cpifaceSessionAPI_t *cpifaceSession);

#endif
