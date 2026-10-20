/* Which controller driver (CH32RVUsbDcd.h) serves a series, and how its
 * factory USB bootloader is reached. Included once, by CH32RVUsbDeviceCore.cpp.
 *
 * Every series provides:
 *   port_dcd(controller)   the driver for CH32RVUsbDeviceConfig::controller,
 *                          or nullptr when this part has none (Unsupported /
 *                          PortUnavailable)
 * and, when enterBootloader() can reach the factory USB bootloader,
 * CH32RV_USB_FACTORY_ISP_ENTRY with port_set_boot_mode().
 *
 * Extension point: the series below CH32X035 have no driver yet. Their clock
 * and PHY bring-up from the TinyUSB days (V103 USBFS with USBPRE; V20x USBFS
 * on PB6/PB7 and the USBD (fsdev) on PA11/PA12, USBPRE by SYSCLK, the parts
 * without one or the other; V30x USBFS, and USBHS with its PLL from HSE) is in
 * this file at commit 3ab1761 - each becomes a CH32RVUsbDcd of its own. */
#pragma once

#include "ch32rv_registers.h"
#include "CH32RVUsbDcd.h"

/* ======================================================= CH32X035 / X033 */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)

static const CH32RVUsbDcd *port_dcd(CH32RVUsbController controller)
{
    if (controller != CH32RVUsbController::Auto && controller != CH32RVUsbController::FullSpeed) {
        return nullptr;
    }
    return &ch32rv_usb_dcd_usbfs_x03x;
}

/* FLASH_STATR.BOOT_MODE: after the next software reset, run the BOOT area
 * (the factory bootloader). Unlocked by the flash keys, then the boot-mode
 * keys - the sequence ch32rv's `isp enter --via probe` uses (measured on
 * CH32X035F8U6, ch32rv docs/protocol/factory-isp.ja.md). */
#define CH32RV_USB_FACTORY_ISP_ENTRY 1
#define X03X_FLASH_KEYR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define X03X_FLASH_STATR         CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define X03X_FLASH_CTLR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define X03X_FLASH_BOOT_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x28u)
#define X03X_FLASH_KEY1          0x45670123u
#define X03X_FLASH_KEY2          0xCDEF89ABu
#define X03X_FLASH_STATR_BOOT_MODE (1u << 14)
#define X03X_FLASH_CTLR_LOCK     (1u << 7)

static void port_set_boot_mode(void)
{
    X03X_FLASH_KEYR = X03X_FLASH_KEY1;
    X03X_FLASH_KEYR = X03X_FLASH_KEY2;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY1;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY2;
    X03X_FLASH_STATR |= X03X_FLASH_STATR_BOOT_MODE;
    X03X_FLASH_CTLR |= X03X_FLASH_CTLR_LOCK;
}

/* ======================================================= CH32H417 / H416 / H415 */
#elif defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

/* Auto: USBSS where the part has it (H417 / H416), else USBFS. USBHS (PB8 /
 * PB9) shares its pins with SWCLK / SWDIO and takes them from the debug
 * interface, so only an explicit HighSpeed picks it. USBFS sits on PA11 /
 * PA12, where the factory bootloader listens too. */
static const CH32RVUsbDcd *port_dcd(CH32RVUsbController controller)
{
    switch (controller) {
    case CH32RVUsbController::FullSpeed:
        return &ch32rv_usb_dcd_usbfs_otg;
    case CH32RVUsbController::HighSpeed:
        return &ch32rv_usb_dcd_usbhs2;
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416)
    case CH32RVUsbController::Auto:
    case CH32RVUsbController::SuperSpeed:
        return &ch32rv_usb_dcd_usbss_h417;
#else
    case CH32RVUsbController::Auto:
        return &ch32rv_usb_dcd_usbfs_otg;
#endif
    default:
        return nullptr;
    }
}

/* No factory-ISP entry yet. The X035's way (FLASH_STATR.BOOT_MODE, then a
 * software reset) does reach the BOOT area here, but on the bench
 * (2026-10-20) no bootloader appeared on USBFS, BOOT_MODE then survived every
 * reset, and the application started through the BOOT area faulted. Until the
 * H417's bootloader is understood, enterBootloader() is a plain reset. */

/* ======================================================= the USBOTG_FS layout
 * V20x (PB6 / PB7), V30x, V205, L103 / M103, M030 (PA11 / PA12):
 * CH32RVUsbDcdUsbfsOtg.cpp. Their other controllers (V20x USBD, V30x and V205
 * USBHS) come with their drivers. */
#elif defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || \
    defined(CH32RV_SERIES_CH32M030)

static const CH32RVUsbDcd *port_dcd(CH32RVUsbController controller)
{
    if (controller == CH32RVUsbController::Auto || controller == CH32RVUsbController::FullSpeed) {
        return &ch32rv_usb_dcd_usbfs_otg;
    }
#if defined(CH32RV_SERIES_CH32V205)
    if (controller == CH32RVUsbController::HighSpeed) {
        return &ch32rv_usb_dcd_usbhs2;                   /* needs HSE */
    }
#endif
    return nullptr;
}

/* ======================================================= CH32X305 / X315, CH32V407 / V467
 * The second-generation USBHS (CH32RVUsbDcdUsbhs2.cpp); V407 / V467 have two,
 * USBHS1 by default and USBHS2 as HighSpeed2. X315's USBSS comes with its
 * driver. */
#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || defined(CH32RV_SERIES_CH32V407) || \
    defined(CH32RV_SERIES_CH32V467)

static const CH32RVUsbDcd *port_dcd(CH32RVUsbController controller)
{
    if (controller == CH32RVUsbController::Auto || controller == CH32RVUsbController::HighSpeed) {
        return &ch32rv_usb_dcd_usbhs2;
    }
#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
    if (controller == CH32RVUsbController::HighSpeed2) {
        return &ch32rv_usb_dcd_usbhs2_b;
    }
#endif
    return nullptr;
}

/* ======================================================= the rest: no driver yet */
#else

static const CH32RVUsbDcd *port_dcd(CH32RVUsbController controller)
{
    (void)controller;
    return nullptr;
}

#endif
