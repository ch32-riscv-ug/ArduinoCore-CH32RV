/* Host driver for tests/unit/test_core_strftime.py: the core's strftime() (cores/arduino/ch32rv_strftime.c)
 * against newlib 4.5's output in the C locale, its tzset() (ch32rv_tzset.c) against what newlib's leaves
 * behind, and its gmtime_r() (ch32rv_gmtime.c) against the host's. The core's are built as core_strftime,
 * core_tzset and core_gmtime_r. The expected values were printed by newlib's strftime.c and tzset_r.c built
 * on the host (with the host's snprintf and sscanf for its sniprintf and siscanf); prints one line per
 * failure. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/_tz_structs.h>

size_t core_strftime(char *, size_t, const char *, const struct tm *);
struct tm *core_gmtime_r(const time_t *, struct tm *);
void core_tzset(void);

static __tzinfo_type tz;
__tzinfo_type *__gettzinfo(void)
{
    return &tz;
}
char *_tzname[2] = {"CET", "CEST"};
long _timezone;
int _daylight;
static int calc_year = -99;
int __tzcalc_limits(int year)
{
    calc_year = year;
    return 1;
}

static int failures, checks;

struct c {
    int y, yd, wd, mon, md, h, mi, s, dst;
};
static const struct c cases[] = {
    {70, 0, 4, 0, 1, 0, 0, 0, 0},                  /* 1970-01-01 Thu */
    {100, 59, 2, 1, 29, 23, 59, 60, 1},            /* 2000-02-29 Tue, a leap second, DST */
    {125, 301, 3, 9, 29, 12, 34, 56, 0},           /* 2025-10-29 Wed */
    {121, 0, 5, 0, 1, 0, 0, 1, -1},                /* 2021-01-01 Fri: ISO week 53 of 2020; DST unknown */
    {119, 364, 2, 11, 31, 11, 0, 0, 0},            /* 2019-12-31 Tue: ISO week 1 of 2020 */
    {116, 2, 0, 0, 3, 13, 5, 9, 0},                /* 2016-01-03 Sun after a leap year */
    {-1900, 0, 6, 0, 1, 1, 2, 3, 0},               /* year 0 */
    {-1901, 364, 5, 11, 31, 1, 2, 3, 0},           /* year -1 */
    {-3901, 100, 1, 3, 10, 1, 2, 3, 0},            /* year -2001 */
    {8100, 200, 3, 6, 19, 1, 2, 3, 0},             /* year 10000 */
    {98099, 10, 4, 0, 11, 1, 2, 3, 1},             /* year 99999 */
    {2147483647, 0, 1, 0, 1, 0, 0, 0, 0},          /* tm_year INT_MAX (no %s: int overflow in its formula) */
    {-2147483647 - 1, 365, 6, 11, 31, 0, 0, 0, 0}, /* INT_MIN */
};
static const char *const fmts[] = {
    "%a %A %b %B %h %c %x %X %r %D %R %T %n%t%%",
    "%C %d %e %F %g %G %H %I %j %k %l %m %M %p %P %q %S %u %U %V %w %W %y %Y",
    "%s %z %Z",
    "%+C %0C %3C %+5C %+Y %0Y %6Y %+6Y %010Y %+G %06G %EY %Od",
    "%+F %010F %+12F %5F %v %+v %012v",
};
static const struct {
    int c, f;
    const char *want;
} golden[] = {
    {0, 0, "Thu Thursday Jan January Jan Thu Jan  1 00:00:00 1970 01/01/70 00:00:00 12:00:00 AM 01/01/70 00:00 00:00:00 \n\t%"},
    {0, 1, "19 01  1 1970-01-01 70 1970 00 12 001  0 12 01 00 AM am 1 00 4 00 01 4 00 70 1970"},
    {0, 2, "-3600 +0100 CET"},
    {0, 3, "19 19 019 00019 1970 1970 001970 001970 0000001970 1970 001970 1970 01"},
    {0, 4, "1970-01-01 1970-01-01 001970-01-01 1970-01-01  1-Jan-1970  1-Jan-1970  1-Jan-001970"},
    {1, 0, "Tue Tuesday Feb February Feb Tue Feb 29 23:59:60 2000 02/29/00 23:59:60 11:59:60 PM 02/29/00 23:59 23:59:60 \n\t%"},
    {1, 1, "20 29 29 2000-02-29 00 2000 23 11 060 23 11 02 59 PM pm 1 60 2 09 09 2 09 00 2000"},
    {1, 2, "951861600 +0200 CEST"},
    {1, 3, "20 20 020 00020 2000 2000 002000 002000 0000002000 2000 002000 2000 29"},
    {1, 4, "2000-02-29 2000-02-29 002000-02-29 2000-02-29 29-Feb-2000 29-Feb-2000 29-Feb-002000"},
    {2, 0, "Wed Wednesday Oct October Oct Wed Oct 29 12:34:56 2025 10/29/25 12:34:56 12:34:56 PM 10/29/25 12:34 12:34:56 \n\t%"},
    {2, 1, "20 29 29 2025-10-29 25 2025 12 12 302 12 12 10 34 PM pm 4 56 3 43 44 3 43 25 2025"},
    {2, 2, "1761737696 +0100 CET"},
    {2, 3, "20 20 020 00020 2025 2025 002025 002025 0000002025 2025 002025 2025 29"},
    {2, 4, "2025-10-29 2025-10-29 002025-10-29 2025-10-29 29-Oct-2025 29-Oct-2025 29-Oct-002025"},
    {3, 0, "Fri Friday Jan January Jan Fri Jan  1 00:00:01 2021 01/01/21 00:00:01 12:00:01 AM 01/01/21 00:00 00:00:01 \n\t%"},
    {3, 1, "20 01  1 2021-01-01 20 2020 00 12 001  0 12 01 00 AM am 1 01 5 00 53 5 00 21 2021"},
    {3, 2, "1609459201  "},
    {3, 3, "20 20 020 00020 2021 2021 002021 002021 0000002021 2020 002020 2021 01"},
    {3, 4, "2021-01-01 2021-01-01 002021-01-01 2021-01-01  1-Jan-2021  1-Jan-2021  1-Jan-002021"},
    {4, 0, "Tue Tuesday Dec December Dec Tue Dec 31 11:00:00 2019 12/31/19 11:00:00 11:00:00 AM 12/31/19 11:00 11:00:00 \n\t%"},
    {4, 1, "20 31 31 2019-12-31 20 2020 11 11 365 11 11 12 00 AM am 4 00 2 52 01 2 52 19 2019"},
    {4, 2, "1577786400 +0100 CET"},
    {4, 3, "20 20 020 00020 2019 2019 002019 002019 0000002019 2020 002020 2019 31"},
    {4, 4, "2019-12-31 2019-12-31 002019-12-31 2019-12-31 31-Dec-2019 31-Dec-2019 31-Dec-002019"},
    {5, 0, "Sun Sunday Jan January Jan Sun Jan  3 13:05:09 2016 01/03/16 13:05:09 01:05:09 PM 01/03/16 13:05 13:05:09 \n\t%"},
    {5, 1, "20 03  3 2016-01-03 15 2015 13 01 003 13  1 01 05 PM pm 1 09 7 01 53 0 00 16 2016"},
    {5, 2, "1451822709 +0100 CET"},
    {5, 3, "20 20 020 00020 2016 2016 002016 002016 0000002016 2015 002015 2016 03"},
    {5, 4, "2016-01-03 2016-01-03 002016-01-03 2016-01-03  3-Jan-2016  3-Jan-2016  3-Jan-002016"},
    {6, 0, "Sat Saturday Jan January Jan Sat Jan  1 01:02:03  01/01/00 01:02:03 01:02:03 AM 01/01/00 01:02 01:02:03 \n\t%"},
    {6, 1, "00 01  1 0000-01-01 01 -1 01 01 001  1  1 01 02 AM am 1 03 6 00 52 6 00 00 "},
    {6, 2, "-62167132677 +0100 CET"},
    {6, 3, "00 00 000 00000   000000 000000 0000000000 -1 -00001  01"},
    {6, 4, "-01-01 0000-01-01 000000-01-01 0000-01-01  1-Jan-0000  1-Jan-  1-Jan-000000"},
    {7, 0, "Fri Friday Dec December Dec Fri Dec 31 01:02:03 -1 12/31/01 01:02:03 01:02:03 AM 12/31/01 01:02 01:02:03 \n\t%"},
    {7, 1, "-0 31 31 -001-12-31 01 -1 01 01 365  1  1 12 02 AM am 4 03 5 52 52 5 52 01 -1"},
    {7, 2, "-62167219077 +0100 CET"},
    {7, 3, "-0 -0 -00 -0000 -1 -1 -00001 -00001 -000000001 -1 -00001 -1 31"},
    {7, 4, "-1-12-31 -001-12-31 -00001-12-31 -001-12-31 31-Dec--001 31-Dec--1 31-Dec--00001"},
    {8, 0, "Mon Monday Apr April Apr Mon Apr 10 01:02:03 -2001 04/10/01 01:02:03 01:02:03 AM 04/10/01 01:02 01:02:03 \n\t%"},
    {8, 1, "-20 10 10 -2001-04-10 01 -2001 01 01 101  1  1 04 02 AM am 2 03 1 15 15 1 15 01 -2001"},
    {8, 2, "-125303932677 +0100 CET"},
    {8, 3, "-20 -20 -20 -0020 -2001 -2001 -02001 -02001 -000002001 -2001 -02001 -2001 10"},
    {8, 4, "-2001-04-10 -2001-04-10 -02001-04-10 -2001-04-10 10-Apr--2001 10-Apr--2001 10-Apr--02001"},
    {9, 0, "Wed Wednesday Jul July Jul Wed Jul 19 01:02:03 10000 07/19/00 01:02:03 01:02:03 AM 07/19/00 01:02 01:02:03 \n\t%"},
    {9, 1, "100 19 19 +10000-07-19 00 10000 01 01 201  1  1 07 02 AM am 3 03 3 29 29 3 29 00 10000"},
    {9, 2, "253419580923 +0100 CET"},
    {9, 3, "+100 100 100 +00100 +10000 10000 010000 +10000 0000010000 +10000 010000 10000 19"},
    {9, 4, "+10000-07-19 10000-07-19 +10000-07-19 +10000-07-19 19-Jul-+10000 19-Jul-+10000 19-Jul-010000"},
    {10, 0, "Thu Thursday Jan January Jan Thu Jan 11 01:02:03 99999 01/11/99 01:02:03 01:02:03 AM 01/11/99 01:02 01:02:03 \n\t%"},
    {10, 1, "999 11 11 +99999-01-11 99 99999 01 01 011  1  1 01 02 AM am 1 03 4 01 02 4 02 99 99999"},
    {10, 2, "3093497305323 +0200 CEST"},
    {10, 3, "+999 999 999 +00999 +99999 99999 099999 +99999 0000099999 +99999 099999 99999 11"},
    {10, 4, "+99999-01-11 99999-01-11 +99999-01-11 +99999-01-11 11-Jan-+99999 11-Jan-+99999 11-Jan-099999"},
    {11, 0, "Mon Monday Jan January Jan Mon Jan  1 00:00:00 2147485547 01/01/47 00:00:00 12:00:00 AM 01/01/47 00:00 00:00:00 \n\t%"},
    {11, 1, "21474855 01  1 +2147485547-01-01 47 2147485547 00 12 001  0 12 01 00 AM am 1 00 1 00 01 1 01 47 2147485547"},
    {11, 3, "+21474855 21474855 21474855 +21474855 +2147485547 2147485547 2147485547 +2147485547 2147485547 +2147485547 2147485547 2147485547 01"},
    {11, 4, "+2147485547-01-01 2147485547-01-01 +2147485547-01-01 +2147485547-01-01  1-Jan-+2147485547  1-Jan-+2147485547  1-Jan-2147485547"},
    {12, 0, "Sat Saturday Dec December Dec Sat Dec 31 00:00:00 -2147481748 12/31/48 00:00:00 12:00:00 AM 12/31/48 00:00 00:00:00 \n\t%"},
    {12, 1, "-21474817 31 31 -2147481748-12-31 48 -2147481748 00 12 366  0 12 12 00 AM am 4 00 6 52 52 6 52 48 -2147481748"},
    {12, 3, "-21474817 -21474817 -21474817 -21474817 -2147481748 -2147481748 -2147481748 -2147481748 -2147481748 -2147481748 -2147481748 -2147481748 31"},
    {12, 4, "-2147481748-12-31 -2147481748-12-31 -2147481748-12-31 -2147481748-12-31 31-Dec--2147481748 31-Dec--2147481748 31-Dec--2147481748"},
};

