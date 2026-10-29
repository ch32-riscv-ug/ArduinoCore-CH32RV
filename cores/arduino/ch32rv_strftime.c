/* strftime() on the core's formatter, in place of newlib-nano's: the same
 * conversions and the same text in the C locale (newlib 4.5's, including its
 * '+' / '0' and width on %C %F %G %Y %v, and %k %l %P %q %v), without newlib's
 * printf, the heap or TZ parsing coming in with it. %s %z %Z read the time zone
 * as it was last set (tzset(), configTzTime(), localtime_r()) and do not call
 * tzset() themselves: TZ changed with setenv() alone is seen after the next
 * tzset(). The newlib entries of the Floating Point menu keep newlib's. */
#include "ch32rv_format.h"

#if !CH32RV_PRINTF_NEWLIB

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/_tz_structs.h> /* after time.h: it needs time_t */

#define YEAR_BASE 1900

struct st {
    char *s;
    size_t max, n;
    const struct tm *t;
};

static void to_st(void *ctx, const char *p, size_t k)
{
    struct st *st = (struct st *)ctx;
    while (k--) {
        if (st->n + 1 < st->max) {
            st->s[st->n] = *p;
        }
        st->n++;
        p++;
    }
}

static void out(struct st *st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    ch32rv_vformat(to_st, st, fmt, ap);
    va_end(ap);
}

/* The i-th of the NUL-separated names, "?" out of range. */
static const char *name(const char *list, int i, int count)
{
    if ((unsigned)i >= (unsigned)count) {
        return "?";
    }
    while (i--) {
        list += strlen(list) + 1;
    }
    return list;
}

static const char days[] = "Sunday\0Monday\0Tuesday\0Wednesday\0Thursday\0Friday\0Saturday";
static const char months[] = "January\0February\0March\0April\0May\0June\0July\0August\0September\0October\0"
                             "November\0December";

static bool leap(int y)
{
    return !(y % 4) && ((y % 100) || !(y % 400));
}

/* newlib's: -1 if the day belongs to the last ISO week of the year before, 1 to the first of the next. */
static int iso_year_adjust(const struct tm *t)
{
    const int lp = leap(t->tm_year + (YEAR_BASE - (t->tm_year < 0 ? 0 : 2000)));
    const int yd = t->tm_yday, wd = t->tm_wday;
    if (yd <= 2 && (wd == 0 || (wd == 6 && yd <= 1) || (wd == 5 && yd == 0))) {
        return -1;
    }
    const int left = 364 + lp - yd; /* days to Dec 31 */
    if (left >= 0 && left <= 2 && wd >= 1 && wd <= 3 - left) {
        return 1;
    }
    return 0;
}

/* The two-digit year of %y %D %g. */
static int year2(const struct tm *t)
{
    return t->tm_year >= 0 ? t->tm_year % 100 : abs(t->tm_year + YEAR_BASE) % 100;
}

/* %Y %G: at least `width` digits; '+' before 5 or more digits under the '+' flag. */
static void year(struct st *st, char pad, unsigned long width, unsigned v, bool sign)
{
    const char *pre = "-";
    if (!sign) {
        pre = "";
        if (pad == '+' && v >= 10000) {
            pre = "+";
            sign = true;
        }
    }
    if (width && sign) {
        width--;
    }
    out(st, "%s%.*u", pre, (int)width, v);
}

static void full_year(struct st *st, char pad, unsigned long width)
{
    const struct tm *t = st->t;
    const bool sign = t->tm_year < -YEAR_BASE;
    const unsigned v = (unsigned)t->tm_year + YEAR_BASE;
    year(st, pad, width, sign ? 0u - v : v, sign);
}

/* -offset of the time zone in use for tm_isdst, in seconds east of UTC. */
static long zone_offset(const struct tm *t)
{
    return -__gettzinfo()->__tzrule[t->tm_isdst > 0].offset;
}

