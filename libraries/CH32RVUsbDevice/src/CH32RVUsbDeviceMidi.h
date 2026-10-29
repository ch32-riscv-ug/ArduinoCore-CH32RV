/* USB MIDI 1.0, in EspUsbDevice's shape: 4-byte event packets, channel
 * messages with a 0-based channel (0..15) first, and up to 16 cables per
 * direction multiplexed on one pair of bulk endpoints. Included by
 * CH32RVUsbDevice.h. */
#pragma once

/* How MIDI from the host is taken: Queue (readPacket()), Callback (each
 * event to onPacket() from deliverEvents(); begin() fails without one) or
 * Disabled (the OUT endpoint is never armed). */
enum class CH32RVUsbMidiRxMode : uint8_t { Queue, Callback, Disabled };

/* EspUsbMidiConfig's names. Packets are 4-byte events. rxPackets /
 * txPackets 0 keep the class's own queue (RX_PACKETS / TX_PACKETS events);
 * fewer use part of it, more need the sketch's array (rxBuffer / txBuffer,
 * kept from begin() to end()) - nothing is allocated. A queue smaller than
 * one USB packet still takes whole packets: the rest waits in the
 * endpoint's buffer, and the next packet is not armed until it is read.
 * transferPackets 0 sends up to one bus packet per transfer; another value
 * caps the events per transfer. With autoFlush false, writePacket() only
 * queues and flush() starts sending. streamTermination FunctionDefault is
 * EndWithShortPacket (a ZLP after a transfer of whole packets that empties
 * the queue). */
struct CH32RVUsbMidiConfig {
    uint8_t inCables = 1, outCables = 1;
    const char *name = nullptr;     /* nullptr keeps the function's name */
    size_t rxPackets = 0;
    size_t txPackets = 0;
    size_t transferPackets = 0;
    CH32RVUsbMidiRxMode rxMode = CH32RVUsbMidiRxMode::Queue;
    bool autoFlush = true;
    CH32RVUsbStreamTermination streamTermination = CH32RVUsbStreamTermination::FunctionDefault;
    CH32RVUsbDeviceMidiPacket *rxBuffer = nullptr;
    CH32RVUsbDeviceMidiPacket *txBuffer = nullptr;
};

class CH32RVUsbDeviceMidi : public CH32RVUsbDeviceFunction {
public:
    static constexpr uint8_t MAX_CABLES = 16;
    typedef CH32RVCallback<void(const CH32RVUsbDeviceMidiPacket &packet)> PacketCallback;

    /* Each cable is a MIDI port on the host. A count outside 1..MAX_CABLES
     * fails the device's begin() with InvalidArgument. */
    explicit CH32RVUsbDeviceMidi(CH32RVUsbDevice &device = USBDevice, uint8_t cableCount = 1);
    /* IN is device to host (what this device sends), OUT host to device. */
    CH32RVUsbDeviceMidi(CH32RVUsbDevice &device, uint8_t inCableCount, uint8_t outCableCount);

    bool begin() { return true; }
    /* While the device is stopped; false (nothing changed) for a setting
     * that cannot be used. It replaces every setting: start from
     * configuration() to keep the others. */
    bool configure(const CH32RVUsbMidiConfig &config);
    const CH32RVUsbMidiConfig &configuration() const { return _config; }
    /* rxMode Callback: each event, from deliverEvents(); the packet is valid
     * during the call. */
    void onPacket(PacketCallback cb) { _onPacket = cb; deliverWith(deliverThis); }
    uint8_t inCableCount() const { return _config.inCables; }
    uint8_t outCableCount() const { return _config.outCables; }
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
    /* Packets waiting from the host (the queue and what waits in the
     * endpoint's buffer, so it can be more than receiveCapacity()). */
    size_t availablePackets();
    /* Packets writePacket() takes now. */
    size_t availableForWritePackets() const;
    /* The queues in events, and the most events one transfer sends; 0 while
     * the device is stopped. */
    size_t receiveCapacity() const;
    size_t sendCapacity() const;
    size_t transferCapacity() const;
    /* Start sending what writePacket() queued (needed with autoFlush false). */
    void flush();
    /* Packets from the host dropped: a reserved code index (0, 1), a cable
     * beyond outCableCount(), or a transfer that is not whole packets. */
    uint32_t receiveErrors() const { return _receiveErrors; }

    /* The endpoint pair, set while the descriptors are laid out. */
    uint8_t _ep = 0;
    /* rxMode Callback has onPacket() (checked at begin()). */
    bool receiveReady() const;

    /* 4-byte event packets kept from the host by default: two full OUT
     * packets' worth; and to send. */
    static constexpr uint16_t RX_PACKETS = CH32RV_USB_MAX_PACKET / 2;
    static constexpr uint16_t TX_PACKETS = 32;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    size_t usbDeliver();
    static size_t deliverThis(CH32RVUsbDeviceFunction &f) { return static_cast<CH32RVUsbDeviceMidi &>(f).usbDeliver(); }

private:
    bool send(uint8_t cin, uint8_t channel, uint8_t a, uint8_t b, uint8_t cable);
    void receive();
    void drain();
    void push();
    size_t waitingInPacket() const;
    CH32RVUsbMidiConfig _config;
    PacketCallback _onPacket = nullptr;
    uint8_t _out[CH32RV_USB_MAX_PACKET];         /* one OUT packet */
    uint8_t _in[64];
    CH32RVUsbDeviceMidiPacket _rxStore[RX_PACKETS], _txStore[TX_PACKETS];
    CH32RVUsbDeviceMidiPacket *_rx = _rxStore, *_tx = _txStore;
    uint16_t _rxSize = RX_PACKETS, _txSize = TX_PACKETS;
    /* Queues as counts from a tail: no free-running index, any size. */
    uint16_t _rxTail = 0, _rxUsed = 0, _txTail = 0, _txUsed = 0;
    uint16_t _outLength = 0, _outAt = 0;         /* the OUT packet not yet moved to the queue */
    uint16_t _inLength = 0;                      /* the IN transfer in flight */
    uint16_t _packet = 64;
    uint32_t _receiveErrors = 0;
    bool _outBusy = false, _inBusy = false, _zlp = false;
};
