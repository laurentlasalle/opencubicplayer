#ifndef _GSFVIS_H
#define _GSFVIS_H 1

/* Tracker view of the GSF files that use the sound engine of Nintendo's SDK for
 * the Game Boy Advance, MusicPlayer2000 (MP2K, also called "Sappy"), the engine of
 * most GBA games.
 *
 * A second Game Boy Advance (the "scout") runs the same program a few seconds
 * ahead of what is heard, and the state of the engine is read from its memory
 * after each video frame (59.73 per second):
 *  - one column for each track of the song (the parts of the sequence): the note
 *    of the voice that the track plays (the newest and loudest one for a chord);
 *  - one column for each Game Boy sound channel (square 1, square 2, wave, noise)
 *    used by the engine.
 * The engine is found through the pointer to its state that it keeps at the end
 * of the internal work RAM (0x03007FF0). The programs that use another engine get
 * no tracker view. */

#include <stddef.h>
#include <stdint.h>

struct nsfvis_layout_t;
struct nsfvis_ch_t;
struct gsfvis_t;

#define GSFVIS_MAXTRACKS 16

/* NULL if the program does not use MP2K (or uses it only after the first seconds) */
OCP_INTERNAL struct gsfvis_t *gsfvis_open (const uint8_t *image, size_t size, int multiboot);
OCP_INTERNAL void gsfvis_close (struct gsfvis_t *v);
OCP_INTERNAL void gsfvis_reset (struct gsfvis_t *v); /* back to the start of the song */
OCP_INTERNAL const struct nsfvis_layout_t *gsfvis_layout (struct gsfvis_t *v);
OCP_INTERNAL int gsfvis_tracks (struct gsfvis_t *v);    /* number of track columns (the Game Boy columns follow) */
OCP_INTERNAL uint32_t gsfvis_player (struct gsfvis_t *v); /* address of the music player of the song (MusicPlayerInfo) */

/* runs the program up to time t (seconds since the start); emit() is called for
 * each video frame with its time and decoded columns */
OCP_INTERNAL void gsfvis_run_until (struct gsfvis_t *v, double t, void (*emit)(void *arg, double time, const struct nsfvis_ch_t *ch), void *arg);

/* the engine structures used by gsfplay.c to mute a track */
#define MP2K_PLAYER_TRACKCOUNT 8    /* u8: tracks allocated to the player */
#define MP2K_PLAYER_TRACKS    44    /* u32: MusicPlayerTrack array */
#define MP2K_TRACK_SIZE       80
#define MP2K_TRACK_FLAGS       0    /* u8: 0x80 in use, 0x03 volume to compute again */
#define MP2K_TRACK_VOLX       19    /* u8: volume set by the program (64 = full), multiplies the volume of the sequence */

#endif
