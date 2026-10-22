/* Host controller driver for the WCH USBFS block in host mode
 * (CH32X035/X033, and the OTG_FS of V20x, V30x, V205, L103 / M103, M030 and
 * H41x).
 *
 * It began as the TinyUSB host controller driver here (TinyUSB's own
 * portable/wch/hcd_ch32_usbfs.c busy-waits, panics on surprises, retries a
 * STALL by itself and runs one transfer at a time); it keeps that file's
 * register knowledge (and the EVT's, HOST_KM's USBFSH_Transact) and is the
 * CH32RVUsbHcd table (CH32RVUsbHcd.h) now:
 *
 *   - every open endpoint can have a transfer pending at once (HID interrupt
 *     IN, CDC bulk IN and a control transfer all waiting is ordinary). The
 *     hardware has a single transaction engine, so a scheduler runs them one
 *     transaction at a time, round robin;
 *   - a NAK puts the endpoint back to wait: bulk and control retry at the next
 *     frame, interrupt endpoints at their next polling interval. The SOF
 *     interrupt (every 1 ms) is the clock;
 *   - STALL, a missing handshake (three tries) and anything unexpected end the
 *     transfer with the result the classes act on;
 *   - IN data with the wrong toggle (the device repeating a packet whose ACK it
 *     lost) is dropped and asked for again, as the EVT does;
 *   - abort, close and detach clean up instead of leaving the engine busy.
 *
 * Packets go through one 64-byte DMA buffer each way: full speed caps bulk,
 * interrupt and control at 64 bytes. Isochronous endpoints get one packet per
 * frame without a handshake.
 */
#include <Arduino.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "ch32rv_usbfs_irq.h"
#include "CH32RVUsbHcd.h"

/* Which block, where, and how its registers are laid out. */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CH32RV_HCD_USBFS_BASE   0x40023400u
#define CH32RV_HCD_USBFS_LAYOUT 1
#else
#include "ch32rv_usbfs_otg.h"
#ifdef CH32RV_USBFS_OTG_BASE      /* V20x, V30x, V205, L103 / M103, M030, H41x */
#define CH32RV_HCD_USBFS_BASE   CH32RV_USBFS_OTG_BASE
#define CH32RV_HCD_USBFS_LAYOUT 2
#endif
#endif

#ifdef CH32RV_HCD_USBFS_BASE

/* ------------------------------------------------------------- registers */

#define R8(off)  (*(volatile uint8_t *)(CH32RV_HCD_USBFS_BASE + (off)))
#define R16(off) (*(volatile uint16_t *)(CH32RV_HCD_USBFS_BASE + (off)))
#define R32(off) (*(volatile uint32_t *)(CH32RV_HCD_USBFS_BASE + (off)))

/* The host registers overlay the device ones, and the two layouts of the
 * block place them differently (EVT USBFSH_TypeDef):
 *   1  CH32X035 / X033: endpoint registers 4 bytes apart from 0x20, DMA
 *      addresses 16 bit (SRAM's low half), the toggles in bits 7 / 6;
 *   2  the USBOTG_FS layout (V20x, V30x, V205, L103 / M103, M030, H41x - EVT
 *      USBFSH_TypeDef compared 2026-10-22): endpoint registers from 0x30, DMA addresses
 *      32 bit, HOST_SETUP 16 bit (SOF_EN bit 2, PRE_PID_EN bit 10), both
 *      toggles in bit 2. */

