/* USBHS, WCH's second-generation IP (USB 2.0 high speed), device role
 * (CH32RVUsbDcd.h): CH32H41x (0x40030000), CH32V205 (0x40023400), CH32X305 /
 * X315 (0x40030000) and CH32V407 / V467, which have two (USBHS1 at 0x40024000,
 * USBHS2 at 0x40023400). One register map (EVT USBHSD_TypeDef, the same struct
 * in all four EVTs); also on CH564 / CH585 per TinyUSB. Not the V30x USBHS.
 *
 * Per endpoint: a DMA address each way (RX 0x24 + 4 (n-1), TX 0x40 + 4 (n-1),
 * EP0 one at 0x20), a maximum receive length (0x5C + 4n), the receive length
 * (0x7C, 0x80 + 4 (n-1)), and a transmit length / transmit control / receive
 * control at 0x9C + 4n. Control bytes: DONE bit 7 (cleared by writing 0),
 * toggle bit 2 (TX, EP0 RX) or bits 3:2 (EPn RX), response 1:0 with NAK 00 /
 * STALL 01 / ACK 10; EP0's RX has SETUP_IS at bit 3 and every RX TOG_MATCH at
 * bit 4 (RM chapter 25 of CH32H417). Toggles are kept by software.
 *
 * The interrupt handling follows TinyUSB PR #3779 (dcd_ch32h417_usbhs.c,
 * measured on H417 there; unmerged as of 2026-10): INT_ST names one endpoint
 * while the transfer flag is the OR of every DONE, and a SETUP that arrives
 * while any DONE is pending gets no handshake, so the handler drains all of
 * them; a response change writes DONE back as 1 so a completion that lands
 * meanwhile is not lost; SUSPEND fires constantly and only changes are
 * reported; RX_SOF is set every frame and always cleared. TinyUSB still saw
 * about a third of the SETUPs go unanswered and suspects this IP lacks the
 * USBFS INT_BUSY auto-NAK.
 *
 * Per series below: the clock. On H41x PB8 / PB9 are also SWCLK / SWDIO;
 * opening the driver there turns the debug interface off (AFIO_PCFR1.SW_CFG =
 * 100b, the EVT's GPIO_Remap_SWJ_Disable): a probe gets the chip back only
 * through a reset (ch32rv --connect-under-reset, with NRST wired), and
 * sketches are best written by DFU. Build only everywhere so far. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || \
    defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "CH32RVUsbDcd.h"

namespace {

/* Register offsets (one controller). */
enum : uint32_t {
    O_CONTROL = 0x00, O_BASE_MODE = 0x01, O_INT_EN = 0x02, O_DEV_AD = 0x03, O_WAKE_CTRL = 0x04,
    O_INT_FG = 0x08, O_MIS_ST = 0x0A, O_UEP_TX_EN = 0x10, O_UEP_RX_EN = 0x12, O_UEP0_DMA = 0x20,
    O_UEP_TX_ISO = 0xBC, O_UEP_RX_ISO = 0xBE,
};

constexpr uint8_t UD_LPM_EN = 0x80, UD_DEV_EN = 0x20, UD_DMA_EN = 0x10, UD_PHY_SUSPENDM = 0x08, UD_RST_SIE = 0x02,
                  UD_RST_LINK = 0x01, UD_SPEED_HIGH = 0x01;
constexpr uint8_t UDIE_LINK_RDY = 0x40, UDIE_SOF = 0x20, UDIE_TRANSFER = 0x10, UDIE_LPM_ACT = 0x08,
                  UDIE_BUS_SLEEP = 0x04, UDIE_SUSPEND = 0x02, UDIE_BUS_RST = 0x01;
constexpr uint8_t UDMS_HS_MOD = 0x80, UDMS_SUSPEND = 0x02;
constexpr uint8_t DONE = 0x80, TOG_MATCH = 0x10, SETUP_IS = 0x08, TOG_MASK = 0x0C, TOG1 = 0x04, RES_MASK = 0x03,
                  RES_NAK = 0x00, RES_STALL = 0x01, RES_ACK = 0x02;

struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t mps;
    uint16_t last;               /* bytes in the packet in flight */
    uint8_t *bounce;             /* one packet, DMA-side */
    bool active, iso, tog;
};

bool wait_ctlr(uint32_t bit)
{
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & bit);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    return true;
}

