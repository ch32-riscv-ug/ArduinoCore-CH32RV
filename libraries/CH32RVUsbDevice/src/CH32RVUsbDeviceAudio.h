/* USB Audio, UAC1 or UAC2, in EspUsbDevice's shape (EspUsbAudioFunction,
 * renamed Esp -> CH32RV): one function with a playback stream (host to
 * device, a speaker), a capture stream (device to host, a microphone), or
 * both (a headset), each with one PCM format, a feature unit with mute and
 * volume per channel, and an event queue for what the host changed. Included
 * by CH32RVUsbDevice.h.
 *
 *     CH32RVUsbAudioFunction audio(device);
 *     CH32RVUsbAudioPlaybackStream &playback = audio.addPlaybackStream();
 *     ...
 *     playback.addFormat({16000, 2, 2, 16});   // before device.begin()
 *
 * Packets are isochronous, one per 1 ms frame at full speed and one per
 * 125 us microframe at high speed. Full speed on CH32X035/X033 caps every
 * endpoint at 64 bytes, so a stream there is at most 64 bytes per
 * millisecond: 16-bit mono up to 32 kHz, 16-bit stereo up to 16 kHz.
 * addFormat() refuses a format that does not fit. A high-speed controller
 * takes 48 kHz stereo.
 *
 * Packets move in ch32rv_usb_task() (after loop(), inside delay()): a sketch
 * that blocks for longer than the FIFOs hold (4 packets) loses samples.
 *
 * Differences from EspUsbDevice: one format per stream (Esp accepted more but
 * its begin() refused them), the function joins the device when declared,
 * and a format that cannot be described makes CH32RVUsbDevice::begin() fail
 * (DescriptorTooLarge) - check audio.begin() first to tell the two apart. */
#pragma once


/* One PCM format: sample frames per second, channels (1 or 2), bytes per
 * sample in the stream (2..4) and how many of their bits carry the sample
 * (16, 24 or 32). */
struct CH32RVUsbAudioFormat {
    uint32_t sampleRate = 48000;
    uint8_t channels = 2;
    uint8_t bytesPerSample = 2;
    uint8_t bitsPerSample = 16;
};

enum class CH32RVUsbAudioDirection : uint8_t {
    Playback,   /* host -> device (OUT) */
    Capture,    /* device -> host (IN) */
};

enum class CH32RVUsbAudioProtocol : uint8_t {
    Uac1,       /* every host has a driver; the default */
    Uac2,       /* clock entity, asynchronous playback with feedback */
};

enum class CH32RVUsbAudioEventType : uint8_t {
    SampleRateChanged,
    MuteChanged,
    VolumeChanged,
    StreamStateChanged,   /* the host opened (alt 1) or closed (alt 0) a stream */
};

enum class CH32RVUsbAudioEventTarget : uint8_t {
    Function,
    Playback,
    Capture,
};

struct CH32RVUsbAudioEvent {
    CH32RVUsbAudioEventType type = CH32RVUsbAudioEventType::SampleRateChanged;
    CH32RVUsbAudioEventTarget target = CH32RVUsbAudioEventTarget::Function;
    uint8_t channel = 0;            /* 0 = master */
    uint8_t alternateSetting = 0;
    uint32_t sampleRate = 0;
    int16_t volumeDb256 = 0;        /* 1/256 dB: -90 dB .. 0 dB in 1 dB steps */
    bool muted = false;
    bool enabled = false;
};

struct CH32RVUsbAudioStreamStats {
    uint32_t transferredBytes = 0;  /* through the endpoint */
    uint32_t overrunCount = 0;      /* times the FIFO overflowed */
    uint32_t overrunBytes = 0;      /* bytes overwritten */
    uint32_t underrunCount = 0;     /* capture: packets shorter than nominal */
    uint32_t underrunBytes = 0;
};

struct CH32RVUsbAudioVolumeRange {
    int16_t min = 0;
    int16_t max = 0;
    int16_t resolution = 0;
};

class CH32RVUsbAudioFunction;

class CH32RVUsbAudioPlaybackStream {
public:
    static constexpr CH32RVUsbAudioDirection direction() { return CH32RVUsbAudioDirection::Playback; }
    /* The stream's format, before the device begins. One per stream. */
    bool addFormat(const CH32RVUsbAudioFormat &format);
    /* Bytes received from the host and not read yet. */
    int available() const;
    size_t read(void *data, size_t length);
    bool clearBuffer();
    CH32RVUsbAudioStreamStats stats() const;
    void resetStats();

private:
    friend class CH32RVUsbAudioFunction;
    explicit CH32RVUsbAudioPlaybackStream(CH32RVUsbAudioFunction &function) : _function(function) {}

