/* ch32rv_vprintf(), alone in its file for the reason ch32rv_sbrk.c gives: in
 * core.a a member is linked only when something needs one of its symbols, so
 * SerialDMSeq.printf() must not drag the USART driver in behind it. */
#include "ch32rv_printf.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

size_t ch32rv_vprintf(arduino::Print &out, const char *format, va_list ap)
{
    char small[64];
    va_list copy;
    va_copy(copy, ap);
    const int len = vsnprintf(small, sizeof small, format, copy);
    va_end(copy);
    if (len < 0) {
        return 0;
    }
    if ((size_t)len < sizeof small) {
        return out.write((const uint8_t *)small, (size_t)len);
    }
    char *big = (char *)malloc((size_t)len + 1);
    if (big == nullptr) {
        return 0;
    }
    vsnprintf(big, (size_t)len + 1, format, ap);
    const size_t n = out.write((const uint8_t *)big, (size_t)len);
    free(big);
    return n;
}
