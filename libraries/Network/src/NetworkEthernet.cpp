#include "NetworkEthernet.h"

#if NETWORK_ETHERNET_GMAC
#include <ch32rv_gpio.h>
#include <ch32rv_registers.h>
#include <string.h>

#include "lwip/netif.h"

/* One Synopsys MAC (0x40028000) with its DMA (0x40029000) on every series
 * here: the same registers, descriptors and checksum engine. What differs is
 * the clock, how the PHY is powered, what the PHY reports and which of the
 * EVT's PHY workarounds it needs (each from that series' EVT NetLib):
 *
 *   CH32H417           eth_driver_100M.c   HSE 25 MHz -> ETH PLL, 10/100 PHY in the MAC block
 *   CH32V407 / V467    eth_driver.c        the same PHY; ETH PLL on other RCC bits, no workarounds
 *   CH32V307           eth_driver_10M.c    10BASE-T PHY: PLL3 at 60 MHz, enabled through EXTEN
 *   CH32V317           eth_driver_CH32V317.c   10/100 PHY behind an internal RMII: PLL3 at 100 MHz
 *
 * Run on CH32H417 (docs/network-stack.ja.md); the others are built, not yet
 * run on a board. */

#if defined(CH32RV_SERIES_CH32H417)
#define ETH_H417 1
#elif defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467)
#define ETH_V4X7 1
#elif defined(CH32RV_SERIES_CH32V307)
#define ETH_V307 1
#elif defined(CH32RV_SERIES_CH32V317)
#define ETH_V317 1
#endif
#if defined(ETH_V307) || defined(ETH_V317)
#define ETH_PLL3 1
#else
#define ETH_PLL3 0
#endif

/* RCC */
#define RCC_CTLR    CH32RV_REG32(0x40021000u)
#define RCC_CFGR2   CH32RV_REG32(0x4002102Cu)
#define CTLR_HSEON  (1u << 16)
#define CTLR_HSERDY (1u << 17)
#if defined(ETH_H417)
#define RCC_MACEN   CH32RV_REG32(0x40021018u)     /* HBPCENR */
#define MACEN_BITS  (1u << 14)
#define CTLR_PLLON  (1u << 26)                    /* ETH_PLLON: 25 MHz x 20 */
#define CTLR_PLLRDY (1u << 27)
#elif defined(ETH_V4X7)
#define RCC_MACEN   CH32RV_REG32(0x40021014u)     /* HBPCENR */
#define MACEN_BITS  (1u << 14)
#define CTLR_PLLON  (1u << 22)                    /* ETH_PLLON */
#define CTLR_PLLRDY (1u << 23)
#else
#define RCC_MACEN   CH32RV_REG32(0x40021014u)     /* AHBPCENR: ETHMAC, ETHMACTX, ETHMACRX */
#define MACEN_BITS  (7u << 14)
#define CTLR_PLLON  (1u << 28)                    /* PLL3ON */
#define CTLR_PLLRDY (1u << 29)
#define CTLR_PLL2ON (1u << 26)
#endif
#if defined(ETH_V307)
#define EXTEN_CTR   CH32RV_REG32(0x40023800u)
#define EXTEN_ETH10M (1u << 2)                    /* the 10M PHY and its clock */
#endif

/* MAC and DMA, as the EVT's ch32*_eth.h name them. */
#define MAC(off)   CH32RV_REG32(0x40028000u + (off))
#define MACCR      MAC(0x00)
#define MACFFR     MAC(0x04)
#define MACMIIAR   MAC(0x10)
#define MACMIIDR   MAC(0x14)
#define MACFCR     MAC(0x18)
#define MACA0HR    MAC(0x40)
#define MACA0LR    MAC(0x44)
#define MACPHYCR   MAC(0x80)                      /* H417, V407: PHY power-down and reset */
#define MMCCR      MAC(0x100)
#define MMCRFCECR  MAC(0x194)                     /* frames with a CRC error */
#define MMCRGUFCR  MAC(0x1C4)                     /* good unicast frames */
#define DMA(off)   CH32RV_REG32(0x40029000u + (off))
#define DMABMR     DMA(0x00)
#define DMATPDR    DMA(0x04)
#define DMARPDR    DMA(0x08)
#define DMARDLAR   DMA(0x0C)
#define DMATDLAR   DMA(0x10)
#define DMASR      DMA(0x14)
#define DMAOMR     DMA(0x18)

