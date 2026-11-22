/* Host controller driver for the WCH USBFS block in host mode
 * (CH32X035/X033, the OTG_FS of V20x, V30x, V205, L103 / M103, M030 and
 * H41x, and the older USB IP of CH32V103 - "USBHD" in its EVT).
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

#include "internal/ch32rv_irq.h"
#include "internal/ch32rv_registers.h"
#include "internal/ch32rv_usbfs_irq.h"
#include "internal/CH32RVUsbHcd.h"

/* Which block, where, and how its registers are laid out. */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
#define CH32RV_HCD_USBFS_BASE   0x40023400u
#define CH32RV_HCD_USBFS_LAYOUT 1
#elif defined(CH32RV_SERIES_CH32V103)
#define CH32RV_HCD_USBFS_BASE   0x40023400u
#define CH32RV_HCD_USBFS_LAYOUT 3
#else
#include "internal/ch32rv_usbfs_otg.h"
#ifdef CH32RV_USBFS_OTG_BASE      /* V20x, V30x, V205, L103 / M103, M030, H41x */
#define CH32RV_HCD_USBFS_BASE   CH32RV_USBFS_OTG_BASE
#define CH32RV_HCD_USBFS_LAYOUT 2
#endif
#endif

#ifdef CH32RV_HCD_USBFS_BASE

/* ------------------------------------------------------------- the port
 * Clock, PHY and pins for host mode per series, before the driver opens
 * (the table's prepare / release / poll):
 *   port_open()          false when the clock cannot feed the PHY or the part has no USBFS
 *   port_close()
 *   port_has_host()      whether this die has the host half
 *   port_poll()          from the task: put back a clock taken away under it */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)
/* CH32X035 / X033: USBFS on PC16 (D-) / PC17 (D+). Host mode wants the 15 kOhm
 * pull-downs (the controller's, UH_PD_DIS clear) and no pull-up, the PHY on
 * the 3.3 V supply, and the pads handed to it (EVT USBFS_Host_Init). */
#define X03X_AFIO_UDM_PUE_MASK 0x00000003u
#define X03X_AFIO_UDP_PUE_MASK 0x0000000Cu
#define X03X_AFIO_USB_PHY_V33  0x00000040u
#define X03X_AFIO_USB_IOEN     0x00000080u
#define USBFS_BASE_CTRL        (*(volatile uint8_t *)0x40023400u)

static bool port_open(void)
{
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    pinMode(PC16, INPUT_PULLDOWN);
    pinMode(PC17, INPUT_PULLDOWN);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK)) |
                       X03X_AFIO_USB_PHY_V33 | X03X_AFIO_USB_IOEN;
    return true;
}

static void port_close(void)
{
    CH32RV_AFIO_CTLR &= ~X03X_AFIO_USB_IOEN;
}

static void port_poll(void) {}

#elif defined(CH32RV_SERIES_CH32V103)
/* CH32V103: the older USB IP on PA11 (D-) / PA12 (D+), the same as its device
 * driver (CH32RVUsbDcdUsbV103.cpp) sets up: 48 MHz from the PLL (RCC_CFGR0
 * USBPRE bit 22: 0 = PLL / 1.5 at 72 MHz, 1 = PLL / 1 at 48 MHz), the pads
 * handed to the PHY (EXTEN_CTR USBFS_IO_EN, bit 2) on the 3.3 V supply
 * (USB_5V_SEL, bit 3, clear) - EVT USBFS_RCC_Init / USBFS_Host_Init. The
 * host's 15 kOhm pull-downs are the controller's (UHOST_CTRL.PD_DIS clear). */
#define V103_EXTEN_CTR        CH32RV_REG32(0x40023800u)
#define V103_EXTEN_USBFS_IO_EN 0x00000004u
#define V103_EXTEN_USB_5V_SEL  0x00000008u
#define USBFS_BASE_CTRL        (*(volatile uint8_t *)0x40023400u)

