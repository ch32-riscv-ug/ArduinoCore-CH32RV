/* USB Video Class camera (EspUsbDeviceVideo): one control interface and one
 * streaming interface, advertising one format at one frame size and rate.
 * Included by CH32RVUsbDevice.h.
 *
 * The device is the source; there is no host-to-device direction. Give it a
 * frame with sendFrame() and the class cuts it into payloads (a 2-byte UVC
 * header each, one packet per payload) and sends them, calling the
 * completion callback when the last one has gone. One frame is in flight at a
 * time.
 *
 * Bandwidth is the thing to decide first. On CH32X035/X033 (USBFS, full
 * speed) every endpoint is 64 bytes, isochronous ones included, so:
 *   - bulk streaming (the default) gets whatever the bus has left - several
 *     hundred KB/s on an idle bus, and the host retries a lost packet, so no
 *     frame is torn;
 *   - isochronous (setTransferType(Isochronous)) is one 64-byte packet per
 *     1 ms frame: 62 bytes of picture, about 62 KB/s - a 64x48 YUY2 frame
 *     (6 KB) at 10 fps.
 * RAM bounds the frame before the bus does: a 20 KB part holds a 96x64 YUY2
 * frame (12 KB) and not much more. sendGeneratedFrame() (an extension) needs
 * no frame buffer at all - the bytes are asked for payload by payload - so a
 * generated or line-by-line source can send 160x120 or larger. A high-speed
 * controller (USBHS on V305/V307/V317) has 512-byte bulk and 1024-byte
 * isochronous packets every 125 us.
 *
 * Payloads move in ch32rv_usb_task() (after loop(), inside delay()).
 *
 * Differences from EspUsbDeviceVideo: callbacks are plain function pointers;
 * the setters work until the device begins (the descriptor is laid out at
 * CH32RVUsbDevice::begin(), not at this object's begin()); there is no atomic
 * in-flight claim, because everything runs from the one USB task on one core;
 * onFillPayload()/sendGeneratedFrame() are additions. */
#pragma once


/* Pixel format the camera advertises. Yuy2 needs no encoder and every host
 * displays it - the honest choice for a test pattern and tiny frames. Mjpeg is
 * what makes larger frames practical, given an encoder (or stored JPEGs). */
enum class CH32RVUsbDeviceVideoFormat : uint8_t {
    Mjpeg = 0,
    Yuy2,
};

/* How the stream travels: bulk (what the bus has spare, retried) or
 * isochronous (a fixed share, reserved when the host starts streaming). */
enum class CH32RVUsbDeviceVideoTransfer : uint8_t {
    Bulk = 0,
    Isochronous,
};

/* What the host committed to after probe/commit negotiation. It can differ
 * from what was advertised (a host may ask for a slower frame interval), so a
 * sketch that cares reads it here. */
struct CH32RVUsbDeviceVideoCommit {
    uint8_t formatIndex = 0;
    uint8_t frameIndex = 0;
    /* 100 ns units, as UVC counts: 333333 is 30 fps. */
    uint32_t frameInterval = 0;
    uint32_t maxVideoFrameSize = 0;
    /* Most the host accepts in one payload, header included: one packet. */
    uint32_t maxPayloadTransferSize = 0;
};

class CH32RVUsbDeviceVideo : public CH32RVUsbDeviceFunction {
public:
    typedef CH32RVCallback<void()> FrameCompleteCallback;
    typedef CH32RVCallback<void(const CH32RVUsbDeviceVideoCommit &commit)> CommitCallback;
    /* Fill dst with `length` bytes of the frame starting at byte `offset`
     * (sendGeneratedFrame()). Runs on the USB task once per payload. */
    typedef CH32RVCallback<void(uint8_t *dst, size_t length, uint32_t offset)> FillCallback;

    /* name, when given, is the function name the host shows (iFunction on the
     * IAD and iInterface on both interfaces); CH32RV_USB_VIDEO_NAME otherwise. */
    explicit CH32RVUsbDeviceVideo(CH32RVUsbDevice &device = USBDevice, const char *name = nullptr);

    /* All five take effect when the device begins and are refused after,
     * because the host has read the descriptor by then. */
    bool setFormat(CH32RVUsbDeviceVideoFormat format);
    bool setTransferType(CH32RVUsbDeviceVideoTransfer type);
    /* Even, 2..4096 each way (YUY2 packs two pixels into four bytes). */
    bool setFrameSize(uint16_t width, uint16_t height);
    /* 1..120. */
    bool setFrameRate(uint8_t framesPerSecond);
    /* Upper bound on one encoded frame (MJPEG); hosts size their buffers by
     * it. 0 restores the default: width * height * 2 for Yuy2, an eighth of
     * that for Mjpeg. */
    bool setMaxFrameSize(uint32_t bytes);

    /* Nothing to start: the function joined the device when it was declared
     * (EspUsbDevice calls begin() from the device). True when attached. */
    bool begin() { return attached(); }
    /* Forget a frame in flight and the commit. */
    void end();

