/* Host controller driver for the first-generation USBHS (USB 2.0 high speed)
 * of CH32V305 / V307 / V317 in host mode. Its clock and interrupt are the
 * core's (internal/ch32rv_usbhs1.h, shared with the device driver).
 *
 * Map (EVT ch32v30x.h USBHSH_TypeDef; RM CH32FV2x_V3x 22.2.3): CONTROL 0x00
 * (HOST_MODE 7, speed 6:5 - 00 full, 01 high, 10 low - INT_BUSY 3, RESET_SIE
 * 2, CLR_ALL 1, DMA_EN 0), HOST_CTRL 0x01 (SOF_EN 7, PHY_SUSPENDM 4,
 * TX_BUS_RESET 0), INT_EN 0x02, DEV_AD 0x03, FRAME_NO 0x04 (frame 10:0,
 * microframe 15:13), SPEED_TYPE 0x08 (1:0: 00 full, 01 high, 10 low), MIS_ST
 * 0x09 (DEV_ATTACH 1), INT_FG 0x0A (HST_SOF 3, TRANSFER 1, DETECT 0), INT_ST
 * 0x0B (TOG_OK 6, the PID that came back 3:0), RX_LEN 0x0C, EP_CONFIG 0x10
 * (TX_EN 3, RX_EN 18), EP_TYPE 0x14 (isochronous: TX 3, RX 18), RX_DMA 0x24,
 * TX_DMA 0x64, RX_MAX_LEN 0xA0, EP_PID 0xE0, RX_CTRL 0xE3 and TX_CTRL 0xE6
 * (toggle 4:3, no handshake 2), TX_LEN 0xE4.
 *
 * The block has one transaction engine, as USBFS: EP_PID takes the token and
 * the endpoint, clearing INT_FG.TRANSFER lets it run (INT_BUSY holds the next
 * one while the flag is up), INT_ST says what came back. So this is
 * CH32RVUsbHcdUsbfs.cpp's scheduler with high-speed packets (up to 1024 bytes,
 * one DMA buffer each way; RX_DMA and TX_DMA keep 17 address bits, so the
 * buffers sit in the first 128 KB of SRAM - open() checks):
 *
 *   - every open endpoint can have a transfer pending; one transaction at a
 *     time goes on the wire, round robin;
 *   - the clock is FRAME_NO, read at every SOF interrupt: microframes (125 us)
 *     when the root port runs at high speed, frames (1 ms) otherwise. A NAK
 *     puts bulk and control back for the next tick, interrupt endpoints for
 *     their polling interval;
 *   - an IN with the wrong data PID is the device repeating a packet whose ACK
 *     it lost: dropped and asked for again (the EVT's USBHSH_Transact);
 *   - NYET after an OUT is an accepted packet; the next goes as OUT again
 *     rather than PING.
 *
 * Root port: TX_BUS_RESET is held for the reset (the stack's 20 ms; the PHY
 * runs the high-speed chirp in it), SPEED_TYPE has the speed after it. Not
 * here: split transactions (a full- or low-speed device behind a high-speed
 * hub; this IP's SPLIT_DATA register is 12 bits, its fields undocumented) -
 * epOpen refuses such a device - and the PRE packet for a low-speed device
 * behind a full-speed hub (no such bit; the speed field is set to low and
 * the rest left to the PHY). Build only. */
#include "internal/ch32rv_usbhs1.h"

#ifdef CH32RV_USBHS1_BASE

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "internal/CH32RVUsbHcd.h"

#define R8(o)  (*(volatile uint8_t *)(CH32RV_USBHS1_BASE + (o)))
#define R16(o) (*(volatile uint16_t *)(CH32RV_USBHS1_BASE + (o)))
#define R32(o) (*(volatile uint32_t *)(CH32RV_USBHS1_BASE + (o)))

#define CONTROL     R8(0x00)
#define HOST_CTRL   R8(0x01)
#define INT_EN      R8(0x02)
#define DEV_AD      R8(0x03)
#define FRAME_NO    R16(0x04)
#define SUSPEND     R8(0x06)
#define SPEED_TYPE  R8(0x08)
#define MIS_ST      R8(0x09)
#define INT_FG      R8(0x0A)
#define INT_ST      R8(0x0B)
#define RX_LEN      R16(0x0C)
#define EP_CONFIG   R32(0x10)
#define EP_TYPE     R32(0x14)
#define RX_DMA      R32(0x24)
#define TX_DMA      R32(0x64)
#define RX_MAX_LEN  R16(0xA0)
#define EP_PID      R8(0xE0)
#define RX_CTRL     R8(0xE3)
#define TX_LEN      R16(0xE4)
#define TX_CTRL     R8(0xE6)

