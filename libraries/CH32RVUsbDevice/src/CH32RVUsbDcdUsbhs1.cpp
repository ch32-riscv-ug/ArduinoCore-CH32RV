/* USBHS of CH32V305 / V307 / V317 (the first-generation IP), device role
 * (CH32RVUsbDcd.h).
 *
 * Base 0x40023400 (EVT USBHS_BASE = AHB + 0x3400). Map and bits as the
 * TinyUSB driver this core shipped until 2026-10-20 had them
 * (dcd_ch32_usbhs.c, Greg Davill / Denis Krasutski, MIT; register header
 * ch32_usbhs_reg.h): CONTROL 0x00 (DMA_EN 0, CLR_ALL 1, FORCE_RST 2,
 * INT_BUSY_EN 3, DEV_PU_EN 4, speed 6:5), HOST_CTRL 0x01 (PHY_SUSPENDM 4),
 * INT_EN 0x02, DEV_AD 0x03, SUSPEND 0x06 (DEV_REMOTE_WAKEUP 2), SPEED_TYPE
 * 0x08, INT_FG 0x0A (BUS_RST 0, TRANSFER 1, SUSPEND 2, SETUP 5), INT_ST 0x0B
 * (TOG_OK 6, token 5:4, endpoint 3:0), RX_LEN 0x0C, ENDP_CONFIG 0x10 (TX
 * enables 15:0, RX 31:16), ENDP_TYPE 0x14 (isochronous, same layout),
 * BUF_MODE 0x18, EP0 DMA 0x1C, RX DMA 0x1C + 4n, TX DMA 0x58 + 4n, MAX_LEN
 * 0x98 + 4n, then per endpoint at 0xD8 + 4n the transmit length, transmit
 * control (+2) and receive control (+3): response 1:0 (ACK 00, NYET 01, NAK
 * 10, STALL 11), toggle 4:3, auto-toggle 5. Sixteen endpoints.
 *
 * Unlike the second-generation IP this one has INT_BUSY (the SIE NAKs while
 * a transfer interrupt is pending) and hardware toggles; SETUP is its own
 * interrupt.
 *
 * The PHY needs HSE: its PLL takes a 3, 4, 5 or 8 MHz reference divided from
 * the crystal (RM: "the clock source of USBHSPLL can only be HSE"),
 * CH32RVUsbDeviceConfig::hseHz names it. RCC_CFGR2 (+0x2C): USBHSDIV 26:24
 * (HSE / (n + 1)), USBHSPLLSRC 27 (0 = HSE), USBHSCKREF 29:28 (00 3 MHz,
 * 01 4 MHz, 10 8 MHz, 11 5 MHz), USBHSPLL_ALIVE 30. Pads PB6 / PB7 belong to
 * the PHY. Build only. */
#if defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "CH32RVUsbDcd.h"
#include "CH32RVUsbDeviceInternal.h"

#define HS_BASE 0x40023400u
#define R8(o)  (*(volatile uint8_t *)(HS_BASE + (o)))
#define R16(o) (*(volatile uint16_t *)(HS_BASE + (o)))
#define R32(o) (*(volatile uint32_t *)(HS_BASE + (o)))

#define CONTROL     R8(0x00)
#define HOST_CTRL   R8(0x01)
#define INT_EN      R8(0x02)
#define DEV_AD      R8(0x03)
#define SUSPEND_R   R8(0x06)
#define SPEED_TYPE  R8(0x08)
#define INT_FG      R8(0x0A)
#define INT_ST      R8(0x0B)
#define RX_LEN      R16(0x0C)
#define ENDP_CONFIG R32(0x10)
#define ENDP_TYPE   R32(0x14)
#define BUF_MODE    R32(0x18)
#define EP0_DMA     R32(0x1C)
#define RX_DMA(n)   R32(0x1Cu + 4u * (n))
#define TX_DMA(n)   R32(0x58u + 4u * (n))
#define MAX_LEN(n)  R16(0x98u + 4u * (n))
#define TX_LEN(n)   R16(0xD8u + 4u * (n))
#define TX_CTRL(n)  R8(0xDAu + 4u * (n))
#define RX_CTRL(n)  R8(0xDBu + 4u * (n))

#define C_DMA_EN     0x01
#define C_INT_BUSY   0x08
#define C_DEV_PU_EN  0x10
#define C_HIGH_SPEED 0x20
#define H_PHY_SUSPENDM 0x10
#define IE_BUS_RST   0x01
#define IE_TRANSFER  0x02
#define IE_SUSPEND   0x04
#define IE_SETUP     0x20
#define ST_TOG_OK    0x40
#define ST_TOKEN     0x30
#define TOKEN_OUT    0x00
#define TOKEN_SOF    0x10
#define TOKEN_IN     0x20
#define MIS_SUSPEND  0x04