#define MACCR_RE (1u << 2)
#define MACCR_TE (1u << 3)
#define MACCR_RD (1u << 9)         /* retry disable: the V30x EVTs set it */
#define MACCR_IPCO (1u << 10)      /* checksum offload: received IPv4 and TCP/UDP/ICMP checked */
#define MACCR_DM (1u << 11)        /* full duplex */
#define MACCR_SPEED (3u << 14)
#define MACCR_100M (1u << 14)
#define MACCR_PR (1u << 20)        /* V307: the 10M PHY's on-chip 50-ohm termination */
#define MACFFR_PM 1u               /* promiscuous */
#define MACFFR_PAM (1u << 4)       /* pass all multicast (mDNS) */
#define MACFFR_RA (1u << 31)       /* receive all */
#define DMABMR_SR 1u
#define DMASR_TBUS (1u << 2)
#define DMASR_RBUS (1u << 7)
#define DMAOMR_SR (1u << 1)
#define DMAOMR_ST (1u << 13)
#define DMAOMR_FTF (1u << 20)
#define DMAOMR_TSF (1u << 21)

#define DESC_OWN (1u << 31)
#define TX_LS (1u << 29)
#define TX_FS (1u << 28)
#define TX_TCH (1u << 20)
#define TX_CIC_FULL (3u << 22)     /* insert the IPv4 header and TCP/UDP/ICMP checksums */
#define RX_ES (1u << 15)
#define RX_FS (1u << 9)
#define RX_LS (1u << 8)
#define RX_RCH (1u << 14)

/* The internal PHY (MDIO address 1; the V307's SMI ignores the address). */
#define PHY_ADDR 1u
#define PHY_BCR 0x00
#define PHY_BSR 0x01
#define PHY_ANLPAR 0x05
#define PHY_ANER 0x06
#define PHY_PAGE 0x1F
#define BCR_RESET 0x8000u
#define BCR_100M (1u << 13)
#define BCR_ANEG (1u << 12)
#define BCR_POWERDOWN (1u << 11)
#define BCR_RESTART_ANEG (1u << 9)
#define BCR_FULL (1u << 8)
#define BSR_LINK (1u << 2)
#define BSR_ANEG_DONE (1u << 5)
#define ANLPAR_SELECTOR 0x1Fu

static const uint16_t BUF = 1536;
#if defined(ETH_H417) || defined(ETH_V4X7)
static const uint8_t RX_N = 8, TX_N = 4;
static const uint32_t PHY_PERIOD_MS = 20;
#elif defined(ETH_V317)
static const uint8_t RX_N = 4, TX_N = 3;     /* 64 KB of RAM */
static const uint32_t PHY_PERIOD_MS = 10;    /* the EVT's link task period */
#else
static const uint8_t RX_N = 4, TX_N = 3;
static const uint32_t PHY_PERIOD_MS = 10;    /* the EVT's tick; its link task runs every 50 ms */
#endif

struct Desc {
    volatile uint32_t status, control, buf, next;
};

__attribute__((aligned(32))) static Desc rxDesc[RX_N];
__attribute__((aligned(32))) static Desc txDesc[TX_N];
__attribute__((aligned(32))) static uint8_t rxBuf[RX_N][BUF];
__attribute__((aligned(32))) static uint8_t txBuf[TX_N][BUF];

NetworkEthernet ETH;

static bool waitSet(volatile uint32_t &reg, uint32_t bit, uint32_t ms)
{
    for (uint32_t t0 = millis(); !(reg & bit);) {
        if (millis() - t0 > ms) {
            return false;
        }
    }
    return true;
}

/* MDC from HCLK: the CR field (bits 4:2) by the EVT headers' ranges. */
static uint32_t mdcRange()
{
    if (F_CPU <= 35000000u) {
        return 0x8u;               /* /16 */
    }
    if (F_CPU <= 60000000u) {
        return 0xCu;               /* /26 */
    }
    if (F_CPU <= 100000000u) {
        return 0x0u;               /* /42 */
    }
    if (F_CPU <= 150000000u) {
        return 0x4u;               /* /62 */
    }
    return 0x10u;                  /* /102 */
}

#if ETH_PLL3
/* PREDIV2 and PLL3MUL for PLL3 = `hz` from HSE (both written only with PLL2
 * and PLL3 off). The multiplier codes: 0 x2.5, 1 x12.5, 2..14 x4..x16, 15 x20. */
static bool pll3Bits(uint32_t hse, uint32_t hz, uint32_t &bits)
{
    static const uint8_t twice[16] = {5, 25, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 40};
    for (uint32_t div = 1; div <= 16; div++) {
        for (uint32_t m = 0; m < 16; m++) {
            if ((uint64_t)hse * twice[m] == (uint64_t)hz * 2u * div) {
                bits = (m << 12) | ((div - 1u) << 4);
                return true;
            }
        }
    }
    return false;
}
#endif

/* The clocks the MAC and PHY run on. SystemInit() - which the core runs
 * again when a debugger has moved the clock - turns HSE and the PLLs off
 * (and clears CFGR2 on the V30x), so poll() calls this again then. */
