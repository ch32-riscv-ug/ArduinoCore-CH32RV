/* Ours (tools/vendor/vendor_tinyusb.py OURS): what the vendored portable/wch/ch32_usbfs_reg.h and ch32_usbhs_reg.h
 * need from WCH's ch32v30x.h when CFG_TUSB_MCU == OPT_MCU_CH32V307.
 *
 * Would live next to ch32x035.h in libraries/CH32RVTinyUSB/src and be listed
 * in OURS (tools/vendor/vendor_tinyusb.py). The vendored tree stays verbatim.
 *
 * What TinyUSB uses, and nothing more:
 *   USBFS path (ch32_usbfs_reg.h:111-113, dcd_ch32_usbfs.c):
 *     USBOTG_FS (-> register block), OTG_FS_IRQn, __IO,
 *     NVIC_EnableIRQ / NVIC_DisableIRQ,
 *     fields BASE_CTRL UDEV_CTRL INT_EN DEV_ADDR MIS_ST INT_FG INT_ST RX_LEN
 *            UEP4_1_MOD UEP2_3_MOD UEP5_6_MOD UEP7_MOD
 *            UEP0_DMA (indexed [0..7]) UEP0_TX_LEN (indexed [2*ep])
 *            UEP0_TX_CTRL / UEP0_RX_CTRL (indexed [4*ep])
 *   USBHS path (ch32_usbhs_reg.h:19-20, dcd_ch32_usbhs.c):
 *     USBHSD (-> register block), USBHS_IRQn, __IO, NVIC_EnableIRQ/DisableIRQ,
 *     fields CONTROL HOST_CTRL INT_EN DEV_AD FRAME_NO SPEED_TYPE INT_FG INT_ST
 *            RX_LEN ENDP_CONFIG ENDP_TYPE BUF_MODE UEP0_DMA
 *            UEP1_RX_DMA (indexed [ep-1]) UEP1_TX_DMA (indexed [ep-1])
 *            UEP0_MAX_LEN (indexed [2*ep]) UEP0_TX_LEN (indexed [2*ep])
 *            UEP0_TX_CTRL / UEP0_RX_CTRL (indexed [4*ep])
 *
 * Layouts: USBFSD_TypeDef and USBHSD_TypeDef from the CH32V307 EVT,
 * EXAM/SRC/Peripheral/inc/ch32v30x.h:766-896 and 977-1029; same names and
 * offsets (checked below against RM V2.5 tables 22-2 / 23-x).
 *
 * NOTE the bases are the other way round from X035: on V30x the HS
 * controller sits at AHB+0x3400 (0x40023400) and USBFS at 0x50000000
 * (EVT ch32v30x.h:1225, 1236). */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ch32rv_registers.h"

#ifndef __IO
#define __IO volatile
#endif

/* ------------------------------------------------------------ USBFS / OTG_FS */
#define CH32RV_V30X_FS_EP(n)        \
    __IO uint16_t UEP##n##_TX_LEN;  \
    __IO uint8_t  UEP##n##_TX_CTRL; \
    __IO uint8_t  UEP##n##_RX_CTRL

typedef struct {
    __IO uint8_t  BASE_CTRL;     /* 0x00 */
    __IO uint8_t  UDEV_CTRL;     /* 0x01 */
    __IO uint8_t  INT_EN;        /* 0x02 */
    __IO uint8_t  DEV_ADDR;      /* 0x03 */
    __IO uint8_t  Reserve0;      /* 0x04 */
    __IO uint8_t  MIS_ST;        /* 0x05 */
    __IO uint8_t  INT_FG;        /* 0x06 */
    __IO uint8_t  INT_ST;        /* 0x07 */
    __IO uint16_t RX_LEN;        /* 0x08 */
    __IO uint16_t Reserve1;      /* 0x0A */
    __IO uint8_t  UEP4_1_MOD;    /* 0x0C */
    __IO uint8_t  UEP2_3_MOD;    /* 0x0D */
    __IO uint8_t  UEP5_6_MOD;    /* 0x0E */
    __IO uint8_t  UEP7_MOD;      /* 0x0F */
    __IO uint32_t UEP0_DMA;      /* 0x10 */
    __IO uint32_t UEP1_DMA;
    __IO uint32_t UEP2_DMA;
    __IO uint32_t UEP3_DMA;
    __IO uint32_t UEP4_DMA;      /* 0x20 */
    __IO uint32_t UEP5_DMA;
    __IO uint32_t UEP6_DMA;
    __IO uint32_t UEP7_DMA;
    CH32RV_V30X_FS_EP(0);        /* 0x30 */
    CH32RV_V30X_FS_EP(1);
    CH32RV_V30X_FS_EP(2);
    CH32RV_V30X_FS_EP(3);
    CH32RV_V30X_FS_EP(4);        /* 0x40 */
    CH32RV_V30X_FS_EP(5);
    CH32RV_V30X_FS_EP(6);
    CH32RV_V30X_FS_EP(7);
    __IO uint32_t Reserve2;      /* 0x50 */
    __IO uint32_t OTG_CR;        /* 0x54 */
    __IO uint32_t OTG_SR;        /* 0x58 */
} USBFSD_TypeDef;

