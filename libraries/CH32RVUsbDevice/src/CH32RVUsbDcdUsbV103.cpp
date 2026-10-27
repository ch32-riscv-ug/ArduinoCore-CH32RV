/* CH32V103 USB (the older USBFS IP, "USBHD" in its EVT), device role
 * (CH32RVUsbDcd.h).
 *
 * Map from the EVT ch32v10x_usb.h (base 0x40023400): CTRL 0x00, UDEV_CTRL
 * 0x01, INT_EN 0x02, DEV_AD 0x03, MIS_ST 0x05, INT_FG 0x06, INT_ST 0x07,
 * RX_LEN 0x08, mode bytes UEP4_1 0x0C / UEP2_3 0x0D / UEP5_6 0x0E / UEP7
 * 0x0F, a DMA address per endpoint at 0x10 + 4n holding only the low 16 bits
 * of the SRAM address, and per endpoint at 0x30 + 4n the transmit length and
 * one control byte (+2) with the X035's bits: T_RES 1:0, R_RES 3:2, AUTO_TOG
 * 4, T_TOG 6, R_TOG 7. So the X035 driver's logic (CH32RVUsbDcdUsbfsX03x.cpp)
 * with this map: an endpoint with both directions takes OUT at its buffer and
 * IN at buffer + 64.
 *
 * Clock: RCC_CFGR0 bit 22 USBPRE, 0 = PLL / 1.5, 1 = PLL / 1 (72 MHz or 48
 * MHz SYSCLK). EXTEN_CTR (0x40023800) USBFS_IO_EN (bit 2) hands PA11 / PA12 to
 * the PHY; USB_5V_SEL (bit 3) stays clear for a 3.3 V supply. Build only. */
#if defined(CH32RV_SERIES_CH32V103)

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
#define UEP5_6_MOD R8(0x0E)
#define UEP7_MOD   R8(0x0F)
#define EP_DMA(ep) R16(0x10u + 4u * (ep))

static inline uint32_t len_off(uint8_t ep) { return 0x30u + 4u * ep; }
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

#define EXTEN_CTR        CH32RV_REG32(0x40023800u)
#define EXTEN_USBFS_IO_EN 0x00000004u
#define EXTEN_USB_5V_SEL  0x00000008u

/* EP0's 64 bytes, then 128 for each of EP1-7: OUT, then IN. */
static uint8_t ep0_region[64] __attribute__((aligned(4)));
static uint8_t ep_region[7][128] __attribute__((aligned(4)));

static inline uint8_t *out_buf(uint8_t ep) { return ep ? ep_region[ep - 1] : ep0_region; }
static inline uint8_t *in_buf(uint8_t ep) { return ep ? ep_region[ep - 1] + 64 : ep0_region; }

namespace {
struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint8_t last;                 /* bytes in the IN packet in flight */
    bool active, iso;
    uint8_t held;                                    /* iso OUT: a packet that came while none was asked for */
};
}  // namespace
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

/* Both directions' enable bits go on together, even for an endpoint used one
 * way: with only TX enabled the SIE takes the IN data from the start of the
 * region instead of +64, which would move the buffer under the driver. The
 * unused direction stays NAK. */
static uint8_t opened_in, opened_out;

static void mode_bits(uint8_t ep, bool in, bool on)
{
    static const uint8_t off[8] = {0, 0x0C, 0x0D, 0x0D, 0x0C, 0x0E, 0x0E, 0x0F};
    static const uint8_t both[8] = {0, 0xC0, 0x0C, 0xC0, 0x0C, 0x0C, 0xC0, 0x0C};
    volatile uint8_t &reg = R8(off[ep]);
    const uint8_t bit = (uint8_t)(1u << ep);
    uint8_t &opened = in ? opened_in : opened_out;
    opened = (uint8_t)(on ? (opened | bit) : (opened & ~bit));
    const bool any = ((opened_in | opened_out) & bit) != 0;
    reg = (uint8_t)(any ? (reg | both[ep]) : (reg & ~both[ep]));
}

/* ------------------------------------------------------------ table */
static bool dcd_open(void)
{
    if (ch32rv_usbfs_irq_hook && ch32rv_usbfs_irq_hook != isr) {
        return false;                                    /* the USB host has the controller */
    }
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 72000000
    CH32RV_RCC_CFGR0 &= ~(1u << 22);                     /* PLL / 1.5 */
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
    CH32RV_RCC_CFGR0 |= 1u << 22;                        /* PLL / 1 */
#else
    return false;                                        /* no 48 MHz from this clock */
#endif
    pinMode(PA11, INPUT);
    pinMode(PA12, INPUT);
    EXTEN_CTR = (EXTEN_CTR & ~EXTEN_USB_5V_SEL) | EXTEN_USBFS_IO_EN;
    ch32rv_clock_enable(USBFS);
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    delayMicroseconds(10);
    BASE_CTRL = 0;
    memset(xin, 0, sizeof xin);
    memset(xout, 0, sizeof xout);
    UEP4_1_MOD = 0;
    UEP2_3_MOD = 0;
    UEP5_6_MOD = 0;
    UEP7_MOD = 0;
    opened_in = opened_out = 0;
    for (uint8_t ep = 0; ep < 8; ep++) {
        EP_DMA(ep) = (uint16_t)(uintptr_t)out_buf(ep);   /* the low half of the SRAM address */
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
    BASE_CTRL = (uint8_t)((on ? UC_DEV_PU_EN : 0) | UC_INT_BUSY | UC_DMA_EN);
}

static void dcd_close(void)
{
    ch32rv_irq_disable(CH32RV_IRQN_USBFS);
    dcd_connect(false);
    EXTEN_CTR &= ~EXTEN_USBFS_IO_EN;
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
    UEP5_6_MOD = 0;
    UEP7_MOD = 0;
    opened_in = opened_out = 0;
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
        if (x.iso && x.held) {         /* a packet is already here */
            const uint16_t n = x.held < length ? x.held : length;
            memcpy(buffer, out_buf(ep), n);
            x.held = 0;
            ch32rv_usbd_isr_xfer(ep, n);   /* as from the interrupt, which is masked here */
            interrupts();
            return true;
        }
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

/* Remote wakeup: the EVT has no resume for this IP; not offered. */
static void dcd_remote_wakeup(void)
{
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
                    if (x.iso) {
                        /* Kept in the DMA buffer for the next xfer(): the stack
                         * re-arms from the task, and a packet a frame is not
                         * waited for. A newer one replaces it. */
                        x.held = (uint8_t)(RX_LEN > x.mps ? x.mps : RX_LEN);
                    }
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

const CH32RVUsbDcd ch32rv_usb_dcd_usb_v103 = {
    "USB (CH32V103)", 8, 64, 1u << (uint8_t)CH32RVUsbSpeed::Full,
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup, nullptr,
};

#endif