/* ------------------------------------------------------------ per series: clocks */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415)
/* The USBHS PLL from HSE (25 MHz reference), UTMI (EVT USBHS_RCC_Init). The
 * PHY pads are PB8 / PB9, which are SWCLK / SWDIO: SDI goes off. */
constexpr uint32_t CTLR_HSEON = 1u << 16, CTLR_HSERDY = 1u << 17, CTLR_PLLON = 1u << 20, CTLR_PLLRDY = 1u << 21;
constexpr uint32_t CFGR0_UTMION = 1u << 23;
#define RCC_PLLCFGR2 CH32RV_REG32(0x40021034u)

bool clock_ok(uint8_t)
{
    return (CH32RV_RCC_CTLR & CTLR_PLLRDY) && (CH32RV_RCC_CFGR0 & CFGR0_UTMION);
}

bool clock_on(uint8_t)
{
    if (!(CH32RV_RCC_CTLR & CTLR_PLLRDY)) {
        CH32RV_RCC_CTLR |= CTLR_HSEON;
        if (!wait_ctlr(CTLR_HSERDY)) {
            return false;
        }
        CH32RV_RCC_CTLR &= ~CTLR_PLLON;
        RCC_PLLCFGR2 &= ~0x00001F0Fu;                    /* HSE, 25 MHz reference, no input division */
        CH32RV_RCC_CTLR |= CTLR_PLLON;
        if (!wait_ctlr(CTLR_PLLRDY)) {
            return false;
        }
    }
    CH32RV_RCC_CFGR0 |= CFGR0_UTMION;
    ch32rv_clock_enable(USBHS);
    return true;
}

void pads_on(uint8_t)
{
    CH32RV_REG32(CH32RV_AFIO_BASE + 0x00u) = (CH32RV_REG32(CH32RV_AFIO_BASE + 0x00u) & ~(7u << 24)) | (4u << 24);
}

#elif defined(CH32RV_SERIES_CH32V205)
/* The USBHS PLL from HSE with an 8 MHz reference (EVT USBHS_RCC_Init):
 * RCC_CTLR 22 / 23, RCC_CFGR2 (+0x2C) 27 source, 29:28 reference, 26:24
 * input division. High speed needs the crystal. */
constexpr uint32_t CTLR_HSEON = 1u << 16, CTLR_HSERDY = 1u << 17, CTLR_PLLON = 1u << 22, CTLR_PLLRDY = 1u << 23;
#define RCC_CFGR2 CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)

bool clock_ok(uint8_t)
{
    return (CH32RV_RCC_CTLR & CTLR_PLLRDY) && !(RCC_CFGR2 & (1u << 27));
}

bool clock_on(uint8_t)
{
    if (!clock_ok(0)) {
        CH32RV_RCC_CTLR |= CTLR_HSEON;
        if (!wait_ctlr(CTLR_HSERDY)) {
            return false;
        }
        CH32RV_RCC_CTLR &= ~CTLR_PLLON;
        RCC_CFGR2 = (RCC_CFGR2 & ~((3u << 28) | (1u << 27) | (7u << 24))) | (1u << 28);
        CH32RV_RCC_CTLR |= CTLR_PLLON;
        if (!wait_ctlr(CTLR_PLLRDY)) {
            return false;
        }
    }
    ch32rv_clock_enable(USBHS);
    return true;
}

void pads_on(uint8_t) {}

#elif defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315)
/* UTMI (RCC_CFGR0 23) and the controller clock; without HSE the PHY runs from
 * HSI kept in step with the host's SOF by USBHSI (base + 0x200: CAL_RST 0,
 * CAL_EN 1, SOF_FREE 3) - EVT USBHS_RCC_Init. */
constexpr uint32_t CTLR_HSERDY = 1u << 17, CFGR0_UTMION = 1u << 23;
#define USBHSI_CAL_CR CH32RV_REG32(0x40030200u)

bool clock_ok(uint8_t)
{
    return (CH32RV_RCC_CFGR0 & CFGR0_UTMION) != 0;
}

bool clock_on(uint8_t)
{
    CH32RV_RCC_CFGR0 |= CFGR0_UTMION;
    ch32rv_clock_enable(USBHS);
    if (!(CH32RV_RCC_CTLR & CTLR_HSERDY)) {
        USBHSI_CAL_CR |= 0x01u;
        USBHSI_CAL_CR &= ~0x01u;
        USBHSI_CAL_CR |= 0x02u | 0x08u;
    }
    return true;
}

void pads_on(uint8_t) {}

