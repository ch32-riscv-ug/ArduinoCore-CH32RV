/* GPIO primitives shared by the digital, analog and peripheral code.
 * Everything is inline and constant-folds when the pin is a pad name. */
#pragma once

#include "ch32_pins.h"
#include "ch32_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Turn on the port's peripheral clock. Harmless to call repeatedly. */
static inline void ch32_gpio_clock_enable(uint8_t port)
{
    /* One register, PA at CH32_CLKEN_GPIO_BIT0 and the ports contiguous
     * after it - true of every family in clock_enables.csv. */
    CH32_REG32(CH32_CLKEN_GPIO_ADDR) |= 1u << (CH32_CLKEN_GPIO_BIT0 + (port));
}

/* Write the 4-bit configuration nibble for one pin. */
static inline void ch32_gpio_set_config(uint8_t port, uint8_t bit, uint32_t cfg)
{
    volatile uint32_t *reg;
    uint32_t shift;

    if (bit < 8) {
        reg = &CH32_GPIO_CFGLR(port);
        shift = bit * 4u;
#if CH32_GPIO_PORT_WIDTH > 8
    } else if (bit < 16) {
        reg = &CH32_GPIO_CFGHR(port);
        shift = (bit - 8u) * 4u;
#endif
#if CH32_GPIO_PORT_WIDTH > 16
    } else if (bit < 24) {
        reg = &CH32_GPIO_CFGXR(port);
        shift = (bit - 16u) * 4u;
#endif
    } else {
        return;   /* not a pin on this family */
    }
    *reg = (*reg & ~(0xFu << shift)) | ((cfg & 0xFu) << shift);
}

static inline void ch32_gpio_set(uint8_t port, uint8_t bit)
{
#if CH32_GPIO_PORT_WIDTH > 16
    /* Bits 16..23 have their own set register; BCR clears all 24. */
    if (bit >= 16) {
        CH32_GPIO_BSXR(port) = 1u << (bit - 16u);
        return;
    }
#endif
    CH32_GPIO_BSHR(port) = 1u << bit;
}

static inline void ch32_gpio_clear(uint8_t port, uint8_t bit)
{
    CH32_GPIO_BCR(port) = 1u << bit;
}

static inline uint8_t ch32_gpio_read(uint8_t port, uint8_t bit)
{
    return (uint8_t)((CH32_GPIO_INDR(port) >> bit) & 1u);
}

#ifdef __cplusplus
}
#endif
