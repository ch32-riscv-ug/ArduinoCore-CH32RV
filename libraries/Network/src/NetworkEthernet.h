/* Ethernet on the chips with a Synopsys MAC and a PHY inside: CH32H417
 * (10/100), CH32V407 / V467 (10/100), CH32V307 (10BASE-T) and CH32V317
 * (10/100 on an internal RMII), as a NetworkInterface. Run on CH32H417; the
 * others are built from their EVTs and not yet run. The calls follow ESP32's
 * ETH (Arduino core 3.x):
 *
 *   #include <Network.h>
 *   ETH.begin();                      // DHCP unless config() set an address
 *   while (!ETH.hasIP()) delay(10);
 *
 * No interrupt: poll(), from the network task, takes received frames off the
 * DMA's descriptors and, every 20 ms, puts the clocks back if SystemInit()
 * turned them off and watches the PHY (link, speed, and the
 * EVT's three PHY workarounds - P/N polarity, a 100M link that carries
 * nothing, and the reset after an ESD upset).
 *
 * The MAC address is locally administered, from the chip's unique ID (not the
 * one CH32RVUsbDeviceNet makes); setMacAddress() before begin() changes it.
 * The PHY's clock comes from the crystal (HSE): 25 MHz on H417 and V407 (their
 * Ethernet PLL takes nothing else), 8 MHz by default on V307 / V317, whose
 * PLL3 is set from setHseFrequency() - an exact 60 / 100 MHz must exist.
 * docs/network-stack.ja.md */
#pragma once
#include "NetworkInterface.h"

#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || \
    defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
#define NETWORK_ETHERNET 1
#define NETWORK_ETHERNET_GMAC 1        /* NetworkEthernet.cpp */
#elif defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_PART_CH32V203RBT6)
#define NETWORK_ETHERNET 1
#define NETWORK_ETHERNET_10M 1         /* NetworkEthernet10m.cpp: the CH57x-style 10M MAC */
#endif

#if NETWORK_ETHERNET

class NetworkEthernet : public NetworkInterface {
public:
    /* Clocks (HSE, the Ethernet PLL), the MAC and the PHY, then the IP side.
     * With makeDefault (the default), it takes the default route even when
     * another interface (USB NCM) started first. */
    bool begin(bool makeDefault = true);
    void end();
    void poll() override;

    bool setMacAddress(const uint8_t mac[6]);   /* before begin() */
    bool setHseFrequency(uint32_t hz);          /* before begin(); V307 / V317 */
    uint8_t linkSpeed() const { return _speed; } /* 10 or 100 Mbit/s, 0 while the link is down */
    bool fullDuplex() const { return _full; }

    /* Counters since begin(). */
    uint32_t framesReceived() const { return _rx; }
    uint32_t framesSent() const { return _tx; }
    uint32_t receiveErrors() const { return _rxErrors; }   /* frames the MAC flagged, dropped */
    uint32_t transmitBusy() const { return _txBusy; }      /* sends refused: every descriptor in use */
    uint32_t linkChanges() const { return _linkChanges; }  /* times the link came up */
    uint32_t phyResets() const { return _phyResets; }      /* by the link checks (EVT workarounds) */
    uint32_t clockRestarts() const { return _clockRestarts; } /* the PLL found off (SystemInit() after a debugger) */

protected:
    bool transmitFrame(const uint8_t *frame, size_t length) override;
    size_t printDriverInfo(Print &out) const override;

private:
    bool clockOn(uint32_t waitMs);
    bool clockLost() const;
    void phyTask();
    void startMac();
    void linkDown();
    uint16_t phyRead(uint8_t reg) const;
    void phyWrite(uint8_t reg, uint16_t value) const;
    void phyReset();
#if defined(CH32RV_SERIES_CH32V317)
    void phyInit();
#endif
#if NETWORK_ETHERNET_10M
    void phyLink(uint16_t bsr, uint16_t anlpar, bool &up);
    void linkProcess();
#endif
#if defined(CH32RV_SERIES_CH32V307)
    void restartNegotiation();
    void switchPN(uint8_t mode);
    void switchTR();
    void negotiationInit();
    void linkProcess();
#endif

    uint8_t _mac[6] = {0};
#if defined(CH32RV_CLOCK_HSE_HZ)
    uint32_t _hseHz = CH32RV_CLOCK_HSE_HZ;     /* the Clock Source menu's crystal */
#elif defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317)
    uint32_t _hseHz = 8000000;
#else
    uint32_t _hseHz = 25000000;
#endif
    uint32_t _maccr = 0;            /* MACCR without speed, duplex, TE, RE */
    bool _phyOff = false;           /* V307: the PHY's clock off after a link drop */
    uint8_t _step = 0;              /* V317: the accelerate-link sequence */
    uint32_t _stepAt = 0;
    /* V307: WCHNET_LinkProcess()'s state */
    uint8_t _pn = 0, _sucCnt = 0, _linkCnt = 0, _trStep = 0, _trCnt = 0, _linkStatus = 0;
    bool _polarityCheck = false;
    uint32_t _rand = 0, _period = 50, _linkAt = 0;
    bool _macSet = false, _running = false, _linked = false, _full = false;
    uint8_t _speed = 0;
    uint8_t _rxNext = 0, _txNext = 0;
    uint32_t _phyAt = 0;            /* millis() of the next PHY look */
    uint32_t _pnSince = 0;          /* the P/N check's start, 0 when idle */
    uint8_t _invalid = 0, _esd = 0; /* consecutive bad looks while linked */
    uint32_t _rx = 0, _tx = 0, _rxErrors = 0, _txBusy = 0, _linkChanges = 0, _phyResets = 0, _clockRestarts = 0;
};

extern NetworkEthernet ETH;
#endif