#define BASE_CTRL   R8(0x00)
#define HOST_CTRL   R8(0x01)
#define INT_EN      R8(0x02)
#define DEV_ADDR    R8(0x03)
#define MIS_ST      R8(0x05)
#define INT_FG      R8(0x06)
#define INT_ST      R8(0x07)
#define RX_LEN      R16(0x08)
#define HOST_EP_MOD R8(0x0D)
#if CH32RV_HCD_USBFS_LAYOUT == 1
#define SET_RX_DMA(a) (R16(0x18) = (uint16_t)(uintptr_t)(a))
#define SET_TX_DMA(a) (R16(0x1C) = (uint16_t)(uintptr_t)(a))
#define HOST_SETUP  R8(0x26)
#define HOST_EP_PID R8(0x28)
#define HOST_RX_CTRL R8(0x2A)
#define HOST_TX_LEN R8(0x2C)
#define HOST_TX_CTRL R8(0x2E)
#define UH_PRE_PID_EN  0x80
#define UH_SOF_EN      0x40
#define UH_R_TOG       0x80
#define UH_T_TOG       0x40
#else
#define SET_RX_DMA(a) (R32(0x18) = (uint32_t)(uintptr_t)(a))
#define SET_TX_DMA(a) (R32(0x1C) = (uint32_t)(uintptr_t)(a))
#define HOST_SETUP  R16(0x36)
#define HOST_EP_PID R8(0x38)
#define HOST_RX_CTRL R8(0x3B)
#define HOST_TX_LEN R16(0x3C)
#define HOST_TX_CTRL R8(0x3E)
#define UH_PRE_PID_EN  0x0400
#define UH_SOF_EN      0x0004
#define UH_R_TOG       0x04
#define UH_T_TOG       0x04
#endif

#define UC_HOST_MODE   0x80
#define UC_LOW_SPEED   0x40
#define UC_INT_BUSY    0x08
#define UC_RESET_SIE   0x04
#define UC_CLR_ALL     0x02
#define UC_DMA_EN      0x01

#define UH_PD_DIS      0x80
#define UH_LOW_SPEED   0x04
#define UH_BUS_RESET   0x02
#define UH_PORT_EN     0x01

#define UIE_HST_SOF    0x08
#define UIE_TRANSFER   0x02
#define UIE_DETECT     0x01

#define UIF_HST_SOF    0x08
#define UIF_TRANSFER   0x02
#define UIF_DETECT     0x01

#define UMS_DM_LEVEL   0x02
#define UMS_DEV_ATTACH 0x01

#define UIS_TOG_OK     0x40
#define UIS_H_RES_MASK 0x0F

#define UH_EP_TX_EN    0x40
#define UH_EP_RX_EN    0x08
#define UH_R_RES       0x01
#define UH_T_RES       0x01

#define PID_OUT   0x1
#define PID_IN    0x9
#define PID_SETUP 0xD
#define PID_ACK   0x2
#define PID_NAK   0xA
#define PID_STALL 0xE
#define PID_DATA0 0x3
#define PID_DATA1 0xB

#define PACKET_MAX 64u
#define NO_RESPONSE_TRIES 3u
/* A control transfer a device keeps NAKing ends here: USB 2.0 9.2.6.4 gives a
 * device 5 s for any request (TinyUSB's own control stage has no timeout, and
 * its synchronous calls would wait forever). Bulk and interrupt wait for as
 * long as it takes - that is what they are for. */
#define CONTROL_TIMEOUT_FRAMES 5000u

static uint8_t rx_buf[PACKET_MAX] __attribute__((aligned(4)));
static uint8_t tx_buf[PACKET_MAX] __attribute__((aligned(4)));

/* ------------------------------------------------------------- state */
enum { EP_FREE = 0, EP_IDLE, EP_PENDING };

typedef struct {
    uint8_t state;
    uint8_t daddr;
    uint8_t ep_addr;
    uint8_t type;          /* CH32RV_USBH_CONTROL ... */
    uint8_t speed;         /* the device's, CH32RV_USBH_LOW / FULL */
    uint8_t interval;      /* frames between interrupt polls */
    uint8_t toggle;
    uint8_t setup;         /* the pending transfer is a SETUP packet */
    uint8_t tries;         /* consecutive transactions without a handshake */
    uint16_t mps;
    uint16_t len;
    uint16_t done;
    uint8_t *buf;
    uint32_t due;          /* frame number before which it is not tried */
    uint32_t since;        /* frame the transfer was queued */
    uint8_t setup_pkt[8];  /* EP0 OUT: the SETUP packet, copied to tx_buf when sent */
} hcd_ep_t;