#define RES_MASK  0x03u
#define RES_ACK   0x00u
#define RES_NYET  0x01u
#define RES_NAK   0x02u
#define RES_STALL 0x03u
#define TOG_MASK  0x18u
#define TOG1      0x08u
#define AUTOTOG   0x20u

#define NEP 16

#define RCC_CTLR_HSEON   (1u << 16)
#define RCC_CTLR_HSERDY  (1u << 17)
#define RCC_CFGR2        CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)
#define CFGR2_HS_MASK    0x7F000000u                      /* 30:24, not USBFSSRC 31 */
#define CFGR2_ALIVE      (1u << 30)

static uint8_t ep0_buf[64] __attribute__((aligned(4)));

namespace {
struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint16_t last;
    uint8_t *bounce;
    bool active, iso;
};
}  // namespace
static Xfer xin[NEP], xout[NEP];
static bool ep0_tog;
static bool suspended;

/* ------------------------------------------------------------ clocks */
static uint32_t pll_bits(uint32_t hse)
{
    static const struct {
        uint32_t hz;
        uint32_t sel;
    } refs[] = {{4000000u, 1u}, {8000000u, 2u}, {5000000u, 3u}, {3000000u, 0u}};
    for (unsigned r = 0; r < sizeof refs / sizeof refs[0]; r++) {
        for (uint32_t div = 1; div <= 8; div++) {
            if (hse == refs[r].hz * div) {
                return CFGR2_ALIVE | (refs[r].sel << 28) | ((div - 1u) << 24);
            }
        }
    }
    return 0;
}

static bool clock_ok(void)
{
    return (CH32RV_RCC_CTLR & RCC_CTLR_HSERDY) && (RCC_CFGR2 & CFGR2_ALIVE);
}

static bool clock_on(void)
{
    const uint32_t pll = pll_bits(ch32rv_usb_hse_hz);
    if (!pll) {
        return false;
    }
    if (!(CH32RV_RCC_CTLR & RCC_CTLR_HSERDY)) {
        CH32RV_RCC_CTLR |= RCC_CTLR_HSEON;
        for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & RCC_CTLR_HSERDY);) {
            if ((uint32_t)(millis() - t0) > 20u) {
                CH32RV_RCC_CTLR &= ~RCC_CTLR_HSEON;
                return false;                            /* no crystal */
            }
        }
    }
    RCC_CFGR2 = (RCC_CFGR2 & ~CFGR2_HS_MASK) | pll;
    ch32rv_clock_enable(USBHS);
    return true;
}

static void dcd_poll(void)
{
    if (!clock_ok()) {
        clock_on();                                      /* after SystemInit() ran again for a probe */
    }
}

/* ------------------------------------------------------------ helpers */
static void set_tx(uint8_t ep, uint8_t res) { TX_CTRL(ep) = (uint8_t)((TX_CTRL(ep) & ~RES_MASK) | res); }
static void set_rx(uint8_t ep, uint8_t res) { RX_CTRL(ep) = (uint8_t)((RX_CTRL(ep) & ~RES_MASK) | res); }

static void send(uint8_t ep)
{
    Xfer &x = xin[ep];
    const uint16_t left = (uint16_t)(x.len - x.done);
    const uint16_t n = left < x.mps ? left : x.mps;
    if (ep == 0) {
        if (n) {
            memcpy(ep0_buf, x.buf + x.done, n);
        }
        TX_LEN(0) = n;
        TX_CTRL(0) = (uint8_t)(RES_ACK | (ep0_tog ? TOG1 : 0));
        ep0_tog = !ep0_tog;
    } else {
        if (n) {
            memcpy(x.bounce, x.buf + x.done, n);
        }
        TX_DMA(ep) = (uint32_t)(uintptr_t)x.bounce;
        TX_LEN(ep) = n;
        set_tx(ep, x.iso ? RES_NYET : RES_ACK);          /* NYET is "no handshake" for isochronous */
    }
    x.last = n;
}

static void receive(uint8_t ep)
{
    Xfer &x = xout[ep];
    if (ep) {
        RX_DMA(ep) = (uint32_t)(uintptr_t)x.bounce;
        MAX_LEN(ep) = x.mps;
    }
    set_rx(ep, x.iso ? RES_NYET : RES_ACK);
}

static void reset_endpoints(void)
{
    ENDP_CONFIG = 0x00010001u;                           /* EP0 both ways */
    ENDP_TYPE = 0;
    BUF_MODE = 0;
    for (uint8_t ep = 0; ep < NEP; ep++) {
        TX_LEN(ep) = 0;
        TX_CTRL(ep) = RES_NAK;
        RX_CTRL(ep) = RES_NAK;
        MAX_LEN(ep) = 0;
        xin[ep].active = xout[ep].active = false;
    }
    EP0_DMA = (uint32_t)(uintptr_t)ep0_buf;
    MAX_LEN(0) = 64;
    RX_CTRL(0) = RES_ACK;
    ep0_tog = true;
}