#undef CH32RV_V30X_FS_EP

/* ------------------------------------------------------------ USBHS */
#define CH32RV_V30X_HS_MAXLEN(n)     \
    __IO uint16_t UEP##n##_MAX_LEN;  \
    __IO uint16_t RESERVED_ML##n
#define CH32RV_V30X_HS_EP(n)         \
    __IO uint16_t UEP##n##_TX_LEN;   \
    __IO uint8_t  UEP##n##_TX_CTRL;  \
    __IO uint8_t  UEP##n##_RX_CTRL

typedef struct {
    __IO uint8_t  CONTROL;       /* 0x00 */
    __IO uint8_t  HOST_CTRL;     /* 0x01 */
    __IO uint8_t  INT_EN;        /* 0x02 */
    __IO uint8_t  DEV_AD;        /* 0x03 */
    __IO uint16_t FRAME_NO;      /* 0x04 */
    __IO uint8_t  SUSPEND;       /* 0x06 */
    __IO uint8_t  RESERVED0;     /* 0x07 */
    __IO uint8_t  SPEED_TYPE;    /* 0x08 */
    __IO uint8_t  MIS_ST;        /* 0x09 */
    __IO uint8_t  INT_FG;        /* 0x0A */
    __IO uint8_t  INT_ST;        /* 0x0B */
    __IO uint16_t RX_LEN;        /* 0x0C */
    __IO uint16_t RESERVED1;     /* 0x0E */
    __IO uint32_t ENDP_CONFIG;   /* 0x10 */
    __IO uint32_t ENDP_TYPE;     /* 0x14 */
    __IO uint32_t BUF_MODE;      /* 0x18 */
    __IO uint32_t UEP0_DMA;      /* 0x1C */
    __IO uint32_t UEP1_RX_DMA;   /* 0x20 */
    __IO uint32_t UEP2_RX_DMA;
    __IO uint32_t UEP3_RX_DMA;
    __IO uint32_t UEP4_RX_DMA;
    __IO uint32_t UEP5_RX_DMA;
    __IO uint32_t UEP6_RX_DMA;
    __IO uint32_t UEP7_RX_DMA;
    __IO uint32_t UEP8_RX_DMA;
    __IO uint32_t UEP9_RX_DMA;
    __IO uint32_t UEP10_RX_DMA;
    __IO uint32_t UEP11_RX_DMA;
    __IO uint32_t UEP12_RX_DMA;
    __IO uint32_t UEP13_RX_DMA;
    __IO uint32_t UEP14_RX_DMA;
    __IO uint32_t UEP15_RX_DMA;  /* 0x58 */
    __IO uint32_t UEP1_TX_DMA;   /* 0x5C */
    __IO uint32_t UEP2_TX_DMA;
    __IO uint32_t UEP3_TX_DMA;
    __IO uint32_t UEP4_TX_DMA;
    __IO uint32_t UEP5_TX_DMA;
    __IO uint32_t UEP6_TX_DMA;
    __IO uint32_t UEP7_TX_DMA;
    __IO uint32_t UEP8_TX_DMA;
    __IO uint32_t UEP9_TX_DMA;
    __IO uint32_t UEP10_TX_DMA;
    __IO uint32_t UEP11_TX_DMA;
    __IO uint32_t UEP12_TX_DMA;
    __IO uint32_t UEP13_TX_DMA;
    __IO uint32_t UEP14_TX_DMA;
    __IO uint32_t UEP15_TX_DMA;  /* 0x94 */
    CH32RV_V30X_HS_MAXLEN(0);    /* 0x98 */
    CH32RV_V30X_HS_MAXLEN(1);
    CH32RV_V30X_HS_MAXLEN(2);
    CH32RV_V30X_HS_MAXLEN(3);
    CH32RV_V30X_HS_MAXLEN(4);
    CH32RV_V30X_HS_MAXLEN(5);
    CH32RV_V30X_HS_MAXLEN(6);
    CH32RV_V30X_HS_MAXLEN(7);
    CH32RV_V30X_HS_MAXLEN(8);
    CH32RV_V30X_HS_MAXLEN(9);
    CH32RV_V30X_HS_MAXLEN(10);
    CH32RV_V30X_HS_MAXLEN(11);
    CH32RV_V30X_HS_MAXLEN(12);
    CH32RV_V30X_HS_MAXLEN(13);
    CH32RV_V30X_HS_MAXLEN(14);
    CH32RV_V30X_HS_MAXLEN(15);   /* 0xD4 */
    CH32RV_V30X_HS_EP(0);        /* 0xD8 */
    CH32RV_V30X_HS_EP(1);
    CH32RV_V30X_HS_EP(2);
    CH32RV_V30X_HS_EP(3);
    CH32RV_V30X_HS_EP(4);
    CH32RV_V30X_HS_EP(5);
    CH32RV_V30X_HS_EP(6);
    CH32RV_V30X_HS_EP(7);        /* 0xF4 */
    CH32RV_V30X_HS_EP(8);
    CH32RV_V30X_HS_EP(9);
    CH32RV_V30X_HS_EP(10);
    CH32RV_V30X_HS_EP(11);
    CH32RV_V30X_HS_EP(12);
    CH32RV_V30X_HS_EP(13);
    CH32RV_V30X_HS_EP(14);
    CH32RV_V30X_HS_EP(15);       /* 0x114 */
} USBHSD_TypeDef;

