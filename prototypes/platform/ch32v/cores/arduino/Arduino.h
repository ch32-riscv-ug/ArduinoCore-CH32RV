/* W-3 prototype Arduino.h - compile-only stub API.
 * The real core will implement the public API against ArduinoCore-API. */
#pragma once

#include <stdint.h>
#include <stddef.h>

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

#include "pins_arduino.h"

#ifdef __cplusplus
void setup(void);
void loop(void);
#endif
