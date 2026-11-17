/* Which host controller driver (internal/CH32RVUsbHcd.h) serves a series and
 * port (CH32RVUsbHostConfig::port). Included by CH32RVUsbHost.h, so that
 * begin() picks the driver where the sketch calls it: with the port a
 * constant there (the default, or a config the sketch fills in), only that
 * driver's table is referenced and linked. Each driver prepares its own
 * clock, PHY and pins (CH32RVUsbHcd::prepare / open).
 *
 *   CH32RV_USB_HOST_STACK          1 on the series with a host controller driver
 *   ch32rv_usb_host_port_hcd(port) the driver for that port, nullptr when the
 *                                  part has none (Unsupported)
 *
 * The ports per series:
 *   X035 / X033, V20x, L103 / M103, M030   DEFAULT = FULL_SPEED: USBFS
 *   V103                                   DEFAULT = FULL_SPEED: its USB (the older USBFS IP)
 *   V303                                   DEFAULT = FULL_SPEED: USBFS
 *   V305 / V307 / V317                     DEFAULT = FULL_SPEED: USBFS; HIGH_SPEED: the first-generation USBHS
 *                                          (PB6 / PB7, needs HSE)
 *   V205, H41x                             DEFAULT = FULL_SPEED: USBFS; HIGH_SPEED: USBHS (H41x: PB8 / PB9,
 *                                          the SWD pins - only when asked)
 *   H417 / H416                            also SUPER_SPEED: USBSS, SuperSpeed only (not yet tested on
 *                                          hardware)
 *   X305 / X315                            DEFAULT = HIGH_SPEED: USBHS
 *   V407 / V467                            DEFAULT = HIGH_SPEED: USBHS1; HIGH_SPEED_2: USBHS2 */
#pragma once

#include <stdint.h>

#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033) || defined(CH32RV_SERIES_CH32V203) || \
    defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) ||  \
    defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V205) ||  \
    defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || defined(CH32RV_SERIES_CH32M030) ||  \
    defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) ||  \
    defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || defined(CH32RV_SERIES_CH32V407) ||  \
    defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32V103)
#define CH32RV_USB_HOST_STACK 1
#else
#define CH32RV_USB_HOST_STACK 0
#endif

struct CH32RVUsbHcd;
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbfs;      /* USBFS, the OTG_FS layout, V103's USB */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbhs1;     /* V305 / V307 / V317: the first-generation USBHS */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbhs2;     /* the second-generation USBHS (V407 / V467: USBHS1) */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbhs2_b;   /* V407 / V467: USBHS2 */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbss;      /* H417 / H416: USBSS, SuperSpeed only */

static inline __attribute__((always_inline)) const CH32RVUsbHcd *ch32rv_usb_host_port_hcd(uint8_t port)
{
#if !CH32RV_USB_HOST_STACK
    (void)port;
    return nullptr;
#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || defined(CH32RV_SERIES_CH32V407) || \
    defined(CH32RV_SERIES_CH32V467)
    if (port == CH32RV_USB_HOST_PORT_DEFAULT || port == CH32RV_USB_HOST_PORT_HIGH_SPEED) {
        return &ch32rv_usb_hcd_usbhs2;
    }
#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
    if (port == CH32RV_USB_HOST_PORT_HIGH_SPEED_2) {
        return &ch32rv_usb_hcd_usbhs2_b;
    }
#endif
    return nullptr;
#else
    if (port == CH32RV_USB_HOST_PORT_DEFAULT || port == CH32RV_USB_HOST_PORT_FULL_SPEED) {
        return &ch32rv_usb_hcd_usbfs;
    }
#if defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || \
    defined(CH32RV_SERIES_CH32H415)
    if (port == CH32RV_USB_HOST_PORT_HIGH_SPEED) {
        return &ch32rv_usb_hcd_usbhs2;
    }
#endif
#if defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
    if (port == CH32RV_USB_HOST_PORT_HIGH_SPEED) {
        return &ch32rv_usb_hcd_usbhs1;
    }
#endif
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416)
    if (port == CH32RV_USB_HOST_PORT_SUPER_SPEED) {
        return &ch32rv_usb_hcd_usbss;
    }
#endif
    return nullptr;
#endif
}
