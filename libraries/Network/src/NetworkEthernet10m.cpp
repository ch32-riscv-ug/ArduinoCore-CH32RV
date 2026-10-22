/* ETH on CH32V208 / V203RB: a 10M MAC with its PHY (the CH57x family's - not
 * the Synopsys MAC of NetworkEthernet.cpp), after the EVT's
 * CH32V20x/EVT/EXAM/ETH/NetLib/eth_driver.c. One receive buffer address and
 * one transmit buffer address in the MAC: the interrupt moves reception on
 * to the next free buffer of four, poll() hands the full ones to lwIP. No
 * checksum engine (lwIP computes them) and no MDIO: the PHY's registers are
 * the MAC's MIREGADR / MIRD / MIWR.
 *
 * The PHY wants a 60 MHz clock from HCLK / 1 or / 2 (RCC CFGR0 ETHPRE), so
 * HCLK must be 60 or 120 MHz; the core runs these parts at 96 MHz, and
 * begin() fails there. Built, not yet run on a board. */
#include "NetworkEthernet.h"

#if NETWORK_ETHERNET_10M
#include <ch32rv_registers.h>
#include <string.h>

#define R8(off)  (*(volatile uint8_t *)(0x40028000u + (off)))
#define R16(off) (*(volatile uint16_t *)(0x40028000u + (off)))
#define R32(off) (*(volatile uint32_t *)(0x40028000u + (off)))
#define EIE      R8(0x03)
#define EIR      R8(0x04)
#define ESTAT    R8(0x05)
#define ECON2    R8(0x06)
#define ECON1    R8(0x07)
#define ETXST    R16(0x08)
#define ETXLN    R16(0x0A)
#define ERXST    R16(0x0C)
#define ERXLN    R16(0x0E)
#define ERXFCON  R8(0x18)
#define MACON1   R8(0x19)
#define MACON2   R8(0x1A)
#define MAMXFL   R16(0x1E)
#define MIRD     R16(0x20)
#define MIREGADR R8(0x24)
#define MIWR     R32(0x24)
#define MAADR(n) R8(0x28 + (n))          /* MAADRL1 (n = 0) holds mac[5] */
#define EXTEN_CTR CH32RV_REG32(0x40023800u)
#define EXTEN_ETH10M (1u << 2)           /* the PHY and its clock */
#define RCC_CFGR0 CH32RV_REG32(0x40021004u)
#define CFGR0_ETHPRE (1u << 28)          /* PHY clock = HCLK / 2 */

#define EIE_INTIE 0x80u
#define EIE_RXIE 0x40u
#define EIE_LINKIE 0x10u
#define EIE_TXIE 0x08u
#define EIE_R_EN50 0x04u                 /* the on-chip 50-ohm termination */
#define EIE_TXERIE 0x02u
#define EIE_RXERIE 0x01u
#define EIR_RXIF 0x40u
#define EIR_LINKIF 0x10u
#define EIR_TXIF 0x08u
#define EIR_TXERIF 0x02u
#define EIR_RXERIF 0x01u
#define ESTAT_INT 0x80u
#define ESTAT_BUFER 0x40u
#define ESTAT_RXCRCER 0x20u
#define ESTAT_RXNIBBLE 0x10u
#define ESTAT_RXMORE 0x08u
#define ECON1_TXRST 0x80u
#define ECON1_RXRST 0x40u
#define ECON1_TXRTS 0x08u
#define ECON1_RXEN 0x04u
#define ERXFCON_CRCEN 0x20u
#define MACON1_MARXEN 0x01u
#define MACON2_PADCFG 0xE0u
#define PADCFG_AUTO_3 (1u << 5)          /* pad short frames to 60 bytes, then the CRC */
#define MACON2_TXCRCEN 0x10u
#define MACON2_HFRMEN 0x04u
#define MACON2_FULDPX 0x01u

#define PHY_BCR 0x00
#define PHY_BSR 0x01
#define PHY_ANLPAR 0x05
#define PHY_MDIX 0x1E
#define BCR_RESET 0x8000u
#define BCR_RESTART_ANEG (1u << 9)
#define BSR_LINK (1u << 2)
#define BSR_ANEG_DONE (1u << 5)
#define ANLPAR_SELECTOR 0x1Fu
#define PN_N (1u << 2)
#define LINK_INIT 0x00u
#define LINK_SUC_N (1u << 1)
#define LINK_WAIT_SUC (1u << 7)