    CH32RVUsbAudioFunction &_function;
    volatile uint32_t _transferredBytes = 0;
    volatile uint32_t _overrunCount = 0;
    volatile uint32_t _overrunBytes = 0;
};

class CH32RVUsbAudioCaptureStream {
public:
    static constexpr CH32RVUsbAudioDirection direction() { return CH32RVUsbAudioDirection::Capture; }
    bool addFormat(const CH32RVUsbAudioFormat &format);
    /* Queue samples for the host. When the FIFO is full the oldest bytes
     * are overwritten (counted as an overrun). */
    size_t write(const void *data, size_t length);
    /* Bytes write() can take now without overwriting samples the host has not
     * fetched yet (an extension: EspUsbDevice has no such call). A sketch
     * whose own clock runs a little fast writes no more than this, so the
     * host's pace - one packet per USB frame - sets the rate. */
    int availableForWrite();
    bool clearBuffer();
    CH32RVUsbAudioStreamStats stats() const;
    void resetStats();

private:
    friend class CH32RVUsbAudioFunction;
    explicit CH32RVUsbAudioCaptureStream(CH32RVUsbAudioFunction &function) : _function(function) {}

    CH32RVUsbAudioFunction &_function;
    volatile uint32_t _transferredBytes = 0;
    volatile uint32_t _overrunCount = 0;
    volatile uint32_t _overrunBytes = 0;
    volatile uint32_t _underrunCount = 0;
    volatile uint32_t _underrunBytes = 0;
};

class CH32RVUsbAudioFunction : public CH32RVUsbDeviceFunction {
public:
    /* One audio function per device; a second is refused
     * with TooManyInstances. */
    explicit CH32RVUsbAudioFunction(CH32RVUsbDevice &device = USBDevice,
                                    CH32RVUsbAudioProtocol protocol = CH32RVUsbAudioProtocol::Uac1);

    CH32RVUsbAudioProtocol protocol() const { return _protocol; }
    /* Add the stream (each once; a second call returns the same one). Before
     * the device begins. */
    CH32RVUsbAudioPlaybackStream &addPlaybackStream();
    CH32RVUsbAudioCaptureStream &addCaptureStream();
    bool hasPlaybackStream() const { return _playbackEnabled; }
    bool hasCaptureStream() const { return _captureEnabled; }

    /* Whether the function can be described: a stream, and a format on each
     * stream added. The device does not call it; CH32RVUsbDevice::begin()
     * fails in the same cases. end() clears events, stats and stream state. */
    bool begin();
    void end();
    uint8_t interfaceCount() const;
    uint8_t endpointCount() const;

    /* The sample rate the host set (UAC2) or the format's (UAC1). */
    uint32_t currentSampleRate() const;
    /* Mute and volume of the feature unit on a stream; channel 0 is the
     * master, 1..channels the channels. Volume in 1/256 dB, -90 dB .. 0 dB,
     * whole dB. set*() from the sketch queue an event like the host's do. */
    bool hasMute(CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                 uint8_t channel = 0) const;
    bool hasVolume(CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                   uint8_t channel = 0) const;
    bool getMute(bool &mute, CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                 uint8_t channel = 0) const;
    bool setMute(bool mute, CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                 uint8_t channel = 0);
    bool getVolume(int16_t &volume, CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                   uint8_t channel = 0) const;
    bool setVolume(int16_t volume, CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                   uint8_t channel = 0);
    bool getVolumeRange(CH32RVUsbAudioVolumeRange &range,
                        CH32RVUsbAudioDirection direction = CH32RVUsbAudioDirection::Playback,
                        uint8_t channel = 0) const;
    /* What the host changed, oldest first; 8 are kept, more are dropped. */
    bool pollEvent(CH32RVUsbAudioEvent &event);
    size_t pendingEvents() const;
    uint32_t droppedEvents() const;
    void clearEvents();

    /* The configuration descriptor part (CH32RVUsbFunctionType::descriptor)
     * at a speed. False when the function cannot be described. */
    bool writeDescriptor(CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep, uint8_t str,
                         CH32RVUsbSpeed speed) const;

protected:
    void usbReset() override;
    void usbConfigured(CH32RVUsbSpeed speed) override;
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup) override;
    bool usbSetInterface(uint8_t interfaceNumber, uint8_t alternate) override;
    void usbTransferred(uint8_t address, uint16_t length) override;

