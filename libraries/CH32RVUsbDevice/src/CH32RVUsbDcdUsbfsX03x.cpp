/* CH32X035 / X033 USBFS, device role (CH32RVUsbDcd.h).
 *
 * Register map from the CH32X035 EVT: DMA addresses EP0-3 at 0x10 + 4n and
 * EP5-7 at 0x54 + 4(n-5) (EP4 has none: it lives in EP0's region, OUT at +64
 * and IN at +128); length / control words EP0-4 at 0x20 + 4n and EP5-7 at
 * 0x64 + 4(n-5); one control byte per endpoint (T_RES 1:0, R_RES 3:2, T_TOG 6,
 * R_TOG 7). An endpoint with both directions takes OUT at its buffer and IN at
 * buffer + 64; every buffer holds one 64-byte packet. INT_BUSY makes the SIE
 * NAK while a transfer interrupt is pending, so the handler copies each packet
 * before the next can land. Isochronous endpoints answer without handshake
 * ("no response") and always use DATA0. */
#if defined(CH32RV_SERIES_CH32X035) || defined(CH32RV_SERIES_CH32X033)

#include <Arduino.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "ch32rv_usbfs_irq.h"
#include "CH32RVUsbDcd.h"

#define USB_BASE 0x40023400u
#define R8(o)  (*(volatile uint8_t *)(USB_BASE + (o)))
#define R16(o) (*(volatile uint16_t *)(USB_BASE + (o)))
#define R32(o) (*(volatile uint32_t *)(USB_BASE + (o)))

#define BASE_CTRL  R8(0x00)
#define UDEV_CTRL  R8(0x01)
#define INT_EN     R8(0x02)
#define DEV_ADDR   R8(0x03)
#define MIS_ST     R8(0x05)
#define INT_FG     R8(0x06)
#define INT_ST     R8(0x07)
#define RX_LEN     R16(0x08)
#define UEP4_1_MOD R8(0x0C)
#define UEP2_3_MOD R8(0x0D)
#define UEP567_MOD R8(0x0E)

static inline uint32_t len_off(uint8_t ep) { return ep <= 4 ? 0x20u + 4u * ep : 0x64u + 4u * (ep - 5u); }
#define EP_LEN(ep) R16(len_off(ep))
#define EP_CTL(ep) R16(len_off(ep) + 2u)

#define UC_DEV_PU_EN 0x20
#define UC_INT_BUSY  0x08
#define UC_RESET_SIE 0x04
#define UC_CLR_ALL   0x02
#define UC_DMA_EN    0x01
#define UD_PD_DIS    0x80
#define UD_PORT_EN   0x01
#define UIE_SUSPEND  0x04
#define UIE_TRANSFER 0x02
#define UIE_BUS_RST  0x01
#define UMS_SUSPEND  0x04
#define UIS_TOG_OK   0x40
#define UIS_TOKEN    0x30
#define TOKEN_OUT    0x00
#define TOKEN_IN     0x20
#define TOKEN_SETUP  0x30

#define T_MASK  0x03u
#define T_ACK   0x00u
#define T_NONE  0x01u
#define T_NAK   0x02u
#define T_STALL 0x03u
#define R_MASK  0x0Cu
#define R_ACK   0x00u
#define R_NONE  0x04u
#define R_NAK   0x08u
#define R_STALL 0x0Cu
#define T_TOG   0x40u
#define R_TOG   0x80u

/* AFIO_CTLR: USB_IOEN hands PC16/PC17 to the PHY, USB_PHY_V33 the 3.3 V supply
 * (VDD = 3.3 V on every board this core targets so far), UDP_PUE / UDM_PUE
 * 11b the 1.5 kOhm pull-up on D+ / D-. Values from the EVT. */
#define AFIO_UDM_PUE_MASK 0x03u
#define AFIO_UDM_PUE_1K5  0x03u
#define AFIO_UDP_PUE_MASK 0x0Cu
#define AFIO_UDP_PUE_1K5  0x0Cu
#define AFIO_USB_PHY_V33  0x40u
#define AFIO_USB_IOEN     0x80u

/* EP0 + EP4 OUT + EP4 IN, then 128 bytes for each of EP1-3, 5-7. */
static uint8_t ep0_region[192] __attribute__((aligned(4)));
static uint8_t ep_region[6][128] __attribute__((aligned(4)));

