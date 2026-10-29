/* memcpy, memmove and memset a word at a time.
 *
 * newlib-nano builds these for size: one byte per loop turn. On CH32H417's
 * V3F that is 9-10 MB/s for memcpy against 72 MB/s for a word loop (32 KB,
 * measured 2026-10-21), and every USB class, Print and String goes through
 * them. Defined here, they take the place of the library's: the linker only
 * pulls an archive member for a symbol still undefined. One function to a
 * file (memmove and memset in ch32rv_memmove.c, ch32rv_memset.c): a libc
 * object can pull in libc's memcpy before LTO's code calls the core's memset
 * (setenv(), then a struct cleared with = {}), and an object holding both
 * would then define memcpy a second time.
 *
 * No part here is relied on to take a misaligned word access: when source and
 * destination are aligned differently, memcpy reads aligned source words and
 * shifts them together. GCC would turn the loops below back into calls to
 * these very functions, so the file is built without that pattern.
 *
 * Not on the rv32e parts (CH32V00x, M007): there the 16 KB of flash weighs
 * more than the copy speed, and the three cost about 180 bytes. */
#include <stddef.h>
#include <stdint.h>

#ifndef __riscv_32e

#include "ch32rv_string.h"

CH32RV_STRING_FN void *memcpy(void *restrict dst, const void *restrict src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (n >= 16u && (((uintptr_t)d ^ (uintptr_t)s) & 3u) != 0u) {
        /* Alignments differ: aligned word reads of the source, shifted
         * together (little-endian). Only words holding source bytes are read. */
        while ((uintptr_t)d & 3u) {
            *d++ = *s++;
            n--;
        }
        const unsigned shift = 8u * ((uintptr_t)s & 3u);
        const uint32_t *sw = (const uint32_t *)(const void *)((uintptr_t)s & ~(uintptr_t)3u);
        uint32_t *dw = (uint32_t *)(void *)d;
        uint32_t lo = *sw++;
        while (n >= 8u) {                            /* the next word is still all source */
            const uint32_t hi = *sw++;
            *dw++ = (lo >> shift) | (hi << (32u - shift));
            lo = hi;
            n -= 4u;
            s += 4;
        }
        d = (uint8_t *)dw;
    } else if (n >= 8u && (((uintptr_t)d ^ (uintptr_t)s) & 3u) == 0u) {
        while ((uintptr_t)d & 3u) {
            *d++ = *s++;
            n--;
        }
        uint32_t *dw = (uint32_t *)(void *)d;
        const uint32_t *sw = (const uint32_t *)(const void *)s;
        while (n >= 16u) {
            const uint32_t a = sw[0], b = sw[1], c = sw[2], e = sw[3];
            dw[0] = a;
            dw[1] = b;
            dw[2] = c;
            dw[3] = e;
            dw += 4;
            sw += 4;
            n -= 16u;
        }
        while (n >= 4u) {
            *dw++ = *sw++;
            n -= 4u;
        }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

#endif /* __riscv_32e */
