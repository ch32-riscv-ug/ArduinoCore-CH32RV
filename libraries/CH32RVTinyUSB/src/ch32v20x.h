/* Ours (tools/vendor/vendor_tinyusb.py OURS): what the vendored TinyUSB needs from WCH's ch32v20x.h for
 * CFG_TUSB_MCU == OPT_MCU_CH32V20X. Serves CH32V203/V208 *and* CH32L103 (the
 * L103 USBFS block is the V20x one: same base, same layout, same IRQ number;
 *).
 *
 * Who includes it:
 *   portable/wch/ch32_usbfs_reg.h   (USBFS / OTG_FS device, CFG_TUD_WCH_USBIP_USBFS=1)
 *   portable/st/stm32_fsdev/fsdev_ch32.h (USBD = ST "fsdev" clone, CFG_TUD_WCH_USBIP_FSDEV=1,
 *                                     V203/V208 only; L103 has no USBD)
 *
 * Symbols used by those drivers, and nothing else:
 *   USBFS path : __IO, USBOTG_FS (-> struct with BASE_CTRL..UEP7_RX_CTRL),
 *                USBHD_IRQn, NVIC_EnableIRQ, NVIC_DisableIRQ
 *   FSDEV path : IRQn_Type, USB_HP_CAN1_TX_IRQn, USB_LP_CAN1_RX0_IRQn,
 *                USBWakeUp_IRQn, APB1PERIPH_BASE, EXTEN (->EXTEN_CTR),
 *                EXTEN_USBD_PU_EN, NVIC_EnableIRQ, NVIC_DisableIRQ
 *
 * Register layout: USBFSD_TypeDef from the CH32V20x EVT
 * (EXAM/SRC/Peripheral/inc/ch32v20x.h:655-705); the CH32L103 EVT struct
 * (ch32l103.h:609-699) has the same offsets, it only wraps each
 * UEPn_TX_CTRL/UEPn_RX_CTRL pair in a union with a 16-bit UEPn_CTRL.
 * The driver indexes UEPn_DMA / UEPn_TX_LEN / UEPn_TX_CTRL / UEPn_RX_CTRL
 * from EP0 with a 4-byte stride (dcd_ch32_usbfs.c:64-67), so the layout
 * below must stay gap-free; the asserts pin it. */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ch32rv_registers.h"

#ifndef __IO
#define __IO volatile
#endif

/* ------------------------------------------------------------ interrupts
 * Numbers from this core's generated irqn_<variant>.h (same as the EVT
 * enum, ch32v20x.h:85-110 / ch32l103.h:91-92). IRQn_Type exists only
 * because fsdev_ch32.h declares `static const IRQn_Type fsdev_irq[]`. */
typedef enum {
#if defined(CH32RV_IRQN_USB_HP_CAN1_TX) && defined(CH32RV_IRQN_USBWakeUp)
    /* V20x USBD (fsdev). L103 has no USBD and no USBWakeUp (58 is LPTIMWakeUp). */
    USB_HP_CAN1_TX_IRQn  = CH32RV_IRQN_USB_HP_CAN1_TX,   /* 35 */
    USB_LP_CAN1_RX0_IRQn = CH32RV_IRQN_USB_LP_CAN1_RX0,  /* 36 */
    USBWakeUp_IRQn       = CH32RV_IRQN_USBWakeUp,        /* 58 */
#endif
    USBFS_IRQn           = CH32RV_IRQN_USBFS,            /* 59 */
    USBFSWakeUp_IRQn     = CH32RV_IRQN_USBFSWakeUp,      /* 60 */
} IRQn_Type;

/* EVT aliases (ch32v20x.h:142-149) that ch32_usbfs_reg.h relies on. */
#define USBHD_IRQn       USBFS_IRQn
#define USBHDWakeUp_IRQn USBFSWakeUp_IRQn

static inline void NVIC_EnableIRQ(IRQn_Type irqn)
{
    ch32rv_irq_enable((uint32_t)irqn);
}

static inline void NVIC_DisableIRQ(IRQn_Type irqn)
{
    ch32rv_irq_disable((uint32_t)irqn);
}

/* ------------------------------------------------------------ USBFS (OTG_FS)
 * 0x50000000 on V20x (ch32v20x.h:870) and L103 (ch32l103.h:796). */
