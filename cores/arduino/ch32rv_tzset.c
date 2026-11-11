/* tzset() in place of the C library's: the same reading of TZ as newlib 4.5's
 * tzset_r.c, into the core's own time zone (internal/ch32rv_tz.h), with the
 * fields read here rather than by sscanf, and the TZ last read remembered as a
 * hash rather than a copy on the heap. newlib's pulled in its scanf, strtol and
 * malloc (3 KB on V003) for every localtime(). Also the two changes of a year
 * (newlib's __tzcalc_limits), which localtime_r() and mktime() need. */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "internal/ch32rv_tz.h"

#define TZNAME_MIN 3
#define TZNAME_MAX 10

/* Unset TZ: UTC, named GMT, both rules newlib's default. */
struct ch32rv_tz ch32rv_tz = {
    .north = 1,
    .name = {"GMT", "GMT"},
    .rule = {{'J', 0, 0, 0, 0, 0, 0}, {'J', 0, 0, 0, 0, 0, 0}},
};

const int ch32rv_month_lengths[2][12] = {
    {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
    {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
};

static char tzname_std[TZNAME_MAX + 2];
static char tzname_dst[TZNAME_MAX + 2];
static uint32_t prev_hash;
static bool prev_set;

/* sscanf's "%11[...]%n": up to 11 characters of the set; how many. out is left alone when there are none. */
static int name(const char *s, char *out, bool quoted)
{
    int n = 0;
    for (; n < TZNAME_MAX + 1; n++) {
        const char c = s[n];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (quoted && ((c >= '0' && c <= '9') || c == '-' || c == '+')))) {
            break;
        }
        out[n] = c;
    }
    if (n) {
        out[n] = 0;
    }
    return n;
}

/* strtoul()'s number as an unsigned short, as sscanf's %hu reads it: spaces, a sign, digits; past ULONG_MAX,
 * ULONG_MAX. *end is s when there are no digits. */
static unsigned short number(const char *s, const char **end)
{
    const char *p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
        p++;
    }
    const bool neg = *p == '-';
    if (*p == '-' || *p == '+') {
        p++;
    }
    if (*p < '0' || *p > '9') {
        *end = s;
        return 0;
    }
    unsigned long v = 0;
    bool over = false;
    for (; *p >= '0' && *p <= '9'; p++) {
        const unsigned long next = v * 10u + (unsigned long)(*p - '0');
        over |= next / 10u != v;
        v = next;
    }
    *end = p;
    return (unsigned short)(over ? ~0ul : neg ? 0ul - v : v);
}

/* sscanf's "%hu%n:%hu%n:%hu%n" after `lead` (0 or a character that must come first): how many numbers were
 * read, -1 if the text ended before the first; *len is the length up to the last one read. */
static int hms(const char *s, char lead, unsigned short v[3], int *len)
{
    const char *p = s;
    if (lead) {
        if (!*p) {
            return -1;
        }
        if (*p++ != lead) {
            return 0;
        }
    }
    int count = 0;
    for (;;) {
        const char *q = p;
        while (*q == ' ' || (*q >= '\t' && *q <= '\r')) {
            q++;
        }
        if (!*q) {
            return count ? count : -1;
        }
        const char *end;
        const unsigned short n = number(p, &end);
        if (end == p) {
            return count;
        }
        v[count++] = n;
        p = end;
        *len = (int)(p - s);
        if (count == 3 || *p != ':') {
            return count;
        }
        p++;
    }
}

/* Both rules to newlib's default, {'J', 0, ...}. Field by field: a struct copy may become a memcpy call only
 * after LTO, and pull libc's in beside the core's (ch32rv_string.c). */
static void default_rules(struct ch32rv_tz *tz)
{
    for (int i = 0; i < 2; i++) {
        struct ch32rv_tz_rule *const r = &tz->rule[i];
        r->ch = 'J';
        r->m = r->n = r->d = r->s = 0;
        r->change = 0;
        r->offset = 0;
    }
}

