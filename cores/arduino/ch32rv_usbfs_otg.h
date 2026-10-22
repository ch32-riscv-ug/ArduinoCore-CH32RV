/* The USBFS of the USBOTG_FS layout (CH32V20x, V30x, V205, L103 / M103, M030,
 * H41x): where it sits, and its 48 MHz and pads per series. Shared by the
 * device driver (CH32RVUsbDcdUsbfsOtg.cpp) and the host (CH32RVUsbHost), which
 * run one at a time on the one block.
 *
 *   CH32RV_USBFS_OTG_BASE          the block, where this series has it
 *   ch32rv_usbfs_otg_clock_ok()    the 48 MHz is there (SystemInit() run again
 *                                  by a probe turns it off; callers check)
 *   ch32rv_usbfs_otg_clock_on()    make it, turn the block's clock on and hand
 *                                  the pads over; false when it cannot be made */
#pragma once

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || \
    defined(CH32RV_SERIES_CH32M030)

#include <Arduino.h>

#include "ch32rv_registers.h"

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32M030)
#define CH32RV_USBFS_OTG_BASE 0x40023400u
#else
#define CH32RV_USBFS_OTG_BASE 0x50000000u
#endif

static inline bool ch32rv_usbfs_otg_wait_ctlr(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
/* 48 MHz = USBHS PLL (480 MHz from HSE, 25 MHz reference) / 10, selected in
 * CH32RV_USBFS_OTG_RCC_CFGR2 (USBFSSRC bit 20, USBFSDIV 19:16 = 7 for /10). PA11 / PA12 need
 * nothing but the port clock (EVT USBFS_RCC_Init). */
#define CH32RV_USBFS_OTG_RCC_CTLR_HSEON        (1u << 16)
#define CH32RV_USBFS_OTG_RCC_CTLR_HSERDY       (1u << 17)
#define CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLON  (1u << 20)
#define CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY (1u << 21)
#define CH32RV_USBFS_OTG_RCC_CFGR2             CH32RV_REG32(0x40021030u)
#define CH32RV_USBFS_OTG_RCC_PLLCFGR2          CH32RV_REG32(0x40021034u)
#define CH32RV_USBFS_OTG_USB_CLKEN()           (ch32rv_clock_enable(OTG_FS), ch32rv_clock_enable(GPIOA))

static inline bool ch32rv_usbfs_otg_clock_ok(void)
{
    return (CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY) &&
           (CH32RV_USBFS_OTG_RCC_CFGR2 & 0x001F0000u) == ((1u << 20) | (7u << 16));
}

static inline bool ch32rv_usbfs_otg_clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY)) {
        CH32RV_RCC_CTLR |= CH32RV_USBFS_OTG_RCC_CTLR_HSEON;
        if (!ch32rv_usbfs_otg_wait_ctlr(CH32RV_USBFS_OTG_RCC_CTLR_HSERDY)) {
            return false;                                /* no crystal */
        }
        /* Source HSE, reference 25 MHz, no input division: writable only
         * while the PLL is off. */
        CH32RV_RCC_CTLR &= ~CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLON;
        CH32RV_USBFS_OTG_RCC_PLLCFGR2 &= ~0x00001F0Fu;
        CH32RV_RCC_CTLR |= CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLON;
        if (!ch32rv_usbfs_otg_wait_ctlr(CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY)) {
            return false;
        }
    }
    CH32RV_USBFS_OTG_RCC_CFGR2 = (CH32RV_USBFS_OTG_RCC_CFGR2 & ~0x001F0000u) | (1u << 20) | (7u << 16);
    CH32RV_USBFS_OTG_USB_CLKEN();
    return true;
}

#elif defined(CH32RV_SERIES_CH32V205)
/* 48 MHz from the USBHS PLL with an 8 MHz reference, fed by HSE when one runs,
 * else by HSI (EVT USBFS_RCC_Init); CH32RV_USBFS_OTG_RCC_CFGR2 (+0x2C) USBFSSRC bit 31 picks it.
 * The SYSCLK may stay where the board put it. PA11 / PA12. */
#define CH32RV_USBFS_OTG_RCC_CTLR_HSERDY       (1u << 17)
#define CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLON  (1u << 22)
#define CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY (1u << 23)
#define CH32RV_USBFS_OTG_RCC_CFGR2             CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)
#define CH32RV_USBFS_OTG_CFGR2_USBFSSRC_HSPLL  (1u << 31)
#define CH32RV_USBFS_OTG_CFGR2_HSPLL_REF_8M    (1u << 28)
#define CH32RV_USBFS_OTG_CFGR2_HSPLL_REF_MASK  (3u << 28)
#define CH32RV_USBFS_OTG_CFGR2_HSPLL_SRC_HSI   (1u << 27)
#define CH32RV_USBFS_OTG_CFGR2_HSPLL_DIV_MASK  (7u << 24)
#define CH32RV_USBFS_OTG_USB_CLKEN()           ch32rv_clock_enable(USBFS)

