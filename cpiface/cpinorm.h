#ifndef _CPINORM_H
#define _CPINORM_H 1

/* Volume normalization (Omni): every file is played at about the same loudness,
 * whatever its format.
 *
 * It works on the sound sent to the player device (plrDevAPI), so it applies to
 * all the playback plugins. The loudness of a file is the mean power of its sound
 * measured in blocks of 400 ms, without the silent blocks and the blocks much
 * quieter than the rest (as the gating of EBU R 128, without its filter).
 *
 * - The loudness of the files already heard is kept in CPLOUD.DAT, in the data
 *   directory of OCP (next to CPMODNFO.DAT): their gain is right from the start.
 * - A file never heard starts at its own volume. Once 3 seconds of sound were
 *   measured, the gain glides to the one measured, in 1.5 seconds, then stays
 *   fixed until the end of the file. When the file stops, after at least 10
 *   seconds of sound, the loudness of everything heard is saved for the next time.
 * - A boost (up to 12 dB) never takes the highest sample measured above full
 *   scale; a cut goes down to 12 dB.
 *
 * ocp.ini, [sound]: normalize=on/off, normalizetarget= (dBFS, -18 by default).
 */

struct plrDevAPI_t;

/* returns the API to give to the playback plugin of the file: the device itself,
 * or the device through the normalization */
const struct plrDevAPI_t *cpiNormalizeWrap (const struct plrDevAPI_t *device);

#endif
