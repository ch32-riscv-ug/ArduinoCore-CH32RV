/* Host driver for tests/unit/test_core_format.py: runs cores/arduino/ch32rv_format.c against the host's
 * snprintf (for what both must print alike) and against fixed strings (where the core differs on purpose),
 * and prints one line per failure. Built once per Floating Point configuration. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "ch32rv_format.h"

static int failures, checks;

static void to_buf(void *ctx, const char *s, size_t n)
{
    char **p = (char **)ctx;
    memcpy(*p, s, n);
    *p += n;
}

static size_t own(char *out, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char *p = out;
    const size_t n = ch32rv_vformat(to_buf, &p, fmt, ap);
    va_end(ap);
    *p = 0;
    if (n != (size_t)(p - out)) {
        printf("FAIL count %s: returned %zu, wrote %zu\n", fmt, n, (size_t)(p - out));
        failures++;
    }
    return n;
}

static void check(const char *what, const char *got, const char *want)
{
    checks++;
    if (strcmp(got, want) != 0) {
        printf("FAIL %s: got [%s] want [%s]\n", what, got, want);
        failures++;
    }
}

/* The core and the host's printf must agree. */
#define SAME(fmt, ...)                                  \
    do {                                                \
        char a[512], b[512];                            \
        own(a, fmt, __VA_ARGS__);                       \
        snprintf(b, sizeof b, fmt, __VA_ARGS__);        \
        check(fmt, a, b);                               \
    } while (0)

/* The core must print exactly this. */
#define IS(want, fmt, ...)                              \
    do {                                                \
        char a[512];                                    \
        own(a, fmt, __VA_ARGS__);                       \
        check(fmt, a, want);                            \
    } while (0)

static void fixed_is(const char *want, double v, int digits, int width, unsigned flags)
{
    char a[128], *p = a;
    ch32rv_format_fixed(to_buf, &p, v, digits, width, flags);
    *p = 0;
    char what[64];
    snprintf(what, sizeof what, "fixed(%.17g, %d, %d, %#x)", v, digits, width, flags);
    check(what, a, want);
}

int main(void)
{
    /* Integers: every flag, width and precision against the host. */
    static const long values[] = {0, 1, -1, 7, -42, 255, 4096, 65535, -65536, 2147483647L, -2147483647L - 1};
    static const char *const ifmt[] = {"%d", "%i", "%5d", "%-5d|", "%05d", "%+d", "% d", "%+05d", "%-+6d|", "%.3d",
                                       "%8.3d", "%-8.3d|", "%08.3d", "%.0d", "%5.0d|", "%+.0d", "%x", "%X", "%#x",
                                       "%#X", "%08x", "%#08x", "%.5x", "%#.5x", "%o", "%#o", "%#.0o", "%u", "%10u"};
    for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
        for (size_t k = 0; k < sizeof ifmt / sizeof ifmt[0]; k++) {
            SAME(ifmt[k], (int)values[i]);
        }
        /* long is 32 bits on CH32 (64 on the host): 32-bit values only */
        SAME("%ld|%lu|%lx", values[i], (unsigned long)(unsigned)values[i], (unsigned long)(unsigned)values[i]);
        SAME("%hd|%hu|%hhd|%hhu", (int)values[i], (unsigned)values[i], (int)values[i], (unsigned)values[i]);
        SAME("%zu", (size_t)(unsigned)values[i]);
    }
    SAME("%*d|%-*d|%*d", 6, 42, 6, 42, -6, 42);
    SAME("%.*d|%.*d", 4, 7, -1, 7);
    SAME("%c|%3c|%-3c|", 'A', 'B', 'C');
    SAME("%s|%10s|%-10s|%.2s|%8.3s|%-8.1s|%.0s|", "hey", "right", "left", "abcdef", "xyz", "q", "gone");
    SAME("%%|%5%|%d%%", 3);
    SAME("%s", "(null) aside, a long line of text that is longer than any small buffer would be, "
               "0123456789012345678901234567890123456789012345678901234567890123456789");
    IS("(null)", "%s", (const char *)0);
    IS("0x1234abcd", "%p", (void *)0x1234abcd);
    IS("0x0", "%p", (void *)0);

    /* What this does not do prints as written, and the arguments after it still line up. */
    IS("a %lld b 7", "a %lld b %d", 5LL, 7);
    IS("%llx|9", "%llx|%d", 0x123456789abcLL, 9);
    IS("%e|8", "%e|%d", 1.5, 8);
    IS("%.3g %G 5", "%.3g %G %d", 1.5, 2.5, 5);
    IS("%a 6", "%a %d", 1.0, 6);
    IS("%n 4", "%n %d", (int *)0, 4);
    IS("%y 3", "%y %d", 3);
    IS("50%", "50%", 0);

