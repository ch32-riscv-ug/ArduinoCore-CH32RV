/* Shared between the library's own files; not for sketches. */
#pragma once

#include "CH32RVUsbDevice.h"

/* The BOS and Microsoft OS 2.0 descriptor set (CH32RVUsbDevicePlatform.cpp).
 * `build` is set only when a function that needs them is declared (the
 * vendor interface, the DFU runtime), so a device without one links none of
 * it; begin() calls it once the interfaces are numbered. */
struct CH32RVUsbPlatform {
    bool (*build)(const CH32RVUsbDevice &device);
    const uint8_t *bos;
    uint16_t bosLength;
    const uint8_t *msOs20;
    uint16_t msOs20Length;
    uint16_t msOs20Revision;
    bool msOs20Subsets;
    /* Vendor requests the platform descriptors do not answer
     * (CH32RVUsbDeviceVendor::onControlRequest). */
    bool (*vendorRequest)(uint8_t rhport, uint8_t stage, const void *request);
};
extern CH32RVUsbPlatform ch32rv_usb_platform;
void ch32rv_usb_platform_use(void);
