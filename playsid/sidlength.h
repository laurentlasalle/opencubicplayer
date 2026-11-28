#ifndef _SIDLENGTH_H
#define _SIDLENGTH_H 1

/* Lengths of the SID tunes from the song length database of the High Voltage SID
 * Collection (HVSC), DOCUMENTS/Songlengths.md5: one line per file, "<md5>=<length
 * of subsong 1> <length of subsong 2> ...", with lengths written "m:ss" or
 * "m:ss.mmm". The MD5 is the one of the whole file (HVSC 68 and later), so a file
 * whose header text was edited (title, author or release) is not found.
 *
 * The database is looked for:
 *  - in a DOCUMENTS directory next to the file or above it: the layout of the HVSC
 *    (C64Music/DOCUMENTS/Songlengths.md5), wherever the collection is stored;
 *  - otherwise at the path given by [libsidplayfp] songlengths= in ocp.ini.
 * It is kept in memory between files. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct cpifaceSessionAPI_t;
struct ocpfilehandle_t;

/* MD5 of the file, as written in the database (32 lower case hex digits) */
void sidlength_md5 (const uint8_t *data, size_t len, char md5[33]);

/* length of subsong "song" (1..N) in milliseconds, -1 if not found */
int32_t sidlength_lookup (struct cpifaceSessionAPI_t *cpifaceSession, struct ocpfilehandle_t *file, const char *md5, int song);

void sidlength_done (void); /* frees the database kept in memory */

#ifdef __cplusplus
}
#endif

#endif
