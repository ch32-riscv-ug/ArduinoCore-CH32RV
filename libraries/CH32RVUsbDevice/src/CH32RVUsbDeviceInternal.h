/* Shared between the library's own files; not for sketches. */
#pragma once

#include "CH32RVUsbDevice.h"

/* String index of function i: the device's own are 1..3. */
#define CH32RV_USB_FIRST_FUNCTION_STRING 4

/* The BOS and Microsoft OS 2.0 descriptor set (CH32RVUsbDevicePlatform.cpp).
 * `build` is set only when a function that needs them is declared (the
 * vendor interface, the DFU runtime), so a device without one links none of
 * it; begin() calls it once the interfaces are numbered. */
struct CH32RVUsbPlatform {
    bool (*build)(const CH32RVUsbDevice &device);
    /* The BOS and the Microsoft OS 2.0 set are produced when asked for, not
     * kept: their writers, lengths (0: none) and the set's revision. */
    void (*bosWrite)(CH32RVUsbDescriptorWriter &w);
    uint16_t bosLength;
    void (*msOs20Write)(CH32RVUsbDescriptorWriter &w);
    uint16_t msOs20Length;
    uint16_t msOs20Revision;
    bool msOs20Subsets;
    /* Vendor requests to the device, as the device stack hands them over
     * (set when the platform descriptors are linked), and the ones the
     * descriptors do not answer (CH32RVUsbDeviceVendor::onControlRequest). */
    bool (*request)(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup);
    bool (*vendorControl)(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup);
};
/* Answer a GET with what `write` produces, an EP0 packet at a time. */
bool ch32rv_usb_control_in_written(CH32RVUsbDevice &device, uint16_t length,
                                   void (*write)(CH32RVUsbDescriptorWriter &w));
extern CH32RVUsbPlatform ch32rv_usb_platform;

/* The SuperSpeed part of a BOS (USB 2.0 extension, SuperSpeed USB device
 * capability), and whether this device's controller reaches SuperSpeed - set
 * by begin() before the platform builds its BOS, which then carries it too:
 * a 3.x device must have both. */
extern const uint8_t ch32rv_usb_ss_caps[17];
extern bool ch32rv_usb_bos_super;

/* String descriptors a function needs besides its name (the NCM MAC address):
 * indexes from CH32RV_USB_EXTRA_STRING up, answered by this hook. */
#define CH32RV_USB_EXTRA_STRING 0xE0
extern const char *(*ch32rv_usb_extra_string)(uint8_t index);
void ch32rv_usb_platform_use(void);

/* Endpoint parameters by speed. Bulk: 64 bytes at full speed, 512 at high
 * speed, 1024 at SuperSpeed. Interrupt intervals: milliseconds at full speed;
 * at high speed and SuperSpeed the descriptor counts 2^(bInterval-1) units
 * of 125 us, so the period is rounded down to a power of two (10 ms -> 8 ms),
 * never slower than asked. */
static inline uint16_t ch32rv_usb_bulk_size(CH32RVUsbSpeed speed)
{
    return ch32rv_usb_at(speed, CH32RVUsbSpeed::Super) ? 1024 : ch32rv_usb_at(speed, CH32RVUsbSpeed::High) ? 512 : 64;
}

static inline uint8_t ch32rv_usb_interval(CH32RVUsbSpeed speed, uint8_t ms)
{
    if (!ch32rv_usb_series_can(CH32RVUsbSpeed::High) || speed == CH32RVUsbSpeed::Full) {
        return ms ? ms : 1;
    }
    /* b gives 2^(b-4) ms: the largest that is not longer than ms. */
    uint8_t b = 4;
    while (b < 16 && (1u << (b - 3)) <= ms) {
        b++;
    }
    return b;
}

/* Run once from the device task, after the stack's own work and outside any
 * USB callback (CH32RVUsbDeviceDfuRuntime's switch to DFU mode, which ends and
 * begins the device). */
extern void (*volatile ch32rv_usb_deferred)(void);

/* CH32RVUsbDeviceConfig::hseHz of the device that is opening its
 * controller, for drivers whose PHY clock is divided from the crystal. */
extern uint32_t ch32rv_usb_hse_hz;