bool NetworkEthernet::clockOn(uint32_t waitMs)
{
    if (!(RCC_CTLR & CTLR_HSERDY)) {
        RCC_CTLR |= CTLR_HSEON;
        if (!waitSet(RCC_CTLR, CTLR_HSERDY, waitMs)) {
            return false;
        }
    }
#if ETH_PLL3
    if (!(RCC_CTLR & CTLR_PLLRDY)) {
        uint32_t bits;
#if defined(ETH_V307)
        const uint32_t target = 60000000u;          /* the 10M PHY wants exactly this */
#else
        const uint32_t target = 100000000u;
#endif
        if (!pll3Bits(_hseHz, target, bits) || (RCC_CTLR & CTLR_PLL2ON)) {
            return false;                           /* no exact setting, or PREDIV2 is PLL2's too */
        }
        RCC_CTLR &= ~CTLR_PLLON;
        RCC_CFGR2 = (RCC_CFGR2 & ~0xF0F0u) | bits;
        RCC_CTLR |= CTLR_PLLON;
        if (!waitSet(RCC_CTLR, CTLR_PLLRDY, waitMs)) {
            return false;
        }
        delayMicroseconds(300);                     /* the EVT: let the PHY's clock settle */
    }
#else
    if (!(RCC_CTLR & CTLR_PLLRDY)) {
        RCC_CTLR |= CTLR_PLLON;
        if (!waitSet(RCC_CTLR, CTLR_PLLRDY, waitMs)) {
            return false;
        }
    }
#endif
    RCC_MACEN |= MACEN_BITS;
#if defined(ETH_V307)
    if (!_phyOff) {
        EXTEN_CTR |= EXTEN_ETH10M;
    }
#endif
    return true;
}

bool NetworkEthernet::clockLost() const
{
    return !(RCC_CTLR & CTLR_PLLRDY) || (RCC_MACEN & MACEN_BITS) != MACEN_BITS
#if defined(ETH_V307)
        || (!_phyOff && !(EXTEN_CTR & EXTEN_ETH10M))
#endif
        ;
}

uint16_t NetworkEthernet::phyRead(uint8_t reg) const
{
    MACMIIAR = (PHY_ADDR << 11) | ((uint32_t)(reg & 31u) << 6) | mdcRange() | 1u;
    for (uint32_t t0 = micros(); MACMIIAR & 1u;) {
        if (micros() - t0 > 1000u) {
            return 0xFFFF;
        }
    }
    return (uint16_t)MACMIIDR;
}

void NetworkEthernet::phyWrite(uint8_t reg, uint16_t value) const
{
    MACMIIDR = value;
    MACMIIAR = (PHY_ADDR << 11) | ((uint32_t)(reg & 31u) << 6) | mdcRange() | 2u | 1u;
    for (uint32_t t0 = micros(); MACMIIAR & 1u;) {
        if (micros() - t0 > 1000u) {
            return;
        }
    }
}

bool NetworkEthernet::setMacAddress(const uint8_t mac[6])
{
    if (_running) {
        return false;
    }
    memcpy(_mac, mac, 6);
    _macSet = true;
    return true;
}

bool NetworkEthernet::setHseFrequency(uint32_t hz)
{
    if (_running || !hz) {
        return false;
    }
    _hseHz = hz;
    return true;
}

#if defined(ETH_V317)
/* The EVT's PHY_FuncInit(): repeater mode, then the RMII clock setting. */
void NetworkEthernet::phyInit()
{
    phyWrite(PHY_PAGE, 0);
    phyWrite(28, (uint16_t)(phyRead(28) | (1u << 13)));
    phyWrite(PHY_PAGE, 7);
    phyWrite(16, (uint16_t)((phyRead(16) & ~(0xFFu << 4)) | (0x23u << 4)));
    phyWrite(PHY_PAGE, 0);
}

/* ETH_RMIIPinInit(): the internal PHY sits on these pads. */
static void rmiiPins()
{
    static const struct { uint8_t port, bit; bool out; } pads[] = {
        {3, 8, true},   /* PD8 MDIO */
        {4, 15, true},  /* PE15 MDC */
        {4, 8, true},   /* PE8 TXEN */
        {4, 11, true},  /* PE11 TXD0 */
        {4, 10, true},  /* PE10 TXD1 */
        {4, 12, true},  /* PE12 REFCLK */
        {4, 9, false},  /* PE9 CRSDV */
        {4, 14, false}, /* PE14 RXD0 */
        {4, 13, false}, /* PE13 RXD1 */
    };
    ch32rv_clock_enable(AFIO);
    CH32RV_AFIO_PCFR1 |= 1u << 23;                   /* MII_RMII_SEL: RMII */
    for (const auto &p : pads) {
        ch32rv_gpio_clock_enable(p.port);
        ch32rv_gpio_set_config(p.port, p.bit, p.out ? CH32RV_GPIO_CFG_AF_PP_50M : CH32RV_GPIO_CFG_IN_FLOAT);
    }
}
#endif

