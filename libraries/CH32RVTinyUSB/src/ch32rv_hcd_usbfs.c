/* TinyUSB host controller driver for the WCH USBFS block in host mode
 * (CH32X035/X033 today; the same block is the OTG_FS of V20x/V30x).
 *
 * Ours, not vendored. Upstream has portable/wch/hcd_ch32_usbfs.c, written for
 * V20x against the WCH SDK, which busy-waits for the controller, panics on
 * surprises, retries a STALL by itself and runs one transfer at a time. This
 * one keeps that file's register knowledge (and the EVT's, HOST_KM's
 * USBFSH_Transact) and is shaped like TinyUSB expects a host controller:
 *
 *   - every open endpoint can have a transfer pending at once (HID interrupt
 *     IN, CDC bulk IN and a control transfer all waiting is ordinary). The
 *     hardware has a single transaction engine, so a scheduler runs them one
 *     transaction at a time, round robin;
 *   - a NAK puts the endpoint back to wait: bulk and control retry at the next
 *     frame, interrupt endpoints at their next polling interval. The SOF
 *     interrupt (every 1 ms) is the clock;
 *   - STALL, a missing handshake (three tries) and anything unexpected end the
 *     transfer with the result TinyUSB's class drivers act on;
 *   - IN data with the wrong toggle (the device repeating a packet whose ACK it
 *     lost) is dropped and asked for again, as the EVT does;
 *   - abort, close and detach clean up instead of leaving the engine busy.
 *
 * Packets go through one 64-byte DMA buffer each way: full speed caps bulk,
 * interrupt and control at 64 bytes. Isochronous endpoints get one packet per
 * frame without a handshake.
 */
#include "tusb_option.h"

#if CFG_TUH_ENABLED && defined(CH32RV_HCD_USBFS)

#include "host/hcd.h"
#include "host/usbh.h"

#include "ch32rv_registers.h"

/* ------------------------------------------------------------- registers */
#ifndef CH32RV_HCD_USBFS_BASE
#define CH32RV_HCD_USBFS_BASE 0x40023400u   /* CH32X035 USBFS */
#endif
#ifndef CH32RV_HCD_USBFS_IRQ
#error "CH32RV_HCD_USBFS_IRQ: the controller's interrupt number (tusb_config.h)"
#endif

#define R8(off)  (*(volatile uint8_t *)(CH32RV_HCD_USBFS_BASE + (off)))
#define R16(off) (*(volatile uint16_t *)(CH32RV_HCD_USBFS_BASE + (off)))
#define R32(off) (*(volatile uint32_t *)(CH32RV_HCD_USBFS_BASE + (off)))

/* The host registers overlay the device ones, and the two layouts of the
 * block place them differently (EVT USBFSH_TypeDef):
 *   1  CH32X035 / X033: endpoint registers 4 bytes apart from 0x20, DMA
 *      addresses 16 bit (SRAM's low half), the toggles in bits 7 / 6;
 *   2  CH32V20x / V30x OTG_FS: endpoint registers from 0x30, DMA addresses
 *      32 bit, HOST_SETUP 16 bit (SOF_EN bit 2, PRE_PID_EN bit 10), both
 *      toggles in bit 2. */
#ifndef CH32RV_HCD_USBFS_LAYOUT
#define CH32RV_HCD_USBFS_LAYOUT 1
#endif

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

TU_ATTR_ALIGNED(4) static uint8_t rx_buf[PACKET_MAX];
TU_ATTR_ALIGNED(4) static uint8_t tx_buf[PACKET_MAX];

/* ------------------------------------------------------------- state */
enum { EP_FREE = 0, EP_IDLE, EP_PENDING };

typedef struct {
    uint8_t state;
    uint8_t daddr;
    uint8_t ep_addr;
    uint8_t type;          /* TUSB_XFER_* */
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

#ifndef CH32RV_HCD_EP_MAX
#define CH32RV_HCD_EP_MAX (CFG_TUH_DEVICE_MAX * 8 + 2 * (CFG_TUH_DEVICE_MAX + 1))
#endif

static hcd_ep_t eps[CH32RV_HCD_EP_MAX];
static volatile int16_t active = -1;      /* index of the transaction on the wire */
static volatile uint32_t active_since;    /* frame it was started in */
static uint8_t next_rr;                   /* round-robin start */
static volatile uint32_t frame;           /* counts SOF interrupts */
static volatile bool irq_on;
static volatile bool port_up;
static tusb_speed_t root_speed = TUSB_SPEED_FULL;

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
        hcd_int_disable(0);
    }
}

