#include "NetworkEthernet.h"

#if NETWORK_ETHERNET
#include <ch32rv_registers.h>
#include <string.h>

#include "lwip/netif.h"

/* RCC */
#define RCC_CTLR    CH32RV_REG32(0x40021000u)
#define RCC_HBPCENR CH32RV_REG32(0x40021018u)
#define CTLR_HSEON (1u << 16)
#define CTLR_HSERDY (1u << 17)
#define CTLR_ETHPLLON (1u << 26)
#define CTLR_ETHPLLRDY (1u << 27)
#define HBPCENR_ETHMAC (1u << 14)

/* MAC (0x40028000) and its DMA (0x40029000): Synopsys GMAC, as the EVT's
 * ch32h417_eth.h names them. */
#define MAC(off)   CH32RV_REG32(0x40028000u + (off))
#define MACCR      MAC(0x00)
#define MACFFR     MAC(0x04)
#define MACMIIAR   MAC(0x10)
#define MACMIIDR   MAC(0x14)
#define MACFCR     MAC(0x18)
#define MACA0HR    MAC(0x40)
#define MACA0LR    MAC(0x44)
#define MACPHYCR   MAC(0x80)
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
#define MACCR_IPCO (1u << 10)      /* checksum offload: received IPv4 and TCP/UDP/ICMP checked */
#define MACCR_DM (1u << 11)        /* full duplex */
#define MACCR_SPEED (3u << 14)
#define MACCR_100M (1u << 14)
#define MACFFR_PAM (1u << 4)       /* pass all multicast (mDNS) */
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

/* The internal PHY (MDIO address 1) and its vendor registers. */
#define PHY_ADDR 1u
#define PHY_BCR 0x00
#define PHY_BSR 0x01
#define PHY_ANLPAR 0x05
#define PHY_ANER 0x06
#define PHY_INT_MASK 0x13
#define PHY_ESD 0x14
#define PHY_CONTROL1 0x19
#define PHY_STATUS 0x1A
#define PHY_PAGE 0x1F
#define BCR_RESET 0x8000u
#define BCR_100M (1u << 13)
#define BCR_ANEG (1u << 12)
#define BSR_LINK (1u << 2)
#define BSR_ANEG_DONE (1u << 5)
#define STATUS_FULL (1u << 1)
#define STATUS_100M (1u << 3)

static const uint16_t BUF = 1536;
static const uint8_t RX_N = 8, TX_N = 4;
static const uint32_t PHY_PERIOD_MS = 20;

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

/* HSE, the Ethernet PLL from it, the MAC's clock. SystemInit() - which the
 * core runs again when a debugger has moved the clock - turns HSE and the
 * PLL off, so poll() calls this again then. */
static bool clockOn(uint32_t waitMs)
{
    if (!(RCC_CTLR & CTLR_HSERDY)) {
        RCC_CTLR |= CTLR_HSEON;
        if (!waitSet(RCC_CTLR, CTLR_HSERDY, waitMs)) {
            return false;
        }
    }
    if (!(RCC_CTLR & CTLR_ETHPLLRDY)) {
        RCC_CTLR |= CTLR_ETHPLLON;
        if (!waitSet(RCC_CTLR, CTLR_ETHPLLRDY, waitMs)) {
            return false;
        }
    }
    RCC_HBPCENR |= HBPCENR_ETHMAC;
    return true;
}

uint16_t NetworkEthernet::phyRead(uint8_t reg) const
{
    /* CR = 0: MDC = HCLK / 42, for HCLK 60-100 MHz */
    MACMIIAR = (PHY_ADDR << 11) | ((uint32_t)(reg & 31u) << 6) | 1u;
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
    MACMIIAR = (PHY_ADDR << 11) | ((uint32_t)(reg & 31u) << 6) | 2u | 1u;
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

    if (!clockOn(100)) {
        return false;
    }
    MACPHYCR &= ~(1u << 30);           /* PHY out of power-down */
    MACPHYCR |= 1u << 31;              /* and out of reset */

    DMABMR |= DMABMR_SR;
    for (uint32_t t0 = millis(); DMABMR & DMABMR_SR;) {
        if (millis() - t0 > 100u) {
            return false;
        }
    }
    MACMIIAR = 0;
    MACCR = MACCR_IPCO;                /* speed and duplex once the link is up */
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

    phyWrite(PHY_BCR, BCR_RESET);
    _linked = false;
    _speed = 0;
    _pnSince = 0;
    _invalid = _esd = 0;
    _rx = _tx = _rxErrors = _txBusy = _linkChanges = _phyResets = _clockRestarts = 0;
    _phyAt = millis();
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
    MACPHYCR |= 1u << 30;              /* PHY power-down */
    _running = false;
    _linked = false;
    _speed = 0;
}

/* The EVT's ETH_LinkUpCfg(): its PHY tuning write, speed and duplex from the
 * PHY's status, then the MAC and the DMA start. */
void NetworkEthernet::linkUp()
{
    CH32RV_REG32(0x4002A00Cu) = 0xFFFFF;
    CH32RV_REG32(0x4002A00Cu) = 0xFFFFF;
    CH32RV_REG32(0x4002A00Cu) = 0xFFFFF;
    CH32RV_REG32(0x4002A00Cu) = 0xC2000;
    phyWrite(PHY_PAGE, 0);
    const uint16_t st = phyRead(PHY_STATUS);
    _speed = (st & STATUS_100M) ? 100 : 10;
    _full = (st & STATUS_FULL) != 0;
    uint32_t cr = MACCR & ~(MACCR_SPEED | MACCR_DM);
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

void NetworkEthernet::phyReset()
{
    _phyResets++;
    const uint16_t bcr = phyRead(PHY_BCR);
    phyWrite(PHY_BCR, BCR_RESET);
    delayMicroseconds(100);
    phyWrite(PHY_BCR, bcr);
}

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
            linkUp();
        }
        return;
    }
    if (!link && _linked) {
        _linked = false;
        _speed = 0;
        setLinkUp(false);
        return;
    }
    if (!_linked) {
        /* The EVT's WCHNET_CheckPHYPN(): a partner seen for 300 ms (200 at a
         * forced 10M) without a link - try the other polarity. */
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
    /* Linked. The EVT's WCHNET_CheckLinkVaild(): a 100M link whose page-99
     * flag stays clear for 5 looks carries nothing - reset the PHY. */
    if (bcr & BCR_100M) {
        phyWrite(PHY_PAGE, 99);
        const uint16_t ok = phyRead(0x1B);
        phyWrite(PHY_PAGE, 0);
        if (!(ok & (1u << 2))) {
            if (++_invalid >= 5) {
                _invalid = 0;
                phyReset();
                _linked = false;
                _speed = 0;
                setLinkUp(false);
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
            _linked = false;
            _speed = 0;
            setLinkUp(false);
        }
    } else {
        _esd = 0;
    }
}

void NetworkEthernet::poll()
{
    if (!_running) {
        return;
    }
    if ((int32_t)(millis() - _phyAt) >= 0) {
        _phyAt = millis() + PHY_PERIOD_MS;
        if (!(RCC_CTLR & CTLR_ETHPLLRDY) || !(RCC_HBPCENR & HBPCENR_ETHMAC)) {
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
    /* A full frame takes 123 us at 100M: 500 us lets the oldest of the four go. */
    for (uint32_t t0 = micros(); d.status & DESC_OWN;) {
        if (micros() - t0 > 500u) {
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