#else /* CH32V407 / V467 */
/* One USBHS PLL for both: HSE with a 25 MHz reference, or HSI with 20 MHz
 * (EVT usbhsN_dcp.c), RCC_CTLR 20 / 21, RCC_CFGR2 (+0x2C) 27:26 source,
 * 29:28 reference; each controller its UTMI gate, USBHS1 at CFGR2 30 and
 * USBHS2 at 31 (RM; the EVT header has the two names swapped, its functions
 * do not). */
constexpr uint32_t CTLR_HSERDY = 1u << 17, CTLR_PLLON = 1u << 20, CTLR_PLLRDY = 1u << 21;
#define RCC_CFGR2 CH32RV_REG32(CH32RV_RCC_BASE + 0x2Cu)

constexpr uint32_t utmi(uint8_t unit)
{
    return unit == 1 ? (1u << 30) : (1u << 31);
}

bool clock_ok(uint8_t unit)
{
    return (CH32RV_RCC_CTLR & CTLR_PLLRDY) && (RCC_CFGR2 & utmi(unit));
}

bool clock_on(uint8_t unit)
{
    if (!(CH32RV_RCC_CTLR & CTLR_PLLRDY)) {
        CH32RV_RCC_CTLR &= ~CTLR_PLLON;
        const bool hse = (CH32RV_RCC_CTLR & CTLR_HSERDY) != 0;
        RCC_CFGR2 = (RCC_CFGR2 & ~((3u << 28) | (3u << 26))) | (hse ? 0u : (1u << 28) | (1u << 26));
        CH32RV_RCC_CTLR |= CTLR_PLLON;
        if (!wait_ctlr(CTLR_PLLRDY)) {
            return false;
        }
    }
    RCC_CFGR2 |= utmi(unit);
    if (unit == 1) {
        ch32rv_clock_enable(USBHS1);
    } else {
        ch32rv_clock_enable(USBHS2);
    }
    return true;
}

void pads_on(uint8_t) {}
#endif

/* ------------------------------------------------------------ one controller */
template <uint32_t BASE, uint8_t UNIT, uint32_t IRQN>
struct Hs {
    static volatile uint8_t &r8(uint32_t o) { return *(volatile uint8_t *)(BASE + o); }
    static volatile uint16_t &r16(uint32_t o) { return *(volatile uint16_t *)(BASE + o); }
    static volatile uint32_t &r32(uint32_t o) { return *(volatile uint32_t *)(BASE + o); }
    static volatile uint32_t &rx_dma(uint8_t n) { return r32(0x24u + 4u * (n - 1u)); }
    static volatile uint32_t &tx_dma(uint8_t n) { return r32(0x40u + 4u * (n - 1u)); }
    static volatile uint32_t &max_len(uint8_t n) { return r32(0x5Cu + 4u * n); }
    static volatile uint16_t &rx_len(uint8_t n) { return r16(n ? 0x80u + 4u * (n - 1u) : 0x7Cu); }
    static volatile uint16_t &tx_len(uint8_t n) { return r16(0x9Cu + 4u * n); }
    static volatile uint8_t &tx_ctrl(uint8_t n) { return r8(0x9Eu + 4u * n); }
    static volatile uint8_t &rx_ctrl(uint8_t n) { return r8(0x9Fu + 4u * n); }

    static uint8_t ep0_buf[64] __attribute__((aligned(4)));
    static Xfer xin[8], xout[8];
    static bool ep0_tog, ep0_rx_tog;  /* EP0's next IN / expected OUT toggle */
    static bool suspended;

    static void set_tx(uint8_t ep, uint8_t res) { tx_ctrl(ep) = (uint8_t)((tx_ctrl(ep) & ~RES_MASK) | DONE | res); }
    static void set_rx(uint8_t ep, uint8_t res) { rx_ctrl(ep) = (uint8_t)((rx_ctrl(ep) & ~RES_MASK) | DONE | res); }

    static void set_tog(uint8_t ep, bool in, bool data1)
    {
        volatile uint8_t &c = in ? tx_ctrl(ep) : rx_ctrl(ep);
        c = (uint8_t)((c & ~TOG_MASK) | DONE | (data1 ? TOG1 : 0));
    }

    static void arm_ep0_out() { rx_ctrl(0) = (uint8_t)((ep0_rx_tog ? TOG1 : 0) | RES_ACK); }

