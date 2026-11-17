/* Host controller driver for the USBSS controller (USB 3 Gen 1, 5 Gbit/s) of
 * CH32H417 / H416 in host mode. SuperSpeed only: a USB 2.0 device on the
 * same connector is the USBHS controller's (CH32RVUsbHcdUsbhs2.cpp, PB8 /
 * PB9), a host of its own.
 *
 * FIRST DRAFT, BUILT BUT NOT YET RUN ON HARDWARE (2026-11-14). Every
 * "guess:" below is a reading of the RM or the EVT that a bench run has to
 * confirm; "measure:" says what to look at.
 *
 * Sources: CH32H417RM V1.8 chapter 27 (27.2.5 the host registers, 27.2.6 the
 * link) and the EVT's USBSS host demos (EXAM/USBSS/HOST/..., Common/
 * ch32h417_usbss_host.c for the link, ch32h417_enum.c for control transfers
 * and the SS hub, ch32h417_udisk.c for bulk). The EVT polls everything and
 * runs one endpoint at a time; this driver is interrupt driven and runs the
 * scheduler of the other drivers (CH32RVUsbHcdUsbhs2.cpp) on this block:
 *
 *   - the block has one transmit and one receive engine for all endpoints of
 *     all devices (UH_TX_* / UH_RX_*; the device role's per-endpoint chains
 *     are not used by the host). One transaction is on the wire at a time,
 *     round robin over the endpoints with a transfer pending. Each start
 *     writes the device address (USB_CONTROL[30:24]) and the route string
 *     (LINK_TX_ROUTE), so devices behind SuperSpeed hubs share the engines;
 *   - sequence numbers are software's: the RM has the hardware count them
 *     for endpoints other than EP0, but with one engine for every endpoint
 *     each start writes the endpoint's own (as the EVT does: it adds the
 *     packets moved to its copy after every transfer, udisk.c:498);
 *   - flow control: an NRDY, an ACK-TP with NumP 0 after OUT / SETUP, or EOB
 *     on an IN puts the endpoint in flow control until an ERDY for its number
 *     and direction comes (UHIE_ERDY; HOST_STATUS names the endpoint). An
 *     ERDY carries no device address, so every waiting endpoint with that
 *     number and direction is tried again - the wrong device answers NRDY
 *     again, which costs one transaction. A missed ERDY is covered by trying
 *     again after ERDY_RETRY_MS;
 *   - the clock is the ITP interrupt (125 us, the host's isochronous
 *     timestamp): interrupt and isochronous endpoints are served every
 *     2^(bInterval-1) of them. Timeouts use millis(), since no ITP goes out
 *     while the link is in U1 / U2 / U3;
 *   - packets go to and from the caller's buffer directly when it is 4-byte
 *     aligned and has room for whole packets, else through one bounce buffer
 *     each way (the device role found that a receive writes a whole maximum
 *     packet whatever room is left; guess: the host's receive does too).
 *
 * Bursts: the EVT never offers more than one packet (its BrustNum is forced
 * to 1, udisk.c:368 / 423) and sends OUT one packet at a time. BURST_MAX
 * below is that, 1; the start and completion code already handles more
 * packets per arm (HOST_RX/TX_NUMP, the NUMP window, the DMA stride), so
 * raising it is the experiment (design step 4).
 *
 * Isochronous: one transfer is one service interval, up to (Mult + 1) x
 * (bMaxBurst + 1) packets (the companion's), in as many arms as the buffer
 * allows. The ISO bits make the hardware reset the sequence at each ITP (RM
 * UH_RX_CTRL.SEQ) and count HOST_RX_NUMP down per packet.
 *
 * Streams (UAS): not carried. The host registers have no Stream ID field
 * (searched 2026-11-14: RM 27.2.5 / 27.2.1 for "stream", the EVT's
 * ch32h417.h USBSSH_TypeDef and ch32h417_usbss_host.h, every UH_* / HOST_* /
 * TP_RX_* bit; only LMP and DEV_NOTIF payloads are software's, the data
 * packet header is built by the hardware). A bulk endpoint that offers
 * streams is opened and runs without them (Stream ID 0 never sent), which
 * is what a BOT interface needs; a UAS interface would not work.
 * STREAMS HOOK: Ep::streams keeps the companion's MaxStreams for a future
 * part or a register found later.
 *
 * Link power: the stack sends SET_SEL / U1_ENABLE / U2_ENABLE and then calls
 * linkAllow(); the device may then take the link to U1 / U2, and the PHY's
 * register 0x12 is changed on entry and put back through Recovery, as the
 * device role must (CH32RVUsbDcdUsbss.cpp, TinyUSB PR #3779). A transfer due
 * while the link sleeps asks it back to U0 (LINK_TX_UX_EXIT). portSuspend()
 * sends LGO_U3 / wakes it.
 *
 * The block is shared with the device role: open() refuses it while
 * CH32RVUsbDevice holds the core's interrupt hooks (internal/ch32rv_usbss.h). */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416)

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_usbss.h"
#include "internal/CH32RVUsbHcd.h"

#define SS_BASE CH32RV_USBSS_BASE
#define R8(o)  (*(volatile uint8_t *)(SS_BASE + (o)))
#define R16(o) (*(volatile uint16_t *)(SS_BASE + (o)))
#define R32(o) (*(volatile uint32_t *)(SS_BASE + (o)))

/* LINK */
#define LINK_CFG            R32(0x00)
#define LINK_CTRL           R32(0x04)
#define LINK_INT_CTRL       R32(0x08)
#define LINK_INT_FLAG       R32(0x0C)
#define LINK_STATUS         R32(0x10)
#define LINK_ITP_PRE        R8(0x17)
#define LINK_LPM_CR         R16(0x50)
#define LINK_TX_ROUTE       R32(0x4C)              /* not in the RM; the EVT's ch32h417.h */
#define LINK_LMP_PORT_CAP   R32(0x54)
#define LINK_LMP_RX_DATA0   R32(0x58)
#define LINK_LMP_TX_DATA0   R32(0x64)
#define LINK_LMP_TX_DATA1   R32(0x68)
#define LINK_LMP_TX_DATA2   R32(0x6C)
/* USB */
#define USB_CONTROL         R32(0x70)
#define USB_STATUS          R32(0x74)
#define UEP_TX_EN           R16(0x80)
#define UEP_RX_EN           R16(0x82)
/* Host */
#define UH_TX_CTRL          R32(0x84)
#define UH_RX_CTRL          R32(0x88)
#define UH_TX_DMA           R32(0x8C)
#define UH_RX_DMA           R32(0x90)
#define UH_TX_DMA_OFS       R32(0x94)
#define UH_RX_DMA_OFS       R32(0x98)
#define HOST_TX_NUMP        R16(0x9C)
#define HOST_RX_NUMP        R16(0x9E)              /* [15:8] packets received, [7:0] still expected */
#define HOST_STATUS         R32(0xA0)
#define TP_RX_DATA0         R32(0xA8)