#define UC_HOST_MODE  0x80
#define UC_SPEED_MASK 0x60
#define UC_SPEED_LOW  0x40
#define UC_SPEED_FULL 0x00
#define UC_SPEED_HIGH 0x20
#define UC_INT_BUSY   0x08
#define UC_RESET_SIE  0x04
#define UC_CLR_ALL    0x02
#define UC_DMA_EN     0x01

#define UH_SOF_EN       0x80
#define UH_PHY_SUSPENDM 0x10
#define UH_BUS_RESUME   0x04
#define UH_BUS_SUSPEND  0x02
#define UH_BUS_RESET    0x01
#define LINESTATE_MASK  0x30      /* SUSPEND: the PHY's line state, D+ in bit 4, D- in bit 5 */
#define LINESTATE_DP    0x10
#define LINESTATE_DM    0x20

#define UIE_SOF      0x08
#define UIE_SUSPEND  0x04
#define UIE_TRANSFER 0x02
#define UIE_DETECT   0x01
#define UIF_SOF      0x08
#define UIF_SUSPEND  0x04
#define UIF_TRANSFER 0x02
#define UIF_DETECT   0x01
#define UMS_ATTACH   0x02
#define UIS_TOG_OK   0x40

#define EP_TX_EN 0x00000008u
#define EP_RX_EN 0x00040000u
#define EP_TX_ISO 0x00000008u
#define EP_RX_ISO 0x00040000u

#define TOG1    0x08
#define RES_NO  0x04

#define PID_OUT   0x1
#define PID_IN    0x9
#define PID_SETUP 0xD
#define PID_ACK   0x2
#define PID_NYET  0x6
#define PID_NAK   0xA
#define PID_STALL 0xE
#define PID_DATA0 0x3
#define PID_DATA1 0xB

#define PACKET_MAX 1024u
#define NO_RESPONSE_TRIES 3u
#define CONTROL_TIMEOUT_MS 5000u      /* USB 2.0 9.2.6.4 */
#define RESUME_RECOVERY_MS 10u        /* TRSMRCY, USB 2.0 7.1.7.7 */
#define RESUME_SIGNAL_MS   30u        /* the K the block drives after TX_BUS_RESUME (RM 22.2.3.1) */
#define EP_MAX 32

static uint8_t rx_buf[PACKET_MAX] __attribute__((aligned(4)));
static uint8_t tx_buf[PACKET_MAX] __attribute__((aligned(4)));

enum { EP_FREE = 0, EP_IDLE, EP_PENDING };

typedef struct {
    uint8_t state, daddr, ep_addr, type, speed, toggle, setup, tries;
    uint16_t mps, len, done;
    uint16_t interval;     /* ticks between interrupt / isochronous polls */
    uint8_t *buf;
    uint32_t due;          /* tick before which it is not tried */
    uint32_t since;        /* tick the transfer was queued */
    uint32_t served;       /* isochronous: tick its last transfer went out */
    uint8_t served_valid;
    uint8_t setup_pkt[8];
} hcd_ep_t;

static hcd_ep_t eps[EP_MAX];
static volatile int16_t active = -1;
static volatile uint32_t active_since;
static uint8_t next_rr;
static volatile uint32_t ticks;            /* microframes (high speed) or frames since open */
static uint16_t last_frame;                /* FRAME_NO as last read */
static volatile bool irq_on, port_up;
/* Bus power (ch32rv_usb_hcd_usbhs1_power): a suspended port is down
 * (port_up false: nothing starts) with suspend_irq set, which takes the
 * suspend / resume interrupt. Only that pointer and its test are in a sketch
 * that never suspends. */
static void (*volatile suspend_irq)(void);
static uint8_t root_speed = CH32RV_USBH_FULL;

/* A tick is a microframe on a high-speed root port, a frame otherwise. */
static inline uint32_t ticks_per_ms(void) { return root_speed == CH32RV_USBH_HIGH ? 8u : 1u; }

