#ifndef _SIDEND_H
#define _SIDEND_H 1

/* Detection of the end of a SID tune that starts again by itself.
 *
 * A SID file is a C64 program that plays its music forever: many tunes end with a
 * silence, then start again. The detector looks at every video frame of the
 * emulation (1/50 s on PAL, 1/60 s on NTSC): the sound produced, and the control
 * registers of the voices (waveform and gate, registers $04, $0B and $12), which
 * give the rhythm and the instruments of the tune without its pitch.
 *
 * After a silence of at least SIDEND_MIN_SILENCE frames, when the sound comes back,
 * the next SIDEND_COMPARE frames are compared with the start of the tune and with
 * the frames that followed the previous silence. When they are almost the same
 * (SIDEND_SIMILAR percent, the best of a shift of up to SIDEND_SHIFT frames, since
 * the routines of the composers are not locked to the video frame and drift by a
 * frame from one pass to the next), the tune is looping: it ends where the silence
 * started. Otherwise the silence is part of the tune and it goes on.
 *
 * Only the control registers are compared, not the frequencies: some tunes play
 * their first pass differently (e.g. an octave lower) and loop on the following
 * ones; comparing with the previous pass finds those loops one pass later.
 *
 * While it compares, the frames that follow the silence must not be heard yet: the
 * player keeps them aside (SIDEND_HOLD) until the answer is known, then plays them
 * (SIDEND_FLUSH) or drops them (SIDEND_RESTARTED).
 */

#include <stdint.h>

#define SIDEND_MIN_SILENCE  50   /* frames: 1 s at 50 Hz */
#define SIDEND_COMPARE     150   /* frames compared: 3 s at 50 Hz */
#define SIDEND_SHIFT         2   /* frames of drift allowed between two passes */
#define SIDEND_SIMILAR      92   /* percent of equal control registers to call it a loop */

enum sidend_action_t
{
	SIDEND_PLAY,      /* play this frame (nothing is kept aside) */
	SIDEND_HOLD,      /* keep this frame aside, the answer is not known yet */
	SIDEND_FLUSH,     /* not a loop: play the frames kept aside, then this one */
	SIDEND_RESTARTED  /* the tune loops: drop the frames kept aside and this one, the tune is over */
};

#ifdef __cplusplus
extern "C" {
#endif

/* a (sub)tune starts; chips: number of SID chips (1 to 3) */
void sidend_reset (int chips);

/* one frame: its stereo samples (left, right) and the registers of each chip
 * (regs[chip][0x00..0x1f]) at the end of the frame */
enum sidend_action_t sidend_frame (const int16_t *stereo, unsigned int samples, const uint8_t regs[3][0x20]);

#ifdef __cplusplus
}
#endif

#endif