static inline bool ch32rv_usbfs_otg_clock_ok(void)
{
    return (CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY) && (CH32RV_USBFS_OTG_RCC_CFGR2 & CH32RV_USBFS_OTG_CFGR2_USBFSSRC_HSPLL);
}

static inline bool ch32rv_usbfs_otg_clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY)) {
        CH32RV_RCC_CTLR &= ~CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLON;
        CH32RV_USBFS_OTG_RCC_CFGR2 = (CH32RV_USBFS_OTG_RCC_CFGR2 & ~(CH32RV_USBFS_OTG_CFGR2_HSPLL_REF_MASK | CH32RV_USBFS_OTG_CFGR2_HSPLL_SRC_HSI | CH32RV_USBFS_OTG_CFGR2_HSPLL_DIV_MASK)) |
                    CH32RV_USBFS_OTG_CFGR2_HSPLL_REF_8M | ((CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_HSERDY) ? 0u : CH32RV_USBFS_OTG_CFGR2_HSPLL_SRC_HSI);
        CH32RV_RCC_CTLR |= CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLON;
        if (!ch32rv_usbfs_otg_wait_ctlr(CH32RV_USBFS_OTG_RCC_CTLR_USBHS_PLLRDY)) {
            return false;
        }
    }
    CH32RV_USBFS_OTG_RCC_CFGR2 |= CH32RV_USBFS_OTG_CFGR2_USBFSSRC_HSPLL;
    CH32RV_USBFS_OTG_USB_CLKEN();
    return true;
}

#elif defined(CH32RV_SERIES_CH32M030)
/* The PLL is HSI x 18 = 144 MHz, fixed, and its /3 is the USBFS clock (RM
 * figure 3-2). It runs here whatever SYSCLK is: turned on, never switched to. */
#define CH32RV_USBFS_OTG_RCC_CTLR_PLLON  (1u << 24)
#define CH32RV_USBFS_OTG_RCC_CTLR_PLLRDY (1u << 25)
#define CH32RV_USBFS_OTG_USB_CLKEN()     ch32rv_clock_enable(USBFS)

static inline bool ch32rv_usbfs_otg_clock_ok(void)
{
    return (CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_PLLRDY) != 0;
}

static inline bool ch32rv_usbfs_otg_clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & CH32RV_USBFS_OTG_RCC_CTLR_PLLRDY)) {
        CH32RV_RCC_CFGR0 &= ~(1u << 16);                 /* PLLSRC: HSI */
        CH32RV_RCC_CTLR |= CH32RV_USBFS_OTG_RCC_CTLR_PLLON;
        if (!ch32rv_usbfs_otg_wait_ctlr(CH32RV_USBFS_OTG_RCC_CTLR_PLLRDY)) {
            return false;
        }
    }
    CH32RV_USBFS_OTG_USB_CLKEN();
    return true;
}

#else
/* V20x, V30x, L103 / M103: the 48 MHz is the PLL divided by USBPRE
 * (RCC_CFGR0 23:22), so it exists only when SYSCLK is that PLL at a multiple
 * of 48 MHz. The encodings differ: V20x / V30x 00 /1, 01 /2, 10 /3; L103
 * 00 /1, 01 /2, 10 /1.5 (EVT RCC_USBCLKConfig). At a CPU Clock that is none
 * of these (8 MHz, or V20x's 120 MHz for Ethernet) begin() fails. */
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define CH32RV_USBFS_OTG_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define CH32RV_USBFS_OTG_USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000 && \
    !(defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103))
#define CH32RV_USBFS_OTG_USBPRE 2u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 72000000 && \
    (defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103))
#define CH32RV_USBFS_OTG_USBPRE 2u
#endif
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)
#define CH32RV_USBFS_OTG_DP_PIN PB7          /* USBFS sits on PB6 / PB7 there */
#define CH32RV_USBFS_OTG_DM_PIN PB6
#else
#define CH32RV_USBFS_OTG_DP_PIN PA12
#define CH32RV_USBFS_OTG_DM_PIN PA11
#endif
#define CH32RV_USBFS_OTG_USB_CLKEN() ch32rv_clock_enable(USBFS)

static inline bool ch32rv_usbfs_otg_clock_ok(void)
{
#ifdef CH32RV_USBFS_OTG_USBPRE
    return ((CH32RV_RCC_CFGR0 >> 22) & 3u) == CH32RV_USBFS_OTG_USBPRE;
#else
    return false;
#endif
}

static inline bool ch32rv_usbfs_otg_clock_on(void)
{
#ifdef CH32RV_USBFS_OTG_USBPRE
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << 22)) | (CH32RV_USBFS_OTG_USBPRE << 22);
    pinMode(CH32RV_USBFS_OTG_DM_PIN, INPUT);
    pinMode(CH32RV_USBFS_OTG_DP_PIN, INPUT);
    CH32RV_USBFS_OTG_USB_CLKEN();
    return true;
#else
    return false;
#endif
}
#endif

#endif
