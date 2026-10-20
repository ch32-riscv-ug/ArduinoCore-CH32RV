/* CH32H417 / H416 / H415 USBHS (USB 2.0 high speed, 480 Mbit/s), device role
 * (CH32RVUsbDcd.h).
 *
 * WCH's newer USBHS device IP (base 0x40030000, CH32H417RM chapter 25; also on
 * CH564 / CH585 per TinyUSB). Not the V30x USBHS. Per endpoint: a DMA address
 * each way (RX 0x24 + 4 (n-1), TX 0x40 + 4 (n-1), EP0 one at 0x20), a maximum
 * receive length (0x5C + 4n), the receive length (0x7C, 0x80 + 4 (n-1)), and a
 * transmit length / transmit control / receive control at 0x9C + 4n. Control
 * bytes: DONE bit 7 (cleared by writing 0), toggle bit 2 (TX, EP0 RX) or bits
 * 3:2 (EPn RX), response 1:0 with NAK 00 / STALL 01 / ACK 10; EP0's RX has
 * SETUP_IS at bit 3 and every RX TOG_MATCH at bit 4. Toggles are kept by
 * software (TOG_AUTO left off).
 *
 * The interrupt handling follows TinyUSB PR #3779 (dcd_ch32h417_usbhs.c,
 * measured there; unmerged as of 2026-10): INT_ST names one endpoint while the
 * transfer flag is the OR of every DONE, and a SETUP that arrives while any
 * DONE is pending gets no handshake, so the handler drains all of them; a
 * response change writes DONE back as 1 so a completion that lands meanwhile is
 * not lost; SUSPEND fires constantly and only changes are reported; RX_SOF is
 * set every frame and always cleared. TinyUSB still saw about a third of the
 * SETUPs go unanswered and suspects this IP lacks the USBFS INT_BUSY auto-NAK.
 *
 * PB8 / PB9 are also SWCLK / SWDIO. Opening this driver turns the debug
 * interface off (AFIO_PCFR1.SW_CFG = 100b, the EVT's GPIO_Remap_SWJ_Disable):
 * a probe gets the chip back only through a reset (ch32rv
 * --connect-under-reset, with NRST wired), and sketches are best written by
 * DFU. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "CH32RVUsbDcd.h"

#define HS_BASE 0x40030000u
#define R8(o)  (*(volatile uint8_t *)(HS_BASE + (o)))
#define R16(o) (*(volatile uint16_t *)(HS_BASE + (o)))
#define R32(o) (*(volatile uint32_t *)(HS_BASE + (o)))

#define CONTROL    R8(0x00)
#define BASE_MODE  R8(0x01)
#define INT_EN     R8(0x02)
#define DEV_AD     R8(0x03)
#define WAKE_CTRL  R8(0x04)
#define INT_FG     R8(0x08)
#define MIS_ST     R8(0x0A)
#define UEP_TX_EN  R16(0x10)
#define UEP_RX_EN  R16(0x12)
#define UEP0_DMA   R32(0x20)
#define EP_RX_DMA(n) R32(0x24u + 4u * ((n) - 1u))
#define EP_TX_DMA(n) R32(0x40u + 4u * ((n) - 1u))
#define EP_MAX_LEN(n) R32(0x5Cu + 4u * (n))
#define EP_RX_LEN(n) R16((n) ? 0x80u + 4u * ((n) - 1u) : 0x7Cu)
#define EP_TX_LEN(n) R16(0x9Cu + 4u * (n))
#define EP_TX_CTRL(n) R8(0x9Eu + 4u * (n))
#define EP_RX_CTRL(n) R8(0x9Fu + 4u * (n))
#define UEP_TX_ISO R16(0xBC)
#define UEP_RX_ISO R16(0xBE)

#define UD_LPM_EN       0x80
#define UD_DEV_EN       0x20
#define UD_DMA_EN       0x10
#define UD_PHY_SUSPENDM 0x08
#define UD_RST_SIE      0x02
#define UD_RST_LINK     0x01
#define UD_SPEED_HIGH   0x01

#define UDIE_LINK_RDY   0x40
#define UDIE_SOF        0x20
#define UDIE_TRANSFER   0x10
#define UDIE_LPM_ACT    0x08
#define UDIE_BUS_SLEEP  0x04
#define UDIE_SUSPEND    0x02
#define UDIE_BUS_RST    0x01
#define UDMS_HS_MOD     0x80
#define UDMS_SUSPEND    0x02

#define DONE      0x80u
#define TOG_MATCH 0x10u
#define SETUP_IS  0x08u
#define TOG_MASK  0x0Cu
#define TOG1      0x04u
#define RES_MASK  0x03u
#define RES_NAK   0x00u
#define RES_STALL 0x01u
#define RES_ACK   0x02u

/* RCC (EVT USBHS_RCC_Init): the USBHS PLL from HSE (25 MHz reference), UTMI. */
#define RCC_CTLR_HSEON        (1u << 16)
#define RCC_CTLR_HSERDY       (1u << 17)
#define RCC_CTLR_USBHS_PLLON  (1u << 20)
#define RCC_CTLR_USBHS_PLLRDY (1u << 21)
#define RCC_CFGR0_UTMION      (1u << 23)
#define RCC_PLLCFGR2          CH32RV_REG32(0x40021034u)
/* AFIO_PCFR1 (offset 0 on this family, EVT AFIO_TypeDef) SW_CFG[26:24]. */
#define AFIO_PCFR1_H41X       CH32RV_REG32(CH32RV_AFIO_BASE + 0x00u)
#define SW_CFG_MASK           (7u << 24)
#define SW_CFG_SDI_OFF        (4u << 24)

