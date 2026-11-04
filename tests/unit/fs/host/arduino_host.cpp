/* The core functions the API copy needs on the host. */
#include "Arduino.h"
#include <time.h>
#include "api/itoa.h"

static uint64_t now_us()
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000u + (uint64_t)t.tv_nsec / 1000u;
}
static const uint64_t start_us = now_us();
extern "C" unsigned long millis(void) { return (unsigned long)((now_us() - start_us) / 1000u); }
extern "C" unsigned long micros(void) { return (unsigned long)(now_us() - start_us); }
extern "C" void yield(void) {}
extern "C" void delay(unsigned long ms)
{
    const unsigned long t = millis();
    while (millis() - t < ms) {
    }
}
extern "C" char *itoa(int value, char *buf, int radix) { return ltoa(value, buf, radix); }
extern "C" char *utoa(unsigned value, char *buf, int radix) { return ultoa(value, buf, radix); }
