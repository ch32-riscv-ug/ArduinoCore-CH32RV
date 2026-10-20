/* What the host stack (CH32RVUsbHostCore.cpp) asks of a host controller
 * driver, the host-side twin of CH32RVUsbDevice's CH32RVUsbDcd.h. One table
 * per controller; CH32RVUsbHostPort.h picks the series'. Not for sketches.
 *
 * A driver keeps any number of endpoints open, each with at most one transfer
 * pending, and runs them itself (NAK retries, interrupt polling intervals,
 * toggles). It reports from its interrupt through ch32rv_usbh_isr_*; the stack
 * acts on those in normal context (ch32rv_usb_host_task()). */
#pragma once

#include <stdint.h>

/* Transfer results. */
#define CH32RV_USBH_OK      0
#define CH32RV_USBH_FAILED  1   /* no handshake, babble, overrun */
#define CH32RV_USBH_STALLED 2
#define CH32RV_USBH_TIMEOUT 3   /* a control transfer NAKed for 5 s (USB 2.0 9.2.6.4) */

/* Endpoint types, as bmAttributes. */
#define CH32RV_USBH_CONTROL     0
#define CH32RV_USBH_ISOCHRONOUS 1
#define CH32RV_USBH_BULK        2
#define CH32RV_USBH_INTERRUPT   3

/* Device speeds. */
#define CH32RV_USBH_LOW  0
#define CH32RV_USBH_FULL 1
#define CH32RV_USBH_HIGH 2

struct CH32RVUsbHcd {
    const char *name;
    uint16_t maxPacket;           /* the largest packet an endpoint takes */
    bool (*open)(void);           /* false: this part has no host half, or the device stack has it */
    void (*close)(void);
    bool (*connected)(void);      /* something on the root port */
    /* Root port reset: start it, then end it (10 ms or more later) and read
     * the speed of what is attached (CH32RV_USBH_*). */
    void (*portReset)(void);
    uint8_t (*portResetEnd)(void);
    void (*portDisable)(void);    /* stop the port (no SOF) until the next reset */
    /* An endpoint of device `daddr`: EP0 opens both directions (address 0).
     * `speed` is the device's; a low-speed device behind a full-speed hub
     * needs the PRE packet, which the driver adds when the root port runs at
     * full speed. `interval` in frames (interrupt). */
    bool (*epOpen)(uint8_t daddr, uint8_t address, uint8_t type, uint16_t mps, uint8_t interval, uint8_t speed);
    void (*epClose)(uint8_t daddr, uint8_t address);
    void (*deviceClose)(uint8_t daddr);   /* every endpoint of it */
    /* Start a transfer; it ends on a short packet or `length`. EP0's data and
     * status stages are xfer() on 0x80 / 0x00. */
    bool (*xfer)(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length);
    bool (*setup)(uint8_t daddr, const uint8_t packet[8]);
    void (*abort)(uint8_t daddr, uint8_t address);   /* no completion for it */
    void (*resetToggle)(uint8_t daddr, uint8_t address);   /* after CLEAR_FEATURE(ENDPOINT_HALT) */
    uint32_t (*frame)(void);      /* frames (ms) since open */
};

/* From a driver's interrupt. */
extern "C" {
void ch32rv_usbh_isr_port(bool attached);
void ch32rv_usbh_isr_xfer(uint8_t daddr, uint8_t address, uint8_t result, uint16_t length);
}

extern const CH32RVUsbHcd ch32rv_usb_hcd_usbfs;
