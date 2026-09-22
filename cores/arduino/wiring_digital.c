/* Digital I/O. Pin numbers are port-encoded (ADR-0010), so there is no
 * pin->pad table: the port and bit fall out of the number arithmetically. */
#include "Arduino.h"
#include "ch32_gpio.h"

/* Arduino.h spells the port base out itself for portOutputRegister(), so that
 * sketches get the ESP32-shaped macros without the whole register map. This is
 * the one place that sees both definitions, so it is where they are held to
 * the same value. */
_Static_assert(CH32_GPIO_PORT_BASE(0) == CH32_GPIO_BASE(0),
               "CH32_GPIO_PORT_BASE (ch32_pins.h) disagrees with "
               "CH32_GPIO_BASE (ch32_registers.h)");
_Static_assert(CH32_GPIO_PORT_BASE(3) == CH32_GPIO_BASE(3),
               "CH32_GPIO_PORT_BASE (ch32_pins.h) disagrees with "
               "CH32_GPIO_BASE (ch32_registers.h)");

#if defined(CH32_VARIANT_CH32X035) || defined(CH32_VARIANT_CH32X033)
#define CH32_GPIO_NO_OPEN_DRAIN 1
/* Pins whose OUTPUT_OPENDRAIN is emulated (see pinMode). One bit per pad. */
static uint32_t ch32_od_emulated[CH32_PORT_COUNT];
#else
#define CH32_GPIO_NO_OPEN_DRAIN 0
#endif

void pinMode(pin_size_t pin, PinMode mode)
{
    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    const uint8_t bit  = (uint8_t)CH32_PIN_BIT(pin);

    if (port >= CH32_PORT_COUNT) {
        return;
    }
    ch32_gpio_clock_enable(port);

#if CH32_GPIO_NO_OPEN_DRAIN
    ch32_od_emulated[port] &= ~(1u << bit);
#endif
    switch (mode) {
    case INPUT:
        ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_IN_FLOAT);
        break;
    case INPUT_PULLUP:
        ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_IN_PULL);
        ch32_gpio_set(port, bit);          /* OUTDR selects pull-up */
        break;
    case INPUT_PULLDOWN:
        ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_IN_PULL);
        ch32_gpio_clear(port, bit);
        break;
    case OUTPUT_OPENDRAIN:
#if CH32_GPIO_NO_OPEN_DRAIN
        /* errata x035-no-gpio-open-drain: the X0 GPIO block has no general-purpose
         * open-drain output (CNF=01 drives high like push-pull; ch32-data gpio_x0
         * says "no Open Drain output", measured 2026-09-22 with the OEP probe on
         * PA0/PA5/PB3/PB12/PC14). Emulate it: released = floating input, low =
         * push-pull low. digitalWrite() switches between the two. */
        ch32_od_emulated[port] |= 1u << bit;
        ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_IN_FLOAT);
        return;
#else
        ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_OUT_OD_10M);
        break;
#endif
    case OUTPUT:
    default:
        /* TODO(todo.ja.md): 10 MHz slew is hardcoded; expose a speed API. */
        ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_OUT_PP_10M);
        break;
    }
}

void digitalWrite(pin_size_t pin, PinStatus val)
{
    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);
    const uint8_t bit  = (uint8_t)CH32_PIN_BIT(pin);

    if (port >= CH32_PORT_COUNT) {
        return;
    }
#if CH32_GPIO_NO_OPEN_DRAIN
    if (ch32_od_emulated[port] & (1u << bit)) {
        if (val == LOW) {
            ch32_gpio_clear(port, bit);
            ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_OUT_PP_10M);
        } else {
            ch32_gpio_set_config(port, bit, CH32_GPIO_CFG_IN_FLOAT);
        }
        return;
    }
#endif
    if (val == LOW) {
        ch32_gpio_clear(port, bit);
    } else {
        ch32_gpio_set(port, bit);
    }
}

PinStatus digitalRead(pin_size_t pin)
{
    const uint8_t port = (uint8_t)CH32_PIN_PORT(pin);

    if (port >= CH32_PORT_COUNT) {
        return LOW;
    }
    return ch32_gpio_read(port, (uint8_t)CH32_PIN_BIT(pin)) ? HIGH : LOW;
}
