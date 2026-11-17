#include "ch32rv_rng.h"

#include <string.h>

#include "Arduino.h"
#include "internal/ch32rv_registers.h"

#ifdef CH32RV_RNG_BASE

#define RNG_CR (*(volatile uint32_t *)(CH32RV_RNG_BASE + 0x0u))
#define RNG_SR (*(volatile uint32_t *)(CH32RV_RNG_BASE + 0x4u))
#define RNG_DR (*(volatile uint32_t *)(CH32RV_RNG_BASE + 0x8u))
#define RNG_CR_RNGEN 0x4u
#define RNG_SR_DRDY 0x1u
#define RNG_SR_CECS 0x2u   /* clock error, current */
#define RNG_SR_SECS 0x4u   /* seed error, current */
#define RNG_SR_CEIS 0x20u  /* clock error, latched */
#define RNG_SR_SEIS 0x40u  /* seed error, latched */

static bool started;
/* The word read last. A word equal to it is dropped, as the continuous test
 * of FIPS 140-2 drops it: a stuck generator then answers false (no new word
 * in 2 ms) instead of the same word again. */
static uint32_t last_word;

static bool read_word(uint32_t *word);

static void start(void)
{
    ch32rv_clock_enable(RNG);
    RNG_CR |= RNG_CR_RNGEN;
    started = true;
    /* The first word after the generator starts is not one: V307 gave 0. */
    uint32_t first;
    (void)read_word(&first);
}

/* One fresh word, or false. A seed error is cleared by restarting the
 * generator and discarding what it had (as the reference manual describes);
 * a clock error that persists, or no new word within 2 ms, means no usable
 * output. */
static bool read_word(uint32_t *word)
{
    const uint32_t t0 = micros();
    int restarts = 0;
    while ((uint32_t)(micros() - t0) <= 2000u) {
        const uint32_t sr = RNG_SR;
        if (sr & (RNG_SR_SECS | RNG_SR_SEIS)) {
            if (++restarts > 4) {
                return false;
            }
            RNG_SR = ~RNG_SR_SEIS;
            RNG_CR &= ~RNG_CR_RNGEN;
            RNG_CR |= RNG_CR_RNGEN;
            continue;
        }
        if (sr & (RNG_SR_CECS | RNG_SR_CEIS)) {
            RNG_SR = ~RNG_SR_CEIS;
            continue;
        }
        if (!(sr & RNG_SR_DRDY)) {
            continue;
        }
        const uint32_t w = RNG_DR;
        if (w == last_word) {
            continue;              /* the last word again: wait for a new one */
        }
        last_word = w;
        *word = w;
        return true;
    }
    return false;
}

bool ch32rv_rng_read(void *out, size_t len)
{
    if (!started) {
        start();
    }
    unsigned char *p = (unsigned char *)out;
    while (len) {
        uint32_t w;
        if (!read_word(&w)) {
            return false;
        }
        const size_t n = len < 4 ? len : 4;
        memcpy(p, &w, n);
        p += n;
        len -= n;
    }
    return true;
}

#else

bool ch32rv_rng_read(void *out, size_t len)
{
    (void)out;
    (void)len;
    return false;
}

#endif