bool NetworkEthernet::begin(bool makeDefault)
{
    if (_running) {
        return true;
    }
    if (!_macSet) {
        /* Locally administered, unicast, from the 64-bit unique ID - a different
         * mix from CH32RVUsbDeviceNet's, so the two never share an address. */
        const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
        const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
        const uint32_t mix = hi ^ (lo * 0x9E3779B1u);
        _mac[0] = 0x06;
        _mac[1] = (uint8_t)(lo >> 16);
        _mac[2] = (uint8_t)(mix >> 24);
        _mac[3] = (uint8_t)(mix >> 16);
        _mac[4] = (uint8_t)(mix >> 8);
        _mac[5] = (uint8_t)mix;
    }

#if defined(ETH_V317)
    rmiiPins();
#endif
    _phyOff = false;
    if (!clockOn(100)) {
        return false;
    }
#if defined(ETH_H417) || defined(ETH_V4X7)
    MACPHYCR &= ~(1u << 30);           /* PHY out of power-down */
    MACPHYCR |= 1u << 31;              /* and out of reset */
#endif

    DMABMR |= DMABMR_SR;
    for (uint32_t t0 = millis(); DMABMR & DMABMR_SR;) {
        if (millis() - t0 > 100u) {
            return false;
        }
    }
    MACMIIAR = mdcRange();
#if defined(ETH_V307)
    _maccr = MACCR_IPCO | MACCR_RD | MACCR_PR;
#elif defined(ETH_V317)
    _maccr = MACCR_IPCO | MACCR_RD;
#else
    _maccr = MACCR_IPCO;
#endif
    MACCR = _maccr;                    /* speed and duplex once the link is up */
    MACFFR = MACFFR_PAM;               /* our address, broadcast, all multicast */
    MACFCR = 0;
    DMAOMR = DMAOMR_TSF;               /* whole frames to the MAC, which checksum insertion needs; frames
                                          with a bad checksum are dropped (DTCEFD clear) */
    MACA0HR = (uint32_t)_mac[5] << 8 | _mac[4];
    MACA0LR = (uint32_t)_mac[0] | (uint32_t)_mac[1] << 8 | (uint32_t)_mac[2] << 16 | (uint32_t)_mac[3] << 24;

    for (uint8_t i = 0; i < RX_N; i++) {
        rxDesc[i].control = RX_RCH | BUF;
        rxDesc[i].buf = (uint32_t)(uintptr_t)rxBuf[i];
        rxDesc[i].next = (uint32_t)(uintptr_t)&rxDesc[(i + 1) % RX_N];
        rxDesc[i].status = DESC_OWN;
    }
    for (uint8_t i = 0; i < TX_N; i++) {
        txDesc[i].status = TX_TCH;
        txDesc[i].control = 0;
        txDesc[i].buf = (uint32_t)(uintptr_t)txBuf[i];
        txDesc[i].next = (uint32_t)(uintptr_t)&txDesc[(i + 1) % TX_N];
    }
    _rxNext = _txNext = 0;
    DMARDLAR = (uint32_t)(uintptr_t)rxDesc;
    DMATDLAR = (uint32_t)(uintptr_t)txDesc;

    _linked = false;
    _speed = 0;
    _pnSince = 0;
    _invalid = _esd = 0;
    _rx = _tx = _rxErrors = _txBusy = _linkChanges = _phyResets = _clockRestarts = 0;
    phyWrite(PHY_BCR, BCR_RESET);
#if defined(ETH_V307)
    negotiationInit();
#elif defined(ETH_V317)
    delay(1);
    phyInit();
    _step = 0;
#endif
    _rand = (uint32_t)_mac[5] << 8 | _mac[4];      /* the EVT seeds its link timing from the MAC */
    _linkAt = _phyAt = millis();
    _running = true;

    if (!netifStart(_mac)) {
        _running = false;
        return false;
    }
    /* The MAC does every IPv4, TCP, UDP and ICMP checksum both ways. */
    NETIF_SET_CHECKSUM_CTRL(netif(), NETIF_CHECKSUM_GEN_ICMP6 | NETIF_CHECKSUM_CHECK_ICMP6);
    if (makeDefault) {
        setDefault();
    }
    return true;
}

void NetworkEthernet::end()
{
    if (!_running) {
        return;
    }
    netifStop();
    DMAOMR &= ~(DMAOMR_ST | DMAOMR_SR);
    MACCR &= ~(MACCR_TE | MACCR_RE);
#if defined(ETH_H417) || defined(ETH_V4X7)
    MACPHYCR |= 1u << 30;              /* PHY power-down */
#elif defined(ETH_V307)
    EXTEN_CTR &= ~EXTEN_ETH10M;
#else
    phyWrite(PHY_BCR, BCR_POWERDOWN);
#endif
    _running = false;
    _linked = false;
    _speed = 0;
}

/* Speed and duplex into the MAC, then the MAC and the DMA start (the EVT's
 * ETH_Start()). */