static bool port_open(void)
{
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 72000000
    CH32RV_RCC_CFGR0 &= ~(1u << 22);
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
    CH32RV_RCC_CFGR0 |= 1u << 22;
#else
    return false;                                        /* no 48 MHz from this clock */
#endif
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
    V103_EXTEN_CTR = (V103_EXTEN_CTR & ~V103_EXTEN_USB_5V_SEL) | V103_EXTEN_USBFS_IO_EN;
    ch32rv_clock_enable(USBFS);
    return true;
}

static void port_close(void)
{
    V103_EXTEN_CTR &= ~V103_EXTEN_USBFS_IO_EN;
    ch32rv_clock_disable(USBFS);
}

static void port_poll(void) {}

#else /* the USBOTG_FS layout: V20x, V30x, V205, L103 / M103, M030, H41x */

#define USBFS_BASE_CTRL        (*(volatile uint8_t *)CH32RV_USBFS_OTG_BASE)

/* The 48 MHz and the pads as the device driver makes them; the host's 15 kOhm
 * pull-downs are the controller's own (HOST_CTRL), set by the driver. */
static bool port_open(void)
{
#if defined(CH32RV_PART_CH32V203F6P6) || defined(CH32RV_PART_CH32V203G6U6)
    return false;        /* no USBFS on the part */
#else
    return ch32rv_usbfs_otg_clock_on();
#endif
}

static void port_close(void)
{
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
    ch32rv_clock_disable(OTG_FS);
#else
    ch32rv_clock_disable(USBFS);
#endif
}

/* SystemInit() run again by a debug probe turns the extra PLLs off (the
 * device driver's dcd_poll() does the same). */
static void port_poll(void)
{
    if (!ch32rv_usbfs_otg_clock_ok()) {
        ch32rv_usbfs_otg_clock_on();
    }
}

#endif

/* Whether this die has the host half. CH32X035R8T6 / C8T6 / G8U6 / G8R6 do;
 * F8U6, F7P6, D8U6 and CH32X033F8P6 are device-only (datasheet model table),
 * and there BASE_CTRL.HOST_MODE does not stick - the EVT polls for it, and on
 * an F8U6 it reads 0 however long it is given (measured 2026-10-15). So the
 * bit is the test, which also covers a board built for "ANY". */
static bool port_has_host(void)
{
    USBFS_BASE_CTRL = 0x80;
    const uint32_t t0 = micros();
    while (!(USBFS_BASE_CTRL & 0x80) && (uint32_t)(micros() - t0) < 2000u) {
    }
    const bool has = (USBFS_BASE_CTRL & 0x80) != 0;
    USBFS_BASE_CTRL = 0;
    return has;
}


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
 *      toggles in bit 2;
 *   3  CH32V103 (EVT ch32v10x_usb.h): the host registers where layout 2 has
 *      them (HOST_SETUP 0x36 as one byte, EP_PID 0x38, RX_CTRL 0x3A, TX_LEN
 *      0x3C 16 bit, TX_CTRL 0x3E) with layout 1's bits (SOF_EN 0x40,
 *      PRE_PID_EN 0x80, R_TOG 0x80, T_TOG 0x40; R_RES is bit 2) and 16-bit DMA
 *      addresses (its 20 KB of SRAM is in the low half). */

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
#elif CH32RV_HCD_USBFS_LAYOUT == 3
#define SET_RX_DMA(a) (R16(0x18) = (uint16_t)(uintptr_t)(a))
#define SET_TX_DMA(a) (R16(0x1C) = (uint16_t)(uintptr_t)(a))
#define HOST_SETUP  R8(0x36)
#define HOST_EP_PID R8(0x38)
#define HOST_RX_CTRL R8(0x3A)
#define HOST_TX_LEN R16(0x3C)
#define HOST_TX_CTRL R8(0x3E)
#define UH_PRE_PID_EN  0x80
#define UH_SOF_EN      0x40
#define UH_R_TOG       0x80
#define UH_T_TOG       0x40
#define UH_R_RES       0x04
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
#define UC_SYS_CTRL_MASK 0x30   /* host mode: 00 normal, 01 SE0, 10 J, 11 K */
#define UC_SYS_FORCE_SE0 0x10
#define UC_SYS_FORCE_K   0x30
#define UC_INT_BUSY    0x08
#define UC_RESET_SIE   0x04
#define UC_CLR_ALL     0x02
#define UC_DMA_EN      0x01

