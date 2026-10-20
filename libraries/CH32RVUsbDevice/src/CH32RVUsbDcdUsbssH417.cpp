/* CH32H417 / H416 USBSS (SuperSpeed, 5 Gbit/s), device role (CH32RVUsbDcd.h).
 *
 * Register map from CH32H417RM chapter 27 and the EVT's USBSS device demo
 * (base 0x40034000): the LINK block from 0x00, USB_CONTROL / USB_STATUS at
 * 0x70 / 0x74, EP0 at 0x84-0x90, then endpoints 1-7 at 0xC0 + 0x20 (n-1), TX
 * half first and RX half 0x10 later. The bring-up sequence, the PHY values
 * (0x400341F8 / FC are an undocumented PHY window, 0x5003C018 a PHY trim) and
 * the link-state handling are the EVT's.
 *
 * What the RM does not say, and the EVT does not need because it answers in
 * its interrupt, comes from TinyUSB PR #3779 (dcd_ch32h417_usb30.c, measured
 * on silicon there; not merged as of 2026-10):
 *   - EP0's sequence numbers are software's and count over the whole data
 *     stage; a set ACT bit write-locks the EP0 control words, so every arm is
 *     "write 0, then one store".
 *   - The status stage of a control IN raises no interrupt; it is completed
 *     when the stack asks for it. A control OUT / no-data one ends in
 *     UDIF_STATUS, which may come before the stack asked.
 *   - An endpoint transfer rides one chain (up to 16 packets: DMA, packet
 *     stride, last length, then the packet count, which arms it); FC_ST must be
 *     cleared after arming or an NRDY the host already got is never followed
 *     by an ERDY. EOB_MODE=1 marks short packets as end of burst.
 *   - An OUT chain writes a whole maximum packet whatever room is left, so a
 *     receive with less room goes through a bounce buffer.
 *   - Stall clears the chain and sets HALT; EP_CLR would break the STALL TP.
 *   - U1/U2/U3 need PHY register 0x12 changed on entry and put back on the way
 *     out through Recovery, or the link drops about 45 s in.
 *
 * USB 2.0 fallback (the USBHS controller, on PB8/PB9) is not here: those pins
 * are also SWCLK/SWDIO and the debug interface cannot be switched off, so a
 * USB 2.0 host and a WCH-Link fight over them (TinyUSB measured the HS link
 * failing that way). A board that wants USB 2.0 gets the USBHS driver of its
 * own; this one is SuperSpeed only.
 *
 * Everything this core links lives in the shared SRAM (sections_loadcode.ld),
 * which is the only memory the USB DMA reaches. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416)

#include <Arduino.h>
#include <stdlib.h>
#include <string.h>

#include "ch32rv_registers.h"
#include "CH32RVUsbDcd.h"

#define SS_BASE 0x40034000u
#define R8(o)  (*(volatile uint8_t *)(SS_BASE + (o)))
#define R16(o) (*(volatile uint16_t *)(SS_BASE + (o)))
#define R32(o) (*(volatile uint32_t *)(SS_BASE + (o)))

/* LINK */
#define LINK_CFG            R32(0x00)
#define LINK_CTRL           R32(0x04)
#define LINK_INT_CTRL       R32(0x08)
#define LINK_INT_FLAG       R32(0x0C)
#define LINK_STATUS         R32(0x10)
#define LINK_U2_INACT_TIMER R8(0x1D)
#define LINK_U1_WKUP_FILTER R8(0x28)
#define LINK_U1_WKUP_TMR    R16(0x2A)
#define LINK_U2_WKUP_FILTER R8(0x2C)
#define LINK_U3_WKUP_FILTER R8(0x30)
#define LINK_ISO_DLY        R16(0x40)
#define LINK_LPM_CR         R16(0x50)
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
#define UEP0_TX_CTRL        R32(0x84)
#define UEP0_RX_CTRL        R32(0x88)
#define UEP0_TX_DMA         R32(0x8C)
#define UEP0_RX_DMA         R32(0x90)
/* Endpoint n (1-7): TX at 0xC0 + 0x20 (n-1), RX 0x10 above. */
#define EP_TX(n)            (0xC0u + 0x20u * ((n) - 1u))
#define EP_RX(n)            (EP_TX(n) + 0x10u)
#define EP_CFG(b)           R8((b) + 0x0)
#define EP_CR(b)            R8((b) + 0x1)
#define EP_ST(b)            R8((b) + 0x3)
#define EP_CHAIN_ST(b)      R8((b) + 0x5)
#define EP_CHAIN_LEN(b)     R16((b) + 0x6)
#define EP_CHAIN_NUMP(b)    R8((b) + 0x8)          /* TX: EXP_NUMP, RX: MAX_NUMP; writing arms */
#define EP_DMA_OFS(b)       R16((b) + 0xA)
#define EP_DMA(b)           R32((b) + 0xC)
/* The PHY window and trim (EVT USBSS_PHY_Cfg / USBSS_CFG_MOD). */
#define PHY_CFG_CR          (*(volatile uint32_t *)0x400341F8u)
#define PHY_CFG_DAT         (*(volatile uint32_t *)0x400341FCu)
#define PHY_TRIM            (*(volatile uint32_t *)0x5003C018u)

