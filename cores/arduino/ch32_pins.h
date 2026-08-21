/* Pin number encoding shared by every variant (ADR-0010).
 *
 * A pin number is not an index: it carries the GPIO port and bit directly, so
 * digitalWrite() computes the register address arithmetically and no pin->pad
 * table is linked in. The generated variants/<SERIES>/pins_arduino.h defines
 * the pad names (PA0, PC13, ...) and the per-port validity masks on top of it.
 */
#pragma once

#define CH32_PIN_PORT_BITS 5
#define CH32_PORT_COUNT    6   /* PA..PF */

#define CH32_PIN(port, bit)  (((port) << CH32_PIN_PORT_BITS) | (bit))
#define CH32_PIN_PORT(pin)   ((pin) >> CH32_PIN_PORT_BITS)
#define CH32_PIN_BIT(pin)    ((pin) & ((1 << CH32_PIN_PORT_BITS) - 1))

/* Base address of a port's register block. Repeated here rather than pulled
 * from ch32_registers.h so that Arduino.h can offer the port-access macros
 * without putting the whole register map into every sketch's namespace.
 * wiring_digital.c includes both headers and asserts they agree, so the two
 * cannot drift apart. */
#define CH32_GPIO_PORT_BASE(port) (0x40010800u + 0x400u * (uint32_t)(port))

#define NOT_A_PIN            0xffu
#define NOT_AN_ANALOG_PIN    0xffu