/* ------------------------------------------------------------ table */
static bool dcd_open(void)
{
    if (!clock_on()) {
        return false;
    }
    for (uint8_t ep = 0; ep < NEP; ep++) {
        xin[ep].mps = xout[ep].mps = 64;
    }
    suspended = false;
    HOST_CTRL = 0;
    HOST_CTRL = H_PHY_SUSPENDM;
    CONTROL = 0;
    CONTROL = C_DMA_EN | C_INT_BUSY | C_HIGH_SPEED;
    INT_EN = IE_SETUP | IE_TRANSFER | IE_BUS_RST | IE_SUSPEND;
    reset_endpoints();
    DEV_AD = 0;
    INT_FG = 0xFF;
    ch32rv_irq_enable(CH32RV_IRQN_USBHS);
    return true;
}

static void dcd_connect(bool on)
{
    CONTROL = (uint8_t)(C_DMA_EN | C_INT_BUSY | C_HIGH_SPEED | (on ? C_DEV_PU_EN : 0));
}

static void dcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBHS);
    CONTROL = 0;
    INT_EN = 0;
    for (uint8_t ep = 1; ep < NEP; ep++) {
        free(xin[ep].bounce);
        free(xout[ep].bounce);
        xin[ep].bounce = xout[ep].bounce = nullptr;
    }
    ch32rv_clock_disable(USBHS);
    RCC_CFGR2 &= ~CFGR2_ALIVE;
}

static CH32RVUsbSpeed dcd_speed(void)
{
    return (SPEED_TYPE & 3u) == 1u ? CH32RVUsbSpeed::High : CH32RVUsbSpeed::Full;
}

static void dcd_set_address(uint8_t address)
{
    DEV_AD = address;
}

static bool dcd_ep_open(uint8_t address, uint8_t type, uint16_t mps)
{
    const uint8_t ep = address & 0x0F;
    const bool in = address & 0x80;
    if (ep == 0 || ep >= NEP || mps == 0 || mps > 1024) {
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
    x.iso = type == CH32RV_USB_XFER_ISOCHRONOUS;
    noInterrupts();
    if (in) {
        ENDP_CONFIG |= 1u << ep;
        ENDP_TYPE = x.iso ? (ENDP_TYPE | (1u << ep)) : (ENDP_TYPE & ~(1u << ep));
        TX_LEN(ep) = 0;
        TX_CTRL(ep) = (uint8_t)(RES_NAK | (x.iso ? 0 : AUTOTOG));
    } else {
        ENDP_CONFIG |= 1u << (16 + ep);
        ENDP_TYPE = x.iso ? (ENDP_TYPE | (1u << (16 + ep))) : (ENDP_TYPE & ~(1u << (16 + ep)));
        MAX_LEN(ep) = mps;
        RX_CTRL(ep) = (uint8_t)(RES_NAK | (x.iso ? 0 : AUTOTOG));
    }
    interrupts();
    return true;
}

static void dcd_ep_close(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep >= NEP) {
        return;
    }
    noInterrupts();
    if (address & 0x80) {
        xin[ep].active = false;
        TX_CTRL(ep) = RES_NAK;
        TX_LEN(ep) = 0;
        ENDP_TYPE &= ~(1u << ep);
        ENDP_CONFIG &= ~(1u << ep);
    } else {
        xout[ep].active = false;
        RX_CTRL(ep) = RES_NAK;
        MAX_LEN(ep) = 0;
        ENDP_TYPE &= ~(1u << (16 + ep));
        ENDP_CONFIG &= ~(1u << (16 + ep));
    }
    interrupts();
}

static void dcd_ep_close_all(void)
{
    noInterrupts();
    for (uint8_t ep = 1; ep < NEP; ep++) {
        xin[ep].active = xout[ep].active = false;
        TX_LEN(ep) = 0;
        TX_CTRL(ep) = RES_NAK;
        RX_CTRL(ep) = RES_NAK;
        MAX_LEN(ep) = 0;
    }
    ENDP_CONFIG = 0x00010001u;
    ENDP_TYPE = 0;
    interrupts();
}

static bool dcd_xfer(uint8_t address, uint8_t *buffer, uint16_t length)
{
    const uint8_t ep = address & 0x0F;
    if (ep >= NEP) {
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
        if (ep == 0) {
            /* A status OUT is DATA1; a data stage alternates from the SETUP's
             * DATA0. */
            RX_CTRL(0) = length == 0 ? (uint8_t)((RX_CTRL(0) & ~TOG_MASK) | TOG1) : (uint8_t)(RX_CTRL(0) ^ TOG1);
        }
        receive(ep);
    }
    interrupts();
    return true;
}