/* FRAME_NO as ticks: the frame number (11 bits, extended by its steps) and,
 * at high speed, the microframe. Read at every SOF interrupt, whichever of
 * frame or microframe the interrupt comes at. */
static void advance_ticks(void)
{
    const uint16_t f = FRAME_NO;
    const uint32_t now_us = root_speed == CH32RV_USBH_HIGH ? ((uint32_t)(f & 0x07FFu) << 3 | (f >> 13)) : (f & 0x07FFu);
    const uint32_t then = root_speed == CH32RV_USBH_HIGH ? ((uint32_t)(last_frame & 0x07FFu) << 3 | (last_frame >> 13))
                                                         : (last_frame & 0x07FFu);
    const uint32_t wrap = root_speed == CH32RV_USBH_HIGH ? 0x4000u : 0x800u;
    const uint32_t step = (now_us - then) & (wrap - 1u);
    last_frame = f;
    ticks += step ? step : 1u;
}

static void stop_active(void)
{
    if (active < 0) {
        return;
    }
    EP_PID = 0;
    for (volatile uint32_t d = 0; d < 20000u && !(INT_FG & UIF_TRANSFER); d++) {
    }
    INT_EN &= (uint8_t)~UIE_TRANSFER;
    active = -1;
}

static inline void lock(bool *was)
{
    *was = irq_on;
    if (*was) {
        ch32rv_irq_disable(CH32RV_IRQN_USBHS);
    }
}

static inline void unlock(bool was)
{
    if (was) {
        ch32rv_irq_enable(CH32RV_IRQN_USBHS);
    }
}

static hcd_ep_t *find(uint8_t daddr, uint8_t ep_addr)
{
    for (unsigned i = 0; i < EP_MAX; i++) {
        if (eps[i].state != EP_FREE && eps[i].daddr == daddr && eps[i].ep_addr == ep_addr) {
            return &eps[i];
        }
    }
    return NULL;
}

/* bInterval as ticks: high speed 2^(b-1) microframes; full and low speed b
 * frames (interrupt) or 2^(b-1) frames (isochronous). */
static uint16_t interval_ticks(uint8_t type, uint8_t b, uint8_t speed)
{
    if (type != CH32RV_USBH_INTERRUPT && type != CH32RV_USBH_ISOCHRONOUS) {
        return 1;
    }
    if (speed == CH32RV_USBH_HIGH || type == CH32RV_USBH_ISOCHRONOUS) {
        const uint8_t e = b < 1 ? 0 : (b > 16 ? 15 : (uint8_t)(b - 1));
        const uint32_t t = (1u << e) * (speed == CH32RV_USBH_HIGH ? 1u : ticks_per_ms());
        return (uint16_t)(t > 0xFFFFu ? 0xFFFFu : t);
    }
    return (uint16_t)((b ? b : 1) * ticks_per_ms());
}