#define LINK_U3_RESET       0x80000000u
#define LINK_U2_ALLOW       0x00020000u
#define LINK_U1_ALLOW       0x00010000u
#define LINK_LTSSM_MODE     0x00008000u
#define LINK_U2_RXDET       0x00001000u
#define LINK_RX_EQ_EN       0x00000040u
#define LINK_PHY_RESET      0x00000008u
#define LINK_RX_TERM_EN     0x00000002u
#define LINK_DOWN_MODE      0x00000001u

#define LINK_HOT_RESET      0x00010000u            /* TX_TS_CFG bit 0 */
#define LINK_TX_LGO_U3      0x00008000u
#define LINK_TX_UX_EXIT     0x00000200u
#define LINK_TX_WARM_RST    0x00000100u
#define LINK_GO_RX_DET      0x00000080u
#define LINK_GO_DISABLED    0x00000010u
/* PD_MODE P2 as the EVT's host writes it (POWER_MODE_2 = 6): bit 2 is
 * reserved in the RM, and the device role writes 2 alone. Guess: the EVT's
 * value is right for the host; measure: training with 0x2 as well. */
#define LINK_PD_P2          0x00000006u

#define LINK_IF_STATE_CHG   0x80000000u
#define LINK_IF_RX_LMP_TOUT 0x00800000u
#define LINK_IF_TX_LMP      0x00400000u
#define LINK_IF_RX_LMP      0x00200000u
#define LINK_IF_WARM_RST    0x00002000u
#define LINK_IF_TERM_PRES   0x00000400u

#define LINK_STATE_MASK     0x00000F00u
#define LINK_STATE_U0       0x00000000u
#define LINK_STATE_U1       0x00000100u
#define LINK_STATE_U2       0x00000200u
#define LINK_STATE_U3       0x00000300u
#define LINK_STATE_DISABLE  0x00000400u
#define LINK_STATE_RXDET    0x00000500u
#define LINK_STATE_INACTIVE 0x00000600u
#define LINK_STATE_RECOVERY 0x00000800u
#define LINK_STATE_HOTRST   0x00000900u
#define LINK_BUSY           0x00000004u

#define LINK_LPM_EN         0x0200u
#define LINK_LMP_TX_CAP_VLD 0x40000000u

#define LMP_SUBTYPE_MASK    (0xFu << 5)
#define LMP_PORT_CAP        (0x4u << 5)
#define LMP_PORT_CFG        (0x5u << 5)
#define LMP_PORT_CFG_RES    (0x6u << 5)
#define LMP_LINK_SPEED      (1u << 9)
#define LMP_NUM_HP_BUF      4u
#define LMP_DOWN_STREAM     (1u << 16)

#define USBSS_ADDR_MASK     0x7F000000u
#define USBSS_UIE_ITP       0x00100000u
#define USBSS_UHIE_NOTIF    0x00040000u
#define USBSS_UHIE_ERDY     0x00020000u
#define USBSS_UIE_TRANSFER  0x00010000u
#define USBSS_HP_PENDING    0x00000200u
#define USBSS_HOST_MODE     0x00000080u
#define USBSS_ITP_EN        0x00000040u
#define USBSS_FORCE_RST     0x00000004u
#define USBSS_USB_CLR_ALL   0x00000002u
#define USBSS_DMA_EN        0x00000001u

#define USBSS_UIF_ITP       0x00000010u
#define USBSS_UHIF_NOTIF    0x00000004u
#define USBSS_UHIF_ERDY     0x00000002u
#define USBSS_UIF_TRANSFER  0x00000001u
#define USBSS_FLAGS         (USBSS_UIF_ITP | USBSS_UHIF_NOTIF | USBSS_UHIF_ERDY | USBSS_UIF_TRANSFER)
#define HRX_RES(s)          (((s) >> 30) & 3u)     /* 0 ACK, 1 FAILED (deferred), 2 NRDY, 3 STALL */
#define HTX_RES(s)          (((s) >> 22) & 3u)
#define HOST_ACK_NUMP(s)    (((s) >> 16) & 0x1Fu)
enum : uint8_t { RES_ACK = 0, RES_FAILED = 1, RES_NRDY = 2, RES_STALL = 3 };

/* UH_TX_CTRL / UH_RX_CTRL. The RM calls bit 31 of TX "IN completion" and of
 * RX "OUT completion"; the EVT uses each for its own direction (the RM has
 * them crossed). VALID (bit 21) starts the transaction: the EVT's
 * UH_RTX_VALID, inside what the RM lists as the read-only RES field. The
 * EVT writes the NUMP window at bit 24 on TX too, where the RM has
 * reserved bits; done the same here. */
#define UH_ACT              0x80000000u
#define UH_ISO              0x40000000u
#define UH_TX_SETUP         0x20000000u
#define UH_TX_STATUS        0x10000000u
#define UH_NUMP_SHIFT       24
#define UH_TX_LPF           0x00800000u
#define UH_VALID            0x00200000u
#define UH_SEQ_SHIFT        16
#define UH_EP_SHIFT         12
#define UH_LEN_MASK         0x000007FFu

#define HST_RX_ISO_PKT_ERR  0x00020000u
#define HST_RX_EOB_LPF      0x00010000u
#define HST_ERDY_DIR        0x00008000u            /* guess: 1 = IN, as EP_DIR elsewhere in the block */
#define HST_ERDY_EP(h)      (((h) >> 4) & 0xFu)

#define USBSS_UH_TX_EN      0x0002u
#define USBSS_UH_RX_EN      0x0002u

