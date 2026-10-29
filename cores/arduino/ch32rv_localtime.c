/* localtime_r() and localtime(): newlib's, here so that they call the core's
 * gmtime_r() (ch32rv_gmtime.c) rather than pulling newlib's in after LTO. The
 * time zone is still newlib's: TZ, tzset() and the DST rules. */
#include <time.h>
#include <sys/_tz_structs.h> /* after time.h: it needs time_t */

#define isleap(y) ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

/* newlib's own, from its local.h */
void _tzset_unlocked(void);
int __tzcalc_limits(int year);
void __tz_lock(void);
void __tz_unlock(void);
extern const int __month_lengths[2][12];

struct tm *localtime_r(const time_t *__restrict tim_p, struct tm *__restrict res)
{
    __tzinfo_type *const tz = __gettzinfo();

    res = gmtime_r(tim_p, res);

    const int year = res->tm_year + 1900;
    const int *const ip = __month_lengths[isleap(year)];

    __tz_lock();
    _tzset_unlocked();
    if (_daylight) {
        if (year == tz->__tzyear || __tzcalc_limits(year)) {
            res->tm_isdst = tz->__tznorth ? (*tim_p >= tz->__tzrule[0].change && *tim_p < tz->__tzrule[1].change)
                                          : (*tim_p >= tz->__tzrule[0].change || *tim_p < tz->__tzrule[1].change);
        } else {
            res->tm_isdst = -1;
        }
    } else {
        res->tm_isdst = 0;
    }

    long offset = res->tm_isdst == 1 ? tz->__tzrule[1].offset : tz->__tzrule[0].offset;
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
                res->tm_yday = 364 + isleap(res->tm_year + 1900);
            }
            res->tm_mday = ip[res->tm_mon];
        }
    }
    __tz_unlock();
    return res;
}

struct tm *localtime(const time_t *tim_p)
{
    static struct tm tm;
    return localtime_r(tim_p, &tm);
}