static inline uint8_t *out_buf(uint8_t ep)
{
    if (ep == 0) return ep0_region;
    if (ep == 4) return ep0_region + 64;
    return ep_region[ep < 4 ? ep - 1 : ep - 2];
}

static inline uint8_t *in_buf(uint8_t ep)
{
    if (ep == 0) return ep0_region;
    if (ep == 4) return ep0_region + 128;
    return ep_region[ep < 4 ? ep - 1 : ep - 2] + 64;
}

struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint8_t last;                 /* bytes in the IN packet in flight */
    bool active, iso;
};
static Xfer xin[8], xout[8];

static void isr(void);

/* ------------------------------------------------------------ helpers */
static void set_t(uint8_t ep, uint8_t res)
{
    EP_CTL(ep) = (uint16_t)((EP_CTL(ep) & ~T_MASK) | res);
}

static void set_r(uint8_t ep, uint8_t res)
{
    EP_CTL(ep) = (uint16_t)((EP_CTL(ep) & ~R_MASK) | res);
}

static void load_in(uint8_t ep)
{
    Xfer &x = xin[ep];
    const uint16_t left = (uint16_t)(x.len - x.done);
    const uint8_t n = (uint8_t)(left > x.mps ? x.mps : left);
    if (n) {
        memcpy(in_buf(ep), x.buf + x.done, n);
    }
    x.last = n;
    EP_LEN(ep) = n;
    set_t(ep, x.iso ? T_NONE : T_ACK);
}

static void mode_bits(uint8_t ep, bool in, bool on)
{
    static const uint8_t rx_bit[8] = {0, 0x80, 0x08, 0x80, 0x08, 0x02, 0x08, 0x20};
    static const uint8_t tx_bit[8] = {0, 0x40, 0x04, 0x40, 0x04, 0x01, 0x04, 0x10};
    volatile uint8_t &reg = ep == 1 || ep == 4 ? UEP4_1_MOD : ep <= 3 ? UEP2_3_MOD : UEP567_MOD;
    const uint8_t bit = in ? tx_bit[ep] : rx_bit[ep];
    reg = (uint8_t)(on ? (reg | bit) : (reg & ~bit));
}

/* ------------------------------------------------------------ table */
static bool dcd_open(void)
{
    if (ch32rv_usbfs_irq_hook && ch32rv_usbfs_irq_hook != isr) {
        return false;                                    /* the USB host has the controller */
    }
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    /* PC16 (D-) floating input, PC17 (D+) input with pull-up, as the EVT does;
     * the PHY takes the pads once USB_IOEN is set. */
    pinMode(PC16, INPUT);
    pinMode(PC17, INPUT_PULLUP);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(AFIO_UDP_PUE_MASK | AFIO_UDM_PUE_MASK)) |
                       AFIO_USB_PHY_V33 | AFIO_USB_IOEN;
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    delayMicroseconds(10);
    BASE_CTRL = 0;
    memset(xin, 0, sizeof xin);
    memset(xout, 0, sizeof xout);
    UEP4_1_MOD = 0;
    UEP2_3_MOD = 0;
    UEP567_MOD = 0;
    R32(0x10) = (uint32_t)ep0_region;
    for (uint8_t ep = 1; ep < 8; ep++) {
        if (ep != 4) {
            R32(ep <= 3 ? 0x10u + 4u * ep : 0x54u + 4u * (ep - 5u)) = (uint32_t)out_buf(ep);
        }
        EP_CTL(ep) = T_NAK | R_NAK;
        EP_LEN(ep) = 0;
    }
    EP_CTL(0) = T_NAK | R_NAK;
    EP_LEN(0) = 0;
    xin[0].mps = xout[0].mps = 64;
    DEV_ADDR = 0;
    BASE_CTRL = UC_INT_BUSY | UC_DMA_EN;
    INT_FG = 0xFF;
    UDEV_CTRL = UD_PD_DIS | UD_PORT_EN;
    INT_EN = UIE_SUSPEND | UIE_BUS_RST | UIE_TRANSFER;
    ch32rv_usbfs_irq_hook = isr;
    ch32rv_irq_enable(CH32RV_IRQN_USBFS);
    return true;
}

