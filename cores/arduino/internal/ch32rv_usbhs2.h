/* The second-generation USBHS (USB 2.0 high speed; CH32H41x, CH32V205,
 * CH32X305 / X315, CH32V407 / V467 with two): where each controller sits, its
 * interrupt, and its clock and pads per series. Shared by the device driver
 * (CH32RVUsbDcdUsbhs2.cpp) and the host driver (CH32RVUsbHcdUsbhs2.cpp), which
 * run one role at a time on a controller: the device registers from its base,
 * the host's from base + 0x100 (EVT USBHSD / USBHSH).
 *
 *   CH32RV_USBHS2_UNITS             1, or 2 on V407 / V467
 *   CH32RV_USBHS2_BASE1 / _IRQN1    the first (USBHS, or V407's USBHS1)
 *   CH32RV_USBHS2_BASE2 / _IRQN2    V407 / V467's USBHS2
 *   ch32rv_usbhs2_irq_hook[unit-1]  what the core's handler calls; the role
 *                                   that opens the controller sets it
 *   ch32rv_usbhs2_clock_ok(unit)    the PHY clock is there (SystemInit() run
 *                                   again by a probe takes it away; callers
 *                                   check from their task)
 *   ch32rv_usbhs2_clock_on(unit)    make it and turn the controller's clock on;
 *                                   false when it cannot be made
 *   ch32rv_usbhs2_pads_on(unit)     hand the pads over (H41x: PB8 / PB9 are
 *                                   SWCLK / SWDIO, and SDI goes off) */
#pragma once

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || \
    defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)

#include <Arduino.h>

#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void (*volatile ch32rv_usbhs2_irq_hook[2])(void);
#ifdef __cplusplus
}
#endif

#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define CH32RV_USBHS2_UNITS 2
#define CH32RV_USBHS2_BASE1 0x40024000u
#define CH32RV_USBHS2_IRQN1 CH32RV_IRQN_USBHS1
#define CH32RV_USBHS2_BASE2 0x40023400u
#define CH32RV_USBHS2_IRQN2 CH32RV_IRQN_USBHS2
#elif defined(CH32RV_SERIES_CH32V205)
#define CH32RV_USBHS2_UNITS 1
#define CH32RV_USBHS2_BASE1 0x40023400u
#define CH32RV_USBHS2_IRQN1 CH32RV_IRQN_USBHS
#else
#define CH32RV_USBHS2_UNITS 1
#define CH32RV_USBHS2_BASE1 0x40030000u
#define CH32RV_USBHS2_IRQN1 CH32RV_IRQN_USBHS
#endif

#define CH32RV_USBHS2_CTLR_HSEON  (1u << 16)
#define CH32RV_USBHS2_CTLR_HSERDY (1u << 17)