void NetworkEthernet::startMac()
{
    uint32_t cr = _maccr & ~(MACCR_SPEED | MACCR_DM);
    if (_speed == 100) {
        cr |= MACCR_100M;
    }
    if (_full) {
        cr |= MACCR_DM;
    }
    MACCR = cr | MACCR_TE;
    DMAOMR |= DMAOMR_FTF;
    for (uint32_t t0 = micros(); DMAOMR & DMAOMR_FTF;) {
        if (micros() - t0 > 1000u) {
            break;
        }
    }
    MACCR |= MACCR_RE;
    DMAOMR |= DMAOMR_ST | DMAOMR_SR;
    _linked = true;
    _invalid = _esd = 0;
    _linkChanges++;
    setLinkUp(true);
}

void NetworkEthernet::linkDown()
{
    _linked = false;
    _speed = 0;
    setLinkUp(false);
}

void NetworkEthernet::phyReset()
{
    _phyResets++;
    const uint16_t bcr = phyRead(PHY_BCR);
    phyWrite(PHY_BCR, BCR_RESET);
    delayMicroseconds(100);
    phyWrite(PHY_BCR, bcr);
}

#if defined(ETH_H417) || defined(ETH_V4X7)
/* H417 / V407: speed and duplex in page 0's status word 0x1A. The H417's EVT
 * (eth_driver_100M.c) adds three workarounds - P/N polarity, a 100M link that
 * carries nothing, the reset after an ESD upset - and a PHY tuning write at
 * link-up; the V407's EVT has none of them, so they stay H417-only. */
#define PHY_INT_MASK 0x13
#define PHY_ESD 0x14
#define PHY_CONTROL1 0x19
#define PHY_STATUS 0x1A
#define STATUS_FULL (1u << 1)
#define STATUS_100M (1u << 3)

void NetworkEthernet::phyTask()
{
    phyWrite(PHY_PAGE, 0);
    const uint16_t bsr = phyRead(PHY_BSR);
    const uint16_t bcr = phyRead(PHY_BCR);
    if (bsr == 0xFFFF) {
        return;                        /* MDIO timed out */
    }
    const bool link = (bsr & BSR_LINK) != 0;
    if (link && !_linked) {
        /* With autonegotiation on, wait for it to finish - unless the partner
         * does not negotiate (its ability word stays 0: parallel detection). */
        if (!(bcr & BCR_ANEG) || (bsr & BSR_ANEG_DONE) || phyRead(PHY_ANLPAR) == 0) {
#if defined(ETH_H417)
            CH32RV_REG32(0x4002A00Cu) = 0xFFFFF;   /* the EVT's PHY_PARA_CFG() */
            CH32RV_REG32(0x4002A00Cu) = 0xFFFFF;
            CH32RV_REG32(0x4002A00Cu) = 0xFFFFF;
            CH32RV_REG32(0x4002A00Cu) = 0xC2000;
#endif
            phyWrite(PHY_PAGE, 0);
            const uint16_t st = phyRead(PHY_STATUS);
            _speed = (st & STATUS_100M) ? 100 : 10;
            _full = (st & STATUS_FULL) != 0;
            startMac();
        }
        return;
    }
    if (!link && _linked) {
        linkDown();
        return;
    }
#if defined(ETH_H417)
    if (!_linked) {
        /* WCHNET_CheckPHYPN(): a partner seen for 300 ms (200 at a forced 10M)
         * without a link - try the other polarity. */
        if (!(bcr & BCR_ANEG) && (bcr & BCR_100M)) {
            return;
        }
        const uint32_t wait = (bcr & BCR_ANEG) ? 300u : 200u;
        if (phyRead(PHY_STATUS) & (1u << 4)) {
            if (!_pnSince) {
                _pnSince = millis() | 1u;
            } else if (millis() - _pnSince >= wait) {
                _pnSince = 0;
                if (wait == 200u || !(phyRead(PHY_ANER) & 1u)) {
                    phyWrite(PHY_CONTROL1, (uint16_t)(phyRead(PHY_CONTROL1) | 1u));
                }
            }
        } else {
            _pnSince = 0;
        }
        return;
    }
    /* Linked. WCHNET_CheckLinkVaild(): a 100M link whose page-99 flag stays
     * clear for 5 looks carries nothing - reset the PHY. */
    if (bcr & BCR_100M) {
        phyWrite(PHY_PAGE, 99);
        const uint16_t ok = phyRead(0x1B);
        phyWrite(PHY_PAGE, 0);
        if (!(ok & (1u << 2))) {
            if (++_invalid >= 5) {
                _invalid = 0;
                phyReset();
                linkDown();
                return;
            }
        } else {
            _invalid = 0;
        }
    }
    /* WCHNET_ESDExceptionHandling(): a non-zero error word 6 looks running. */
    if (phyRead(PHY_ESD)) {
        if (++_esd >= 6) {
            _esd = 0;
            phyReset();
            linkDown();
        }
    } else {
        _esd = 0;
    }
#endif
}
#endif

