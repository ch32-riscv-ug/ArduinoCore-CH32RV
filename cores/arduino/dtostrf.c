/* dtostrf (String(float) and friends): the core's fixed-point text rather than
 * upstream's dtostrf.c.impl, which formats with sprintf("%f") and forces
 * newlib's float printf in (about 21 KB) on every build that converts a float.
 * Float or double arithmetic as the Floating Point menu says; a negative width
 * left-aligns, as avr-libc's does. */
#include "api/deprecated-avr-comp/avr/dtostrf.h"

#include "ch32rv_format.h"

static void to_buffer(void *ctx, const char *s, size_t n)
{
    char **p = (char **)ctx;
    while (n--) {
        *(*p)++ = *s++;
    }
}

char *dtostrf(double val, signed char width, unsigned char prec, char *sout)
{
    char *p = sout;
    const int w = width;
    ch32rv_format_fixed(to_buffer, &p, val, prec, w < 0 ? -w : w, w < 0 ? CH32RV_FMT_LEFT : 0u);
    *p = 0;
    return sout;
}