namespace {

constexpr uint16_t PACKET_MAX = 1024;
constexpr uint16_t EP0_MPS = 512;
constexpr unsigned EP_MAX = 32;
constexpr unsigned ROUTES = 16;
/* Packets per arm, and the burst window offered / sent: the EVT's 1 (see the
 * top). At most 16 (HOST_*_NUMP takes more, the window field 5 bits). */
constexpr uint8_t BURST_MAX = 1;
constexpr uint8_t FAIL_TRIES = 3;
constexpr uint32_t CONTROL_TIMEOUT_MS = 5000;      /* USB 2.0 9.2.6.4, as the other drivers */
/* An endpoint waiting for an ERDY is tried again after this long. Guess:
 * enough for a missed ERDY and short enough not to stall a CDC; measure:
 * how often it fires (it should not, with the ERDY interrupt working). */
constexpr uint32_t ERDY_RETRY_MS = 100;
/* A transaction whose completion never came. 1024-byte packets take about
 * 2 us each on the wire. */
constexpr uint32_t TRANSACTION_MS = 3;
/* Warm reset: the EVT holds it 80 ms (udisk.c:952); the RM says 80-120. */
constexpr uint32_t WARM_RESET_MS = 80;
constexpr uint32_t RETRAIN_MS = 500;               /* after it, for PORT_CFG_RES again */
/* The EVT waits 1 ms before every STATUS TP: "without it the STATUS packet
 * cannot be transmitted" (enum.c:220). Kept as 8 ITPs. Guess about why: the
 * SETUP's ACK-TP with NumP 0 (flow control) not waited for; this driver does
 * wait for the ERDY, so measure whether the delay can go. */
constexpr uint32_t STATUS_DELAY_TICKS = 8;

enum : uint8_t { EP_FREE = 0, EP_IDLE, EP_PENDING, EP_WAIT_ERDY };

struct Ep {
    uint8_t state, daddr, ep_addr, type;
    uint8_t seq;            /* 5 bits */
    uint8_t window;         /* packets per arm (bMaxBurst + 1, at most BURST_MAX) */
    uint8_t per_interval;   /* periodic: packets per service interval */
    uint8_t in_interval;    /* isochronous: packets moved in this one */
    uint8_t tries;
    uint8_t streams;        /* STREAMS HOOK: the companion's MaxStreams; not carried */
    bool setup, status;     /* EP0: the transfer pending is the SETUP / the STATUS TP */
    bool fc;                /* in flow control: wait for an ERDY before the next transaction */
    bool bounced;           /* the arm in flight uses the bounce buffer */
    uint8_t armed_pkts;
    uint16_t mps, len, done, armed;
    uint16_t interval;      /* ITPs between services (periodic) */
    uint32_t route;
    uint8_t *buf;
    uint32_t due;           /* ITP count before which it is not started */
    uint32_t next;          /* periodic: the next service */
    uint32_t since_ms;      /* the transfer was queued */
    uint32_t wait_ms;       /* it started waiting for an ERDY */
    uint8_t setup_pkt[8];
};

uint8_t tx_buf[PACKET_MAX] __attribute__((aligned(16)));
uint8_t rx_buf[PACKET_MAX] __attribute__((aligned(16)));
Ep eps[EP_MAX];
struct Route {
    uint8_t daddr;
    bool used;
    uint32_t route;
} routes[ROUTES];

volatile int16_t active = -1;
volatile bool active_tx;
volatile uint32_t active_ms;
uint8_t next_rr;
volatile uint32_t ticks;                           /* ITPs */
volatile bool irq_on;

/* The link */
volatile bool link_up;                             /* PORT_CFG_RES received, not lost since */
volatile bool port_up;                             /* the stack has reset it: transfers may run */
volatile bool resetting;                           /* a reset of the stack's: no port events */
volatile bool low_power;                           /* PHY reconfigured for U1 / U2 / U3 */
volatile bool suspended;                           /* U3 */
volatile bool inactive;                            /* SS.Inactive: a warm reset brings it back */
bool warm_next;                                    /* portDisable() was called: the next reset is warm */
volatile uint8_t warm_phase;                       /* 0 none, 1 warm reset on, 2 waiting for the link */
volatile uint32_t warm_at;
volatile uint8_t retrain_fail;
volatile uint32_t release_at;                      /* millis() to let a held link retrain; 0 none */
uint32_t opened_ms;

void usb_isr(void);
void link_isr(void);

/* ------------------------------------------------------------ helpers */
void lock(bool *was)
{
    *was = irq_on;
    if (*was) {
        ch32rv_irq_disable(CH32RV_IRQN_USBSS);
        ch32rv_irq_disable(CH32RV_IRQN_USBSS_LINK);
    }
}

void unlock(bool was)
{
    if (was) {
        ch32rv_irq_enable(CH32RV_IRQN_USBSS_LINK);
        ch32rv_irq_enable(CH32RV_IRQN_USBSS);
    }
}

Ep *find(uint8_t daddr, uint8_t ep_addr)
{
    for (Ep &e : eps) {
        if (e.state != EP_FREE && e.daddr == daddr && e.ep_addr == ep_addr) {
            return &e;
        }
    }
    return nullptr;
}

uint32_t route_of(uint8_t daddr)
{
    for (const Route &r : routes) {
        if (r.used && r.daddr == daddr) {
            return r.route;
        }
    }
    return 0;
}

/* Take the transaction off the engine. Guess: writing 0 to the control
 * word stops one in flight (the EVT writes 0 after every completion and
 * before every new setup); measure: an abort during a long NRDY wait. */
void stop_active()
{
    if (active < 0) {
        return;
    }
    if (active_tx) {
        UH_TX_CTRL = 0;
    } else {
        UH_RX_CTRL = 0;
    }
    active = -1;
}

/* bInterval as ITPs: 2^(bInterval-1) (USB 3.2 9.6.6, both periodic types). */
uint16_t interval_ticks(uint8_t type, uint8_t b)
{
    if (type != CH32RV_USBH_INTERRUPT && type != CH32RV_USBH_ISOCHRONOUS) {
        return 1;
    }
    const uint8_t e = b < 1 ? 0 : (b > 16 ? 15 : (uint8_t)(b - 1));
    const uint32_t t = 1u << e;
    return (uint16_t)(t > 0xFFFFu ? 0xFFFFu : t);
}

void finish(Ep *ep, uint8_t result)
{
    ep->state = EP_IDLE;
    const uint8_t address = ep->setup ? 0x00 : ep->ep_addr;
    const uint16_t length = ep->setup ? 8u : ep->done;
    ep->setup = ep->status = false;
    if (ep->type == CH32RV_USBH_INTERRUPT || ep->type == CH32RV_USBH_ISOCHRONOUS) {
        ep->next = ticks + ep->interval;
    }
    ch32rv_usbh_isr_xfer(ep->daddr, address, result, length);
}

void wait_erdy(Ep *ep)
{
    ep->fc = true;
    ep->state = EP_WAIT_ERDY;
    ep->wait_ms = millis();
}

/* No handshake, or a deferred one (FAILED: a hub's port in a low-power
 * state, RM USB_STATUS.HRX_RES): the ERDY brings it back; FAIL_TRIES of
 * those in a row end the transfer. */
void failed(Ep *ep)
{
    if (++ep->tries >= FAIL_TRIES) {
        finish(ep, CH32RV_USBH_FAILED);
    } else {
        wait_erdy(ep);
    }
}

bool any_pending()
{
    for (const Ep &e : eps) {
        if (e.state == EP_PENDING) {
            return true;
        }
    }
    return false;
}

/* ------------------------------------------------------------ transactions */
void target(const Ep *ep)
{
    USB_CONTROL = (USB_CONTROL & ~USBSS_ADDR_MASK) | ((uint32_t)(ep->daddr & 0x7F) << 24);
    LINK_TX_ROUTE = ep->route;
}

void start_in(Ep *ep)
{
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;
    const uint16_t room = (uint16_t)(ep->len - ep->done);
    uint16_t want = (uint16_t)((room + ep->mps - 1u) / ep->mps);
    if (want == 0) {
        want = 1;                                        /* a zero-length read: one packet */
    }
    uint16_t cap = ep->window;
    if (iso && ep->per_interval > ep->in_interval) {
        const uint16_t left = (uint16_t)(ep->per_interval - ep->in_interval);
        cap = left < cap ? left : cap;
    }
    uint16_t npkt = want < cap ? want : cap;
    uint8_t *dst = ep->buf ? ep->buf + ep->done : nullptr;
    const bool direct = dst && ((uintptr_t)dst & 3u) == 0 && room >= (uint32_t)npkt * ep->mps;
    if (!direct) {
        npkt = 1;
    }
    ep->bounced = !direct;
    ep->armed_pkts = (uint8_t)npkt;
    UH_RX_DMA = (uint32_t)(uintptr_t)(direct ? dst : rx_buf);
    UH_RX_DMA_OFS = ep->mps;
    HOST_RX_NUMP = npkt;
    const uint32_t ctl = (iso ? UH_ISO : 0) | ((uint32_t)npkt << UH_NUMP_SHIFT) | ((uint32_t)ep->seq << UH_SEQ_SHIFT) |
                         ((uint32_t)(ep->ep_addr & 0x0F) << UH_EP_SHIFT);
    active_tx = false;
    UH_RX_CTRL = ctl;
    UH_RX_CTRL = ctl | UH_VALID;
}

void start_out(Ep *ep)
{
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;
    const uint16_t left = (uint16_t)(ep->len - ep->done);
    uint16_t npkt = left ? (uint16_t)((left + ep->mps - 1u) / ep->mps) : 1u;
    uint16_t cap = ep->window;
    if (iso && ep->per_interval > ep->in_interval) {
        const uint16_t l = (uint16_t)(ep->per_interval - ep->in_interval);
        cap = l < cap ? l : cap;
    }
    if (npkt > cap) {
        npkt = cap;
    }
    const uint8_t *src = ep->buf ? ep->buf + ep->done : nullptr;
    const bool direct = src && ((uintptr_t)src & 3u) == 0;
    if (!direct) {
        npkt = 1;
    }
    uint16_t bytes = (uint32_t)npkt * ep->mps < left ? (uint16_t)(npkt * ep->mps) : left;
    const uint16_t last = npkt > 1 ? (uint16_t)(bytes - (npkt - 1u) * ep->mps) : bytes;
    if (!direct && bytes) {
        memcpy(tx_buf, src, bytes);
    }
    ep->bounced = !direct;
    ep->armed = bytes;
    ep->armed_pkts = (uint8_t)npkt;
    UH_TX_DMA = (uint32_t)(uintptr_t)(direct ? src : tx_buf);
    UH_TX_DMA_OFS = ep->mps;
    HOST_TX_NUMP = npkt;
    /* Isochronous: the last packet of the service interval carries LPF. */
    const bool lpf = iso && (ep->done + bytes >= ep->len || ep->in_interval + npkt >= ep->per_interval);
    const uint32_t ctl = (iso ? UH_ISO : 0) | (lpf ? UH_TX_LPF : 0) | ((uint32_t)npkt << UH_NUMP_SHIFT) |
                         ((uint32_t)ep->seq << UH_SEQ_SHIFT) | ((uint32_t)(ep->ep_addr & 0x0F) << UH_EP_SHIFT) | last;
    active_tx = true;
    UH_TX_CTRL = ctl;
    UH_TX_CTRL = ctl | UH_VALID;
}

void start(int16_t index)
{
    Ep *ep = &eps[index];
    active = index;
    active_ms = millis();
    target(ep);
    if (ep->setup) {
        memcpy(tx_buf, ep->setup_pkt, 8);
        UH_TX_DMA = (uint32_t)(uintptr_t)tx_buf;
        HOST_TX_NUMP = 1;
        active_tx = true;
        ep->armed_pkts = 1;
        UH_TX_CTRL = UH_TX_SETUP | (1u << UH_NUMP_SHIFT) | 8u;
        UH_TX_CTRL = UH_TX_SETUP | (1u << UH_NUMP_SHIFT) | 8u | UH_VALID;
    } else if (ep->status) {
        HOST_TX_NUMP = 1;
        active_tx = true;
        ep->armed_pkts = 1;
        UH_TX_CTRL = UH_TX_STATUS | UH_VALID;            /* as the EVT, one write (enum.c:223) */
    } else if (ep->ep_addr & 0x80) {
        start_in(ep);
    } else {
        start_out(ep);
    }
}

void schedule()
{
    if (active >= 0 || !port_up) {
        return;
    }
    const uint32_t ls = LINK_STATUS & LINK_STATE_MASK;
    if (ls != LINK_STATE_U0) {
        /* Guess: the engine does not take the link out of U1 / U2 itself;
         * measure: whether a VALID in U1 goes out without this. U3 stays
         * until portSuspend(false). */
        if ((ls == LINK_STATE_U1 || ls == LINK_STATE_U2) && any_pending()) {
            LINK_CTRL |= LINK_TX_UX_EXIT;
        }
        return;
    }
    for (unsigned k = 0; k < EP_MAX; k++) {
        const unsigned i = (next_rr + k) % EP_MAX;
        Ep *ep = &eps[i];
        if (ep->state == EP_PENDING && (int32_t)(ticks - ep->due) >= 0) {
            next_rr = (uint8_t)((i + 1) % EP_MAX);
            start((int16_t)i);
            return;
        }
    }
}

void tx_done()
{
    const uint32_t s = USB_STATUS;
    const uint8_t res = (uint8_t)HTX_RES(s);
    const uint8_t nump = (uint8_t)HOST_ACK_NUMP(s);
    const uint8_t left = (uint8_t)(HOST_TX_NUMP & 0xFF);
    UH_TX_CTRL = 0;
    const int16_t index = active;
    active = -1;
    if (index < 0) {
        return;
    }
    Ep *ep = &eps[index];
    if (ep->state != EP_PENDING) {
        return;                                          /* aborted while on the wire */
    }
    const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;
    if (iso && !ep->setup) {
        /* No handshake on isochronous OUT: whatever the engine reports, the
         * packets went (guess: the RES field is meaningless here). */
        ep->done = (uint16_t)(ep->done + ep->armed);
        ep->in_interval = (uint8_t)(ep->in_interval + ep->armed_pkts);
        if (ep->done >= ep->len || ep->in_interval >= ep->per_interval) {
            finish(ep, CH32RV_USBH_OK);
        }
        return;
    }
    switch (res) {
    case RES_ACK:
        ep->tries = 0;
        if (nump == 0) {
            ep->fc = true;                               /* the next one waits for its ERDY */
        }
        if (ep->setup) {
            /* The data and status stages start at sequence 0 both ways (EP0's
             * are software's, RM UH_TX_CTRL.SEQ); a NumP of 0 holds both until
             * the ERDY (RM USB_STATUS.HOST_ACK_NUMP). */
            Ep *ep_in = find(ep->daddr, 0x80);
            ep->seq = 0;
            if (ep_in) {
                ep_in->seq = 0;
                ep_in->fc = ep->fc;
            }
            finish(ep, CH32RV_USBH_OK);
        } else if (ep->status) {
            finish(ep, CH32RV_USBH_OK);
        } else {
            /* Guess: HOST_TX_NUMP counts down per packet acknowledged (RM:
             * "reduced by 1 for each packet sent"). */
            const uint8_t sent = left <= ep->armed_pkts ? (uint8_t)(ep->armed_pkts - left) : ep->armed_pkts;
            const uint16_t bytes = sent >= ep->armed_pkts ? ep->armed : (uint16_t)(sent * ep->mps);
            ep->seq = (uint8_t)((ep->seq + (sent ? sent : 1u)) & 0x1Fu);
            ep->done = (uint16_t)(ep->done + bytes);
            if (ep->done >= ep->len) {
                finish(ep, CH32RV_USBH_OK);
            } else if (ep->fc) {
                wait_erdy(ep);
            }
        }
        break;
    case RES_NRDY:
        wait_erdy(ep);
        break;
    case RES_STALL:
        ep->seq = 0;
        ep->fc = false;
        finish(ep, CH32RV_USBH_STALLED);
        break;
    default:
        failed(ep);
        break;
    }
}

void rx_done()
{
    const uint32_t s = USB_STATUS;
    const uint8_t res = (uint8_t)HRX_RES(s);
    const uint32_t rx = UH_RX_CTRL;
    const uint16_t last = (uint16_t)(rx & UH_LEN_MASK);
    const uint16_t nump = HOST_RX_NUMP;
    const uint32_t hst = HOST_STATUS;
    const int16_t index = active;
    Ep *ep = index >= 0 ? &eps[index] : nullptr;
    const bool iso = ep && ep->type == CH32RV_USBH_ISOCHRONOUS;
    /* More than one packet armed: the EVT sets VALID again after each ACK
     * while HOST_RX_NUMP has packets left (udisk.c:434-436), the DMA address
     * moving on by UH_RX_DMA_OFS. Only with BURST_MAX > 1. */
    if (ep && ep->state == EP_PENDING && res == RES_ACK && ep->armed_pkts > 1 && (nump & 0xFF) != 0 &&
        last == ep->mps && !(hst & HST_RX_EOB_LPF)) {
        UH_RX_CTRL = (rx & ~(UH_ACT | UH_VALID));
        UH_RX_CTRL = (rx & ~(UH_ACT | UH_VALID)) | UH_VALID;
        return;
    }
    UH_RX_CTRL = 0;
    active = -1;
    if (!ep || ep->state != EP_PENDING) {
        return;
    }
    if (res == RES_ACK || iso) {
        if (iso && (hst & HST_RX_ISO_PKT_ERR)) {
            finish(ep, CH32RV_USBH_FAILED);
            return;
        }
        /* Guess: [15:8] counts the packets of this arm (RM: "the number of
         * DPP the host has accepted"); with one armed it is one. */
        uint8_t got = ep->armed_pkts > 1 ? (uint8_t)(nump >> 8) : 1u;
        if (got == 0) {
            got = 1;
        }
        const uint32_t bytes = (uint32_t)(got - 1u) * ep->mps + last;
        const uint16_t room = (uint16_t)(ep->len - ep->done);
        const uint16_t keep = bytes < room ? (uint16_t)bytes : room;
        if (ep->bounced && keep && ep->buf) {
            memcpy(ep->buf + ep->done, rx_buf, keep);
        }
        ep->done = (uint16_t)(ep->done + keep);
        ep->tries = 0;
        if (bytes > room) {
            finish(ep, CH32RV_USBH_FAILED);              /* babble: more than asked for */
            return;
        }
        const bool eob = (hst & HST_RX_EOB_LPF) != 0;
        if (iso) {
            ep->in_interval = (uint8_t)(ep->in_interval + got);
            if (eob || last < ep->mps || ep->done >= ep->len || ep->in_interval >= ep->per_interval) {
                finish(ep, CH32RV_USBH_OK);
            }
            return;
        }
        ep->seq = (uint8_t)((ep->seq + got) & 0x1Fu);
        if (eob) {
            ep->fc = true;                               /* EOB: in flow control (RM HOST_STATUS.EOB_LPF) */
        }
        if (last < ep->mps || ep->done >= ep->len) {
            finish(ep, CH32RV_USBH_OK);
        } else if (eob) {
            wait_erdy(ep);
        }
        return;
    }
    if (res == RES_NRDY) {
        wait_erdy(ep);
    } else if (res == RES_STALL) {
        ep->seq = 0;
        ep->fc = false;
        finish(ep, CH32RV_USBH_STALLED);
    } else {
        failed(ep);
    }
}

/* An ERDY for endpoint `n` (direction `in`): every endpoint of that number
 * and direction on any device may go again (the ERDY names no device). EP0's
 * ERDY frees both halves. */
void erdy(uint32_t hst)
{
    const uint8_t n = (uint8_t)HST_ERDY_EP(hst);
    const bool in = (hst & HST_ERDY_DIR) != 0;
    for (Ep &e : eps) {
        if (e.state == EP_FREE || (e.ep_addr & 0x0F) != n) {
            continue;
        }
        if (n != 0 && ((e.ep_addr & 0x80) != 0) != in) {
            continue;
        }
        e.fc = false;
        if (e.state == EP_WAIT_ERDY) {
            e.state = EP_PENDING;
            e.due = ticks;
        }
    }
}

/* From the ITP and from poll(): transactions that never ended, ERDYs that
 * never came, control transfers past their time. */
void timeouts()
{
    const uint32_t now = millis();
    if (active >= 0 && (uint32_t)(now - active_ms) > TRANSACTION_MS) {
        Ep *ep = &eps[active];
        stop_active();
        if (ep->state == EP_PENDING) {
            failed(ep);
        }
    }
    for (Ep &e : eps) {
        if (e.state == EP_WAIT_ERDY && (uint32_t)(now - e.wait_ms) >= ERDY_RETRY_MS) {
            e.state = EP_PENDING;                        /* the ERDY was missed, or will never come */
            e.due = ticks;
        }
        if (e.type == CH32RV_USBH_CONTROL && (e.state == EP_PENDING || e.state == EP_WAIT_ERDY) &&
            (uint32_t)(now - e.since_ms) > CONTROL_TIMEOUT_MS && (&e - eps) != active) {
            e.fc = false;
            finish(&e, CH32RV_USBH_TIMEOUT);
        }
    }
}

void port_down()
{
    port_up = false;
    stop_active();
}

/* ------------------------------------------------------------ link */
void link_lost()
{
    if (!link_up) {
        return;
    }
    link_up = false;
    port_down();
    if (!resetting) {
        ch32rv_usbh_isr_port(false);
    }
}

/* Host bring-up: the EVT's USBSSH_Init (usbss_host.c:69-98) with this
 * core's de-emphasis (CH32RV_USBSS_LINK_DEEMPH, measured on the device role
 * at 3.5 dB; the EVT writes 0x300) and the interrupts this driver uses. */
void link_start()
{
    USB_CONTROL |= USBSS_USB_CLR_ALL;
    USB_CONTROL &= ~USBSS_USB_CLR_ALL;
    UH_TX_CTRL = 0;
    UH_RX_CTRL = 0;
    LINK_CFG = LINK_RX_EQ_EN | CH32RV_USBSS_LINK_DEEMPH | LINK_DOWN_MODE | LINK_PHY_RESET;
    LINK_CTRL = LINK_PD_P2 | LINK_GO_DISABLED;
    LINK_CFG = LINK_RX_EQ_EN | CH32RV_USBSS_LINK_DEEMPH | LINK_DOWN_MODE | LINK_U2_RXDET | LINK_LTSSM_MODE;
    LINK_LPM_CR |= LINK_LPM_EN;
    for (uint32_t t0 = millis(); (LINK_STATUS & LINK_BUSY) && (uint32_t)(millis() - t0) < 10u;) {
    }
    LINK_CFG |= LINK_RX_TERM_EN;
    LINK_INT_FLAG = 0xFFFFFFFFu;
    LINK_INT_CTRL = LINK_IF_TX_LMP | LINK_IF_RX_LMP | LINK_IF_RX_LMP_TOUT | LINK_IF_STATE_CHG | LINK_IF_WARM_RST |
                    LINK_IF_TERM_PRES;
    LINK_CTRL = LINK_PD_P2;                              /* GO_DISABLED off: train */
    /* The EVT writes 122 here ("ITP collision 122 us"); the RM lists the
     * register as read-only with another meaning. Kept as the EVT has it. */
    LINK_ITP_PRE = 122;
    USB_CONTROL |= USBSS_FORCE_RST;
    USB_CONTROL = USBSS_HOST_MODE | USBSS_ITP_EN | USBSS_DMA_EN | USBSS_HP_PENDING | USBSS_UIE_TRANSFER |
                  USBSS_UHIE_ERDY | USBSS_UHIE_NOTIF | USBSS_UIE_ITP;
    UH_TX_CTRL = 0;
    USB_STATUS = USBSS_FLAGS;
    UH_TX_DMA = (uint32_t)(uintptr_t)tx_buf;
    UH_RX_DMA = (uint32_t)(uintptr_t)rx_buf;
    UEP_TX_EN = USBSS_UH_TX_EN;
    UEP_RX_EN = USBSS_UH_RX_EN;
    ch32rv_usbss_phy_setup();
    link_up = port_up = low_power = suspended = inactive = false;
    warm_phase = 0;
    retrain_fail = 0;
    release_at = 0;
}

void link_stop()
{
    USB_CONTROL = USBSS_FORCE_RST;
    LINK_CFG |= LINK_PHY_RESET | LINK_U3_RESET;
    delayMicroseconds(100);
    USB_CONTROL &= ~USBSS_FORCE_RST;
    LINK_CFG &= ~(LINK_PHY_RESET | LINK_U3_RESET);
}

void warm_start()
{
    LINK_CTRL = LINK_PD_P2;                              /* as the EVT before it (udisk.c:950) */
    LINK_CTRL = LINK_PD_P2 | LINK_TX_WARM_RST;
    warm_at = millis();
    warm_phase = 1;
}

/* The warm reset's steps, from portResetEnd() and poll(). */
void warm_step()
{
    if (warm_phase == 1 && (uint32_t)(millis() - warm_at) >= WARM_RESET_MS) {
        LINK_CTRL &= ~LINK_TX_WARM_RST;
        warm_at = millis();
        warm_phase = 2;
    } else if (warm_phase == 2 && (link_up || (uint32_t)(millis() - warm_at) >= RETRAIN_MS)) {
        warm_phase = 0;
        inactive = false;
    }
}

void phy_low_power(bool on)
{
    /* The device role's U1/U2/U3 PHY change (CH32RVUsbDcdUsbss.cpp). Guess:
     * the host's PHY needs the same; measure: the link 45 s into U1/U2
     * (the device role lost it there without this). */
    if (on && !low_power) {
        low_power = true;
        ch32rv_usbss_phy_cfg(0x12, (uint16_t)(0x67C8u & ~(1u << 9)));
    } else if (!on && low_power) {
        low_power = false;
        delayMicroseconds(100);
        ch32rv_usbss_phy_cfg(0x12, 0x67C8);
    }
}

void link_isr(void)
{
    const uint32_t f = LINK_INT_FLAG;
    const uint32_t state = LINK_STATUS & LINK_STATE_MASK;
    if (f & LINK_IF_STATE_CHG) {
        LINK_INT_FLAG = LINK_IF_STATE_CHG;
        switch (state) {
        case LINK_STATE_RXDET:                           /* the device went (usbss_host.c:122) */
            phy_low_power(false);
            suspended = false;
            link_lost();
            break;
        case LINK_STATE_DISABLE:
            /* The EVT clears GO_DISABLED at once (usbss_host.c:132); against
             * a far end that never trains that loops at hardware speed, so
             * after three in a row it waits 20 ms, as the device role does. */
            if (retrain_fail < 0xFF) {
                retrain_fail++;
            }
            if (retrain_fail >= 3) {
                release_at = (millis() + 20u) | 1u;
            } else {
                LINK_CTRL &= ~LINK_GO_DISABLED;
            }
            link_lost();
            break;
        case LINK_STATE_INACTIVE:                        /* only a warm reset leaves it (USB 3.2 7.5.2) */
            inactive = true;
            link_lost();
            break;
        case LINK_STATE_HOTRST:
            LINK_CTRL &= ~LINK_HOT_RESET;
            break;
        case LINK_STATE_U3:
            suspended = true;
            /* fall through */
        case LINK_STATE_U1:
        case LINK_STATE_U2:
            phy_low_power(true);
            break;
        case LINK_STATE_RECOVERY:
            phy_low_power(false);
            break;
        case LINK_STATE_U0:
            suspended = false;                           /* a resume, or a remote wakeup */
            schedule();
            break;
        default:
            break;
        }
    } else if (f & LINK_IF_TERM_PRES) {
        LINK_INT_FLAG = LINK_IF_TERM_PRES;
    } else if (f & LINK_IF_RX_LMP_TOUT) {
        /* Port Configuration failed: train again (usbss_host.c:152-157). */
        LINK_INT_FLAG = LINK_IF_RX_LMP_TOUT;
        LINK_CTRL |= LINK_GO_DISABLED;
        LINK_CTRL |= LINK_GO_RX_DET;
    } else if (f & LINK_IF_TX_LMP) {
        LINK_INT_FLAG = LINK_IF_TX_LMP;
        LINK_LMP_TX_DATA0 = LMP_LINK_SPEED | LMP_PORT_CAP;
        LINK_LMP_TX_DATA1 = LMP_DOWN_STREAM | LMP_NUM_HP_BUF;
        LINK_LMP_TX_DATA2 = 0;
    } else if (f & LINK_IF_RX_LMP) {
        LINK_INT_FLAG = LINK_IF_RX_LMP;
        const uint32_t rx0 = LINK_LMP_RX_DATA0;
        if ((rx0 & LMP_SUBTYPE_MASK) == LMP_PORT_CAP) {
            LINK_LMP_TX_DATA0 = LMP_LINK_SPEED | LMP_PORT_CFG;
            LINK_LMP_TX_DATA1 = 0;
            LINK_LMP_TX_DATA2 = 0;
        } else if ((rx0 & LMP_SUBTYPE_MASK) == LMP_PORT_CFG_RES) {
            /* The link is up at SuperSpeed: the device is in its Default
             * state, no port reset needed (the EVT sends SET_ADDRESS next). */
            LINK_LMP_PORT_CAP |= LINK_LMP_TX_CAP_VLD;
            retrain_fail = 0;
            inactive = false;
            if (!link_up) {
                link_up = true;
                if (!resetting) {
                    ch32rv_usbh_isr_port(true);
                }
            }
        }
    } else if (f & LINK_IF_WARM_RST) {
        /* The EVT's host starts over here (usbss_host.c:205-213), code the
         * device role's; a host sends warm resets, it does not get them.
         * Cleared only. */
        LINK_INT_FLAG = LINK_IF_WARM_RST;
    } else {
        LINK_INT_FLAG = f;
    }
}

/* A Device Notification TP (RM 27.2.5.10). Function Wake (type 1 in the low
 * nibble's upper half, USB 3.2 8.5.6.1) after a remote wakeup: the link is
 * already back in U0 by then, nothing to do. Other notifications (latency
 * tolerance, bus interval adjustment) are not acted on. */
void notification()
{
    (void)TP_RX_DATA0;
}

void usb_isr(void)
{
    const uint32_t st = USB_STATUS;
    /* The completion first: it reads the handshake from USB_STATUS, whose
     * RES fields are read-write - clearing the flags with a word write puts
     * 0 (ACK) there (guess; the EVT reads RES before it clears anything). */
    if (active >= 0) {
        if (active_tx && (UH_TX_CTRL & UH_ACT)) {
            tx_done();
        } else if (!active_tx && (UH_RX_CTRL & UH_ACT)) {
            rx_done();
        }
    }
    USB_STATUS = st & USBSS_FLAGS;                       /* W1C: only what the first read saw */
    if (st & USBSS_UHIF_ERDY) {
        erdy(HOST_STATUS);
    }
    if (st & USBSS_UHIF_NOTIF) {
        notification();
    }
    if (st & USBSS_UIF_ITP) {
        ticks++;
        timeouts();
        /* A new service interval for the isochronous endpoints. */
        for (Ep &e : eps) {
            if (e.type == CH32RV_USBH_ISOCHRONOUS && e.state != EP_PENDING) {
                e.in_interval = 0;
            }
        }
    }
    schedule();
}

/* ------------------------------------------------------------ the table */
bool hcd_open(void)
{
    if (ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_USB] && ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_USB] != usb_isr) {
        return false;                                    /* the device stack has the controller */
    }
    if (!ch32rv_usbss_clock_on()) {
        return false;
    }
    memset(eps, 0, sizeof eps);
    memset(routes, 0, sizeof routes);
    active = -1;
    ticks = 0;
    resetting = warm_next = false;
    opened_ms = millis();
    ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_USB] = usb_isr;
    ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_LINK] = link_isr;
    link_start();
    irq_on = true;
    ch32rv_irq_enable(CH32RV_IRQN_USBSS);
    ch32rv_irq_enable(CH32RV_IRQN_USBSS_LINK);
    return true;
}

void hcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBSS_LINK);
    ch32rv_irq_disable(CH32RV_IRQN_USBSS);
    irq_on = false;
    link_stop();
    memset(eps, 0, sizeof eps);
    active = -1;
    link_up = port_up = false;
    ch32rv_usbss_clock_off();
    if (ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_USB] == usb_isr) {
        ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_USB] = nullptr;
        ch32rv_usbss_irq_hook[CH32RV_USBSS_HOOK_LINK] = nullptr;
    }
}

bool hcd_connected(void)
{
    return link_up || warm_phase != 0;
}

/* The first reset after an attach is nothing: a SuperSpeed device that
 * reached U0 is in its Default state. After portDisable() (the stack's
 * reenumerate()) it is a warm reset, the EVT's way back for a stuck device
 * (udisk.c:950-955). */
void hcd_port_reset(void)
{
    bool was;
    lock(&was);
    port_down();
    USB_CONTROL &= ~USBSS_ADDR_MASK;
    LINK_TX_ROUTE = 0;
    if (warm_next && link_up) {
        warm_next = false;
        resetting = true;
        link_up = false;                                 /* wait for PORT_CFG_RES again */
        warm_start();
    }
    unlock(was);
}

uint8_t hcd_port_reset_end(void)
{
    bool was;
    lock(&was);
    if (warm_phase) {
        warm_step();
        if (warm_phase) {
            unlock(was);
            return CH32RV_USBH_RESET_PENDING;
        }
    }
    resetting = false;
    warm_next = false;
    port_up = link_up;
    schedule();
    unlock(was);
    return CH32RV_USBH_SUPER;
}

