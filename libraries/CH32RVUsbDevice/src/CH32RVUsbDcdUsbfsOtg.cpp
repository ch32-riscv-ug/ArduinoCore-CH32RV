/* USBFS of the USBOTG_FS layout, device role (CH32RVUsbDcd.h): CH32V20x
 * (V203 / V208), CH32V30x (V303 / V305 / V307 / V317), CH32V205, CH32L103 /
 * M103, CH32M030 and CH32H41x - one register map across them (EVT
 * USBFSD_TypeDef, compared field by field 2026-10-21), at 0x50000000 or, on
 * M030 and H41x, 0x40023400.
 *
 * The same SIE as the X035 / X033 USBFS (CH32RVUsbDcdUsbfsX03x.cpp): the
 * control bits, the INT_BUSY auto-NAK and the response encodings match. What
 * differs is the map: a DMA address per endpoint 0-7 at 0x10 + 4n, then per
 * endpoint at 0x30 + 4n the transmit length (+0), the transmit control byte
 * (+2) and the receive control byte (+3), the toggle at bit 2 of each control
 * byte; enable bits in UEP4_1_MOD, UEP2_3_MOD, UEP5_6_MOD and UEP7_MOD. An
 * endpoint with both directions takes OUT at its buffer and IN at buffer + 64;
 * every buffer holds one 64-byte packet.
 *
 * Per series below: the base, the 48 MHz clock and the pads. Measured on
 * CH32H417 only (2026-10-20/21); the rest are built from their EVTs. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_SERIES_CH32V303) || \
    defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103) || \
    defined(CH32RV_SERIES_CH32M030)

#include <Arduino.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "ch32rv_usbfs_irq.h"
#include "CH32RVUsbDcd.h"

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32M030)
#define USB_BASE 0x40023400u
#else
#define USB_BASE 0x50000000u
#endif
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
#define EP_DMA(ep) R32(0x10u + 4u * (ep))
#define EP_TLEN(ep) R16(0x30u + 4u * (ep))
#define EP_TCTL(ep) R8(0x32u + 4u * (ep))
#define EP_RCTL(ep) R8(0x33u + 4u * (ep))

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

#define RES_MASK  0x03u
#define RES_ACK   0x00u
#define RES_NONE  0x01u
#define RES_NAK   0x02u
#define RES_STALL 0x03u
#define TOG       0x04u

/* EP0's 64 bytes, then two 64-byte halves (OUT, IN) for each of EP1-7. */
static uint8_t ep0_buf[64] __attribute__((aligned(4)));
static uint8_t ep_buf[7][128] __attribute__((aligned(4)));

static inline uint8_t *out_buf(uint8_t ep) { return ep ? ep_buf[ep - 1] : ep0_buf; }
static inline uint8_t *in_buf(uint8_t ep) { return ep ? ep_buf[ep - 1] + 64 : ep0_buf; }

struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint8_t last;
    bool active, iso;
    uint8_t held;
};
static Xfer xin[8], xout[8];

static void isr(void);

/* ------------------------------------------------------------ per series */
static bool wait_ctlr(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
/* 48 MHz = USBHS PLL (480 MHz from HSE, 25 MHz reference) / 10, selected in
 * RCC_CFGR2 (USBFSSRC bit 20, USBFSDIV 19:16 = 7 for /10). PA11 / PA12 need
 * nothing but the port clock (EVT USBFS_RCC_Init). */
#define RCC_CTLR_HSEON        (1u << 16)
#define RCC_CTLR_HSERDY       (1u << 17)
#define RCC_CTLR_USBHS_PLLON  (1u << 20)
#define RCC_CTLR_USBHS_PLLRDY (1u << 21)
#define RCC_CFGR2             CH32RV_REG32(0x40021030u)
#define RCC_PLLCFGR2          CH32RV_REG32(0x40021034u)
#define USB_CLKEN()           (ch32rv_clock_enable(OTG_FS), ch32rv_clock_enable(GPIOA))

static bool clock_ok(void)
{
    return (CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY) &&
           (RCC_CFGR2 & 0x001F0000u) == ((1u << 20) | (7u << 16));
}

static bool clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY)) {
        CH32RV_RCC_CTLR |= RCC_CTLR_HSEON;
        if (!wait_ctlr(RCC_CTLR_HSERDY)) {
            return false;                                /* no crystal */
        }
        /* Source HSE, reference 25 MHz, no input division: writable only
         * while the PLL is off. */
        CH32RV_RCC_CTLR &= ~RCC_CTLR_USBHS_PLLON;
        RCC_PLLCFGR2 &= ~0x00001F0Fu;
        CH32RV_RCC_CTLR |= RCC_CTLR_USBHS_PLLON;
        if (!wait_ctlr(RCC_CTLR_USBHS_PLLRDY)) {
            return false;
        }
    }
    RCC_CFGR2 = (RCC_CFGR2 & ~0x001F0000u) | (1u << 20) | (7u << 16);
    USB_CLKEN();
    return true;
}

