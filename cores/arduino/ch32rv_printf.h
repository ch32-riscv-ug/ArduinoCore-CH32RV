/* printf() on a Print, the way arduino-esp32's Print::printf works.
 *
 * ArduinoCore-API's Print is vendored unmodified (ADR-0009) and has no printf,
 * so each Print-derived class in this core that a sketch prints to (Serial1..,
 * SerialDMSeq, SerialSDI, SerialRTT, SerialDMDATA) adds the member with
 * CH32RV_PRINTF_MEMBER. The formatting is the core's own (ch32rv_format.h),
 * or newlib's when the Floating Point menu asks for it; %f follows that menu. */
#pragma once

#include "api/Print.h"

#include <stdarg.h>
#include <stddef.h>

/* Formats straight into `out` (newlib's: a 64-byte stack buffer, the heap for
 * a longer line). */
size_t ch32rv_vprintf(arduino::Print &out, const char *format, va_list ap);

/* Print::printFloat (ArduinoCore-API, patched to call this): float or double
 * arithmetic as the Floating Point menu says. In namespace arduino, where the
 * patch's declaration inside Print::printFloat puts it. */
namespace arduino {
size_t ch32rv_print_float(Print &out, double number, int digits);
}

#define CH32RV_PRINTF_MEMBER                                                  \
    size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3))) \
    {                                                                         \
        va_list ap;                                                           \
        va_start(ap, format);                                                 \
        const size_t n = ch32rv_vprintf(*this, format, ap);                   \
        va_end(ap);                                                           \
        return n;                                                             \
    }
