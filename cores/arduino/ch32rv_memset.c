/* memset a word at a time: see ch32rv_string.c. */
#include <stddef.h>
#include <stdint.h>

#include "ch32rv_string.h"

#ifndef __riscv_32e

CH32RV_STRING_FN void *memset(void *dst, int c, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t v = (uint8_t)c;
    if (n >= 8u) {
        while ((uintptr_t)d & 3u) {
            *d++ = v;
            n--;
        }
        uint32_t w = (uint32_t)v | ((uint32_t)v << 8);     /* no multiply: V00x has none */
        w |= w << 16;
        uint32_t *dw = (uint32_t *)(void *)d;
        while (n >= 4u) {
            *dw++ = w;
            n -= 4u;
        }
        d = (uint8_t *)dw;
    }
    while (n--) {
        *d++ = v;
    }
    return dst;
}

#endif /* __riscv_32e */
