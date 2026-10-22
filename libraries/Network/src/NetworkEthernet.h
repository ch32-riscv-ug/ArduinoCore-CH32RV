/* The CH32H417's Ethernet: its MAC and the 10/100 PHY inside the chip, as a
 * NetworkInterface. The calls follow ESP32's ETH (Arduino core 3.x):
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
 * docs/network-stack.ja.md */
#pragma once
#include "NetworkInterface.h"

#if defined(CH32RV_SERIES_CH32H417)
#define NETWORK_ETHERNET 1

class NetworkEthernet : public NetworkInterface {
public:
    /* Clocks (HSE, the Ethernet PLL), the MAC and the PHY, then the IP side.
     * With makeDefault (the default), it takes the default route even when
     * another interface (USB NCM) started first. */
    bool begin(bool makeDefault = true);
    void end();
    void poll() override;

    bool setMacAddress(const uint8_t mac[6]);   /* before begin() */
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
    void phyTask();
    void linkUp();
    uint16_t phyRead(uint8_t reg) const;
    void phyWrite(uint8_t reg, uint16_t value) const;
    void phyReset();

    uint8_t _mac[6] = {0};
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
