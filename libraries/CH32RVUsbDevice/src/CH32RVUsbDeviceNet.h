/* USB network adapter (CDC-NCM, EspUsbDeviceNet): the host sees an Ethernet
 * interface - Windows 10+, macOS and Linux bind it with no driver install -
 * and the sketch exchanges raw Ethernet frames with it. Included by
 * CH32RVUsbDevice.h.
 *
 * EspUsbDevice's IP layer (ipConfig(), dhcpServer(), beginNetwork() ... on
 * lwIP / esp_netif) is not here: this core has no IP stack. */
#pragma once

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
    /* Send one Ethernet frame (at most 1514 bytes) to the host, copying it.
     * Waits up to timeoutMs for a transmit buffer; false when none freed up
     * or the link is down. */
    bool sendFrame(const uint8_t *data, size_t length, uint32_t timeoutMs = 10);
    /* The host has configured the interface. */
    bool linkUp() const;

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
};
