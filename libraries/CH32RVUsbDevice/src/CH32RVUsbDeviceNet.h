/* USB network adapter (CDC-NCM, EspUsbDeviceNet): the host sees an Ethernet
 * interface - Windows 10+, macOS and Linux bind it with no driver install -
 * and the sketch exchanges raw Ethernet frames with it. Included by
 * CH32RVUsbDevice.h.
 *
 * EspUsbDevice's IP layer (ipConfig(), dhcpServer(), beginNetwork() ...) is
 * declared here and provided by the Network library: a sketch that calls it
 * includes <Network.h> too (CH32V208, V307/V317, V407/V467, H415-H417 - the
 * series the Network library is for). Without beginNetwork() this stays a
 * raw-frame link. */
#pragma once

class NetworkUsbNcm;

class CH32RVUsbDeviceNet : public CH32RVUsbDeviceFunction {
public:
    /* callback(frame, length): a whole Ethernet frame from the host. Runs
     * inside the USB stack; the data is valid only during the call. */
    typedef CH32RVCallback<void(const uint8_t *frame, size_t length)> FrameCallback;

    explicit CH32RVUsbDeviceNet(CH32RVUsbDevice &device = USBDevice);

    /* The 6-byte MAC address the host's side of the link takes. Call before
     * begin(). Default: locally administered, from the chip's unique ID, so
     * two boards on one host differ. */
    void macAddress(const uint8_t mac[6]);
    const uint8_t *macAddress() const;

    void onFrame(FrameCallback cb) { _onFrame = cb; }
    /* Flow control for a receiver that queues frames: after each block from
     * the host, gate() is asked whether another block (up to 2 KB of frames)
     * fits. When it says no, the endpoint is not re-armed and the host waits
     * (USB holds the data back; nothing is lost) until resumeReceive(). */
    typedef CH32RVCallback<bool()> ReceiveGate;
    void receiveGate(ReceiveGate gate) { _receiveGate = gate; }
    void resumeReceive();
    /* Send one Ethernet frame (at most 1514 bytes) to the host, copying it.
     * Waits up to timeoutMs for a transmit buffer; false when none freed up
     * or the link is down. */
    bool sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs = 10);
    /* The host has configured the interface. */
    bool linkUp() const;

    /* --- EspUsbDevice's IP layer, from the Network library (#include <Network.h>) ---
     * The device's own address, gateway and mask: the static address, and the
     * DHCP server's base. Default 192.168.7.1 / 192.168.7.1 / 255.255.255.0.
     * Call before beginNetwork(). */
    void ipConfig(IPAddress local, IPAddress gateway, IPAddress subnet);
    /* Hand the USB host an address (192.168.7.2 ... by default). Off by default;
     * excludes dhcpClient(). */
    void dhcpServer(bool enable);
    /* Take an address from the host's network instead (a host that bridges or
     * shares its connection). Off by default; excludes dhcpServer(). */
    void dhcpClient(bool enable);
    /* The DHCP server offers no router and no DNS unless told to: a host that
     * takes this link as its default route when nothing forwards behind it
     * loses its other traffic. */
    void dhcpAdvertiseGateway(bool enable);
    void dhcpDns(IPAddress dns);
    /* Make this link the device's own default route. Off by default, so it
     * never takes over from an Ethernet interface. */
    void defaultRoute(bool enable);
    /* Bring the IP interface up on this function. Call after USBDevice.begin(). */
    bool beginNetwork();
    bool networkUp() const;
    IPAddress localIP() const;
    /* The Network library's interface for this link (a NetworkInterface:
     * hasIP(), printTo() ...), or nullptr before the first call above. */
    NetworkUsbNcm *networkInterface() const { return _network; }

    /* For the USB stack's callbacks. */
    void handleFrame(const uint8_t *data, size_t length);

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    bool usbSetInterface(uint8_t interfaceNumber, uint8_t alternate) override;
    void usbTransferred(uint8_t address, uint16_t length) override;

private:
    void notify();
    void receiveBlock(uint16_t length);
    FrameCallback _onFrame = nullptr;
    ReceiveGate _receiveGate = nullptr;
    NetworkUsbNcm *_network = nullptr;   /* created by the Network library on first use */
};