#undef CH32RV_V30X_HS_MAXLEN
#undef CH32RV_V30X_HS_EP

/* ------------------------------------------------------------ offsets */
#ifdef __cplusplus
#define CH32RV_V30X_ASSERT static_assert
#else
#define CH32RV_V30X_ASSERT _Static_assert
#endif

/* USBFS: RM V2.5 table 23-x (R32_UEP0_DMA at 0x50000010) and the EVT struct. */
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, RX_LEN) == 0x08, "USBFS RX_LEN");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP4_1_MOD) == 0x0C, "USBFS UEP4_1_MOD");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP7_MOD) == 0x0F, "USBFS UEP7_MOD");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP0_DMA) == 0x10, "USBFS UEP0_DMA");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP7_DMA) == 0x2C, "USBFS UEP7_DMA");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP0_TX_LEN) == 0x30, "USBFS UEP0_TX_LEN");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP0_TX_CTRL) == 0x32, "USBFS UEP0_TX_CTRL");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP0_RX_CTRL) == 0x33, "USBFS UEP0_RX_CTRL");
/* dcd_ch32_usbfs.c:64-67 indexes from EP0 with a fixed stride. */
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP7_TX_LEN) == 0x30 + 7 * 4, "USBFS EP stride");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, UEP7_RX_CTRL) == 0x33 + 7 * 4, "USBFS EP stride");
CH32RV_V30X_ASSERT(offsetof(USBFSD_TypeDef, OTG_CR) == 0x54, "USBFS OTG_CR");

/* USBHS: RM V2.5 table 22-2 (UEP_CONFIG 0x40023410, UEP0_DMA 0x4002341C,
 * UEP1_RX_DMA 0x40023420, UEP1_TX_DMA 0x4002345C, UEP7_TX_CTRL 0x400234F6,
 * UEP15_RX_CTRL 0x40023517). */
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, SPEED_TYPE) == 0x08, "USBHS SPEED_TYPE");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, INT_FG) == 0x0A, "USBHS INT_FG");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, RX_LEN) == 0x0C, "USBHS RX_LEN");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, ENDP_CONFIG) == 0x10, "USBHS UEP_CONFIG");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, BUF_MODE) == 0x18, "USBHS UEP_BUF_MOD");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP0_DMA) == 0x1C, "USBHS UEP0_DMA");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP1_RX_DMA) == 0x20, "USBHS UEP1_RX_DMA");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP15_RX_DMA) == 0x58, "USBHS UEP15_RX_DMA");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP1_TX_DMA) == 0x5C, "USBHS UEP1_TX_DMA");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP0_MAX_LEN) == 0x98, "USBHS UEP0_MAX_LEN");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP15_MAX_LEN) == 0x98 + 15 * 4, "USBHS MAX_LEN stride");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP0_TX_LEN) == 0xD8, "USBHS UEP0_T_LEN");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP0_TX_CTRL) == 0xDA, "USBHS UEP0_TX_CTRL");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP7_TX_CTRL) == 0xF6, "USBHS UEP7_TX_CTRL");
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP15_RX_CTRL) == 0x117, "USBHS UEP15_RX_CTRL");
/* ch32_usbhs_reg.h's USBHS_UEPx_*_OFFSET(n) formulas, same numbers. */
CH32RV_V30X_ASSERT(offsetof(USBHSD_TypeDef, UEP15_TX_DMA) == 0x58 + 4 * 15, "USBHS_UEPx_TX_DMA_OFFSET");
#undef CH32RV_V30X_ASSERT

/* ------------------------------------------------------------ instances */
#define CH32RV_V30X_USBHS_BASE 0x40023400u   /* AHB + 0x3400 */
#define CH32RV_V30X_USBFS_BASE 0x50000000u

#define USBHSD    ((USBHSD_TypeDef *)CH32RV_V30X_USBHS_BASE)
#define USBFSD    ((USBFSD_TypeDef *)CH32RV_V30X_USBFS_BASE)
#define USBOTG_FS USBFSD            /* EVT ch32v30x.h:196 */

/* irqn_v307_d8c.h:79,81 (V305/V307/V317); irqn_v307_d8.h:72 (V303, FS only). */
#define OTG_FS_IRQn CH32RV_IRQN_USBFS
#ifdef CH32RV_IRQN_USBHS
#define USBHS_IRQn  CH32RV_IRQN_USBHS
#endif

static inline void NVIC_EnableIRQ(uint32_t irqn)
{
    ch32rv_irq_enable(irqn);
}

static inline void NVIC_DisableIRQ(uint32_t irqn)
{
    ch32rv_irq_disable(irqn);
}