    static void send(uint8_t ep)
    {
        Xfer &x = xin[ep];
        const uint16_t left = (uint16_t)(x.len - x.done);
        const uint16_t n = left < x.mps ? left : x.mps;
        if (ep == 0) {
            if (n) {
                memcpy(ep0_buf, x.buf + x.done, n);
            }
            tx_len(0) = n;
            tx_ctrl(0) = (uint8_t)(RES_ACK | (ep0_tog ? TOG1 : 0));
            ep0_tog = !ep0_tog;
        } else {
            if (n) {
                memcpy(x.bounce, x.buf + x.done, n);
            }
            tx_dma(ep) = (uint32_t)(uintptr_t)x.bounce;
            tx_len(ep) = n;
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
        rx_dma(ep) = (uint32_t)(uintptr_t)x.bounce;
        max_len(ep) = x.mps;
        if (!x.iso) {
            set_tog(ep, false, x.tog);
        }
        set_rx(ep, RES_ACK);
    }

    static void reset_endpoints()
    {
        r16(O_UEP_TX_EN) = 1;
        r16(O_UEP_RX_EN) = 1;
        r16(O_UEP_TX_ISO) = 0;
        r16(O_UEP_RX_ISO) = 0;
        r32(O_UEP0_DMA) = (uint32_t)(uintptr_t)ep0_buf;
        max_len(0) = 64;
        tx_len(0) = 0;
        tx_ctrl(0) = RES_NAK;
        rx_ctrl(0) = RES_ACK;
        for (uint8_t ep = 1; ep < 8; ep++) {
            tx_len(ep) = 0;
            tx_ctrl(ep) = RES_NAK;
            rx_ctrl(ep) = RES_NAK;
            max_len(ep) = 0;
            xin[ep].active = xout[ep].active = false;
        }
        xin[0].active = xout[0].active = false;
        ep0_tog = true;
        ep0_rx_tog = false;
    }

    /* A probe's attach makes SystemInit() run again (wiring_time.c), which
     * turns the PLL and UTMI off: put them back from the task. */
    static void poll()
    {
        if (!clock_ok(UNIT)) {
            clock_on(UNIT);
        }
    }

    static bool open()
    {
        if (!clock_on(UNIT)) {
            return false;
        }
        pads_on(UNIT);
        for (uint8_t ep = 0; ep < 8; ep++) {
            xin[ep].mps = xout[ep].mps = 64;
        }
        suspended = false;
        r8(O_CONTROL) = UD_RST_LINK | UD_PHY_SUSPENDM;
        r8(O_INT_EN) = UDIE_BUS_RST | UDIE_SUSPEND | UDIE_BUS_SLEEP | UDIE_LPM_ACT | UDIE_TRANSFER | UDIE_LINK_RDY;
        reset_endpoints();
        r8(O_BASE_MODE) = UD_SPEED_HIGH;
        r8(O_DEV_AD) = 0;
        r8(O_INT_FG) = 0xFF;
        r8(O_CONTROL) = UD_DMA_EN | UD_LPM_EN | UD_PHY_SUSPENDM;   /* DEV_EN (the pull-up) on connect */
        ch32rv_irq_enable(IRQN);
        return true;
    }

    static void connect(bool on)
    {
        r8(O_CONTROL) = (uint8_t)((on ? UD_DEV_EN : 0) | UD_DMA_EN | UD_LPM_EN | UD_PHY_SUSPENDM);
    }

    static void close()
    {
        ch32rv_irq_disable(IRQN);
        r8(O_INT_EN) = 0;
        r8(O_INT_FG) = 0xFF;
        r8(O_CONTROL) = UD_RST_SIE | UD_RST_LINK;
        for (uint8_t ep = 1; ep < 8; ep++) {
            free(xin[ep].bounce);
            free(xout[ep].bounce);
            xin[ep].bounce = xout[ep].bounce = nullptr;
        }
    }

    static CH32RVUsbSpeed speed()
    {
        return (r8(O_MIS_ST) & UDMS_HS_MOD) ? CH32RVUsbSpeed::High : CH32RVUsbSpeed::Full;
    }

    static void set_address(uint8_t a) { r8(O_DEV_AD) = a; }

    static bool ep_open(uint8_t address, uint8_t type, uint16_t mps)
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
            r16(O_UEP_TX_EN) |= bit;
            r16(O_UEP_TX_ISO) = x.iso ? (uint16_t)(r16(O_UEP_TX_ISO) | bit) : (uint16_t)(r16(O_UEP_TX_ISO) & ~bit);
            tx_len(ep) = 0;
            tx_ctrl(ep) = RES_NAK;
        } else {
            r16(O_UEP_RX_EN) |= bit;
            r16(O_UEP_RX_ISO) = x.iso ? (uint16_t)(r16(O_UEP_RX_ISO) | bit) : (uint16_t)(r16(O_UEP_RX_ISO) & ~bit);
            max_len(ep) = mps;
            rx_ctrl(ep) = RES_NAK;
        }
        interrupts();
        return true;
    }

    static void ep_close(uint8_t address)
    {
        const uint8_t ep = address & 0x0F;
        if (ep == 0 || ep > 7) {
            return;
        }
        const uint16_t bit = (uint16_t)(1u << ep);
        noInterrupts();
        if (address & 0x80) {
            xin[ep].active = false;
            tx_ctrl(ep) = RES_NAK;
            tx_len(ep) = 0;
            r16(O_UEP_TX_ISO) &= (uint16_t)~bit;
            r16(O_UEP_TX_EN) &= (uint16_t)~bit;
        } else {
            xout[ep].active = false;
            rx_ctrl(ep) = RES_NAK;
            max_len(ep) = 0;
            r16(O_UEP_RX_ISO) &= (uint16_t)~bit;
            r16(O_UEP_RX_EN) &= (uint16_t)~bit;
        }
        interrupts();
    }

    static void ep_close_all()
    {
        noInterrupts();
        for (uint8_t ep = 1; ep < 8; ep++) {
            xin[ep].active = xout[ep].active = false;
            tx_len(ep) = 0;
            tx_ctrl(ep) = RES_NAK;
            rx_ctrl(ep) = RES_NAK;
            max_len(ep) = 0;
        }
        r16(O_UEP_TX_EN) = 1;
        r16(O_UEP_RX_EN) = 1;
        r16(O_UEP_TX_ISO) = 0;
        r16(O_UEP_RX_ISO) = 0;
        interrupts();
    }

    static bool xfer(uint8_t address, uint8_t *buffer, uint16_t length)
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
            arm_ep0_out();                               /* data or status OUT */
        } else {
            receive(ep);
        }
        interrupts();
        return true;
    }

    static void abort(uint8_t address)
    {
        const uint8_t ep = address & 0x0F;
        if (ep == 0 || ep > 7) {
            return;
        }
        noInterrupts();
        if (address & 0x80) {
            xin[ep].active = false;
            tx_len(ep) = 0;
            if ((tx_ctrl(ep) & RES_MASK) != RES_STALL) {
                set_tx(ep, RES_NAK);
            }
        } else {
            xout[ep].active = false;
            if ((rx_ctrl(ep) & RES_MASK) != RES_STALL) {
                set_rx(ep, RES_NAK);
            }
        }
        interrupts();
    }

    static void stall(uint8_t address, bool on)
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
                tx_len(ep) = 0;
                set_tx(ep, RES_STALL);
            } else {
                set_rx(ep, RES_STALL);
            }
        } else {
            /* CLEAR_FEATURE(HALT): DATA0 again; a transfer still armed resumes
             * from the packet the stall dropped. */
            x.tog = false;
            if (x.active) {
                if (in) {
                    send(ep);
                } else {
                    receive(ep);
                }
            } else if (in) {
                tx_ctrl(ep) = RES_NAK;
            } else {
                rx_ctrl(ep) = RES_NAK;
            }
        }
        interrupts();
    }

    static void ep0_stall()
    {
        noInterrupts();
        tx_ctrl(0) = TOG1 | RES_STALL;
        rx_ctrl(0) = TOG1 | RES_STALL;
        xin[0].active = xout[0].active = false;
        interrupts();
    }

    static void remote_wakeup() { r8(O_WAKE_CTRL) |= 0x01; }   /* hardware drives K, clears the bit */

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
            tx_ctrl(0) = (uint8_t)(RES_NAK | (ep0_tog ? TOG1 : 0));
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

    static void isr()
    {
        const uint8_t fg = r8(O_INT_FG);
        if (fg & UDIE_TRANSFER) {
            for (uint8_t ep = 0; ep < 8; ep++) {
                const uint8_t rx = rx_ctrl(ep);
                if (rx & DONE) {
                    if (ep == 0 && (rx & SETUP_IS)) {
                        /* A finished status IN pending with this SETUP goes
                         * first; an unfinished data IN the host abandoned is
                         * dropped. */
                        if ((tx_ctrl(0) & DONE) && xin[0].active && xin[0].done + xin[0].last >= xin[0].len) {
                            tx_ctrl(0) &= (uint8_t)~DONE;
                            in_done(0);
                        }
                        ep0_tog = true;
                        ep0_rx_tog = true;
                        xin[0].active = xout[0].active = false;
                        const uint16_t wlength = (uint16_t)(ep0_buf[6] | (ep0_buf[7] << 8));
                        tx_ctrl(0) = (uint8_t)(RES_NAK | TOG1);
                        rx_ctrl(0) = (uint8_t)((wlength == 0 ? RES_ACK : RES_NAK) | TOG1);
                        ch32rv_usbd_isr_setup(ep0_buf);
                    } else {
                        rx_ctrl(ep) = (uint8_t)(rx & ~DONE);
                        /* A packet out of toggle is the host's retransmission
                         * after a lost ACK: already taken, keep it armed. */
                        if (ep == 0 || xout[ep].iso || (rx & TOG_MATCH)) {
                            out_done(ep, rx_len(ep));
                        }
                    }
                }
                if (tx_ctrl(ep) & DONE) {
                    tx_ctrl(ep) &= (uint8_t)~DONE;
                    in_done(ep);
                }
            }
        } else if (fg & UDIE_SOF) {
            r8(O_INT_FG) = UDIE_SOF;                     /* set every frame; not used */
        } else if (fg & UDIE_BUS_RST) {
            r8(O_DEV_AD) = 0;
            suspended = false;
            reset_endpoints();
            r8(O_INT_FG) = UDIE_BUS_RST;
            ch32rv_usbd_isr_reset();
        } else if (fg & UDIE_SUSPEND) {
            r8(O_INT_FG) = UDIE_SUSPEND;
            const bool now = (r8(O_MIS_ST) & UDMS_SUSPEND) != 0;
            if (now != suspended) {
                suspended = now;
                if (now) {
                    ch32rv_usbd_isr_suspend();
                } else {
                    ch32rv_usbd_isr_resume();
                }
            }
        } else {
            r8(O_INT_FG) = fg;
        }
    }

    static constexpr CH32RVUsbDcd table(const char *name)
    {
        return {name, 8, 512, (1u << (uint8_t)CH32RVUsbSpeed::Full) | (1u << (uint8_t)CH32RVUsbSpeed::High),
                open, close, connect, speed, set_address, ep_open, ep_close, ep_close_all, xfer, abort, stall,
                ep0_stall, remote_wakeup, poll};
    }
};

