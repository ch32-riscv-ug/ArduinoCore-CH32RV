/* The register-level escape hatch, and the home of the core-API examples.
 *
 * A sketch never needs this header: everything the Arduino API offers is
 * already there through Arduino.h. What this adds is the layer underneath -
 * the register map, the GPIO helpers and the alternate-function route tables -
 * for the times when the API does not reach far enough.
 *
 *   #include <CH32.h>
 *   CH32_TIM_ATRLR(CH32_TIM2_BASE) = 999;   // straight at the timer
 *
 * Two warnings that belong on the tin:
 *
 * 1. **This layer is not stable.** The names here are this core's own, not a
 *    vendor SDK's, and they will change - the register map is on its way to
 *    being generated from ch32-device-data rather than hand-written. Sketches
 *    that use it are pinned to a core version in a way that sketches using the
 *    Arduino API are not.
 *
 * 2. **The core is also using these registers.** Writing AFIO_PCFR1 by hand
 *    while Serial or Wire is open will fight begin()/setRoute(), and the
 *    symptom is a peripheral that moves pins when you next call begin().
 *
 * This library also carries the examples for the built-in APIs (Blink,
 * AnalogRead, Fade, tone, interrupts, Serial), because a platform can only
 * ship examples through a library.
 */
#pragma once

#include "Arduino.h"

#include "ch32_gpio.h"
#include "ch32_pins.h"
#include "ch32_registers.h"
#include "ch32_route.h"

/* The chip-level object: CH32.restart(), CH32.resetReason(), the watchdog.
 * Modeled on the ESP cores' `ESP` - see CH32System.h. */
#include "CH32System.h"
