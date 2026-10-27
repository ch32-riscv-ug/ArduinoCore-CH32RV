/* Vendor-specific interface with a bulk OUT/IN endpoint pair
 * (EspUsbDeviceVendor): a raw byte pipe that libusb, WebUSB (Chrome) or
 * WinUSB reach with no driver install - Windows gets its WinUSB binding from
 * the Microsoft OS 2.0 descriptors, which come with this class. Included by
 * CH32RVUsbDevice.h. */
#pragma once

/* CH32RVUsbDeviceVendorControlRequest::stage: setup, data, status. */
static constexpr uint8_t CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP = 1;
static constexpr uint8_t CH32RV_USB_DEVICE_CONTROL_STAGE_DATA = 2;
static constexpr uint8_t CH32RV_USB_DEVICE_CONTROL_STAGE_ACK = 3;

struct CH32RVUsbDeviceVendorControlRequest {
    uint8_t rhport = 0;
    uint8_t stage = 0;
    uint8_t bmRequestType = 0;
    uint8_t bRequest = 0;
    uint16_t wValue = 0;
    uint16_t wIndex = 0;
    uint16_t wLength = 0;
    const void *rawRequest = nullptr;
};

/* EspUsbDeviceVendorDirectError: why writeDirect() refused. */
enum class CH32RVUsbDeviceVendorDirectError : uint8_t {
    None = 0,
    NotSupported,
    NotMounted,
    BadArgument,
    NotAligned,
    NotDmaCapable,
    Busy,
    TransferFailed,
};

class CH32RVUsbDeviceVendor : public CH32RVUsbDeviceFunction, public Print {
public:
    /* callback(bytes available) when data arrives. */
    typedef CH32RVCallback<void(size_t available)> RxCallback;
    /* callback(sentBytes) when a transmit on the bulk IN endpoint completed. */
    typedef CH32RVCallback<void(size_t sentBytes)> TxCompleteCallback;
    typedef CH32RVCallback<void(const uint8_t *data, size_t length)> RxDataCallback;
    /* A vendor request the library does not answer itself. Return true when
     * handled: answer it at stage SETUP with sendControlResponse(), and
     * return true at the later stages. */
    typedef CH32RVCallback<bool(const CH32RVUsbDeviceVendorControlRequest &request)> ControlRequestCallback;

    /* Full speed: bulk endpoints are 8, 16, 32 or 64 bytes. */
    explicit CH32RVUsbDeviceVendor(CH32RVUsbDevice &device = USBDevice, uint16_t endpointSize = 64);

    bool mounted() const;
    int available();
    int read();
    size_t read(uint8_t *buffer, size_t size);
    /* The next byte read() would return, left in place; -1 when none. Not in
     * EspUsbDevice: for code that parses the stream a byte ahead. */
    int peek();
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
    /* printf() as on HardwareSerial (and ESP32's Print). */
    CH32RV_PRINTF_MEMBER
    /* Room in the transmit FIFO now; write() never takes more. */
    size_t writeAvailable() const;
    /* The transmit buffer's size (twice the largest packet, at least 256). */
    static size_t writeCapacity();
    /* Wait until the FIFO has room for `bytes` (clamped to writeCapacity()),
     * running the stack meanwhile. Flushes first. */
    bool waitWritable(size_t bytes, uint32_t timeoutMs = 100);
    /* Send what is in the FIFO now, whole packet or not: needed after a write
     * shorter than the endpoint size. */
    void flush() override;

    /* EspUsbDevice's zero-copy path: one bulk IN transfer straight from
     * `buffer`, which stays the caller's - untouched and alive - until
     * onTxComplete() reports it. Any length up to 65535 and any alignment;
     * a length that is a multiple of the packet size gets no zero-length
     * packet after it (send a short one, or let the host read whole
     * transfers). Not mixed with write(): refused (Busy) while the FIFO has
     * data in flight, and write() waits while a direct transfer is out.
     * Arming the next one from onTxComplete() keeps a stream gapless. The
     * controllers move it at their own speed: the USBSS DMA reads the buffer
     * itself (keep it 4-byte aligned, or it goes a packet at a time through a
     * bounce buffer), the USBFS ones copy a packet at a time. */
    static bool directWriteSupported() { return true; }
    bool writeDirect(const void *buffer, size_t length);
    CH32RVUsbDeviceVendorDirectError lastDirectError() const { return _directError; }
    const char *lastDirectErrorName() const;