#if CH32RV_PRINTF_FLOAT
    /* %f: Print::printFloat's rounding (0.5 added at the last digit); away from exact halves it is C's. */
    static const double fvalues[] = {0.0, 1.0, -1.0, 0.1, 3.14159, -2.71828, 123.456, 0.000123, 98.6, 1e6 + 0.3};
    static const char *const ffmt[] = {"%f", "%.0f", "%.1f", "%.2f", "%.3f", "%8.2f", "%-8.2f|", "%08.2f", "%+.2f",
                                       "% .2f", "%F"};
    for (size_t i = 0; i < sizeof fvalues / sizeof fvalues[0]; i++) {
        for (size_t k = 0; k < sizeof ffmt / sizeof ffmt[0]; k++) {
#if CH32RV_FP_FLOAT
            /* float: compare against C on the float value, to 6 digits */
            char a[64], b[64];
            own(a, ffmt[k], fvalues[i]);
            snprintf(b, sizeof b, ffmt[k], (double)(float)fvalues[i]);
            check(ffmt[k], a, b);
#else
            SAME(ffmt[k], fvalues[i]);
#endif
        }
    }
    SAME("%*.*f|%-*.*f|", 9, 3, 2.5, 7, 1, 0.26);
    IS("0.3", "%.1f", 0.25); /* an exact half rounds up; C rounds to even */
    IS("-0.00", "%.2f", -0.0);
    IS("nan|inf|-inf", "%f|%f|%f", __builtin_nan(""), __builtin_inf(), -__builtin_inf());
    IS("  -inf", "%6.1f", -__builtin_inf());
    IS("ovf|-ovf", "%f|%f", 5e9, -5e9);
#if CH32RV_FP_FLOAT
    IS("0.123456788", "%.9f", 0.123456789);   /* float's digits, peeled in float */
    IS("16777216.0", "%.1f", 16777217.0);
#else
    IS("0.123456789", "%.9f", 0.123456789);
    IS("123456.7813", "%.4f", 123456.78125); /* an exact half rounds up; C rounds to even */
    IS("16777217.0", "%.1f", 16777217.0);
#endif
#else
    /* No %f: shown as written, and nothing after it slips. */
    IS("%.2f|3|%f|x", "%.2f|%d|%f|%s", 3.14159, 3, 1.0, "x");
#endif

    /* Print::printFloat's words and its sign on -0.0. */
    fixed_is("nan", __builtin_nan(""), 2, 0, CH32RV_FMT_PRINTFLOAT);
    fixed_is("inf", -__builtin_inf(), 2, 0, CH32RV_FMT_PRINTFLOAT);
    fixed_is("ovf", -5e9, 2, 0, CH32RV_FMT_PRINTFLOAT);
    fixed_is("0.00", -0.0, 2, 0, CH32RV_FMT_PRINTFLOAT);
    fixed_is("-0.00", -0.004, 2, 0, CH32RV_FMT_PRINTFLOAT);
    fixed_is("3", 2.5, 0, 0, CH32RV_FMT_PRINTFLOAT);
    fixed_is("   -1.5", -1.5, 1, 7, 0);
    fixed_is("-1.5   ", -1.5, 1, 7, CH32RV_FMT_LEFT);

    printf("checks %d failures %d\n", checks, failures);
    return failures != 0;
}
