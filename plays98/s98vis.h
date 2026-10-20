#ifndef _S98VIS_H
#define _S98VIS_H 1

#include <stdint.h>

struct nsfvis_layout_t;
struct nsfvis_ch_t;
struct s98vis_t;

struct s98vis_t *s98vis_open (const uint8_t *data, uint32_t len);
void s98vis_close (struct s98vis_t *v);
void s98vis_reset (struct s98vis_t *v); /* back to the start of the song */
const struct nsfvis_layout_t *s98vis_layout (struct s98vis_t *v);

/* reads the song up to time t (seconds, loops included); emit() is called for each
 * frame (every 1/60 s) with its song time and decoded channels */
void s98vis_run_until (struct s98vis_t *v, double t, void (*emit)(void *arg, double time, const struct nsfvis_ch_t *ch), void *arg);

#endif
