/* ch32rv_random() / ch32rv_fill_random() (Arduino.h): arduino-esp32's
 * esp_random() / esp_fill_random() under this core's names.
 *
 * Where the series has the hardware generator (CH32RV_RNG_BASE: V30x, V4x7,
 * H41x) every word is four of the generator's words (ch32rv_rng_read())
 * folded together (hardware_word() says why). Elsewhere, and if
 * the generator reports an error that does not clear, the words come from a
 * software mix of the chip's unique ID and the SysTick count at each call:
 * different from chip to chip and from run to run when the calls' timing
 * differs, but predictable to anyone who knows them - not for keys, nonces or
 * anything secret. ch32rv_rng_read() is the call that says false instead. */
#include "Arduino.h"
#include "ch32rv_rng.h"
#include "internal/ch32rv_ticks.h"

#include <string.h>

static uint32_t mix_state;

/* A 32-bit finalizer (MurmurHash3's fmix32): every input bit moves about half
 * of the output bits. */
static uint32_t fmix32(uint32_t h)
{
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

static uint32_t software_word(void)
{
    if (mix_state == 0u) {
#ifdef CH32RV_ESIG_UNIID1_ADDR
        mix_state = fmix32(*(const volatile uint32_t *)CH32RV_ESIG_UNIID1_ADDR) ^
                    *(const volatile uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
#endif
        mix_state |= 1u;
    }
    /* Weyl sequence plus the time of the call, then the finalizer. */
    mix_state += 0x9E3779B9u ^ ch32rv_ticks();
    return fmix32(mix_state);
}

#ifdef CH32RV_RNG_BASE
/* The generator's raw words are not uniform: V307's repeated words far apart
 * (one 4 times in 64) and leaned to 0x38 / 0xC7 / 0x55 / 0xAA byte patterns,
 * and H417's repeated one in 64 (bench, 2026-11-17). Each word given out is
 * four raw words folded through the finalizer (a conditioning step, as a
 * TRNG's output takes before use; it carries their entropy, it adds none).
 * ch32rv_rng_read() stays raw: Mbed TLS conditions it itself (SHA in its
 * entropy pool). */
static bool hardware_word(uint32_t *out)
{
    uint32_t raw[4];
    if (!ch32rv_rng_read(raw, sizeof raw)) {
        return false;
    }
    uint32_t h = 0;
    for (int i = 0; i < 4; i++) {
        h = fmix32(h ^ raw[i]) + 0x9E3779B9u;
    }
    *out = h;
    return true;
}
#endif

static uint32_t next_word(void)
{
#ifdef CH32RV_RNG_BASE
    uint32_t w;
    if (hardware_word(&w)) {
        return w;
    }
#endif
    return software_word();
}

uint32_t ch32rv_random(void)
{
    return next_word();
}

void ch32rv_fill_random(void *buf, size_t len)
{
    unsigned char *p = (unsigned char *)buf;
    while (len) {
        const uint32_t w = next_word();
        const size_t n = len < 4u ? len : 4u;
        memcpy(p, &w, n);
        p += n;
        len -= n;
    }
}