/* Every endpoint of every device the stack keeps at once: EP0 both ways for
 * the devices and address 0, and the others the classes open. */
#define EP_MAX 32
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static hcd_ep_t eps[EP_MAX];
static volatile int16_t active = -1;      /* index of the transaction on the wire */
static volatile uint32_t active_since;    /* frame it was started in */
static uint8_t next_rr;                   /* round-robin start */
static volatile uint32_t frame;           /* counts SOF interrupts */
static volatile bool irq_on;
static volatile bool port_up;
static uint8_t root_speed = CH32RV_USBH_FULL;

static void schedule(void);

/* Take the transaction off the wire. The engine finishes a token it has
 * started, so wait (bounded: a full-speed transaction is well under 100 us)
 * for its end before the next start() clears the flag - a late UIF_TRANSFER
 * would otherwise be read as the next transaction's. */
static void stop_active(void)
{
    if (active < 0) {
        return;
    }
    HOST_EP_PID = 0;
    for (volatile uint32_t d = 0; d < 20000u && !(INT_FG & UIF_TRANSFER); d++) {
    }
    INT_EN &= (uint8_t)~UIE_TRANSFER;
    active = -1;
}

static inline void lock(bool *was)
{
    *was = irq_on;
    if (*was) {
        ch32rv_irq_disable(CH32RV_IRQN_USBFS);
    }
}

static inline void unlock(bool was)
{
    if (was) {
        ch32rv_irq_enable(CH32RV_IRQN_USBFS);
    }
}

static hcd_ep_t *find(uint8_t daddr, uint8_t ep_addr)
{
    for (unsigned i = 0; i < ARRAY_SIZE(eps); i++) {
        if (eps[i].state != EP_FREE && eps[i].daddr == daddr && eps[i].ep_addr == ep_addr) {
            return &eps[i];
        }
    }
    return NULL;
}

static hcd_ep_t *open_ep(uint8_t daddr, uint8_t ep_addr, uint16_t mps, uint8_t type, uint8_t interval,
                         uint8_t speed)
{
    hcd_ep_t *ep = find(daddr, ep_addr);
    if (!ep) {
        for (unsigned i = 0; i < ARRAY_SIZE(eps); i++) {
            if (eps[i].state == EP_FREE) {
                ep = &eps[i];
                break;
            }
        }
    }
    if (!ep) {
        return NULL;
    }
    if (ep - eps == active) {
        stop_active();      /* re-opened under a transaction: that one is over */
    }
    memset(ep, 0, sizeof(*ep));
    ep->state = EP_IDLE;
    ep->speed = speed;
    ep->daddr = daddr;
    ep->ep_addr = ep_addr;
    ep->mps = mps ? mps : 8;
    ep->type = type;
    ep->interval = interval ? interval : 1;
    return ep;
}

/* The transfer on `ep` is over: hand it to the stack. */
static void finish(hcd_ep_t *ep, uint8_t result)
{
    ep->state = EP_IDLE;
    const uint8_t address = ep->setup ? 0x00 : ep->ep_addr;
    const uint16_t length = ep->setup ? 8u : ep->done;
    ep->setup = 0;
    ch32rv_usbh_isr_xfer(ep->daddr, address, result, length);
}

/* Address and speed for a transaction to `daddr`. A low-speed device on the
 * root port runs the port at low speed; behind a full-speed hub it needs the
 * PRE packet in front of its tokens instead. */
