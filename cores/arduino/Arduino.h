/* Public entry point for sketches. The API surface is ArduinoCore-API
 * (cores/arduino/api, ADR-0009); this header only adds what the CH32 core
 * itself defines: the pin encoding, the generated variant pin map, and the
 * serial instances. */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "ch32_pins.h"

#ifdef __cplusplus
#include "api/ArduinoAPI.h"
using namespace arduino;
#else
#include "api/Common.h"
#endif

/* Pad names, per-port validity masks and analog aliases for the selected
 * series (generated; see ADR-0010). */
#include "pins_arduino.h"

/* Pin numbers are sparse, so a range check is not enough: a pin is valid only
 * if its port exists in this series and the port's mask has the bit set. Both
 * fold to a constant when `pin` is a pad name. */
#define digitalPinIsValid(pin) \
    ((CH32_PIN_PORT(pin) < CH32_PORT_COUNT) && \
     ((CH32_PORT_MASK(CH32_PIN_PORT(pin)) >> CH32_PIN_BIT(pin)) & 1u))

/* Same, restricted to the pads present on every part in the series - the set a
 * sketch built for the ANY menu entry can rely on. */
#define digitalPinIsCommon(pin) \
    ((CH32_PIN_PORT(pin) < CH32_PORT_COUNT) && \
     ((CH32_PORT_COMMON_MASK(CH32_PIN_PORT(pin)) >> CH32_PIN_BIT(pin)) & 1u))

/* EXTI lines are numbered by the pin's bit, not by the port, so the pin number
 * carries everything attachInterrupt() needs. */
#define digitalPinToInterrupt(pin) (pin)

/* api/Common.h declares these two but leaves them to the core. MIE is bit 3 of
 * mstatus, and csrsi/csrci take the bit as an immediate, so each is one
 * instruction with no scratch register.
 *
 * noInterrupts() does not nest: a second call still leaves one interrupts()
 * away from enabled, which is the AVR behaviour libraries are written
 * against. */
static inline void interrupts(void)
{
    __asm__ volatile ("csrsi mstatus, 8" ::: "memory");
}

static inline void noInterrupts(void)
{
    __asm__ volatile ("csrci mstatus, 8" ::: "memory");
}

#ifdef NUM_ANALOG_INPUTS
#define digitalPinToAnalogChannel(pin) CH32_PIN_TO_ADC_CHANNEL(pin)
#define analogInputToDigitalPin(chan)  CH32_ADC_CHANNEL_TO_PIN(chan)
#define digitalPinHasADC(pin) \
    (CH32_PIN_TO_ADC_CHANNEL(pin) != NOT_AN_ANALOG_PIN)
#endif

#ifdef __cplusplus
extern "C" {
#endif
void SystemInit(void);

/* ArduinoCore-API does not declare these two - they arrived in the Arduino
 * API after the version this core pins, and several cores define them
 * themselves. Ours are implemented in C (wiring_analog.c / wiring_pwm.c), so
 * the declarations belong in this extern "C" block: without them a sketch
 * cannot call a function the core has had all along. */
void analogReadResolution(int bits);
void analogWriteResolution(int bits);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "HardwareSerial.h"

void setup(void);
void loop(void);
#endif
