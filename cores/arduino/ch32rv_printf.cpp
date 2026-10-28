/* ch32rv_vprintf(), alone in its file for the reason ch32rv_sbrk.c gives: in
 * core.a a member is linked only when something needs one of its symbols, so
 * SerialDMSeq.printf() must not drag the USART driver in behind it. */
#include "ch32rv_printf.h"

#include "ch32rv_format.h"

#if CH32RV_PRINTF_NEWLIB
#include <stdio.h>
#include <stdlib.h>
#endif

static void to_print(void *ctx, const char *s, size_t n)
{
    static_cast<arduino::Print *>(ctx)->write(reinterpret_cast<const uint8_t *>(s), n);
}

size_t ch32rv_vprintf(arduino::Print &out, const char *format, va_list ap)
{
#if CH32RV_PRINTF_NEWLIB
    /* newlib's own formatting, for its complete %f / %e / %g. */
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
#else
    return ch32rv_vformat(to_print, &out, format, ap);
#endif
}

size_t arduino::ch32rv_print_float(arduino::Print &out, double number, int digits)
{
    return ch32rv_format_fixed(to_print, &out, number, digits < 0 ? 2 : digits, 0, CH32RV_FMT_PRINTFLOAT);
}