#if defined(ETH_V317)
/* V317 (eth_driver_CH32V317.c): speed and duplex from BCR; while down, the
 * P/N check (page 0 0x10 bit 12 for 300 ms, 200 at a forced 10M: pulse 0x16
 * bit 5) and the "accelerate link" sequence; while up at 100M, the link check
 * (0x10 bit 9 clear 5 looks: reset the PHY). */
void NetworkEthernet::phyTask()
{
    phyWrite(PHY_PAGE, 0);
    const uint16_t bsr = phyRead(PHY_BSR);
    const uint16_t bcr = phyRead(PHY_BCR);
    if (bsr == 0xFFFF) {
        return;
    }
    const bool link = (bsr & BSR_LINK) != 0;
    if (link && !_linked) {
        if (!(bcr & BCR_ANEG) || (bsr & BSR_ANEG_DONE) || phyRead(PHY_ANLPAR) == 0) {
            _speed = (bcr & BCR_100M) ? 100 : 10;
            _full = (bcr & BCR_FULL) != 0;
            _step = 0;
            _pnSince = 0;
            startMac();
            phyWrite(PHY_PAGE, 0);
            phyWrite(0x13, 0);
        }
        return;
    }
    if (!link && _linked) {
        linkDown();
        return;
    }
    if (!_linked) {
        if (!(bcr & BCR_ANEG) && (bcr & BCR_100M)) {
            return;
        }
        const uint32_t wait = (bcr & BCR_ANEG) ? 300u : 200u;
        if (phyRead(0x10) & (1u << 12)) {
            if (!_pnSince) {
                _pnSince = millis() | 1u;
            } else if (millis() - _pnSince >= wait) {
                _pnSince = 0;
                if (wait == 200u || !(phyRead(PHY_ANER) & 1u)) {
                    phyWrite(PHY_PAGE, 0);
                    phyWrite(0x16, (uint16_t)(phyRead(0x16) | (1u << 5)));
                    phyWrite(0x16, (uint16_t)(phyRead(0x16) & ~(1u << 5)));
                    (void)phyRead(0x1E);   /* clears the interrupt status */
                }
            }
        } else {
            _pnSince = 0;
        }
        if (!(bcr & BCR_ANEG)) {
            return;
        }
        /* WCHNET_AccelerateLink(): power the PHY down with a shorter link
         * time for 1.2 s, power it up, re-initialise, wait for page 99 0x19 = 2. */
        switch (_step) {
        case 0:
            _step = 1;
            _stepAt = millis();
            phyWrite(PHY_PAGE, 0);
            phyWrite(0x18, (uint16_t)(phyRead(0x18) & ~(1u << 15)));
            phyWrite(PHY_BCR, (uint16_t)(phyRead(PHY_BCR) | BCR_POWERDOWN));
            phyWrite(0x13, 4);
            break;
        case 1:
            if (millis() - _stepAt > 1200u) {
                _step = 2;
                phyWrite(PHY_PAGE, 0);
                phyWrite(0x18, (uint16_t)(phyRead(0x18) | (1u << 15)));
                phyWrite(PHY_BCR, (uint16_t)(phyRead(PHY_BCR) & ~BCR_POWERDOWN));
                delayMicroseconds(300);
                phyInit();
            }
            break;
        case 2:
            phyWrite(PHY_PAGE, 99);
            if ((phyRead(0x19) & 0xFu) == 2u) {
                _step = 3;
                phyWrite(PHY_PAGE, 0);
                phyWrite(0x13, 0);
            }
            phyWrite(PHY_PAGE, 0);
            break;
        default:
            break;
        }
        return;
    }
    if (_speed == 100) {
        phyWrite(PHY_PAGE, 0);
        if (!(phyRead(0x10) & (1u << 9))) {
            if (++_invalid >= 5) {
                _invalid = 0;
                phyReset();
                linkDown();
            }
        } else {
            _invalid = 0;
        }
    }
}
#endif

#if defined(ETH_V307)
/* V307 (eth_driver_10M.c): a 10BASE-T PHY. MDIX register 0x1E: [3:2] P/N
 * (00 normal, 01 reversed, 10 automatic), [1:0] T/R (01 MDIX, 10 MDI).
 * While no link, the EVT's WCHNET_LinkProcess() every 50 ms (50-150 ms
 * while it tries T/R) switches T/R and P/N and restarts autonegotiation,
 * and resets the PHY after 5 s without a partner. A link that drops turns
 * the PHY's clock off for 500 ms. After a link comes up it receives
 * everything for a while: CRC errors with no good unicast mean the polarity
 * is wrong (WCHNET_PhyPNProcess()). Not ported: the chip revision 0x20 MAC
 * re-initialisation and its parallel-detection polarity alternation. */