#define U3_LINK_RESET       0x80000000u
#define LINK_U2_ALLOW       0x00020000u
#define LINK_U1_ALLOW       0x00010000u
#define LINK_LTSSM_MODE     0x00008000u
#define LINK_TOUT_MODE      0x00200000u
#define LINK_TX_DEEMPH_MASK 0x00000300u
#define LINK_RX_EQ_EN       0x00000040u
#define LINK_PHY_RESET      0x00000008u
#define LINK_RX_TERM_EN     0x00000002u

#define LINK_HOT_RESET      0x00010000u
#define LINK_TX_UX_EXIT     0x00000200u
#define LINK_GO_RX_DET      0x00000080u
#define LINK_GO_DISABLED    0x00000010u
#define LINK_P2_MODE        0x00000002u

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
#define LINK_STATE_INACTIVE 0x00000600u
#define LINK_STATE_RECOVERY 0x00000800u
#define LINK_STATE_HOTRST   0x00000900u
#define LINK_RX_WARM_RST    0x00000002u

#define LINK_LPM_EN         0x0200u
#define LINK_LMP_TX_CAP_VLD 0x40000000u

#define LMP_SUBTYPE_MASK    (0xFu << 5)
#define LMP_U2_INACT_TOUT   (0x2u << 5)
#define LMP_PORT_CAP        (0x4u << 5)
#define LMP_PORT_CFG        (0x5u << 5)
#define LMP_PORT_CFG_RES    (0x6u << 5)
#define LMP_LINK_SPEED      (1u << 9)
#define LMP_NUM_HP_BUF      4u
#define LMP_UP_STREAM       (2u << 16)

#define USBSS_UIE_TRANSFER  0x00010000u
#define USBSS_UDIE_SETUP    0x00020000u
#define USBSS_UDIE_STATUS   0x00040000u
#define USBSS_SETUP_FLOW    0x00000020u
#define USBSS_FORCE_RST     0x00000004u
#define USBSS_USB_CLR_ALL   0x00000002u
#define USBSS_DMA_EN        0x00000001u
#define USBSS_EP_DIR        0x00001000u
#define USBSS_EP_ID_MASK    0x00000700u
#define USBSS_UDIF_STATUS   0x00000004u
#define USBSS_UDIF_SETUP    0x00000002u
#define USBSS_UIF_TRANSFER  0x00000001u

#define EP0_TX_ERDY         0x00800000u
#define EP0_TX_DPH          0x00200000u
#define EP0_TX_STALL        0x00400000u
#define EP0_RX_ERDY         0x00800000u
#define EP0_RX_ACK          0x00200000u
#define EP0_RX_STALL        0x00400000u
#define EP0_LEN_MASK        0x000007FFu

#define EP_CHAIN_AUTO       0x80u
#define EP_EOB_MODE         0x08u
#define EP_ERDY_AUTO        0x04u
#define EP_SEQ_AUTO         0x02u
#define EP_ISO_MODE         0x01u
#define EP_HALT             0x80u
#define EP_CLR              0x40u
#define EP_CHAIN_CLR        0x20u
#define EP_FC_ST            0x40u
#define EP_CHAIN_IF         0x40u

/* RCC: the USBSS PLL takes HSE (its reference select defaults to 25 MHz,
 * the evaluation board's crystal), the PIPE and UTMI clocks sit in CFGR0. */
#define RCC_CTLR_HSEON        (1u << 16)
#define RCC_CTLR_HSERDY       (1u << 17)
#define RCC_CTLR_USBSS_PLLON  (1u << 22)
#define RCC_CTLR_USBSS_PLLRDY (1u << 23)
#define RCC_CFGR0_PIPEON      (1u << 22)
#define RCC_CFGR0_UTMION      (1u << 23)

#define EP0_SIZE   512u
#define CHAIN_MAX  16u            /* packets per chain, and the ERDY window offered */
#define RX_BURST   1u             /* packets an OUT arm takes (bMaxBurst 0) */

static uint8_t ep0_buf[EP0_SIZE] __attribute__((aligned(16)));

struct Xfer {
    uint8_t *buf;
    uint16_t len, done;
    uint16_t armed;              /* bytes in the chain in flight */
    uint16_t mps;
    uint8_t *bounce;             /* one packet; when the caller's buffer cannot take the DMA */
    bool bounced;                /* the chain in flight uses it */
    bool active, iso;
};
static Xfer xin[8], xout[8];