static inline void unlock(bool was)
{
    if (was) {
        hcd_int_enable(0);
    }
}

static hcd_ep_t *find(uint8_t daddr, uint8_t ep_addr)
{
    for (unsigned i = 0; i < TU_ARRAY_SIZE(eps); i++) {
        if (eps[i].state != EP_FREE && eps[i].daddr == daddr && eps[i].ep_addr == ep_addr) {
            return &eps[i];
        }
    }
    return NULL;
}

static hcd_ep_t *open_ep(uint8_t daddr, uint8_t ep_addr, uint16_t mps, uint8_t type, uint8_t interval)
{
    hcd_ep_t *ep = find(daddr, ep_addr);
    if (!ep) {
        for (unsigned i = 0; i < TU_ARRAY_SIZE(eps); i++) {
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
    tu_memclr(ep, sizeof(*ep));
    ep->state = EP_IDLE;
    ep->daddr = daddr;
    ep->ep_addr = ep_addr;
    ep->mps = mps ? mps : 8;
    ep->type = type;
    ep->interval = interval ? interval : 1;
    return ep;
}

/* The transfer on `ep` is over: hand it to TinyUSB. */
static void finish(hcd_ep_t *ep, xfer_result_t result, bool in_isr)
{
    ep->state = EP_IDLE;
    hcd_event_xfer_complete(ep->daddr, ep->setup ? 0x00 : ep->ep_addr,
                            ep->setup ? 8u : ep->done, result, in_isr);
    ep->setup = 0;
}

/* Address and speed for a transaction to `daddr`. A low-speed device on the
 * root port runs the port at low speed; behind a full-speed hub it needs the
 * PRE packet in front of its tokens instead. */
static void select_device(uint8_t daddr)
{
    DEV_ADDR = (uint8_t)((DEV_ADDR & 0x80u) | (daddr & 0x7Fu));
    /* tuh_speed_get(0) is the device being enumerated - behind a hub too. */
    const tusb_speed_t speed = tuh_speed_get(daddr);
    if (speed == TUSB_SPEED_LOW && root_speed == TUSB_SPEED_LOW) {
        BASE_CTRL |= UC_LOW_SPEED;
        HOST_CTRL |= UH_LOW_SPEED;
        HOST_SETUP &= ~UH_PRE_PID_EN;
    } else if (speed == TUSB_SPEED_LOW) {
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
    select_device(ep->daddr);
    const bool iso = ep->type == TUSB_XFER_ISOCHRONOUS;
    uint8_t pid;
    if (ep->setup) {
        pid = PID_SETUP;
        memcpy(tx_buf, ep->setup_pkt, 8);         /* tx_buf is shared with every OUT */
        HOST_TX_LEN = 8;
        HOST_TX_CTRL = 0;                         /* SETUP is always DATA0 */
    } else if (tu_edpt_dir(ep->ep_addr) == TUSB_DIR_IN) {
        pid = PID_IN;
        HOST_RX_CTRL = (uint8_t)((ep->toggle ? UH_R_TOG : 0) | (iso ? UH_R_RES : 0));
    } else {
        pid = PID_OUT;
        const uint16_t n = (uint16_t)TU_MIN((uint16_t)(ep->len - ep->done), ep->mps);
        if (n) {
            memcpy(tx_buf, ep->buf + ep->done, n);
        }
        HOST_TX_LEN = n;
        HOST_TX_CTRL = (uint8_t)((ep->toggle ? UH_T_TOG : 0) | (iso ? UH_T_RES : 0));
    }
    HOST_EP_PID = (uint8_t)((pid << 4) | tu_edpt_number(ep->ep_addr));
    INT_EN |= UIE_TRANSFER;
    INT_FG = UIF_TRANSFER;                        /* clearing the flag starts it */
}

/* Start the next pending transaction that is due, if the wire is free. */
static void schedule(void)
{
    if (active >= 0 || !port_up) {
        return;
    }
    const unsigned n = TU_ARRAY_SIZE(eps);
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
static void transfer_done(bool in_isr)
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
    const bool in = !ep->setup && tu_edpt_dir(ep->ep_addr) == TUSB_DIR_IN;
    const bool iso = ep->type == TUSB_XFER_ISOCHRONOUS;

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
            finish(ep, XFER_RESULT_SUCCESS, in_isr);
        } else if (in) {
            const uint16_t got = RX_LEN;
            const uint16_t room = (uint16_t)(ep->len - ep->done);
            const uint16_t keep = TU_MIN(got, room);
            if (keep && ep->buf) {
                memcpy(ep->buf + ep->done, rx_buf, keep);
            }
            ep->done = (uint16_t)(ep->done + keep);
            if (iso || got < ep->mps || ep->done >= ep->len) {
                finish(ep, got > room ? XFER_RESULT_FAILED : XFER_RESULT_SUCCESS, in_isr);
            }
        } else {
            const uint16_t sent = HOST_TX_LEN;
            ep->done = (uint16_t)(ep->done + sent);
            if (iso || ep->done >= ep->len) {
                finish(ep, XFER_RESULT_SUCCESS, in_isr);
            }
        }
    } else if (res == PID_NAK) {
        ep->tries = 0;
        ep->due = frame + (ep->type == TUSB_XFER_INTERRUPT ? ep->interval : 1u);
        if (ep->type == TUSB_XFER_CONTROL && (uint32_t)(frame - ep->since) > CONTROL_TIMEOUT_FRAMES) {
            finish(ep, XFER_RESULT_TIMEOUT, in_isr);
        }
    } else if (res == PID_STALL) {
        ep->toggle = 0;
        finish(ep, XFER_RESULT_STALLED, in_isr);
    } else if (in && (res == PID_DATA0 || res == PID_DATA1)) {
        /* A repeat of the last packet (wrong toggle): drop it, ask again. */
        ep->tries = 0;
    } else if (res == 0 && ++ep->tries < NO_RESPONSE_TRIES) {
        ep->due = frame + 1u;                      /* no handshake: try again */
    } else {
        finish(ep, XFER_RESULT_FAILED, in_isr);
    }
    schedule();
}

/* ------------------------------------------------------------- HCD API */
bool hcd_init(uint8_t rhport, const tusb_rhport_init_t *rh_init)
{
    (void)rh_init;
    tu_memclr(eps, sizeof(eps));
    active = -1;
    port_up = false;
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
    if (MIS_ST & UMS_DEV_ATTACH) {
        hcd_event_device_attach(rhport, false);
    }
    return true;
}

bool hcd_deinit(uint8_t rhport)
{
    (void)rhport;
    hcd_int_disable(0);
    INT_EN = 0;
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    for (volatile int d = 0; d < 2000; d++) {
    }
    BASE_CTRL = 0;
    tu_memclr(eps, sizeof(eps));
    active = -1;
    port_up = false;
    return true;
}

void hcd_int_enable(uint8_t rhport)
{
    (void)rhport;
    irq_on = true;
    ch32rv_irq_enable(CH32RV_HCD_USBFS_IRQ);
}

void hcd_int_disable(uint8_t rhport)
{
    (void)rhport;
    ch32rv_irq_disable(CH32RV_HCD_USBFS_IRQ);
    irq_on = false;
}

uint32_t hcd_frame_number(uint8_t rhport)
{
    (void)rhport;
    return frame;
}

bool hcd_port_connect_status(uint8_t rhport)
{
    (void)rhport;
    return (MIS_ST & UMS_DEV_ATTACH) != 0;
}

void hcd_port_reset(uint8_t rhport)
{
    (void)rhport;
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

void hcd_port_reset_end(uint8_t rhport)
{
    (void)rhport;
    bool was;
    lock(&was);
    HOST_CTRL &= (uint8_t)~UH_BUS_RESET;
    for (volatile int d = 0; d < 4000; d++) {    /* the EVT waits ~2 ms; the line settles in µs */
    }
    root_speed = (MIS_ST & UMS_DM_LEVEL) ? TUSB_SPEED_LOW : TUSB_SPEED_FULL;
    if (root_speed == TUSB_SPEED_LOW) {
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
}

tusb_speed_t hcd_port_speed_get(uint8_t rhport)
{
    (void)rhport;
    if (!(HOST_CTRL & UH_PORT_EN)) {
        return (MIS_ST & UMS_DM_LEVEL) ? TUSB_SPEED_LOW : TUSB_SPEED_FULL;
    }
    return root_speed;
}

void hcd_device_close(uint8_t rhport, uint8_t daddr)
{
    (void)rhport;
    bool was;
    lock(&was);
    for (unsigned i = 0; i < TU_ARRAY_SIZE(eps); i++) {
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

bool hcd_edpt_open(uint8_t rhport, uint8_t daddr, tusb_desc_endpoint_t const *desc)
{
    (void)rhport;
    const uint16_t mps = tu_edpt_packet_size(desc);
    const uint8_t type = desc->bmAttributes.xfer;
    TU_VERIFY(mps <= PACKET_MAX);
    bool was;
    lock(&was);
    bool ok;
    if (tu_edpt_number(desc->bEndpointAddress) == 0) {
        ok = open_ep(daddr, 0x00, mps, TUSB_XFER_CONTROL, 0) && open_ep(daddr, 0x80, mps, TUSB_XFER_CONTROL, 0);
    } else {
        uint8_t interval = desc->bInterval;
        if (type == TUSB_XFER_ISOCHRONOUS) {
            interval = (uint8_t)(1u << (TU_MIN(desc->bInterval, 5u) - (desc->bInterval ? 1u : 0u)));
        }
        ok = open_ep(daddr, desc->bEndpointAddress, mps, type, interval) != NULL;
    }
    unlock(was);
    return ok;
}

bool hcd_edpt_close(uint8_t rhport, uint8_t daddr, uint8_t ep_addr)
{
    (void)rhport;
    bool was;
    lock(&was);
    hcd_ep_t *ep = find(daddr, ep_addr);
    if (ep) {
        if ((int16_t)(ep - eps) == active) {
            stop_active();
        }
        ep->state = EP_FREE;
        schedule();
    }
    unlock(was);
    return ep != NULL;
}

bool hcd_edpt_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr, uint8_t *buffer, uint16_t buflen)
{
    (void)rhport;
    bool was;
    lock(&was);
    hcd_ep_t *ep = find(daddr, ep_addr);
    const bool ok = ep && ep->state == EP_IDLE;
    if (ok) {
        ep->buf = buffer;
        ep->len = buflen;
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

bool hcd_setup_send(uint8_t rhport, uint8_t daddr, uint8_t const setup_packet[8])
{
    (void)rhport;
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
        memcpy(ep->setup_pkt, setup_packet, 8);   /* tx_buf may be another OUT's right now */
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

bool hcd_edpt_abort_xfer(uint8_t rhport, uint8_t daddr, uint8_t ep_addr)
{
    (void)rhport;
    bool was;
    lock(&was);
    /* usbh aborts a control transfer as EP 0 alone: its data stage on 0x80
     * goes too, or it would keep polling and complete into the next request. */
    hcd_ep_t *list[2] = {find(daddr, ep_addr),
                         tu_edpt_number(ep_addr) == 0 ? find(daddr, (uint8_t)(ep_addr ^ 0x80u)) : NULL};
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
    return list[0] != NULL;
}

/* usbh has sent CLEAR_FEATURE(ENDPOINT_HALT) itself; the host's half is the
 * data toggle, which starts over at DATA0. */
bool hcd_edpt_clear_stall(uint8_t rhport, uint8_t daddr, uint8_t ep_addr)
{
    (void)rhport;
    hcd_ep_t *ep = find(daddr, ep_addr);
    if (ep) {
        ep->toggle = 0;
    }
    return true;
}

/* Make the root port's device go through enumeration again, as if it had
 * been unplugged and plugged in: the way back for a class driver that is stuck
 * (TinyUSB's MSC host cannot abandon a command). Task context. */
void ch32rv_hcd_usbfs_reenumerate(uint8_t rhport)
{
    bool was;
    lock(&was);
    stop_active();
    port_up = false;
    HOST_CTRL &= (uint8_t)~UH_PORT_EN;
    HOST_SETUP &= ~UH_SOF_EN;
    unlock(was);
    hcd_event_device_remove(rhport, false);
    if (MIS_ST & UMS_DEV_ATTACH) {
        hcd_event_device_attach(rhport, false);
    }
}

void hcd_int_handler(uint8_t rhport, bool in_isr)
{
    const uint8_t flags = INT_FG;

    if (flags & UIF_DETECT) {
        INT_FG = UIF_DETECT;
        if (MIS_ST & UMS_DEV_ATTACH) {
            hcd_event_device_attach(rhport, in_isr);
        } else {
            port_up = false;
            if (active >= 0) {
                INT_EN &= (uint8_t)~UIE_TRANSFER;
                HOST_EP_PID = 0;
                active = -1;
            }
            HOST_CTRL &= (uint8_t)~UH_PORT_EN;
            HOST_SETUP &= ~UH_SOF_EN;
            hcd_event_device_remove(rhport, in_isr);
        }
    }
    if ((flags & UIF_TRANSFER) && (INT_EN & UIE_TRANSFER)) {
        transfer_done(in_isr);
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
                    finish(ep, XFER_RESULT_FAILED, in_isr);
                }
            }
        }
        schedule();
    }
}

#endif
