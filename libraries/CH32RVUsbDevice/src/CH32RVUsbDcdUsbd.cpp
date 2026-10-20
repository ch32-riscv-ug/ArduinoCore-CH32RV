/* USBD of CH32V203 / V208: the ST "fsdev" full-speed device (as on STM32F103),
 * device role (CH32RVUsbDcd.h).
 *
 * From the EVT's USBLIB (ST's usb_lib): registers at 0x40005C00 - EPnR at
 * 4n (n 0-7), CNTR 0x40, ISTR 0x44, FNR 0x48, DADDR 0x4C, BTABLE 0x50 - and
 * 512 bytes of packet memory at 0x40006000, one 16-bit word per 32-bit slot
 * (byte offset b lives at 0x40006000 + 2b), shared with CAN1: the two cannot
 * run together. EPnR: CTR_RX 15 and CTR_TX 7 cleared by writing 0, DTOG_RX
 * 14 / DTOG_TX 6 and STAT_RX 13:12 / STAT_TX 5:4 toggled by writing 1, SETUP
 * 11, type 10:9, EP_KIND 8, address 3:0. The D+ pull-up is EXTEN_CTR
 * (0x40023800) USBD_PU_EN, bit 1. Interrupts: USB_LP_CAN1_RX0 (and
 * USB_HP_CAN1_TX for isochronous / double-buffered endpoints), shared with
 * CAN1 by name. 48 MHz from the PLL through USBPRE, as for USBFS; PA11 / PA12.
 * Build only. */
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)

#include <Arduino.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "CH32RVUsbDcd.h"

#define USBD_BASE 0x40005C00u
#define PMA_BASE  0x40006000u
#define PMA_SIZE  512u
#define EPR(n)    (*(volatile uint16_t *)(USBD_BASE + 4u * (n)))
#define CNTR      (*(volatile uint16_t *)(USBD_BASE + 0x40u))
#define ISTR      (*(volatile uint16_t *)(USBD_BASE + 0x44u))
#define DADDR     (*(volatile uint16_t *)(USBD_BASE + 0x4Cu))
#define BTABLE    (*(volatile uint16_t *)(USBD_BASE + 0x50u))
#define EXTEN_CTR (*(volatile uint32_t *)0x40023800u)
#define EXTEN_USBD_PU_EN 0x00000002u

#define CTR_RX   0x8000u
#define DTOG_RX  0x4000u
#define STAT_RX  0x3000u
#define SETUP_B  0x0800u
#define T_FIELD  0x0600u
#define KIND     0x0100u
#define CTR_TX   0x0080u
#define DTOG_TX  0x0040u
#define STAT_TX  0x0030u
#define EA       0x000Fu
#define KEEP     (CTR_RX | SETUP_B | T_FIELD | KIND | CTR_TX | EA)
#define S_STALL  1u
#define S_NAK    2u
#define S_VALID  3u

#define I_CTR    0x8000u
#define I_SUSP   0x0800u
#define I_RESET  0x0400u
#define I_WKUP   0x1000u
#define I_DIR    0x0010u
#define C_FRES   0x0001u
#define C_PDWN   0x0002u
#define C_FSUSP  0x0008u

/* Buffer descriptor table at PMA offset 0, 8 bytes per endpoint. */
static inline volatile uint16_t &pma16(uint16_t off) { return *(volatile uint16_t *)(PMA_BASE + 2u * off); }
static inline volatile uint16_t &addr_tx(uint8_t ep) { return pma16((uint16_t)(ep * 8u + 0u)); }
static inline volatile uint16_t &count_tx(uint8_t ep) { return pma16((uint16_t)(ep * 8u + 2u)); }
static inline volatile uint16_t &addr_rx(uint8_t ep) { return pma16((uint16_t)(ep * 8u + 4u)); }
static inline volatile uint16_t &count_rx(uint8_t ep) { return pma16((uint16_t)(ep * 8u + 6u)); }

struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint16_t last;
    uint16_t pma;                /* its buffer in packet memory */
    bool active, iso;
};
static Xfer xin[8], xout[8];
static uint16_t pma_next;        /* the packet memory handed out so far */
static uint8_t ep_type[8];       /* EPnR type bits per endpoint number */
static bool suspended;

/* ------------------------------------------------------------ helpers */
static void set_stat_tx(uint8_t ep, uint16_t stat)
{
    const uint16_t v = EPR(ep);
    EPR(ep) = (uint16_t)(((v & (KEEP | STAT_TX)) ^ (uint16_t)(stat << 4)) | CTR_RX | CTR_TX);
}

