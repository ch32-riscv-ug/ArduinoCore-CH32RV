/* What the host stack (CH32RVUsbHostCore.cpp) asks of a host controller
 * driver, the host-side twin of CH32RVUsbDevice's CH32RVUsbDcd.h. One table
 * per controller; CH32RVUsbHostPort.h picks the series' (only the one a sketch's
 * port names is linked). Not for sketches.
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
#define CH32RV_USBH_SUPER 3   /* USB 3 Gen 1 (USBSS) */

/* portResetEnd(): the reset is still running, ask again later. */
#define CH32RV_USBH_RESET_PENDING 0xFF

/* An endpoint to open. `interval` is bInterval as the descriptor has it (the
 * driver turns it into its own ticks). The SuperSpeed fields are the
 * endpoint companion's (USB 3.2 9.6.7), 0 elsewhere: `maxBurst` bMaxBurst
 * (packets per burst - 1), `attributes` its bmAttributes (bulk: MaxStreams
 * in bits 4:0, isochronous: Mult in bits 1:0), `bytesPerInterval`
 * wBytesPerInterval (periodic endpoints). `ttHub` / `ttPort`: a full- or
 * low-speed device behind a high-speed hub is reached through that hub's
 * transaction translator (USB 2.0 11.14) - the address of the nearest
 * high-speed hub on the way to the root and the port of it the way goes down
 * (split transactions, the driver's); 0 / 0 when there is none. A driver
 * without split transactions refuses such an endpoint. */
struct CH32RVUsbHcdEp {
    uint8_t address, type;
    uint16_t mps;
    uint8_t interval, speed;
    uint8_t maxBurst, attributes;
    uint16_t bytesPerInterval;
    uint8_t ttHub, ttPort;
};

struct CH32RVUsbHcd {
    const char *name;
    uint16_t maxPacket;           /* the largest packet an endpoint takes */
    bool (*open)(void);           /* false: this part has no host half, or the device stack has it */
    void (*close)(void);
    bool (*connected)(void);      /* something on the root port */
    /* Root port reset: start it, then end it (10 ms or more later) and read
     * the speed of what is attached (CH32RV_USBH_*), or
     * CH32RV_USBH_RESET_PENDING while a longer reset (SuperSpeed's warm
     * reset) still runs - the stack asks again. */
    void (*portReset)(void);
    uint8_t (*portResetEnd)(void);
    void (*portDisable)(void);    /* stop the port (no SOF) until the next reset */
    /* An endpoint of device `daddr`: EP0 opens both directions (address 0).
     * `speed` is the device's; a low-speed device behind a full-speed hub
     * needs the PRE packet, which the driver adds when the root port runs at
     * full speed; one behind a high-speed hub (ttHub) goes by split
     * transactions. */
    bool (*epOpen)(uint8_t daddr, const CH32RVUsbHcdEp &ep);
    void (*epClose)(uint8_t daddr, uint8_t address);
    void (*deviceClose)(uint8_t daddr);   /* every endpoint of it */
    /* Start a transfer; it ends on a short packet or `length`. EP0's data and
     * status stages are xfer() on 0x80 / 0x00. */
    bool (*xfer)(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length);
    bool (*setup)(uint8_t daddr, const uint8_t packet[8]);
    void (*abort)(uint8_t daddr, uint8_t address);   /* no completion for it */
    void (*resetToggle)(uint8_t daddr, uint8_t address);   /* after CLEAR_FEATURE(ENDPOINT_HALT) */
    uint32_t (*frame)(void);      /* frames (ms) since open */
    void (*poll)(void);           /* from the task: put back a clock taken away under it (nullptr: nothing) */
    /* Where device `daddr` sits, before its EP0 opens (address 0 too): the
     * SuperSpeed route string (USB 3.2 8.9: 4 bits per hub tier, the port
     * on the hub nearest the root in bits 3:0; 0 on the root port). nullptr:
     * the driver does not need it. */
    void (*deviceAt)(uint8_t daddr, uint32_t route);
    /* Link power management of the root port (nullptr: none). linkAllow:
     * accept the device's own entry into U1 / U2 (after the stack sent
     * SET_SEL and U1_ENABLE / U2_ENABLE). portSuspend: true sends the link to
     * U3 (suspend), false wakes it to U0; false when the link is not up. */
    void (*linkAllow)(bool u1, bool u2);
    bool (*portSuspend)(bool suspend);
    /* Before open(): the part's clock, PHY and pins for host mode, and
     * whether the die has the host half - false is Unsupported (open()'s
     * false is Busy). release() undoes it after close(). nullptr: nothing
     * (the driver opens its own clock in open()). */
    bool (*prepare)(void);
    void (*release)(void);
    /* Packets larger than the classes' static buffers (CH32RV_USB_HOST_MAX_PACKET)
     * on this controller - V30x's first-generation USBHS, 512 bytes where the
     * series' buffers are full speed's 64: where the classes get bigger ones
     * (malloc / free). nullptr: the static buffers hold every packet, and the
     * heap is not linked. */
    void *(*bufferAlloc)(unsigned bytes);
    void (*bufferFree)(void *buffer);
};

/* From a driver's interrupt. */
extern "C" {
void ch32rv_usbh_isr_port(bool attached);
void ch32rv_usbh_isr_xfer(uint8_t daddr, uint8_t address, uint8_t result, uint16_t length);
}

extern const CH32RVUsbHcd ch32rv_usb_hcd_usbfs;
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbhs2;     /* the second-generation USBHS (V407 / V467: USBHS1) */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbhs2_b;   /* V407 / V467: USBHS2 */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbss;      /* H417 / H416: USBSS, SuperSpeed only */
extern const CH32RVUsbHcd ch32rv_usb_hcd_usbhs1;     /* V305 / V307 / V317: the first-generation USBHS */