#elif defined(CH32RV_SERIES_CH32V205)
/* 48 MHz from the USBHS PLL with an 8 MHz reference, fed by HSE when one runs,
 * else by HSI (EVT USBFS_RCC_Init); RCC_CFGR2 (+0x2C) USBFSSRC bit 31 picks it.
 * The SYSCLK may stay where the board put it. PA11 / PA12. */
#define RCC_CTLR_HSERDY       (1u << 17)
#define RCC_CTLR_USBHS_PLLON  (1u << 22)
#define RCC_CTLR_USBHS_PLLRDY (1u << 23)
#define RCC_CFGR2             CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)
#define CFGR2_USBFSSRC_HSPLL  (1u << 31)
#define CFGR2_HSPLL_REF_8M    (1u << 28)
#define CFGR2_HSPLL_REF_MASK  (3u << 28)
#define CFGR2_HSPLL_SRC_HSI   (1u << 27)
#define CFGR2_HSPLL_DIV_MASK  (7u << 24)
#define USB_CLKEN()           ch32rv_clock_enable(USBFS)

static bool clock_ok(void)
{
    return (CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY) && (RCC_CFGR2 & CFGR2_USBFSSRC_HSPLL);
}

static bool clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & RCC_CTLR_USBHS_PLLRDY)) {
        CH32RV_RCC_CTLR &= ~RCC_CTLR_USBHS_PLLON;
        RCC_CFGR2 = (RCC_CFGR2 & ~(CFGR2_HSPLL_REF_MASK | CFGR2_HSPLL_SRC_HSI | CFGR2_HSPLL_DIV_MASK)) |
                    CFGR2_HSPLL_REF_8M | ((CH32RV_RCC_CTLR & RCC_CTLR_HSERDY) ? 0u : CFGR2_HSPLL_SRC_HSI);
        CH32RV_RCC_CTLR |= RCC_CTLR_USBHS_PLLON;
        if (!wait_ctlr(RCC_CTLR_USBHS_PLLRDY)) {
            return false;
        }
    }
    RCC_CFGR2 |= CFGR2_USBFSSRC_HSPLL;
    USB_CLKEN();
    return true;
}

#elif defined(CH32RV_SERIES_CH32M030)
/* The PLL is HSI x 18 = 144 MHz, fixed, and its /3 is the USBFS clock (RM
 * figure 3-2). It runs here whatever SYSCLK is: turned on, never switched to. */
#define RCC_CTLR_PLLON  (1u << 24)
#define RCC_CTLR_PLLRDY (1u << 25)
#define USB_CLKEN()     ch32rv_clock_enable(USBFS)

static bool clock_ok(void)
{
    return (CH32RV_RCC_CTLR & RCC_CTLR_PLLRDY) != 0;
}

static bool clock_on(void)
{
    if (!(CH32RV_RCC_CTLR & RCC_CTLR_PLLRDY)) {
        CH32RV_RCC_CFGR0 &= ~(1u << 16);                 /* PLLSRC: HSI */
        CH32RV_RCC_CTLR |= RCC_CTLR_PLLON;
        if (!wait_ctlr(RCC_CTLR_PLLRDY)) {
            return false;
        }
    }
    USB_CLKEN();
    return true;
}