/* EP0 */
static uint8_t ep0_setup[8];
static uint8_t ep0_tx_seq, ep0_rx_seq;
static uint16_t ep0_in_sent;     /* bytes of this data stage sent so far */
static uint8_t pending_addr;
static bool pending_addr_valid;
static uint32_t pending_allow, pending_deny;   /* LINK_CFG U1/U2 permission, at the status stage */
static bool status_waiting;      /* a control OUT / no-data status stage is armed */
static bool status_done;         /* UDIF_STATUS came before the stack asked for the status */

/* Link */
static bool connected;           /* dcd_connect(true) and not since disconnected */
static bool link_up;             /* Port Config exchanged: enumerating at SuperSpeed */
static bool low_power;           /* PHY reconfigured for U1/U2/U3 */
static bool suspended;           /* U3 */
static uint8_t retrain_fail;
static volatile uint32_t release_at;   /* millis() to let a held link retrain; 0 = not held */

/* ------------------------------------------------------------ helpers */
static void phy_cfg(uint8_t addr, uint16_t data)
{
    PHY_CFG_CR = (1u << 23) | ((uint32_t)addr << 16) | data;
    PHY_CFG_DAT = 0x01u;
    (void)PHY_CFG_DAT;
}

static void set_address(uint8_t a)
{
    USB_CONTROL = (USB_CONTROL & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

/* The DMA address is written with every arm: the controller's address
 * registers advance as packets move (TinyUSB's CH569 / H417 notes), and a
 * configuration descriptor read once came back with its bytes from offset 96
 * on taken from 32 bytes earlier (bench, 2026-10-20). */
static void ep0_arm_in(uint16_t n)
{
    UEP0_TX_CTRL = 0;
    UEP0_TX_DMA = (uint32_t)(uintptr_t)ep0_buf;
    UEP0_TX_CTRL = EP0_TX_DPH | n | ((uint32_t)ep0_tx_seq << 16) | EP0_TX_ERDY;
    ep0_tx_seq = (uint8_t)((ep0_tx_seq + 1u) & 0x1Fu);
}

static void ep0_arm_out(void)
{
    UEP0_RX_CTRL = 0;
    UEP0_RX_DMA = (uint32_t)(uintptr_t)ep0_buf;
    UEP0_RX_CTRL = EP0_RX_ERDY | EP0_RX_ACK | ((uint32_t)ep0_rx_seq << 16);
}

/* The stack's view of a finished status stage, plus what this driver keeps
 * for that moment: the address and the U1/U2 permission. */
static void status_finished(void)
{
    if (pending_addr_valid) {
        set_address(pending_addr);
        pending_addr_valid = false;
    }
    LINK_CFG = (LINK_CFG | pending_allow) & ~pending_deny;
    pending_allow = pending_deny = 0;
    UEP0_TX_CTRL = 0;
    UEP0_RX_CTRL = 0;
}

static void ep0_send_next(void)
{
    Xfer &x = xin[0];
    const uint16_t left = (uint16_t)(x.len - x.done);
    const uint16_t n = left < EP0_SIZE ? left : EP0_SIZE;
    if (n) {
        memcpy(ep0_buf, x.buf + x.done, n);
    }
    x.armed = n;
    ep0_arm_in(n);
}

static void arm_in(uint8_t ep)
{
    Xfer &x = xin[ep];
    const uint32_t b = EP_TX(ep);
    const uint16_t left = (uint16_t)(x.len - x.done);
    uint16_t npkt, last;
    uint8_t *src = x.buf + x.done;
    if (x.bounce && (((uintptr_t)src & 3u) != 0)) {
        /* Unaligned: one packet at a time through the bounce buffer. */
        last = left < x.mps ? left : x.mps;
        npkt = 1;
        if (last) {
            memcpy(x.bounce, src, last);
        }
        src = x.bounce;
    } else if (left == 0) {
        npkt = 1;
        last = 0;
    } else {
        npkt = (uint16_t)((left + x.mps - 1u) / x.mps);
        if (npkt > CHAIN_MAX) {
            npkt = CHAIN_MAX;
            last = x.mps;
        } else {
            last = (uint16_t)(left - (uint16_t)(npkt - 1u) * x.mps);
        }
    }
    EP_DMA(b) = (uint32_t)(uintptr_t)src;
    EP_DMA_OFS(b) = x.mps;
    EP_CHAIN_LEN(b) = last;
    EP_CHAIN_NUMP(b) = (uint8_t)npkt;
    EP_ST(b) = EP_FC_ST;
    x.armed = (uint16_t)((uint16_t)(npkt - 1u) * x.mps + last);
}

static void arm_out(uint8_t ep)
{
    Xfer &x = xout[ep];
    const uint32_t b = EP_RX(ep);
    uint8_t *dst = x.buf + x.done;
    x.bounced = (uint16_t)(x.len - x.done) < x.mps || ((uintptr_t)dst & 3u) != 0;
    EP_DMA(b) = (uint32_t)(uintptr_t)(x.bounced ? x.bounce : dst);
    EP_DMA_OFS(b) = x.mps;
    EP_CHAIN_NUMP(b) = RX_BURST;
    EP_ST(b) = EP_FC_ST;
}

/* ------------------------------------------------------------ bring-up */
static void controller_reset(void)
{
    USB_CONTROL |= USBSS_FORCE_RST;
    USB_STATUS = USBSS_UIF_TRANSFER;
    USB_CONTROL = USBSS_UIE_TRANSFER | USBSS_UDIE_SETUP | USBSS_UDIE_STATUS | USBSS_DMA_EN | USBSS_SETUP_FLOW;
}

/* After a hot or warm reset: endpoints, flags and the address go; the link
 * stays. A reset also takes U1/U2 permission away (USB 3.2 table 9-10). */
static void engine_reset(void)
{
    controller_reset();
    const uint32_t c = USB_CONTROL;
    USB_CONTROL = c | USBSS_USB_CLR_ALL;
    USB_CONTROL = c & ~USBSS_USB_CLR_ALL;
    UEP0_TX_CTRL = 0;
    UEP0_RX_CTRL = 0;
    UEP0_TX_DMA = (uint32_t)(uintptr_t)ep0_buf;
    UEP0_RX_DMA = (uint32_t)(uintptr_t)ep0_buf;
    ep0_tx_seq = ep0_rx_seq = 0;
    status_waiting = status_done = false;
    pending_addr_valid = false;
    pending_allow = pending_deny = 0;
    LINK_CFG &= ~(LINK_U1_ALLOW | LINK_U2_ALLOW);
    for (uint8_t ep = 0; ep < 8; ep++) {
        xin[ep].active = xout[ep].active = false;
    }
}

static void link_start(void)
{
    LINK_CFG = LINK_RX_EQ_EN | LINK_TX_DEEMPH_MASK | LINK_PHY_RESET;
    LINK_CTRL = LINK_P2_MODE | LINK_GO_DISABLED;
    LINK_CFG = LINK_RX_EQ_EN | LINK_TX_DEEMPH_MASK | LINK_LTSSM_MODE | LINK_TOUT_MODE;
    LINK_LPM_CR |= LINK_LPM_EN;
    LINK_CFG |= LINK_RX_TERM_EN;
    LINK_INT_CTRL = LINK_IF_TX_LMP | LINK_IF_RX_LMP | LINK_IF_RX_LMP_TOUT | LINK_IF_STATE_CHG |
                    LINK_IF_WARM_RST | LINK_IF_TERM_PRES;
    LINK_CTRL = LINK_P2_MODE;
    LINK_U1_WKUP_TMR = 120;
    LINK_U1_WKUP_FILTER = 50;
    LINK_U2_WKUP_FILTER = 0;
    LINK_U3_WKUP_FILTER = 0;
    controller_reset();
    phy_cfg(0x03, 0x7C12);
    phy_cfg(0x0D, 0x79AA);
    phy_cfg(0x15, 0x4430);
    phy_cfg(0x13, 0x0010);
    phy_cfg(0x11, 0x0501);
    PHY_TRIM = 0xB0054000u;
    UEP_TX_EN = 0;
    UEP_RX_EN = 0;
    engine_reset();
    link_up = low_power = suspended = false;
    retrain_fail = 0;
    release_at = 0;
}

static void link_stop(void)
{
    USB_CONTROL = USBSS_FORCE_RST;
    LINK_CFG |= LINK_PHY_RESET | U3_LINK_RESET;
    delayMicroseconds(100);
    USB_CONTROL &= ~USBSS_FORCE_RST;
    LINK_CFG &= ~(LINK_PHY_RESET | U3_LINK_RESET);
}

/* ------------------------------------------------------------ table */
static bool clocks_on(void)
{
    CH32RV_RCC_CTLR |= RCC_CTLR_HSEON;
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & RCC_CTLR_HSERDY);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;                                /* no crystal */
        }
    }
    CH32RV_RCC_CTLR |= RCC_CTLR_USBSS_PLLON;
    for (uint32_t t0 = millis(); !(CH32RV_RCC_CTLR & RCC_CTLR_USBSS_PLLRDY);) {
        if ((uint32_t)(millis() - t0) > 100u) {
            return false;
        }
    }
    ch32rv_clock_enable(USBSS);
    CH32RV_RCC_CFGR0 |= RCC_CFGR0_PIPEON | RCC_CFGR0_UTMION;
    return true;
}