static void select_device(const hcd_ep_t *ep)
{
    DEV_ADDR = (uint8_t)((DEV_ADDR & 0x80u) | (ep->daddr & 0x7Fu));
    const uint8_t speed = ep->speed;
    if (speed == CH32RV_USBH_LOW && root_speed == CH32RV_USBH_LOW) {
        BASE_CTRL |= UC_LOW_SPEED;
        HOST_CTRL |= UH_LOW_SPEED;
        HOST_SETUP &= ~UH_PRE_PID_EN;
    } else if (speed == CH32RV_USBH_LOW) {
        BASE_CTRL |= UC_LOW_SPEED;
        HOST_CTRL &= (uint8_t)~UH_LOW_SPEED;
        HOST_SETUP |= UH_PRE_PID_EN;
    } else {
        BASE_CTRL &= (uint8_t)~UC_LOW_SPEED;
        HOST_CTRL &= (uint8_t)~UH_LOW_SPEED;
        HOST_SETUP &= ~UH_PRE_PID_EN;
    }
}

/* Put one transaction of `ep` on the wire. */
static void start(int16_t index)
{
    hcd_ep_t *ep = &eps[index];
    active = index;
    active_since = frame;
    select_device(ep);
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;
    uint8_t pid;
    if (ep->setup) {
        pid = PID_SETUP;
        memcpy(tx_buf, ep->setup_pkt, 8);         /* tx_buf is shared with every OUT */
        HOST_TX_LEN = 8;
        HOST_TX_CTRL = 0;                         /* SETUP is always DATA0 */
    } else if (ep->ep_addr & 0x80u) {
        pid = PID_IN;
        HOST_RX_CTRL = (uint8_t)((ep->toggle ? UH_R_TOG : 0) | (iso ? UH_R_RES : 0));
    } else {
        pid = PID_OUT;
        const uint16_t left = (uint16_t)(ep->len - ep->done);
        const uint16_t n = left < ep->mps ? left : ep->mps;
        if (n) {
            memcpy(tx_buf, ep->buf + ep->done, n);
        }
        HOST_TX_LEN = n;
        HOST_TX_CTRL = (uint8_t)((ep->toggle ? UH_T_TOG : 0) | (iso ? UH_T_RES : 0));
    }
    HOST_EP_PID = (uint8_t)((pid << 4) | (ep->ep_addr & 0x0Fu));
    INT_EN |= UIE_TRANSFER;
    INT_FG = UIF_TRANSFER;                        /* clearing the flag starts it */
}

/* Start the next pending transaction that is due, if the wire is free. */
static void schedule(void)
{
    if (active >= 0 || !port_up) {
        return;
    }
    const unsigned n = ARRAY_SIZE(eps);
    for (unsigned k = 0; k < n; k++) {
        const unsigned i = (next_rr + k) % n;
        hcd_ep_t *ep = &eps[i];
        if (ep->state == EP_PENDING && (int32_t)(frame - ep->due) >= 0) {
            next_rr = (uint8_t)((i + 1) % n);
            start((int16_t)i);
            return;
        }
    }
}

