/* Bit-banged helpers and the PRNG. Pure software, no CH32 registers. */
#include "Arduino.h"

#include <stdlib.h>

void shiftOut(pin_size_t dataPin, pin_size_t clockPin, BitOrder bitOrder,
              uint8_t val)
{
    for (uint8_t i = 0; i < 8; i++) {
        const uint8_t bit = (bitOrder == LSBFIRST) ? i : (uint8_t)(7 - i);
        digitalWrite(dataPin, ((val >> bit) & 1u) ? HIGH : LOW);
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(pin_size_t dataPin, pin_size_t clockPin, BitOrder bitOrder)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(clockPin, HIGH);
        if (digitalRead(dataPin) == HIGH) {
            value |= (uint8_t)(1u << ((bitOrder == LSBFIRST) ? i : (7 - i)));
        }
        digitalWrite(clockPin, LOW);
    }
    return value;
}

/* micros() wraps every ~71 minutes; the subtraction stays correct across the
 * wrap because it is modulo 2^32, and no timeout here is anywhere near that. */
unsigned long pulseIn(pin_size_t pin, uint8_t state, unsigned long timeout)
{
    const unsigned long start = micros();
    const PinStatus want = state ? HIGH : LOW;

    while (digitalRead(pin) == want) {              /* finish a pulse in flight */
        if (micros() - start >= timeout) {
            return 0;
        }
    }
    while (digitalRead(pin) != want) {              /* wait for it to begin */
        if (micros() - start >= timeout) {
            return 0;
        }
    }
    const unsigned long begin = micros();
    while (digitalRead(pin) == want) {
        if (micros() - start >= timeout) {
            return 0;
        }
    }
    return micros() - begin;
}

unsigned long pulseInLong(pin_size_t pin, uint8_t state, unsigned long timeout)
{
    return pulseIn(pin, state, timeout);
}