static void dcd_connect(bool on)
{
    if (on) {
        BASE_CTRL = UC_DEV_PU_EN | UC_INT_BUSY | UC_DMA_EN;
        CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~AFIO_UDP_PUE_MASK) | AFIO_UDP_PUE_1K5;
    } else {
        CH32RV_AFIO_CTLR &= ~(AFIO_UDP_PUE_MASK | AFIO_UDM_PUE_MASK);
        BASE_CTRL = UC_INT_BUSY | UC_DMA_EN;
    }
}

static void dcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBFS);
    dcd_connect(false);
    CH32RV_AFIO_CTLR &= ~AFIO_USB_IOEN;
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    delayMicroseconds(10);
    BASE_CTRL = 0;
    if (ch32rv_usbfs_irq_hook == isr) {
        ch32rv_usbfs_irq_hook = nullptr;
    }
}

static CH32RVUsbSpeed dcd_speed(void)
{
    return CH32RVUsbSpeed::Full;
}

static void dcd_set_address(uint8_t address)
{
    DEV_ADDR = address;
}

static bool dcd_ep_open(uint8_t address, uint8_t type, uint16_t mps)
{
    const uint8_t ep = address & 0x0F;
    const bool in = address & 0x80;
    if (ep == 0 || ep > 7 || mps > 64) {
        return false;
    }
    Xfer &x = in ? xin[ep] : xout[ep];
    x = Xfer();
    x.mps = mps;
    x.iso = type == CH32RV_USB_XFER_ISOCHRONOUS;
    mode_bits(ep, in, true);
    if (in) {
        EP_LEN(ep) = 0;
        EP_CTL(ep) = (uint16_t)((EP_CTL(ep) & ~(T_MASK | T_TOG)) | (x.iso ? T_NONE : T_NAK));
    } else {
        EP_CTL(ep) = (uint16_t)((EP_CTL(ep) & ~(R_MASK | R_TOG)) | R_NAK);
    }
    return true;
}

static void dcd_ep_close(uint8_t address)
{
    const uint8_t ep = address & 0x0F;
    if (ep == 0 || ep > 7) {
        return;
    }
    const bool in = address & 0x80;
    noInterrupts();
    (in ? xin[ep] : xout[ep]).active = false;
    if (in) {
        set_t(ep, T_NAK);
        EP_LEN(ep) = 0;
    } else {
        set_r(ep, R_NAK);
    }
    mode_bits(ep, in, false);
    interrupts();
}

static void dcd_ep_close_all(void)
{
    for (uint8_t ep = 1; ep < 8; ep++) {
        xin[ep].active = xout[ep].active = false;
        EP_CTL(ep) = T_NAK | R_NAK;
        EP_LEN(ep) = 0;
    }
    UEP4_1_MOD = 0;
    UEP2_3_MOD = 0;
    UEP567_MOD = 0;
}

static bool dcd_xfer(uint8_t address, uint8_t *buffer, uint16_t length)
{
    const uint8_t ep = address & 0x0F;
    if (ep > 7) {
        return false;
    }
    noInterrupts();
    if (address & 0x80) {
        Xfer &x = xin[ep];
        x.buf = buffer;
        x.len = length;
        x.done = 0;
        x.active = true;
        load_in(ep);
    } else {
        Xfer &x = xout[ep];
        x.buf = buffer;
        x.len = length;
        x.done = 0;
        x.active = true;
        set_r(ep, x.iso ? R_NONE : R_ACK);
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
        EP_LEN(ep) = 0;
        if ((EP_CTL(ep) & T_MASK) != T_STALL) {
            set_t(ep, xin[ep].iso ? T_NONE : T_NAK);
        }
    } else {
        xout[ep].active = false;
        if ((EP_CTL(ep) & R_MASK) != R_STALL) {
            set_r(ep, xout[ep].iso ? R_NONE : R_NAK);
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
    noInterrupts();
    if (address & 0x80) {
        if (on) {
            set_t(ep, T_STALL);
        } else {
            EP_CTL(ep) = (uint16_t)((EP_CTL(ep) & ~(T_MASK | T_TOG)) |
                                    (xin[ep].active ? (xin[ep].iso ? T_NONE : T_ACK) : T_NAK));
        }
    } else {
        if (on) {
            set_r(ep, R_STALL);
        } else {
            EP_CTL(ep) = (uint16_t)((EP_CTL(ep) & ~(R_MASK | R_TOG)) |
                                    (xout[ep].active ? (xout[ep].iso ? R_NONE : R_ACK) : R_NAK));
        }
    }
    interrupts();
}

static void dcd_ep0_stall(void)
{
    EP_CTL(0) = T_TOG | T_STALL | R_TOG | R_STALL;
    xin[0].active = xout[0].active = false;
}

/* Remote wakeup: the K state for 8 ms by moving the pull-up from D+ to D-
 * (the EVT's USBFS_Send_Resume). */
static void dcd_remote_wakeup(void)
{
    pinMode(PC16, INPUT_PULLUP);
    pinMode(PC17, INPUT_PULLDOWN);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~AFIO_UDP_PUE_MASK) | AFIO_UDM_PUE_1K5;
    const uint32_t t0 = micros();
    while ((uint32_t)(micros() - t0) < 8000u) {
    }
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~AFIO_UDM_PUE_MASK) | AFIO_UDP_PUE_1K5;
    pinMode(PC16, INPUT);
    pinMode(PC17, INPUT_PULLUP);
}