#else
/* V20x, V30x, L103 / M103: the 48 MHz is the PLL divided by USBPRE
 * (RCC_CFGR0 23:22), so it exists only when SYSCLK is that PLL at a multiple
 * of 48 MHz. The encodings differ: V20x / V30x 00 /1, 01 /2, 10 /3; L103
 * 00 /1, 01 /2, 10 /1.5 (EVT RCC_USBCLKConfig). At the boards' default 8 MHz
 * of L103 / M103 there is none, and begin() fails. */
#if CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 48000000
#define USBPRE 0u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 96000000
#define USBPRE 1u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 144000000 && \
    !(defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103))
#define USBPRE 2u
#elif CH32RV_CLOCK_USE_PLL && CH32RV_CLOCK_SYSCLK_HZ == 72000000 && \
    (defined(CH32RV_SERIES_CH32L103) || defined(CH32RV_SERIES_CH32M103))
#define USBPRE 2u
#endif
#if defined(CH32RV_SERIES_CH32V203) || defined(CH32RV_SERIES_CH32V208)
#define DP_PIN PB7                                       /* USBFS sits on PB6 / PB7 there */
#define DM_PIN PB6
#else
#define DP_PIN PA12
#define DM_PIN PA11
#endif
#define USB_CLKEN() ch32rv_clock_enable(USBFS)

static bool clock_ok(void)
{
#ifdef USBPRE
    return ((CH32RV_RCC_CFGR0 >> 22) & 3u) == USBPRE;
#else
    return false;
#endif
}

static bool clock_on(void)
{
#ifdef USBPRE
    CH32RV_RCC_CFGR0 = (CH32RV_RCC_CFGR0 & ~(3u << 22)) | (USBPRE << 22);
    pinMode(DM_PIN, INPUT);
    pinMode(DP_PIN, INPUT);
    USB_CLKEN();
    return true;
#else
    return false;
#endif
}
#endif

/* SystemInit() runs again whenever a debug probe has rewritten RCC
 * (wiring_time.c), and its EVT reset sequence turns the extra oscillators and
 * PLLs off: USB stops dead while staying on the bus (seen on H417 after a
 * `ch32rv verify`). Put the clock back from the task. */
static void dcd_poll(void)
{
    if (!clock_ok()) {
        clock_on();
    }
}

/* ------------------------------------------------------------ helpers */
static void set_t(uint8_t ep, uint8_t res)
{
    EP_TCTL(ep) = (uint8_t)((EP_TCTL(ep) & ~RES_MASK) | res);
}

static void set_r(uint8_t ep, uint8_t res)
{
    EP_RCTL(ep) = (uint8_t)((EP_RCTL(ep) & ~RES_MASK) | res);
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
    EP_TLEN(ep) = n;
    set_t(ep, x.iso ? RES_NONE : RES_ACK);
}

/* Both directions' enable bits go on together, as on the X035: the IN half
 * then sits at +64 whichever direction the endpoint uses. */
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
    if (!clock_on()) {
        return false;
    }
    BASE_CTRL = UC_RESET_SIE | UC_CLR_ALL;
    delayMicroseconds(10);
    BASE_CTRL = 0;
    memset(xin, 0, sizeof xin);
    memset(xout, 0, sizeof xout);
    R8(0x0C) = R8(0x0D) = R8(0x0E) = R8(0x0F) = 0;
    opened_in = opened_out = 0;
    for (uint8_t ep = 0; ep < 8; ep++) {
        EP_DMA(ep) = (uint32_t)(uintptr_t)out_buf(ep);
        EP_TCTL(ep) = RES_NAK;
        EP_RCTL(ep) = RES_NAK;
        EP_TLEN(ep) = 0;
    }
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
    UDEV_CTRL = 0;
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
        EP_TLEN(ep) = 0;
        EP_TCTL(ep) = x.iso ? RES_NONE : RES_NAK;
    } else {
        EP_RCTL(ep) = RES_NAK;
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
        EP_TCTL(ep) = RES_NAK;
        EP_TLEN(ep) = 0;
    } else {
        EP_RCTL(ep) = RES_NAK;
    }
    mode_bits(ep, in, false);
    interrupts();
}

