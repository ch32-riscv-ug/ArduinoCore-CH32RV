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

/* Endpoint parameters by speed. Bulk: 64 bytes at full speed, 512 at high
 * speed. Interrupt intervals: milliseconds at full speed; at high speed the
 * descriptor counts 2^(bInterval-1) microframes of 125 us, so the period is
 * rounded down to a power of two (10 ms -> 8 ms), never slower than asked. */
static inline uint16_t ch32rv_usb_bulk_size(bool hs)
{
    return hs ? 512 : 64;
}

static inline uint8_t ch32rv_usb_interval(bool hs, uint8_t ms)
{
    if (!hs) {
        return ms ? ms : 1;
    }
    /* b gives 2^(b-4) ms: the largest that is not longer than ms. */
    uint8_t b = 4;
    while (b < 16 && (1u << (b - 3)) <= ms) {
        b++;
    }
    return b;
}
