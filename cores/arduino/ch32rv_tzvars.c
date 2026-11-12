/* The C library's public tzname, timezone and daylight, kept equal to the
 * core's time zone (internal/ch32rv_tz.h) after every tzset().
 *
 * Defined here, weak: picolibc's own definitions then stay out of the link
 * unless something of picolibc itself needs them, and when it does they win
 * over these without a multiple definition; the writes below reach whichever
 * was kept. This file does not include <time.h>, which declares picolibc's
 * tzname `char *const[2]` (written by its own tzset() only). */
#include <stddef.h>

/* internal/ch32rv_tz.h's, declared here: that header brings <time.h> in. */
void ch32rv_tz_publish(const char *std, const char *dst, long tz, int dl);

__attribute__((weak)) char *tzname[2] = {(char *)"GMT", (char *)"GMT"};
__attribute__((weak)) long timezone;
__attribute__((weak)) int daylight;

void ch32rv_tz_publish(const char *std, const char *dst, long tz, int dl)
{
    tzname[0] = (char *)std;
    tzname[1] = (char *)dst;
    timezone = tz;
    daylight = dl;
}