/* TZ, then what tzset() leaves: the two names, _timezone, _daylight, the year __tzcalc_limits() was called
 * with (-99: not called) and the two rules (ch, m, n, d, s, offset). In this order: on some malformed TZ
 * newlib keeps a name from the TZ before, and so does the core. */
static const struct {
    const char *tz, *std, *dst;
    long timezone;
    int daylight, calc;
    struct {
        char ch;
        int m, n, d, s;
        long offset;
    } rule[2];
} tzs[] = {
    {"JST-9", "JST", "JST", -32400, 0, -99, {{'J', 0, 0, 0, 0, -32400}, {'J', 0, 0, 0, 0, 0}}},
    {"UTC0", "UTC", "UTC", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"GMT0", "GMT", "GMT", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"EST5EDT", "EST", "EDT", 18000, 1, 2025, {{'M', 3, 2, 0, 7200, 18000}, {'M', 11, 1, 0, 7200, 14400}}},
    {"CET-1CEST,M3.5.0,M10.5.0/3", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 5, 0, 7200, -3600}, {'M', 10, 5, 0, 10800, -7200}}},
    {"AEST-10AEDT,M10.1.0,M4.1.0/3", "AEST", "AEDT", -36000, 1, 2025, {{'M', 10, 1, 0, 7200, -36000}, {'M', 4, 1, 0, 10800, -39600}}},
    {"PST8PDT,M3.2.0,M11.1.0", "PST", "PDT", 28800, 1, 2025, {{'M', 3, 2, 0, 7200, 28800}, {'M', 11, 1, 0, 7200, 25200}}},
    {"IST-5:30", "IST", "IST", -19800, 0, -99, {{'J', 0, 0, 0, 0, -19800}, {'J', 0, 0, 0, 0, 0}}},
    {"<+0530>-5:30", "+0530", "+0530", -19800, 0, -99, {{'J', 0, 0, 0, 0, -19800}, {'J', 0, 0, 0, 0, 0}}},
    {"<-03>3", "-03", "-03", 10800, 0, -99, {{'J', 0, 0, 0, 0, 10800}, {'J', 0, 0, 0, 0, 0}}},
    {"<+1030>-10:30<+11>-11,M10.1.0,M4.1.0", "+1030", "+11", -37800, 1, 2025, {{'M', 10, 1, 0, 7200, -37800}, {'M', 4, 1, 0, 7200, -39600}}},
    {"NZST-12NZDT,M9.5.0,M4.1.0/3", "NZST", "NZDT", -43200, 1, 2025, {{'M', 9, 5, 0, 7200, -43200}, {'M', 4, 1, 0, 10800, -46800}}},
    {"EST5EDT4,J60/1:30,J300/2:15:30", "EST", "EDT", 18000, 1, 2025, {{'J', 0, 0, 60, 5400, 18000}, {'J', 0, 0, 300, 8130, 14400}}},
    {"XXX3YYY,100,200/0", "XXX", "YYY", 10800, 1, 2025, {{'D', 0, 0, 100, 7200, 10800}, {'D', 0, 0, 200, 0, 7200}}},
    {":CET-1CEST,M3.5.0,M10.5.0/3", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 5, 0, 7200, -3600}, {'M', 10, 5, 0, 10800, -7200}}},
    {"AB3", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"ABCDEFGHIJK0", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"<AB>3", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"<ABCDEFGHIJ>1", "ABCDEFGHIJ", "ABCDEFGHIJ", 3600, 0, -99, {{'J', 0, 0, 0, 0, 3600}, {'J', 0, 0, 0, 0, 0}}},
    {"JST", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"JST-", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"JST-9:", "JST", "JST", -32400, 0, -99, {{'J', 0, 0, 0, 0, -32400}, {'J', 0, 0, 0, 0, 0}}},
    {"JST-9::", "JST", "JST", -32400, 0, -99, {{'J', 0, 0, 0, 0, -32400}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,M13.1.0,M10.5.0", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,M3.6.0,M10.5.0", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,M3.5.7,M10.5.0", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,M3.5,M10.5.0", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,M3.5.0/,M10.5.0", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,,", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 2, 0, 7200, -3600}, {'M', 11, 1, 0, 7200, -7200}}},
    {"CET-1CEST", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 2, 0, 7200, -3600}, {'M', 11, 1, 0, 7200, -7200}}},
    {"CET-1CEST-2", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 2, 0, 7200, -3600}, {'M', 11, 1, 0, 7200, -7200}}},
    {"CET-1<>", "CET", "CET", -3600, 0, -99, {{'J', 0, 0, 0, 0, -3600}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1<CEST>-2,M3.5.0,M10.5.0/3", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 5, 0, 7200, -3600}, {'M', 10, 5, 0, 10800, -7200}}},
    {"CET-1<!x>", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"UTC70000<:0530>,J1/0,M13.1.0", "", "", 0, 0, -99, {{'J', 0, 0, 1, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"A 3", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET 1", "CET", "CET", 3600, 0, -99, {{'J', 0, 0, 0, 0, 3600}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,J-5,J99999999999999999999", "CET", "CEST", -3600, 1, 2025, {{'J', 0, 0, 65531, 7200, -3600}, {'J', 0, 0, 65535, 7200, -7200}}},
    {"CET-1CEST, 60,M 3. 5.0", "CET", "CEST", -3600, 1, 2025, {{'D', 0, 0, 60, 7200, -3600}, {'M', 3, 5, 0, 7200, -7200}}},
    {"CET99999", "CET", "CET", 124066800, 0, -99, {{'J', 0, 0, 0, 0, 124066800}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1:2:3:4", "CET", "CET", -3723, 0, -99, {{'J', 0, 0, 0, 0, -3723}, {'J', 0, 0, 0, 0, 0}}},
    {"", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {":", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"<", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"<>", "", "", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
    {"CET-1CEST,M3.5.0/25:00:00,M10.5.0/-1", "CET", "CEST", -3600, 1, 2025, {{'M', 3, 5, 0, 90000, -3600}, {'M', 10, 5, 0, 235926000, -7200}}},
    /* unset */ {0, "GMT", "GMT", 0, 0, -99, {{'J', 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0}}},
};

static struct tm tm_of(const struct c *c)
{
    struct tm t;
    memset(&t, 0, sizeof t);
    t.tm_year = c->y, t.tm_yday = c->yd, t.tm_wday = c->wd, t.tm_mon = c->mon, t.tm_mday = c->md;
    t.tm_hour = c->h, t.tm_min = c->mi, t.tm_sec = c->s, t.tm_isdst = c->dst;
    return t;
}

static void check_size(const char *fmt, const struct tm *t, size_t max, size_t want)
{
    char b[64];
    checks++;
    const size_t n = core_strftime(b, max, fmt, t);
    if (n != want || (n && strlen(b) != n)) {
        printf("FAIL strftime(%zu, \"%s\") returned %zu, want %zu\n", max, fmt, n, want);
        failures++;
    }
}

int main(void)
{
    tz.__tzrule[0].offset = -3600; /* CET: 1 hour east, in TZ's sign */
    tz.__tzrule[1].offset = -7200;
    for (size_t i = 0; i < sizeof golden / sizeof golden[0]; i++) {
        const struct tm t = tm_of(&cases[golden[i].c]);
        char b[256];
        checks++;
        const size_t n = core_strftime(b, sizeof b, fmts[golden[i].f], &t);
        if (n != strlen(golden[i].want) || strcmp(b, golden[i].want)) {
            printf("FAIL case %d \"%s\": got %zu [%s] want [%s]\n", golden[i].c, fmts[golden[i].f], n, b,
                   golden[i].want);
            failures++;
        }
    }

    /* The whole result and its NUL must fit, or 0; an unknown conversion or a lone % at the end is 0. */
    const struct tm t = tm_of(&cases[2]);
    check_size("%F %T", &t, 20, 19);
    check_size("%F %T", &t, 19, 0);
    check_size("%c", &t, 25, 24);
    check_size("%c", &t, 24, 0);
    check_size("ab", &t, 3, 2);
    check_size("ab", &t, 2, 0);
    check_size("", &t, 1, 0);
    check_size("x", &t, 0, 0);
    check_size("%F%Q", &t, 64, 0);
    check_size("%F%", &t, 64, 0);

    /* gmtime_r() against the host's while the day count (from year 0, March) fits in 32 bits: past that it wraps in a
     * 32-bit long, newlib's as well as the core's, and the host's does not. */
    struct {
        time_t t;
    } edges[] = {{0}, {-1}, {86399}, {86400}, {-86400}, {-86401}, {951782400}, {4107542400}, {-62167219200},
                 {-62167219201}, {253402300799}, {253402300800}, {185480425151999}, {-185604749222400}};
    uint64_t x = 88172645463325252ull;
    for (long i = 0; i < 3000000; i++) {
        time_t v;
        if (i < (long)(sizeof edges / sizeof edges[0])) {
            v = edges[i].t;
        } else {
            x ^= x << 13, x ^= x >> 7, x ^= x << 17;
            v = (time_t)((int64_t)x >> (17 + i % 47));
        }
        struct tm want, got;
        if (!gmtime_r(&v, &want)) {
            continue;
        }
        memset(&got, 0x55, sizeof got);
        checks++;
        if (core_gmtime_r(&v, &got) != &got || got.tm_year != want.tm_year || got.tm_mon != want.tm_mon ||
            got.tm_mday != want.tm_mday || got.tm_hour != want.tm_hour || got.tm_min != want.tm_min ||
            got.tm_sec != want.tm_sec || got.tm_wday != want.tm_wday || got.tm_yday != want.tm_yday ||
            got.tm_isdst != 0) {
            if (failures++ < 20) {
                printf("FAIL gmtime_r(%lld): got %d-%d-%d %d:%d:%d wd %d yd %d, want %d-%d-%d %d:%d:%d wd %d yd %d\n",
                       (long long)v, got.tm_year, got.tm_mon, got.tm_mday, got.tm_hour, got.tm_min, got.tm_sec,
                       got.tm_wday, got.tm_yday, want.tm_year, want.tm_mon, want.tm_mday, want.tm_hour, want.tm_min,
                       want.tm_sec, want.tm_wday, want.tm_yday);
            }
        }
    }

    for (size_t i = 0; i < sizeof tzs / sizeof tzs[0]; i++) {
        if (tzs[i].tz) {
            setenv("TZ", tzs[i].tz, 1);
        } else {
            unsetenv("TZ");
        }
        calc_year = -99;
        tz.__tzyear = 2025;
        core_tzset();
        int same = !strcmp(_tzname[0], tzs[i].std) && !strcmp(_tzname[1], tzs[i].dst) &&
                   _timezone == tzs[i].timezone && _daylight == tzs[i].daylight && calc_year == tzs[i].calc;
        for (int j = 0; j < 2; j++) {
            const __tzrule_type *r = &tz.__tzrule[j];
            same &= r->ch == tzs[i].rule[j].ch && r->m == tzs[i].rule[j].m && r->n == tzs[i].rule[j].n &&
                    r->d == tzs[i].rule[j].d && r->s == tzs[i].rule[j].s && r->offset == tzs[i].rule[j].offset;
        }
        checks++;
        if (!same) {
            printf("FAIL tzset(\"%s\"): %s/%s %ld %d calc %d, rules %c%d.%d.%d/%d@%ld %c%d.%d.%d/%d@%ld\n",
                   tzs[i].tz ? tzs[i].tz : "(unset)", _tzname[0], _tzname[1], _timezone, _daylight, calc_year,
                   tz.__tzrule[0].ch, tz.__tzrule[0].m, tz.__tzrule[0].n, tz.__tzrule[0].d, tz.__tzrule[0].s,
                   tz.__tzrule[0].offset, tz.__tzrule[1].ch, tz.__tzrule[1].m, tz.__tzrule[1].n, tz.__tzrule[1].d,
                   tz.__tzrule[1].s, tz.__tzrule[1].offset);
            failures++;
        }
    }

    printf("checks %d failures %d\n", checks, failures);
    return failures != 0;
}