private:
    friend class CH32RVUsbAudioPlaybackStream;
    friend class CH32RVUsbAudioCaptureStream;

    struct Stream {
        CH32RVUsbAudioFormat format;
        bool hasFormat = false;
        uint8_t alternate = 0;
        bool muted[3] = {};
        int16_t volume[3] = {};
        /* Set when the descriptor is written. */
        uint8_t interfaceNumber = 0;
        uint8_t dataEndpoint = 0;
        uint8_t feedbackEndpoint = 0;
    };

    bool addFormat(CH32RVUsbAudioDirection direction, const CH32RVUsbAudioFormat &format);
    /* A stream, and a format on each stream added (begin()). */
    bool describable() const;
    Stream *stream(CH32RVUsbAudioDirection direction);
    const Stream *stream(CH32RVUsbAudioDirection direction) const;
    /* Entity IDs, in EspUsbDevice's order: clock 1, then playback's USB
     * input terminal, feature unit and speaker, then capture's microphone,
     * feature unit and USB output terminal. 0 when the stream is absent. */
    uint8_t featureId(CH32RVUsbAudioDirection direction) const;
    uint8_t usbTerminalId(CH32RVUsbAudioDirection direction) const;
    uint8_t physicalTerminalId(CH32RVUsbAudioDirection direction) const;
    const Stream *featureStream(uint8_t entityId, CH32RVUsbAudioEventTarget *target) const;
    Stream *featureStream(uint8_t entityId, CH32RVUsbAudioEventTarget *target);
    bool controlValue(const Stream &s, uint8_t selector, uint8_t channel, int32_t &value) const;
    bool applyControl(uint8_t entityId, uint8_t selector, uint8_t channel, int32_t value);
    void pushEvent(const CH32RVUsbAudioEvent &event);
    void updateInterfaceCount();
    /* wMaxPacketSize for a format: one (micro)frame's nominal sample frames
     * plus one, within the endpoint's limit. 0 when the format is malformed
     * or its nominal packet does not fit (CH32RVUsbDeviceAudioDescriptor.cpp). */
    uint16_t packetSize(const CH32RVUsbAudioFormat &format, CH32RVUsbAudioDirection direction, bool hs) const;
    /* The largest packet an endpoint takes: the controller's, within the bus
     * limit (CH32RVUsbDeviceAudio.cpp). */
    uint16_t endpointLimit(bool hs) const;
    /* Requests on the control interface's entities, and UAC1's on a data
     * endpoint: the answer to a GET goes into _ctl. */
    bool getEntity(const CH32RVUsbSetup &setup, uint16_t &length);
    bool setEntity(const CH32RVUsbSetup &setup, const uint8_t *data);
    bool getEndpoint(const CH32RVUsbSetup &setup, uint16_t &length);
    bool setEndpoint(const CH32RVUsbSetup &setup, const uint8_t *data);
    void receivePacket();
    void sendPacket();
    void sendFeedback();

    CH32RVUsbAudioProtocol _protocol;
    bool _playbackEnabled = false;
    bool _captureEnabled = false;
    CH32RVUsbAudioPlaybackStream _playback{*this};
    CH32RVUsbAudioCaptureStream _capture{*this};
    mutable Stream _streams[2];          /* [0] playback, [1] capture */
    mutable uint8_t _controlInterface = 0;
    uint32_t _sampleRate = 0;
    CH32RVUsbAudioEvent _events[8];
    volatile uint8_t _eventHead = 0;
    volatile uint8_t _eventTail = 0;
    uint32_t _droppedEvents = 0;

    /* The streams' data: a ring each way between the endpoint and read() /
     * write(), four packets deep, and the packet in flight. Transfers
     * finish in ch32rv_usb_task(), the sketch's own thread. */
    static constexpr uint16_t FIFO_SIZE = 4 * CH32RV_USB_MAX_PACKET;
    uint8_t _rxFifo[FIFO_SIZE];
    uint8_t _txFifo[FIFO_SIZE];
    uint8_t _rxPacket[CH32RV_USB_MAX_PACKET] __attribute__((aligned(4)));
    uint8_t _txPacket[CH32RV_USB_MAX_PACKET] __attribute__((aligned(4)));
    uint8_t _feedback[4] __attribute__((aligned(4)));
    uint8_t _ctl[16] __attribute__((aligned(4)));
    uint16_t _rxHead = 0, _rxCount = 0, _rxDepth = 0;
    uint16_t _txHead = 0, _txCount = 0, _txDepth = 0;
    uint16_t _rxPacketSize = 0, _txPacketSize = 0;
    uint32_t _txRemainder = 0;                /* sample frames' fraction carried between packets */
    bool _highSpeed = false;
};