void hcd_port_disable(void)
{
    bool was;
    lock(&was);
    port_down();
    warm_next = true;
    unlock(was);
}

void hcd_device_close(uint8_t daddr)
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

Ep *open_ep(uint8_t daddr, uint8_t ep_addr, const CH32RVUsbHcdEp &e)
{
    Ep *ep = find(daddr, ep_addr);
    if (!ep) {
        for (Ep &x : eps) {
            if (x.state == EP_FREE) {
                ep = &x;
                break;
            }
        }
    }
    if (!ep) {
        return nullptr;
    }
    if (ep - eps == active) {
        stop_active();
    }
    memset(ep, 0, sizeof *ep);
    ep->state = EP_IDLE;
    ep->daddr = daddr;
    ep->ep_addr = ep_addr;
    ep->type = (ep_addr & 0x0F) == 0 ? (uint8_t)CH32RV_USBH_CONTROL : e.type;
    ep->mps = e.mps ? e.mps : EP0_MPS;
    ep->route = route_of(daddr);
    ep->interval = interval_ticks(ep->type, e.interval);
    const uint8_t burst = (uint8_t)((e.maxBurst > 15 ? 15 : e.maxBurst) + 1u);
    ep->window = ep->type == CH32RV_USBH_CONTROL ? 1u : (burst < BURST_MAX ? burst : BURST_MAX);
    if (ep->type == CH32RV_USBH_ISOCHRONOUS) {
        /* Mult (bits 1:0, at most 2): that many more bursts per interval. */
        const uint8_t mult = (uint8_t)((e.attributes & 3u) > 2u ? 2u : (e.attributes & 3u));
        ep->per_interval = (uint8_t)((mult + 1u) * burst);
    } else if (ep->type == CH32RV_USBH_INTERRUPT) {
        ep->per_interval = burst;
    } else if (ep->type == CH32RV_USBH_BULK) {
        ep->streams = (uint8_t)(e.attributes & 0x1Fu);
    }
    ep->next = ticks;
    return ep;
}

