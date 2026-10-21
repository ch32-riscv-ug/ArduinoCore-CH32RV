/* One network interface - an Ethernet MAC, a USB network function - on lwIP.
 * The API follows ESP32's NetworkInterface (Arduino core 3.x), so ETH-style
 * sketches carry over.
 *
 * Addressing: DHCP by default; config() with a local address makes it static.
 * Call config() before the driver's begin(), or after it to change the
 * address of a running interface.
 *
 * A link driver derives from this class. It implements transmitFrame(), hands
 * received frames to frameReceived() and reports the link with setLinkUp(),
 * all in task context (its poll(), or a USB stack callback) - never from an
 * interrupt. netifStart() / netifStop() bring the lwIP side up and down. */
#pragma once
#include <Arduino.h>

struct netif;

class NetworkInterface : public Printable {
public:
    NetworkInterface();
    virtual ~NetworkInterface();

    /* All zero: DHCP. local, gateway and subnet: a static address. dns1/dns2
     * set the DNS servers either way (DHCP's own are used when they are 0). */
    bool config(IPAddress local = (uint32_t)0, IPAddress gateway = (uint32_t)0, IPAddress subnet = (uint32_t)0,
                IPAddress dns1 = (uint32_t)0, IPAddress dns2 = (uint32_t)0, IPAddress dns3 = (uint32_t)0);
    bool dnsIP(uint8_t dns_no, IPAddress ip);

    /* Sent to the DHCP server (option 12). Default: "ch32rv-" and the last three
     * bytes of the MAC address. Set before the driver's begin(). */
    const char *getHostname() const;
    bool setHostname(const char *hostname);

    bool started() const;      /* the driver started it */
    bool linkUp() const;       /* cable / USB host present */
    bool connected() const;    /* the same as linkUp(), as on ESP32 */
    bool hasIP() const;

    /* The interface that traffic without a better match leaves through (the
     * first one started, until this is called). */
    bool setDefault();
    bool isDefault() const;

    uint8_t *macAddress(uint8_t *mac) const;
    String macAddress() const;
    IPAddress localIP() const;
    IPAddress subnetMask() const;
    IPAddress gatewayIP() const;
    IPAddress dnsIP(uint8_t dns_no = 0) const;
    IPAddress broadcastIP() const;
    IPAddress networkID() const;
    uint8_t subnetCIDR() const;

    size_t printTo(Print &out) const override;

    /* For the library: lwIP's netif, or nullptr before netifStart(). */
    struct netif *netif() const;

    /* For the task: drain what the driver queued. */
    virtual void poll() {}

protected:
    /* One whole Ethernet frame (destination MAC .. payload, no FCS) to send.
     * The data is valid only during the call; copy it if it must outlive it.
     * false when there is no room now (the frame is dropped; TCP resends). */
    virtual bool transmitFrame(const uint8_t *frame, size_t length) = 0;
    virtual size_t printDriverInfo(Print &out) const;

    bool netifStart(const uint8_t mac[6], uint16_t mtu = 1500);
    void netifStop();
    /* A received frame, copied in. Task context only. */
    void frameReceived(const uint8_t *frame, size_t length);
    void setLinkUp(bool up);

private:
    void applyAddress();
    static int8_t output(struct netif *n, struct pbuf *p);   /* lwIP's linkoutput */
    static int8_t init(struct netif *n);
    static void statusChanged(struct netif *n);

    struct netif *_netif = nullptr;    /* allocated by netifStart() */
    uint32_t _ip = 0, _gateway = 0, _mask = 0, _dns[2] = {0, 0};   /* network byte order, as lwIP */
    uint8_t _mac[6] = {0};
    char _hostname[33] = {0};
    bool _link = false;
};
