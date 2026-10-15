/* Ours (tools/vendor/vendor_tinyusb.py OURS).
 * Which controller rhport 0 drives, on the V30x parts with USBFS and USBHS.
 * Set before tusb_init(); changing it while the stack runs is refused by the
 * caller (CH32RVUsbDevice::begin() only selects when not started). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32RV_DCD_USBFS 0u
#define CH32RV_DCD_USBHS 1u
#define CH32RV_DCD_USBD  2u   /* V20x */

bool ch32rv_dcd_select(uint8_t which);
uint8_t ch32rv_dcd_selected(void);

#ifdef __cplusplus
}
#endif
