/* W-3 prototype Arduino.h - compile-only stub API.
 * The real core will implement the public API against ArduinoCore-API. */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "ch32_pins.h"

#define HIGH 1
#define LOW  0

#define INPUT         0
#define OUTPUT        1
#define INPUT_PULLUP  2

#ifdef __cplusplus
extern "C" {
#endif

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  digitalRead(uint8_t pin);
void delay(uint32_t ms);
uint32_t millis(void);
void yield(void);

#ifdef __cplusplus
}
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

#ifdef NUM_ANALOG_INPUTS
#define digitalPinToAnalogChannel(pin) CH32_PIN_TO_ADC_CHANNEL(pin)
#define analogInputToDigitalPin(chan)  CH32_ADC_CHANNEL_TO_PIN(chan)
#define digitalPinHasADC(pin) \
    (CH32_PIN_TO_ADC_CHANNEL(pin) != NOT_AN_ANALOG_PIN)
#endif

#ifdef __cplusplus
void setup(void);
void loop(void);
#endif