bool hcd_ep_open(uint8_t daddr, const CH32RVUsbHcdEp &e)
{
    if (e.mps > PACKET_MAX) {
        return false;
    }
    bool was;
    lock(&was);
    bool ok;
    if ((e.address & 0x0F) == 0) {
        ok = open_ep(daddr, 0x00, e) && open_ep(daddr, 0x80, e);
    } else {
        ok = open_ep(daddr, e.address, e) != nullptr;
    }
    unlock(was);
    return ok;
}

void hcd_ep_close(uint8_t daddr, uint8_t address)
{
    bool was;
    lock(&was);
    Ep *ep = find(daddr, address);
    if (ep) {
        if ((int16_t)(ep - eps) == active) {
            stop_active();
        }
        ep->state = EP_FREE;
        schedule();
    }
    unlock(was);
}

void queue(Ep *ep)
{
    ep->tries = 0;
    ep->since_ms = millis();
    ep->in_interval = 0;
    ep->due = ticks;
    if ((ep->type == CH32RV_USBH_INTERRUPT || ep->type == CH32RV_USBH_ISOCHRONOUS) &&
        (int32_t)(ep->next - ticks) > 0) {
        ep->due = ep->next;
    }
    if (ep->status) {
        ep->due = ticks + STATUS_DELAY_TICKS;
    }
    if (ep->fc) {
        ep->state = EP_WAIT_ERDY;                        /* flow controlled since the last one */
        ep->wait_ms = ep->since_ms;
    } else {
        ep->state = EP_PENDING;
    }
}