/* The transaction on the wire has ended. */
static void transfer_done(void)
{
    INT_EN &= (uint8_t)~UIE_TRANSFER;
    HOST_EP_PID = 0;
    const uint8_t status = INT_ST;
    const int16_t index = active;
    active = -1;
    if (index < 0) {
        return;
    }
    hcd_ep_t *ep = &eps[index];
    if (ep->state != EP_PENDING) {
        return;                                    /* aborted while on the wire */
    }
    const uint8_t res = status & UIS_H_RES_MASK;
    const bool in = !ep->setup && (ep->ep_addr & 0x80u);
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;

    if ((status & UIS_TOG_OK) || (iso && (!in || res == PID_DATA0 || res == PID_DATA1))) {
        ep->tries = 0;
        ep->toggle ^= 1u;
        if (ep->setup) {
            /* The data and status stages start at DATA1 both ways. */
            hcd_ep_t *ep_in = find(ep->daddr, 0x80);
            ep->toggle = 1;
            if (ep_in) {
                ep_in->toggle = 1;
            }
            finish(ep, CH32RV_USBH_OK);
        } else if (in) {
            const uint16_t got = RX_LEN;
            const uint16_t room = (uint16_t)(ep->len - ep->done);
            const uint16_t keep = got < room ? got : room;
            if (keep && ep->buf) {
                memcpy(ep->buf + ep->done, rx_buf, keep);
            }
            ep->done = (uint16_t)(ep->done + keep);
            if (iso || got < ep->mps || ep->done >= ep->len) {
                finish(ep, got > room ? CH32RV_USBH_FAILED : CH32RV_USBH_OK);
            }
        } else {
            const uint16_t sent = HOST_TX_LEN;
            ep->done = (uint16_t)(ep->done + sent);
            if (iso || ep->done >= ep->len) {
                finish(ep, CH32RV_USBH_OK);
            }
        }
    } else if (res == PID_NAK) {
        ep->tries = 0;
        ep->due = frame + (ep->type == CH32RV_USBH_INTERRUPT ? ep->interval : 1u);
        if (ep->type == CH32RV_USBH_CONTROL && (uint32_t)(frame - ep->since) > CONTROL_TIMEOUT_FRAMES) {
            finish(ep, CH32RV_USBH_TIMEOUT);
        }
    } else if (res == PID_STALL) {
        ep->toggle = 0;
        finish(ep, CH32RV_USBH_STALLED);
    } else if (in && (res == PID_DATA0 || res == PID_DATA1)) {
        /* A repeat of the last packet (wrong toggle): drop it, ask again. */
        ep->tries = 0;
    } else if (res == 0 && ++ep->tries < NO_RESPONSE_TRIES) {
        ep->due = frame + 1u;                      /* no handshake: try again */
    } else {
        finish(ep, CH32RV_USBH_FAILED);
    }
    schedule();
}

/* ------------------------------------------------------------- the table */
static void isr(void);

static bool hcd_open(void)
{
    if (ch32rv_usbfs_irq_hook && ch32rv_usbfs_irq_hook != isr) {
        return false;                                 /* the device stack has the block */
    }
    memset(eps, 0, sizeof(eps));
    active = -1;
    port_up = false;
    frame = 0;
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    for (volatile int d = 0; d < 2000; d++) {
    }
    BASE_CTRL = UC_HOST_MODE;
    HOST_CTRL = 0;                                 /* D+/D- pull-downs on */
    DEV_ADDR = 0;
    HOST_EP_MOD = UH_EP_TX_EN | UH_EP_RX_EN;
    SET_RX_DMA(rx_buf);
    SET_TX_DMA(tx_buf);
    HOST_RX_CTRL = 0;
    HOST_TX_CTRL = 0;
    BASE_CTRL = UC_HOST_MODE | UC_INT_BUSY | UC_DMA_EN;
    HOST_SETUP = 0;
    INT_FG = 0xFF;
    INT_EN = UIE_DETECT | UIE_HST_SOF;
    ch32rv_usbfs_irq_hook = isr;
    irq_on = true;
    ch32rv_irq_enable(CH32RV_IRQN_USBFS);
    return true;
}

static void hcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBFS);
    irq_on = false;
    INT_EN = 0;
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    for (volatile int d = 0; d < 2000; d++) {
    }
    BASE_CTRL = 0;
    memset(eps, 0, sizeof(eps));
    active = -1;
    port_up = false;
    if (ch32rv_usbfs_irq_hook == isr) {
        ch32rv_usbfs_irq_hook = nullptr;
    }
}

static bool hcd_connected(void)
{
    return (MIS_ST & UMS_DEV_ATTACH) != 0;
}

static void hcd_port_reset(void)
{
    bool was;
    lock(&was);
    port_up = false;
    stop_active();
    /* SE0 during the reset can raise DETECT; it is not an attach or a detach. */
    INT_EN &= (uint8_t)~UIE_DETECT;
    DEV_ADDR = 0;
    BASE_CTRL &= (uint8_t)~UC_LOW_SPEED;
    HOST_SETUP &= ~(UH_PRE_PID_EN | UH_SOF_EN);
    HOST_CTRL = (uint8_t)((HOST_CTRL & ~(UH_LOW_SPEED | UH_PORT_EN)) | UH_BUS_RESET);
    unlock(was);
}

