/* The chip-level object CH32RV, and the home of the core-API examples.
 *
 *   #include <CH32RV.h>
 *   CH32RV.restart();   CH32RV.getUniqueId();   CH32RV.deepSleep(...);
 *
 * The register map, the GPIO helpers and the route tables are the core's own
 * (ch32rv_registers.h and the rest): not part of what a release keeps, and
 * no longer pulled in here (docs/public-api.ja.md says what is).
 *
 * This library also carries the examples for the built-in APIs (Blink,
 * AnalogRead, Fade, tone, interrupts, Serial), because a platform can only
 * ship examples through a library.
 */
#pragma once

#include "Arduino.h"

/* The chip-level object: CH32RV.restart(), CH32RV.resetReason(), the watchdog.
 * Modeled on the ESP cores' `ESP` - see CH32RVSystem.h. */
#include "CH32RVSystem.h"