static hcd_ep_t *open_ep(uint8_t daddr, uint8_t ep_addr, uint16_t mps, uint8_t type, uint8_t interval, uint8_t speed)
{
    hcd_ep_t *ep = find(daddr, ep_addr);
    if (!ep) {
        for (unsigned i = 0; i < EP_MAX; i++) {
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
        stop_active();
    }
    memset(ep, 0, sizeof *ep);
    ep->state = EP_IDLE;
    ep->speed = speed;
    ep->daddr = daddr;
    ep->ep_addr = ep_addr;
    ep->mps = mps ? mps : 8;
    ep->type = type;
    ep->interval = interval_ticks(type, interval, speed);
    return ep;
}

static void finish(hcd_ep_t *ep, uint8_t result)
{
    ep->state = EP_IDLE;
    const uint8_t address = ep->setup ? 0x00 : ep->ep_addr;
    const uint16_t length = ep->setup ? 8u : ep->done;
    ep->setup = 0;
    ch32rv_usbh_isr_xfer(ep->daddr, address, result, length);
}

static void start(int16_t index)
{
    hcd_ep_t *ep = &eps[index];
    active = index;
    active_since = ticks;
    DEV_AD = (uint8_t)(ep->daddr & 0x7Fu);
    const uint8_t speed = ep->speed == CH32RV_USBH_HIGH ? UC_SPEED_HIGH
                          : ep->speed == CH32RV_USBH_LOW ? UC_SPEED_LOW
                                                         : UC_SPEED_FULL;
    CONTROL = (uint8_t)((CONTROL & ~UC_SPEED_MASK) | speed);
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;
    EP_TYPE = iso ? (EP_TX_ISO | EP_RX_ISO) : 0u;
    uint8_t pid;
    if (ep->setup) {
        pid = PID_SETUP;
        memcpy(tx_buf, ep->setup_pkt, 8);
        TX_LEN = 8;
        TX_CTRL = 0;                                     /* DATA0 */
    } else if (ep->ep_addr & 0x80u) {
        pid = PID_IN;
        RX_MAX_LEN = ep->mps;
        RX_CTRL = (uint8_t)((ep->toggle ? TOG1 : 0) | (iso ? RES_NO : 0));
    } else {
        pid = PID_OUT;
        const uint16_t left = (uint16_t)(ep->len - ep->done);
        const uint16_t n = left < ep->mps ? left : ep->mps;
        if (n) {
            memcpy(tx_buf, ep->buf + ep->done, n);
        }
        TX_LEN = n;
        TX_CTRL = (uint8_t)((ep->toggle ? TOG1 : 0) | (iso ? RES_NO : 0));
    }
    EP_PID = (uint8_t)((pid << 4) | (ep->ep_addr & 0x0Fu));
    INT_EN |= UIE_TRANSFER;
    INT_FG = UIF_TRANSFER;                               /* clearing the flag lets it run */
}

static void schedule(void)
{
    if (active >= 0 || !port_up) {
        return;
    }
    for (unsigned k = 0; k < EP_MAX; k++) {
        const unsigned i = (next_rr + k) % EP_MAX;
        hcd_ep_t *ep = &eps[i];
        if (ep->state == EP_PENDING && (int32_t)(ticks - ep->due) >= 0) {
            next_rr = (uint8_t)((i + 1) % EP_MAX);
            if (ep->type == CH32RV_USBH_ISOCHRONOUS) {
                ep->served = ticks;
                ep->served_valid = 1;
            }
            start((int16_t)i);
            return;
        }
    }
}

static void transfer_done(void)
{
    INT_EN &= (uint8_t)~UIE_TRANSFER;
    EP_PID = 0;
    const uint8_t status = INT_ST;
    const int16_t index = active;
    active = -1;
    if (index < 0) {
        return;
    }
    hcd_ep_t *ep = &eps[index];
    if (ep->state != EP_PENDING) {
        return;                                          /* aborted while on the wire */
    }
    const uint8_t res = status & 0x0Fu;
    const bool in = !ep->setup && (ep->ep_addr & 0x80u);
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;

    if (in && (res == PID_DATA0 || res == PID_DATA1)) {
        if (!iso && !(status & UIS_TOG_OK)) {
            ep->tries = 0;                               /* a repeat (lost ACK): drop it, ask again */
        } else {
            ep->tries = 0;
            ep->toggle ^= 1u;
            const uint16_t got = (uint16_t)(RX_LEN & 0x7FFu);
            const uint16_t room = (uint16_t)(ep->len - ep->done);
            const uint16_t keep = got < room ? got : room;
            if (keep && ep->buf) {
                memcpy(ep->buf + ep->done, rx_buf, keep);
            }
            ep->done = (uint16_t)(ep->done + keep);
            if (iso || got < ep->mps || ep->done >= ep->len) {
                finish(ep, got > room ? CH32RV_USBH_FAILED : CH32RV_USBH_OK);
            }
        }
    } else if (!in && (res == PID_ACK || res == PID_NYET || iso)) {
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
        } else {
            ep->done = (uint16_t)(ep->done + (TX_LEN & 0x7FFu));
            if (iso || ep->done >= ep->len) {
                finish(ep, CH32RV_USBH_OK);
            }
        }
    } else if (res == PID_NAK) {
        ep->tries = 0;
        ep->due = ticks + (ep->type == CH32RV_USBH_INTERRUPT ? ep->interval : 1u);
        if (ep->type == CH32RV_USBH_CONTROL && (uint32_t)(ticks - ep->since) > CONTROL_TIMEOUT_MS * ticks_per_ms()) {
            finish(ep, CH32RV_USBH_TIMEOUT);
        }
    } else if (res == PID_STALL) {
        ep->toggle = 0;
        finish(ep, CH32RV_USBH_STALLED);
    } else if (iso && res == 0) {
        finish(ep, CH32RV_USBH_OK);                      /* nothing this interval */
    } else if (res == 0 && ++ep->tries < NO_RESPONSE_TRIES) {
        ep->due = ticks + 1u;                            /* no handshake: try again */
    } else {
        finish(ep, CH32RV_USBH_FAILED);
    }
    schedule();
}

