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
#define CH32RV_USBH_SUPER 3   /* USB 3 Gen 1 (USBSS) */

/* portResetEnd(): the reset is still running, ask again later. */
#define CH32RV_USBH_RESET_PENDING 0xFF

/* An endpoint to open. `interval` is bInterval as the descriptor has it (the
 * driver turns it into its own ticks). The SuperSpeed fields are the
 * endpoint companion's (USB 3.2 9.6.7), 0 elsewhere: `maxBurst` bMaxBurst
 * (packets per burst - 1), `attributes` its bmAttributes (bulk: MaxStreams
 * in bits 4:0, isochronous: Mult in bits 1:0), `bytesPerInterval`
 * wBytesPerInterval (periodic endpoints). */
struct CH32RVUsbHcdEp {
    uint8_t address, type;
    uint16_t mps;
    uint8_t interval, speed;
    uint8_t maxBurst, attributes;
    uint16_t bytesPerInterval;
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
     * full speed. */
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
