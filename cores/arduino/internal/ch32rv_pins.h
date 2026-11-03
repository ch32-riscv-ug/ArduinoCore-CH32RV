/* Pin number encoding shared by every variant (ADR-0010).
 *
 * A pin number is not an index: it carries the GPIO port and bit directly, so
 * digitalWrite() computes the register address arithmetically and no pin->pad
 * table is linked in. The generated variants/<SERIES>/pins_arduino.h defines
 * the pad names (PA0, PC13, ...) and the per-port validity masks on top of it.
 *
 * The port field starts at 2, so PA0 is 64 and every pad is 64..254. The
 * numbers 0..63 never name a pad: a product board whose silkscreen prints pin
 * numbers maps them to pads through its own table (CH32RV_BOARD_PINS, see
 * Arduino.h); on any other board they are invalid. pin_size_t is uint8_t and
 * 0xFF is NOT_A_PIN, which leaves room for PA..PF up to bit 30.
 */
#pragma once

#define CH32RV_PIN_PORT_BITS  5
#define CH32RV_PIN_PORT_FIRST 2   /* PA's value in the port field */
#define CH32RV_PORT_COUNT     6   /* PA..PF */
/* Numbers below this are a board's printed numbers, never a pad. */
#define CH32RV_BOARD_PIN_LIMIT (CH32RV_PIN_PORT_FIRST << CH32RV_PIN_PORT_BITS)

#define CH32RV_PIN(port, bit) \
    ((((port) + CH32RV_PIN_PORT_FIRST) << CH32RV_PIN_PORT_BITS) | (bit))
/* The port index (0 = PA). A number below CH32RV_BOARD_PIN_LIMIT comes out as
 * 254 or 255, so the `port >= CH32RV_PORT_COUNT` checks reject it. */
#define CH32RV_PIN_PORT(pin) \
    ((uint8_t)(((pin) >> CH32RV_PIN_PORT_BITS) - CH32RV_PIN_PORT_FIRST))
#define CH32RV_PIN_BIT(pin)    ((pin) & ((1 << CH32RV_PIN_PORT_BITS) - 1))

/* Base address of a port's register block. Repeated here rather than pulled
 * from ch32rv_registers.h so that Arduino.h can offer the port-access macros
 * without putting the whole register map into every sketch's namespace.
 * wiring_digital.c includes both headers and asserts they agree, so the two
 * cannot drift apart. */
#define CH32RV_GPIO_PORT_BASE(port) (0x40010800u + 0x400u * (uint32_t)(port))

#define NOT_A_PIN            0xffu
#define NOT_AN_ANALOG_PIN    0xffu