static void set_stat_rx(uint8_t ep, uint16_t stat)
{
    const uint16_t v = EPR(ep);
    EPR(ep) = (uint16_t)(((v & (KEEP | STAT_RX)) ^ (uint16_t)(stat << 12)) | CTR_RX | CTR_TX);
}

static void clear_dtog(uint8_t ep, bool in)
{
    const uint16_t v = EPR(ep);
    const uint16_t bit = in ? DTOG_TX : DTOG_RX;
    if (v & bit) {
        EPR(ep) = (uint16_t)((v & KEEP) | bit | CTR_RX | CTR_TX);
    }
}

static void clear_ctr(uint8_t ep, uint16_t which)
{
    EPR(ep) = (uint16_t)((EPR(ep) & KEEP & ~which) | ((CTR_RX | CTR_TX) & ~which));
}

static void set_rx_count(uint8_t ep, uint16_t size)
{
    count_rx(ep) = size > 62 ? (uint16_t)(0x8000u | (((size + 31u) / 32u - 1u) << 10))
                             : (uint16_t)(((size + 1u) / 2u) << 10);
}

static void pma_write(uint16_t off, const uint8_t *src, uint16_t n)
{
    for (uint16_t i = 0; i < n; i += 2) {
        const uint16_t lo = src[i];
        const uint16_t hi = (uint16_t)(i + 1 < n ? src[i + 1] : 0);
        pma16((uint16_t)(off + i)) = (uint16_t)(lo | (hi << 8));
    }
}

static void pma_read(uint16_t off, uint8_t *dst, uint16_t n)
{
    for (uint16_t i = 0; i < n; i += 2) {
        const uint16_t w = pma16((uint16_t)(off + i));
        dst[i] = (uint8_t)w;
        if (i + 1 < n) {
            dst[i + 1] = (uint8_t)(w >> 8);
        }
    }
}

static bool pma_alloc(uint16_t size, uint16_t *off)
{
    size = (uint16_t)((size + 1u) & ~1u);
    if (size > 62) {
        size = (uint16_t)((size + 31u) & ~31u);          /* RX counts in 32-byte blocks above 62 */
    }
    if (pma_next + size > PMA_SIZE) {
        return false;
    }
    *off = pma_next;
    pma_next = (uint16_t)(pma_next + size);
    return true;
}

static void send(uint8_t ep)
{
    Xfer &x = xin[ep];
    const uint16_t left = (uint16_t)(x.len - x.done);
    const uint16_t n = left < x.mps ? left : x.mps;
    if (n) {
        pma_write(x.pma, x.buf + x.done, n);
    }
    count_tx(ep) = n;
    x.last = n;
    set_stat_tx(ep, S_VALID);
}

/* Endpoint 0 and the bus reset: the table, EP0's two buffers, address 0. */
static void reset_device(void)
{
    BTABLE = 0;
    pma_next = 64;                                       /* after the 8-entry table */
    for (uint8_t ep = 0; ep < 8; ep++) {
        EPR(ep) = (uint16_t)(EPR(ep) & (DTOG_RX | STAT_RX | DTOG_TX | STAT_TX));   /* toggles to 0, type 0 */
        xin[ep].active = xout[ep].active = false;
        ep_type[ep] = 0;
    }
    pma_alloc(64, &xout[0].pma);
    pma_alloc(64, &xin[0].pma);
    xin[0].mps = xout[0].mps = 64;
    addr_tx(0) = xin[0].pma;
    count_tx(0) = 0;
    addr_rx(0) = xout[0].pma;
    set_rx_count(0, 64);
    EPR(0) = (uint16_t)(0x0200u | CTR_RX | CTR_TX);     /* control, address 0 */
    set_stat_rx(0, S_VALID);
    set_stat_tx(0, S_NAK);
    DADDR = 0x80;                                        /* EF */
}

/* ------------------------------------------------------------ clocks */
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000
#define USBPRE 2u
#endif

static bool clock_on(void)
{
#ifdef USBPRE
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << 22)) | (USBPRE << 22);
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
    ch32rv_clock_enable(USB);
    return true;
#else
    return false;                                        /* no 48 MHz from this clock */
#endif
}

static void dcd_poll(void)
{
#ifdef USBPRE
    if (((CH32RV_RCC_CFGR0 >> 22) & 3u) != USBPRE) {
        clock_on();
    }
#endif
}

