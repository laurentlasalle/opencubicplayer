#ifndef _S98PLAY_H
#define _S98PLAY_H

struct s98info_t
{
	double length;      /* one pass, seconds */
	double looplength;  /* 0 if no loop */
	double position;    /* seconds played, loops included */
	const char *devices;
};

struct ocpfilehandle_t;
struct cpifaceSessionAPI_t;

OCP_INTERNAL int  s98OpenPlayer (struct ocpfilehandle_t *, struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void s98ClosePlayer (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void s98Idle (struct cpifaceSessionAPI_t *cpifaceSession);
OCP_INTERNAL void s98SetLoop (int s);
OCP_INTERNAL int  s98IsLooped (void);
OCP_INTERNAL void s98Restart (void);
OCP_INTERNAL void s98GetInfo (struct s98info_t *info);
OCP_INTERNAL int  s98ChannelCount (void);
OCP_INTERNAL void s98SetMute (struct cpifaceSessionAPI_t *cpifaceSession, int ch, int mute);

#endif
