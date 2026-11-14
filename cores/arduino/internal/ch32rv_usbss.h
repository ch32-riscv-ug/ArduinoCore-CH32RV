/* The USBSS controller (USB 3.0 SuperSpeed; CH32H417 / H416, and CH32X315
 * device only): where it sits, its two interrupts, its clocks and its PHY.
 * Shared by the device driver (CH32RVUsbDcdUsbss.cpp) and the host driver
 * (CH32RVUsbHcdUsbss.cpp), which run one role at a time on it: the same
 * registers, HOST_MODE in USB_CONTROL telling them apart (RM chapter 27).
 *
 *   CH32RV_USBSS_BASE                  the registers (LINK from 0x00, USB from 0x70)
 *   ch32rv_usbss_irq_hook[0] / [1]     what the core's USBSS / USBSS_LINK
 *                                      handlers call; the role that opens the
 *                                      controller sets both
 *   ch32rv_usbss_clock_on()            HSE, the USBSS PLL, the bus clock and
 *                                      PIPE (and UTMI on H41x); false when the
 *                                      crystal or the PLL does not come up
 *   ch32rv_usbss_clock_ok()            all of them still there (SystemInit()
 *                                      run again by a probe takes them away;
 *                                      callers check from their task)
 *   ch32rv_usbss_clock_off()           the bus clock, PIPE and the PLL off
 *   ch32rv_usbss_phy_setup()           the PHY values of the series' EVT
 *   CH32RV_USBSS_LINK_DEEMPH           the LINK_CFG de-emphasis this core uses
 *   CH32RV_USBSS_LINK_CTRL_EXTRA       series bits of LINK_CTRL */
#pragma once

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32X315)

#include <Arduino.h>

#include "internal/ch32rv_registers.h"

#ifdef __cplusplus
extern "C" {
#endif
extern void (*volatile ch32rv_usbss_irq_hook[2])(void);
#ifdef __cplusplus
}
#endif

#define CH32RV_USBSS_HOOK_USB  0
#define CH32RV_USBSS_HOOK_LINK 1

#if defined(CH32RV_SERIES_CH32X315)
#define CH32RV_USBSS_BASE 0x40038000u
#else
#define CH32RV_USBSS_BASE 0x40034000u
#endif

/* RCC: the USBSS PLL takes HSE (its reference select defaults to the
 * evaluation boards' crystal, 25 MHz on H41x, 20 MHz on X315). H41x: PLL in
 * RCC_CTLR 22 / 23, PIPE and UTMI in CFGR0 22 / 23. X315: PLL in RCC_CTLR
 * 26 / 27, PIPE in RCC_CTLR 28, no UTMI. */
#define CH32RV_USBSS_CTLR_HSEON  (1u << 16)
#define CH32RV_USBSS_CTLR_HSERDY (1u << 17)
#if defined(CH32RV_SERIES_CH32X315)
#define CH32RV_USBSS_CTLR_PLLON  (1u << 26)
#define CH32RV_USBSS_CTLR_PLLRDY (1u << 27)
#define CH32RV_USBSS_PIPE_REG    CH32RV_RCC_CTLR
#define CH32RV_USBSS_PIPE_BITS   (1u << 28)
#define CH32RV_USBSS_LINK_DEEMPH     0u                  /* 6 dB */
#define CH32RV_USBSS_LINK_CTRL_EXTRA 0x00000400u         /* LINK_U3_WKUP_EN */
#else
#define CH32RV_USBSS_CTLR_PLLON  (1u << 22)
#define CH32RV_USBSS_CTLR_PLLRDY (1u << 23)
#define CH32RV_USBSS_PIPE_REG    CH32RV_RCC_CFGR0
#define CH32RV_USBSS_PIPE_BITS   ((1u << 22) | (1u << 23))   /* PIPE, UTMI */
/* 3.5 dB, the USB 3 Gen 1 value. The EVT writes the whole field (0x300, not
 * a value the RM lists); with it some trainings came up with tens of link
 * errors a second at the hub's port, and the microphone lost a packet to a
 * transaction error every few hundred milliseconds. At 3.5 dB none of 30
 * trainings did; 6 dB enumerated late twice in 10 (bench, 6-2 behind a
 * Realtek hub, 2026-11-14). */
#define CH32RV_USBSS_LINK_DEEMPH     0x00000100u
#define CH32RV_USBSS_LINK_CTRL_EXTRA 0u
#endif

static inline bool ch32rv_usbss_wait_ctlr(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

static inline bool ch32rv_usbss_clock_on(void)
{
    CH32RV_RCC_CTLR |= CH32RV_USBSS_CTLR_HSEON;
    if (!ch32rv_usbss_wait_ctlr(CH32RV_USBSS_CTLR_HSERDY)) {
        return false;                                    /* no crystal */
    }
    CH32RV_RCC_CTLR |= CH32RV_USBSS_CTLR_PLLON;
    if (!ch32rv_usbss_wait_ctlr(CH32RV_USBSS_CTLR_PLLRDY)) {
        return false;
    }
    ch32rv_clock_enable(USBSS);
    CH32RV_USBSS_PIPE_REG |= CH32RV_USBSS_PIPE_BITS;
    return true;
}

static inline bool ch32rv_usbss_clock_ok(void)
{
    const uint32_t ready = CH32RV_USBSS_CTLR_HSERDY | CH32RV_USBSS_CTLR_PLLRDY;
    return (CH32RV_RCC_CTLR & ready) == ready &&
           (CH32RV_USBSS_PIPE_REG & CH32RV_USBSS_PIPE_BITS) == CH32RV_USBSS_PIPE_BITS;
}

static inline void ch32rv_usbss_clock_off(void)
{
    CH32RV_USBSS_PIPE_REG &= ~CH32RV_USBSS_PIPE_BITS;
    ch32rv_clock_disable(USBSS);
    CH32RV_RCC_CTLR &= ~CH32RV_USBSS_CTLR_PLLON;
}

/* The PHY window (EVT USBSS_PHY_Cfg: 0x1F8 / 0x1FC, undocumented), and on
 * H41x a trim word at 0x5003C018. */
static inline void ch32rv_usbss_phy_cfg(uint8_t addr, uint16_t data)
{
    CH32RV_REG32(CH32RV_USBSS_BASE + 0x1F8u) = (1u << 23) | ((uint32_t)addr << 16) | data;
    CH32RV_REG32(CH32RV_USBSS_BASE + 0x1FCu) = 0x01u;
    (void)CH32RV_REG32(CH32RV_USBSS_BASE + 0x1FCu);
}

static inline void ch32rv_usbss_phy_setup(void)
{
#if defined(CH32RV_SERIES_CH32X315)
    ch32rv_usbss_phy_cfg(0x0C, 0x285D);
    ch32rv_usbss_phy_cfg(0x11, 0x0501);
    ch32rv_usbss_phy_cfg(0x10, 0xED29);
    ch32rv_usbss_phy_cfg(0x0D, 0xBAAA);
#else
    ch32rv_usbss_phy_cfg(0x03, 0x7C12);
    ch32rv_usbss_phy_cfg(0x0D, 0x79AA);
    ch32rv_usbss_phy_cfg(0x15, 0x4430);
    ch32rv_usbss_phy_cfg(0x13, 0x0010);
    ch32rv_usbss_phy_cfg(0x11, 0x0501);
    CH32RV_REG32(0x5003C018u) = 0xB0054000u;
#endif
}

#endif