static uint8_t ep0_buf[64] __attribute__((aligned(4)));

struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint16_t last;               /* bytes in the packet in flight */
    uint8_t *bounce;             /* one packet, DMA-side */
    bool active, iso, tog;
};
static Xfer xin[8], xout[8];
static bool ep0_tog, ep0_rx_tog;  /* EP0's next IN / expected OUT toggle */
static bool suspended;

static void isr(void);

/* ------------------------------------------------------------ helpers */
static void set_tx(uint8_t ep, uint8_t res)
{
    EP_TX_CTRL(ep) = (uint8_t)((EP_TX_CTRL(ep) & ~RES_MASK) | DONE | res);
}

static void set_rx(uint8_t ep, uint8_t res)
{
    EP_RX_CTRL(ep) = (uint8_t)((EP_RX_CTRL(ep) & ~RES_MASK) | DONE | res);
}

static void set_tog(uint8_t ep, bool in, bool data1)
{
    if (in) {
        EP_TX_CTRL(ep) = (uint8_t)((EP_TX_CTRL(ep) & ~TOG_MASK) | DONE | (data1 ? TOG1 : 0));
    } else {
        EP_RX_CTRL(ep) = (uint8_t)((EP_RX_CTRL(ep) & ~TOG_MASK) | DONE | (data1 ? TOG1 : 0));
    }
}

static void arm_ep0_out(void)
{
    EP_RX_CTRL(0) = (uint8_t)((ep0_rx_tog ? TOG1 : 0) | RES_ACK);
}

static void send(uint8_t ep)
{
    Xfer &x = xin[ep];
    const uint16_t left = (uint16_t)(x.len - x.done);
    const uint16_t n = left < x.mps ? left : x.mps;
    if (ep == 0) {
        if (n) {
            memcpy(ep0_buf, x.buf + x.done, n);
        }
        EP_TX_LEN(0) = n;
        EP_TX_CTRL(0) = (uint8_t)(RES_ACK | (ep0_tog ? TOG1 : 0));
        ep0_tog = !ep0_tog;
    } else {
        if (n) {
            memcpy(x.bounce, x.buf + x.done, n);
        }
        EP_TX_DMA(ep) = (uint32_t)(uintptr_t)x.bounce;
        EP_TX_LEN(ep) = n;
        if (!x.iso) {
            set_tog(ep, true, x.tog);
        }
        set_tx(ep, RES_ACK);
    }
    x.last = n;
}

static void receive(uint8_t ep)
{
    Xfer &x = xout[ep];
    EP_RX_DMA(ep) = (uint32_t)(uintptr_t)x.bounce;
    EP_MAX_LEN(ep) = x.mps;
    if (!x.iso) {
        set_tog(ep, false, x.tog);
    }
    set_rx(ep, RES_ACK);
}