static bool dcd_open(void)
{
    if (!clocks_on()) {
        return false;
    }
    memset(xin, 0, sizeof xin);
    memset(xout, 0, sizeof xout);
    xin[0].mps = xout[0].mps = EP0_SIZE;
    link_stop();
    return true;
}

static void dcd_connect(bool on)
{
    connected = on;
    if (on) {
        link_start();
        ch32rv_irq_enable(CH32RV_IRQN_USBSS);
        ch32rv_irq_enable(CH32RV_IRQN_USBSS_LINK);
    } else {
        ch32rv_irq_disable(CH32RV_IRQN_USBSS_LINK);
        ch32rv_irq_disable(CH32RV_IRQN_USBSS);
        link_stop();                                     /* terminations off: the host sees it go */
        link_up = false;
    }
}

static void dcd_close(void)
{
    dcd_connect(false);
    for (uint8_t ep = 1; ep < 8; ep++) {
        free(xin[ep].bounce);
        free(xout[ep].bounce);
        xin[ep].bounce = xout[ep].bounce = nullptr;
    }
    CH32RV_RCC_CFGR0 &= ~RCC_CFGR0_PIPEON;
    ch32rv_clock_disable(USBSS);
    CH32RV_RCC_CTLR &= ~RCC_CTLR_USBSS_PLLON;
}