static bool format(struct st *st, const char *f)
{
    const struct tm *t = st->t;
    for (;;) {
        const char *lit = f;
        while (*f && *f != '%') {
            f++;
        }
        to_st(st, lit, (size_t)(f - lit));
        if (!*f) {
            return true;
        }
        f++;
        char pad = 0;
        unsigned long width = 0;
        if (*f == '0' || *f == '+') {
            pad = *f++;
        }
        if (*f >= '1' && *f <= '9') {
            do {
                width = width * 10 + (unsigned long)(*f++ - '0');
            } while (*f >= '0' && *f <= '9');
        }
        if (*f == 'E' || *f == 'O') {
            f++;
        }
        const char c = *f;
        switch (c) {
        case 'a':
        case 'A':
            out(st, c == 'a' ? "%.3s" : "%s", name(days, t->tm_wday, 7));
            break;
        case 'b':
        case 'h':
        case 'B':
            out(st, c == 'B' ? "%s" : "%.3s", name(months, t->tm_mon, 12));
            break;
        case 'c':
            if (!format(st, "%a %b %e %H:%M:%S %Y")) {
                return false;
            }
            break;
        case 'r':
            if (!format(st, "%I:%M:%S %p")) {
                return false;
            }
            break;
        case 'x':
            if (!format(st, "%m/%d/%y")) {
                return false;
            }
            break;
        case 'X':
            if (!format(st, "%H:%M:%S")) {
                return false;
            }
            break;
        case 'C': {
            const int neg = t->tm_year < -YEAR_BASE;
            const int century =
                t->tm_year >= 0 ? t->tm_year / 100 + YEAR_BASE / 100 : abs(t->tm_year + YEAR_BASE) / 100;
            if (width < 2) {
                width = 2;
            }
            out(st, "%s%.*d", neg ? "-" : (pad == '+' && century >= 100 ? "+" : ""), (int)(width - neg), century);
            break;
        }
        case 'd':
            out(st, "%.2d", t->tm_mday);
            break;
        case 'e':
            out(st, "%2d", t->tm_mday);
            break;
        case 'D':
            out(st, "%.2d/%.2d/%.2d", t->tm_mon + 1, t->tm_mday, year2(t));
            break;
        case 'F':
        case 'v': /* %F: %+4Y-%m-%d, %v: %e-%b-%+4Y; a flag or width changes the year's */
            if (c == 'v') {
                out(st, "%2d-%.3s-", t->tm_mday, name(months, t->tm_mon, 12));
            }
            full_year(st, pad ? pad : '+', !pad ? 4 : width < 6 ? 0 : width - 6);
            if (c == 'F') {
                out(st, "-%.2d-%.2d", t->tm_mon + 1, t->tm_mday);
            }
            break;
        case 'g':
        case 'G': {
            int adjust = iso_year_adjust(t);
            bool sign = t->tm_year < -YEAR_BASE;
            int century = t->tm_year >= 0 ? t->tm_year / 100 + YEAR_BASE / 100 : abs(t->tm_year + YEAR_BASE) / 100;
            int y = year2(t);
            if (c == 'g') {
                if (adjust < 0 && t->tm_year <= -YEAR_BASE) {
                    adjust = 1;
                } else if (adjust > 0 && t->tm_year < -YEAR_BASE) {
                    adjust = -1;
                }
                out(st, "%.2d", ((y + adjust) % 100 + 100) % 100);
                break;
            }
            if (adjust < 0 && t->tm_year <= -YEAR_BASE) {
                sign = true;
                adjust = 1;
            } else if (adjust > 0 && sign) {
                adjust = -1;
            }
            y += adjust;
            if (y == -1) {
                y = 99;
                century--;
            } else if (y == 100) {
                y = 0;
                century++;
            }
            year(st, pad, width, (unsigned)century * 100u + (unsigned)y, sign);
            break;
        }
        case 'H':
        case 'k':
            out(st, c == 'k' ? "%2d" : "%.2d", t->tm_hour);
            break;
        case 'I':
        case 'l': {
            const int h12 = (t->tm_hour == 0 || t->tm_hour == 12) ? 12 : t->tm_hour % 12;
            out(st, c == 'I' ? "%.2d" : "%2d", h12);
            break;
        }
        case 'j':
            out(st, "%.3d", t->tm_yday + 1);
            break;
        case 'm':
            out(st, "%.2d", t->tm_mon + 1);
            break;
        case 'M':
            out(st, "%.2d", t->tm_min);
            break;
        case 'n':
            to_st(st, "\n", 1);
            break;
        case 'p':
            to_st(st, t->tm_hour < 12 ? "AM" : "PM", 2);
            break;
        case 'P':
            to_st(st, t->tm_hour < 12 ? "am" : "pm", 2);
            break;
        case 'q':
            out(st, "%.1d", t->tm_mon / 3 + 1);
            break;
        case 'R':
            out(st, "%.2d:%.2d", t->tm_hour, t->tm_min);
            break;
        case 's': {
            const long offset = t->tm_isdst >= 0 ? zone_offset(t) : 0;
            const int y = t->tm_year;
            const long long d = ((long long)y - 69) / 4 - (y - 1) / 100 + (y + 299) / 400 + (y - 70) * 365 + t->tm_yday;
            out(st, "%lld", ((d * 24 + t->tm_hour) * 60 + t->tm_min) * 60 + t->tm_sec - offset);
            break;
        }
        case 'S':
            out(st, "%.2d", t->tm_sec);
            break;
        case 't':
            to_st(st, "\t", 1);
            break;
        case 'T':
            out(st, "%.2d:%.2d:%.2d", t->tm_hour, t->tm_min, t->tm_sec);
            break;
        case 'u':
        case 'w': {
            const char d = (char)('0' + (c == 'u' && t->tm_wday == 0 ? 7 : t->tm_wday));
            to_st(st, &d, 1);
            break;
        }
        case 'U':
            out(st, "%.2d", (t->tm_yday + 7 - t->tm_wday) / 7);
            break;
        case 'V': {
            const int adjust = iso_year_adjust(t);
            const int wday = t->tm_wday ? t->tm_wday - 1 : 6;
            int week = (t->tm_yday + 10 - wday) / 7;
            if (adjust > 0) {
                week = 1;
            } else if (adjust < 0) {
                /* the year before has 53 weeks if this one starts on a Friday, or on a Saturday after a leap year */
                week = 52 + (4 >= (wday - t->tm_yday - leap(t->tm_year + (YEAR_BASE - 1 - (t->tm_year < 0 ? 0 : 2000)))));
            }
            out(st, "%.2d", week);
            break;
        }
        case 'W':
            out(st, "%.2d", (t->tm_yday + 7 - (t->tm_wday ? t->tm_wday - 1 : 6)) / 7);
            break;
        case 'y':
            out(st, "%.2d", year2(t));
            break;
        case 'Y':
            full_year(st, pad, width);
            break;
        case 'z':
            if (t->tm_isdst >= 0) {
                const long offset = zone_offset(t);
                out(st, "%+03ld%.2ld", offset / 3600, labs(offset / 60) % 60);
            }
            break;
        case 'Z':
            if (t->tm_isdst >= 0) {
                out(st, "%s", _tzname[t->tm_isdst > 0]);
            }
            break;
        case '%':
            to_st(st, "%", 1);
            break;
        default:
            return false;
        }
        f++;
    }
}

size_t strftime(char *__restrict s, size_t maxsize, const char *__restrict fmt, const struct tm *__restrict t)
{
    struct st st = {s, maxsize, 0, t};
    if (!format(&st, fmt) || st.n >= maxsize) {
        return 0;
    }
    s[st.n] = 0;
    return st.n;
}

#endif
