/* Counting bits in a word, without a multiply where the core has none.
 *
 * clang expands __builtin_ctz into a De Bruijn multiply and a table, and
 * __builtin_popcount into adds and a final multiply. On rv32ec (CH32V003,
 * no M, no Zmmul) each multiply is a __mulsi3 call, a shift-and-add loop:
 * the EXTI dispatch ran one in every pin interrupt (V003, 2026-11-12). Where
 * the core multiplies in hardware, or has Zbb's ctz / cpop, the builtins are
 * the shortest and fastest; elsewhere these do it with compares and shifts.
 *
 * CH32RV_BITS_PORTABLE forces the portable code (the host tests). */
#pragma once

#include <stdint.h>

#if !defined(CH32RV_BITS_PORTABLE) && \
    (defined(__riscv_zbb) || defined(__riscv_mul) || defined(__riscv_zmmul))

static inline unsigned ch32rv_ctz32(uint32_t x) { return (unsigned)__builtin_ctz(x); }
static inline unsigned ch32rv_popcount32(uint32_t x) { return (unsigned)__builtin_popcount(x); }

#else

/* Undefined for 0, as __builtin_ctz is (this one gives 31). */
static inline unsigned ch32rv_ctz32(uint32_t x)
{
    unsigned n = 0;
    if (!(x & 0xffffu)) {
        n += 16;
        x >>= 16;
    }
    if (!(x & 0xffu)) {
        n += 8;
        x >>= 8;
    }
    if (!(x & 0xfu)) {
        n += 4;
        x >>= 4;
    }
    if (!(x & 3u)) {
        n += 2;
        x >>= 2;
    }
    return n + !(x & 1u);
}

static inline unsigned ch32rv_popcount32(uint32_t x)
{
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0f0f0f0fu;
    x += x >> 8;
    x += x >> 16;
    return x & 0x3fu;
}

#endif