static const uint16_t BUF = 1536;
static const uint8_t RX_N = 4;
static const uint32_t TASK_MS = 50;      /* PHY_LINK_TASK_PERIOD */

__attribute__((aligned(4))) static uint8_t rxBuf[RX_N][BUF];
__attribute__((aligned(4))) static uint8_t txBuf[BUF];
static volatile uint16_t rxLen[RX_N];    /* 0: free; else a whole frame waiting for poll() */
static volatile uint8_t rxFill, rxTake;  /* the buffer the MAC writes, the next one to hand over */
static volatile bool linkEvent, polarityDetect;
static volatile uint8_t crcErrors;
static volatile uint32_t rxErrors;

/* WCHNET_LinkProcess()'s and WCHNET_HandlePhyNegotiation()'s state. */
static uint8_t phyPN = 0x01, sucCnt, linkCnt, retryCnt, pnChangeCnt, linkStatus;
static bool phyLinked, phyLinkReset;
static uint32_t phyTime, taskAt;
static uint16_t lastBsr = 0xFFFF;

NetworkEthernet ETH;

uint16_t NetworkEthernet::phyRead(uint8_t reg) const
{
    MIREGADR = reg;
    return MIRD;
}

void NetworkEthernet::phyWrite(uint8_t reg, uint16_t value) const
{
    MIWR = (uint32_t)(reg & 0x1Fu) | (1u << 8) | ((uint32_t)value << 16);
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
    (void)hz;                            /* the PHY's clock is HCLK's here */
    return !_running;
}

static void negotiationInit()
{
    sucCnt = linkCnt = retryCnt = pnChangeCnt = 0;
    linkStatus = LINK_INIT;
    phyLinked = false;
}

extern "C" __attribute__((interrupt)) void ETH_IRQHandler(void)
{
    const uint8_t f = EIR;
    if (f & EIR_RXIF) {
        EIR = EIR_RXIF;
        if (!(ESTAT & (ESTAT_BUFER | ESTAT_RXCRCER | ESTAT_RXNIBBLE | ESTAT_RXMORE))) {
            const uint8_t next = (uint8_t)((rxFill + 1u) % RX_N);
            if (!rxLen[next]) {
                rxLen[rxFill] = ERXLN;   /* without the FCS (the EVT adds 4 for its descriptor) */
                rxFill = next;
                ERXST = (uint16_t)(uintptr_t)rxBuf[next];
            } else {
                rxErrors++;              /* nowhere to go next: this buffer takes the next frame */
            }
        } else {
            rxErrors++;
        }
        if (polarityDetect) {
            polarityDetect = false;      /* a good frame: the polarity is right */
            ERXFCON &= ~ERXFCON_CRCEN;
        }
    }
    if (f & (EIR_TXIF | EIR_TXERIF)) {
        EIR = f & (EIR_TXIF | EIR_TXERIF);
    }
    if (f & EIR_LINKIF) {
        EIR = EIR_LINKIF;
        linkEvent = true;
    }
    if (f & EIR_RXERIF) {
        EIR = EIR_RXERIF;
        if (polarityDetect) {
            crcErrors++;
        }
    }
}