bool hcd_xfer(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length)
{
    bool was;
    lock(&was);
    Ep *ep = find(daddr, address);
    const bool ok = ep && ep->state == EP_IDLE;
    if (ok) {
        ep->buf = buffer;
        ep->len = length;
        ep->done = 0;
        ep->setup = false;
        /* EP0 with nothing to move is the status stage: a data stage of 0
         * bytes is never started (the stack goes to the status at once). */
        ep->status = (address & 0x0F) == 0 && length == 0;
        queue(ep);
        schedule();
    }
    unlock(was);
    return ok;
}

bool hcd_setup(uint8_t daddr, const uint8_t packet[8])
{
    bool was;
    lock(&was);
    Ep *ep = find(daddr, 0x00);
    const bool ok = ep != nullptr;
    if (ok) {
        Ep *ep_in = find(daddr, 0x80);
        if ((int16_t)(ep - eps) == active || (ep_in && (int16_t)(ep_in - eps) == active)) {
            stop_active();
        }
        if (ep_in) {
            ep_in->state = EP_IDLE;
            ep_in->fc = false;
        }
        memcpy(ep->setup_pkt, packet, 8);
        ep->buf = nullptr;
        ep->len = 8;
        ep->done = 0;
        ep->setup = true;
        ep->status = false;
        ep->fc = false;                                  /* a SETUP ends the flow control (RM USB_STATUS) */
        queue(ep);
        schedule();
    }
    unlock(was);
    return ok;
}

