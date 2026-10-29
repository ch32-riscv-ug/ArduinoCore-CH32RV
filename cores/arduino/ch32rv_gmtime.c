/* gmtime_r() and gmtime() in place of newlib's, the same results without
 * newlib's 64-bit division (__divdi3 and __moddi3, 1.9 KB on RV32): time_t is
 * 64 bits, and the seconds are split into days on 32-bit words. Days past
 * a long wrap as newlib's do. localtime_r() calls this (ch32rv_localtime.c). */
#include <stdint.h>
#include <time.h>

#define SECSPERDAY            86400L
#define EPOCH_ADJUSTMENT_DAYS 719468L
#define ADJUSTED_EPOCH_WDAY   3
#define DAYS_PER_ERA          146097L
#define DAYS_PER_CENTURY      36524L
#define DAYS_PER_4_YEARS      (3 * 365 + 366)
#define DAYS_PER_YEAR         365
#define DAYS_IN_JANUARY       31
#define DAYS_IN_FEBRUARY      28
#define YEARS_PER_ERA         400
#define isleap(y)             ((((y) % 4) == 0 && ((y) % 100) != 0) || ((y) % 400) == 0)

/* 'used': localtime_r() of libc may come in after LTO and call it. */
__attribute__((used)) struct tm *gmtime_r(const time_t *__restrict tim_p, struct tm *__restrict res)
{
    /* |t| / 86400 as (|t| >> 7) / 675, 16 bits at a time: the low 32 bits of the quotient (newlib keeps
     * those in a long) and the remainder. */
    const time_t t = *tim_p;
    const unsigned long long a = t < 0 ? 0ull - (unsigned long long)t : (unsigned long long)t;
    const uint32_t hi = (uint32_t)(a >> 39), lo = (uint32_t)(a >> 7);
    const uint32_t up = (hi % 675u) << 16 | lo >> 16;
    const uint32_t down = (up % 675u) << 16 | (lo & 0xffffu);
    uint32_t q = (up / 675u) << 16 | down / 675u;
    long rem = (long)((down % 675u) << 7 | ((uint32_t)a & 127u));
    if (t < 0) {
        q = 0u - q;
        rem = -rem;
    }
    if (rem < 0) {
        rem += SECSPERDAY;
        q--;
    }
    const long days = (int32_t)(q + (uint32_t)EPOCH_ADJUSTMENT_DAYS);

    res->tm_hour = (int)(rem / 3600);
    rem %= 3600;
    res->tm_min = (int)(rem / 60);
    res->tm_sec = (int)(rem % 60);

    int weekday = (int)((ADJUSTED_EPOCH_WDAY + days) % 7);
    if (weekday < 0) {
        weekday += 7;
    }
    res->tm_wday = weekday;

    /* newlib's: http://howardhinnant.github.io/date_algorithms.html#civil_from_days */
    const int era = (int)((days >= 0 ? days : days - (DAYS_PER_ERA - 1)) / DAYS_PER_ERA);
    const unsigned long eraday = (unsigned long)(days - era * DAYS_PER_ERA); /* [0, 146096] */
    const unsigned erayear = (unsigned)((eraday - eraday / (DAYS_PER_4_YEARS - 1) + eraday / DAYS_PER_CENTURY -
                                         eraday / (DAYS_PER_ERA - 1)) / 365); /* [0, 399] */
    const unsigned yearday = (unsigned)(eraday - (DAYS_PER_YEAR * erayear + erayear / 4 - erayear / 100)); /* [0, 365] */
    unsigned month = (5 * yearday + 2) / 153;                                                            /* [0, 11] */
    const unsigned day = yearday - (153 * month + 2) / 5 + 1;                                            /* [1, 31] */
    month += month < 10 ? 2 : -10;
    const int year = (int)(erayear + (unsigned)(era * YEARS_PER_ERA) + (month <= 1));

    res->tm_yday = (int)(yearday >= DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY
                             ? yearday - (DAYS_PER_YEAR - DAYS_IN_JANUARY - DAYS_IN_FEBRUARY)
                             : yearday + DAYS_IN_JANUARY + DAYS_IN_FEBRUARY + isleap(erayear));
    res->tm_year = year - 1900;
    res->tm_mon = (int)month;
    res->tm_mday = (int)day;
    res->tm_isdst = 0;
    return res;
}

struct tm *gmtime(const time_t *tim_p)
{
    static struct tm tm;
    return gmtime_r(tim_p, &tm);
}
