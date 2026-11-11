/* The C library's public tzname, timezone and daylight, kept equal to the
 * core's time zone (internal/ch32rv_tz.h) after every tzset().
 *
 * Defined here, weak, under the C library's own names: the library's own
 * definitions then stay out of the link unless something of the library
 * itself needs them, and when it does they win over these without a
 * multiple definition; the writes below reach whichever was kept. This file
 * does not include <time.h>, whose declarations differ between libraries
 * (picolibc's tzname is `char *const[2]`, written by its own tzset() only).
 * A library this file does not know gets no public variables: the core's
 * functions do not read them. */
#include <stddef.h>

#include "internal/ch32rv_libc.h"

/* internal/ch32rv_tz.h's, declared here: that header brings <time.h> in. */
void ch32rv_tz_read(void);
void ch32rv_tz_publish(const char *std, const char *dst, long tz, int dl);

#if CH32RV_LIBC_NEWLIB
__attribute__((weak)) char *_tzname[2] = {(char *)"GMT", (char *)"GMT"};
__attribute__((weak)) long _timezone;
__attribute__((weak)) int _daylight;
#define LIBC_TZNAME   _tzname
#define LIBC_TIMEZONE _timezone
#define LIBC_DAYLIGHT _daylight
#elif CH32RV_LIBC_PICOLIBC
__attribute__((weak)) char *tzname[2] = {(char *)"GMT", (char *)"GMT"};
__attribute__((weak)) long timezone;
__attribute__((weak)) int daylight;
#define LIBC_TZNAME   tzname
#define LIBC_TIMEZONE timezone
#define LIBC_DAYLIGHT daylight
#endif

#if CH32RV_LIBC_NEWLIB
/* newlib's own name for tzset() without its lock. Nothing of the core calls it; a newlib function that still
 * does (wcsftime) finds it here rather than bringing in newlib's tzset.o, whose tzset() would clash with the
 * core's. */
__attribute__((used)) void _tzset_unlocked(void)
{
    ch32rv_tz_read();
}
#endif

void ch32rv_tz_publish(const char *std, const char *dst, long tz, int dl)
{
#ifdef LIBC_TZNAME
    LIBC_TZNAME[0] = (char *)std;
    LIBC_TZNAME[1] = (char *)dst;
    LIBC_TIMEZONE = tz;
    LIBC_DAYLIGHT = dl;
#else
    (void)std;
    (void)dst;
    (void)tz;
    (void)dl;
#endif
}