/* ------------------------------------------------------------ table */
static bool dcd_open(void)
{
    if (!clock_on()) {
        return false;
    }
    CNTR = C_FRES;                                       /* power up, hold reset */
    delayMicroseconds(1);
    CNTR = 0;
    ISTR = 0;
    memset(xin, 0, sizeof xin);
    memset(xout, 0, sizeof xout);
    suspended = false;
    reset_device();
    CNTR = I_CTR | I_RESET | I_SUSP | I_WKUP;
    ch32rv_irq_enable(CH32RV_IRQN_USB_LP_CAN1_RX0);
    ch32rv_irq_enable(CH32RV_IRQN_USB_HP_CAN1_TX);
    return true;
}

static void dcd_connect(bool on)
{
    EXTEN_CTR = on ? (EXTEN_CTR | EXTEN_USBD_PU_EN) : (EXTEN_CTR & ~EXTEN_USBD_PU_EN);
}

static void dcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USB_LP_CAN1_RX0);
    ch32rv_irq_disable(CH32RV_IRQN_USB_HP_CAN1_TX);
    dcd_connect(false);
    CNTR = C_FRES | C_PDWN;
    ch32rv_clock_disable(USB);
}

static CH32RVUsbSpeed dcd_speed(void)
{
    return CH32RVUsbSpeed::Full;
}

static void dcd_set_address(uint8_t address)
{
    DADDR = (uint16_t)(0x80u | address);
}

static bool dcd_ep_open(uint8_t address, uint8_t type, uint16_t mps)
{
    const uint8_t ep = address & 0x0F;
    const bool in = address & 0x80;
    if (ep == 0 || ep > 7 || mps == 0 || mps > 64) {
        return false;
    }
    Xfer &x = in ? xin[ep] : xout[ep];
    if (!pma_alloc(mps, &x.pma)) {
        return false;                                    /* 512 bytes of packet memory in all */
    }
    x.mps = mps;
    x.active = false;
    x.iso = type == CH32RV_USB_XFER_ISOCHRONOUS;
    static const uint16_t type_bits[4] = {0x0200u, 0x0400u, 0x0000u, 0x0600u};   /* control, iso, bulk, interrupt */
    noInterrupts();
    const uint16_t v = EPR(ep);
    EPR(ep) = (uint16_t)((v & (CTR_RX | CTR_TX | KIND)) | type_bits[type & 3] | ep | CTR_RX | CTR_TX);
    if (in) {
        addr_tx(ep) = x.pma;
        count_tx(ep) = 0;
        clear_dtog(ep, true);
        set_stat_tx(ep, S_NAK);
    } else {
        addr_rx(ep) = x.pma;
        set_rx_count(ep, mps);
        clear_dtog(ep, false);
        set_stat_rx(ep, S_NAK);
    }
    interrupts();
    return true;
}

static void dcd_ep_close(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep > 7) {
        return;
    }
    noInterrupts();
    if (address & 0x80) {
        xin[ep].active = false;
        set_stat_tx(ep, 0);
    } else {
        xout[ep].active = false;
        set_stat_rx(ep, 0);
    }
    interrupts();
}

static void dcd_ep_close_all(void)
{
    noInterrupts();
    for (uint8_t ep = 1; ep < 8; ep++) {
        xin[ep].active = xout[ep].active = false;
        set_stat_tx(ep, 0);
        set_stat_rx(ep, 0);
    }
    pma_next = 64 + 128;                                 /* keep the table and EP0's buffers */
    interrupts();
}

static bool dcd_xfer(uint8_t address, uint8_t *buffer, uint16_t length)
{
    const uint8_t ep = address & 0x0F;
    if (ep > 7) {
        return false;
    }
    noInterrupts();
    Xfer &x = (address & 0x80) ? xin[ep] : xout[ep];
    x.buf = buffer;
    x.len = length;
    x.done = 0;
    x.active = true;
    if (address & 0x80) {
        send(ep);
    } else {
        set_stat_rx(ep, S_VALID);
    }
    interrupts();
    return true;
}

static void dcd_abort(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep > 7) {
        return;
    }
    noInterrupts();
    if (address & 0x80) {
        xin[ep].active = false;
        if (((EPR(ep) & STAT_TX) >> 4) != S_STALL) {
            set_stat_tx(ep, S_NAK);
        }
    } else {
        xout[ep].active = false;
        if (((EPR(ep) & STAT_RX) >> 12) != S_STALL) {
            set_stat_rx(ep, S_NAK);
        }
    }
    interrupts();
}