#define PHY_SR 0x10                /* bit 2: full duplex */
#define PHY_MDIX 0x1E
#define PN_P (0u << 2)
#define PN_N (1u << 2)
#define PN_AUTO (2u << 2)
#define LINK_WAIT_SUC (1u << 7)

void NetworkEthernet::restartNegotiation()
{
    uint16_t v = phyRead(PHY_BCR);
    phyWrite(PHY_BCR, (uint16_t)((v & ~1u) | BCR_RESTART_ANEG));
    v = phyRead(PHY_BCR);
    phyWrite(PHY_BCR, (uint16_t)(v | 0x03u | BCR_RESTART_ANEG));
}

void NetworkEthernet::switchPN(uint8_t mode)
{
    uint16_t v = mode;
    if (mode != PN_AUTO) {
        v = (uint16_t)((phyRead(PHY_MDIX) & ~(0x03u << 2)) | mode);
    }
    phyWrite(PHY_MDIX, v);
    _pn = mode;
    restartNegotiation();
}

void NetworkEthernet::switchTR()
{
    uint16_t v = phyRead(PHY_MDIX);
    v = (uint16_t)((v & ~0x03u) | ((v & 1u) ? 2u : 1u));
    phyWrite(PHY_MDIX, v);
    restartNegotiation();
}

void NetworkEthernet::negotiationInit()
{
    _sucCnt = _linkCnt = _trStep = _trCnt = 0;
    _linkStatus = 0;
    _polarityCheck = false;
    _pn = PN_AUTO;
    phyWrite(PHY_MDIX, PN_AUTO);
}

void NetworkEthernet::linkProcess()
{
    const uint16_t anlpar = phyRead(PHY_ANLPAR), bsr = phyRead(PHY_BSR);
    if (anlpar & ANLPAR_SELECTOR) {
        if (_trStep == 0) {
            _trStep = 1;
            _trCnt = 1;
            switchTR();
            _rand = _rand * 214017u + 2531017u;
            _period = _rand % 100u + 50u;
            return;
        }
        if (_trStep == 1) {
            _trStep = 2;
            _trCnt = 0;
        }
        if (!(_linkStatus & LINK_WAIT_SUC)) {
            if (_pn == PN_AUTO) {
                switchPN(PN_P);
            } else {
                _linkStatus = LINK_WAIT_SUC;
            }
        } else if ((_sucCnt++ == 5) && !(bsr & BSR_ANEG_DONE)) {
            _sucCnt = 0;
            switchPN(_pn == PN_N ? PN_P : PN_N);
        }
        _linkCnt = 0;
        return;
    }
    if (_trStep == 1) {
        if (++_trCnt == 8) {
            _trCnt = 0;
            _trStep = 0;
            phyWrite(PHY_MDIX, PN_AUTO);
            return;
        }
        switchTR();
        return;
    }
    if (_linkStatus == LINK_WAIT_SUC || _pn == PN_N) {
        if (_linkCnt++ == 15) {
            _linkCnt = _sucCnt = _trStep = 0;
            _linkStatus = 0;
            switchPN(PN_AUTO);
        }
    } else if (_pn == PN_P) {
        if (_linkCnt++ == 4) {
            _linkCnt = 0;
            switchPN(PN_N);
        }
    } else if (_linkCnt++ == 5000u / 50u) {
        phyWrite(PHY_BCR, BCR_RESET);          /* PHY_LINK_RESET() */
        _phyResets++;
        negotiationInit();
    }
}