/* The reading itself; ch32rv_tz_read() publishes what it leaves. */
static void read_tz(void)
{
    struct ch32rv_tz *const tz = &ch32rv_tz;
    const char *tzenv = getenv("TZ");

    if (!tzenv) {
        tz->timezone = 0;
        tz->daylight = 0;
        tz->name[0] = "GMT";
        tz->name[1] = "GMT";
        default_rules(tz);
        prev_set = false;
        return;
    }

    uint32_t h = 2166136261u;
    for (const char *p = tzenv; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    if (prev_set && h == prev_hash) {
        return;
    }
    prev_hash = h;
    prev_set = true;

    /* unnamed UTC in case of error */
    tz->timezone = 0;
    tz->daylight = 0;
    tz->name[0] = "";
    tz->name[1] = "";
    default_rules(tz);

    if (*tzenv == ':') {
        ++tzenv;
    }

    /* <MESZ+0330> or MESZ */
    int n;
    if (*tzenv == '<') {
        ++tzenv;
        n = name(tzenv, tzname_std, true);
        if (n < TZNAME_MIN || n > TZNAME_MAX || tzenv[n] != '>') {
            return;
        }
        ++tzenv;
    } else {
        n = name(tzenv, tzname_std, false);
        if (n < TZNAME_MIN || n > TZNAME_MAX) {
            return;
        }
    }
    tzenv += n;

    int sign = 1;
    if (*tzenv == '-') {
        sign = -1;
        ++tzenv;
    } else if (*tzenv == '+') {
        ++tzenv;
    }
    unsigned short t[3] = {0, 0, 0};
    if (hms(tzenv, 0, t, &n) < 1) {
        return;
    }
    const long offset0 = sign * (t[2] + 60L * t[1] + 3600L * t[0]);
    tzenv += n;

    if (*tzenv == '<') {
        ++tzenv;
        const int k = name(tzenv, tzname_dst, true);
        if (k == 0 && tzenv[0] == '>') {
            n = -1; /* no DST */
        } else {
            if (k) {
                n = k;
            } /* else newlib's sscanf left n (the offset's length) and the name as they were: kept alike */
            if (n < TZNAME_MIN || n > TZNAME_MAX || tzenv[n] != '>') {
                return;
            }
            ++tzenv;
        }
    } else {
        n = name(tzenv, tzname_dst, false);
        if (n == 0) {
            n = -1;
        } else if (n < TZNAME_MIN || n > TZNAME_MAX) {
            return;
        }
    }
    if (n < 0) {
        tz->name[0] = tzname_std;
        tz->name[1] = tz->name[0];
        tz->rule[0].offset = offset0;
        tz->timezone = offset0;
        return;
    }
    tzenv += n;

    sign = 1;
    if (*tzenv == '-') {
        sign = -1;
        ++tzenv;
    } else if (*tzenv == '+') {
        ++tzenv;
    }
    t[0] = t[1] = t[2] = 0;
    n = 0;
    const long offset1 = hms(tzenv, 0, t, &n) <= 0 ? offset0 - 3600 : sign * (t[2] + 60L * t[1] + 3600L * t[0]);
    tzenv += n;

    for (int i = 0; i < 2; ++i) {
        if (*tzenv == ',') {
            ++tzenv;
        }
        if (*tzenv == 'M') {
            /* "M%hu%n.%hu%n.%hu%n" */
            unsigned short mwd[3];
            const char *p = tzenv + 1, *end;
            int got = 0;
            while (got < 3) {
                mwd[got] = number(p, &end);
                if (end == p) {
                    break;
                }
                got++;
                p = end;
                if (got < 3 && *p++ != '.') {
                    break;
                }
            }
            if (got != 3 || mwd[0] < 1 || mwd[0] > 12 || mwd[1] < 1 || mwd[1] > 5 || mwd[2] > 6) {
                return;
            }
            tz->rule[i].ch = 'M';
            tz->rule[i].m = mwd[0];
            tz->rule[i].n = mwd[1];
            tz->rule[i].d = mwd[2];
            tzenv = end;
        } else {
            char ch = 'D';
            if (*tzenv == 'J') {
                ch = 'J';
                ++tzenv;
            }
            const char *end;
            const unsigned short d = number(tzenv, &end);
            if (end == tzenv) {
                /* unspecified: the US rules since 2007, M3.2.0 and M11.1.0 */
                tz->rule[i].ch = 'M';
                tz->rule[i].m = i ? 11 : 3;
                tz->rule[i].n = i ? 1 : 2;
                tz->rule[i].d = 0;
            } else {
                tz->rule[i].ch = ch;
                tz->rule[i].d = d;
            }
            tzenv = end;
        }

        /* 02:00:00 unless given */
        t[0] = 2, t[1] = 0, t[2] = 0;
        n = 0;
        if (*tzenv == '/' && hms(tzenv, '/', t, &n) <= 0) {
            default_rules(tz);
            return;
        }
        tz->rule[i].s = t[2] + 60 * t[1] + 3600 * t[0];
        tzenv += n;
    }

    tz->rule[0].offset = offset0;
    tz->rule[1].offset = offset1;
    tz->name[0] = tzname_std;
    tz->name[1] = tzname_dst;
    ch32rv_tz_limits(tz->year);
    tz->timezone = tz->rule[0].offset;
    tz->daylight = tz->rule[0].offset != tz->rule[1].offset;
}

void ch32rv_tz_read(void)
{
    read_tz();
    ch32rv_tz_publish(ch32rv_tz.name[0], ch32rv_tz.name[1], ch32rv_tz.timezone, ch32rv_tz.daylight);
}

/* newlib 4.5's __tzcalc_limits (tzcalc_limits.c): the days from 1970 to the
 * year, then each rule's day in it. */
int ch32rv_tz_limits(int year)
{
    struct ch32rv_tz *const tz = &ch32rv_tz;
    if (year < 1970) {
        return 0;
    }
    tz->year = year;
    const int years = year - 1970;
    const int year_days = years * 365 + (years - 1 + 2) / 4 - (years - 1 + 70) / 100 + (years - 1 + 370) / 400;
    for (int i = 0; i < 2; ++i) {
        struct ch32rv_tz_rule *const r = &tz->rule[i];
        int days;
        if (r->ch == 'J') {
            /* The Julian day n (1 <= n <= 365), as a day of the year from 0. */
            days = year_days + r->d + (CH32RV_ISLEAP(year) && r->d >= 60) - 1;
        } else if (r->ch == 'D') {
            days = year_days + r->d;
        } else {
            const int *const ip = ch32rv_month_lengths[CH32RV_ISLEAP(year)];
            days = year_days;
            int j;
            for (j = 1; j < r->m; ++j) {
                days += ip[j - 1];
            }
            const int m_wday = (4 + days) % 7; /* 1970-01-01 was a Thursday */
            int wday_diff = r->d - m_wday;
            if (wday_diff < 0) {
                wday_diff += 7;
            }
            int m_day = (r->n - 1) * 7 + wday_diff;
            while (m_day >= ip[j - 1]) {
                m_day -= 7;
            }
            days += m_day;
        }
        /* the change in UTC: the local time of day plus the offset before it */
        r->change = (time_t)days * 86400 + r->s + r->offset;
    }
    tz->north = tz->rule[0].change < tz->rule[1].change;
    return 1;
}

/* One thread: no lock. */
__attribute__((used)) void tzset(void)
{
    ch32rv_tz_read();
}
