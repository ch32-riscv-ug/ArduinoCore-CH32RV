/* A pin's EXTI line, below attachInterrupt() (wiring_interrupts.c): for a
 * handler that follows the line itself after its first edge - SoftwareSerial
 * reads a whole frame from the start bit's interrupt - and so has to drop the
 * edges that came meanwhile, and wants to know how long its interrupt takes
 * to arrive. One EXTI line per bit number, the same registers on every
 * family. */
#pragma once

#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forget the edges the pin's line has seen (they would raise its interrupt
 * again on return). */
static inline void ch32rv_exti_clear(uint8_t pin)
{
    CH32RV_EXTI_INTFR = 1u << CH32RV_PIN_BIT(pin);
}

/* Raise the pin's line from software, as an edge would (SWIEVR): its
 * interrupt runs by the same path, so the time to it is the edge's. */
static inline void ch32rv_exti_trigger(uint8_t pin)
{
    CH32RV_EXTI_SWIEVR = 1u << CH32RV_PIN_BIT(pin);
}

#ifdef __cplusplus
}
#endif
