/* The first-generation USBHS (USB 2.0 high speed; CH32V305 / V307 / V317):
 * its interrupt and the PHY's clock. Shared by the device driver
 * (CH32RVUsbDcdUsbhs1.cpp) and the host driver (CH32RVUsbHcdUsbhs1.cpp),
 * which run one role at a time on the controller.
 *
 *   CH32RV_USBHS1_BASE                  0x40023400 (EVT USBHS_BASE)
 *   ch32rv_usbhs1_irq_hook              what the core's USBHS_IRQHandler calls;
 *                                       the role that opens the controller sets it
 *   ch32rv_usbhs1_clock_ok()            the PHY's PLL is alive (SystemInit() run
 *                                       again by a probe takes it away; callers
 *                                       check from their task)
 *   ch32rv_usbhs1_clock_on(hse_hz)      HSE on and the PHY's PLL from it; false
 *                                       when there is no crystal or no reference
 *                                       divides from `hse_hz`
 *   ch32rv_usbhs1_clock_off()
 *
 * The PHY needs HSE: its PLL takes a 3, 4, 5 or 8 MHz reference divided from
 * the crystal (RM: "the clock source of USBHSPLL can only be HSE"). RCC_CFGR2
 * (+0x2C): USBHSDIV 26:24 (HSE / (n + 1)), USBHSPLLSRC 27 (0 = HSE),
 * USBHSCKREF 29:28 (00 3 MHz, 01 4 MHz, 10 8 MHz, 11 5 MHz), USBHSPLL_ALIVE
 * 30. Pads PB6 / PB7 belong to the PHY. */
#pragma once

#if defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)

#include <Arduino.h>

#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void (*volatile ch32rv_usbhs1_irq_hook)(void);
#ifdef __cplusplus
}
#endif

#define CH32RV_USBHS1_BASE        0x40023400u
#define CH32RV_USBHS1_HSEON       (1u << 16)
#define CH32RV_USBHS1_HSERDY      (1u << 17)
#define CH32RV_USBHS1_CFGR2       CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)
#define CH32RV_USBHS1_CFGR2_MASK  0x7F000000u                /* 30:24, not USBFSSRC 31 */
#define CH32RV_USBHS1_CFGR2_ALIVE (1u << 30)

/* The CFGR2 bits for a crystal of `hse_hz`, 0 when no reference divides from it. */
static inline uint32_t ch32rv_usbhs1_pll_bits(uint32_t hse_hz)
{
    static const struct {
        uint32_t hz;
        uint32_t sel;
    } refs[] = {{4000000u, 1u}, {8000000u, 2u}, {5000000u, 3u}, {3000000u, 0u}};
    for (unsigned r = 0; r < sizeof refs / sizeof refs[0]; r++) {
        for (uint32_t div = 1; div <= 8; div++) {
            if (hse_hz == refs[r].hz * div) {
                return CH32RV_USBHS1_CFGR2_ALIVE | (refs[r].sel << 28) | ((div - 1u) << 24);
            }
        }
    }
    return 0;
}

static inline bool ch32rv_usbhs1_clock_ok(void)
{
    return (CH32RV_RCC_CTLR & CH32RV_USBHS1_HSERDY) && (CH32RV_USBHS1_CFGR2 & CH32RV_USBHS1_CFGR2_ALIVE);
}

static inline bool ch32rv_usbhs1_clock_on(uint32_t hse_hz)
{
    const uint32_t pll = ch32rv_usbhs1_pll_bits(hse_hz);
    if (!pll) {
        return false;
    }
    if (!(CH32RV_RCC_CTLR & CH32RV_USBHS1_HSERDY)) {
        CH32RV_RCC_CTLR |= CH32RV_USBHS1_HSEON;
        for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & CH32RV_USBHS1_HSERDY);) {
            if ((uint32_t)(millis() - t0) > 20u) {
                CH32RV_RCC_CTLR &= ~CH32RV_USBHS1_HSEON;
                return false;                            /* no crystal */
            }
        }
    }
    CH32RV_USBHS1_CFGR2 = (CH32RV_USBHS1_CFGR2 & ~CH32RV_USBHS1_CFGR2_MASK) | pll;
    ch32rv_clock_enable(USBHS);
    return true;
}

static inline void ch32rv_usbhs1_clock_off(void)
{
    ch32rv_clock_disable(USBHS);
    CH32RV_USBHS1_CFGR2 &= ~CH32RV_USBHS1_CFGR2_ALIVE;
}

#endif
