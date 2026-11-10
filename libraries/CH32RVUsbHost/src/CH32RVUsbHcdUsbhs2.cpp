/* Host controller driver for the second-generation USBHS (USB 2.0 high
 * speed) in host mode: CH32V407 / V467 (two: USBHS1, USBHS2), CH32H41x,
 * CH32V205, CH32X305 / X315. Where they sit and their clocks:
 * internal/ch32rv_usbhs2.h; the host registers are at the controller's base +
 * 0x100 (EVT USBHSH_TypeDef, the same in all four EVTs; RM chapter 22.2.2 of
 * CH32V407).
 *
 * The block has one transaction engine, like USBFS: R32_UH_CONTROL takes the
 * token (PID, endpoint, the data PID to send) with HOST_ACTION, the engine
 * runs it, UHIF_TRANSFER says it is over and INT_ST holds the PID that came
 * back (ACK / NAK / STALL / NYET, DATA0 / DATA1 for an IN, 0 for none). So
 * this is CH32RVUsbHcdUsbfs.cpp's scheduler on other registers:
 *
 *   - every open endpoint can have a transfer pending; one transaction at a
 *     time goes on the wire, round robin;
 *   - the clock is the SOF interrupt: a microframe (125 us) when the root port
 *     runs at high speed, a frame (1 ms) otherwise. A NAK puts bulk and
 *     control back for the next tick, interrupt endpoints for their polling
 *     interval (2^(bInterval-1) microframes for a high-speed one);
 *   - an IN with the wrong data PID is the device repeating a packet whose
 *     ACK it lost: dropped and asked for again (the EVT's USBHSH_Transact);
 *   - NYET after an OUT is an accepted packet (the device has no room for the
 *     next one yet); the next goes as OUT again rather than PING, which a
 *     device NAKs until it has room;
 *   - packets go through one DMA buffer each way, a whole packet of the
 *     largest size (1024, isochronous) - RX_DMA keeps 17 address bits, so the
 *     receive buffer has to sit in the first 128 KB of SRAM (open() checks).
 *
 * Root port: the controller times the bus reset and the high-speed chirp
 * itself (PORT_CTRL.SET_PORT_RESET, PORT_CHG.RESET when done); the speed is
 * PORT_ST's HS / LS bits after it. SOF stays enabled throughout (open()). A low-speed device behind a full-speed hub
 * gets the PRE packet (CONTROL.PRE_PID_EN) when the root port is full speed.
 * Not here yet: split transactions (full- and low-speed devices behind a
 * high-speed hub; the block has SPLIT_DATA for them) and PING. */
#include "internal/ch32rv_usbhs2.h"

#ifdef CH32RV_USBHS2_UNITS

#include <Arduino.h>
#include <string.h>

#include "internal/CH32RVUsbHcd.h"

