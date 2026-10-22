/* usbpd_hw.h - where the USBPD block lives, per part. HAND-WRITTEN, TEMPORARY.
 *
 * These facts belong in the generated variant headers: upstream
 * ch32-device-data already carries them (memory_map.csv says the block sits
 * at 0x40027000 on X035/L103/V205 and 0x40024400 on H417/X315; pin_roles.csv
 * names the CC pads per series) - but those tables entered the data set
 * after the commit this repository is pinned to, so the defines cannot be
 * generated yet. Until the next adoption, this file carries them by hand.
 *
 * Register layout and bit names follow the EVT's ch32x035_usbpd.h (reference
 * only), with this core's prefix convention.
 */
#pragma once

#include <stdint.h>

#include <Arduino.h>

#include "ch32rv_gpio.h"
#include "ch32rv_registers.h"
#include "pins_arduino.h"

/* ---- which parts have the block wired up here ----------------------------
 * The block is the same on X035/X033, L103/M103, V205, X305/X315 and H41x
 * (the EVTs' ch32*_usbpd.h agree register for register); what differs is where
 * it sits, the CC pads and how they are routed, and which AFIO bits set the
 * PHY up. X035 is checked on hardware, the others are built only. M030 has two
 * ports and another comparator encoding (CC_CVS, CC_CE) - not here. X033 shares
 * the X035's die but bonds no CC pad (device-data's pinout). */
#if defined(CH32RV_VARIANT_CH32X035)
#define CH32RV_USBPD_BASE      0x40027000u
/* AFIO_CTLR bits the PHY needs. IN_HVT raises the CC input threshold while
 * PD is in use; PHY_V33 says VDD is 3.3 V so the pull-up limiter can be
 * bypassed (the EVT sets both on a 3.3 V system). */
#define CH32RV_USBPD_AFIO_REG  CH32RV_REG32(CH32RV_AFIO_BASE + 0x18u)
#define CH32RV_USBPD_AFIO_CLR  0u
#define CH32RV_USBPD_AFIO_SET  ((1u << 9) | (1u << 8))
/* The CC pads. A source tells an open line (no sink) from a sink's Rd by
 * reading the pad as a GPIO: with Rp on, an open line rises above the input
 * high threshold, a sink's Rd holds it under (the EVT's USBPD_SRC). */
#define CH32RV_USBPD_CC1_PIN   PC14
#define CH32RV_USBPD_CC2_PIN   PC15
#define CH32RV_USBPD_CC_AF     0u          /* the pads are plain inputs */
#define CH32RV_USBPD_FILTER    0u
#elif defined(CH32RV_VARIANT_CH32L103) || defined(CH32RV_VARIANT_CH32M103)
#define CH32RV_USBPD_BASE      0x40027000u
#define CH32RV_USBPD_AFIO_REG  CH32RV_REG32(CH32RV_AFIO_BASE + 0x18u)   /* AFIO_CR */
#define CH32RV_USBPD_AFIO_CLR  0u
#define CH32RV_USBPD_AFIO_SET  (1u << 9)   /* USBPD_IN_HVT; no PHY_V33 bit here */
#define CH32RV_USBPD_CC1_PIN   PB6
#define CH32RV_USBPD_CC2_PIN   PB7
#define CH32RV_USBPD_CC_AF     0u
#define CH32RV_USBPD_FILTER    (1u << 0)   /* PD_FILT_ED: the EVT turns the input filter on */
#elif defined(CH32RV_VARIANT_CH32V205)
/* Lots whose 5th digit is 0 have no USBPD (errata v205-can-usbpd). */
#define CH32RV_USBPD_BASE      0x40027000u
#define CH32RV_USBPD_AFIO_REG  CH32RV_REG32(CH32RV_AFIO_BASE + 0x18u)   /* AFIO_CR */
#define CH32RV_USBPD_AFIO_CLR  0u
#define CH32RV_USBPD_AFIO_SET  (1u << 9)   /* USBPD_IN_HVT */
#define CH32RV_USBPD_CC1_PIN   PA0
#define CH32RV_USBPD_CC2_PIN   PA1
#define CH32RV_USBPD_CC_AF     7u
#define CH32RV_USBPD_FILTER    0u
#elif defined(CH32RV_VARIANT_CH32X305) || defined(CH32RV_VARIANT_CH32X315)
#define CH32RV_USBPD_BASE      0x40024400u
/* AFIO_CR: USBPDHVT (bit 0) and USBPDRISE[2:1] = 2, the EVT's edge rate. */
#define CH32RV_USBPD_AFIO_REG  CH32RV_REG32(CH32RV_AFIO_BASE + 0x18u)
#define CH32RV_USBPD_AFIO_CLR  (3u << 1)
#define CH32RV_USBPD_AFIO_SET  ((1u << 0) | (2u << 1))
#define CH32RV_USBPD_CC1_PIN   PD4
#define CH32RV_USBPD_CC2_PIN   PD5
#define CH32RV_USBPD_CC_AF     2u
#define CH32RV_USBPD_FILTER    (1u << 0)
#elif defined(CH32RV_VARIANT_CH32H417) || defined(CH32RV_VARIANT_CH32H416) || defined(CH32RV_VARIANT_CH32H415)
#define CH32RV_USBPD_BASE      0x40024400u
#define CH32RV_USBPD_AFIO_REG  CH32RV_REG32(CH32RV_AFIO_BASE + 0x00u)   /* AFIO_PCFR1 */
#define CH32RV_USBPD_AFIO_CLR  0u
#define CH32RV_USBPD_AFIO_SET  (1u << 20)  /* USBPD_CC_HVT */
#define CH32RV_USBPD_CC1_PIN   PB3
#define CH32RV_USBPD_CC2_PIN   PB4
#define CH32RV_USBPD_CC_AF     4u
#define CH32RV_USBPD_FILTER    0u
#endif