static void dcd_abort(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep >= NEP) {
        return;
    }
    noInterrupts();
    if (address & 0x80) {
        xin[ep].active = false;
        TX_LEN(ep) = 0;
        if ((TX_CTRL(ep) & RES_MASK) != RES_STALL) {
            set_tx(ep, xin[ep].iso ? RES_NYET : RES_NAK);
        }
    } else {
        xout[ep].active = false;
        if ((RX_CTRL(ep) & RES_MASK) != RES_STALL) {
            set_rx(ep, xout[ep].iso ? RES_NYET : RES_NAK);
        }
    }
    interrupts();
}

static void dcd_stall(uint8_t address, bool on)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep >= NEP) {
        return;
    }
    noInterrupts();
    if (address & 0x80) {
        Xfer &x = xin[ep];
        if (on) {
            TX_LEN(ep) = 0;
            TX_CTRL(ep) = RES_STALL;
        } else {
            TX_CTRL(ep) = (uint8_t)(RES_NAK | (x.iso ? 0 : AUTOTOG));   /* DATA0 */
            if (x.active) {
                send(ep);
            }
        }
    } else {
        Xfer &x = xout[ep];
        if (on) {
            RX_CTRL(ep) = RES_STALL;
        } else {
            RX_CTRL(ep) = (uint8_t)(RES_NAK | (x.iso ? 0 : AUTOTOG));
            if (x.active) {
                receive(ep);
            }
        }
    }
    interrupts();
}

static void dcd_ep0_stall(void)
{
    noInterrupts();
    TX_CTRL(0) = RES_STALL;
    RX_CTRL(0) = RES_STALL;
    xin[0].active = xout[0].active = false;
    interrupts();
}

static void dcd_remote_wakeup(void)
{
    SUSPEND_R |= 0x04;
    delay(10);
    SUSPEND_R &= (uint8_t)~0x04;
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
    if (ep == 0) {
        TX_CTRL(0) = (uint8_t)(RES_NAK | (ep0_tog ? TOG1 : 0));
    } else {
        set_tx(ep, x.iso ? RES_NYET : RES_NAK);
    }
    ch32rv_usbd_isr_xfer((uint8_t)(0x80 | ep), x.done);
}

static void out_done(uint8_t ep, uint16_t n, bool tog_ok)
{
    Xfer &x = xout[ep];
    if (!x.active) {
        return;
    }
    if (!tog_ok && ep != 0 && !x.iso) {
        return;                                          /* a retransmission: already taken */
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
    if (x.done >= x.len || n < x.mps) {
        x.active = false;
        set_rx(ep, (ep && x.iso) ? RES_NYET : RES_NAK);
        ch32rv_usbd_isr_xfer(ep, x.done);
    } else {
        if (ep == 0) {
            RX_CTRL(0) ^= TOG1;
        }
        receive(ep);
    }
}

static void isr(void)
{
    const uint8_t fg = INT_FG, st = INT_ST;
    if (fg & IE_TRANSFER) {
        const uint8_t ep = st & 0x0F;
        const uint8_t token = st & ST_TOKEN;
        if (ep < NEP) {
            if (token == TOKEN_OUT) {
                out_done(ep, RX_LEN, (st & ST_TOG_OK) != 0);
            } else if (token == TOKEN_IN) {
                in_done(ep);
            }
        }
        INT_FG = IE_TRANSFER;
    } else if (fg & IE_SETUP) {
        ep0_tog = true;
        xin[0].active = xout[0].active = false;
        const uint16_t wlength = (uint16_t)(ep0_buf[6] | (ep0_buf[7] << 8));
        RX_CTRL(0) = wlength == 0 ? RES_ACK : RES_NAK;
        TX_CTRL(0) = RES_NAK;
        ch32rv_usbd_isr_setup(ep0_buf);
        INT_FG = IE_SETUP;
    } else if (fg & IE_BUS_RST) {
        DEV_AD = 0;
        suspended = false;
        reset_endpoints();
        INT_FG = IE_BUS_RST;
        ch32rv_usbd_isr_reset();
    } else if (fg & IE_SUSPEND) {
        INT_FG = IE_SUSPEND;
        const bool now = (R8(0x09) & MIS_SUSPEND) != 0;
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

const CH32RVUsbDcd ch32rv_usb_dcd_usbhs1 = {
    "USBHS (CH32V305/V307/V317)", NEP, 512, (1u << (uint8_t)CH32RVUsbSpeed::Full) | (1u << (uint8_t)CH32RVUsbSpeed::High),
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup,
    dcd_poll,
};

#endif