static CH32RVUsbSpeed dcd_speed(void)
{
    return CH32RVUsbSpeed::Super;
}

static void dcd_set_address(uint8_t address)
{
    /* Applied at the status stage already (status_finished); the stack's call
     * comes after it and writes the same value. */
    set_address(address);
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
    x.iso = type == CH32RV_USB_XFER_ISOCHRONOUS;
    if (in) {
        const uint32_t b = EP_TX(ep);
        EP_CFG(b) = EP_CHAIN_AUTO | EP_ERDY_AUTO | EP_SEQ_AUTO | EP_EOB_MODE | (x.iso ? EP_ISO_MODE : 0);
        EP_CR(b) = EP_CLR | EP_CHAIN_CLR;
        EP_CR(b) = CHAIN_MAX;
        UEP_TX_EN |= (uint16_t)(1u << ep);
    } else {
        const uint32_t b = EP_RX(ep);
        EP_CFG(b) = EP_CHAIN_AUTO | EP_ERDY_AUTO | EP_SEQ_AUTO | (x.iso ? EP_ISO_MODE : 0);
        EP_CR(b) = EP_CLR | EP_CHAIN_CLR;
        EP_CR(b) = RX_BURST;
        UEP_RX_EN |= (uint16_t)(1u << ep);
    }
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
        EP_CR(EP_TX(ep)) = EP_CLR | EP_CHAIN_CLR;
        UEP_TX_EN &= (uint16_t)~(1u << ep);
        xin[ep].active = false;
    } else {
        EP_CR(EP_RX(ep)) = EP_CLR | EP_CHAIN_CLR;
        UEP_RX_EN &= (uint16_t)~(1u << ep);
        xout[ep].active = false;
    }
    interrupts();
}

static void dcd_ep_close_all(void)
{
    noInterrupts();
    for (uint8_t ep = 1; ep < 8; ep++) {
        EP_CR(EP_TX(ep)) = EP_CLR | EP_CHAIN_CLR;
        EP_CR(EP_RX(ep)) = EP_CLR | EP_CHAIN_CLR;
        xin[ep].active = xout[ep].active = false;
    }
    UEP_TX_EN = 0;
    UEP_RX_EN = 0;
    interrupts();
}

/* EP0, as the stack drives it: the data stage in one or more calls, then the
 * status stage as a zero-length transfer the other way. */
static void ep0_xfer(bool in, uint8_t *buffer, uint16_t length)
{
    const bool setup_in = (ep0_setup[0] & 0x80) != 0;
    if (length == 0 && in != setup_in) {
        /* The status stage. */
        if (in) {
            /* Control OUT / no data: the device answers the host's status. */
            if (status_done) {
                status_done = false;
                status_finished();
                ch32rv_usbd_isr_xfer(0x80, 0);
            } else {
                status_waiting = true;
                ep0_arm_in(0);
                ep0_arm_out();
            }
        } else {
            /* Control IN: armed when the data stage ended; the controller
             * takes the host's status without an interrupt. */
            status_done = false;
            status_finished();
            ch32rv_usbd_isr_xfer(0x00, 0);
        }
        return;
    }
    Xfer &x = in ? xin[0] : xout[0];
    x.buf = buffer;
    x.len = length;
    x.done = 0;
    x.active = true;
    if (in) {
        ep0_send_next();
    } else {
        ep0_arm_out();
    }
}

