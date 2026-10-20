#ifndef _S98LIB_H
#define _S98LIB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct s98lib_info_t
{
	double length;      /* seconds for one pass */
	double looplength;  /* seconds of the looping part, 0 if the song does not loop */
	int    devices;
	char   devicenames[96];
};

struct s98lib_t;

struct s98lib_t *s98lib_open (const uint8_t *data, uint32_t len, uint32_t rate, uint32_t buflen);
void     s98lib_close (struct s98lib_t *s);
uint32_t s98lib_render (struct s98lib_t *s, int16_t *buf, uint32_t frames); /* interleaved stereo, returns frames */
int      s98lib_finished (struct s98lib_t *s); /* end of song + fade + trailing silence reached */
void     s98lib_setloop (struct s98lib_t *s, int loops, int fade_seconds); /* loops = 0: loop forever */
void     s98lib_restart (struct s98lib_t *s);
void     s98lib_setspeed (struct s98lib_t *s, double speed);
void     s98lib_getinfo (struct s98lib_t *s, struct s98lib_info_t *info);
double   s98lib_curtime (struct s98lib_t *s); /* seconds played, loops included */

#ifdef __cplusplus
}
#endif

#endif
