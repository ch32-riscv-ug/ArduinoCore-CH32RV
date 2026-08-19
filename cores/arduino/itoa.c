/* Conversions newlib does not provide but the Arduino API declares.
 * itoa/utoa come from newlib; only the long forms are missing
 * (api/itoa.h declares all four). */
#include "api/itoa.h"

#include <string.h>

static char *ch32_ultoa(unsigned long value, char *buf, int radix, int negative)
{
    char tmp[8 * sizeof(unsigned long) + 1];
    char *p = tmp;
    char *out = buf;

    if (radix < 2 || radix > 36) {
        *buf = '\0';
        return buf;
    }
    do {
        const unsigned long digit = value % (unsigned long)radix;
        *p++ = (char)(digit < 10 ? '0' + digit : 'a' + digit - 10);
        value /= (unsigned long)radix;
    } while (value);

    if (negative) {
        *out++ = '-';
    }
    while (p != tmp) {
        *out++ = *--p;
    }
    *out = '\0';
    return buf;
}

char *ltoa(long value, char *buf, int radix)
{
    if (radix == 10 && value < 0) {
        /* Negate in unsigned space so LONG_MIN does not overflow. */
        return ch32_ultoa(0UL - (unsigned long)value, buf, radix, 1);
    }
    return ch32_ultoa((unsigned long)value, buf, radix, 0);
}

char *ultoa(unsigned long value, char *buf, int radix)
{
    return ch32_ultoa(value, buf, radix, 0);
}
