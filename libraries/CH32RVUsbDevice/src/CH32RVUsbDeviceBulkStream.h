/* What CDC and Vendor share (EspUsbDeviceBulkStream): a bulk OUT / IN pair
 * with receive and transmit FIFOs, the per-transfer callbacks and the
 * borrowed (zero-copy) transfers. Included by CH32RVUsbDevice.h. */
#pragma once

class CH32RVUsbDeviceBulkStream : public CH32RVUsbDeviceFunction {
public:
    /* callback(bytes available) when data arrives. */
    typedef CH32RVCallback<void(size_t available)> RxCallback;
    /* callback(sentBytes) when write()'s data went out. */
    typedef CH32RVCallback<void(size_t sentBytes)> TxCompleteCallback;
    typedef CH32RVCallback<void(const uint8_t *data, size_t length)> RxDataCallback;
    typedef CH32RVCallback<void(const CH32RVUsbTransferResult &result)> DirectCompleteCallback;

    /* The host has configured the device. */
    bool mounted() const;
    /* Buffers, how write() ends on the wire and how data is received
     * (CH32RVUsbBulkStreamConfig). While the device is stopped; false
     * (nothing changed) for a setting that cannot be used. */
    bool configure(const CH32RVUsbBulkStreamConfig &config);
    const CH32RVUsbBulkStreamConfig &configuration() const { return _config; }
    /* Bytes the receive FIFO holds at most, and write() can queue. */
    size_t readCapacity() const { return (size_t)(_rxSize - 1); }
    size_t writeCapacity() const { return (size_t)(_txSize - 1); }
    /* Room in the transmit FIFO now; write() never takes more at once. 0
     * while a borrowed transfer (writeDirect()) is out. */
    size_t writeAvailable() const;

    /* The zero-copy path: one bulk IN transfer straight from `buffer`, which
     * stays the caller's - untouched and alive - until onDirectTxComplete()
     * hands it back, which happens exactly once for an accepted transfer:
     * sent (Success), a bus reset (BusReset), end() (Stopped, from inside
     * end()) or cancelDirectTx() (Cancelled, from inside it). A refusal
     * borrows nothing. Any length up to maxDirectTransfer() and any
     * alignment; a long one goes out as one transfer with no short packet
     * inside it. Not mixed with write(): refused (Busy) while the FIFO has
     * data in flight, and write() takes nothing while a direct transfer
     * is out (writeAvailable() is 0). Arming the next one from
     * onDirectTxComplete() keeps a stream gapless. The controllers move it at their own speed: the USBSS DMA
     * reads the buffer itself (keep it 4-byte aligned, or it goes a packet at
     * a time through a bounce buffer), the USBFS ones copy a packet at a
     * time. */
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
     * A run-time choice (rxMode Stream; Callback only adds that begin() needs
     * it). Delivered from deliverEvents(): the transfer is held there until
     * then (the host waits), and the next one starts once the call returns.
     * The data is valid during the call. Transfers land in the receive
     * buffer (configure()), whole packets of it: a bigger one lets
     * SuperSpeed OUT run at speed. */
    void onRxData(RxDataCallback cb);

    /* For the USB stack's callbacks. */
    void handleRx();
    void handleTxComplete(size_t sentBytes);
    void directEnded(CH32RVUsbTransferStatus status, size_t actual, bool known);
    void directRxEnded(CH32RVUsbTransferStatus status, size_t actual, bool known);
    /* The receive mode has the callback it needs (checked at begin()). */
    bool receiveReady() const
    {
        return (_config.rxMode != CH32RVUsbBulkRxMode::Callback || _onRxData) &&
               (_config.rxMode != CH32RVUsbBulkRxMode::Borrowed || _onDirectRx);
    }

    /* The data endpoint pair, set while the descriptors are laid out. */
    uint8_t _ep = 0;

protected:
    /* `terminate`: what streamTermination FunctionDefault means (CDC ends
     * write()'s data with a ZLP after whole packets, Vendor does not). */
    CH32RVUsbDeviceBulkStream(CH32RVUsbDevice &device, const CH32RVUsbFunctionType &type, const char *name,
                              bool terminate);
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    /* The packet size of the bulk pair at the speed the host picked. */
    void bulkConfigured(uint16_t packet);
    virtual size_t usbDeliver();
    static size_t deliverThis(CH32RVUsbDeviceFunction &f)
    {
        return static_cast<CH32RVUsbDeviceBulkStream &>(f).usbDeliver();
    }