/* ------------------------------------------------------------ clocks */
static bool wait_bit(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

static bool clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY)) {
        CH32RV_RCC_CTLR |= RCC_CTLR_HSEON;
        if (!wait_bit(RCC_CTLR_HSERDY)) {
            return false;
        }
        CH32RV_RCC_CTLR &= ~RCC_CTLR_USBHS_PLLON;
        RCC_PLLCFGR2 &= ~0x00001F0Fu;                    /* HSE, 25 MHz reference, no input division */
        CH32RV_RCC_CTLR |= RCC_CTLR_USBHS_PLLON;
        if (!wait_bit(RCC_CTLR_USBHS_PLLRDY)) {
            return false;
        }
    }
    CH32RV_RCC_CFGR0 |= RCC_CFGR0_UTMION;
    ch32rv_clock_enable(USBHS);
    return true;
}

/* A probe's attach makes SystemInit() run again (wiring_time.c), which turns
 * the USBHS PLL and UTMI off: put them back from the task. */
static void dcd_poll(void)
{
    if (!(CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY) || !(CH32RV_RCC_CFGR0 & RCC_CFGR0_UTMION)) {
        clock_on();
    }
}

/* ------------------------------------------------------------ table */
static void reset_endpoints(void)
{
    UEP_TX_EN = 1;
    UEP_RX_EN = 1;
    UEP_TX_ISO = 0;
    UEP_RX_ISO = 0;
    UEP0_DMA = (uint32_t)(uintptr_t)ep0_buf;
    EP_MAX_LEN(0) = 64;
    EP_TX_LEN(0) = 0;
    EP_TX_CTRL(0) = RES_NAK;
    EP_RX_CTRL(0) = RES_ACK;
    for (uint8_t ep = 1; ep < 8; ep++) {
        EP_TX_LEN(ep) = 0;
        EP_TX_CTRL(ep) = RES_NAK;
        EP_RX_CTRL(ep) = RES_NAK;
        EP_MAX_LEN(ep) = 0;
        xin[ep].active = xout[ep].active = false;
    }
    xin[0].active = xout[0].active = false;
    ep0_tog = true;
    ep0_rx_tog = false;
}

static bool dcd_open(void)
{
    if (!clock_on()) {
        return false;
    }
    AFIO_PCFR1_H41X = (AFIO_PCFR1_H41X & ~SW_CFG_MASK) | SW_CFG_SDI_OFF;   /* PB8 / PB9 to the PHY */
    for (uint8_t ep = 0; ep < 8; ep++) {
        xin[ep].mps = xout[ep].mps = 64;
    }
    suspended = false;
    CONTROL = UD_RST_LINK | UD_PHY_SUSPENDM;
    INT_EN = UDIE_BUS_RST | UDIE_SUSPEND | UDIE_BUS_SLEEP | UDIE_LPM_ACT | UDIE_TRANSFER | UDIE_LINK_RDY;
    reset_endpoints();
    BASE_MODE = UD_SPEED_HIGH;
    DEV_AD = 0;
    INT_FG = 0xFF;
    CONTROL = UD_DMA_EN | UD_LPM_EN | UD_PHY_SUSPENDM;     /* DEV_EN (the pull-up) on connect */
    ch32rv_irq_enable(CH32RV_IRQN_USBHS);
    return true;
}

static void dcd_connect(bool on)
{
    CONTROL = (uint8_t)((on ? UD_DEV_EN : 0) | UD_DMA_EN | UD_LPM_EN | UD_PHY_SUSPENDM);
}

static void dcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBHS);
    INT_EN = 0;
    INT_FG = 0xFF;
    CONTROL = UD_RST_SIE | UD_RST_LINK;
    for (uint8_t ep = 1; ep < 8; ep++) {
        free(xin[ep].bounce);
        free(xout[ep].bounce);
        xin[ep].bounce = xout[ep].bounce = nullptr;
    }
    ch32rv_clock_disable(USBHS);
    /* The debug interface stays off until a reset (SW_CFG is write-only). */
}

static CH32RVUsbSpeed dcd_speed(void)
{
    return (MIS_ST & UDMS_HS_MOD) ? CH32RVUsbSpeed::High : CH32RVUsbSpeed::Full;
}

static void dcd_set_address(uint8_t address)
{
    DEV_AD = address;
}