namespace {

/* Host registers, from the controller's base + 0x100. */
enum : uint32_t {
    H_CFG = 0x100, H_INT_EN = 0x102, H_DEV_AD = 0x103, H_CONTROL = 0x104, H_INT_FLAG = 0x108, H_INT_ST = 0x109,
    H_MIS_ST = 0x10A, H_SPLIT = 0x110, H_FRAME = 0x114, H_TX_LEN = 0x118, H_RX_LEN = 0x11C, H_RX_MAX_LEN = 0x120,
    H_RX_DMA = 0x124, H_TX_DMA = 0x128, H_PORT_CTRL = 0x12C, H_PORT_CFG = 0x130, H_PORT_INT_EN = 0x132,
    H_PORT_ST = 0x134, H_PORT_CHG = 0x136,
};

constexpr uint8_t CFG_FORCE_FS = 0x40, CFG_SOF_EN = 0x20, CFG_DMA_EN = 0x10, CFG_PHY_SUSPENDM = 0x08,
                  CFG_CLR_ALL = 0x04, CFG_RST_SIE = 0x02, CFG_RST_LINK = 0x01;
constexpr uint8_t IF_FIFO_OVER = 0x80, IF_TX_HALT = 0x40, IF_SOF_ACT = 0x20, IF_TRANSFER = 0x10;
constexpr uint32_t CTL_RX_NO_RES = 0x800000, CTL_TX_NO_RES = 0x400000, CTL_PRE_PID_EN = 0x080000,
                   CTL_HOST_ACTION = 0x010000, CTL_TOG1 = 0x0100;
constexpr uint32_t PORT_SET_RESET = 0x01, PORT_CLR_EN = 0x10;
constexpr uint8_t PORT_CFG_PD_EN = 0x80, PORT_CFG_HOST_EN = 0x01;
constexpr uint16_t PST_HS = 0x0400, PST_LS = 0x0200, PST_RESET = 0x0010, PST_CONNECT = 0x0001;
constexpr uint8_t PCHG_RESET = 0x10, PCHG_EN = 0x02, PCHG_CONNECT = 0x01;

constexpr uint8_t PID_OUT = 0x1, PID_IN = 0x9, PID_SETUP = 0xD, PID_ACK = 0x2, PID_NYET = 0x6, PID_NAK = 0xA,
                  PID_STALL = 0xE, PID_DATA0 = 0x3, PID_DATA1 = 0xB;

constexpr uint16_t PACKET_MAX = 1024;
constexpr uint8_t NO_RESPONSE_TRIES = 3;
constexpr uint32_t CONTROL_TIMEOUT_MS = 5000;      /* USB 2.0 9.2.6.4 */
constexpr unsigned EP_MAX = 32;

enum : uint8_t { EP_FREE = 0, EP_IDLE, EP_PENDING };

struct Ep {
    uint8_t state, daddr, ep_addr, type, speed, toggle, setup, tries;
    uint16_t mps, len, done;
    uint16_t interval;     /* ticks between interrupt / isochronous polls */
    uint8_t *buf;
    uint32_t due;          /* tick before which it is not tried */
    uint32_t since;        /* tick the transfer was queued */
    uint8_t setup_pkt[8];
};

/* The two DMA buffers of all controllers: one role and one transaction at a
 * time per controller, but V407 / V467 can run a host on each. */
template <uint32_t BASE, uint8_t UNIT, uint32_t IRQN>
struct Hcd {
    static volatile uint8_t &r8(uint32_t o) { return *(volatile uint8_t *)(BASE + o); }
    static volatile uint16_t &r16(uint32_t o) { return *(volatile uint16_t *)(BASE + o); }
    static volatile uint32_t &r32(uint32_t o) { return *(volatile uint32_t *)(BASE + o); }

    static uint8_t rx_buf[PACKET_MAX] __attribute__((aligned(4)));
    static uint8_t tx_buf[PACKET_MAX] __attribute__((aligned(4)));
    static Ep eps[EP_MAX];
    static volatile int16_t active;
    static volatile uint32_t active_since;
    static uint8_t next_rr;
    static volatile uint32_t ticks;              /* SOF interrupts since the port came up */
    static volatile bool irq_on, port_up, resetting;
    static uint8_t root_speed;

    /* A tick is a microframe on a high-speed root port, a frame otherwise. */
    static uint32_t ticks_per_ms() { return root_speed == CH32RV_USBH_HIGH ? 8u : 1u; }

    static void lock(bool *was)
    {
        *was = irq_on;
        if (*was) {
            ch32rv_irq_disable(IRQN);
        }
    }

    static void unlock(bool was)
    {
        if (was) {
            ch32rv_irq_enable(IRQN);
        }
    }

    /* Take the transaction off the wire: HOST_ACTION clears itself when the
     * engine is done, so wait (bounded; a transaction is microseconds) for
     * that before the next one starts. */
    static void stop_active()
    {
        if (active < 0) {
            return;
        }
        for (volatile uint32_t d = 0; d < 20000u && (r32(H_CONTROL) & CTL_HOST_ACTION); d++) {
        }
        r8(H_INT_FLAG) = IF_TRANSFER;
        r8(H_INT_EN) = (uint8_t)(r8(H_INT_EN) & ~IF_TRANSFER);
        active = -1;
    }

