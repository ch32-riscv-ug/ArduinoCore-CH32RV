/* Ours (tools/vendor/vendor_tinyusb.py OURS): what the vendored TinyUSB needs from WCH's ch32v10x.h for
 * CFG_TUSB_MCU == OPT_MCU_CH32V103.
 *
 * For this MCU, portable/wch/ch32_usbfs_reg.h (lines 21-113) includes
 * <ch32v10x.h> and then defines USBOTG_FS_TypeDef and USBOTG_FS itself
 * (base 0x40023400, combined per-endpoint control register). So unlike
 * ch32x035.h / ch32v20x.h this header must NOT define a USB register struct,
 * USBOTG_FS or USBFSD: doing so would be a redefinition error.
 *
 * What the driver still needs from here:
 *   __IO, USBHD_IRQn (dcd_ch32_usbfs.c:422/427), NVIC_EnableIRQ, NVIC_DisableIRQ
 *
 * IRQ numbers: EVT ch32v10x.h:85-91 (USBWakeUp_IRQn = 58, USBFS_IRQn = 59,
 * USBHD_IRQn alias); this core's irqn_v103.h:48-49. */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "ch32rv_registers.h"

#ifndef __IO
#define __IO volatile
#endif

typedef enum {
    USBWakeUp_IRQn = CH32RV_IRQN_USBWakeUp,   /* 58 */
    USBFS_IRQn     = CH32RV_IRQN_USBFS,       /* 59 */
} IRQn_Type;

#define USBHD_IRQn USBFS_IRQn                 /* ch32v10x.h:90 */

static inline void NVIC_EnableIRQ(IRQn_Type irqn)
{
    ch32rv_irq_enable((uint32_t)irqn);
}

static inline void NVIC_DisableIRQ(IRQn_Type irqn)
{
    ch32rv_irq_disable((uint32_t)irqn);
}

/* Not used by TinyUSB; the PHY bring-up  writes
 * these. EVT ch32v10x.h:593 (EXTEN_BASE), 3181-3185 (bits); RM CH32xRM
 * 25.2.1. */
#define CH32V10X_EXTEN_CTR          CH32RV_REG32(0x40023800u)
#define CH32V10X_EXTEN_USBFS_IO_EN  0x00000004u   /* bit 2: pads to the USBHD PHY */
#define CH32V10X_EXTEN_USB_5V_SEL   0x00000008u   /* bit 3: 1 = VDD is 5 V */