static bool dcd_ep_open(uint8_t address, uint8_t type, uint16_t mps)
{
    const uint8_t ep = address & 0x0F;
    const bool in = address & 0x80;
    if (ep == 0 || ep > 7 || mps == 0 || mps > 1024) {
        return false;
    }
    Xfer &x = in ? xin[ep] : xout[ep];
    if (!x.bounce || x.mps < mps) {
        free(x.bounce);
        x.bounce = (uint8_t *)malloc(mps);
        if (!x.bounce) {
            return false;
        }
    }
    x.mps = mps;
    x.active = false;
    x.tog = false;
    x.iso = type == CH32RV_USB_XFER_ISOCHRONOUS;
    const uint16_t bit = (uint16_t)(1u << ep);
    noInterrupts();
    if (in) {
        UEP_TX_EN |= bit;
        UEP_TX_ISO = x.iso ? (uint16_t)(UEP_TX_ISO | bit) : (uint16_t)(UEP_TX_ISO & ~bit);
        EP_TX_LEN(ep) = 0;
        EP_TX_CTRL(ep) = RES_NAK;
    } else {
        UEP_RX_EN |= bit;
        UEP_RX_ISO = x.iso ? (uint16_t)(UEP_RX_ISO | bit) : (uint16_t)(UEP_RX_ISO & ~bit);
        EP_MAX_LEN(ep) = mps;
        EP_RX_CTRL(ep) = RES_NAK;
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
    const uint16_t bit = (uint16_t)(1u << ep);
    noInterrupts();
    if (address & 0x80) {
        xin[ep].active = false;
        EP_TX_CTRL(ep) = RES_NAK;
        EP_TX_LEN(ep) = 0;
        UEP_TX_ISO &= (uint16_t)~bit;
        UEP_TX_EN &= (uint16_t)~bit;
    } else {
        xout[ep].active = false;
        EP_RX_CTRL(ep) = RES_NAK;
        EP_MAX_LEN(ep) = 0;
        UEP_RX_ISO &= (uint16_t)~bit;
        UEP_RX_EN &= (uint16_t)~bit;
    }
    interrupts();
}

static void dcd_ep_close_all(void)
{
    noInterrupts();
    for (uint8_t ep = 1; ep < 8; ep++) {
        xin[ep].active = xout[ep].active = false;
        EP_TX_LEN(ep) = 0;
        EP_TX_CTRL(ep) = RES_NAK;
        EP_RX_CTRL(ep) = RES_NAK;
        EP_MAX_LEN(ep) = 0;
    }
    UEP_TX_EN = 1;
    UEP_RX_EN = 1;
    UEP_TX_ISO = 0;
    UEP_RX_ISO = 0;
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
    } else if (ep == 0) {
        arm_ep0_out();                                   /* data or status OUT */
    } else {
        receive(ep);
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
        EP_TX_LEN(ep) = 0;
        if ((EP_TX_CTRL(ep) & RES_MASK) != RES_STALL) {
            set_tx(ep, RES_NAK);
        }
    } else {
        xout[ep].active = false;
        if ((EP_RX_CTRL(ep) & RES_MASK) != RES_STALL) {
            set_rx(ep, RES_NAK);
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
    Xfer &x = in ? xin[ep] : xout[ep];
    noInterrupts();
    if (on) {
        if (in) {
            EP_TX_LEN(ep) = 0;
            set_tx(ep, RES_STALL);
        } else {
            set_rx(ep, RES_STALL);
        }
    } else {
        /* CLEAR_FEATURE(HALT): DATA0 again; a transfer still armed resumes from
         * the packet the stall dropped. */
        x.tog = false;
        if (x.active) {
            if (in) {
                send(ep);
            } else {
                receive(ep);
            }
        } else if (in) {
            EP_TX_CTRL(ep) = RES_NAK;
        } else {
            EP_RX_CTRL(ep) = RES_NAK;
        }
    }
    interrupts();
}

static void dcd_ep0_stall(void)
{
    noInterrupts();
    EP_TX_CTRL(0) = TOG1 | RES_STALL;
    EP_RX_CTRL(0) = TOG1 | RES_STALL;
    xin[0].active = xout[0].active = false;
    interrupts();
}

static void dcd_remote_wakeup(void)
{
    WAKE_CTRL |= 0x01;                                   /* hardware drives K and clears the bit */
}

/* ------------------------------------------------------------ interrupt */
static void in_done(uint8_t ep)
{
    Xfer &x = xin[ep];
    if (!x.active) {
        if (ep) {
            set_tx(ep, RES_NAK);
        }
        return;
    }
    x.done = (uint16_t)(x.done + x.last);
    if (ep && !x.iso) {
        x.tog = !x.tog;
    }
    if (x.done < x.len) {
        send(ep);
        return;
    }
    x.active = false;
    if (ep == 0) {
        EP_TX_CTRL(0) = (uint8_t)(RES_NAK | (ep0_tog ? TOG1 : 0));
    } else {
        set_tx(ep, RES_NAK);
    }
    ch32rv_usbd_isr_xfer((uint8_t)(0x80 | ep), x.done);
}

static void out_done(uint8_t ep, uint16_t n)
{
    Xfer &x = xout[ep];
    if (!x.active) {
        if (ep) {
            set_rx(ep, x.iso ? RES_ACK : RES_NAK);
        }
        return;
    }
    const uint16_t room = (uint16_t)(x.len - x.done);
    uint16_t take = n < room ? n : room;
    if (take > x.mps) {
        take = x.mps;
    }
    if (take) {
        memcpy(x.buf + x.done, ep ? x.bounce : ep0_buf, take);
    }
    x.done = (uint16_t)(x.done + take);
    if (ep == 0) {
        ep0_rx_tog = !ep0_rx_tog;
    } else if (!x.iso) {
        x.tog = !x.tog;
    }
    if (x.done >= x.len || n < x.mps) {
        x.active = false;
        set_rx(ep, RES_NAK);
        ch32rv_usbd_isr_xfer(ep, x.done);
    } else if (ep == 0) {
        arm_ep0_out();
    } else {
        receive(ep);
    }
}

static void isr(void)
{
    const uint8_t fg = INT_FG;
    if (fg & UDIE_TRANSFER) {
        for (uint8_t ep = 0; ep < 8; ep++) {
            const uint8_t rx = EP_RX_CTRL(ep);
            if (rx & DONE) {
                if (ep == 0 && (rx & SETUP_IS)) {
                    /* A finished status IN pending with this SETUP goes first;
                     * an unfinished data IN the host abandoned is dropped. */
                    if ((EP_TX_CTRL(0) & DONE) && xin[0].active && xin[0].done + xin[0].last >= xin[0].len) {
                        EP_TX_CTRL(0) &= (uint8_t)~DONE;
                        in_done(0);
                    }
                    ep0_tog = true;
                    ep0_rx_tog = true;
                    xin[0].active = xout[0].active = false;
                    const uint16_t wlength = (uint16_t)(ep0_buf[6] | (ep0_buf[7] << 8));
                    EP_TX_CTRL(0) = (uint8_t)(RES_NAK | TOG1);
                    EP_RX_CTRL(0) = (uint8_t)((wlength == 0 ? RES_ACK : RES_NAK) | TOG1);
                    ch32rv_usbd_isr_setup(ep0_buf);
                } else {
                    EP_RX_CTRL(ep) = (uint8_t)(rx & ~DONE);
                    /* A packet out of toggle is the host's retransmission after
                     * a lost ACK: already taken, keep the endpoint armed. */
                    if (ep == 0 || xout[ep].iso || (rx & TOG_MATCH)) {
                        out_done(ep, EP_RX_LEN(ep));
                    }
                }
            }
            if (EP_TX_CTRL(ep) & DONE) {
                EP_TX_CTRL(ep) &= (uint8_t)~DONE;
                in_done(ep);
            }
        }
    } else if (fg & UDIE_SOF) {
        INT_FG = UDIE_SOF;                               /* set every frame; not used */
    } else if (fg & UDIE_BUS_RST) {
        DEV_AD = 0;
        suspended = false;
        reset_endpoints();
        INT_FG = UDIE_BUS_RST;
        ch32rv_usbd_isr_reset();
    } else if (fg & UDIE_SUSPEND) {
        INT_FG = UDIE_SUSPEND;
        const bool now = (MIS_ST & UDMS_SUSPEND) != 0;
        if (now != suspended) {
            suspended = now;
            if (now) {
                ch32rv_usbd_isr_suspend();
            } else {
                ch32rv_usbd_isr_resume();
            }
        }
    } else {
        INT_FG = fg;
    }
}

extern "C" __attribute__((interrupt)) void USBHS_IRQHandler(void)
{
    isr();
}

const CH32RVUsbDcd ch32rv_usb_dcd_usbhs_h41x = {
    "USBHS (CH32H41x)", 8, 512, (1u << (uint8_t)CH32RVUsbSpeed::Full) | (1u << (uint8_t)CH32RVUsbSpeed::High),
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup,
    dcd_poll,
};

#endif