static bool dcd_xfer(uint8_t address, uint8_t *buffer, uint16_t length)
{
    const uint8_t ep = address & 0x0F;
    if (ep > 7) {
        return false;
    }
    noInterrupts();
    if (ep == 0) {
        ep0_xfer((address & 0x80) != 0, buffer, length);
    } else {
        Xfer &x = (address & 0x80) ? xin[ep] : xout[ep];
        x.buf = buffer;
        x.len = length;
        x.done = 0;
        x.active = true;
        if (address & 0x80) {
            arm_in(ep);
        } else {
            arm_out(ep);
        }
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
        EP_CR(EP_TX(ep)) = (uint8_t)((EP_CR(EP_TX(ep)) & EP_HALT) | EP_CHAIN_CLR | CHAIN_MAX);
    } else {
        xout[ep].active = false;
        EP_CR(EP_RX(ep)) = (uint8_t)((EP_CR(EP_RX(ep)) & EP_HALT) | EP_CHAIN_CLR | RX_BURST);
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
    const uint32_t b = in ? EP_TX(ep) : EP_RX(ep);
    const uint8_t nump = in ? CHAIN_MAX : RX_BURST;
    Xfer &x = in ? xin[ep] : xout[ep];
    noInterrupts();
    if (on) {
        EP_CR(b) = (uint8_t)(EP_CHAIN_CLR | nump);
        EP_CR(b) = (uint8_t)(EP_HALT | nump);
    } else {
        /* CLEAR_FEATURE(HALT): the sequence starts over; a transfer that was
         * waiting resumes from the start of the chain the clear dropped. */
        EP_CR(b) = EP_CLR | EP_CHAIN_CLR;
        EP_CR(b) = nump;
        if (x.active) {
            if (in) {
                arm_in(ep);
            } else {
                arm_out(ep);
            }
        }
    }
    interrupts();
}

static void dcd_ep0_stall(void)
{
    noInterrupts();
    UEP0_TX_CTRL = 0;
    UEP0_RX_CTRL = 0;
    UEP0_TX_CTRL = EP0_TX_STALL;
    UEP0_RX_CTRL = EP0_RX_ERDY | EP0_RX_STALL;
    xin[0].active = xout[0].active = false;
    status_waiting = false;
    interrupts();
}

/* Function wake would be a Device Notification TP; asking the link to leave
 * U3 is what the controller offers. */
static void dcd_remote_wakeup(void)
{
    LINK_CTRL |= LINK_TX_UX_EXIT;
}


/* SystemInit() runs again when a debug probe has rewritten RCC (wiring_time.c)
 * and its reset sequence turns HSE and the USBSS PLL off, and the PIPE / UTMI
 * clocks with CFGR0: the link is gone. Bring the clocks back and train again. */
static void clock_check(void)
{
    const bool ok = (CH32RV_RCC_CTLR & (RCC_CTLR_HSERDY | RCC_CTLR_USBSS_PLLRDY)) ==
                        (RCC_CTLR_HSERDY | RCC_CTLR_USBSS_PLLRDY) &&
                    (CH32RV_RCC_CFGR0 & (RCC_CFGR0_PIPEON | RCC_CFGR0_UTMION)) == (RCC_CFGR0_PIPEON | RCC_CFGR0_UTMION);
    if (ok || !clocks_on()) {
        return;
    }
    if (connected) {
        if (link_up) {
            ch32rv_usbd_isr_suspend();
        }
        link_start();
    }
}

/* A link that fails to train against a quiet far end loops Disabled ->
 * RxDetect -> Polling at hardware speed; after three in a row it is held in
 * Disabled for 20 ms (TinyUSB measured the storm starving the debug link). */
static void dcd_poll(void)
{
    clock_check();
    const uint32_t at = release_at;
    if (at && (int32_t)(millis() - at) >= 0) {
        release_at = 0;
        LINK_CTRL &= ~LINK_GO_DISABLED;
    }
}

/* ------------------------------------------------------------ interrupts */
static void link_isr(void)
{
    const uint32_t f = LINK_INT_FLAG;
    const uint32_t state = LINK_STATUS & LINK_STATE_MASK;
    if (f & LINK_IF_STATE_CHG) {
        LINK_INT_FLAG = LINK_IF_STATE_CHG;
        switch (state) {
        case LINK_STATE_DISABLE:
            if (retrain_fail < 0xFF) {
                retrain_fail++;
            }
            if (retrain_fail >= 3) {
                release_at = (millis() + 20u) | 1u;
            } else {
                LINK_CTRL &= ~LINK_GO_DISABLED;
            }
            /* fall through */
        case LINK_STATE_INACTIVE:
            if (link_up) {
                link_up = false;
                suspended = low_power = false;
                ch32rv_usbd_isr_suspend();               /* gone: as good as suspended until a reset */
            }
            break;
        case LINK_STATE_HOTRST:
            engine_reset();
            suspended = low_power = false;
            ch32rv_usbd_isr_reset();
            LINK_CTRL &= ~LINK_HOT_RESET;
            break;
        case LINK_STATE_U3:
            suspended = true;
            ch32rv_usbd_isr_suspend();
            /* fall through */
        case LINK_STATE_U1:
        case LINK_STATE_U2:
            low_power = true;
            phy_cfg(0x12, (uint16_t)(0x67C8u & ~(1u << 9)));
            break;
        case LINK_STATE_RECOVERY:
            if (low_power) {
                low_power = false;
                delayMicroseconds(100);
                phy_cfg(0x12, 0x67C8);
            }
            if (suspended) {
                suspended = false;
                ch32rv_usbd_isr_resume();
            }
            break;
        default:
            break;
        }
    } else if (f & LINK_IF_TERM_PRES) {
        LINK_INT_FLAG = LINK_IF_TERM_PRES;
    } else if (f & LINK_IF_RX_LMP_TOUT) {
        LINK_INT_FLAG = LINK_IF_RX_LMP_TOUT;
        LINK_CTRL |= LINK_GO_DISABLED;
        LINK_CTRL |= LINK_GO_RX_DET;
    } else if (f & LINK_IF_TX_LMP) {
        LINK_INT_FLAG = LINK_IF_TX_LMP;
        LINK_LMP_TX_DATA0 = LMP_LINK_SPEED | LMP_PORT_CAP;
        LINK_LMP_TX_DATA1 = LMP_UP_STREAM | LMP_NUM_HP_BUF;
        LINK_LMP_TX_DATA2 = 0;
    } else if (f & LINK_IF_RX_LMP) {
        LINK_INT_FLAG = LINK_IF_RX_LMP;
        const uint32_t rx0 = LINK_LMP_RX_DATA0;
        if ((rx0 & LMP_SUBTYPE_MASK) == LMP_PORT_CFG) {
            /* Port Configuration: answer it, and the link is ours. */
            LINK_LMP_TX_DATA0 = LMP_LINK_SPEED | LMP_PORT_CFG_RES;
            LINK_LMP_TX_DATA1 = 0;
            LINK_LMP_TX_DATA2 = 0;
            LINK_LMP_PORT_CAP |= LINK_LMP_TX_CAP_VLD;
            retrain_fail = 0;
            link_up = true;
            ch32rv_usbd_isr_reset();
        } else if ((rx0 & LMP_SUBTYPE_MASK) == LMP_U2_INACT_TOUT) {
            LINK_U2_INACT_TIMER = (uint8_t)(rx0 >> 9);
        }
    } else if (f & LINK_IF_WARM_RST) {
        LINK_INT_FLAG = LINK_IF_WARM_RST;
        if (LINK_STATUS & LINK_RX_WARM_RST) {
            engine_reset();
            suspended = low_power = false;
            LINK_CTRL |= LINK_GO_DISABLED;
            __asm__ volatile("nop; nop; nop; nop");
            LINK_CTRL &= ~LINK_GO_DISABLED;
            ch32rv_usbd_isr_reset();
        }
    } else {
        LINK_INT_FLAG = f;
    }
}

static void setup_isr(void)
{
    memcpy(ep0_setup, ep0_buf, 8);
    ep0_tx_seq = ep0_rx_seq = 0;
    ep0_in_sent = 0;
    status_waiting = status_done = false;
    pending_addr_valid = false;
    pending_allow = pending_deny = 0;
    xin[0].active = xout[0].active = false;
    /* Requests whose effect this driver owns, applied once their status
     * stage has run (status_finished): the stack answers them generically. */
    const uint8_t type = ep0_setup[0], req = ep0_setup[1];
    const uint16_t value = (uint16_t)(ep0_setup[2] | (ep0_setup[3] << 8));
    if (type == 0x00 && req == 5) {                      /* SET_ADDRESS */
        pending_addr = (uint8_t)(value & 0x7F);
        pending_addr_valid = true;
    } else if (type == 0x00 && req == 0x31) {            /* SET_ISOCH_DELAY: ns */
        LINK_ISO_DLY = value;
    } else if (type == 0x00 && (req == 1 || req == 3) && (value == 48 || value == 49)) {
        /* U1_ENABLE / U2_ENABLE: without the ALLOW bit the link refuses LGO_Ux. */
        const uint32_t bit = value == 48 ? LINK_U1_ALLOW : LINK_U2_ALLOW;
        (req == 3 ? pending_allow : pending_deny) = bit;
    }
    ch32rv_usbd_isr_setup(ep0_setup);
}

static void ep0_in_isr(void)
{
    Xfer &x = xin[0];
    if (!x.active) {
        return;                                          /* the status ZLP: UDIF_STATUS ends it */
    }
    x.done = (uint16_t)(x.done + x.armed);
    ep0_in_sent = (uint16_t)(ep0_in_sent + x.armed);
    if (x.done < x.len) {
        ep0_send_next();
        return;
    }
    x.active = false;
    UEP0_TX_CTRL = 0;
    /* The end of the data stage - a short packet, or all the host asked for:
     * the status OUT is armed now, before the stack hears of it, or the host's
     * status DP gets an NRDY that no ERDY follows. */
    const uint16_t wlength = (uint16_t)(ep0_setup[6] | (ep0_setup[7] << 8));
    if (x.armed < EP0_SIZE || ep0_in_sent >= wlength) {
        ep0_arm_out();
    }
    ch32rv_usbd_isr_xfer(0x80, x.done);
}

static void ep0_out_isr(void)
{
    Xfer &x = xout[0];
    const uint16_t n = (uint16_t)(UEP0_RX_CTRL & EP0_LEN_MASK);
    if (!x.active) {
        /* A control IN's status ZLP: nothing to report (ep0_xfer finishes it). */
        UEP0_RX_CTRL = 0;
        return;
    }
    ep0_rx_seq = (uint8_t)((ep0_rx_seq + 1u) & 0x1Fu);
    const uint16_t room = (uint16_t)(x.len - x.done);
    const uint16_t take = n < room ? n : room;
    memcpy(x.buf + x.done, ep0_buf, take);
    x.done = (uint16_t)(x.done + take);
    if (x.done >= x.len || n < EP0_SIZE) {
        x.active = false;
        UEP0_RX_CTRL = 0;
        ch32rv_usbd_isr_xfer(0x00, x.done);
    } else {
        ep0_arm_out();
    }
}

static void ep_in_isr(uint8_t ep)
{
    Xfer &x = xin[ep];
    EP_CHAIN_ST(EP_TX(ep)) = EP_CHAIN_IF;
    if (!x.active) {
        return;
    }
    x.done = (uint16_t)(x.done + x.armed);
    if (x.done < x.len) {
        arm_in(ep);
        return;
    }
    x.active = false;
    ch32rv_usbd_isr_xfer((uint8_t)(0x80 | ep), x.done);
}

static void ep_out_isr(uint8_t ep)
{
    Xfer &x = xout[ep];
    const uint32_t b = EP_RX(ep);
    const uint16_t n = EP_CHAIN_LEN(b);
    EP_CHAIN_ST(b) = EP_CHAIN_IF;
    if (!x.active) {
        return;
    }
    const uint16_t room = (uint16_t)(x.len - x.done);
    uint16_t take = n < room ? n : room;
    if (take > x.mps) {
        take = x.mps;
    }
    if (x.bounced && take) {
        memcpy(x.buf + x.done, x.bounce, take);
    }
    x.done = (uint16_t)(x.done + take);
    if (x.done >= x.len || n < x.mps) {
        x.active = false;
        ch32rv_usbd_isr_xfer(ep, x.done);
    } else {
        arm_out(ep);
    }
}

extern "C" __attribute__((interrupt)) void USBSS_LINK_IRQHandler(void)
{
    link_isr();
}

extern "C" __attribute__((interrupt)) void USBSS_IRQHandler(void)
{
    const uint32_t st = USB_STATUS;
    if ((st & USBSS_UDIF_SETUP) && !(st & USBSS_UDIF_STATUS)) {
        USB_STATUS = USBSS_UDIF_SETUP;
        setup_isr();
    } else if (st & USBSS_UDIF_STATUS) {
        USB_STATUS = USBSS_UDIF_STATUS;
        if (status_waiting) {
            status_waiting = false;
            status_finished();
            ch32rv_usbd_isr_xfer(0x80, 0);
        } else {
            UEP0_TX_CTRL = 0;
            UEP0_RX_CTRL = 0;
            status_done = true;                          /* before the stack asked: see ep0_xfer */
        }
    } else if (st & USBSS_UIF_TRANSFER) {
        const uint8_t ep = (uint8_t)((st & USBSS_EP_ID_MASK) >> 8);
        const bool in = (st & USBSS_EP_DIR) != 0;
        if (ep == 0) {
            if (in) {
                ep0_in_isr();
            } else {
                ep0_out_isr();
            }
        } else if (in) {
            ep_in_isr(ep);
        } else {
            ep_out_isr(ep);
        }
    }
}

const CH32RVUsbDcd ch32rv_usb_dcd_usbss_h417 = {
    "USBSS (CH32H417/H416)", 8, 1024, 1u << (uint8_t)CH32RVUsbSpeed::Super,
    dcd_open, dcd_close, dcd_connect, dcd_speed, dcd_set_address,
    dcd_ep_open, dcd_ep_close, dcd_ep_close_all, dcd_xfer, dcd_abort, dcd_stall, dcd_ep0_stall, dcd_remote_wakeup,
    dcd_poll,
};

#endif
