/* The debug interface's pads (SWIO / SWDIO, SWCLK) as ordinary pads.
 *
 * After every reset they belong to the debugger (AFIO_PCFR1.SW_CFG = 0xx), and
 * a peripheral routed through them does not reach the pin: an I2C on X035's
 * PC18 / PC19 times out on every address. ch32rv_release_debug_pins() hands
 * them over (SW_CFG = 100b) until the next reset; from then on no WCH-Link or
 * OEP probe can attach to the running sketch. It is never done on the core's
 * own initiative: on V00x the debug wire is the only way back into a chip
 * without a USB bootloader (CH32RV.releaseDebugPins(), the user's decision on
 * 2026-11-26).
 *
 * A route a sketch chooses by its pins (setPins(), setRoute(), begin() with
 * pins) through a pad that is still the debugger's is refused, so the mistake
 * shows as a false return instead of a silent timeout. The default routes are
 * left alone - V006's Serial2 and V407's Serial4 CTS default to a debug pad and
 * always have. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Turn the debug interface off until reset; its pads become ordinary pads. */
void ch32rv_release_debug_pins(void);

/* Whether ch32rv_release_debug_pins() has run since reset. */
bool ch32rv_debug_pins_released(void);

/* Whether `pin` is one of the debug interface's pads and still the debugger's. */
bool ch32rv_debug_pin_locked(uint8_t pin);

#ifdef __cplusplus
}
#endif
