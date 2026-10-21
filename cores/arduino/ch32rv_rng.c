#include "ch32rv_rng.h"

#include <string.h>

#include "Arduino.h"
#include "ch32rv_registers.h"

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

static void start(void)
{
    ch32rv_clock_enable(RNG);
    RNG_CR |= RNG_CR_RNGEN;
    started = true;
}

/* One fresh word, or false. A seed error is cleared by restarting the
 * generator and discarding what it had (as the reference manual describes);
 * a clock error that persists means no usable output. */
static bool read_word(uint32_t *word)
{
    for (int attempt = 0; attempt < 4; attempt++) {
        const uint32_t t0 = micros();
        uint32_t sr;
        while (!((sr = RNG_SR) & (RNG_SR_DRDY | RNG_SR_SECS | RNG_SR_CECS))) {
            if ((uint32_t)(micros() - t0) > 2000u) {
                return false;
            }
        }
        if (sr & (RNG_SR_SECS | RNG_SR_SEIS)) {
            RNG_SR = ~RNG_SR_SEIS;
            RNG_CR &= ~RNG_CR_RNGEN;
            RNG_CR |= RNG_CR_RNGEN;
            continue;
        }
        if (sr & (RNG_SR_CECS | RNG_SR_CEIS)) {
            RNG_SR = ~RNG_SR_CEIS;
            continue;
        }
        *word = RNG_DR;
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