#define UH_PD_DIS      0x80
#define UH_DP_PIN      0x20
#define UH_DM_PIN      0x10
#define UH_LOW_SPEED   0x04
#define UH_BUS_RESET   0x02
#define UH_PORT_EN     0x01

#define UIE_HST_SOF    0x08
#define UIE_SUSPEND    0x04
#define UIE_TRANSFER   0x02
#define UIE_DETECT     0x01

#define UIF_HST_SOF    0x08
#define UIF_SUSPEND    0x04
#define UIF_TRANSFER   0x02
#define UIF_DETECT     0x01

#define UMS_DM_LEVEL   0x02
#define UMS_DEV_ATTACH 0x01

#define UIS_TOG_OK     0x40
#define UIS_H_RES_MASK 0x0F

#define UH_EP_TX_EN    0x40
#define UH_EP_RX_EN    0x08
#ifndef UH_R_RES
#define UH_R_RES       0x01
#endif
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
/* After a resume no transaction goes out for this long: the device's resume
 * recovery (USB 2.0 7.1.7.7, TRSMRCY 10 ms). */
#define RESUME_RECOVERY_US 10000u   /* SOF only, nothing to the device */
#define RESUME_SIGNAL_US 20000u     /* the host's resume K, TDRSMDN */

static uint8_t rx_buf[PACKET_MAX] CH32RV_DMA_MEM;
static uint8_t tx_buf[PACKET_MAX] CH32RV_DMA_MEM;

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
/* Bus power (ch32rv_usb_hcd_usbfs_power): a suspended port is down
 * (port_up false: nothing starts) with suspend_irq set, which takes the
 * suspend / resume interrupt. Only that pointer and its test are in a sketch
 * that never suspends. */
static void (*volatile suspend_irq)(void);

static void schedule(void);

/* Take the transaction off the wire. The engine finishes a token it has
 * started, so wait (bounded: a full-speed transaction is well under 100 us)
 * for its end before the next start() clears the flag - a late UIF_TRANSFER
 * would otherwise be read as the next transaction's. */
static void transfer_done(void);

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

/* For a suspend: the transaction on the wire is let end and its result kept. An IN that brought data was
 * acknowledged and the device has moved its toggle on - thrown away, that packet was lost (a CDC echo after a
 * suspend never came back, L103 / V205, 2026-11-19). Called with the port already down, so transfer_done()'s
 * schedule() starts nothing. */