void NetworkEthernet::phyTask()
{
    if (_phyOff) {
        /* A link that dropped: the PHY's clock off for 500 ms, then a reset. */
        if (millis() - _stepAt >= 500u) {
            _phyOff = false;
            EXTEN_CTR |= EXTEN_ETH10M;
            phyWrite(PHY_BCR, BCR_RESET);
            negotiationInit();
        }
        return;
    }
    const uint16_t bsr = phyRead(PHY_BSR);
    if (bsr == 0xFFFF) {
        return;
    }
    const bool link = (bsr & BSR_LINK) != 0;
    if (link && !_linked) {
        const uint16_t bcr = phyRead(PHY_BCR), anlpar = phyRead(PHY_ANLPAR);
        _speed = 10;
        if (!(bcr & BCR_ANEG)) {
            _full = false;
        } else if (anlpar == 0) {
            if (!(bsr & BSR_ANEG_DONE)) {
                phyWrite(PHY_BCR, BCR_RESET);
                negotiationInit();
                return;
            }
            _full = false;                       /* parallel detection: half duplex */
        } else if (bsr & BSR_ANEG_DONE) {
            _full = (phyRead(PHY_SR) & (1u << 2)) || (anlpar & ANLPAR_SELECTOR) != 0x01u;
        } else {
            EXTEN_CTR &= ~EXTEN_ETH10M;
            _phyOff = true;
            _stepAt = millis();
            return;
        }
        /* ETH_LinkUpCfg(): take everything until a good unicast frame shows
         * the polarity is right. */
        MACFFR |= MACFFR_RA | MACFFR_PM;
        MMCCR |= 1u;
        for (uint32_t t0 = micros(); MMCCR & 1u;) {
            if (micros() - t0 > 10000u) {
                break;
            }
        }
        _polarityCheck = true;
        _stepAt = millis();
        startMac();
        return;
    }
    if (!link && _linked) {
        linkDown();
        EXTEN_CTR &= ~EXTEN_ETH10M;
        _phyOff = true;
        _stepAt = millis();
        return;
    }
    if (_linked) {
        if (_polarityCheck && millis() - _stepAt >= 100u) {
            _stepAt = millis();
            if (MMCRGUFCR == 0 && MMCRFCECR >= 3u) {
                const uint16_t v = phyRead(PHY_MDIX);
                phyWrite(PHY_MDIX, (uint16_t)((v >> 2) & 1u ? v & ~(3u << 2) : v | (1u << 2)));
                MMCCR |= 1u;
            }
            if (MMCRGUFCR != 0) {
                _polarityCheck = false;
                MACFFR &= ~(MACFFR_RA | MACFFR_PM);
            }
        }
        return;
    }
    /* No link: WCHNET_HandlePhyNegotiation() -> WCHNET_LinkProcess(). */
    if (_trStep < 2 && (phyRead(PHY_ANLPAR) & ANLPAR_SELECTOR)) {
        _period = 0;                             /* ACCELERATE_LINK_PROCESS() */
    }
    if (millis() - _linkAt >= _period) {
        if (_trStep == 1) {
            _rand = _rand * 214017u + 2531017u;
            _period = _rand % 100u + 50u;
        } else {
            _period = 50;
        }
        _linkAt = millis();
        linkProcess();
    }
}
#endif

void NetworkEthernet::poll()
{
    if (!_running) {
        return;
    }
    if ((int32_t)(millis() - _phyAt) >= 0) {
        _phyAt = millis() + PHY_PERIOD_MS;
        if (clockLost()) {
            _clockRestarts++;
            clockOn(20);               /* the PHY then finds its link again */
        }
        phyTask();
    }
    for (uint8_t n = 0; n < RX_N; n++) {
        Desc &d = rxDesc[_rxNext];
        const uint32_t st = d.status;
        if (st & DESC_OWN) {
            break;
        }
        const uint32_t len = (st >> 16) & 0x3FFFu;
        if (!(st & RX_ES) && (st & RX_FS) && (st & RX_LS) && len > 4u) {
            frameReceived(rxBuf[_rxNext], len - 4u);   /* without the FCS */
            _rx++;
        } else {
            _rxErrors++;
        }
        d.status = DESC_OWN;
        _rxNext = (uint8_t)((_rxNext + 1u) % RX_N);
    }
    if (DMASR & DMASR_RBUS) {
        DMASR = DMASR_RBUS;
        DMARPDR = 0;                   /* the DMA had stopped for want of descriptors */
    }
}

bool NetworkEthernet::transmitFrame(const uint8_t *frame, size_t length)
{
    if (!_running || !_linked || length > BUF) {
        return false;
    }
    Desc &d = txDesc[_txNext];
    /* A full frame takes 123 us at 100M (1.2 ms at 10M): wait for the oldest. */
    const uint32_t limit = _speed == 100 ? 500u : 3000u;
    for (uint32_t t0 = micros(); d.status & DESC_OWN;) {
        if (micros() - t0 > limit) {
            _txBusy++;
            return false;
        }
    }
    memcpy(txBuf[_txNext], frame, length);
    d.control = (uint32_t)length & 0x1FFFu;
    d.status = TX_TCH | TX_FS | TX_LS | TX_CIC_FULL;
    __asm__ volatile("fence rw, rw" ::: "memory");
    d.status = TX_TCH | TX_FS | TX_LS | TX_CIC_FULL | DESC_OWN;
    DMASR = DMASR_TBUS;
    DMATPDR = 0;
    _txNext = (uint8_t)((_txNext + 1u) % TX_N);
    _tx++;
    return true;
}

size_t NetworkEthernet::printDriverInfo(Print &out) const
{
    size_t n = out.print("Ethernet ");
    if (_linked) {
        n += out.print(_speed);
        n += out.print(_full ? "M full" : "M half");
    } else {
        n += out.print("link down");
    }
    if (_clockRestarts) {
        n += out.print(" clock restarts ");
        n += out.print(_clockRestarts);
    }
    if (_rxErrors || _txBusy) {
        n += out.print(" rx errors ");
        n += out.print(_rxErrors);
        n += out.print(" tx busy ");
        n += out.print(_txBusy);
    }
    return n;
}
#endif
