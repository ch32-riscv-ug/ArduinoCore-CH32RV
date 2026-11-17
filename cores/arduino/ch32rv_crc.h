/* CRC-32 over a buffer: the CRC block's own CRC (EVT CRC/CRC_Calculation),
 * on every series - the CRC block where the series has one (V103, V20x, V205,
 * V30x, V4x7, L103 / M103, X305 / X315, H41x: CH32RV_HAS_CRC_HARDWARE), the
 * same result in software elsewhere.
 *
 * The block is STM32F1's: polynomial 0x04C11DB7, starts at 0xFFFFFFFF, takes
 * 32-bit words most significant bit first, no reflection and no final XOR.
 * Over bytes, the first byte the most significant of a word, that is the
 * catalogue's CRC-32/MPEG-2 (check value 0x0376E6E7 for "123456789"). It is
 * not zlib's / Ethernet's CRC-32 (the same polynomial, reflected, with a
 * final XOR: 0xCBF43926 for "123456789").
 *
 *     uint32_t crc = ch32rv_crc32(buf, len);
 *     // or in pieces, with the same result:
 *     uint32_t c = CH32RV_CRC32_INIT;
 *     c = ch32rv_crc32_update(c, part1, n1);
 *     c = ch32rv_crc32_update(c, part2, n2);
 *
 * Any length and alignment. Whole words go through the block, at most 64 at
 * a time with interrupts masked (a few microseconds), so an interrupt that
 * uses it too gets its own result; the 0-3 bytes left over, and everything
 * on a series without the block, go through a 16-entry table. Not shared
 * across H41x's two cores: one core uses it at a time. CH32RV.crc32() is the
 * same call on the CH32RV object. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "pins_arduino.h"

/* The block's RCC enable is in the variant exactly where the block exists. */
#ifdef CH32RV_CLKEN_CRC_ADDR
#define CH32RV_HAS_CRC_HARDWARE 1
#endif

/* Where every CRC starts, and what an empty buffer gives. */
#define CH32RV_CRC32_INIT 0xFFFFFFFFu

#ifdef __cplusplus
extern "C" {
#endif

/* crc after len more bytes; crc is CH32RV_CRC32_INIT or what the call before
 * returned. */
uint32_t ch32rv_crc32_update(uint32_t crc, const void *data, size_t len);

static inline uint32_t ch32rv_crc32(const void *data, size_t len)
{
    return ch32rv_crc32_update(CH32RV_CRC32_INIT, data, len);
}

#ifdef __cplusplus
}
#endif
