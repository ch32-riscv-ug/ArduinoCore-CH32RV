/* GPIO primitives shared by the digital, analog and peripheral code.
 * Everything is inline and constant-folds when the pin is a pad name. */
#pragma once

#include "internal/ch32rv_pins.h"
#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif

/* No open-drain output mode on the pads (CH32X035 / X033): OUTPUT_OPENDRAIN
 * is emulated - low a push-pull 0, high an input let go (wiring_digital.c;
 * SoftWire does the same). */
#if defined(CH32RV_VARIANT_CH32X035) || defined(CH32RV_VARIANT_CH32X033)
#define CH32RV_GPIO_NO_OPEN_DRAIN 1
#else
#define CH32RV_GPIO_NO_OPEN_DRAIN 0
#endif

/* Turn on the port's peripheral clock. Harmless to call repeatedly. */
static inline void ch32rv_gpio_clock_enable(uint8_t port)
{
    /* One register, PA at CH32RV_CLKEN_GPIO_BIT0 and the ports contiguous
     * after it - true of every family in clock_enables.csv. */
    ch32rv_clock_enable_at(CH32RV_CLKEN_GPIO_ADDR, 1u << (CH32RV_CLKEN_GPIO_BIT0 + (port)));
}

/* Write the 4-bit configuration nibble for one pin. */
#if defined(CH32RV_USBPD_CC_PORT_MASK)
#ifdef __cplusplus
extern "C" {
#endif
/* ch32rv_usbpd_cc.c: the pad's Rd off (its PORT_CCx.CCx_PD), the USBPD block's clock on for the write. */
void ch32rv_usbpd_cc_release(uint8_t port, uint8_t bit);
#ifdef __cplusplus
}
#endif
#endif

/* The nibble alone (and X035's USB pad errata), for a pad already given to its function: what an open-drain
 * write switches per bit on the parts without open drain (ch32rv_gpio_od_write()). The first configuration goes
 * through ch32rv_gpio_set_config(), which also lets go of a USB PD CC pad's Rd. */
static inline void ch32rv_gpio_write_config(uint8_t port, uint8_t bit, uint32_t cfg)
{
    volatile uint32_t *reg;
    uint32_t shift;

    if (bit < 8) {
        reg = &CH32RV_GPIO_CFGLR(port);
        shift = bit * 4u;
#if CH32RV_GPIO_PORT_WIDTH > 8
    } else if (bit < 16) {
        reg = &CH32RV_GPIO_CFGHR(port);
        shift = (bit - 8u) * 4u;
#endif
#if CH32RV_GPIO_PORT_WIDTH > 16
    } else if (bit < 24) {
        reg = &CH32RV_GPIO_CFGXR(port);
        shift = (bit - 16u) * 4u;
#endif
    } else {
        return;   /* not a pin on this family */
    }
    /* A port's register holds eight pads: the cores' lock keeps another
     * core's pad in it (H41x, internal/ch32rv_cores.h). */
    const uint32_t lock = ch32rv_cores_lock();
    *reg = (*reg & ~(0xFu << shift)) | ((cfg & 0xFu) << shift);
    ch32rv_cores_unlock(lock);
#if defined(CH32RV_VARIANT_CH32X035) || defined(CH32RV_VARIANT_CH32X033)
    /* errata x035-usb-pads-open-drain: PC16/PC17 are the USB PHY pads. With
     * AFIO_CTLR.USB_PHY_V33 set (reset default 0x45) a GPIO or AF open-drain
     * output on them drives high instead of releasing, so an I2C slave can never
     * ACK and Wire on route 2/4 always reports an address NACK. Drop the bit
     * whenever either pad becomes an output; USB init sets it again itself. */
    if (port == 2u && (bit == 16u || bit == 17u) && (cfg & 0x3u) != 0u) {
        CH32RV_AFIO_CTLR &= ~CH32RV_AFIO_CTLR_USB_PHY_V33;
    }
#endif
}

static inline void ch32rv_gpio_set_config(uint8_t port, uint8_t bit, uint32_t cfg)
{
    ch32rv_gpio_write_config(port, bit, cfg);
#if defined(CH32RV_USBPD_CC_PORT_MASK)
    /* A USB PD CC pad: the chip switches its Rd (~5.1 kOhm to ground) on at reset, which holds a GPIO, I2C or
     * UART there low. A pad given another function drops it (CH32RVUsbPd sets PORT_CCx itself). */
    if (CH32RV_USBPD_CC_PORT_MASK(port) & (1u << bit)) {
        ch32rv_usbpd_cc_release(port, bit);
    }
#endif
}

/* The alternate function a pad carries, on the families that choose it per
 * pin (V205, X305, X315, H417: AFIO GPIOx_AFLR / AFHR, one 4-bit number per
 * pad, two registers per port from GPIOA_AFLR - AFIO + 0x20, or where the
 * board says: CH32H417 has them from 0x04 for PA..PF). A route's value is
 * that number there (ch32rv_route.h). Elsewhere a route moves as a whole
 * through PCFR1 and this does nothing. */
