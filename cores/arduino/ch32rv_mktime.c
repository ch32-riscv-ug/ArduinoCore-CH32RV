/* mktime() in place of the C library's: newlib 4.5's (mktime.c, G. Haley;
 * tm_isdst handling by Craig Howland) on the core's time zone (ch32rv_tzset.c,
 * internal/ch32rv_tz.h), so that a local time converts with the TZ that
 * localtime_r() uses whichever C library is linked. div() is written out as
 * / and % (the same truncation), so libc's does not come in with it. */
#include <time.h>

#include "internal/ch32rv_tz.h"

#define SEC_IN_MINUTE 60L
#define SEC_IN_HOUR   3600L
#define SEC_IN_DAY    86400L

static const int days_before_month[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};

/* tm_year counts from 1900 */
#define ISLEAP_TM(y)     (((y) % 4) == 0 && (((y) % 100) != 0 || (((y) + 1900) % 400) == 0))
#define DAYS_IN_YEAR(y)  (ISLEAP_TM(y) ? 366 : 365)

static int days_in_month(int mon, int days_in_feb)
{
    return mon == 1 ? days_in_feb : ch32rv_month_lengths[0][mon];
}

/* q and r of v / d with r carried into the range [0, d): newlib's div() and its fix-up. */
static void carry(int *v, int d, int *next)
{
    if (*v < 0 || *v > d - 1) {
        *next += *v / d;
        if ((*v %= d) < 0) {
            *v += d;
            --*next;
        }
    }
}

static void validate_structure(struct tm *tim_p)
{
    carry(&tim_p->tm_sec, 60, &tim_p->tm_min);
    carry(&tim_p->tm_min, 60, &tim_p->tm_hour);
    carry(&tim_p->tm_hour, 24, &tim_p->tm_mday);
    carry(&tim_p->tm_mon, 12, &tim_p->tm_year);

    int days_in_feb = DAYS_IN_YEAR(tim_p->tm_year) == 366 ? 29 : 28;
    if (tim_p->tm_mday <= 0) {
        while (tim_p->tm_mday <= 0) {
            if (--tim_p->tm_mon == -1) {
                tim_p->tm_year--;
                tim_p->tm_mon = 11;
                days_in_feb = DAYS_IN_YEAR(tim_p->tm_year) == 366 ? 29 : 28;
            }
            tim_p->tm_mday += days_in_month(tim_p->tm_mon, days_in_feb);
        }
    } else {
        while (tim_p->tm_mday > days_in_month(tim_p->tm_mon, days_in_feb)) {
            tim_p->tm_mday -= days_in_month(tim_p->tm_mon, days_in_feb);
            if (++tim_p->tm_mon == 12) {
                tim_p->tm_year++;
                tim_p->tm_mon = 0;
                days_in_feb = DAYS_IN_YEAR(tim_p->tm_year) == 366 ? 29 : 28;
            }
        }
    }
}

time_t mktime(struct tm *tim_p)
{
    const struct ch32rv_tz *const tz = &ch32rv_tz;
    time_t tim = 0;
    long days = 0;
    int year, isdst = 0;

    validate_structure(tim_p);

    /* hours, minutes, seconds */
    tim += tim_p->tm_sec + (tim_p->tm_min * SEC_IN_MINUTE) + (tim_p->tm_hour * SEC_IN_HOUR);

    /* days in the year */
    days += tim_p->tm_mday - 1;
    days += days_before_month[tim_p->tm_mon];
    if (tim_p->tm_mon > 1 && DAYS_IN_YEAR(tim_p->tm_year) == 366) {
        days++;
    }
    tim_p->tm_yday = (int)days;

    if (tim_p->tm_year > 10000 || tim_p->tm_year < -10000) {
        return (time_t)-1;
    }

    /* days in the other years */
    if ((year = tim_p->tm_year) > 70) {
        for (year = 70; year < tim_p->tm_year; year++) {
            days += DAYS_IN_YEAR(year);
        }
    } else if (year < 70) {
        for (year = 69; year > tim_p->tm_year; year--) {
            days -= DAYS_IN_YEAR(year);
        }
        days -= DAYS_IN_YEAR(year);
    }

    tim += (time_t)days * SEC_IN_DAY;

    ch32rv_tz_read();

    if (tz->daylight) {
        const int y = tim_p->tm_year + 1900;
        /* a positive tm_isdst as 1 */
        const int tm_isdst = tim_p->tm_isdst > 0 ? 1 : tim_p->tm_isdst;
        isdst = tm_isdst;

        if (y == tz->year || ch32rv_tz_limits(y)) {
            /* the start of DST in DST local time, the start of standard time in both */
            const time_t startdst_dst = tz->rule[0].change - (time_t)tz->rule[1].offset;
            const time_t startstd_dst = tz->rule[1].change - (time_t)tz->rule[1].offset;
            const time_t startstd_std = tz->rule[1].change - (time_t)tz->rule[0].offset;
            /* in the overlap of the two local times the caller decides (or leaves -1) */
            if (!(tim >= startstd_std && tim < startstd_dst)) {
                isdst = tz->north ? (tim >= startdst_dst && tim < startstd_std)
                                  : (tim >= startdst_dst || tim < startstd_std);
                /* the caller said which and was wrong: corrected, unless it asked mktime() to decide */
                if (tm_isdst >= 0 && (isdst ^ tm_isdst) == 1) {
                    int diff = (int)(tz->rule[0].offset - tz->rule[1].offset);
                    if (!isdst) {
                        diff = -diff;
                    }
                    tim_p->tm_sec += diff;
                    tim += diff;
                    int mday = tim_p->tm_mday;
                    validate_structure(tim_p);
                    mday = tim_p->tm_mday - mday;
                    if (mday) { /* rolled over */
                        if (mday > 1) {
                            mday = -1;
                        } else if (mday < -1) {
                            mday = 1;
                        }
                        days += mday;
                        if ((tim_p->tm_yday += mday) < 0) {
                            --year;
                            tim_p->tm_yday = DAYS_IN_YEAR(year) - 1;
                        } else {
                            mday = DAYS_IN_YEAR(year);
                            if (tim_p->tm_yday > (mday - 1)) {
                                tim_p->tm_yday -= mday;
                            }
                        }
                    }
                }
            }
        }
    }

    /* to UTC */
    tim += (time_t)(isdst == 1 ? tz->rule[1].offset : tz->rule[0].offset);

    tim_p->tm_isdst = isdst;

    if ((tim_p->tm_wday = (int)((days + 4) % 7)) < 0) {
        tim_p->tm_wday += 7;
    }
    return tim;
}