static void dcd_stall(uint8_t address, bool on)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep > 7) {
        return;
    }
    const bool in = address & 0x80;
    noInterrupts();
    if (on) {
        if (in) {
            set_stat_tx(ep, S_STALL);
        } else {
            set_stat_rx(ep, S_STALL);
        }
    } else {
        clear_dtog(ep, in);                              /* DATA0 again */
        if (in) {
            if (xin[ep].active) {
                send(ep);
            } else {
                set_stat_tx(ep, S_NAK);
            }
        } else {
            set_stat_rx(ep, xout[ep].active ? S_VALID : S_NAK);
        }
    }
    interrupts();
}

static void dcd_ep0_stall(void)
{
    noInterrupts();
    set_stat_tx(0, S_STALL);
    set_stat_rx(0, S_STALL);
    xin[0].active = xout[0].active = false;
    interrupts();
}

/* Resume signalling: CNTR.RESUME (bit 4) for 1-15 ms. */
static void dcd_remote_wakeup(void)
{
    CNTR = (uint16_t)(CNTR | 0x0010u);
    delay(5);
    CNTR = (uint16_t)(CNTR & ~0x0010u);
}

/* ------------------------------------------------------------ interrupt */
static void in_done(uint8_t ep)
{
    Xfer &x = xin[ep];
    if (!x.active) {
        return;
    }
    x.done = (uint16_t)(x.done + x.last);
    if (x.done < x.len) {
        send(ep);
        return;
    }
    x.active = false;
    ch32rv_usbd_isr_xfer((uint8_t)(0x80 | ep), x.done);
}

static void out_done(uint8_t ep)
{
    Xfer &x = xout[ep];
    const uint16_t n = (uint16_t)(count_rx(ep) & 0x3FFu);
    if (!x.active) {
        return;
    }
    const uint16_t room = (uint16_t)(x.len - x.done);
    const uint16_t take = n < room ? n : room;
    if (take) {
        pma_read(x.pma, x.buf + x.done, take);
    }
    x.done = (uint16_t)(x.done + take);
    if (x.done >= x.len || n < x.mps) {
        x.active = false;
        ch32rv_usbd_isr_xfer(ep, x.done);
    } else {
        set_stat_rx(ep, S_VALID);
    }
}

static void isr(void)
{
    uint16_t is = ISTR;
    while (is & I_CTR) {
        const uint8_t ep = is & 0x0F;
        const uint16_t v = EPR(ep);
        if (v & CTR_TX) {
            clear_ctr(ep, CTR_TX);
            in_done(ep);
        }
        if (v & CTR_RX) {
            if (ep == 0 && (v & SETUP_B)) {
                static uint8_t setup[8];
                pma_read(xout[0].pma, setup, 8);
                clear_ctr(0, CTR_RX);
                xin[0].active = xout[0].active = false;
                ch32rv_usbd_isr_setup(setup);
            } else {
                clear_ctr(ep, CTR_RX);
                out_done(ep);
            }
        }
        is = ISTR;
    }
    if (is & I_RESET) {
        ISTR = (uint16_t)~I_RESET;
        suspended = false;
        reset_device();
        ch32rv_usbd_isr_reset();
    }
    if (is & I_SUSP) {
        CNTR = (uint16_t)(CNTR | C_FSUSP);
        ISTR = (uint16_t)~I_SUSP;
        if (!suspended) {
            suspended = true;
            ch32rv_usbd_isr_suspend();
        }
    }
    if (is & I_WKUP) {
        CNTR = (uint16_t)(CNTR & ~C_FSUSP);
        ISTR = (uint16_t)~I_WKUP;
        if (suspended) {
            suspended = false;
            ch32rv_usbd_isr_resume();
        }
    }
    ISTR = (uint16_t)~(is & 0x7F00u & ~(I_RESET | I_SUSP | I_WKUP));   /* the rest: errors, SOF */
}

extern "C" __attribute__((interrupt)) void USB_LP_CAN1_RX0_IRQHandler(void)
{
    isr();
}

extern "C" __attribute__((interrupt)) void USB_HP_CAN1_TX_IRQHandler(void)
{
    isr();
}

const CH32RVUsbDcd ch32rv_usb_dcd_usbd = {
    "USBD (CH32V203/V208)", 8, 64, 1u << (uint8_t)CH32RVUsbSpeed::Full,
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup,
    dcd_poll,
};

#endif
