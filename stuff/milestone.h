#ifndef OCP_MILESTONE_H
#define OCP_MILESTONE_H 1

/* Timestamped startup milestones written to stderr (which is redirected to
 * %APPDATA%\OpenCubicPlayer\ocp-log.txt on Windows). stderr is unbuffered,
 * so the last line tells where OCP was if it hangs or crashes. */

#include <stdarg.h>
#include <stdio.h>
#include <time.h>

static inline void ocp_milestone (const char *fmt, ...)
{
	struct timespec ts;
	struct tm tmv;
	va_list ap;

	timespec_get (&ts, TIME_UTC);
#ifdef _WIN32
	localtime_s (&tmv, &ts.tv_sec);
#else
	localtime_r (&ts.tv_sec, &tmv);
#endif
	fprintf (stderr, "[%02d:%02d:%02d.%03ld] ", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, (long)(ts.tv_nsec / 1000000));
	va_start (ap, fmt);
	vfprintf (stderr, fmt, ap);
	va_end (ap);
	fputc ('\n', stderr);
	fflush (stderr);
}

#endif
