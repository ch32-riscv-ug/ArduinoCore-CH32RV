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
    /* Data-stage bytes so far: 0 at SETUP, the actual total at DATA and ACK
     * (either direction; no ZLP or status packet). */
    uint16_t actualBytes = 0;
    const void *rawRequest = nullptr;
};

class CH32RVUsbDeviceVendor : public CH32RVUsbDeviceFunction, public Print {
public:
    /* callback(bytes available) when data arrives. */
    typedef CH32RVCallback<void(size_t available)> RxCallback;
    /* callback(sentBytes) when a transmit on the bulk IN endpoint completed. */
    typedef CH32RVCallback<void(size_t sentBytes)> TxCompleteCallback;
    typedef CH32RVCallback<void(const uint8_t *data, size_t length)> RxDataCallback;
    typedef CH32RVCallback<void(const CH32RVUsbTransferResult &result)> DirectCompleteCallback;
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
    /* The next byte read() would return, left in place; -1 when none. */
    int peek();
    size_t write(uint8_t data) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;
    /* printf() as on HardwareSerial (and ESP32's Print). */
    CH32RV_PRINTF_MEMBER
    /* Buffers, how write() ends on the wire (Vendor's default sends no
     * zero-length packet) and how data is received (rxMode). While the
     * device is stopped; false (nothing changed) for a setting that cannot be
     * used. */
    bool configure(const CH32RVUsbBulkStreamConfig &config);
    /* Bytes the receive FIFO holds at most, and write() can queue. */
    size_t readCapacity() const { return (size_t)(_rxSize - 1); }
    size_t writeCapacity() const { return (size_t)(_txSize - 1); }
    /* Room in the transmit FIFO now; write() never takes more. */
    size_t writeAvailable() const;
    /* The same for Print (whose own answer is 0, which stalls code that waits for room). */
    int availableForWrite() override { return (int)writeAvailable(); }
    /* Wait until the FIFO has room for `bytes`, running the stack meanwhile.
     * Flushes first. False on the timeout and for more than writeCapacity(). */
    bool waitWritable(size_t bytes, uint32_t timeoutMs = 100);
    /* Send what is in the FIFO now. write() already starts sending; this runs
     * the stack once more. */
    void flush() override;

    /* The zero-copy path (EspUsbDevice's): one bulk IN transfer straight from
     * `buffer`, which stays the caller's - untouched and alive - until
     * onDirectTxComplete() hands it back, which happens exactly once for an
     * accepted transfer: sent (Success), a bus reset (BusReset), end()
     * (Stopped, from inside end()) or cancelDirectTx() (Cancelled, from
     * inside it). A refusal borrows nothing. Any length up to
     * maxDirectTransfer() and any alignment; a long one goes out as one
     * transfer with no short packet inside it. Not mixed with write():
     * refused (Busy) while the FIFO has data in flight, and write() waits
     * while a direct transfer is out. Arming the next one from
     * onDirectTxComplete() keeps a stream gapless. The controllers move it
     * at their own speed: the USBSS DMA reads the buffer itself (keep it
     * 4-byte aligned, or it goes a packet at a time through a bounce buffer),
     * the USBFS ones copy a packet at a time. */
    static bool directWriteSupported() { return true; }
    size_t maxDirectTransfer() const { return DIRECT_MAX; }
    CH32RVUsbTransferSubmission writeDirect(const void *buffer, size_t length,
                                            CH32RVUsbTransferTermination termination =
                                                CH32RVUsbTransferTermination::Continue);
    /* From deliverEvents() (from end() for Stopped, from the cancel for Cancelled). */
    void onDirectTxComplete(DirectCompleteCallback cb) { _onDirectTx = cb; deliverWith(deliverThis); }
    /* Receive straight into `buffer` (rxMode Borrowed): one OUT transfer of
     * up to `capacity` bytes, whole packets of the speed the host picked,
     * that ends full or on a short packet. The buffer is handed back to
     * onDirectRxComplete() exactly once, as for writeDirect(); actualBytes
     * is the data. Until the next readDirect() the endpoint is not armed and
     * the host's writes wait. Any alignment. */
    CH32RVUsbTransferSubmission readDirect(void *buffer, size_t capacity);
    /* The largest readDirect() capacity; 0 unless rxMode is Borrowed. */
    size_t maxDirectReceive() const;
    void onDirectRxComplete(DirectCompleteCallback cb) { _onDirectRx = cb; deliverWith(deliverThis); }
    /* Stop the loan out (id 0: whichever it is; another id: only that one).
     * The controller lets go of the buffer at once and the completion
     * callback gets it with Cancelled before this returns; part of it may
     * have moved (actualBytesKnown false). False when there is none to
     * cancel (a second cancel of one is false: it has been handed back).
     * The data toggle is left as it is. */
    bool cancelDirectTx(uint32_t id = 0);
    bool cancelDirectRx(uint32_t id = 0);

