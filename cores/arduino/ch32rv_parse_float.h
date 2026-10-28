/* Stream::parseFloat's arithmetic: the digits it reads, one at a time, and the
 * float they make. The Floating Point menu picks one of three:
 *   CH32RV_PARSE_FLOAT 1: double, the way upstream does it; the nearest float
 *                        for any input, links double arithmetic
 *                      2: integers; the nearest float for inputs of up to 11
 *                        significant digits (a longer one can miss by the
 *                        last bit when it lies that close to a halfway point),
 *                        no double arithmetic
 *                      3: float; the smallest, misses by the last bit on
 *                        about one input in eight
 * Without it: 1 under Double, 2 under Float (CH32RV_FP_FLOAT). */
#pragma once

#include <stdint.h>

#include "ch32rv_format.h"

#ifndef CH32RV_PARSE_FLOAT
#define CH32RV_PARSE_FLOAT (CH32RV_FP_FLOAT ? 2 : 1)
#endif

#if CH32RV_PARSE_FLOAT == 1

typedef struct { double value, fraction; } ch32rv_fread_t;
#define CH32RV_FREAD_INIT {0, 1}

static inline void ch32rv_fread_digit(ch32rv_fread_t *r, int d, int fraction)
{
    if (fraction) {
        r->fraction *= (double)0.1L;    /* a double 0.1 under -fsingle-precision-constant too */
        r->value = r->value + r->fraction * d;
    } else {
        r->value = r->value * 10 + d;
    }
}

static inline float ch32rv_fread_end(ch32rv_fread_t *r, int negative)
{
    return negative ? -r->value : r->value;
}

#elif CH32RV_PARSE_FLOAT == 2

/* m: the first 11 significant digits, k of them after the point; e: integer
 * digits past those; st: a nonzero digit was dropped */
typedef struct { uint64_t m; int k, e, st; } ch32rv_fread_t;
#define CH32RV_FREAD_INIT {0, 0, 0, 0}

static inline void ch32rv_fread_digit(ch32rv_fread_t *r, int d, int fraction)
{
    if (r->m < 10000000000ull) {
        r->m = r->m * 10 + d;
        r->k += fraction;
    } else {
        r->st |= d != 0;
        r->e += !fraction;
    }
}

/* m / 10^k to the nearest float, ties to even: a 26-bit quotient in
 * integers, the bits below it kept as round and sticky */
static inline float ch32rv_fread_end(ch32rv_fread_t *r, int negative)
{
    uint64_t m = r->m, p = 1;
    float v = 0;
    if (m) {
        int k = r->e ? 0 : r->k, s = 0;
        while (k--)
            p *= 10;
        while (m >= (p << 26)) {
            p <<= 1;
            s--;
        }
        if (s == 0)
            while ((m << s) < (p << 25))
                s++;
        uint64_t n = s > 0 ? m << s : m, q = n / p;
        uint32_t mant = (uint32_t)(q >> 2);
        int st = r->st | (int)(q & 1) | (n != q * p);
        if ((q & 2) && (st || (mant & 1)))
            mant++;
        union { uint32_t u; float f; } scale = { (uint32_t)(127 + 2 - s) << 23 };
        v = (float)mant * scale.f;
        for (int e = r->e; e; e--)
            v *= 10;
    }
    return negative ? -v : v;
}

#else

typedef struct { uint32_t m; int k, e; } ch32rv_fread_t;
#define CH32RV_FREAD_INIT {0, 0, 0}

static inline void ch32rv_fread_digit(ch32rv_fread_t *r, int d, int fraction)
{
    if (r->m < 429496729u) {
        r->m = r->m * 10 + d;
        r->k += fraction;
    } else if (!fraction && r->e < 10) {
        r->e++;
    }
}

static inline float ch32rv_fread_end(ch32rv_fread_t *r, int negative)
{
    static const float p10[] = {1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f};
    float v = (float)r->m;
    if (r->e)
        v *= p10[r->e];
    else if (r->k)
        v /= p10[r->k < 10 ? r->k : 10];
    return negative ? -v : v;
}

#endif