template <uint32_t B, uint8_t U, uint32_t I> uint8_t Hs<B, U, I>::ep0_buf[64] __attribute__((aligned(4)));
template <uint32_t B, uint8_t U, uint32_t I> Xfer Hs<B, U, I>::xin[8];
template <uint32_t B, uint8_t U, uint32_t I> Xfer Hs<B, U, I>::xout[8];
template <uint32_t B, uint8_t U, uint32_t I> bool Hs<B, U, I>::ep0_tog;
template <uint32_t B, uint8_t U, uint32_t I> bool Hs<B, U, I>::ep0_rx_tog;
template <uint32_t B, uint8_t U, uint32_t I> bool Hs<B, U, I>::suspended;

}  // namespace

#if defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
using Hs1 = Hs<0x40024000u, 1, CH32RV_IRQN_USBHS1>;
using Hs2 = Hs<0x40023400u, 2, CH32RV_IRQN_USBHS2>;
extern "C" __attribute__((interrupt)) void USBHS1_IRQHandler(void) { Hs1::isr(); }
extern "C" __attribute__((interrupt)) void USBHS2_IRQHandler(void) { Hs2::isr(); }
const CH32RVUsbDcd ch32rv_usb_dcd_usbhs2 = Hs1::table("USBHS1 (CH32V407/V467)");
const CH32RVUsbDcd ch32rv_usb_dcd_usbhs2_b = Hs2::table("USBHS2 (CH32V407/V467)");
#else
#if defined(CH32RV_SERIES_CH32V205)
using Hs1 = Hs<0x40023400u, 1, CH32RV_IRQN_USBHS>;
#else
using Hs1 = Hs<0x40030000u, 1, CH32RV_IRQN_USBHS>;
#endif
extern "C" __attribute__((interrupt)) void USBHS_IRQHandler(void) { Hs1::isr(); }
const CH32RVUsbDcd ch32rv_usb_dcd_usbhs2 = Hs1::table("USBHS (second generation)");
#endif

#endif
