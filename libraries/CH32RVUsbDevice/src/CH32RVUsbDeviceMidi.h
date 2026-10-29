/* USB MIDI 1.0, in EspUsbDevice's shape: 4-byte event packets, channel
 * messages with a 0-based channel (0..15) first, and up to 16 cables per
 * direction multiplexed on one pair of bulk endpoints. Included by
 * CH32RVUsbDevice.h. */
#pragma once

class CH32RVUsbDeviceMidi : public CH32RVUsbDeviceFunction {
public:
    static constexpr uint8_t MAX_CABLES = 16;

    /* Each cable is a MIDI port on the host. A count outside 1..MAX_CABLES
     * fails the device's begin() with InvalidArgument. */
    explicit CH32RVUsbDeviceMidi(CH32RVUsbDevice &device = USBDevice, uint8_t cableCount = 1);
    /* IN is device to host (what this device sends), OUT host to device. */
    CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount);

    bool begin() { return true; }
    uint8_t inCableCount() const { return _inCables; }
    uint8_t outCableCount() const { return _outCables; }
    /* Bytes the configuration descriptor takes (a 16-cable one is 572). */
    uint16_t descriptorLength() const;

    bool readPacket(CH32RVUsbDeviceMidiPacket &packet);
    /* False for a cable the host was not told about (>= inCableCount()) or
     * a reserved code index (0, 1), as well as when the queue is full. */
    bool writePacket(const CH32RVUsbDeviceMidiPacket &packet);
    /* cable is 0-based (0..inCableCount()-1). Sending to a cable the host was
     * not told about fails. */
    bool noteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable = 0);
    bool noteOff(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t cable = 0);
    bool controlChange(uint8_t channel, uint8_t control, uint8_t value, uint8_t cable = 0);
    bool programChange(uint8_t channel, uint8_t program, uint8_t cable = 0);
    bool polyPressure(uint8_t channel, uint8_t note, uint8_t pressure, uint8_t cable = 0);
    bool channelPressure(uint8_t channel, uint8_t pressure, uint8_t cable = 0);
    /* 0..16383, 8192 = centre. */
    bool pitchBend(uint8_t channel, uint16_t value, uint8_t cable = 0);
    /* Packets waiting from the host. */
    size_t availablePackets();
    /* Packets writePacket() takes now. */
    size_t availableForWritePackets() const;
    /* Packets from the host dropped: a reserved code index (0, 1), a cable
     * beyond outCableCount(), or a transfer that is not whole packets. */
    uint32_t receiveErrors() const { return _receiveErrors; }

    /* The endpoint pair, set while the descriptors are laid out. */
    uint8_t _ep = 0;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    void usbTransferred(uint8_t address, uint16_t length) override;

private:
    /* 4-byte event packets kept from the host: two full OUT packets' worth. */
    static constexpr uint16_t RX_PACKETS = CH32RV_USB_MAX_PACKET / 2;
    static constexpr uint16_t TX_PACKETS = 32;
    bool send(uint8_t cin, uint8_t channel, uint8_t a, uint8_t b, uint8_t cable);
    void receive();
    void push();
    uint8_t _inCables;
    uint8_t _outCables;
    uint8_t _out[CH32RV_USB_MAX_PACKET];         /* one OUT packet */
    uint8_t _in[64];
    uint8_t _rx[RX_PACKETS][4], _tx[TX_PACKETS][4];
    uint16_t _rxHead = 0, _rxTail = 0, _txHead = 0, _txTail = 0;
    uint16_t _packet = 64;
    uint32_t _receiveErrors = 0;
    bool _outBusy = false, _inBusy = false;
};
