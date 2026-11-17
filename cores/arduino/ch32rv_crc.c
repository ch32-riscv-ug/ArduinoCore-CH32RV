/* ch32rv_crc32_update(): the CRC block where there is one, a nibble table
 * elsewhere and for the bytes after the last whole word (ch32rv_crc.h). */
#include "ch32rv_crc.h"

#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"

#define CRC32_POLY 0x04C11DB7u

/* The CRC of each nibble as the top four bits of the state, four steps on. */
static const uint32_t crc32_nibble[16] = {
    0x00000000u, 0x04C11DB7u, 0x09823B6Eu, 0x0D4326D9u, 0x130476DCu, 0x17C56B6Bu, 0x1A864DB2u, 0x1E475005u,
    0x2608EDB8u, 0x22C9F00Fu, 0x2F8AD6D6u, 0x2B4BCB61u, 0x350C9B64u, 0x31CD86D3u, 0x3C8EA00Au, 0x384FBDBDu,
};

static uint32_t crc32_bytes(uint32_t crc, const uint8_t *p, size_t len)
{
    while (len--) {
        const uint32_t b = *p++;
        crc = (crc << 4) ^ crc32_nibble[(crc >> 28) ^ (b >> 4)];
        crc = (crc << 4) ^ crc32_nibble[(crc >> 28) ^ (b & 0xFu)];
    }
    return crc;
}

#ifdef CH32RV_HAS_CRC_HARDWARE

/* At the same address on every family that has it (device-data memory_map). */
#define CRC_BASE   0x40023000u
#define CRC_DATAR  CH32RV_REG32(CRC_BASE + 0x0u)
#define CRC_CTLR   CH32RV_REG32(CRC_BASE + 0x8u)
#define CRC_CTLR_RESET 0x1u

/* One word into the block, and its state once it has taken it (`prev`: the
 * state before). On V307 and H417 the block needs time per word and does not
 * hold the bus meanwhile: a read too soon returns 0, and a write too soon goes
 * in as if the state were 0 (bench, 2026-11-17: a word written right after
 * another gave f(0 ^ w)). On H417 the first read after the write can still
 * return the state from before it (the write not yet arrived): waiting for "not
 * 0" alone stopped there, and the next word went in on 0. So the read is
 * repeated until it is neither 0 nor the previous state - bounded, for the
 * one word in 2^32 whose result is one of those. V103 answers at once. */
static uint32_t crc_feed(uint32_t word, uint32_t prev)
{
    CRC_DATAR = word;
    uint32_t state = 0;
    for (int n = 0; n < 64; n++) {
        state = CRC_DATAR;
        if (state != 0u && state != prev) {
            break;
        }
    }
    return state;
}

/* Words per masked stretch: about 64 bus writes. */
#define CRC_CHUNK_WORDS 64u

/* The word that takes the block from its reset value to `crc`. One word
 * moves the state to f(state ^ w), f being 32 steps of the polynomial; f is
 * undone a step at a time (the polynomial's bit 0 is set, so the bit shifted
 * out shows in bit 0). */
static uint32_t crc32_seed_word(uint32_t crc)
{
    for (int i = 0; i < 32; i++) {
        crc = (crc & 1u) ? (((crc ^ CRC32_POLY) >> 1) | 0x80000000u) : (crc >> 1);
    }
    return crc ^ CH32RV_CRC32_INIT;
}

uint32_t ch32rv_crc32_update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    if (len >= 4u) {
        ch32rv_clock_enable(CRC);
    }
    while (len >= 4u) {
        size_t n = len / 4u;
        if (n > CRC_CHUNK_WORDS) {
            n = CRC_CHUNK_WORDS;
        }
        const uint32_t seed = crc == CH32RV_CRC32_INIT ? 0u : crc32_seed_word(crc);
        const uint32_t saved = ch32rv_irq_save();
        CRC_CTLR = CRC_CTLR_RESET;
        /* The reset arrived (the same posted-write delay as crc_feed()'s). */
        for (int k = 0; k < 64 && CRC_DATAR != CH32RV_CRC32_INIT; k++) {
        }
        uint32_t state = CH32RV_CRC32_INIT;
        if (crc != CH32RV_CRC32_INIT) {
            state = crc_feed(seed, state);
        }
        for (size_t i = 0; i < n; i++, p += 4) {
            /* The first byte is the word's most significant. */
            state = crc_feed(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3], state);
        }
        crc = state;
        ch32rv_irq_restore(saved);
        len -= n * 4u;
    }
    return crc32_bytes(crc, p, len);
}

#else

uint32_t ch32rv_crc32_update(uint32_t crc, const void *data, size_t len)
{
    return crc32_bytes(crc, (const uint8_t *)data, len);
}

#endif