static void port_down(void)
{
    port_up = false;
    if (active >= 0) {
        INT_EN &= (uint8_t)~UIE_TRANSFER;
        EP_PID = 0;
        active = -1;
    }
}

static void isr(void)
{
    const uint8_t flags = INT_FG;
    /* DETECT only while enabled: the reset's SE0 raises it (hcd_port_reset). */
    if ((flags & UIF_DETECT) && (INT_EN & UIE_DETECT)) {
        INT_FG = UIF_DETECT;
        if (MIS_ST & UMS_ATTACH) {
            ch32rv_usbh_isr_port(true);
        } else {
            port_down();
            HOST_CTRL &= (uint8_t)~UH_SOF_EN;
            ch32rv_usbh_isr_port(false);
        }
    }
    if ((flags & UIF_TRANSFER) && (INT_EN & UIE_TRANSFER)) {
        transfer_done();
    }
    if ((flags & UIF_SUSPEND) && suspend_irq) {
        suspend_irq();
    }
    if (flags & UIF_SOF) {
        INT_FG = UIF_SOF;
        advance_ticks();
        /* A transaction takes microseconds; one still open after two frames
         * lost its completion: no handshake. */
        if (active >= 0 && (uint32_t)(ticks - active_since) >= 2u * ticks_per_ms()) {
            hcd_ep_t *ep = &eps[active];
            stop_active();
            if (ep->state == EP_PENDING) {
                if (++ep->tries < NO_RESPONSE_TRIES) {
                    ep->due = ticks + 1u;
                } else {
                    finish(ep, CH32RV_USBH_FAILED);
                }
            }
        }
        schedule();
    }
}

/* The crystal the PHY's PLL divides its reference from: the Clock Source
 * menu's when HSE is chosen there, else the boards' usual 8 MHz. */
#ifdef CH32RV_CLOCK_HSE_HZ
#define USBHS1_HOST_HSE_HZ CH32RV_CLOCK_HSE_HZ
#else
#define USBHS1_HOST_HSE_HZ 8000000u
#endif

/* ------------------------------------------------------------- the table */
static bool hcd_open(void)
{
    if (ch32rv_usbhs1_irq_hook && ch32rv_usbhs1_irq_hook != isr) {
        return false;                                    /* the device stack has the controller */
    }
    if ((uint32_t)((uintptr_t)rx_buf - 0x20000000u) + PACKET_MAX > 0x20000u ||
        (uint32_t)((uintptr_t)tx_buf - 0x20000000u) + PACKET_MAX > 0x20000u) {
        return false;                                    /* the DMA reaches the first 128 KB only */
    }
    if (!ch32rv_usbhs1_clock_on(USBHS1_HOST_HSE_HZ)) {
        return false;
    }
    memset(eps, 0, sizeof eps);
    active = -1;
    port_up = false;
    suspend_irq = nullptr;
    ticks = 0;
    root_speed = CH32RV_USBH_FULL;
    CONTROL = UC_CLR_ALL | UC_RESET_SIE;
    delayMicroseconds(10);
    CONTROL = 0;
    CONTROL = UC_HOST_MODE | UC_SPEED_HIGH | UC_INT_BUSY | UC_DMA_EN;
    EP_CONFIG = EP_TX_EN | EP_RX_EN;
    EP_TYPE = 0;
    RX_DMA = (uint32_t)(uintptr_t)rx_buf;
    TX_DMA = (uint32_t)(uintptr_t)tx_buf;
    HOST_CTRL = UH_PHY_SUSPENDM;
    RX_MAX_LEN = PACKET_MAX;
    DEV_AD = 0;
    RX_CTRL = 0;
    TX_CTRL = 0;
    INT_FG = 0xFF;
    INT_EN = UIE_DETECT | UIE_SOF;
    ch32rv_usbhs1_irq_hook = isr;
    irq_on = true;
    ch32rv_irq_enable(CH32RV_IRQN_USBHS);
    return true;
}