    static Ep *find(uint8_t daddr, uint8_t ep_addr)
    {
        for (unsigned i = 0; i < EP_MAX; i++) {
            if (eps[i].state != EP_FREE && eps[i].daddr == daddr && eps[i].ep_addr == ep_addr) {
                return &eps[i];
            }
        }
        return nullptr;
    }

    /* bInterval as ticks: high speed 2^(b-1) microframes (interrupt and
     * isochronous); full and low speed b frames (interrupt) or 2^(b-1) frames
     * (isochronous). */
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

    static Ep *open_ep(uint8_t daddr, uint8_t ep_addr, uint16_t mps, uint8_t type, uint8_t interval, uint8_t speed)
    {
        Ep *ep = find(daddr, ep_addr);
        if (!ep) {
            for (unsigned i = 0; i < EP_MAX; i++) {
                if (eps[i].state == EP_FREE) {
                    ep = &eps[i];
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
        ep->speed = speed;
        ep->daddr = daddr;
        ep->ep_addr = ep_addr;
        ep->mps = mps ? mps : 8;
        ep->type = type;
        ep->interval = interval_ticks(type, interval, speed);
        return ep;
    }

    static void finish(Ep *ep, uint8_t result)
    {
        ep->state = EP_IDLE;
        const uint8_t address = ep->setup ? 0x00 : ep->ep_addr;
        const uint16_t length = ep->setup ? 8u : ep->done;
        ep->setup = 0;
        ch32rv_usbh_isr_xfer(ep->daddr, address, result, length);
    }

    static void start(int16_t index)
    {
        Ep *ep = &eps[index];
        active = index;
        active_since = ticks;
        r8(H_DEV_AD) = (uint8_t)(ep->daddr & 0x7F);
        const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;
        uint32_t ctl = CTL_HOST_ACTION | ((uint32_t)(ep->ep_addr & 0x0F) << 4);
        if (ep->speed == CH32RV_USBH_LOW && root_speed == CH32RV_USBH_FULL) {
            ctl |= CTL_PRE_PID_EN;                       /* behind a full-speed hub */
        }
        if (ep->setup) {
            memcpy(tx_buf, ep->setup_pkt, 8);
            r32(H_TX_LEN) = 8;
            ctl |= PID_SETUP;                            /* DATA0 */
        } else if (ep->ep_addr & 0x80) {
            r32(H_RX_MAX_LEN) = ep->mps;
            ctl |= PID_IN | (ep->toggle ? CTL_TOG1 : 0) | (iso ? CTL_RX_NO_RES : 0);
        } else {
            const uint16_t left = (uint16_t)(ep->len - ep->done);
            const uint16_t n = left < ep->mps ? left : ep->mps;
            if (n) {
                memcpy(tx_buf, ep->buf + ep->done, n);
            }
            r32(H_TX_LEN) = n;
            ctl |= PID_OUT | (ep->toggle ? CTL_TOG1 : 0) | (iso ? CTL_TX_NO_RES : 0);
        }
        r8(H_INT_FLAG) = IF_TRANSFER | IF_FIFO_OVER | IF_TX_HALT;
        r8(H_INT_EN) = (uint8_t)(r8(H_INT_EN) | IF_TRANSFER);
        r32(H_CONTROL) = ctl;
    }

    static void schedule()
    {
        if (active >= 0 || !port_up) {
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

    static void transfer_done(uint8_t flags)
    {
        r8(H_INT_FLAG) = IF_TRANSFER | IF_FIFO_OVER | IF_TX_HALT;
        r8(H_INT_EN) = (uint8_t)(r8(H_INT_EN) & ~IF_TRANSFER);
        const uint8_t res = r8(H_INT_ST) & 0x0F;
        const int16_t index = active;
        active = -1;
        if (index < 0) {
            return;
        }
        Ep *ep = &eps[index];
        if (ep->state != EP_PENDING) {
            return;                                      /* aborted while on the wire */
        }
        const bool in = !ep->setup && (ep->ep_addr & 0x80);
        const bool iso = ep->type == CH32RV_USBH_ISOCHRONOUS;

        if (flags & IF_FIFO_OVER) {                      /* more than RX_MAX_LEN: babble */
            finish(ep, CH32RV_USBH_FAILED);
        } else if (in && (res == PID_DATA0 || res == PID_DATA1)) {
            if (!iso && (res == PID_DATA1) != (ep->toggle != 0)) {
                ep->tries = 0;                           /* a repeat (lost ACK): drop it, ask again */
            } else {
                ep->tries = 0;
                ep->toggle ^= 1u;
                const uint16_t got = (uint16_t)(r32(H_RX_LEN) & 0x7FF);
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
                Ep *ep_in = find(ep->daddr, 0x80);
                ep->toggle = 1;
                if (ep_in) {
                    ep_in->toggle = 1;
                }
                finish(ep, CH32RV_USBH_OK);
            } else {
                ep->done = (uint16_t)(ep->done + (r32(H_TX_LEN) & 0x7FF));
                if (iso || ep->done >= ep->len) {
                    finish(ep, CH32RV_USBH_OK);
                }
            }
        } else if (res == PID_NAK) {
            ep->tries = 0;
            ep->due = ticks + (ep->type == CH32RV_USBH_INTERRUPT ? ep->interval : 1u);
            if (ep->type == CH32RV_USBH_CONTROL &&
                (uint32_t)(ticks - ep->since) > CONTROL_TIMEOUT_MS * ticks_per_ms()) {
                finish(ep, CH32RV_USBH_TIMEOUT);
            }
        } else if (res == PID_STALL) {
            ep->toggle = 0;
            finish(ep, CH32RV_USBH_STALLED);
        } else if (iso && res == 0) {
            finish(ep, CH32RV_USBH_OK);                  /* nothing this interval */
        } else if (res == 0 && ++ep->tries < NO_RESPONSE_TRIES) {
            ep->due = ticks + 1u;                        /* no handshake: try again */
        } else {
            finish(ep, CH32RV_USBH_FAILED);
        }
        schedule();
    }

    static void port_down()
    {
        port_up = false;
        stop_active();
        r8(H_INT_EN) = 0;
    }

    static void isr()
    {
        const uint8_t chg = r8(H_PORT_CHG);
        if (chg) {
            r8(H_PORT_CHG) = chg;
            if ((chg & PCHG_CONNECT) && !resetting) {
                if (r16(H_PORT_ST) & PST_CONNECT) {
                    ch32rv_usbh_isr_port(true);
                } else {
                    port_down();
                    ch32rv_usbh_isr_port(false);
                }
            }
        }
        const uint8_t flags = r8(H_INT_FLAG);
        if ((flags & IF_TRANSFER) && (r8(H_INT_EN) & IF_TRANSFER)) {
            transfer_done(flags);
        }
        if (flags & IF_SOF_ACT) {
            r8(H_INT_FLAG) = IF_SOF_ACT;
            ticks++;
            /* A transaction takes microseconds; one still open after two
             * frames lost its completion: no handshake. */
            if (active >= 0 && (uint32_t)(ticks - active_since) >= 2u * ticks_per_ms()) {
                Ep *ep = &eps[active];
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

    /* ------------------------------------------------------------ the table */
    static bool open()
    {
        if (ch32rv_usbhs2_irq_hook[UNIT - 1] && ch32rv_usbhs2_irq_hook[UNIT - 1] != isr) {
            return false;                                /* the device stack has the controller */
        }
        if ((uint32_t)((uintptr_t)rx_buf - 0x20000000u) + PACKET_MAX > 0x20000u) {
            return false;                                /* RX_DMA reaches the first 128 KB only */
        }
        if (!ch32rv_usbhs2_clock_on(UNIT)) {
            return false;
        }
        ch32rv_usbhs2_pads_on(UNIT);
        memset(eps, 0, sizeof eps);
        active = -1;
        port_up = resetting = false;
        ticks = 0;
        root_speed = CH32RV_USBH_FULL;
        r8(H_CFG) = CFG_CLR_ALL | CFG_RST_SIE | CFG_RST_LINK;
        for (volatile int d = 0; d < 2000; d++) {
        }
        r8(H_CFG) = CFG_RST_LINK | CFG_PHY_SUSPENDM;
        r32(H_TX_DMA) = (uint32_t)(uintptr_t)tx_buf;
        r32(H_RX_DMA) = (uint32_t)(uintptr_t)rx_buf;
        r32(H_RX_MAX_LEN) = PACKET_MAX;
        r8(H_PORT_CFG) = PORT_CFG_PD_EN | PORT_CFG_HOST_EN;
        r32(H_FRAME) |= 0x01000000u;                     /* SOF_CNT_EN */
        /* SOF on from here, as the EVT does, and never off: started only after
         * the port reset, the port dropped its connection within the same
         * millisecond (PORT_ST 0x0403 -> 0x0000 -> 0x0001, enable and high
         * speed gone), every time, with an ESP32-P4 at high speed (2026-11-11). */
        r8(H_CFG) = CFG_SOF_EN | CFG_DMA_EN | CFG_PHY_SUSPENDM;
        r8(H_INT_FLAG) = 0xFF;
        r8(H_PORT_CHG) = 0xFF;
        r8(H_INT_EN) = 0;
        r8(H_PORT_INT_EN) = PCHG_CONNECT;
        ch32rv_usbhs2_irq_hook[UNIT - 1] = isr;
        irq_on = true;
        ch32rv_irq_enable(IRQN);
        return true;
    }

    static void close()
    {
        ch32rv_irq_disable(IRQN);
        irq_on = false;
        r8(H_INT_EN) = 0;
        r8(H_PORT_INT_EN) = 0;
        r8(H_CFG) = CFG_CLR_ALL | CFG_RST_SIE | CFG_RST_LINK;
        r8(H_PORT_CFG) = 0;
        memset(eps, 0, sizeof eps);
        active = -1;
        port_up = false;
        if (ch32rv_usbhs2_irq_hook[UNIT - 1] == isr) {
            ch32rv_usbhs2_irq_hook[UNIT - 1] = nullptr;
        }
    }

    static bool connected() { return (r16(H_PORT_ST) & PST_CONNECT) != 0; }

    static void port_reset()
    {
        bool was;
        lock(&was);
        port_down();
        resetting = true;
        r8(H_DEV_AD) = 0;
        r8(H_PORT_CHG) = 0xFF;
        r32(H_PORT_CTRL) = PORT_SET_RESET;
        unlock(was);
    }

    /* The controller ends the reset (and the chirp) itself: wait for that,
     * bounded, then read the speed and start SOF. */
    static uint8_t port_reset_end()
    {
        for (uint32_t t0 = millis(); (r16(H_PORT_ST) & PST_RESET) && (uint32_t)(millis() - t0) < 100u;) {
        }
        bool was;
        lock(&was);
        const uint16_t st = r16(H_PORT_ST);
        root_speed = (st & PST_HS) ? CH32RV_USBH_HIGH : (st & PST_LS) ? CH32RV_USBH_LOW : CH32RV_USBH_FULL;
        r8(H_PORT_CHG) = 0xFF;                           /* the reset itself is not an attach */
        resetting = false;
        r8(H_INT_FLAG) = 0xFF;
        r8(H_INT_EN) = IF_SOF_ACT;
        port_up = true;
        schedule();
        unlock(was);
        return root_speed;
    }

    static void port_disable()
    {
        bool was;
        lock(&was);
        port_down();
        r32(H_PORT_CTRL) = PORT_CLR_EN;
        unlock(was);
    }

    static void device_close(uint8_t daddr)
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

    static bool ep_open(uint8_t daddr, uint8_t address, uint8_t type, uint16_t mps, uint8_t interval, uint8_t speed)
    {
        if (mps > PACKET_MAX) {
            return false;
        }
        bool was;
        lock(&was);
        bool ok;
        if ((address & 0x0F) == 0) {
            ok = open_ep(daddr, 0x00, mps, CH32RV_USBH_CONTROL, 0, speed) &&
                 open_ep(daddr, 0x80, mps, CH32RV_USBH_CONTROL, 0, speed);
        } else {
            ok = open_ep(daddr, address, mps, type, interval, speed) != nullptr;
        }
        unlock(was);
        return ok;
    }

    static void ep_close(uint8_t daddr, uint8_t address)
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

    static bool xfer(uint8_t daddr, uint8_t address, uint8_t *buffer, uint16_t length)
    {
        bool was;
        lock(&was);
        Ep *ep = find(daddr, address);
        const bool ok = ep && ep->state == EP_IDLE;
        if (ok) {
            ep->buf = buffer;
            ep->len = length;
            ep->done = 0;
            ep->tries = 0;
            ep->setup = 0;
            ep->due = ticks;
            ep->since = ticks;
            ep->state = EP_PENDING;
            schedule();
        }
        unlock(was);
        return ok;
    }

    static bool setup(uint8_t daddr, const uint8_t packet[8])
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
            }
            memcpy(ep->setup_pkt, packet, 8);
            ep->buf = nullptr;
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

    static void abort(uint8_t daddr, uint8_t address)
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
            ep->setup = 0;
        }
        schedule();
        unlock(was);
    }

    static void reset_toggle(uint8_t daddr, uint8_t address)
    {
        Ep *ep = find(daddr, address);
        if (ep) {
            ep->toggle = 0;
        }
    }

    static uint32_t frame() { return ticks / ticks_per_ms(); }

    /* A probe's attach makes SystemInit() run again, which takes the PHY
     * clock away: put it back from the task. */
    static void poll()
    {
        if (!ch32rv_usbhs2_clock_ok(UNIT)) {
            ch32rv_usbhs2_clock_on(UNIT);
        }
    }

    static constexpr CH32RVUsbHcd table(const char *name)
    {
        return {name, PACKET_MAX, open, close, connected, port_reset, port_reset_end, port_disable,
                ep_open, ep_close, device_close, xfer, setup, abort, reset_toggle, frame, poll};
    }
};

template <uint32_t B, uint8_t U, uint32_t I> uint8_t Hcd<B, U, I>::rx_buf[PACKET_MAX] __attribute__((aligned(4)));
template <uint32_t B, uint8_t U, uint32_t I> uint8_t Hcd<B, U, I>::tx_buf[PACKET_MAX] __attribute__((aligned(4)));
template <uint32_t B, uint8_t U, uint32_t I> Ep Hcd<B, U, I>::eps[EP_MAX];
template <uint32_t B, uint8_t U, uint32_t I> volatile int16_t Hcd<B, U, I>::active = -1;
template <uint32_t B, uint8_t U, uint32_t I> volatile uint32_t Hcd<B, U, I>::active_since;
template <uint32_t B, uint8_t U, uint32_t I> uint8_t Hcd<B, U, I>::next_rr;
template <uint32_t B, uint8_t U, uint32_t I> volatile uint32_t Hcd<B, U, I>::ticks;
template <uint32_t B, uint8_t U, uint32_t I> volatile bool Hcd<B, U, I>::irq_on;
template <uint32_t B, uint8_t U, uint32_t I> volatile bool Hcd<B, U, I>::port_up;
template <uint32_t B, uint8_t U, uint32_t I> volatile bool Hcd<B, U, I>::resetting;
template <uint32_t B, uint8_t U, uint32_t I> uint8_t Hcd<B, U, I>::root_speed;

}  // namespace

const CH32RVUsbHcd ch32rv_usb_hcd_usbhs2 = Hcd<CH32RV_USBHS2_BASE1, 1, CH32RV_USBHS2_IRQN1>::table("USBHS host");
#if CH32RV_USBHS2_UNITS == 2
const CH32RVUsbHcd ch32rv_usb_hcd_usbhs2_b = Hcd<CH32RV_USBHS2_BASE2, 2, CH32RV_USBHS2_IRQN2>::table("USBHS2 host");
#endif

#endif
