/* The SDIO block where it is (CH32V303 / V305 / V307 / V317, V407 / V467,
 * H415 / H417): one register layout (layout ed03b2b3 in ch32-device-data, the
 * same as STM32F1's SDIO with WCH's DCTRL2), at 0x40018000 on all three. Its
 * kernel clock is HCLK, and SDIO_CK = HCLK / (CLKDIV + 2), at most HCLK / 2
 * (CH32FV2x_V3xRM 28.2.3). Pins per series:
 *
 *   V30x  CK PC12, CMD PD2, D0-D3 PC8-PC11 (fixed)
 *   V4x7  CK PC12, CMD PD2, D2 PC10, D3 PC11; D0 / D1 PC8 / PC9, or PB14 /
 *         PB15 with AFIO PCFR2 bit 12 (SDIO_RM)
 *   H41x  per pin, by AF number (pinout.csv, CH32H417QEU6): CK PB11 (AF8) /
 *         PC1 (AF11), CMD PB10 (AF8) / PA0 (AF9), D0-D3 PE8-PE11 (AF8) or
 *         PB14 / PB15 / PB3 / PB4 (AF9). The default is the EVT's (PE8-PE11,
 *         PB10, PB11). PC8-PC12 / PD2 there are the SDMMC block's, not this.
 *         CH32H415 (64 pins) has D0-D2 only on PB14 / PB15 / PB3, D3 on PB4
 *         or PE11: its default is CK PB11, CMD PB10, D0-D3 PB14 / PB15 / PB3 /
 *         PB4 */
#pragma once

#include <Arduino.h>
#include <internal/ch32rv_gpio.h>
#include <internal/ch32rv_registers.h>

#if defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317)
#define SDIO_HW_V30X 1
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define SDIO_HW_V4X7 1
#elif defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H417)
#define SDIO_HW_H41X 1
#endif

#if defined(SDIO_HW_V30X) || defined(SDIO_HW_V4X7) || defined(SDIO_HW_H41X)
#define SDIO_HW 1
#else
#define SDIO_HW 0
#endif

#if SDIO_HW
#define SDIO_BASE_ADDR 0x40018000u
#define SDIO_REG(off) CH32RV_REG32(SDIO_BASE_ADDR + (off))
#define SDIO_POWER SDIO_REG(0x00)
#define SDIO_CLKCR SDIO_REG(0x04)
#define SDIO_ARG SDIO_REG(0x08)
#define SDIO_CMD SDIO_REG(0x0C)
#define SDIO_RESPCMD SDIO_REG(0x10)
#define SDIO_RESP1 SDIO_REG(0x14)
#define SDIO_RESP2 SDIO_REG(0x18)
#define SDIO_RESP3 SDIO_REG(0x1C)
#define SDIO_RESP4 SDIO_REG(0x20)
#define SDIO_DTIMER SDIO_REG(0x24)
#define SDIO_DLEN SDIO_REG(0x28)
#define SDIO_DCTRL SDIO_REG(0x2C)
#define SDIO_DCOUNT SDIO_REG(0x30)
#define SDIO_STA SDIO_REG(0x34)
#define SDIO_ICR SDIO_REG(0x38)
#define SDIO_MASK SDIO_REG(0x3C)
#define SDIO_FIFOCNT SDIO_REG(0x48)
#define SDIO_FIFO SDIO_REG(0x80)

/* CLKCR */
#define SDIO_CLKCR_CLKEN (1u << 8)
#define SDIO_CLKCR_WIDBUS_4 (1u << 11)
#define SDIO_CLKCR_HWFC_EN (1u << 14)
/* CMD */
#define SDIO_CMD_RESP_SHORT (1u << 6)
#define SDIO_CMD_RESP_LONG (3u << 6)
#define SDIO_CMD_CPSMEN (1u << 10)
/* DCTRL */
#define SDIO_DCTRL_DTEN (1u << 0)
#define SDIO_DCTRL_DTDIR_READ (1u << 1)
#define SDIO_DCTRL_BLOCK_512 (9u << 4)
/* STA */
#define SDIO_STA_CCRCFAIL (1u << 0)
#define SDIO_STA_DCRCFAIL (1u << 1)
#define SDIO_STA_CTIMEOUT (1u << 2)
#define SDIO_STA_DTIMEOUT (1u << 3)
#define SDIO_STA_TXUNDERR (1u << 4)
#define SDIO_STA_RXOVERR (1u << 5)
#define SDIO_STA_CMDREND (1u << 6)
#define SDIO_STA_CMDSENT (1u << 7)
#define SDIO_STA_DATAEND (1u << 8)
#define SDIO_STA_STBITERR (1u << 9)
#define SDIO_STA_DBCKEND (1u << 10)
#define SDIO_STA_TXACT (1u << 12)
#define SDIO_STA_RXACT (1u << 13)
#define SDIO_STA_TXFIFOHE (1u << 14)
#define SDIO_STA_RXFIFOHF (1u << 15)
#define SDIO_STA_RXDAVL (1u << 21)
#define SDIO_STA_DATA_ERRORS (SDIO_STA_DCRCFAIL | SDIO_STA_DTIMEOUT | SDIO_STA_TXUNDERR | SDIO_STA_RXOVERR | \
                              SDIO_STA_STBITERR)
#define SDIO_ICR_ALL 0x00C007FFu

/* GPIO configuration nibble: alternate function push-pull, 50 MHz. */
#define SDIO_PIN_AF_PP 0xBu

/* The kernel clock (HCLK). The series here run HCLK at F_CPU. */
#define SDIO_KERNEL_HZ ((uint32_t)F_CPU)
#endif