void hcd_abort(uint8_t daddr, uint8_t address)
{
    bool was;
    lock(&was);
    Ep *list[2] = {find(daddr, address), (address & 0x0F) == 0 ? find(daddr, (uint8_t)(address ^ 0x80)) : nullptr};
    for (Ep *ep : list) {
        if (!ep) {
            continue;
        }
        if ((int16_t)(ep - eps) == active) {
            stop_active();
        }
        ep->state = EP_IDLE;
        ep->setup = ep->status = false;
    }
    schedule();
    unlock(was);
}

/* CLEAR_FEATURE(ENDPOINT_HALT): the sequence starts over (USB 3.2 8.12.1.2). */
void hcd_reset_toggle(uint8_t daddr, uint8_t address)
{
    bool was;
    lock(&was);
    Ep *ep = find(daddr, address);
    if (ep) {
        ep->seq = 0;
        ep->fc = false;
    }
    unlock(was);
}

/* SuperSpeed has no frames; milliseconds since open(), as the frame counter
 * of the other drivers counts them. */
uint32_t hcd_frame(void)
{
    return (uint32_t)(millis() - opened_ms);
}

/* SystemInit() run again by a probe takes HSE, the USBSS PLL and PIPE / UTMI
 * away (see the device role's clock_check): put them back and train again.
 * Also the timers that need no interrupt: the retrain hold, the warm reset,
 * timeouts while no ITP comes (U1 / U2 / U3), and a sleeping link with work
 * due. */
void hcd_poll(void)
{
    if (!ch32rv_usbss_clock_ok() && ch32rv_usbss_clock_on()) {
        bool was;
        lock(&was);
        const bool was_up = link_up;
        link_start();
        if (was_up && !resetting) {
            ch32rv_usbh_isr_port(false);
        }
        unlock(was);
        return;
    }
    bool was;
    lock(&was);
    const uint32_t at = release_at;
    if (at && (int32_t)(millis() - at) >= 0) {
        release_at = 0;
        LINK_CTRL &= ~LINK_GO_DISABLED;
    }
    if (inactive && warm_phase == 0) {
        warm_start();                                    /* SS.Inactive: the only way out */
    }
    if (warm_phase && !resetting) {
        warm_step();
    }
    timeouts();
    schedule();
    unlock(was);
}

void hcd_device_at(uint8_t daddr, uint32_t route)
{
    bool was;
    lock(&was);
    Route *slot = nullptr;
    for (Route &r : routes) {
        if (r.used && r.daddr == daddr) {
            slot = &r;
            break;
        }
        if (!r.used && !slot) {
            slot = &r;
        }
    }
    if (slot) {
        slot->used = true;
        slot->daddr = daddr;
        slot->route = route & 0xFFFFFu;
    }
    for (Ep &e : eps) {
        if (e.state != EP_FREE && e.daddr == daddr) {
            e.route = route & 0xFFFFFu;
        }
    }
    unlock(was);
}

/* Guess: in DOWN_MODE the ALLOW bits accept the device's LGO_U1 / LGO_U2
 * (in the device role they let it send them, CH32RVUsbDcdUsbss.cpp). With
 * them clear the link answers LXU (refuses), which is safe. */
void hcd_link_allow(bool u1, bool u2)
{
    uint32_t c = LINK_CFG & ~(LINK_U1_ALLOW | LINK_U2_ALLOW);
    c |= (u1 ? LINK_U1_ALLOW : 0) | (u2 ? LINK_U2_ALLOW : 0);
    LINK_CFG = c;
}

/* Suspend: LGO_U3 (RM LINK_CTRL.TX_LGO_U3; the device may not refuse).
 * The RM then asks for PD_MODE P3; not done (guess: the PHY 0x12 change is
 * what the link needs, as in the device role; measure: the current in U3 and
 * the wakeup with and without P3). Resume: U3 wakeup LFPS (TX_UX_EXIT). */
bool hcd_port_suspend(bool on)
{
    if (!link_up) {
        return false;
    }
    bool was;
    lock(&was);
    const uint32_t ls = LINK_STATUS & LINK_STATE_MASK;
    bool ok = true;
    if (on) {
        stop_active();
        if (ls == LINK_STATE_U0 || ls == LINK_STATE_U1 || ls == LINK_STATE_U2) {
            LINK_CTRL |= LINK_TX_LGO_U3;
        } else if (ls != LINK_STATE_U3) {
            ok = false;
        }
    } else if (ls == LINK_STATE_U3 || suspended) {
        LINK_CTRL |= LINK_TX_UX_EXIT;
    }
    unlock(was);
    return ok;
}

}  // namespace

/* The classes' buffers are full speed's 64 bytes (H417 / H416's default port
 * is USBFS); a SuperSpeed endpoint's (up to 1024) come from the heap. */
static void *buffer_alloc(unsigned bytes) { return malloc(bytes); }
static void buffer_free(void *buffer) { free(buffer); }

const CH32RVUsbHcd ch32rv_usb_hcd_usbss = {
    "USBSS host", PACKET_MAX,
    hcd_open, hcd_close, hcd_connected, hcd_port_reset, hcd_port_reset_end, hcd_port_disable,
    hcd_ep_open, hcd_ep_close, hcd_device_close, hcd_xfer, hcd_setup, hcd_abort, hcd_reset_toggle, hcd_frame,
    hcd_poll, hcd_device_at, hcd_link_allow, hcd_port_suspend, nullptr, nullptr, buffer_alloc, buffer_free,
};

#endif
