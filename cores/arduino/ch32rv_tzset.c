/* tzset() in place of newlib's: the same reading of TZ (newlib 4.5's
 * tzset_r.c), with the fields read here rather than by sscanf, and the TZ last
 * read remembered as a hash rather than a copy on the heap. newlib's pulled in
 * its scanf, strtol and malloc (3 KB on V003) for every localtime(). */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/_tz_structs.h> /* after time.h: it needs time_t */

#define TZNAME_MIN 3
#define TZNAME_MAX 10

int __tzcalc_limits(int year);

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
static void default_rules(__tzinfo_type *tz)
{
    for (int i = 0; i < 2; i++) {
        __tzrule_type *const r = &tz->__tzrule[i];
        r->ch = 'J';
        r->m = r->n = r->d = r->s = 0;
        r->change = 0;
        r->offset = 0;
    }
}

__attribute__((used)) void _tzset_unlocked(void)
{
    __tzinfo_type *const tz = __gettzinfo();
    const char *tzenv = getenv("TZ");

    if (!tzenv) {
        _timezone = 0;
        _daylight = 0;
        _tzname[0] = "GMT";
        _tzname[1] = "GMT";
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
    _timezone = 0;
    _daylight = 0;
    _tzname[0] = "";
    _tzname[1] = "";
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
        _tzname[0] = tzname_std;
        _tzname[1] = _tzname[0];
        tz->__tzrule[0].offset = offset0;
        _timezone = offset0;
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
            tz->__tzrule[i].ch = 'M';
            tz->__tzrule[i].m = mwd[0];
            tz->__tzrule[i].n = mwd[1];
            tz->__tzrule[i].d = mwd[2];
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
                tz->__tzrule[i].ch = 'M';
                tz->__tzrule[i].m = i ? 11 : 3;
                tz->__tzrule[i].n = i ? 1 : 2;
                tz->__tzrule[i].d = 0;
            } else {
                tz->__tzrule[i].ch = ch;
                tz->__tzrule[i].d = d;
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
        tz->__tzrule[i].s = t[2] + 60 * t[1] + 3600 * t[0];
        tzenv += n;
    }

    tz->__tzrule[0].offset = offset0;
    tz->__tzrule[1].offset = offset1;
    _tzname[0] = tzname_std;
    _tzname[1] = tzname_dst;
    __tzcalc_limits(tz->__tzyear);
    _timezone = tz->__tzrule[0].offset;
    _daylight = tz->__tzrule[0].offset != tz->__tzrule[1].offset;
}

/* newlib's lock is a no-op here: one thread. */
__attribute__((used)) void tzset(void)
{
    _tzset_unlocked();
}