static uint8_t hcd_port_reset_end(void)
{
    bool was;
    lock(&was);
    HOST_CTRL &= (uint8_t)~UH_BUS_RESET;
    for (volatile int d = 0; d < 4000; d++) {    /* the EVT waits ~2 ms; the line settles in µs */
    }
    root_speed = (MIS_ST & UMS_DM_LEVEL) ? CH32RV_USBH_LOW : CH32RV_USBH_FULL;
    if (root_speed == CH32RV_USBH_LOW) {
        BASE_CTRL |= UC_LOW_SPEED;
        HOST_CTRL |= UH_LOW_SPEED;
    }
    HOST_CTRL |= UH_PORT_EN;
    HOST_SETUP |= UH_SOF_EN;
    INT_FG = UIF_DETECT;                           /* the reset itself is not an attach */
    INT_EN |= UIE_DETECT;
    port_up = true;
    schedule();
    unlock(was);
    return root_speed;
}

/* Back to a disabled port: what a detach leaves, and how the stack makes a
 * stuck device enumerate again (a reset follows). */
static void hcd_port_disable(void)
{
    bool was;
    lock(&was);
    stop_active();
    port_up = false;
    HOST_CTRL &= (uint8_t)~UH_PORT_EN;
    HOST_SETUP &= ~UH_SOF_EN;
    unlock(was);
}

static void hcd_device_close(uint8_t daddr)
{
    bool was;
    lock(&was);
    for (unsigned i = 0; i < ARRAY_SIZE(eps); i++) {
        if (eps[i].state != EP_FREE && eps[i].daddr == daddr) {
            if ((int16_t)i == active) {
                stop_active();
            }
            eps[i].state = EP_FREE;
        }
    }
    schedule();
    unlock(was);
}

static bool hcd_ep_open(uint8_t daddr, uint8_t address, uint8_t type, uint16_t mps, uint8_t interval, uint8_t speed)
{
    if (mps > PACKET_MAX) {
        return false;
    }
    bool was;
    lock(&was);
    bool ok;
    if ((address & 0x0Fu) == 0) {
        ok = open_ep(daddr, 0x00, mps, CH32RV_USBH_CONTROL, 0, speed) &&
             open_ep(daddr, 0x80, mps, CH32RV_USBH_CONTROL, 0, speed);
    } else {
        if (type == CH32RV_USBH_ISOCHRONOUS) {
            interval = (uint8_t)(1u << ((interval > 5u ? 5u : interval) - (interval ? 1u : 0u)));
        }
        ok = open_ep(daddr, address, mps, type, interval, speed) != NULL;
    }
    unlock(was);
    return ok;
}

static void hcd_ep_close(uint8_t daddr, uint8_t address)
{
    bool was;
    lock(&was);
    hcd_ep_t *ep = find(daddr, address);
    if (ep) {
        if ((int16_t)(ep - eps) == active) {
            stop_active();
        }
        ep->state = EP_FREE;
        schedule();
    }
    unlock(was);
}

static bool hcd_xfer(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length)
{
    bool was;
    lock(&was);
    hcd_ep_t *ep = find(daddr, address);
    const bool ok = ep && ep->state == EP_IDLE;
    if (ok) {
        ep->buf = buffer;
        ep->len = length;
        ep->done = 0;
        ep->tries = 0;
        ep->setup = 0;
        ep->due = frame;
        ep->since = frame;
        ep->state = EP_PENDING;
        schedule();
    }
    unlock(was);
    return ok;
}

