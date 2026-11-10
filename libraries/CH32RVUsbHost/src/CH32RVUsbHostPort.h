/* Which host controller driver (CH32RVUsbHcd.h) serves a series and port
 * (CH32RVUsbHostConfig::port), and how USBFS's pins and clock are set up.
 * Included once, by CH32RVUsbHost.cpp.
 *
 *   port_hcd(port)       the driver for that port, nullptr when the part has
 *                        none (Unsupported)
 * and for the USBFS driver (a driver with its own clock - USBHS - opens it in
 * open() and keeps it in poll()):
 *   port_open()          clock, PHY and pins for host mode; false when the
 *                        clock cannot feed the PHY or the part has no USBFS
 *   port_close()
 *   port_has_host()      whether this die has the host half
 *   port_poll()          from the task: put back a clock taken away under it
 *
 * The ports per series:
 *   X035 / X033, V20x, L103 / M103, M030   DEFAULT = FULL_SPEED: USBFS
 *   V30x                                   DEFAULT = FULL_SPEED: USBFS (its USBHS host is not here yet)
 *   V205, H41x                             DEFAULT = FULL_SPEED: USBFS; HIGH_SPEED: USBHS (H41x: PB8 / PB9,
 *                                          the SWD pins - only when asked)
 *   X305 / X315                            DEFAULT = HIGH_SPEED: USBHS
 *   V407 / V467                            DEFAULT = HIGH_SPEED: USBHS1; HIGH_SPEED_2: USBHS2 */
#pragma once

#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_usbhs2.h"
#include "internal/CH32RVUsbHcd.h"

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

static void port_poll(void) {}

#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || defined(CH32RV_SERIES_CH32V407) || \
    defined(CH32RV_SERIES_CH32V467)
/* No USBFS: the USBHS driver alone. */
#define PORT_NO_USBFS 1

static bool port_open(void) { return false; }
static void port_close(void) {}
static void port_poll(void) {}

#else /* the USBOTG_FS layout: V20x, V30x, V205, L103 / M103, M030, H41x */

#include "internal/ch32rv_usbfs_otg.h"

#define USBFS_BASE_CTRL        (*(volatile uint8_t *)CH32RV_USBFS_OTG_BASE)

/* The 48 MHz and the pads as the device driver makes them; the host's 15 kOhm
 * pull-downs are the controller's own (HOST_CTRL), set by the driver. */
static bool port_open(void)
{
#if defined(CH32RV_PART_CH32V203F6P6) || defined(CH32RV_PART_CH32V203G6U6)
    return false;        /* no USBFS on the part */
#else
    return ch32rv_usbfs_otg_clock_on();
#endif
}

static void port_close(void)
{
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
    ch32rv_clock_disable(OTG_FS);
#else
    ch32rv_clock_disable(USBFS);
#endif
}

/* SystemInit() run again by a debug probe turns the extra PLLs off (the
 * device driver's dcd_poll() does the same). */
static void port_poll(void)
{
    if (!ch32rv_usbfs_otg_clock_ok()) {
        ch32rv_usbfs_otg_clock_on();
    }
}

#endif

/* Whether this die has the host half. CH32X035R8T6 / C8T6 / G8U6 / G8R6 do;
 * F8U6, F7P6, D8U6 and CH32X033F8P6 are device-only (datasheet model table),
 * and there BASE_CTRL.HOST_MODE does not stick - the EVT polls for it, and on
 * an F8U6 it reads 0 however long it is given (measured 2026-10-15). So the
 * bit is the test, which also covers a board built for "ANY". */
static bool port_has_host(void)
{
#ifdef PORT_NO_USBFS
    return false;
#else
    USBFS_BASE_CTRL = 0x80;
    const uint32_t t0 = micros();
    while (!(USBFS_BASE_CTRL & 0x80) && (uint32_t)(micros() - t0) < 2000u) {
    }
    const bool has = (USBFS_BASE_CTRL & 0x80) != 0;
    USBFS_BASE_CTRL = 0;
    return has;
#endif
}

static const CH32RVUsbHcd *port_hcd(uint8_t port)
{
#if defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || defined(CH32RV_SERIES_CH32V407) || \
    defined(CH32RV_SERIES_CH32V467)
    if (port == CH32RV_USB_HOST_PORT_DEFAULT || port == CH32RV_USB_HOST_PORT_HIGH_SPEED) {
        return &ch32rv_usb_hcd_usbhs2;
    }
#if CH32RV_USBHS2_UNITS == 2
    if (port == CH32RV_USB_HOST_PORT_HIGH_SPEED_2) {
        return &ch32rv_usb_hcd_usbhs2_b;
    }
#endif
    return nullptr;
#else
    if (port == CH32RV_USB_HOST_PORT_DEFAULT || port == CH32RV_USB_HOST_PORT_FULL_SPEED) {
        return &ch32rv_usb_hcd_usbfs;
    }
#ifdef CH32RV_USBHS2_UNITS
    if (port == CH32RV_USB_HOST_PORT_HIGH_SPEED) {
        return &ch32rv_usb_hcd_usbhs2;
    }
#endif
    return nullptr;
#endif
}

/* Whether `h` is the USBFS driver (the one port_open() and the rest are for). */
#ifdef PORT_NO_USBFS
#define port_is_usbfs(h) ((void)(h), false)
#else
#define port_is_usbfs(h) ((h) == &ch32rv_usb_hcd_usbfs)
#endif
