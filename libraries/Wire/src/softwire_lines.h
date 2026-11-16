/* SoftWire's two lines, written and read through the pins' registers
 * (SoftWire.cpp, the controller; SoftWireTarget.cpp, the target).
 *
 * digitalWrite() / digitalRead() looked the pin up on every call, and on an
 * X035 (48 MHz) that capped SoftWire near 37 kHz. The pins are open-drain
 * outputs from begin() on: the core's open-drain write (ch32rv_gpio.h) is all
 * it takes, the pads without open drain included. */
#pragma once

#include "Arduino.h"
#include "internal/ch32rv_gpio.h"

namespace arduino {

static inline void softwire_line_write(uint8_t pin, bool high)
{
    const pin_size_t pad = CH32RV_PIN_RESOLVE(pin);
    ch32rv_gpio_od_write((uint8_t)CH32RV_PIN_PORT(pad), (uint8_t)CH32RV_PIN_BIT(pad), high);
}

static inline bool softwire_line_high(uint8_t pin)
{
    const pin_size_t pad = CH32RV_PIN_RESOLVE(pin);
    return ch32rv_gpio_read((uint8_t)CH32RV_PIN_PORT(pad), (uint8_t)CH32RV_PIN_BIT(pad)) != 0;
}

}  // namespace arduino
