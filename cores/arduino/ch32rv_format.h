/* The core's own printf formatting, and the fixed-point text Print::print(double),
 * dtostrf() and printf's %f share.
 *
 * It writes straight into a sink, a piece at a time: no line buffer on the
 * stack, no heap. The Floating Point menu picks how floats are handled:
 *   CH32RV_FP_FLOAT     1: float arithmetic (with -fsingle-precision-constant),
 *                       the way AVR's 32-bit double prints; 0: double
 *   CH32RV_PRINTF_FLOAT 1: printf's %f / %F; 0: %f prints its own text
 *                       ("%.2f") and links no floating point at all
 * Conversions this does not do (%e %g %a, long long, %n) print their own
 * text too, so nothing is dropped silently. */
#pragma once

#include <stdarg.h>
#include <stddef.h>

#ifndef CH32RV_FP_FLOAT
#define CH32RV_FP_FLOAT 0
#endif
#ifndef CH32RV_PRINTF_FLOAT
#define CH32RV_PRINTF_FLOAT 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*ch32rv_sink_fn)(void *ctx, const char *s, size_t n);

/* printf's conversions: %[flags - 0 + space #][width|*][.precision|*]
 * [hh h l z t] d i u o x X c s p %, and %f %F with CH32RV_PRINTF_FLOAT.
 * Returns the characters produced. */
size_t ch32rv_vformat(ch32rv_sink_fn sink, void *ctx, const char *format, va_list ap);

#define CH32RV_FMT_LEFT       0x01u
#define CH32RV_FMT_ZERO       0x02u
#define CH32RV_FMT_PLUS       0x04u
#define CH32RV_FMT_SPACE      0x08u
#define CH32RV_FMT_PRINTFLOAT 0x10u /* Print::printFloat's words: "nan", "inf", "ovf" without a sign */

/* v with `digits` decimals, Print::printFloat's way: 0.5 added at the last
 * digit, then the digits peeled off; "ovf" past 4294967040. */
size_t ch32rv_format_fixed(ch32rv_sink_fn sink, void *ctx, double v, int digits, int width, unsigned flags);

#ifdef __cplusplus
}
#endif