static void hcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBHS);
    irq_on = false;
    INT_EN = 0;
    HOST_CTRL = 0;
    CONTROL = UC_CLR_ALL | UC_RESET_SIE;
    delayMicroseconds(10);
    CONTROL = 0;
    memset(eps, 0, sizeof eps);
    active = -1;
    port_up = false;
    ch32rv_usbhs1_clock_off();
    if (ch32rv_usbhs1_irq_hook == isr) {
        ch32rv_usbhs1_irq_hook = nullptr;
    }
}

static bool hcd_connected(void) { return (MIS_ST & UMS_ATTACH) != 0; }

static void hcd_port_reset(void)
{
    bool was;
    lock(&was);
    port_down();
    INT_EN &= (uint8_t)~UIE_DETECT;
    DEV_AD = 0;
    CONTROL = (uint8_t)((CONTROL & ~UC_SPEED_MASK) | UC_SPEED_HIGH);   /* the chirp needs high speed asked for */
    HOST_CTRL = (uint8_t)((HOST_CTRL & ~(UH_SOF_EN | UH_BUS_SUSPEND)) | UH_BUS_RESET);
    unlock(was);
}

/* The reset ends here; the PHY has the speed (chirp included) a little later. */
static uint8_t hcd_port_reset_end(void)
{
    bool was;
    lock(&was);
    HOST_CTRL &= (uint8_t)~UH_BUS_RESET;
    unlock(was);
    delay(2);
    lock(&was);
    const uint8_t s = SPEED_TYPE & 3u;
    root_speed = s == 1u ? CH32RV_USBH_HIGH : s == 2u ? CH32RV_USBH_LOW : CH32RV_USBH_FULL;
    CONTROL = (uint8_t)((CONTROL & ~UC_SPEED_MASK) |
                        (root_speed == CH32RV_USBH_HIGH ? UC_SPEED_HIGH
                         : root_speed == CH32RV_USBH_LOW ? UC_SPEED_LOW
                                                         : UC_SPEED_FULL));
    last_frame = FRAME_NO;
    HOST_CTRL |= UH_SOF_EN;
    INT_FG = UIF_DETECT;                                 /* the reset itself is not an attach */
    INT_EN |= UIE_DETECT;
    port_up = true;
    schedule();
    unlock(was);
    return root_speed;
}

static void hcd_port_disable(void)
{
    bool was;
    lock(&was);
    stop_active();
    port_down();
    HOST_CTRL &= (uint8_t)~UH_SOF_EN;
    unlock(was);
}

/* While suspended: the suspend / resume event with the bus in K (D- high at
 * full and high speed - the bus is at full speed's levels in suspend - D+ at
 * low speed) is the device's remote wakeup. Once the port is up again (a
 * reset after a detach while suspended) it is not this one's. */
static void on_suspend_irq(void)
{
    INT_FG = UIF_SUSPEND;
    const uint8_t k = root_speed == CH32RV_USBH_LOW ? LINESTATE_DP : LINESTATE_DM;
    if (port_up) {
        INT_EN &= (uint8_t)~UIE_SUSPEND;
        suspend_irq = nullptr;
    } else if ((SUSPEND & LINESTATE_MASK) == k) {
        INT_EN &= (uint8_t)~UIE_SUSPEND;
        ch32rv_usbh_isr_wakeup();
    }
}

/* Suspend: SOF off and TX_BUS_SUSPEND, held while suspended (RM 22.2.3.1:
 * "pulled up by software"). Resume: TX_BUS_RESUME for a moment, and the
 * block drives K for 30 ms itself; SOF starts again after it, and transfers
 * after the device's recovery. Build only - no V30x host is wired to a
 * device on the bench (the TODO's usb-peer-power). */