#ifdef CH32RV_USBPD_BASE
/* The clock enable comes from the variant (clock_enables.csv). */
#define CH32RV_USBPD_CLKEN_ADDR CH32RV_CLKEN_USBPD_ADDR
#define CH32RV_USBPD_CLKEN_MASK CH32RV_CLKEN_USBPD_MASK
#define CH32RV_USBPD_IRQ       CH32RV_IRQN_USBPD
#endif

#ifdef CH32RV_USBPD_BASE

/* ---- registers ------------------------------------------------------------
 * A 16-bit-heavy block: CONFIG carries the interrupt enables, STATUS the
 * flags (write 1 to clear), and the BMC engine is pointed at RAM through DMA
 * and told the bit clock through BMC_CLK_CNT. */
#define CH32RV_USBPD_CONFIG       CH32RV_REG16(CH32RV_USBPD_BASE + 0x00u)
#define CH32RV_USBPD_BMC_CLK_CNT  CH32RV_REG16(CH32RV_USBPD_BASE + 0x02u)
#define CH32RV_USBPD_CONTROL      CH32RV_REG8(CH32RV_USBPD_BASE + 0x04u)
#define CH32RV_USBPD_TX_SEL       CH32RV_REG8(CH32RV_USBPD_BASE + 0x05u)
#define CH32RV_USBPD_BMC_TX_SZ    CH32RV_REG16(CH32RV_USBPD_BASE + 0x06u)
#define CH32RV_USBPD_STATUS       CH32RV_REG8(CH32RV_USBPD_BASE + 0x09u)
#define CH32RV_USBPD_BMC_BYTE_CNT CH32RV_REG16(CH32RV_USBPD_BASE + 0x0Au)
#define CH32RV_USBPD_PORT_CC1     CH32RV_REG16(CH32RV_USBPD_BASE + 0x0Cu)
#define CH32RV_USBPD_PORT_CC2     CH32RV_REG16(CH32RV_USBPD_BASE + 0x0Eu)
#define CH32RV_USBPD_DMA          CH32RV_REG32(CH32RV_USBPD_BASE + 0x10u)

/* CONFIG */
#define CH32RV_UPD_PD_ALL_CLR   (1u << 1)
#define CH32RV_UPD_CC_SEL       (1u << 2)    /* 0: talk on CC1, 1: on CC2    */
#define CH32RV_UPD_PD_DMA_EN    (1u << 3)
#define CH32RV_UPD_IE_RX_ACT    (1u << 13)
#define CH32RV_UPD_IE_RX_RESET  (1u << 14)
#define CH32RV_UPD_IE_TX_END    (1u << 15)

/* CONTROL */
#define CH32RV_UPD_PD_TX_EN     (1u << 0)
#define CH32RV_UPD_BMC_START    (1u << 1)