#ifndef CH32RV_AFIO_AFLR_OFFSET
#define CH32RV_AFIO_AFLR_OFFSET 0x20u
#endif
#ifndef CH32RV_AFIO_AF_PORTS
#define CH32RV_AFIO_AF_PORTS 5u
#endif
static inline void ch32rv_pin_set_af(uint8_t pin, uint32_t af)
{
#if CH32RV_GPIO_PER_PIN_AF
    const uint32_t port = (uint32_t)CH32RV_PIN_PORT(pin);
    const uint32_t bit = (uint32_t)CH32RV_PIN_BIT(pin);
    if (bit >= 16u || port >= CH32RV_AFIO_AF_PORTS) {
        return;   /* no AFLR / AFHR for this port */
    }
    /* The selection registers are AFIO's: with its clock off the write is
     * lost and the pad stays on no function - analogWrite() on CH32H417 gave
     * a pin stuck high whenever nothing else (Serial) had turned AFIO on. */
    ch32rv_clock_enable(AFIO);
    volatile uint32_t *reg = &CH32RV_REG32(CH32RV_AFIO_BASE + CH32RV_AFIO_AFLR_OFFSET + port * 8u +
                                           (bit >= 8u ? 4u : 0u));
    const uint32_t shift = (bit & 7u) * 4u;
    const uint32_t lock = ch32rv_cores_lock();
    *reg = (*reg & ~(0xFu << shift)) | ((af & 0xFu) << shift);
    ch32rv_cores_unlock(lock);
#else
    (void)pin;
    (void)af;
#endif
}

/* The pad's output speed on the families that keep it in a register of its
 * own (CH32H41x, X3x5: GPIOx_SPEED, two bits a pad, reset 00 = 10 MHz; the
 * variant's CH32RV_GPIO_SPEED_OFFSET): code 0 10 MHz, 1 50, 2 100, 3 180 MHz
 * (CH32H417RM 9.3.1.8). Elsewhere the configuration nibble's MODE is the
 * speed and this does nothing. */
static inline void ch32rv_gpio_set_speed(uint8_t port, uint8_t bit, uint32_t code)
{
#ifdef CH32RV_GPIO_SPEED_OFFSET
    if (bit >= 16u) {
        return;
    }
    volatile uint32_t *reg = &CH32RV_REG32(CH32RV_GPIO_BASE(port) + CH32RV_GPIO_SPEED_OFFSET);
    const uint32_t shift = (uint32_t)bit * 2u;
    const uint32_t lock = ch32rv_cores_lock();
    *reg = (*reg & ~(3u << shift)) | ((code & 3u) << shift);
    ch32rv_cores_unlock(lock);
#else
    (void)port;
    (void)bit;
    (void)code;
#endif
}

static inline void ch32rv_gpio_set(uint8_t port, uint8_t bit)
{
#if CH32RV_GPIO_PORT_WIDTH > 16
    /* Bits 16..23 have their own set register; BCR clears all 24. */
    if (bit >= 16) {
        CH32RV_GPIO_BSXR(port) = 1u << (bit - 16u);
        return;
    }
#endif
    CH32RV_GPIO_BSHR(port) = 1u << bit;
}

static inline void ch32rv_gpio_clear(uint8_t port, uint8_t bit)
{
    CH32RV_GPIO_BCR(port) = 1u << bit;
}

static inline uint8_t ch32rv_gpio_read(uint8_t port, uint8_t bit)
{
    return (uint8_t)((CH32RV_GPIO_INDR(port) >> bit) & 1u);
}

/* An open-drain line, low or let go: a pad set to OUTPUT_OPENDRAIN, written
 * without digitalWrite()'s look-ups (SoftWire's clock). Where the pads have no
 * open drain (X035 / X033) low is a push-pull 0 and high an input, as
 * digitalWrite() does there. */
static inline void ch32rv_gpio_od_write(uint8_t port, uint8_t bit, int high)
{
#if CH32RV_GPIO_NO_OPEN_DRAIN
    /* The pad is the GPIO's since pinMode(): only the nibble here. SoftWire's target answers within SCL's low
     * from this (X035 at 48 MHz): the CC pad check of ch32rv_gpio_set_config() in it made the reads too late
     * (softwire::test_wire_slave, 2026-11-20). */
    if (high) {
        ch32rv_gpio_write_config(port, bit, CH32RV_GPIO_CFG_IN_FLOAT);
    } else {
        ch32rv_gpio_clear(port, bit);
        ch32rv_gpio_write_config(port, bit, CH32RV_GPIO_CFG_OUT_PP_10M);
    }
#else
    if (high) {
        ch32rv_gpio_set(port, bit);
    } else {
        ch32rv_gpio_clear(port, bit);
    }
#endif
}

#ifdef __cplusplus
}
#endif