static bool hcd_port_suspend(bool on)
{
    bool was;
    lock(&was);
    const bool suspended = suspend_irq && !port_up;
    bool ok = true;
    if (on && !suspended) {
        ok = port_up;
        if (ok) {
            port_up = false;
            stop_active();
            suspend_irq = on_suspend_irq;
            HOST_CTRL = (uint8_t)((HOST_CTRL & ~UH_SOF_EN) | UH_BUS_SUSPEND);
            INT_FG = UIF_SUSPEND;
            INT_EN |= UIE_SUSPEND;
        }
    } else if (!on && suspended) {
        INT_EN &= (uint8_t)~UIE_SUSPEND;
        suspend_irq = nullptr;
        HOST_CTRL = (uint8_t)((HOST_CTRL & ~UH_BUS_SUSPEND) | UH_BUS_RESUME);
        delayMicroseconds(1);
        HOST_CTRL &= (uint8_t)~UH_BUS_RESUME;
        unlock(was);
        for (const uint32_t t0 = millis(); (uint32_t)(millis() - t0) <= RESUME_SIGNAL_MS;) {
        }
        lock(&was);
        const bool attached = (MIS_ST & UMS_ATTACH) != 0;
        if (attached) {
            last_frame = FRAME_NO;
            HOST_CTRL |= UH_SOF_EN;
        }
        unlock(was);
        for (const uint32_t t0 = millis(); (uint32_t)(millis() - t0) <= RESUME_RECOVERY_MS;) {
        }
        lock(&was);
        if (attached && (MIS_ST & UMS_ATTACH)) {          /* not detached meanwhile */
            port_up = true;
            schedule();
        }
    }
    unlock(was);
    return ok;
}

static void hcd_device_close(uint8_t daddr)
{
    bool was;
    lock(&was);
    for (unsigned i = 0; i < EP_MAX; i++) {
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

static bool hcd_ep_open(uint8_t daddr, const CH32RVUsbHcdEp &e)
{
    if (e.mps > PACKET_MAX || e.ttHub) {
        return false;                                    /* no split transactions on this IP (above) */
    }
    bool was;
    lock(&was);
    bool ok;
    if ((e.address & 0x0Fu) == 0) {
        ok = open_ep(daddr, 0x00, e.mps, CH32RV_USBH_CONTROL, 0, e.speed) &&
             open_ep(daddr, 0x80, e.mps, CH32RV_USBH_CONTROL, 0, e.speed);
    } else {
        ok = open_ep(daddr, e.address, e.mps, e.type, e.interval, e.speed) != NULL;
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
        ep->due = ticks;
        /* One isochronous packet per interval: the next an interval after the
         * last, not as soon as it is queued. */
        if (ep->type == CH32RV_USBH_ISOCHRONOUS && ep->served_valid &&
            (int32_t)(ep->served + ep->interval - ticks) > 0) {
            ep->due = ep->served + ep->interval;
        }
        ep->since = ticks;
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
    const bool ok = ep != NULL;
    if (ok) {
        hcd_ep_t *ep_in = find(daddr, 0x80);
        if ((int16_t)(ep - eps) == active || (ep_in && (int16_t)(ep_in - eps) == active)) {
            stop_active();
        }
        if (ep_in) {
            ep_in->state = EP_IDLE;
        }
        memcpy(ep->setup_pkt, packet, 8);
        ep->buf = NULL;
        ep->len = 8;
        ep->done = 0;
        ep->tries = 0;
        ep->toggle = 0;
        ep->setup = 1;
        ep->due = ticks;
        ep->since = ticks;
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

static void hcd_reset_toggle(uint8_t daddr, uint8_t address)
{
    hcd_ep_t *ep = find(daddr, address);
    if (ep) {
        ep->toggle = 0;
    }
}

static uint32_t hcd_frame(void) { return ticks / ticks_per_ms(); }

/* A probe's attach makes SystemInit() run again, which takes the PHY's PLL
 * away: put it back from the task. */
static void hcd_poll(void)
{
    if (!ch32rv_usbhs1_clock_ok()) {
        ch32rv_usbhs1_clock_on(USBHS1_HOST_HSE_HZ);
    }
}

/* The classes' buffers are full speed's 64 bytes on V30x (a sketch on USBFS
 * carries no more); a high-speed endpoint's come from the heap. */
static void *buffer_alloc(unsigned bytes) { return malloc(bytes); }
static void buffer_free(void *buffer) { free(buffer); }

const CH32RVUsbHcd ch32rv_usb_hcd_usbhs1 = {
    "USBHS host (CH32V305/V307/V317)", PACKET_MAX,
    hcd_open, hcd_close, hcd_connected, hcd_port_reset, hcd_port_reset_end, hcd_port_disable,
    hcd_ep_open, hcd_ep_close, hcd_device_close, hcd_xfer, hcd_setup, hcd_abort, hcd_reset_toggle, hcd_frame,
    hcd_poll, nullptr, nullptr, nullptr, nullptr, buffer_alloc, buffer_free,
};
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbhs1_power = {&ch32rv_usb_hcd_usbhs1, hcd_port_suspend};

#endif
