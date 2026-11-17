/* The SDIO host where the variant has one (CH32RV_SDIO_BASE: CH32V303 / V305
 * / V307 / V317, V407 / V467, H415 / H417): its registers, its pads and its
 * DMA line, for SD_MMC.
 *
 * One register layout on all three (layout ed03b2b3 in ch32-device-data, the
 * same as STM32F1's SDIO with WCH's DCTRL2 at 0x60). Its kernel clock is HCLK,
 * SDIO_CK = HCLK / (CLKDIV + 2), at most HCLK / 2 (CH32FV2x_V3xRM 28.2.3,
 * CH32H417RM 32.6.2).
 *
 * The pads come from the variant (tools/generate/generate.py, load_sdio):
 * CH32RV_SDIO_PADS lists { pad, signal, route } for every pad a signal
 * reaches - signal 0 CK, 1 CMD, 2..9 D0..D7 - with a signal's default first.
 * Where pads take their alternate function one by one (CH32RV_GPIO_PER_PIN_AF,
 * H41x) the route is that pad's function number and each signal picks its pad
 * alone. Elsewhere the route is the value of one AFIO field
 * (CH32RV_SDIO_REMAP_PCFR / _SHIFT: V4x7's SDIO_RM) and all pads must be on
 * the same route; V30x has route 0 only. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "pins_arduino.h"
#include "internal/ch32rv_dma.h"
#include "internal/ch32rv_gpio.h"
#include "internal/ch32rv_registers.h"

#ifdef CH32RV_SDIO_BASE
#define CH32RV_HAS_SDIO 1

#define CH32RV_SDIO_REG(off) CH32RV_REG32(CH32RV_SDIO_BASE + (off))
#define CH32RV_SDIO_POWER    CH32RV_SDIO_REG(0x00)
#define CH32RV_SDIO_CLKCR    CH32RV_SDIO_REG(0x04)
#define CH32RV_SDIO_ARG      CH32RV_SDIO_REG(0x08)
#define CH32RV_SDIO_CMD      CH32RV_SDIO_REG(0x0C)
#define CH32RV_SDIO_RESPCMD  CH32RV_SDIO_REG(0x10)
#define CH32RV_SDIO_RESP1    CH32RV_SDIO_REG(0x14)
#define CH32RV_SDIO_RESP2    CH32RV_SDIO_REG(0x18)
#define CH32RV_SDIO_RESP3    CH32RV_SDIO_REG(0x1C)
#define CH32RV_SDIO_RESP4    CH32RV_SDIO_REG(0x20)
#define CH32RV_SDIO_DTIMER   CH32RV_SDIO_REG(0x24)
#define CH32RV_SDIO_DLEN     CH32RV_SDIO_REG(0x28)
#define CH32RV_SDIO_DCTRL    CH32RV_SDIO_REG(0x2C)
#define CH32RV_SDIO_DCOUNT   CH32RV_SDIO_REG(0x30)
#define CH32RV_SDIO_STA      CH32RV_SDIO_REG(0x34)
#define CH32RV_SDIO_ICR      CH32RV_SDIO_REG(0x38)
#define CH32RV_SDIO_MASK     CH32RV_SDIO_REG(0x3C)
#define CH32RV_SDIO_FIFOCNT  CH32RV_SDIO_REG(0x48)
#define CH32RV_SDIO_FIFO_ADDR (CH32RV_SDIO_BASE + 0x80u)
#define CH32RV_SDIO_FIFO     CH32RV_SDIO_REG(0x80)

/* CLKCR */
#define CH32RV_SDIO_CLKCR_CLKEN   (1u << 8)
#define CH32RV_SDIO_CLKCR_WIDBUS_4 (1u << 11)
#define CH32RV_SDIO_CLKCR_WIDBUS_8 (2u << 11)
#define CH32RV_SDIO_CLKCR_HWFC_EN (1u << 14)
/* CMD */
#define CH32RV_SDIO_CMD_RESP_SHORT (1u << 6)
#define CH32RV_SDIO_CMD_RESP_LONG  (3u << 6)
#define CH32RV_SDIO_CMD_CPSMEN     (1u << 10)
/* DCTRL: DBLOCKSIZE is log2 of the block's bytes, bits 7:4 */
#define CH32RV_SDIO_DCTRL_DTEN       (1u << 0)
#define CH32RV_SDIO_DCTRL_DTDIR_READ (1u << 1)
#define CH32RV_SDIO_DCTRL_DMAEN      (1u << 3)
#define CH32RV_SDIO_DCTRL_BLOCK(log2) ((uint32_t)(log2) << 4)
/* STA */
#define CH32RV_SDIO_STA_CCRCFAIL (1u << 0)
#define CH32RV_SDIO_STA_DCRCFAIL (1u << 1)
#define CH32RV_SDIO_STA_CTIMEOUT (1u << 2)
#define CH32RV_SDIO_STA_DTIMEOUT (1u << 3)
#define CH32RV_SDIO_STA_TXUNDERR (1u << 4)
#define CH32RV_SDIO_STA_RXOVERR  (1u << 5)
#define CH32RV_SDIO_STA_CMDREND  (1u << 6)
#define CH32RV_SDIO_STA_CMDSENT  (1u << 7)
#define CH32RV_SDIO_STA_DATAEND  (1u << 8)
#define CH32RV_SDIO_STA_STBITERR (1u << 9)
#define CH32RV_SDIO_STA_DBCKEND  (1u << 10)
#define CH32RV_SDIO_STA_TXACT    (1u << 12)
#define CH32RV_SDIO_STA_RXACT    (1u << 13)
#define CH32RV_SDIO_STA_TXFIFOHE (1u << 14)
#define CH32RV_SDIO_STA_RXFIFOHF (1u << 15)
#define CH32RV_SDIO_STA_RXDAVL   (1u << 21)
#define CH32RV_SDIO_STA_DATA_ERRORS (CH32RV_SDIO_STA_DCRCFAIL | CH32RV_SDIO_STA_DTIMEOUT | \
                                     CH32RV_SDIO_STA_TXUNDERR | CH32RV_SDIO_STA_RXOVERR | CH32RV_SDIO_STA_STBITERR)
