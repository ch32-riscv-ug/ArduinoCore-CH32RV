/* The controller driver interface of the device stack (CH32RVUsbDeviceCore.cpp).
 *
 * One table per controller kind (X035/X033 USBFS today; V103 USBFS, V20x
 * USBFS / USBD, V30x USBFS / USBHS, the second-generation USBHS and USBSS plug
 * in here later). The driver moves whole transfers - it splits them into
 * packets in its interrupt - and reports what happened through the
 * ch32rv_usbd_isr_* calls below; the core turns those into callbacks in normal
 * context. Nothing here is sized at build time: a driver states what its
 * hardware has in the table. */
#pragma once

#include <stdint.h>

#include "CH32RVUsbDeviceTypes.h"

enum CH32RVUsbXferType : uint8_t {
    CH32RV_USB_XFER_CONTROL = 0,
    CH32RV_USB_XFER_ISOCHRONOUS = 1,
    CH32RV_USB_XFER_BULK = 2,
    CH32RV_USB_XFER_INTERRUPT = 3,
};

struct CH32RVUsbDcd {
    const char *name;
    uint8_t endpoints;            /* endpoint numbers 0..endpoints-1 */
    uint16_t maxPacket;           /* largest packet any endpoint takes */
    uint8_t speeds;               /* bit n: CH32RVUsbSpeed n (Full, High, Super) can be reached */
    /* Clocks, pins, PHY, the controller reset, the interrupt. False when the
     * part cannot (the controller is missing, or taken by the USB host). */
    bool (*open)(void);
    void (*close)(void);
    void (*connect)(bool on);     /* the D+ pull-up */
    CH32RVUsbSpeed (*speed)(void);   /* the speed the host chose (after a reset) */
    void (*setAddress)(uint8_t address);
    /* Open one endpoint direction (address with bit 7 for IN). */
    bool (*epOpen)(uint8_t address, uint8_t type, uint16_t maxPacket);
    void (*epClose)(uint8_t address);
    void (*epCloseAll)(void);     /* every endpoint but 0 */
    /* Start a transfer of `length` bytes (0: a zero-length packet). An OUT
     * transfer ends on a short packet or when `length` is reached. The
     * buffer stays the caller's until the completion. */
    bool (*xfer)(uint8_t address, uint8_t *buffer, uint16_t length);
    /* Drop the transfer in flight without completing it; the endpoint NAKs
     * (isochronous: sends nothing) until the next xfer. */
    void (*abort)(uint8_t address);
    /* Stall, or clear the stall and reset the data toggle to DATA0. */
    void (*stall)(uint8_t address, bool on);
    void (*ep0Stall)(void);       /* both directions, until the next SETUP */
    void (*remoteWakeup)(void);   /* resume signalling (SuperSpeed: function wake) */
    /* Optional: called from the device task, for work a driver must not do
     * in its interrupt or has to time (USBSS: releasing a held link). */
    void (*poll)(void);
};

static inline bool ch32rv_usb_dcd_can(const CH32RVUsbDcd &dcd, CH32RVUsbSpeed speed)
{
    return ch32rv_usb_series_can(speed) && ((dcd.speeds >> (uint8_t)speed) & 1u);
}


/* From a driver's interrupt. */
extern "C" {
void ch32rv_usbd_isr_reset(void);
void ch32rv_usbd_isr_setup(const uint8_t setup[8]);
void ch32rv_usbd_isr_xfer(uint8_t address, uint16_t length);   /* a transfer finished */
void ch32rv_usbd_isr_suspend(void);
void ch32rv_usbd_isr_resume(void);
}

/* The drivers this build has (CH32RVUsbDevicePort.h picks one). */
extern const CH32RVUsbDcd ch32rv_usb_dcd_usbfs_x03x;
extern const CH32RVUsbDcd ch32rv_usb_dcd_usbss_h417;
extern const CH32RVUsbDcd ch32rv_usb_dcd_usbfs_otg;