bool NetworkEthernet::begin(bool makeDefault)
{
    if (_running) {
        return true;
    }
#if F_CPU == 120000000
    RCC_CFGR0 |= CFGR0_ETHPRE;
#elif F_CPU == 60000000
    RCC_CFGR0 &= ~CFGR0_ETHPRE;
#else
    (void)makeDefault;
    return false;                        /* the PHY needs exactly 60 MHz: HCLK 60 or 120 MHz */
#endif
#if F_CPU == 120000000 || F_CPU == 60000000
    if (!_macSet) {
        /* Locally administered, unicast, from the 64-bit unique ID (as on the
         * other series, not CH32RVUsbDeviceNet's). */
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
    delay(100);                          /* the EVT waits before touching the MAC */
    EIE = EIE_INTIE | EIE_RXIE | EIE_LINKIE | EIE_TXIE | EIE_TXERIE | EIE_RXERIE | EIE_R_EN50;
    EIR = 0xFF;
    ESTAT |= ESTAT_INT | ESTAT_BUFER;
    ECON1 |= ECON1_TXRST | ECON1_RXRST;
    ECON1 &= (uint8_t)~(ECON1_TXRST | ECON1_RXRST);
    ERXFCON = 0;                         /* every frame; lwIP drops what is not ours */
    for (uint8_t i = 0; i < 6; i++) {
        MAADR(i) = _mac[5 - i];
    }
    MACON1 |= MACON1_MARXEN;
    MACON2 = (uint8_t)((MACON2 & ~(MACON2_PADCFG | MACON2_HFRMEN)) | PADCFG_AUTO_3 | MACON2_TXCRCEN | MACON2_FULDPX);
    MAMXFL = BUF;
    ECON2 = (uint8_t)((ECON2 & ~(0x07u << 1)) | (5u << 1));
    EXTEN_CTR |= EXTEN_ETH10M;

    for (uint8_t i = 0; i < RX_N; i++) {
        rxLen[i] = 0;
    }
    rxFill = rxTake = 0;
    ERXST = (uint16_t)(uintptr_t)rxBuf[0];
    linkEvent = polarityDetect = false;
    crcErrors = 0;
    rxErrors = 0;
    phyPN = 0x01;
    phyLinkReset = false;
    negotiationInit();
    lastBsr = 0xFFFF;
    phyTime = taskAt = millis();
    _linked = false;
    _speed = 0;
    _rx = _tx = _rxErrors = _txBusy = _linkChanges = _phyResets = _clockRestarts = 0;
    _running = true;
    ch32rv_irq_enable(CH32RV_IRQN_ETH);

    if (!netifStart(_mac)) {
        _running = false;
        ch32rv_irq_disable(CH32RV_IRQN_ETH);
        return false;
    }
    if (makeDefault) {
        setDefault();
    }
    return true;
#endif
}

void NetworkEthernet::end()
{
    if (!_running) {
        return;
    }
    netifStop();
    ch32rv_irq_disable(CH32RV_IRQN_ETH);
    ECON1 &= (uint8_t)~ECON1_RXEN;
    EXTEN_CTR &= ~EXTEN_ETH10M;
    _running = false;
    _linked = false;
    _speed = 0;
}

/* The EVT's ETH_PHYLink(), on a link change. */
void NetworkEthernet::phyLink(uint16_t bsr, uint16_t anlpar, bool &up)
{
    up = false;
    if ((bsr & BSR_LINK) && (bsr & BSR_ANEG_DONE)) {
        /* ETH_LinkUpCfg(): keep CRC-error frames until a good one shows the
         * polarity is right; start receiving. */
        ERXFCON |= ERXFCON_CRCEN;
        crcErrors = 0;
        polarityDetect = true;
        phyTime = millis();
        phyLinked = true;
        ERXST = (uint16_t)(uintptr_t)rxBuf[rxFill];
        ECON1 |= ECON1_RXEN;
        up = true;
        return;
    }
    if ((bsr & BSR_LINK) && anlpar == 0) {
        phyWrite(PHY_BCR, BCR_RESET);
        _phyResets++;
        negotiationInit();
        return;
    }
    /* ETH_LinkDownCfg(): the PHY's clock off for 500 ms. */
    EXTEN_CTR &= ~EXTEN_ETH10M;
    phyLinkReset = true;
    phyTime = millis();
    phyLinked = false;
}

/* WCHNET_LinkProcess(): while there is no link, every 50 ms - P/N and T/R. */
void NetworkEthernet::linkProcess()
{
    const uint16_t anlpar = phyRead(PHY_ANLPAR), bsr = phyRead(PHY_BSR);
    const auto restart = [this]() { phyWrite(PHY_BCR, (uint16_t)(phyRead(PHY_BCR) | BCR_RESTART_ANEG)); };
    if (anlpar & ANLPAR_SELECTOR) {
        if (!(linkStatus & LINK_WAIT_SUC)) {
            if ((phyPN & 0x0Cu) == 0u) {
                sucCnt = linkCnt = 0;
                linkStatus = LINK_WAIT_SUC;
            } else if (!(linkStatus & LINK_SUC_N)) {
                retryCnt = 0;
                linkStatus |= LINK_SUC_N;
                phyPN &= (uint8_t)~PN_N;
                restart();
                phyWrite(PHY_MDIX, phyPN);
            } else {
                sucCnt = linkCnt = 0;
                linkStatus = LINK_WAIT_SUC;
            }
        } else if ((sucCnt++ == 5) && !(bsr & BSR_ANEG_DONE)) {
            sucCnt = retryCnt = pnChangeCnt = 0;
            linkStatus = LINK_INIT;
            restart();
            phyPN = (phyPN & 0x0Cu) == 0u ? (uint8_t)(phyPN | PN_N) : (uint8_t)(phyPN & ~PN_N);
            phyWrite(PHY_MDIX, phyPN);
        }
        return;
    }
    if (bsr & BSR_ANEG_DONE) {
        sucCnt = linkCnt = 0;
        linkStatus = LINK_WAIT_SUC;
        return;
    }
    if (linkStatus == LINK_WAIT_SUC) {
        if (linkCnt++ == 10) {
            linkCnt = retryCnt = pnChangeCnt = 0;
            linkStatus = LINK_INIT;
        }
    } else if (linkStatus == LINK_INIT) {
        if (pnChangeCnt++ == 10) {
            pnChangeCnt = 0;
            phyPN = (uint8_t)(((phyRead(PHY_MDIX) & ~0x0Cu) ^ 0x03u) & 0xFFu);
        } else {
            phyPN = (phyPN & 0x0Cu) == 0u ? (uint8_t)(phyPN | PN_N) : (uint8_t)(phyPN & ~PN_N);
        }
        phyWrite(PHY_MDIX, phyPN);
    } else if (linkStatus == LINK_SUC_N) {
        if ((phyPN & 0x0Cu) == 0u) {
            phyPN |= PN_N;
            restart();
            delayMicroseconds(10);
            phyWrite(PHY_MDIX, phyPN);
        } else if (retryCnt++ == 15) {
            retryCnt = pnChangeCnt = 0;
            linkStatus = LINK_INIT;
        }
    }
}

void NetworkEthernet::poll()
{
    if (!_running) {
        return;
    }
    /* Frames the interrupt filled, oldest first. */
    while (rxLen[rxTake]) {
        const uint16_t len = rxLen[rxTake];
        if (len <= BUF) {
            frameReceived(rxBuf[rxTake], len);
            _rx++;
        }
        rxLen[rxTake] = 0;
        rxTake = (uint8_t)((rxTake + 1u) % RX_N);
    }
    _rxErrors = rxErrors;
    if (millis() - taskAt < 10u) {
        return;
    }
    taskAt = millis();
    /* WCHNET_HandlePhyNegotiation() */
    if (phyLinkReset) {
        if (millis() - phyTime >= 500u) {
            phyLinkReset = false;
            EXTEN_CTR |= EXTEN_ETH10M;
            phyWrite(PHY_BCR, BCR_RESET);
            negotiationInit();
        }
        return;
    }
    const uint16_t bsr = phyRead(PHY_BSR);
    if (linkEvent || bsr != lastBsr) {
        linkEvent = false;
        lastBsr = bsr;
        bool up;
        phyLink(bsr, phyRead(PHY_ANLPAR), up);
        if (up && !_linked) {
            _speed = 10;
            _full = true;                /* MACON2.FULDPX, as the EVT */
            _linked = true;
            _linkChanges++;
            setLinkUp(true);
        } else if (!up && _linked) {
            _linked = false;
            _speed = 0;
            setLinkUp(false);
        }
    }
    if (!phyLinked) {
        if (millis() - phyTime >= TASK_MS) {
            phyTime = millis();
            linkProcess();
        }
    } else if (polarityDetect && millis() - phyTime >= 2u * TASK_MS) {
        /* WCHNET_PhyPNProcess(): CRC errors and no good frame - the other polarity. */
        phyTime = millis();
        if (crcErrors >= 3u) {
            const uint16_t v = phyRead(PHY_MDIX);
            phyWrite(PHY_MDIX, (uint16_t)((v >> 2) & 1u ? v & ~(3u << 2) : v | (1u << 2)));
            crcErrors = 0;
        }
    }
}

bool NetworkEthernet::transmitFrame(const uint8_t *frame, size_t length)
{
    if (!_running || !_linked || length > BUF) {
        return false;
    }
    /* One frame at a time; a full one takes 1.2 ms at 10M. */
    for (uint32_t t0 = micros(); ECON1 & ECON1_TXRTS;) {
        if (micros() - t0 > 3000u) {
            _txBusy++;
            return false;
        }
    }
    memcpy(txBuf, frame, length);
    ETXLN = (uint16_t)length;
    ETXST = (uint16_t)(uintptr_t)txBuf;
    ECON1 |= ECON1_TXRTS;
    _tx++;
    return true;
}

size_t NetworkEthernet::printDriverInfo(Print &out) const
{
    size_t n = out.print("Ethernet 10M ");
    n += out.print(_linked ? "link up" : "link down");
    if (_rxErrors || _txBusy) {
        n += out.print(" rx errors ");
        n += out.print(_rxErrors);
        n += out.print(" tx busy ");
        n += out.print(_txBusy);
    }
    return n;
}
#endif
