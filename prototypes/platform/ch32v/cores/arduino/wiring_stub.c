/* W-3 prototype: compile-only stubs. No hardware behavior. */
#include "Arduino.h"

void SystemInit(void) {}

static volatile uint32_t stub_sink;

void pinMode(uint8_t pin, uint8_t mode) { stub_sink = (uint32_t)pin << 8 | mode; }
void digitalWrite(uint8_t pin, uint8_t val) { stub_sink = (uint32_t)pin << 8 | val; }
int  digitalRead(uint8_t pin) { return (int)(stub_sink >> 8 == pin); }
uint32_t millis(void) { return stub_sink; }
void yield(void) {}

void delay(uint32_t ms)
{
    /* busy loop placeholder; real implementation uses SysTick */
    for (volatile uint32_t i = 0; i < ms * 1000u; i++) {
    }
}