/* STATUS (the flag bits are write-1-to-clear) */
#define CH32RV_UPD_BMC_AUX_MASK 0x03u        /* what arrived: */
#define CH32RV_UPD_AUX_SOP0     0x01u        /*   an ordinary message        */
#define CH32RV_UPD_AUX_HRST     0x02u        /*   a hard reset               */
#define CH32RV_UPD_IF_RX_ACT    (1u << 5)
#define CH32RV_UPD_IF_RX_RESET  (1u << 6)
#define CH32RV_UPD_IF_TX_END    (1u << 7)
#define CH32RV_UPD_IF_ALL       (CH32RV_UPD_IF_RX_ACT | CH32RV_UPD_IF_RX_RESET | \
                               CH32RV_UPD_IF_TX_END | (1u << 2) | (1u << 3) | \
                               (1u << 4))

/* PORT_CC1 / PORT_CC2 */
#define CH32RV_UPD_PA_CC_AI     (1u << 0)    /* comparator output            */
#define CH32RV_UPD_CC_PD        (1u << 1)    /* present Rd (we are a sink)   */
#define CH32RV_UPD_CC_PU_MASK   (3u << 2)    /* present Rp (we are a source):  */
#define CH32RV_UPD_CC_PU_330    (1u << 2)    /*   330 uA: 3.0 A advertised     */
#define CH32RV_UPD_CC_PU_180    (2u << 2)    /*   180 uA: 1.5 A                */
#define CH32RV_UPD_CC_PU_80     (3u << 2)    /*    80 uA: Default USB power    */
#define CH32RV_UPD_CC_LVE       (1u << 4)    /* drive the line (transmit)    */
#define CH32RV_UPD_CC_CMP_MASK  (7u << 5)
#define CH32RV_UPD_CC_CMP_22    (2u << 5)    /* 0.22 V: "a source is there"  */
#define CH32RV_UPD_CC_CMP_66    (5u << 5)    /* 0.66 V: idle threshold       */

/* The one USBPD interrupt goes to whichever role began (usbpd_irq.cpp);
 * null when none has. A role refuses to begin while the other holds it. */
extern void (*ch32rv_usbpd_irq_hook)(void);

/* TX_SEL: the K-code preamble to send. SOP for messages, RST for hard reset. */
#define CH32RV_UPD_SOP0         0x00u        /* SYNC1 SYNC1 SYNC1 SYNC2      */
#define CH32RV_UPD_HARD_RESET   ((1u << 0) | (2u << 2) | (2u << 4) | (2u << 6))

/* BMC bit clock: HCLK ticks per half-bit when sending (600 kHz, 300 kbit/s)
 * and the receiver's sampling count (400 kHz) - the EVTs' tables at 12 to 240
 * MHz are these two divisions (48 MHz: 80 and 120). Rounded, so 8 MHz sends at
 * 615 kHz, 2.5 % fast, within the 270-330 kbit/s the specification allows. */
#define CH32RV_UPD_TMR_TX ((uint16_t)((F_CPU + 300000u) / 600000u) - 1u)
#define CH32RV_UPD_TMR_RX ((uint16_t)((F_CPU + 200000u) / 400000u) - 1u)

/* Clocks, the PHY's AFIO bits and the CC pads: what the sink and the source
 * both do first. Where the pads are routed per pin (V205, X305/X315, H41x)
 * they become AF push-pull on the USBPD function; the GPIO input still reads
 * the line, which the source uses to see an open one. */
static inline void ch32rv_usbpd_phy_init(void)
{
    ch32rv_clock_enable_at(CH32RV_USBPD_CLKEN_ADDR, CH32RV_USBPD_CLKEN_MASK);
    ch32rv_clock_enable(AFIO);
    CH32RV_USBPD_AFIO_REG = (CH32RV_USBPD_AFIO_REG & ~CH32RV_USBPD_AFIO_CLR) | CH32RV_USBPD_AFIO_SET;
#if CH32RV_USBPD_CC_AF
    static const uint8_t pads[2] = {(uint8_t)CH32RV_USBPD_CC1_PIN, (uint8_t)CH32RV_USBPD_CC2_PIN};
    for (uint8_t i = 0; i < 2; i++) {
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(pads[i]);
        ch32rv_gpio_clock_enable(port);
        ch32rv_gpio_set_config(port, (uint8_t)CH32RV_PIN_BIT(pads[i]), CH32RV_GPIO_CFG_AF_PP_50M);
        ch32rv_pin_set_af(pads[i], CH32RV_USBPD_CC_AF);
    }
#else
    pinMode(CH32RV_USBPD_CC1_PIN, INPUT);
    pinMode(CH32RV_USBPD_CC2_PIN, INPUT);
#endif
}

#endif /* CH32RV_USBPD_BASE */
