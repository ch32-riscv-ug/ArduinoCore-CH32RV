/* snprintf, sprintf, vsnprintf and vsprintf on the core's formatter
 * (ch32rv_format.h), in place of newlib-nano's: the same conversions and %f
 * as Print::printf, by the Floating Point menu; no FILE, no heap, a few hundred
 * bytes of stack rather than 400-600. Defined here, they take the place of the
 * library's (core.a is searched before libc; see ch32rv_string.c). The newlib
 * entries of the menu keep newlib's. */
#include "ch32rv_format.h"

#if !CH32RV_PRINTF_NEWLIB

#include <stdint.h>
#include <stdio.h>

struct sbuf {
    char *p;
    size_t room; /* bytes left, the terminating NUL included */
};

static void to_sbuf(void *ctx, const char *s, size_t n)
{
    struct sbuf *b = (struct sbuf *)ctx;
    while (n--) {
        if (b->room > 1u) {
            *b->p++ = *s;
            b->room--;
        }
        s++;
    }
}

int vsnprintf(char *buf, size_t size, const char *format, va_list ap)
{
    struct sbuf b = {buf, size};
    const size_t n = ch32rv_vformat(to_sbuf, &b, format, ap);
    if (size) {
        *b.p = 0;
    }
    return (int)n;
}

int snprintf(char *buf, size_t size, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf(buf, size, format, ap);
    va_end(ap);
    return n;
}

int vsprintf(char *buf, const char *format, va_list ap)
{
    return vsnprintf(buf, SIZE_MAX, format, ap);
}

int sprintf(char *buf, const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    const int n = vsnprintf(buf, SIZE_MAX, format, ap);
    va_end(ap);
    return n;
}

#endif