/* ------------------------------------------------------------ interrupt */
static void isr(void)
{
    const uint8_t fg = INT_FG, st = INT_ST;
    if (fg & UIE_TRANSFER) {
        const uint8_t ep = st & 0x0F;
        switch (st & UIS_TOKEN) {
        case TOKEN_SETUP:
            EP_CTL(0) = T_TOG | T_NAK | R_TOG | R_NAK;   /* data stage DATA1 both ways */
            xin[0].active = xout[0].active = false;
            ch32rv_usbd_isr_setup(ep0_region);
            break;
        case TOKEN_IN:
            if (ep < 8) {
                Xfer &x = xin[ep];
                if (!x.iso) {
                    EP_CTL(ep) ^= T_TOG;
                }
                if (!x.active) {
                    set_t(ep, x.iso ? T_NONE : T_NAK);
                    break;
                }
                x.done = (uint16_t)(x.done + x.last);
                if (x.done < x.len) {
                    load_in(ep);
                } else {
                    x.active = false;
                    if (x.iso) {
                        EP_LEN(ep) = 0;                  /* nothing queued: send empty, not stale */
                    } else {
                        set_t(ep, T_NAK);
                    }
                    ch32rv_usbd_isr_xfer((uint8_t)(0x80 | ep), x.done);
                }
            }
            break;
        case TOKEN_OUT:
            if (ep < 8) {
                Xfer &x = xout[ep];
                if (!x.iso && !(st & UIS_TOG_OK)) {
                    break;                               /* a repeated packet: already taken */
                }
                if (!x.iso) {
                    EP_CTL(ep) ^= R_TOG;
                }
                if (!x.active) {
                    set_r(ep, x.iso ? R_NONE : R_NAK);
                    break;
                }
                const uint16_t n = RX_LEN;
                const uint16_t room = (uint16_t)(x.len - x.done);
                const uint16_t take = n < room ? n : room;
                if (take) {
                    memcpy(x.buf + x.done, out_buf(ep), take);
                }
                x.done = (uint16_t)(x.done + take);
                if (n < x.mps || x.done >= x.len) {
                    x.active = false;
                    set_r(ep, x.iso ? R_NONE : R_NAK);
                    ch32rv_usbd_isr_xfer(ep, x.done);
                }
            }
            break;
        }
        INT_FG = UIE_TRANSFER;
    } else if (fg & UIE_BUS_RST) {
        DEV_ADDR = 0;
        for (uint8_t ep = 0; ep < 8; ep++) {
            xin[ep].active = xout[ep].active = false;
            EP_CTL(ep) = T_NAK | R_NAK;
            EP_LEN(ep) = 0;
        }
        INT_FG = UIE_BUS_RST;
        ch32rv_usbd_isr_reset();
    } else if (fg & UIE_SUSPEND) {
        INT_FG = UIE_SUSPEND;
        if (MIS_ST & UMS_SUSPEND) {
            ch32rv_usbd_isr_suspend();
        } else {
            ch32rv_usbd_isr_resume();
        }
    } else {
        INT_FG = fg;
    }
}

const CH32RVUsbDcd ch32rv_usb_dcd_usbfs_x03x = {
    "USBFS (CH32X035/X033)", 8, 64, 1u << (uint8_t)CH32RVUsbSpeed::Full,
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup,
};

#endif