static void finish_active(void)
{
    if (active < 0) {
        return;
    }
    HOST_EP_PID = 0;
    for (volatile uint32_t d = 0; d < 20000u && !(INT_FG & UIF_TRANSFER); d++) {
    }
    if (INT_FG & UIF_TRANSFER) {
        transfer_done();
        return;
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
    suspend_irq = nullptr;
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

static bool hcd_ep_open(uint8_t daddr, const CH32RVUsbHcdEp &e)
{
    const uint8_t address = e.address, type = e.type, speed = e.speed;
    const uint16_t mps = e.mps;
    uint8_t interval = e.interval;
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

/* The K state on the root port: D- high and D+ low at full speed, the other
 * way at low speed (a device's remote wakeup, USB 2.0 7.1.7.7). */
static bool line_k(void)
{
    const uint8_t lines = HOST_CTRL & (UH_DP_PIN | UH_DM_PIN);
    return lines == (root_speed == CH32RV_USBH_LOW ? UH_DP_PIN : UH_DM_PIN);
}

/* Suspend: no SOF, the bus idles and the device suspends after 3 ms (USB 2.0
 * 7.1.7.6). Resume: the block drives the K state itself in host mode with
 * BASE_CTRL's SYS_CTRL 11 (01 SE0, 10 J - CH32V103 EVT ch32v10x_usb.h,
 * CH32V20x RM table 23-2; the same bits in the X03x and USBOTG_FS layouts):
 * K for 20 ms (TDRSMDN), the low-speed EOP (SE0 for two low-speed bits),
 * then SOF again and nothing else for the resume recovery. SOF alone does not
 * wake an RP2040 (TinyUSB), nor do the low-speed bit or the pads as GPIO (the
 * PHY keeps them) - measured on V205 2026-11-18. While suspended, the suspend
 * / resume interrupt with the bus in K is the device's remote wakeup; it also
 * comes as the bus goes idle, in J, which is not. */
/* Suspended: the interrupt with the bus in K is the device's remote wakeup
 * (it also comes as the bus goes idle, in J). Once the port is up again - a
 * reset after a detach while suspended - it is not this one's any more. */
static void on_suspend_irq(void)
{
    INT_FG = UIF_SUSPEND;
    if (port_up) {
        INT_EN &= (uint8_t)~UIE_SUSPEND;
        suspend_irq = nullptr;
    } else if (line_k()) {
        INT_EN &= (uint8_t)~UIE_SUSPEND;
        ch32rv_usbh_isr_wakeup();
    }
}

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
            finish_active();
            HOST_SETUP &= ~UH_SOF_EN;
            suspend_irq = on_suspend_irq;
            INT_FG = UIF_SUSPEND;
            INT_EN |= UIE_SUSPEND;
        }
    } else if (!on && suspended) {
        INT_EN &= (uint8_t)~UIE_SUSPEND;
        suspend_irq = nullptr;
        BASE_CTRL = (uint8_t)((BASE_CTRL & ~UC_SYS_CTRL_MASK) | UC_SYS_FORCE_K);
        unlock(was);
        for (const uint32_t t0 = micros(); (uint32_t)(micros() - t0) < RESUME_SIGNAL_US;) {
        }
        lock(&was);
        /* The EOP: SE0, then the bus back to the block, with interrupts off and nothing between - the two
         * writes' own gap is the SE0 (a full-speed device takes 160 ns for an EOP). Anything longer read as a
         * bus reset: delayMicroseconds(2) on L103, and a 4-turn loop at 48 MHz on X035, left the peer
         * unaddressed - no answer to any token after the resume (2026-11-19); with none, L103, V205 and X035
         * all resume. */
        const uint32_t irq = ch32rv_irq_save();
        BASE_CTRL = (uint8_t)((BASE_CTRL & ~UC_SYS_CTRL_MASK) | UC_SYS_FORCE_SE0);
        BASE_CTRL &= (uint8_t)~UC_SYS_CTRL_MASK;
        ch32rv_irq_restore(irq);
        const bool attached = (MIS_ST & UMS_DEV_ATTACH) != 0;
        if (attached) {
            HOST_SETUP |= UH_SOF_EN;
        }
        unlock(was);
        for (const uint32_t t0 = micros(); (uint32_t)(micros() - t0) < RESUME_RECOVERY_US;) {
        }
        lock(&was);
        if (attached && (HOST_CTRL & UH_PORT_EN)) {   /* not detached meanwhile */
            port_up = true;
            schedule();
        }
    }
    unlock(was);
    return ok;
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
    if ((flags & UIF_SUSPEND) && suspend_irq) {
        suspend_irq();
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

/* Clock and pads first, then whether the die has the host half (a device-only
 * X035 is Unsupported, not Busy). */
static bool hcd_prepare(void)
{
    if (!port_open()) {
        return false;
    }
    if (!port_has_host()) {
        port_close();
        return false;
    }
    return true;
}

const CH32RVUsbHcd ch32rv_usb_hcd_usbfs = {
    "USBFS host", PACKET_MAX,
    hcd_open, hcd_close, hcd_connected, hcd_port_reset, hcd_port_reset_end, hcd_port_disable,
    hcd_ep_open, hcd_ep_close, hcd_device_close, hcd_xfer, hcd_setup, hcd_abort, hcd_reset_toggle, hcd_frame,
    port_poll, nullptr, nullptr, hcd_prepare, port_close, nullptr, nullptr,
};
extern const CH32RVUsbHcdPower ch32rv_usb_hcd_usbfs_power = {&ch32rv_usb_hcd_usbfs, hcd_port_suspend};

#endif
