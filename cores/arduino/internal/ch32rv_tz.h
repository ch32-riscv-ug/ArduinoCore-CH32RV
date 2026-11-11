/* The time zone, kept by the core rather than by the C library: tzset()
 * (ch32rv_tzset.c) reads TZ into it, and localtime_r(), mktime() and
 * strftime()'s %z %Z %s read it (ch32rv_localtime.c, ch32rv_mktime.c,
 * ch32rv_strftime.c). The time functions then behave the same whichever C
 * library is linked, and none of them reaches into that library's internals.
 * The readings and the arithmetic are newlib 4.5's (tzset_r.c,
 * tzcalc_limits.c, lcltime_r.c, mktime.c), so the results are what they were
 * when newlib's own state was used. libc's public tzname, timezone and
 * daylight follow every tzset() (ch32rv_tzvars.c). */
#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One of TZ's two rules: when the change happens and the offset after it. */
struct ch32rv_tz_rule {
    char ch;       /* 'J' (Julian day, no Feb 29), 'D' (day of year from 0) or 'M' (month.week.day) */
    int m, n, d;   /* 'M': month 1-12, week 1-5 (5: the last), weekday 0-6; 'J' / 'D': the day */
    int s;         /* the time of day of the change, in seconds */
    time_t change; /* the change in `year`, UTC */
    long offset;   /* seconds west of UTC, TZ's sign */
};

struct ch32rv_tz {
    int north; /* the rule 0 change comes before the rule 1 change in the year */
    int year;  /* the year `change` is for */
    long timezone;
    int daylight;
    const char *name[2];
    struct ch32rv_tz_rule rule[2]; /* 0: standard time, 1: daylight saving time */
};

extern struct ch32rv_tz ch32rv_tz;

/* newlib's __month_lengths: [leap year][month]. */
extern const int ch32rv_month_lengths[2][12];

/* tzset() without anything else: reads TZ when it changed. */
void ch32rv_tz_read(void);

/* The two changes of `year` (from 1970), into ch32rv_tz.rule[].change; 0 before 1970. */
int ch32rv_tz_limits(int year);

/* libc's tzname, timezone and daylight set to these (ch32rv_tzvars.c, which
 * declares it itself: it must not see <time.h>). */
void ch32rv_tz_publish(const char *std, const char *dst, long timezone, int daylight);

#define CH32RV_ISLEAP(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

#ifdef __cplusplus
}
#endif