    CH32RVUsbDeviceVideoFormat format() const { return _format; }
    uint16_t width() const { return _width; }
    uint16_t height() const { return _height; }
    uint8_t frameRate() const { return _frameRate; }
    uint32_t maxFrameSize() const;
    CH32RVUsbDeviceVideoTransfer transferType() const { return _transfer; }
    bool bulkStreaming() const { return _transfer == CH32RVUsbDeviceVideoTransfer::Bulk; }
    /* Bytes the descriptor takes at the given speed. */
    uint16_t descriptorLength(bool highSpeed = false) const;
    /* wMaxPacketSize of the isochronous endpoint at that speed: the
     * controller's largest packet, capped at 1023 (FS) / 1024 (HS). On USBFS
     * (X035) 64. */
    uint16_t isochronousPacketSize(bool highSpeed) const;
    /* One payload, header included: a packet at the speed the host runs the
     * bus (full speed before it has). */
    uint16_t payloadBufferSize() const;

    /* The host has committed and (isochronous) selected the streaming
     * alternate setting. False before, and after unplug. */
    bool streaming() const;
    /* Hand one whole frame to the class driver. The buffer is read payload by
     * payload, not copied: leave it alone until the completion callback (or
     * frameInFlight() turns false). Fails while a frame is in flight, before
     * the host streams, or on a null, empty or oversized frame. */
    bool sendFrame(const void *frame, size_t length);
    /* Send a `length`-byte frame with no buffer: the fill callback supplies
     * each payload's bytes as it goes out. An extension; the way to send
     * frames bigger than RAM. */
    bool sendGeneratedFrame(size_t length);
    bool frameInFlight() const { return _inFlight; }

    /* From deliverEvents(), once the last payload of a frame has gone (and
     * frameInFlight() is false): arming the next frame there keeps the stream
     * going at the rate the sketch delivers. */
    void onFrameComplete(FrameCompleteCallback callback) { _onFrameComplete = callback; deliverWith(deliverThis); }
    /* From deliverEvents(), when the host has committed its streaming
     * parameters: the latest commit. */
    void onCommit(CommitCallback callback) { _onCommit = callback; deliverWith(deliverThis); }
    /* The bytes of each payload of sendGeneratedFrame(), as it goes out: a
     * provider, called inside the USB service while the host waits. */
    void onFillPayload(FillCallback callback) { _onFill = callback; }
    /* What the host committed to, or zeros before it has. */
    CH32RVUsbDeviceVideoCommit commit() const { return _commit; }

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    bool usbSetInterface(uint8_t interfaceNumber, uint8_t alternate) override;
    void usbTransferred(uint8_t address, uint16_t length) override;
    void usbHaltCleared(uint8_t address) override;
    size_t usbDeliver();
    static size_t deliverThis(CH32RVUsbDeviceFunction &f) { return static_cast<CH32RVUsbDeviceVideo &>(f).usbDeliver(); }

private:
    friend bool video_descriptor(const CH32RVUsbDeviceFunction &, CH32RVUsbDescriptorWriter &, uint8_t, uint8_t *,
                                 uint8_t, CH32RVUsbSpeed);
    bool configurable() const;
    bool arm(const uint8_t *frame, size_t length);
    void defaultProbe(uint8_t *probe) const;
    void sendPayload();
    void stop();
    void handleFrameComplete();
    void handleCommit(const CH32RVUsbDeviceVideoCommit &commit);
    void handleFill(uint8_t *dst, size_t length, uint32_t offset);

    CH32RVUsbDeviceVideoFormat _format = CH32RVUsbDeviceVideoFormat::Mjpeg;
    uint16_t _width = 320;
    uint16_t _height = 240;
    uint8_t _frameRate = 15;
    uint32_t _maxFrameSize = 0;
    volatile bool _inFlight = false;
    bool _frameDone = false, _committed = false;   /* for usbDeliver() */
    FrameCompleteCallback _onFrameComplete = nullptr;
    CommitCallback _onCommit = nullptr;
    FillCallback _onFill = nullptr;
    CH32RVUsbDeviceVideoCommit _commit;
    CH32RVUsbDeviceVideoTransfer _transfer = CH32RVUsbDeviceVideoTransfer::Bulk;

    /* Set when the descriptor is laid out. */
    uint8_t _ep = 0;
    uint8_t _vcInterface = 0;
    /* The stream: committed (bulk) or alternate 1 (isochronous). */
    bool _streaming = false;
    bool _highSpeed = false;
    uint8_t _fid = 0;                   /* the frame ID bit, toggled each frame */
    const uint8_t *_frame = nullptr;    /* null: sendGeneratedFrame() */
    uint32_t _length = 0, _offset = 0;
    uint16_t _sending = 0;              /* frame bytes in the payload in flight */
    uint8_t _probe[48] __attribute__((aligned(4)));   /* UVC 1.5 probe / commit */
    uint8_t _payload[CH32RV_USB_MAX_PACKET] __attribute__((aligned(4)));
};
