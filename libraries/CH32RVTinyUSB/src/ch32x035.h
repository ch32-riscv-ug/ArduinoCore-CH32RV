/* What the vendored portable/wch/ch32_usbfs_reg.h needs from WCH's ch32x035.h.
 *
 * For OPT_MCU_CH32X035 that header does `#include <ch32x035.h>` and then uses
 * USBFSD (the register block), USBFS_IRQn, __IO and NVIC_EnableIRQ/DisableIRQ.
 * This core carries no WCH SDK, so this file supplies exactly those; the
 * vendored tree stays verbatim (vendor/tinyusb.lock.toml: patches = []).
 *
 * The register block is USBFSD_TypeDef from the CH32X035 EVT header
 * (EXAM/SRC/Peripheral/inc/ch32x035.h), same field names and offsets. The
 * TinyUSB driver indexes UEPn_TX_LEN / UEPn_CTRL_H from EP0 with a fixed gap
 * before EP5, so the reserved words must stay where they are. X033 has the
 * same USBFS block. */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ch32rv_registers.h"

#ifndef __IO
#define __IO volatile
#endif

#define CH32RV_USBFS_EP_REG(n)              \
    union {                                 \
        __IO uint32_t UEP##n##_CTRL;        \
        struct {                            \
            __IO uint16_t UEP##n##_TX_LEN;  \
            __IO uint16_t UEP##n##_CTRL_H;  \
        };                                  \
    }

typedef struct {
    __IO uint8_t  BASE_CTRL;     /* 0x00 */
    __IO uint8_t  UDEV_CTRL;     /* 0x01 */
    __IO uint8_t  INT_EN;        /* 0x02 */
    __IO uint8_t  DEV_ADDR;      /* 0x03 */
    uint8_t       RESERVED0;
    __IO uint8_t  MIS_ST;        /* 0x05 */
    __IO uint8_t  INT_FG;        /* 0x06 */
    __IO uint8_t  INT_ST;        /* 0x07 */
    __IO uint16_t RX_LEN;        /* 0x08 */
    uint16_t      RESERVED1;
    __IO uint8_t  UEP4_1_MOD;    /* 0x0C */
    __IO uint8_t  UEP2_3_MOD;    /* 0x0D */
    __IO uint8_t  UEP567_MOD;    /* 0x0E */
    uint8_t       RESERVED2;
    __IO uint32_t UEP0_DMA;      /* 0x10 */
    __IO uint32_t UEP1_DMA;
    __IO uint32_t UEP2_DMA;
    __IO uint32_t UEP3_DMA;
    CH32RV_USBFS_EP_REG(0);      /* 0x20 */
    CH32RV_USBFS_EP_REG(1);
    CH32RV_USBFS_EP_REG(2);
    CH32RV_USBFS_EP_REG(3);
    CH32RV_USBFS_EP_REG(4);      /* 0x30 */
    uint32_t      RESERVED3[8];
    __IO uint32_t UEP5_DMA;      /* 0x54 */
    __IO uint32_t UEP6_DMA;
    __IO uint32_t UEP7_DMA;
    uint32_t      RESERVED11;
    CH32RV_USBFS_EP_REG(5);      /* 0x64 */
    CH32RV_USBFS_EP_REG(6);
    CH32RV_USBFS_EP_REG(7);
    __IO uint32_t UEPX_MOD;      /* 0x70 */
} USBFSD_TypeDef;

#undef CH32RV_USBFS_EP_REG

#ifdef __cplusplus
#define CH32RV_USBFS_ASSERT static_assert
#else
#define CH32RV_USBFS_ASSERT _Static_assert
#endif

CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP0_DMA) == 0x10, "USBFSD UEP0_DMA");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP0_TX_LEN) == 0x20, "USBFSD UEP0_TX_LEN");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP5_DMA) == 0x54, "USBFSD UEP5_DMA");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP5_TX_LEN) == 0x64, "USBFSD UEP5_TX_LEN");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEPX_MOD) == 0x70, "USBFSD UEPX_MOD");
#undef CH32RV_USBFS_ASSERT

#define USBFSD     ((USBFSD_TypeDef *)0x40023400u)   /* AHB + 0x3400 */
#define USBFS_IRQn CH32RV_IRQN_USBFS

static inline void NVIC_EnableIRQ(uint32_t irqn)
{
    ch32rv_irq_enable(irqn);
}

static inline void NVIC_DisableIRQ(uint32_t irqn)
{
    ch32rv_irq_disable(irqn);
}
