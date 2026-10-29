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

struct CH32RVUsbNetStats {
    uint32_t txFrames = 0, rxFrames = 0, invalidNtbs = 0;
};

/* Transfer blocks (NTB16; IN is device to host) in EspUsbNetConfig's names.
 * 0 keeps the class's own: one 2048-byte block each way, eight frames a
 * block, 1514-byte frames. Blocks are 2048..65535 bytes. More than one
 * buffer, or a bigger block, needs the sketch's array (txBuffer /
 * rxBuffer, 4-byte aligned, kept from begin() to end()): txBuffers x the
 * block rounded up to 4 bytes, rxBuffers x the block rounded up to the
 * series' largest packet. With two or more transmit buffers, frames sent
 * while a block is on the wire are gathered into the next one; with two
 * or more receive buffers the next block from the host comes in while
 * onFrame() still works through the last. At most 8 buffers each way. */
struct CH32RVUsbNetConfig {
    size_t ntbInBytes = 0, ntbOutBytes = 0;
    size_t txBuffers = 0, rxBuffers = 0;
    size_t maxDatagrams = 0;        /* frames in one block (both ways), 1..32 */
    uint16_t maxFrameBytes = 0;     /* the Ethernet frame with its header, 14 and up */
    uint8_t *txBuffer = nullptr, *rxBuffer = nullptr;
    size_t txBufferBytes = 0, rxBufferBytes = 0;
    const char *name = nullptr;     /* nullptr keeps the function's name */
};

class CH32RVUsbDeviceNet : public CH32RVUsbDeviceFunction {
public:
    /* callback(frame, length): a whole Ethernet frame from the host; the data
     * is valid only during the call. */
    typedef CH32RVCallback<void(const uint8_t *frame, size_t length)> FrameCallback;

    explicit CH32RVUsbDeviceNet(CH32RVUsbDevice &device = USBDevice);

    /* While the device is stopped; false (nothing changed) for a setting
     * that cannot be used. */
    bool configure(const CH32RVUsbNetConfig &config);
    const CH32RVUsbNetConfig &configuration() const { return _config; }
    /* The block sizes and buffer counts in use; 0 while the device is stopped. */
    size_t ntbInCapacity() const;
    size_t ntbOutCapacity() const;
    size_t txBufferCount() const;
    size_t rxBufferCount() const;

    /* The 6-byte MAC address the host's side of the link takes. Call before
     * begin(). Default: locally administered, from the chip's unique ID, so
     * two boards on one host differ. */
    bool macAddress(const uint8_t mac[6]);
    const uint8_t *macAddress() const;

    /* For the sketch: run from deliverEvents(). The host's block of frames is
     * held until then (the host waits), and the next one is received once
     * the last frame's call returns. */
    void onFrame(FrameCallback cb) { _onFrame = cb; deliverWith(deliverThis); }
    /* For a network stack (the Network library): called inside the USB
     * service as each frame arrives - copy it and return, never enter the
     * stack from here. Takes the frames instead of onFrame(). */
    void onStackFrame(FrameCallback cb) { _stackFrame = cb; }
    /* Flow control for a receiver that queues frames: after each block from
     * the host, gate() is asked whether another block (up to 2 KB of frames)
     * fits. When it says no, the endpoint is not re-armed and the host waits
     * (USB holds the data back; nothing is lost) until resumeReceive(). */
    typedef CH32RVCallback<bool()> ReceiveGate;
    void receiveGate(ReceiveGate gate) { _receiveGate = gate; }
    void resumeReceive();
    /* Send one Ethernet frame (at most maxFrameBytes, 1514 by default) to
     * the host, copying it: true once it is in a transmit block, not when the
     * host has it. Waits up to timeoutMs for room; false when none freed up
     * or the link is down. */
    bool sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs = 10);
    /* The host has configured the interface. */
    bool linkUp() const;
    /* Frames sent (counted as their block goes out) and received, and blocks
     * from the host that were not a valid NTB16 (EspUsbNetStats's names). */
    CH32RVUsbNetStats stats() const { return _stats; }

    /* --- EspUsbDevice's IP layer, from the Network library (#include <Network.h>) ---
     * The device's own address, gateway and mask: the static address, and the
     * DHCP server's base. Default 192.168.7.1 / 192.168.7.1 / 255.255.255.0.
     * Call before beginNetwork(): these setters refuse (false) while it is up. */
    bool ipConfig(IPAddress local, IPAddress gateway, IPAddress subnet);
    /* Hand the USB host an address (192.168.7.2 ... by default). Off by default;
     * excludes dhcpClient(). */
    bool dhcpServer(bool enable);
    /* Take an address from the host's network instead (a host that bridges or
     * shares its connection). Off by default; excludes dhcpServer(). */
    bool dhcpClient(bool enable);
    /* The DHCP server offers no router and no DNS unless told to: a host that
     * takes this link as its default route when nothing forwards behind it
     * loses its other traffic. */
    bool dhcpAdvertiseGateway(bool enable);
    bool dhcpDns(IPAddress dns);
    /* Make this link the device's own default route. Off by default, so it
     * never takes over from an Ethernet interface. */
    bool defaultRoute(bool enable);
    /* Bring the IP interface up on this function. Call after USBDevice.begin(). */
    bool beginNetwork();
    /* Take the IP interface down again; the function goes back to raw frames. */
    bool endNetwork();
    bool networkUp() const;
    IPAddress localIP() const;
    /* The Network library's interface for this link (a NetworkInterface:
     * hasIP(), printTo() ...), or nullptr before the first call above. */
    NetworkUsbNcm *networkInterface() const { return _network; }

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    bool usbSetInterface(uint8_t interfaceNumber, uint8_t alternate) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    size_t usbDeliver();
    static size_t deliverThis(CH32RVUsbDeviceFunction &f) { return static_cast<CH32RVUsbDeviceNet &>(f).usbDeliver(); }

private:
    void notify();
    size_t receiveBlock(const uint8_t *ntb, uint16_t length, const FrameCallback &to);
    void receiveNext();
    void sendNext();
    CH32RVUsbNetConfig _config;
    FrameCallback _onFrame = nullptr;
    FrameCallback _stackFrame = nullptr;
    ReceiveGate _receiveGate = nullptr;
    NetworkUsbNcm *_network = nullptr;   /* created by the Network library on first use */
    bool _networkBegun = false;
    CH32RVUsbNetStats _stats;
};