static inline bool ch32rv_usbhs2_wait_ctlr(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
/* The USBHS PLL from HSE (25 MHz reference), UTMI (EVT USBHS_RCC_Init). */
#define CH32RV_USBHS2_PLLON      (1u << 20)
#define CH32RV_USBHS2_PLLRDY     (1u << 21)
#define CH32RV_USBHS2_UTMION     (1u << 23)          /* RCC_CFGR0 */
#define CH32RV_USBHS2_PLLCFGR2   CH32RV_REG32(0x40021034u)

static inline bool ch32rv_usbhs2_clock_ok(uint8_t unit)
{
    (void)unit;
    return (CH32RV_RCC_CTLR & CH32RV_USBHS2_PLLRDY) && (CH32RV_RCC_CFGR0 & CH32RV_USBHS2_UTMION);
}

static inline bool ch32rv_usbhs2_clock_on(uint8_t unit)
{
    (void)unit;
    if (!(CH32RV_RCC_CTLR & CH32RV_USBHS2_PLLRDY)) {
        CH32RV_RCC_CTLR |= CH32RV_USBHS2_CTLR_HSEON;
        if (!ch32rv_usbhs2_wait_ctlr(CH32RV_USBHS2_CTLR_HSERDY)) {
            return false;
        }
        CH32RV_RCC_CTLR &= ~CH32RV_USBHS2_PLLON;
        CH32RV_USBHS2_PLLCFGR2 &= ~0x00001F0Fu;         /* HSE, 25 MHz reference, no input division */
        CH32RV_RCC_CTLR |= CH32RV_USBHS2_PLLON;
        if (!ch32rv_usbhs2_wait_ctlr(CH32RV_USBHS2_PLLRDY)) {
            return false;
        }
    }
    CH32RV_RCC_CFGR0 |= CH32RV_USBHS2_UTMION;
    ch32rv_clock_enable(USBHS);
    return true;
}

/* PB8 / PB9 are SWCLK / SWDIO: AFIO_PCFR1.SW_CFG = 100b (the EVT's
 * GPIO_Remap_SWJ_Disable) hands them to the PHY. */
static inline void ch32rv_usbhs2_pads_on(uint8_t unit)
{
    (void)unit;
    CH32RV_REG32(CH32RV_AFIO_BASE + 0x00u) = (CH32RV_REG32(CH32RV_AFIO_BASE + 0x00u) & ~(7u << 24)) | (4u << 24);
}

#elif defined(CH32RV_SERIES_CH32V205)
/* The USBHS PLL from HSE with an 8 MHz reference (EVT USBHS_RCC_Init):
 * RCC_CTLR 22 / 23, RCC_CFGR2 (+0x2C) 27 source, 29:28 reference, 26:24
 * input division. High speed needs the crystal. */
#define CH32RV_USBHS2_PLLON  (1u << 22)
#define CH32RV_USBHS2_PLLRDY (1u << 23)
#define CH32RV_USBHS2_CFGR2  CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)

static inline bool ch32rv_usbhs2_clock_ok(uint8_t unit)
{
    (void)unit;
    return (CH32RV_RCC_CTLR & CH32RV_USBHS2_PLLRDY) && !(CH32RV_USBHS2_CFGR2 & (1u << 27));
}

static inline bool ch32rv_usbhs2_clock_on(uint8_t unit)
{
    if (!ch32rv_usbhs2_clock_ok(unit)) {
        CH32RV_RCC_CTLR |= CH32RV_USBHS2_CTLR_HSEON;
        if (!ch32rv_usbhs2_wait_ctlr(CH32RV_USBHS2_CTLR_HSERDY)) {
            return false;
        }
        CH32RV_RCC_CTLR &= ~CH32RV_USBHS2_PLLON;
        CH32RV_USBHS2_CFGR2 = (CH32RV_USBHS2_CFGR2 & ~((3u << 28) | (1u << 27) | (7u << 24))) | (1u << 28);
        CH32RV_RCC_CTLR |= CH32RV_USBHS2_PLLON;
        if (!ch32rv_usbhs2_wait_ctlr(CH32RV_USBHS2_PLLRDY)) {
            return false;
        }
    }
    ch32rv_clock_enable(USBHS);
    return true;
}

static inline void ch32rv_usbhs2_pads_on(uint8_t unit) { (void)unit; }

#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
/* UTMI (RCC_CFGR0 23) and the controller clock; without HSE the PHY runs from
 * HSI kept in step with the host's SOF by USBHSI (base + 0x200: CAL_RST 0,
 * CAL_EN 1, SOF_FREE 3) - EVT USBHS_RCC_Init. That calibration follows a
 * host's SOF, so a host on this controller needs HSE. */
#define CH32RV_USBHS2_UTMION (1u << 23)
#define CH32RV_USBHS2_CAL_CR CH32RV_REG32(0x40030200u)

static inline bool ch32rv_usbhs2_clock_ok(uint8_t unit)
{
    (void)unit;
    return (CH32RV_RCC_CFGR0 & CH32RV_USBHS2_UTMION) != 0;
}

static inline bool ch32rv_usbhs2_clock_on(uint8_t unit)
{
    (void)unit;
    CH32RV_RCC_CFGR0 |= CH32RV_USBHS2_UTMION;
    ch32rv_clock_enable(USBHS);
    if (!(CH32RV_RCC_CTLR & CH32RV_USBHS2_CTLR_HSERDY)) {
        CH32RV_USBHS2_CAL_CR |= 0x01u;
        CH32RV_USBHS2_CAL_CR &= ~0x01u;
        CH32RV_USBHS2_CAL_CR |= 0x02u | 0x08u;
    }
    return true;
}

static inline void ch32rv_usbhs2_pads_on(uint8_t unit) { (void)unit; }

#else /* CH32V407 / V467 */
/* One USBHS PLL for both: HSE with a 25 MHz reference, or HSI with 20 MHz
 * (EVT usbhsN_dcp.c), RCC_CTLR 20 / 21, RCC_CFGR2 (+0x2C) 27:26 source,
 * 29:28 reference; each controller its UTMI gate, USBHS1 at CFGR2 30 and
 * USBHS2 at 31 (RM; the EVT header has the two names swapped, its functions
 * do not).
 *
 * High speed needs the crystal, and the CPU clock may run from HSI with HSE
 * off, so it is started here. From HSI the link comes up at high speed and
 * short packets go, but a full one does not (V407 with an ESP32-P4 as the
 * host, 2026-11-11: up to 36 bytes went, every 512-byte IN was a transaction
 * error at the host). HSI only on a board without a crystal. */
#define CH32RV_USBHS2_PLLON      (1u << 20)
#define CH32RV_USBHS2_PLLRDY     (1u << 21)
#define CH32RV_USBHS2_CFGR2      CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)
#define CH32RV_USBHS2_PLL_SOURCE ((3u << 28) | (3u << 26))
#define CH32RV_USBHS2_PLL_HSI    ((1u << 28) | (1u << 26))

static inline uint32_t ch32rv_usbhs2_utmi(uint8_t unit)
{
    return unit == 1 ? (1u << 30) : (1u << 31);
}

static inline uint32_t ch32rv_usbhs2_pll_source(void)
{
    return (CH32RV_RCC_CTLR & CH32RV_USBHS2_CTLR_HSERDY) ? 0u : CH32RV_USBHS2_PLL_HSI;
}

/* HSE off again (SystemInit() after a probe's attach) shows as the PLL still
 * set to it: on again from the task. */
static inline bool ch32rv_usbhs2_clock_ok(uint8_t unit)
{
    return (CH32RV_RCC_CTLR & CH32RV_USBHS2_PLLRDY) && (CH32RV_USBHS2_CFGR2 & ch32rv_usbhs2_utmi(unit)) &&
           (CH32RV_USBHS2_CFGR2 & CH32RV_USBHS2_PLL_SOURCE) == ch32rv_usbhs2_pll_source();
}

static inline bool ch32rv_usbhs2_clock_on(uint8_t unit)
{
    if (!(CH32RV_RCC_CTLR & CH32RV_USBHS2_CTLR_HSERDY)) {
        CH32RV_RCC_CTLR |= CH32RV_USBHS2_CTLR_HSEON;
        if (!ch32rv_usbhs2_wait_ctlr(CH32RV_USBHS2_CTLR_HSERDY)) {
            CH32RV_RCC_CTLR &= ~CH32RV_USBHS2_CTLR_HSEON;   /* no crystal */
        }
    }
    const uint32_t source = ch32rv_usbhs2_pll_source();
    if (!(CH32RV_RCC_CTLR & CH32RV_USBHS2_PLLRDY) || (CH32RV_USBHS2_CFGR2 & CH32RV_USBHS2_PLL_SOURCE) != source) {
        CH32RV_RCC_CTLR &= ~CH32RV_USBHS2_PLLON;          /* the source is writable only while it is off */
        CH32RV_USBHS2_CFGR2 = (CH32RV_USBHS2_CFGR2 & ~CH32RV_USBHS2_PLL_SOURCE) | source;
        CH32RV_RCC_CTLR |= CH32RV_USBHS2_PLLON;
        if (!ch32rv_usbhs2_wait_ctlr(CH32RV_USBHS2_PLLRDY)) {
            return false;
        }
    }
    CH32RV_USBHS2_CFGR2 |= ch32rv_usbhs2_utmi(unit);
    if (unit == 1) {
        ch32rv_clock_enable(USBHS1);
    } else {
        ch32rv_clock_enable(USBHS2);
    }
    return true;
}

static inline void ch32rv_usbhs2_pads_on(uint8_t unit) { (void)unit; }
#endif

#endif
