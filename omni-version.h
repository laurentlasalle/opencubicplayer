#ifndef _OMNI_VERSION_H
#define _OMNI_VERSION_H 1

/* Open Cubic Player "Omni": unofficial build with extra features.
 * The OCP version (configure.ac) stays the upstream one; Omni releases are
 * numbered separately and published as <upstream version>-omni.<n>,
 * e.g. 3.5.0-omni.7. Restart at 1 when moving to a new upstream version. */
#define OMNI_VERSION 7

#define OMNI_STR_(x) #x
#define OMNI_STR(x) OMNI_STR_(x)
#define OMNI_VERSION_STRING OMNI_STR(OMNI_VERSION)

/* Patch byte stored in CPMODNFO.DAT as the "last scan version": files of an
 * unknown type are scanned again when a newer build may support them. The
 * Omni release goes in the low nibble so it increases between Omni releases
 * of the same upstream version. */
#define OCP_SCAN_PATCH_VERSION ((OCP_PATCH_VERSION << 4) | (OMNI_VERSION & 15))

#endif
