/* memmove a word at a time: see ch32rv_string.c. */
#include <stddef.h>
#include <stdint.h>

#include "ch32rv_string.h"

#ifndef __riscv_32e

void *memcpy(void *restrict dst, const void *restrict src, size_t n);

CH32RV_STRING_FN void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d == s || n == 0u) {
        return dst;
    }
    if (d < s || d >= s + n) {
        /* Forward is safe: every read is at or ahead of the write. */
        return memcpy(dst, src, n);
    }
    /* Overlapping with the destination above: from the end down. */
    d += n;
    s += n;
    if (n >= 8u && (((uintptr_t)d ^ (uintptr_t)s) & 3u) == 0u) {
        while ((uintptr_t)d & 3u) {
            *--d = *--s;
            n--;
        }
        uint32_t *dw = (uint32_t *)(void *)d;
        const uint32_t *sw = (const uint32_t *)(const void *)s;
        while (n >= 4u) {
            *--dw = *--sw;
            n -= 4u;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--) {
        *--d = *--s;
    }
    return dst;
}

#endif /* __riscv_32e */