#define CH32RV_SDIO_ICR_ALL 0x00C007FFu

/* The kernel clock, HCLK. */
#define CH32RV_SDIO_KERNEL_HZ ((uint32_t)CH32RV_HCLK_HZ)

/* Signals in the pad table. */
#define CH32RV_SDIO_SIGNALS 10   /* CK, CMD, D0..D7 */

typedef struct {
    uint8_t pad;
    uint8_t signal;   /* 0 CK, 1 CMD, 2..9 D0..D7 */
    uint8_t route;    /* the pad's function number (per-pin families), else the AFIO field's value */
} ch32rv_sdio_pad_t;

/* Where the AFIO field that moves the pads is (V4x7). */
#if defined(CH32RV_SDIO_REMAP_PCFR) && CH32RV_SDIO_REMAP_PCFR == 2
#define CH32RV_SDIO_REMAP_REG CH32RV_AFIO_PCFR2
#elif defined(CH32RV_SDIO_REMAP_PCFR)
#define CH32RV_SDIO_REMAP_REG CH32RV_AFIO_PCFR1
#endif

/* The pads for signals 0 .. n - 1: chosen[i] the index in `table` of signal
 * i's pad, the one want[i] names or, for -1, the signal's default. False when
 * a signal has no such pad, or (one route for all) no route has them all. */
static inline bool ch32rv_sdio_pick(const ch32rv_sdio_pad_t *table, uint8_t count, const int16_t *want, uint8_t n,
                                    uint8_t *chosen)
{
#if CH32RV_GPIO_PER_PIN_AF
    for (uint8_t i = 0; i < n; i++) {
        uint8_t k = 0;
        while (k < count && !(table[k].signal == i && (want[i] < 0 || table[k].pad == want[i]))) {
            k++;
        }
        if (k == count) {
            return false;
        }
        chosen[i] = k;
    }
    return true;
#else
    /* Each route in table order (route 0 first): the first that has them all. */
    for (uint8_t r = 0; r < count; r++) {
        const uint8_t route = table[r].route;
        bool all = true;
        for (uint8_t i = 0; i < n && all; i++) {
            uint8_t k = 0;
            while (k < count && !(table[k].route == route && table[k].signal == i &&
                                  (want[i] < 0 || table[k].pad == want[i]))) {
                k++;
            }
            all = k < count;
            chosen[i] = k;
        }
        if (all) {
            return true;
        }
    }
    return false;
#endif
}

/* The chosen pads given to the SDIO: their ports clocked, function selected,
 * alternate-function push-pull at the fastest edge (the bus runs up to 50
 * MHz; the card's pull-ups hold CMD and D when nobody drives). */
static inline void ch32rv_sdio_pins_apply(const ch32rv_sdio_pad_t *table, const uint8_t *chosen, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        const ch32rv_sdio_pad_t *p = &table[chosen[i]];
        const uint8_t port = (uint8_t)CH32RV_PIN_PORT(p->pad), bit = (uint8_t)CH32RV_PIN_BIT(p->pad);
        ch32rv_gpio_clock_enable(port);
        ch32rv_pin_set_af(p->pad, p->route);
        ch32rv_gpio_set_speed(port, bit, 3u);
        ch32rv_gpio_set_config(port, bit, CH32RV_GPIO_CFG_AF_PP_50M);
    }
#ifdef CH32RV_SDIO_REMAP_REG
    ch32rv_clock_enable(AFIO);
    CH32RV_SDIO_REMAP_REG = (CH32RV_SDIO_REMAP_REG & ~(1u << CH32RV_SDIO_REMAP_SHIFT)) |
                            ((uint32_t)table[chosen[0]].route << CH32RV_SDIO_REMAP_SHIFT);
#endif
}

/* The pads back to floating inputs. */
static inline void ch32rv_sdio_pins_release(const ch32rv_sdio_pad_t *table, const uint8_t *chosen, uint8_t n)
{
    for (uint8_t i = 0; i < n; i++) {
        const uint8_t pad = table[chosen[i]].pad;
        ch32rv_gpio_set_config((uint8_t)CH32RV_PIN_PORT(pad), (uint8_t)CH32RV_PIN_BIT(pad), CH32RV_GPIO_CFG_IN_FLOAT);
    }
}

#else
#define CH32RV_HAS_SDIO 0
#endif
