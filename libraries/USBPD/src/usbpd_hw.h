/* usbpd_hw.h - where the USBPD block lives, per part. HAND-WRITTEN, TEMPORARY.
 *
 * These facts belong in the generated variant headers: upstream
 * ch32-device-data already carries them (memory_map.csv says the block sits
 * at 0x40027000 on X035/L103/V205 and 0x40024400 on H417/X315; pin_roles.csv
 * names the CC pads per series) - but those tables entered the data set
 * after the commit this repository is pinned to, so the defines cannot be
 * generated yet. Until the next adoption, this file carries the one family
 * that is being brought up. Everything here is spelled the same way the
 * generator will spell it, so the swap is a deletion.
 *
 * Register layout and bit names follow the EVT's ch32x035_usbpd.h (reference
 * only), with this core's prefix convention.
 */
#pragma once

#include <stdint.h>

#include "ch32_registers.h"
#include "pins_arduino.h"

/* ---- which parts have the block wired up here ----------------------------
 * X033 shares the X035 die and placement. L103/V205 (same base) and
 * X315/H417 (0x40024400) are not brought up yet: their AFIO/RCC plumbing
 * differs and nothing has been checked - absence here is deliberate. */
#if defined(CH32_VARIANT_CH32X035) || defined(CH32_VARIANT_CH32X033)
#define CH32_USBPD_BASE      0x40027000u
/* The clock enable comes from the variant now that clock_enables.csv exists
 * (usbpd_plumbing.csv confirms the same bit); the literal stays as the
 * fallback for a variant generated before the table. */
#ifdef CH32_CLKEN_USBPD_ADDR
#define CH32_USBPD_CLKEN_ADDR CH32_CLKEN_USBPD_ADDR
#define CH32_USBPD_CLKEN_MASK CH32_CLKEN_USBPD_MASK
#else
#define CH32_USBPD_CLKEN_ADDR 0x40021014u   /* RCC AHBPCENR               */
#define CH32_USBPD_CLKEN_MASK (1u << 17)    /* RCC_AHBPeriph_USBPD        */
#endif
#define CH32_USBPD_IRQ       CH32_IRQN_USBPD
/* AFIO_CTLR bits the PHY needs. IN_HVT raises the CC input threshold while
 * PD is in use; PHY_V33 says VDD is 3.3 V so the pull-up limiter can be
 * bypassed (the EVT sets both on a 3.3 V system). */
#define CH32_USBPD_AFIO_CTLR CH32_REG32(CH32_AFIO_BASE + 0x18u)
#define CH32_USBPD_IN_HVT    (1u << 9)
#define CH32_USBPD_PHY_V33   (1u << 8)
#endif

#ifdef CH32_USBPD_BASE

/* ---- registers ------------------------------------------------------------
 * A 16-bit-heavy block: CONFIG carries the interrupt enables, STATUS the
 * flags (write 1 to clear), and the BMC engine is pointed at RAM through DMA
 * and told the bit clock through BMC_CLK_CNT. */
#define CH32_USBPD_CONFIG       CH32_REG16(CH32_USBPD_BASE + 0x00u)
#define CH32_USBPD_BMC_CLK_CNT  CH32_REG16(CH32_USBPD_BASE + 0x02u)
#define CH32_USBPD_CONTROL      CH32_REG8(CH32_USBPD_BASE + 0x04u)
#define CH32_USBPD_TX_SEL       CH32_REG8(CH32_USBPD_BASE + 0x05u)
#define CH32_USBPD_BMC_TX_SZ    CH32_REG16(CH32_USBPD_BASE + 0x06u)
#define CH32_USBPD_STATUS       CH32_REG8(CH32_USBPD_BASE + 0x09u)
#define CH32_USBPD_BMC_BYTE_CNT CH32_REG16(CH32_USBPD_BASE + 0x0Au)
#define CH32_USBPD_PORT_CC1     CH32_REG16(CH32_USBPD_BASE + 0x0Cu)
#define CH32_USBPD_PORT_CC2     CH32_REG16(CH32_USBPD_BASE + 0x0Eu)
#define CH32_USBPD_DMA          CH32_REG32(CH32_USBPD_BASE + 0x10u)

/* CONFIG */
#define CH32_UPD_PD_ALL_CLR   (1u << 1)
#define CH32_UPD_CC_SEL       (1u << 2)    /* 0: talk on CC1, 1: on CC2    */
#define CH32_UPD_PD_DMA_EN    (1u << 3)
#define CH32_UPD_IE_RX_ACT    (1u << 13)
#define CH32_UPD_IE_RX_RESET  (1u << 14)
#define CH32_UPD_IE_TX_END    (1u << 15)

/* CONTROL */
#define CH32_UPD_PD_TX_EN     (1u << 0)
#define CH32_UPD_BMC_START    (1u << 1)

/* STATUS (the flag bits are write-1-to-clear) */
#define CH32_UPD_BMC_AUX_MASK 0x03u        /* what arrived: */
#define CH32_UPD_AUX_SOP0     0x01u        /*   an ordinary message        */
#define CH32_UPD_AUX_HRST     0x02u        /*   a hard reset               */
#define CH32_UPD_IF_RX_ACT    (1u << 5)
#define CH32_UPD_IF_RX_RESET  (1u << 6)
#define CH32_UPD_IF_TX_END    (1u << 7)
#define CH32_UPD_IF_ALL       (CH32_UPD_IF_RX_ACT | CH32_UPD_IF_RX_RESET | \
                               CH32_UPD_IF_TX_END | (1u << 2) | (1u << 3) | \
                               (1u << 4))

/* PORT_CC1 / PORT_CC2 */
#define CH32_UPD_PA_CC_AI     (1u << 0)    /* comparator output            */
#define CH32_UPD_CC_PD        (1u << 1)    /* present Rd (we are a sink)   */
#define CH32_UPD_CC_LVE       (1u << 4)    /* drive the line (transmit)    */
#define CH32_UPD_CC_CMP_MASK  (7u << 5)
#define CH32_UPD_CC_CMP_22    (2u << 5)    /* 0.22 V: "a source is there"  */
#define CH32_UPD_CC_CMP_66    (5u << 5)    /* 0.66 V: idle threshold       */

/* TX_SEL: the K-code preamble to send. SOP for messages, RST for hard reset. */
#define CH32_UPD_SOP0         0x00u        /* SYNC1 SYNC1 SYNC1 SYNC2      */
#define CH32_UPD_HARD_RESET   ((1u << 0) | (2u << 2) | (2u << 4) | (2u << 6))

/* BMC bit clock: HSI ticks per half-bit. F_CPU-derived the way the EVT and
 * every reference implementation derive it. */
#if F_CPU == 48000000
#define CH32_UPD_TMR_TX (80u - 1u)
#define CH32_UPD_TMR_RX (120u - 1u)
#elif F_CPU == 24000000
#define CH32_UPD_TMR_TX (40u - 1u)
#define CH32_UPD_TMR_RX (60u - 1u)
#elif F_CPU == 12000000
#define CH32_UPD_TMR_TX (20u - 1u)
#define CH32_UPD_TMR_RX (30u - 1u)
#else
#error "no BMC clock constants for this F_CPU (48/24/12 MHz are known)"
#endif

#endif /* CH32_USBPD_BASE */
