/* random()/randomSeed() and tone()/noTone().
 *
 * C++ on purpose: api/Common.h declares these outside its extern "C" block,
 * so they need C++ linkage to satisfy the sketch's calls.
 */
#include "Arduino.h"

/* xorshift32 rather than newlib's random(): it is a handful of instructions,
 * needs no heap, and avoids the global-namespace clash between the Arduino
 * random(long) overload and stdlib's random(void). */
static uint32_t ch32_rand_state = 1;

static uint32_t ch32_next(void)
{
    uint32_t x = ch32_rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ch32_rand_state = x;
    return x;
}

void randomSeed(unsigned long seed)
{
    /* Zero would lock xorshift at zero forever; the Arduino API also treats a
     * zero seed as "leave the sequence alone". */
    if (seed != 0) {
        ch32_rand_state = (uint32_t)seed;
    }
}

long random(long howbig)
{
    if (howbig <= 0) {
        return 0;
    }
    return (long)(ch32_next() % (uint32_t)howbig);
}

long random(long howsmall, long howbig)
{
    if (howsmall >= howbig) {
        return howsmall;
    }
    return random(howbig - howsmall) + howsmall;
}

/* tone() and noTone() live in wiring_tone.c. */
