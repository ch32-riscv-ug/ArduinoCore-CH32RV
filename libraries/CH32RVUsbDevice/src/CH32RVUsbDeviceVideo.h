/* USB Video Class camera (EspUsbDeviceVideo): one control interface and one
 * streaming interface, advertising one format at one frame size and rate.
 * Included by CH32RVUsbDevice.h.
 *
 * The device is the source; there is no host-to-device direction. Give it a
 * frame with sendFrame() and TinyUSB's video driver cuts it into payloads
 * (a 2-byte UVC header each) and sends them, calling the completion callback
 * when the last one has gone. One frame is in flight at a time.
 *
 * Bandwidth is the thing to decide first. On CH32X035/X033 (USBFS, full
 * speed) every endpoint is 64 bytes, isochronous ones included, so:
 *   - bulk streaming (the default here, CFG_TUD_VIDEO_STREAMING_BULK=1) gets
 *     whatever the bus has left - several hundred KB/s on an idle bus, and the
 *     host retries a lost packet, so no frame is torn;
 *   - isochronous (-DCFG_TUD_VIDEO_STREAMING_BULK=0 in build_opt.h) is one
 *     64-byte packet per 1 ms frame: 62 bytes of picture, about 62 KB/s -
 *     a 64x48 YUY2 frame (6 KB) at 10 fps.
 * RAM bounds the frame before the bus does: a 20 KB part holds a 96x64 YUY2
 * frame (12 KB) and not much more. sendGeneratedFrame() (an extension) needs
 * no frame buffer at all - the bytes are asked for payload by payload - so a
 * generated or line-by-line source can send 160x120 or larger. A high-speed
 * controller (USBHS, not wired up yet) has 512-byte bulk and 1024-byte
 * isochronous packets every 125 us.
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

/* What the host committed to after probe/commit negotiation. It can differ
 * from what was advertised (a host may ask for a slower frame interval), so a
 * sketch that cares reads it here. */
struct CH32RVUsbDeviceVideoCommit {
    uint8_t formatIndex = 0;
    uint8_t frameIndex = 0;
    /* 100 ns units, as UVC counts: 333333 is 30 fps. */
    uint32_t frameInterval = 0;
    uint32_t maxVideoFrameSize = 0;
    /* Most the host accepts in one payload, header included; TinyUSB caps it
     * at CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE. */
    uint32_t maxPayloadTransferSize = 0;
};

class CH32RVUsbDeviceVideo : public CH32RVUsbDeviceFunction {
public:
    typedef void (*FrameCompleteCallback)();
    typedef void (*CommitCallback)(const CH32RVUsbDeviceVideoCommit &commit);
    /* Fill dst with `length` bytes of the frame starting at byte `offset`
     * (sendGeneratedFrame()). Runs on the USB task once per payload. */
    typedef void (*FillCallback)(uint8_t *dst, size_t length, uint32_t offset);

    /* name, when given, is the function name the host shows (iFunction on the
     * IAD and iInterface on both interfaces); CH32RV_USB_VIDEO_NAME otherwise. */
    explicit CH32RVUsbDeviceVideo(CH32RVUsbDevice &device = USBDevice, const char *name = nullptr);

    /* All four take effect when the device begins and are refused after,
     * because the host has read the descriptor by then. */
    bool setFormat(CH32RVUsbDeviceVideoFormat format);
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
    /* Bytes the descriptor takes at the given speed. */
    uint16_t descriptorLength(bool highSpeed = false) const;
    /* wMaxPacketSize of the isochronous endpoint at that speed: the payload
     * buffer, capped at 1023 (FS) / 1024 (HS). On USBFS (X035) 64 at most. */
    static uint16_t isochronousPacketSize(bool highSpeed);
    /* CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE as the library was built (0 on a
     * series without USB device support). */
    static uint16_t payloadBufferSize();
    /* CFG_TUD_VIDEO_STREAMING_BULK as the library was built. */
    static bool bulkStreaming();

    /* The host has committed and (isochronous) selected the streaming
     * alternate setting. False before, and after unplug. */
    bool streaming() const;
    /* Hand one whole frame to the class driver. The buffer is read payload by
     * payload, not copied: leave it alone until the completion callback (or
     * frameInFlight() turns false). Fails while a frame is in flight, before
     * the host streams, or on a null, empty or oversized frame. */
    bool sendFrame(const void *frame, size_t length);
    /* Send a `length`-byte frame with no buffer: the fill callback supplies
     * each payload's bytes as it goes out. An extension (TinyUSB's
     * tud_video_prepare_payload_cb); the way to send frames bigger than RAM. */
    bool sendGeneratedFrame(size_t length);
    bool frameInFlight() const { return _inFlight; }

    /* Runs on the USB task when the last payload of a frame has gone. Arming
     * the next frame from inside it is allowed; keep everything else out. */
    void onFrameComplete(FrameCompleteCallback callback) { _onFrameComplete = callback; }
    /* Runs when the host commits its streaming parameters. */
    void onCommit(CommitCallback callback) { _onCommit = callback; }
    void onFillPayload(FillCallback callback) { _onFill = callback; }
    /* What the host committed to, or zeros before it has. */
    CH32RVUsbDeviceVideoCommit commit() const { return _commit; }

    /* For the USB stack's callbacks. Not part of the sketch API. */
    void handleFrameComplete();
    void handleCommit(const CH32RVUsbDeviceVideoCommit &commit);
    void handleFill(uint8_t *dst, size_t length, uint32_t offset);

private:
    bool configurable() const;
    bool arm(void *frame, size_t length);

    CH32RVUsbDeviceVideoFormat _format = CH32RVUsbDeviceVideoFormat::Mjpeg;
    uint16_t _width = 320;
    uint16_t _height = 240;
    uint8_t _frameRate = 15;
    uint32_t _maxFrameSize = 0;
    volatile bool _inFlight = false;
    FrameCompleteCallback _onFrameComplete = nullptr;
    CommitCallback _onCommit = nullptr;
    FillCallback _onFill = nullptr;
    CH32RVUsbDeviceVideoCommit _commit;
};
