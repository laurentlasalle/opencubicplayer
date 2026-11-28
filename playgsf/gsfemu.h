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

/* the scout of the tracker view (gsfvis.c): runs one video frame, without sound */
OCP_INTERNAL void gsfemu_frame (struct gsfemu_t *e);

/* access to the memory of the Game Boy Advance (work RAM), to read the state of
 * the sound engine of the program */
OCP_INTERNAL uint8_t  gsfemu_read8  (struct gsfemu_t *e, uint32_t address);
OCP_INTERNAL uint32_t gsfemu_read32 (struct gsfemu_t *e, uint32_t address);
OCP_INTERNAL void     gsfemu_write8 (struct gsfemu_t *e, uint32_t address, uint8_t value);

/* hook() is called after each video frame rendered by gsfemu_render() */
OCP_INTERNAL void gsfemu_set_frame_hook (struct gsfemu_t *e, void (*hook)(void *arg, struct gsfemu_t *e), void *arg);

/* version of mGBA, e.g. "0.10.5" */
OCP_INTERNAL const char *gsfemu_version (void);

#endif
