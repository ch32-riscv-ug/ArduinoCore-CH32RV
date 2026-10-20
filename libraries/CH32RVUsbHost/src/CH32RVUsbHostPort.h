/* Which host controller driver (CH32RVUsbHcd.h) serves a series, and how its
 * pins and clock are set up. Included once, by CH32RVUsbHost.cpp.
 *
 * Every series with a host provides:
 *   port_hcd()        the driver
 *   port_open()       clock, PHY and pins for host mode; false when the clock
 *                     cannot feed the PHY or the part has no such block
 *   port_close()
 *   port_has_host()   whether this die has the host half
 * Extension point: another controller (V30x USBHS, the second-generation
 * USBHS) becomes a table of its own and a branch here. */
#pragma once

#include "ch32rv_registers.h"
#include "CH32RVUsbHcd.h"

#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
/* CH32X035 / X033: USBFS on PC16 (D-) / PC17 (D+). Host mode wants the 15 kOhm
 * pull-downs (the controller's, UH_PD_DIS clear) and no pull-up, the PHY on
 * the 3.3 V supply, and the pads handed to it (EVT USBFS_Host_Init). */
#define X03X_AFIO_UDM_PUE_MASK 0x00000003u
#define X03X_AFIO_UDP_PUE_MASK 0x0000000Cu
#define X03X_AFIO_USB_PHY_V33  0x00000040u
#define X03X_AFIO_USB_IOEN     0x00000080u
#define USBFS_BASE_CTRL        (*(volatile uint8_t *)0x40023400u)

static bool port_open(void)
{
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    pinMode(PC16, INPUT_PULLDOWN);
    pinMode(PC17, INPUT_PULLDOWN);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK)) |
                       X03X_AFIO_USB_PHY_V33 | X03X_AFIO_USB_IOEN;
    return true;
}

static void port_close(void)
{
    CH32RV_AFIO_CTLR &= ~X03X_AFIO_USB_IOEN;
}

#else /* V20x / V30x OTG_FS */

#define USBFS_BASE_CTRL        (*(volatile uint8_t *)0x50000000u)
/* RCC_CFGR0[23:22] USBPRE: 48 MHz for the PHY from the PLL (as the device). */
#define HOST_USBPRE_SHIFT 22u
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define HOST_USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define HOST_USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000
#define HOST_USBPRE 2u
#endif

static bool port_open(void)
{
#if defined(HOST_USBPRE) && !defined(CH32RV_PART_CH32V203F6P6) && !defined(CH32RV_PART_CH32V203G6U6)
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << HOST_USBPRE_SHIFT)) | (HOST_USBPRE << HOST_USBPRE_SHIFT);
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)
    pinMode(PB6, INPUT);
    pinMode(PB7, INPUT);
#else
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
#endif
    ch32rv_clock_enable(USBFS);
    return true;
#else
    return false;        /* no 48 MHz for the PHY at this clock, or no USBFS on the part */
#endif
}

static void port_close(void)
{
    ch32rv_clock_disable(USBFS);
}

#endif

/* Whether this die has the host half. CH32X035R8T6 / C8T6 / G8U6 / G8R6 do;
 * F8U6, F7P6, D8U6 and CH32X033F8P6 are device-only (datasheet model table),
 * and there BASE_CTRL.HOST_MODE does not stick - the EVT polls for it, and on
 * an F8U6 it reads 0 however long it is given (measured 2026-10-15). So the
 * bit is the test, which also covers a board built for "ANY". */
static bool port_has_host(void)
{
    USBFS_BASE_CTRL = 0x80;
    const uint32_t t0 = micros();
    while (!(USBFS_BASE_CTRL & 0x80) && (uint32_t)(micros() - t0) < 2000u) {
    }
    const bool has = (USBFS_BASE_CTRL & 0x80) != 0;
    USBFS_BASE_CTRL = 0;
    return has;
}

static const CH32RVUsbHcd *port_hcd(void)
{
    return &ch32rv_usb_hcd_usbfs;
}