    /* From deliverEvents(). onRx(): data waits in the FIFO (not at all when
     * it was read before). onTxComplete(): the bytes of write() sent since
     * the last delivery (writeDirect() reports to onDirectTxComplete()). */
    void onRx(RxCallback cb) { _onRx = cb; deliverWith(deliverThis); }
    void onTxComplete(TxCompleteCallback cb) { _onTxComplete = cb; deliverWith(deliverThis); }
    /* Received data, straight from the endpoint buffer, one transfer at a
     * time; while set, nothing goes to the receive FIFO (available() stays 0).
     * EspUsbDevice has this on its direct builds only; here it is a run-time
     * choice. Delivered from deliverEvents(): the transfer is held there until
     * then (the host waits), and the next one starts once the call returns.
     * The data is valid during the call. Transfers land in the receive buffer
     * (configure()), whole packets of it: a bigger one lets SuperSpeed OUT run
     * at speed. */
    void onRxData(RxDataCallback cb) { _onRxData = cb; deliverWith(deliverThis); }
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
    void directEnded(CH32RVUsbTransferStatus status, size_t actual, bool known);
    void directRxEnded(CH32RVUsbTransferStatus status, size_t actual, bool known);
    bool handleControlRequest(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup);
    /* The receive mode has the callback it needs (checked at begin()). */
    bool receiveReady() const
    {
        return (_rxMode != CH32RVUsbBulkRxMode::Callback || _onRxData) &&
               (_rxMode != CH32RVUsbBulkRxMode::Borrowed || _onDirectRx);
    }

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
    static constexpr size_t DIRECT_MAX = 0x7FFFFFFFu;
    /* A borrowed transfer is armed in pieces of whole packets the controller takes at once. */
    uint16_t directPiece() const { return (uint16_t)(0xFFFFu / _packet * _packet); }
    void receive();
    void push();
    uint16_t rxUsed() const;
    uint8_t _rxStore[BUFFER], _txStore[BUFFER];
    uint8_t *_rx = _rxStore, *_tx = _txStore;
    uint16_t _rxSize = BUFFER, _txSize = BUFFER;
    /* As CH32RVUsbDeviceCdcSerial: OUT straight into _rx, wrapping early at _rxEnd. */
    uint16_t _rxHead = 0, _rxTail = 0, _rxEnd = 0;
    uint16_t _txHead = 0, _txTail = 0, _txSending = 0;
    uint16_t _packet = 64;
    bool _outBusy = false, _inBusy = false, _zlp = false, _direct = false;
    bool _terminate = false;          /* a ZLP after whole packets (EndWithShortPacket) */
    uint8_t _subclass = 0, _protocol = 0;
    uint8_t *_rxAt = nullptr;         /* where the OUT transfer in flight lands */
    uint16_t _rxHeld = 0;             /* an onRxData() transfer waiting for usbDeliver() */
    bool _rxPending = false;          /* onRx() */
    size_t _txDone = 0;               /* onTxComplete(): bytes since the last delivery */
    uint16_t _endpointSize;
    RxCallback _onRx = nullptr;
    TxCompleteCallback _onTxComplete = nullptr;
    DirectCompleteCallback _onDirectTx = nullptr;
    CH32RVUsbTransferResult _directOut;   /* the loan in flight, then its result */
    bool _directDone = false;            /* _directOut waits for delivery */
    bool _directZlp = false;             /* EndWithShortPacket: a ZLP follows */
    uint16_t _directArmed = 0;           /* the piece in flight */
    uint32_t _directId = 0;
    DirectCompleteCallback _onDirectRx = nullptr;
    CH32RVUsbTransferResult _directIn;   /* readDirect()'s loan, then its result */
    bool _directRx = false, _directInDone = false;
    uint16_t _directInArmed = 0;
    uint32_t _directInId = 0;
    CH32RVUsbBulkRxMode _rxMode = CH32RVUsbBulkRxMode::Stream;
    RxDataCallback _onRxData = nullptr;
    ControlRequestCallback _onControl = nullptr;
};