static void dcd_ep_close_all(void)
{
    for (uint8_t ep = 1; ep < 8; ep++) {
        xin[ep].active = xout[ep].active = false;
        EP_TCTL(ep) = RES_NAK;
        EP_RCTL(ep) = RES_NAK;
        EP_TLEN(ep) = 0;
    }
    R8(0x0C) = R8(0x0D) = R8(0x0E) = R8(0x0F) = 0;
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
        if (x.iso && x.held) {
            const uint16_t n = x.held < length ? x.held : length;
            memcpy(buffer, out_buf(ep), n);
            x.held = 0;
            ch32rv_usbd_isr_xfer(ep, n);
            interrupts();
            return true;
        }
        x.active = true;
        set_r(ep, x.iso ? RES_NONE : RES_ACK);
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
        EP_TLEN(ep) = 0;
        if ((EP_TCTL(ep) & RES_MASK) != RES_STALL) {
            set_t(ep, xin[ep].iso ? RES_NONE : RES_NAK);
        }
    } else {
        xout[ep].active = false;
        if ((EP_RCTL(ep) & RES_MASK) != RES_STALL) {
            set_r(ep, xout[ep].iso ? RES_NONE : RES_NAK);
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
            set_t(ep, RES_STALL);
        } else {
            EP_TCTL(ep) = (uint8_t)(xin[ep].active ? (xin[ep].iso ? RES_NONE : RES_ACK) : RES_NAK);
        }
    } else {
        if (on) {
            set_r(ep, RES_STALL);
        } else {
            EP_RCTL(ep) = (uint8_t)(xout[ep].active ? (xout[ep].iso ? RES_NONE : RES_ACK) : RES_NAK);
        }
    }
    interrupts();
}

static void dcd_ep0_stall(void)
{
    EP_TCTL(0) = TOG | RES_STALL;
    EP_RCTL(0) = TOG | RES_STALL;
    xin[0].active = xout[0].active = false;
}

/* Remote wakeup: the RM gives this USBFS no resume bit; not offered. */
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
            EP_TCTL(0) = TOG | RES_NAK;                  /* data stage DATA1 both ways */
            EP_RCTL(0) = TOG | RES_NAK;
            xin[0].active = xout[0].active = false;
            ch32rv_usbd_isr_setup(ep0_buf);
            break;
        case TOKEN_IN:
            if (ep < 8) {
                Xfer &x = xin[ep];
                if (!x.iso) {
                    EP_TCTL(ep) ^= TOG;
                }
                if (!x.active) {
                    set_t(ep, x.iso ? RES_NONE : RES_NAK);
                    break;
                }
                x.done = (uint16_t)(x.done + x.last);
                if (x.done < x.len) {
                    load_in(ep);
                } else {
                    x.active = false;
                    if (x.iso) {
                        EP_TLEN(ep) = 0;
                    } else {
                        set_t(ep, RES_NAK);
                    }
                    ch32rv_usbd_isr_xfer((uint8_t)(0x80 | ep), x.done);
                }
            }
            break;
        case TOKEN_OUT:
            if (ep < 8) {
                Xfer &x = xout[ep];
                if (!x.iso && !(st & UIS_TOG_OK)) {
                    break;
                }
                if (!x.iso) {
                    EP_RCTL(ep) ^= TOG;
                }
                if (!x.active) {
                    if (x.iso) {
                        x.held = (uint8_t)(RX_LEN > x.mps ? x.mps : RX_LEN);
                    }
                    set_r(ep, x.iso ? RES_NONE : RES_NAK);
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
                    set_r(ep, x.iso ? RES_NONE : RES_NAK);
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
            EP_TCTL(ep) = RES_NAK;
            EP_RCTL(ep) = RES_NAK;
            EP_TLEN(ep) = 0;
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

const CH32RVUsbDcd ch32rv_usb_dcd_usbfs_otg = {
    "USBFS (USBOTG_FS)", 8, 64, 1u << (uint8_t)CH32RVUsbSpeed::Full,
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup,
    dcd_poll,
};

#endif
