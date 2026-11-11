/* localtime_r() and localtime(): newlib 4.5's (lcltime_r.c), on the core's
 * gmtime_r() (ch32rv_gmtime.c) and the core's time zone (ch32rv_tzset.c,
 * internal/ch32rv_tz.h) rather than the C library's. */
#include <time.h>

#include "internal/ch32rv_tz.h"

struct tm *localtime_r(const time_t *__restrict tim_p, struct tm *__restrict res)
{
    const struct ch32rv_tz *const tz = &ch32rv_tz;

    res = gmtime_r(tim_p, res);

    const int year = res->tm_year + 1900;
    const int *const ip = ch32rv_month_lengths[CH32RV_ISLEAP(year)];

    ch32rv_tz_read();
    if (tz->daylight) {
        if (year == tz->year || ch32rv_tz_limits(year)) {
            res->tm_isdst = tz->north ? (*tim_p >= tz->rule[0].change && *tim_p < tz->rule[1].change)
                                      : (*tim_p >= tz->rule[0].change || *tim_p < tz->rule[1].change);
        } else {
            res->tm_isdst = -1;
        }
    } else {
        res->tm_isdst = 0;
    }

    long offset = res->tm_isdst == 1 ? tz->rule[1].offset : tz->rule[0].offset;
    const int hours = (int)(offset / 3600);
    offset = offset % 3600;
    const int mins = (int)(offset / 60);
    const int secs = (int)(offset % 60);

    res->tm_sec -= secs;
    res->tm_min -= mins;
    res->tm_hour -= hours;

    if (res->tm_sec >= 60) {
        res->tm_min += 1;
        res->tm_sec -= 60;
    } else if (res->tm_sec < 0) {
        res->tm_min -= 1;
        res->tm_sec += 60;
    }
    if (res->tm_min >= 60) {
        res->tm_hour += 1;
        res->tm_min -= 60;
    } else if (res->tm_min < 0) {
        res->tm_hour -= 1;
        res->tm_min += 60;
    }
    if (res->tm_hour >= 24) {
        ++res->tm_yday;
        ++res->tm_wday;
        if (res->tm_wday > 6) {
            res->tm_wday = 0;
        }
        ++res->tm_mday;
        res->tm_hour -= 24;
        if (res->tm_mday > ip[res->tm_mon]) {
            res->tm_mday -= ip[res->tm_mon];
            res->tm_mon += 1;
            if (res->tm_mon == 12) {
                res->tm_mon = 0;
                res->tm_year += 1;
                res->tm_yday = 0;
            }
        }
    } else if (res->tm_hour < 0) {
        res->tm_yday -= 1;
        res->tm_wday -= 1;
        if (res->tm_wday < 0) {
            res->tm_wday = 6;
        }
        res->tm_mday -= 1;
        res->tm_hour += 24;
        if (res->tm_mday == 0) {
            res->tm_mon -= 1;
            if (res->tm_mon < 0) {
                res->tm_mon = 11;
                res->tm_year -= 1;
                res->tm_yday = 364 + CH32RV_ISLEAP(res->tm_year + 1900);
            }
            res->tm_mday = ip[res->tm_mon];
        }
    }
    return res;
}

struct tm *localtime(const time_t *tim_p)
{
    static struct tm tm;
    return localtime_r(tim_p, &tm);
}
