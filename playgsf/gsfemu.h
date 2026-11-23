#ifndef _GSFEMU_H
#define _GSFEMU_H 1

/* Game Boy Advance emulation for the GSF player, using mGBA by Jeffrey Pfau
 * (https://mgba.io, Mozilla Public License 2.0). Only the sound is produced: no
 * video buffer is given to mGBA, so it draws nothing. */

#include <stddef.h>
#include <stdint.h>

struct gsfemu_t;

#define GSFEMU_CHANNELS 6 /* 4 Game Boy channels (square 1, square 2, wave, noise), Direct Sound A and B */

/* the image is copied */
OCP_INTERNAL struct gsfemu_t *gsfemu_open (const uint8_t *image, size_t size, int multiboot, uint32_t rate);
OCP_INTERNAL void gsfemu_close (struct gsfemu_t *e);

/* starts the program again */
OCP_INTERNAL void gsfemu_restart (struct gsfemu_t *e);

/* renders stereo 16-bit samples (left, right) */
OCP_INTERNAL void gsfemu_render (struct gsfemu_t *e, int16_t *buf, unsigned int frames);

/* speed: 0x100 = normal (tempo and pitch together) */
OCP_INTERNAL void gsfemu_set_speed (struct gsfemu_t *e, unsigned int speed);

OCP_INTERNAL void gsfemu_mute (struct gsfemu_t *e, int channel, int mute);

/* version of mGBA, e.g. "0.10.5" */
OCP_INTERNAL const char *gsfemu_version (void);

#endif