static bool hcd_setup(uint8_t daddr, const uint8_t packet[8])
{
    bool was;
    lock(&was);
    hcd_ep_t *ep = find(daddr, 0x00);
    bool ok = ep != NULL;
    if (ok) {
        hcd_ep_t *ep_in = find(daddr, 0x80);
        /* A new SETUP replaces whatever control stage was on the wire. */
        if ((int16_t)(ep - eps) == active || (ep_in && (int16_t)(ep_in - eps) == active)) {
            stop_active();
        }
        if (ep_in) {
            ep_in->state = EP_IDLE;
        }
        memcpy(ep->setup_pkt, packet, 8);   /* tx_buf may be another OUT's right now */
        ep->buf = NULL;
        ep->len = 8;
        ep->done = 0;
        ep->tries = 0;
        ep->toggle = 0;
        ep->setup = 1;
        ep->due = frame;
        ep->since = frame;
        ep->state = EP_PENDING;
        schedule();
    }
    unlock(was);
    return ok;
}

static void hcd_abort(uint8_t daddr, uint8_t address)
{
    bool was;
    lock(&was);
    /* A control transfer goes as a whole: its data stage on the other
     * direction too, or it would keep polling and complete into the next
     * request. */
    hcd_ep_t *list[2] = {find(daddr, address), (address & 0x0Fu) == 0 ? find(daddr, (uint8_t)(address ^ 0x80u)) : NULL};
    for (unsigned k = 0; k < 2; k++) {
        hcd_ep_t *ep = list[k];
        if (!ep) {
            continue;
        }
        if ((int16_t)(ep - eps) == active) {
            stop_active();
        }
        ep->state = EP_IDLE;
        ep->setup = 0;
    }
    schedule();
    unlock(was);
}

/* The stack has sent CLEAR_FEATURE(ENDPOINT_HALT); the host's half is the
 * data toggle, which starts over at DATA0. */
static void hcd_reset_toggle(uint8_t daddr, uint8_t address)
{
    hcd_ep_t *ep = find(daddr, address);
    if (ep) {
        ep->toggle = 0;
    }
}

static uint32_t hcd_frame(void)
{
    return frame;
}

static void isr(void)
{
    const uint8_t flags = INT_FG;

    /* Only while DETECT is enabled: during a port reset the SE0 raises the flag
     * and DEV_ATTACH reads 0, and another interrupt (SOF) would otherwise take
     * that for a detach and end the enumeration. hcd_port_reset_end() clears
     * the flag before enabling it again. */
    if ((flags & UIF_DETECT) && (INT_EN & UIE_DETECT)) {
        INT_FG = UIF_DETECT;
        if (MIS_ST & UMS_DEV_ATTACH) {
            ch32rv_usbh_isr_port(true);
        } else {
            port_up = false;
            if (active >= 0) {
                INT_EN &= (uint8_t)~UIE_TRANSFER;
                HOST_EP_PID = 0;
                active = -1;
            }
            HOST_CTRL &= (uint8_t)~UH_PORT_EN;
            HOST_SETUP &= ~UH_SOF_EN;
            ch32rv_usbh_isr_port(false);
        }
    }
    if ((flags & UIF_TRANSFER) && (INT_EN & UIE_TRANSFER)) {
        transfer_done();
    }
    if (flags & UIF_HST_SOF) {
        INT_FG = UIF_HST_SOF;
        frame++;
        /* A transaction never takes a frame; one still open two frames on
         * lost its completion. Count it as no handshake and move on. */
        if (active >= 0 && (uint32_t)(frame - active_since) >= 2u) {
            hcd_ep_t *ep = &eps[active];
            stop_active();
            if (ep->state == EP_PENDING) {
                if (++ep->tries < NO_RESPONSE_TRIES) {
                    ep->due = frame + 1u;
                } else {
                    finish(ep, CH32RV_USBH_FAILED);
                }
            }
        }
        schedule();
    }
}

const CH32RVUsbHcd ch32rv_usb_hcd_usbfs = {
    "USBFS host", PACKET_MAX,
    hcd_open, hcd_close, hcd_connected, hcd_port_reset, hcd_port_reset_end, hcd_port_disable,
    hcd_ep_open, hcd_ep_close, hcd_device_close, hcd_xfer, hcd_setup, hcd_abort, hcd_reset_toggle, hcd_frame,
};

#endif
