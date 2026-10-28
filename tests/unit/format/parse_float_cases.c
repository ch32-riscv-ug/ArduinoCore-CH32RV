/* Host driver for tests/unit/test_core_parse_float.py: feeds Stream::parseFloat's digits through
 * cores/arduino/ch32rv_parse_float.h and compares the float against the host's strtof. Built once per
 * CH32RV_PARSE_FLOAT; prints one line per failure. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ch32rv_parse_float.h"

static int failures, checks;

/* Stream::parseFloat's loop over a string that holds only what it takes */
static float parse(const char *s)
{
    ch32rv_fread_t r = CH32RV_FREAD_INIT;
    int negative = 0, fraction = 0;
    for (; *s; s++) {
        if (*s == '-')
            negative = 1;
        else if (*s == '.')
            fraction = 1;
        else
            ch32rv_fread_digit(&r, *s - '0', fraction);
    }
    return ch32rv_fread_end(&r, negative);
}

/* upstream's arithmetic, its 0.1 a double whatever the constants are */
static float upstream(const char *s)
{
    double value = 0, fraction = 1;
    int negative = 0, isFraction = 0;
    for (; *s; s++) {
        if (*s == '-')
            negative = 1;
        else if (*s == '.')
            isFraction = 1;
        else if (isFraction) {
            fraction *= (double)0.1L;
            value = value + fraction * (*s - '0');
        } else
            value = value * 10 + *s - '0';
    }
    return negative ? -value : value;
}

static int significant(const char *s)
{
    int n = 0, lead = 1;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            continue;
        lead &= *s == '0';
        n += !lead;
    }
    return n;
}

static long missed;   /* inputs off by the last bit, where that is allowed */

static void check(const char *s)
{
    float want = strtof(s, 0), got = parse(s);
    checks++;
    if (got == want)
        return;
    int ulp1 = got == nextafterf(want, INFINITY) || got == nextafterf(want, -INFINITY);
    int ok;
    if (CH32RV_PARSE_FLOAT == 1)
        ok = got == upstream(s);
    else if (CH32RV_PARSE_FLOAT == 2)
        ok = significant(s) > 11 && ulp1;
    else
        ok = ulp1;
    missed += ok;
    if (!ok) {
        printf("FAIL %s: %.9g, strtof %.9g\n", s, got, want);
        failures++;
    }
}

static uint32_t seed = 12345;
static uint32_t next(void)
{
    seed = seed * 1103515245u + 12345u;
    return seed >> 1;
}

int main(void)
{
    static const char *fixed[] = {
        "0", "-0", "0.0", "1", "-1", "0.1", "0.5", "1.5", "-2.25", "3.14159", "16777216", "16777217",
        "16777219", "4294967295", "4294967296", "0.0000001", "0.000000001", "1.00000006", "1.00000018",
        "123456789012", "99999999999.9", "3.4028235", "12345.678", "0.30000001192092896",
    };
    for (unsigned i = 0; i < sizeof fixed / sizeof *fixed; i++)
        check(fixed[i]);
    static const uint32_t pow10[] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000};
    char buf[48];
    for (int i = 0; i < 300000; i++) {
        uint32_t ip = next() % pow10[1 + next() % 9];
        int fd = next() % 10;
        if (fd)
            snprintf(buf, sizeof buf, "%s%lu.%0*lu", next() & 1 ? "-" : "", (unsigned long)ip, fd,
                     (unsigned long)(next() % pow10[fd]));
        else
            snprintf(buf, sizeof buf, "%lu", (unsigned long)ip);
        check(buf);
    }
    /* float's way misses on about one input in eight; more would be a fault of its own */
    if (CH32RV_PARSE_FLOAT == 3 && missed > checks / 5) {
        printf("FAIL %ld of %d off by the last bit\n", missed, checks);
        failures++;
    }
    printf("parse_float %d: checks %d, last bit off %ld, failures %d\n", CH32RV_PARSE_FLOAT, checks, missed,
           failures);
    return failures != 0;
}