    int bulkAvailable();
    int bulkPeek();
    size_t bulkRead(uint8_t *buffer, size_t size);
    /* Copy what fits into the transmit FIFO and start sending; how much. */
    size_t bulkQueue(const uint8_t *buffer, size_t size);
    /* Nothing queued and nothing on the wire. */
    bool bulkIdle() const { return !_inBusy && _txHead == _txTail; }
    void push();

private:
    /* The borrowed transfers and onRxData()'s path, reached through this
     * table: set by the calls that start them, so a sketch that never does
     * (most SerialUSB ones) links none of their code. */
    struct Extra {
        bool (*transferred)(CH32RVUsbDeviceBulkStream &s, uint8_t address, uint16_t length);
        void (*reset)(CH32RVUsbDeviceBulkStream &s, CH32RVUsbTransferStatus why);
        size_t (*deliver)(CH32RVUsbDeviceBulkStream &s);
        bool (*receive)(CH32RVUsbDeviceBulkStream &s);
    };
    static const Extra extra;
    const Extra *_extra = nullptr;
    static bool extraTransferred(CH32RVUsbDeviceBulkStream &s, uint8_t address, uint16_t length);
    static void extraReset(CH32RVUsbDeviceBulkStream &s, CH32RVUsbTransferStatus why);
    static size_t extraDeliver(CH32RVUsbDeviceBulkStream &s);
    static bool extraReceive(CH32RVUsbDeviceBulkStream &s);
    static constexpr uint16_t BUFFER = CH32RV_USB_MAX_PACKET < 128 ? 256 : 2 * CH32RV_USB_MAX_PACKET;
    static constexpr size_t DIRECT_MAX = 0x7FFFFFFFu;
    /* A borrowed transfer is armed in pieces of whole packets the controller takes at once. */
    uint16_t directPiece() const { return (uint16_t)(0xFFFFu / _packet * _packet); }
    void receive();
    uint16_t rxUsed() const;
    CH32RVUsbBulkStreamConfig _config;
    uint8_t _rxStore[BUFFER], _txStore[BUFFER];
    uint8_t *_rx = _rxStore, *_tx = _txStore;
    uint16_t _rxSize = BUFFER, _txSize = BUFFER;
    /* OUT transfers land straight in _rx, a packet's worth of contiguous
     * room at a time; _rxEnd marks where the data wraps early. */
    uint16_t _rxHead = 0, _rxTail = 0, _rxEnd = 0;
    uint16_t _txHead = 0, _txTail = 0, _txSending = 0;
    uint16_t _packet = 64;
    bool _outBusy = false, _inBusy = false, _zlp = false;
    bool _terminate;                  /* a ZLP after whole packets (EndWithShortPacket) */
    const bool _terminateByDefault;
    uint8_t *_rxAt = nullptr;         /* where the OUT transfer in flight lands */
    uint16_t _rxHeld = 0;             /* an onRxData() transfer waiting for usbDeliver() */
    bool _rxPending = false;          /* onRx() */
    size_t _txDone = 0;               /* onTxComplete(): bytes since the last delivery */
    RxCallback _onRx = nullptr;
    TxCompleteCallback _onTxComplete = nullptr;
    RxDataCallback _onRxData = nullptr;
    DirectCompleteCallback _onDirectTx = nullptr;
    CH32RVUsbTransferResult _directOut;   /* the loan in flight, then its result */
    bool _direct = false, _directDone = false;
    bool _directZlp = false;             /* EndWithShortPacket: a ZLP follows */
    uint16_t _directArmed = 0;           /* the piece in flight */
    uint32_t _directId = 0;
    DirectCompleteCallback _onDirectRx = nullptr;
    CH32RVUsbTransferResult _directIn;   /* readDirect()'s loan, then its result */
    bool _directRx = false, _directInDone = false;
    uint16_t _directInArmed = 0;
    uint32_t _directInId = 0;
};
