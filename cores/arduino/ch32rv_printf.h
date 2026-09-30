/* printf() on a Print, the way arduino-esp32's Print::printf works.
 *
 * ArduinoCore-API's Print is vendored unmodified (ADR-0009) and has no printf,
 * so each Print-derived class in this core that a sketch prints to (Serial1..,
 * SerialDMSeq, SerialSDI, SerialRTT, SerialDMDATA) adds the member with
 * CH32RV_PRINTF_MEMBER. Floats need the "printf() float support" menu, as
 * printf() itself does. */
#pragma once

#include "api/Print.h"

#include <stdarg.h>
#include <stddef.h>

/* Formats into a 64-byte stack buffer, and the heap only for a longer line. */
size_t ch32rv_vprintf(arduino::Print &out, const char *format, va_list ap);

#define CH32RV_PRINTF_MEMBER                                                  \
    size_t printf(const char *format, ...) __attribute__((format(printf, 2, 3))) \
    {                                                                         \
        va_list ap;                                                           \
        va_start(ap, format);                                                 \
        const size_t n = ch32rv_vprintf(*this, format, ap);                   \
        va_end(ap);                                                           \
        return n;                                                             \
    }
