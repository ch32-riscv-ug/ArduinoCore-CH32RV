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
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include "HardwareSerial.h"

void setup(void);
void loop(void);
#endif