typedef struct {
    __IO uint8_t  BASE_CTRL;     /* 0x00 R8_USB_CTRL   */
    __IO uint8_t  UDEV_CTRL;     /* 0x01 R8_UDEV_CTRL  */
    __IO uint8_t  INT_EN;        /* 0x02 */
    __IO uint8_t  DEV_ADDR;      /* 0x03 */
    __IO uint8_t  Reserve0;      /* 0x04 */
    __IO uint8_t  MIS_ST;        /* 0x05 */
    __IO uint8_t  INT_FG;        /* 0x06 */
    __IO uint8_t  INT_ST;        /* 0x07 */
    __IO uint32_t RX_LEN;        /* 0x08 (R16_USB_RX_LEN in a 32-bit slot) */
    __IO uint8_t  UEP4_1_MOD;    /* 0x0C */
    __IO uint8_t  UEP2_3_MOD;    /* 0x0D */
    __IO uint8_t  UEP5_6_MOD;    /* 0x0E */
    __IO uint8_t  UEP7_MOD;      /* 0x0F */
    __IO uint32_t UEP0_DMA;      /* 0x10 */
    __IO uint32_t UEP1_DMA;
    __IO uint32_t UEP2_DMA;
    __IO uint32_t UEP3_DMA;
    __IO uint32_t UEP4_DMA;      /* 0x20 - own register here, unlike X035 */
    __IO uint32_t UEP5_DMA;
    __IO uint32_t UEP6_DMA;
    __IO uint32_t UEP7_DMA;      /* 0x2C */
    __IO uint16_t UEP0_TX_LEN;   /* 0x30 */
    __IO uint8_t  UEP0_TX_CTRL;  /* 0x32 */
    __IO uint8_t  UEP0_RX_CTRL;  /* 0x33 */
    __IO uint16_t UEP1_TX_LEN;   /* 0x34 */
    __IO uint8_t  UEP1_TX_CTRL;
    __IO uint8_t  UEP1_RX_CTRL;
    __IO uint16_t UEP2_TX_LEN;   /* 0x38 */
    __IO uint8_t  UEP2_TX_CTRL;
    __IO uint8_t  UEP2_RX_CTRL;
    __IO uint16_t UEP3_TX_LEN;   /* 0x3C (10 bits: EP3 iso up to 1023 B) */
    __IO uint8_t  UEP3_TX_CTRL;
    __IO uint8_t  UEP3_RX_CTRL;
    __IO uint16_t UEP4_TX_LEN;   /* 0x40 */
    __IO uint8_t  UEP4_TX_CTRL;
    __IO uint8_t  UEP4_RX_CTRL;
    __IO uint16_t UEP5_TX_LEN;   /* 0x44 */
    __IO uint8_t  UEP5_TX_CTRL;
    __IO uint8_t  UEP5_RX_CTRL;
    __IO uint16_t UEP6_TX_LEN;   /* 0x48 */
    __IO uint8_t  UEP6_TX_CTRL;
    __IO uint8_t  UEP6_RX_CTRL;
    __IO uint16_t UEP7_TX_LEN;   /* 0x4C */
    __IO uint8_t  UEP7_TX_CTRL;
    __IO uint8_t  UEP7_RX_CTRL;
    __IO uint32_t Reserve1;      /* 0x50 */
    __IO uint32_t OTG_CR;        /* 0x54 */
    __IO uint32_t OTG_SR;        /* 0x58 */
} USBFSD_TypeDef;

#ifdef __cplusplus
#define CH32RV_USBFS_ASSERT static_assert
#else
#define CH32RV_USBFS_ASSERT _Static_assert
#endif
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, RX_LEN) == 0x08, "USBFSD RX_LEN");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP4_1_MOD) == 0x0C, "USBFSD UEP4_1_MOD");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP7_MOD) == 0x0F, "USBFSD UEP7_MOD");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP0_DMA) == 0x10, "USBFSD UEP0_DMA");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP7_DMA) == 0x2C, "USBFSD UEP7_DMA");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP0_TX_LEN) == 0x30, "USBFSD UEP0_TX_LEN");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP0_TX_CTRL) == 0x32, "USBFSD UEP0_TX_CTRL");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP0_RX_CTRL) == 0x33, "USBFSD UEP0_RX_CTRL");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP1_TX_LEN) == 0x34, "USBFSD EP stride");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, UEP7_RX_CTRL) == 0x4F, "USBFSD UEP7_RX_CTRL");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, OTG_CR) == 0x54, "USBFSD OTG_CR");
CH32RV_USBFS_ASSERT(offsetof(USBFSD_TypeDef, OTG_SR) == 0x58, "USBFSD OTG_SR");
#undef CH32RV_USBFS_ASSERT

#define USBFS_BASE ((uint32_t)0x50000000u)
#define USBFSD     ((USBFSD_TypeDef *)USBFS_BASE)
#define USBOTG_FS  USBFSD                        /* ch32v20x.h:148 */

/* ------------------------------------------------------------ USBD (fsdev)
 * fsdev_ch32.h computes FSDEV_REG_BASE = APB1PERIPH_BASE + 0x5C00 and
 * FSDEV_PMA_BASE = APB1PERIPH_BASE + 0x6000 (RM CH32FV2x_V3xRM ch21: 512-byte
 * PMA at 0x40006000, shared with CAN1), and drives the D+ pull-up through
 * EXTEN (ch32v20x.h:4656). Defined on L103 too - harmless, since the fsdev
 * driver is compiled only when CFG_TUD_WCH_USBIP_FSDEV is 1. */
#define APB1PERIPH_BASE ((uint32_t)0x40000000u)

typedef struct {
    __IO uint32_t EXTEN_CTR;
} EXTEN_TypeDef;

#define EXTEN_BASE        ((uint32_t)0x40023800u)    /* AHB + 0x3800 */
#define EXTEN             ((EXTEN_TypeDef *)EXTEN_BASE)
#define EXTEN_USBD_LS     ((uint32_t)0x00000001u)    /* bit 0: USBD low speed */
#define EXTEN_USBD_PU_EN  ((uint32_t)0x00000002u)    /* bit 1: USBD internal 1.5k pull-up */