    /* From deliverEvents(). onRx(): data waits in the FIFO (not at all when
     * it was read before). onTxComplete(): the bytes sent since the last
     * delivery, so a stream armed from it pauses until the sketch delivers. */
    void onRx(RxCallback cb) { _onRx = cb; deliverWith(deliverThis); }
    void onTxComplete(TxCompleteCallback cb) { _onTxComplete = cb; deliverWith(deliverThis); }
    /* Received data, straight from the endpoint buffer, one transfer at a
     * time; while set, nothing goes to the receive FIFO (available() stays 0).
     * EspUsbDevice has this on its direct builds only; here it is a run-time
     * choice. Delivered from deliverEvents(): the transfer is held there until
     * then (the host waits), and the next one starts once the call returns.
     * The data is valid during the call. */
    void onRxData(RxDataCallback cb) { _onRxData = cb; deliverWith(deliverThis); }
    /* Where onRxData() transfers land instead of the class's own buffer (2 x
     * the largest packet): a bigger one lets SuperSpeed OUT run at speed. It
     * stays the class's until end(); at most 65535 bytes are used, in whole
     * packets. nullptr goes back to the class's buffer. */
    void setRxDataBuffer(void *buffer, size_t size)
    {
        _rxData = static_cast<uint8_t *>(buffer);
        _rxDataSize = size > 65535u ? 65535u : (uint16_t)size;
    }
    /* A provider: called inside the USB service while the host waits. */
    void onControlRequest(ControlRequestCallback cb) { _onControl = cb; }
    /* Answer a request at stage SETUP: data, or a status stage when none. */
    bool sendControlResponse(const CH32RVUsbDeviceVendorControlRequest &request, const void *data = nullptr,
                             size_t length = 0);
    uint16_t endpointSize() const { return _endpointSize; }
    /* bInterfaceSubClass / bInterfaceProtocol (class stays 0xFF), for a
     * device a host finds by them - an OEP probe is 0xFF / 0x4F / 0x45.
     * Before begin(). */
    void setInterfaceProtocol(uint8_t subclass, uint8_t protocol)
    {
        _subclass = subclass;
        _protocol = protocol;
    }
    uint8_t interfaceSubclass() const { return _subclass; }
    uint8_t interfaceProtocol() const { return _protocol; }

    /* For the USB stack's callbacks. */
    void handleRx();
    void handleTxComplete(size_t sentBytes);
    bool handleControlRequest(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup);

    /* The endpoint pair, set while the descriptors are laid out. */
    uint8_t _ep = 0;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    size_t usbDeliver();
    static size_t deliverThis(CH32RVUsbDeviceFunction &f) { return static_cast<CH32RVUsbDeviceVendor &>(f).usbDeliver(); }

private:
    static constexpr uint16_t BUFFER = CH32RV_USB_MAX_PACKET < 128 ? 256 : 2 * CH32RV_USB_MAX_PACKET;
    void receive();
    void push();
    uint16_t rxUsed() const;
    uint8_t _rx[BUFFER], _tx[BUFFER];
    /* As CH32RVUsbDeviceCdcSerial: OUT straight into _rx, wrapping early at _rxEnd. */
    uint16_t _rxHead = 0, _rxTail = 0, _rxEnd = 0;
    uint16_t _txHead = 0, _txTail = 0, _txSending = 0;
    uint16_t _packet = 64;
    bool _outBusy = false, _inBusy = false, _zlp = false, _direct = false;
    uint8_t _subclass = 0, _protocol = 0;
    uint8_t *_rxData = nullptr;
    uint8_t *_rxAt = nullptr;         /* where the OUT transfer in flight lands */
    uint16_t _rxDataSize = 0;
    uint16_t _rxHeld = 0;             /* an onRxData() transfer waiting for usbDeliver() */
    bool _rxPending = false;          /* onRx() */
    size_t _txDone = 0;               /* onTxComplete(): bytes since the last delivery */
    uint16_t _endpointSize;
    RxCallback _onRx = nullptr;
    TxCompleteCallback _onTxComplete = nullptr;
    RxDataCallback _onRxData = nullptr;
    ControlRequestCallback _onControl = nullptr;
    CH32RVUsbDeviceVendorDirectError _directError = CH32RVUsbDeviceVendorDirectError::None;
};
