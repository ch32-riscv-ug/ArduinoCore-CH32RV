/* USB Audio (UAC1 / UAC2) on the device stack, with EspUsbDevice's
 * topology, controls and events (EspUsbAudio.cpp and its internal model,
 * control, request and event files, folded into one). The descriptor is in
 * CH32RVUsbDeviceAudioDescriptor.cpp.
 *
 * Everything - packets, control requests, and so every event - happens in
 * the USB task, i.e. the sketch's own thread (after loop(), inside delay()),
 * so the FIFOs need no locking. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

namespace {

CH32RVUsbAudioFunction *g_audio = nullptr;

/* Feature unit volume: -90 dB .. 0 dB in 1 dB steps, in 1/256 dB. */
constexpr int32_t VOLUME_MIN = -90 * 256;
constexpr int32_t VOLUME_MAX = 0;
constexpr int32_t VOLUME_RESOLUTION = 256;

/* Control selectors: on the clock (UAC2) and on a feature unit. */
constexpr uint8_t SEL_SAMPLE_RATE = 1;
constexpr uint8_t SEL_CLOCK_VALID = 2;
constexpr uint8_t SEL_MUTE = 1;
constexpr uint8_t SEL_VOLUME = 2;
constexpr uint8_t CLOCK_ID = 1;

/* Requests: UAC2 CUR / RANGE, UAC1 SET_CUR and GET_CUR..GET_RES. */
constexpr uint8_t UAC2_CUR = 0x01;
constexpr uint8_t UAC2_RANGE = 0x02;
constexpr uint8_t UAC1_SET_CUR = 0x01;
constexpr uint8_t UAC1_GET_CUR = 0x81;
constexpr uint8_t UAC1_GET_MIN = 0x82;
constexpr uint8_t UAC1_GET_MAX = 0x83;
constexpr uint8_t UAC1_GET_RES = 0x84;

void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

uint8_t slot(CH32RVUsbAudioDirection d)
{
    return d == CH32RVUsbAudioDirection::Playback ? 0 : 1;
}

bool audio_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf, uint8_t *ep,
                      uint8_t str, CH32RVUsbSpeed speed)
{
    return static_cast<const CH32RVUsbAudioFunction &>(self).writeDescriptor(w, itf, ep, str, speed);
}

/* Not const: the interface count follows the streams the sketch adds (one
 * audio function per device, so one type object serves it). */
CH32RVUsbFunctionType audio_type = {audio_descriptor, 1, 1, nullptr, 0};

} // namespace

/* ---------------------------------------------------------- setup */
CH32RVUsbAudioFunction::CH32RVUsbAudioFunction(CH32RVUsbDevice &device, CH32RVUsbAudioProtocol protocol)
    : CH32RVUsbDeviceFunction(device, audio_type, CH32RV_USB_AUDIO_NAME), _protocol(protocol)
{
    if (attached() && !g_audio) {
        g_audio = this;
    }
}

uint16_t CH32RVUsbAudioFunction::endpointLimit(bool hs) const
{
    const uint32_t controller = device().maxPacketSize();
    const uint32_t bus = hs ? 1024u : 1023u;
    const uint32_t limit = controller < bus ? controller : bus;
    return (uint16_t)(limit < CH32RV_USB_MAX_PACKET ? limit : CH32RV_USB_MAX_PACKET);
}

void CH32RVUsbAudioFunction::updateInterfaceCount()
{
    if (g_audio == this) {
        audio_type.interfaces = interfaceCount();
    }
}

CH32RVUsbAudioPlaybackStream &CH32RVUsbAudioFunction::addPlaybackStream()
{
    if (!device().started()) {
        _playbackEnabled = true;
        updateInterfaceCount();
    }
    return _playback;
}

CH32RVUsbAudioCaptureStream &CH32RVUsbAudioFunction::addCaptureStream()
{
    if (!device().started()) {
        _captureEnabled = true;
        updateInterfaceCount();
    }
    return _capture;
}

bool CH32RVUsbAudioPlaybackStream::addFormat(const CH32RVUsbAudioFormat &format)
{
    return _function.addFormat(CH32RVUsbAudioDirection::Playback, format);
}

bool CH32RVUsbAudioCaptureStream::addFormat(const CH32RVUsbAudioFormat &format)
{
    return _function.addFormat(CH32RVUsbAudioDirection::Capture, format);
}

/* Checked against full speed, which every controller can fall back to. */
bool CH32RVUsbAudioFunction::addFormat(CH32RVUsbAudioDirection d, const CH32RVUsbAudioFormat &format)
{
    Stream &s = _streams[slot(d)];
    if (device().started() || !packetSize(format, d, false)) {
        return false;
    }
    if (s.hasFormat) {
        /* Another rate of the same PCM shape. */
        if (format.channels != s.format.channels || format.bytesPerSample != s.format.bytesPerSample ||
            format.bitsPerSample != s.format.bitsPerSample) {
            return false;
        }
        for (uint8_t i = 0; i < s.rateCount; i++) {
            if (s.rates[i] == format.sampleRate) {
                return true;                     /* already there */
            }
        }
        if (s.rateCount >= RATES_MAX) {
            return false;
        }
    } else {
        s.format = format;
        s.hasFormat = true;
        s.rateCount = 0;
    }
    s.rates[s.rateCount++] = format.sampleRate;
    firstRate();
    return true;
}

void CH32RVUsbAudioFunction::firstRate()
{
    const Stream &s = _streams[0].hasFormat ? _streams[0] : _streams[1];
    _sampleRate = s.hasFormat ? s.rates[0] : 0;
}

CH32RVUsbAudioFormat CH32RVUsbAudioFunction::widest(uint8_t i) const
{
    CH32RVUsbAudioFormat f = _streams[i].format;
    for (uint8_t r = 0; r < _streams[i].rateCount; r++) {
        if (_streams[i].rates[r] > f.sampleRate) {
            f.sampleRate = _streams[i].rates[r];
        }
    }
    return f;
}

bool CH32RVUsbAudioFunction::setRate(uint32_t rate)
{
    const Stream &s = clockStream();
    bool known = false;
    for (uint8_t i = 0; i < s.rateCount; i++) {
        known = known || s.rates[i] == rate;
    }
    if (!known) {
        return false;                            /* the clock stays as it is */
    }
    if (rate != _sampleRate) {
        _sampleRate = rate;
        _txRemainder = 0;
        CH32RVUsbAudioEvent e;
        e.type = CH32RVUsbAudioEventType::SampleRateChanged;
        e.target = CH32RVUsbAudioEventTarget::Function;
        e.sampleRate = rate;
        pushEvent(e);
    }
    return true;
}

bool CH32RVUsbAudioFunction::configure(const CH32RVUsbAudioConfig &config)
{
    if (device().started() || config.eventCapacity > sizeof _events / sizeof _events[0] ||
        !config.playbackBuffer != !config.playbackBufferBytes || !config.captureBuffer != !config.captureBufferBytes) {
        return false;
    }
    _config = config;
    return true;
}

bool CH32RVUsbAudioPlaybackStream::clearFormats()
{
    return _function.clearFormat(CH32RVUsbAudioDirection::Playback);
}

bool CH32RVUsbAudioCaptureStream::clearFormats()
{
    return _function.clearFormat(CH32RVUsbAudioDirection::Capture);
}

bool CH32RVUsbAudioFunction::clearFormat(CH32RVUsbAudioDirection d)
{
    if (device().started()) {
        return false;
    }
    _streams[slot(d)].hasFormat = false;
    _streams[slot(d)].rateCount = 0;
    firstRate();
    return true;
}

bool CH32RVUsbAudioPlaybackStream::currentFormat(CH32RVUsbAudioFormat &format) const
{
    const CH32RVUsbAudioFunction::Stream &s = _function._streams[0];
    if (!s.hasFormat) {
        return false;
    }
    format = s.format;
    format.sampleRate = _function._sampleRate;
    return true;
}

bool CH32RVUsbAudioCaptureStream::currentFormat(CH32RVUsbAudioFormat &format) const
{
    const CH32RVUsbAudioFunction::Stream &s = _function._streams[1];
    if (!s.hasFormat) {
        return false;
    }
    format = s.format;
    format.sampleRate = _function._sampleRate;
    return true;
}

/* Stream i's ring for a nominal packet of `packet` bytes: the configured
 * sample frames (or four packets), in the sketch's buffer or the class's,
 * whole sample frames. False when the frames asked for do not fit. */
bool CH32RVUsbAudioFunction::ring(uint8_t i, uint16_t packet, uint8_t *&buffer, uint16_t &depth) const
{
    const Stream &st = _streams[i];
    const size_t frame = (size_t)st.format.channels * st.format.bytesPerSample;
    const size_t frames = i == 0 ? _config.playbackBufferFrames : _config.captureBufferFrames;
    uint8_t *const given = i == 0 ? _config.playbackBuffer : _config.captureBuffer;
    const size_t room = given ? (i == 0 ? _config.playbackBufferBytes : _config.captureBufferBytes) : FIFO_SIZE;
    buffer = given ? given : const_cast<uint8_t *>(i == 0 ? _rxStore : _txStore);
    size_t want = frames ? frames * frame : (size_t)4 * packet;
    if (frames && (want > room || want > 0xffffu)) {
        return false;
    }
    want = want < room ? want : room;
    want = want < 0xffffu ? want : 0xffffu;
    depth = frame ? (uint16_t)(want - want % frame) : 0;
    return true;
}

bool CH32RVUsbAudioFunction::begin()
{
    if (!attached() || g_audio != this || !describable()) {
        return false;
    }
    end();
    return true;
}

void CH32RVUsbAudioFunction::end()
{
    _streams[0].alternate = 0;
    _streams[1].alternate = 0;
    clearEvents();
    _playback.resetStats();
    _capture.resetStats();
}

uint8_t CH32RVUsbAudioFunction::interfaceCount() const
{
    return (uint8_t)(1 + (_playbackEnabled ? 1 : 0) + (_captureEnabled ? 1 : 0));
}

uint8_t CH32RVUsbAudioFunction::endpointCount() const
{
    /* OUT: playback. IN: capture, and UAC2 playback's feedback. */
    const uint8_t out = _playbackEnabled ? 1 : 0;
    const uint8_t in = (uint8_t)((_captureEnabled ? 1 : 0) +
                                 (_playbackEnabled && _protocol == CH32RVUsbAudioProtocol::Uac2 ? 1 : 0));
    return out > in ? out : in;
}

uint32_t CH32RVUsbAudioFunction::currentSampleRate() const
{
    return _sampleRate;
}

/* ---------------------------------------------------------- controls */
CH32RVUsbAudioFunction::Stream *CH32RVUsbAudioFunction::stream(CH32RVUsbAudioDirection d)
{
    Stream &s = _streams[slot(d)];
    const bool on = d == CH32RVUsbAudioDirection::Playback ? _playbackEnabled : _captureEnabled;
    return on && s.hasFormat ? &s : nullptr;
}

const CH32RVUsbAudioFunction::Stream *CH32RVUsbAudioFunction::stream(CH32RVUsbAudioDirection d) const
{
    return const_cast<CH32RVUsbAudioFunction *>(this)->stream(d);
}

CH32RVUsbAudioFunction::Stream *CH32RVUsbAudioFunction::featureStream(uint8_t id, CH32RVUsbAudioEventTarget *target)
{
    if (id == 0) {
        return nullptr;
    }
    if (id == featureId(CH32RVUsbAudioDirection::Playback)) {
        if (target) {
            *target = CH32RVUsbAudioEventTarget::Playback;
        }
        return stream(CH32RVUsbAudioDirection::Playback);
    }
    if (id == featureId(CH32RVUsbAudioDirection::Capture)) {
        if (target) {
            *target = CH32RVUsbAudioEventTarget::Capture;
        }
        return stream(CH32RVUsbAudioDirection::Capture);
    }
    return nullptr;
}

const CH32RVUsbAudioFunction::Stream *CH32RVUsbAudioFunction::featureStream(uint8_t id,
                                                                           CH32RVUsbAudioEventTarget *target) const
{
    return const_cast<CH32RVUsbAudioFunction *>(this)->featureStream(id, target);
}

/* Channel 0 is the master; the feature unit has every control on each. */
bool CH32RVUsbAudioFunction::controlValue(const Stream &s, uint8_t selector, uint8_t channel, int32_t &value) const
{
    if (channel > s.format.channels || channel >= 3) {
        return false;
    }
    if (selector == SEL_MUTE) {
        value = s.muted[channel] ? 1 : 0;
    } else if (selector == SEL_VOLUME) {
        value = s.volume[channel];
    } else {
        return false;
    }
    return true;
}

bool CH32RVUsbAudioFunction::applyControl(uint8_t entityId, uint8_t selector, uint8_t channel, int32_t value)
{
    CH32RVUsbAudioEventTarget target = CH32RVUsbAudioEventTarget::Function;
    Stream *s = featureStream(entityId, &target);
    int32_t previous = 0;
    if (!s || !controlValue(*s, selector, channel, previous)) {
        return false;
    }
    CH32RVUsbAudioEvent e;
    e.target = target;
    e.channel = channel;
    if (selector == SEL_MUTE) {
        if (value != 0 && value != 1) {
            return false;
        }
        s->muted[channel] = value != 0;
        e.type = CH32RVUsbAudioEventType::MuteChanged;
        e.muted = value != 0;
    } else {
        if (value < VOLUME_MIN || value > VOLUME_MAX || value % VOLUME_RESOLUTION != 0) {
            return false;
        }
        s->volume[channel] = (int16_t)value;
        e.type = CH32RVUsbAudioEventType::VolumeChanged;
        e.volumeDb256 = (int16_t)value;
    }
    if (previous != value) {
        pushEvent(e);
    }
    return true;
}

bool CH32RVUsbAudioFunction::hasMute(CH32RVUsbAudioDirection direction, uint8_t channel) const
{
    bool muted = false;
    return getMute(muted, direction, channel);
}

bool CH32RVUsbAudioFunction::hasVolume(CH32RVUsbAudioDirection direction, uint8_t channel) const
{
    int16_t volume = 0;
    return getVolume(volume, direction, channel);
}

bool CH32RVUsbAudioFunction::getMute(bool &mute, CH32RVUsbAudioDirection direction, uint8_t channel) const
{
    const Stream *s = stream(direction);
    int32_t value = 0;
    if (!s || !controlValue(*s, SEL_MUTE, channel, value)) {
        return false;
    }
    mute = value != 0;
    return true;
}

bool CH32RVUsbAudioFunction::setMute(bool mute, CH32RVUsbAudioDirection direction, uint8_t channel)
{
    return applyControl(featureId(direction), SEL_MUTE, channel, mute ? 1 : 0);
}

bool CH32RVUsbAudioFunction::getVolume(int16_t &volume, CH32RVUsbAudioDirection direction, uint8_t channel) const
{
    const Stream *s = stream(direction);
    int32_t value = 0;
    if (!s || !controlValue(*s, SEL_VOLUME, channel, value)) {
        return false;
    }
    volume = (int16_t)value;
    return true;
}

bool CH32RVUsbAudioFunction::setVolume(int16_t volume, CH32RVUsbAudioDirection direction, uint8_t channel)
{
    return applyControl(featureId(direction), SEL_VOLUME, channel, volume);
}

bool CH32RVUsbAudioFunction::getVolumeRange(CH32RVUsbAudioVolumeRange &range, CH32RVUsbAudioDirection direction,
                                            uint8_t channel) const
{
    if (!hasVolume(direction, channel)) {
        return false;
    }
    range.min = (int16_t)VOLUME_MIN;
    range.max = (int16_t)VOLUME_MAX;
    range.resolution = (int16_t)VOLUME_RESOLUTION;
    return true;
}

/* ---------------------------------------------------------- events
 * Pushed and popped from the sketch's thread only (see the top). */
void CH32RVUsbAudioFunction::pushEvent(const CH32RVUsbAudioEvent &event)
{
    const uint8_t head = _eventHead;
    if ((uint8_t)(head - _eventTail) >= _config.eventCapacity) {
        _droppedEvents++;
        return;
    }
    _events[head % 8] = event;
    _eventHead = (uint8_t)(head + 1);
}

bool CH32RVUsbAudioFunction::pollEvent(CH32RVUsbAudioEvent &event)
{
    const uint8_t tail = _eventTail;
    if (tail == _eventHead) {
        return false;
    }
    event = _events[tail % 8];
    _eventTail = (uint8_t)(tail + 1);
    return true;
}

size_t CH32RVUsbAudioFunction::pendingEvents() const
{
    return (uint8_t)(_eventHead - _eventTail);
}

uint32_t CH32RVUsbAudioFunction::droppedEvents() const
{
    return _droppedEvents;
}

void CH32RVUsbAudioFunction::clearEvents()
{
    _eventTail = _eventHead;
    _droppedEvents = 0;
}

/* ---------------------------------------------------------- stats */
CH32RVUsbAudioStreamStats CH32RVUsbAudioPlaybackStream::stats() const
{
    CH32RVUsbAudioStreamStats r;
    const uint32_t irq = ch32rv_irq_save();
    r.transferredBytes = _transferredBytes;
    r.overrunCount = _overrunCount;
    r.overrunBytes = _overrunBytes;
    ch32rv_irq_restore(irq);
    return r;
}

void CH32RVUsbAudioPlaybackStream::resetStats()
{
    const uint32_t irq = ch32rv_irq_save();
    _transferredBytes = 0;
    _overrunCount = 0;
    _overrunBytes = 0;
    ch32rv_irq_restore(irq);
}

CH32RVUsbAudioStreamStats CH32RVUsbAudioCaptureStream::stats() const
{
    CH32RVUsbAudioStreamStats r;
    const uint32_t irq = ch32rv_irq_save();
    r.transferredBytes = _transferredBytes;
    r.overrunCount = _overrunCount;
    r.overrunBytes = _overrunBytes;
    r.underrunCount = _underrunCount;
    r.underrunBytes = _underrunBytes;
    ch32rv_irq_restore(irq);
    return r;
}

void CH32RVUsbAudioCaptureStream::resetStats()
{
    const uint32_t irq = ch32rv_irq_save();
    _transferredBytes = 0;
    _overrunCount = 0;
    _overrunBytes = 0;
    _underrunCount = 0;
    _underrunBytes = 0;
    ch32rv_irq_restore(irq);
}

/* ---------------------------------------------------------- data */
static void fifo_put(uint8_t *fifo, uint16_t depth, uint16_t &head, uint16_t &count, const uint8_t *data,
                     uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        fifo[(head + count) % depth] = data[i];
        if (count < depth) {
            count++;
        } else {
            head = (uint16_t)((head + 1) % depth);   /* full: the oldest byte goes */
        }
    }
}

static uint16_t fifo_get(const uint8_t *fifo, uint16_t depth, uint16_t &head, uint16_t &count, uint8_t *data,
                         uint16_t n)
{
    if (n > count) {
        n = count;
    }
    for (uint16_t i = 0; i < n; i++) {
        data[i] = fifo[head];
        head = (uint16_t)((head + 1) % depth);
    }
    count = (uint16_t)(count - n);
    return n;
}

int CH32RVUsbAudioPlaybackStream::available() const
{
    ch32rv_usb_task();
    return _function._rxCount;
}

size_t CH32RVUsbAudioPlaybackStream::capacity() const
{
    return _function._rxDepth;
}

size_t CH32RVUsbAudioCaptureStream::capacity() const
{
    return _function._txDepth;
}

static uint16_t frame_bytes(const CH32RVUsbAudioFormat &f)
{
    const uint16_t n = (uint16_t)(f.channels * f.bytesPerSample);
    return n ? n : 1;
}

size_t CH32RVUsbAudioPlaybackStream::read(void *data, size_t length)
{
    if (!data || length == 0) {
        return 0;
    }
    ch32rv_usb_task();
    CH32RVUsbAudioFunction &f = _function;
    const uint16_t frame = frame_bytes(f._streams[0].format);
    uint16_t n = (uint16_t)(length > 0xffffu ? 0xffffu : length);
    if (n > f._rxCount) {
        n = f._rxCount;
    }
    n = (uint16_t)(n - n % frame);
    return fifo_get(f._rxFifo, f._rxDepth ? f._rxDepth : 1, f._rxHead, f._rxCount, (uint8_t *)data, n);
}

bool CH32RVUsbAudioPlaybackStream::clearBuffer()
{
    _function._rxHead = _function._rxCount = 0;
    return true;
}

size_t CH32RVUsbAudioCaptureStream::write(const void *data, size_t length)
{
    CH32RVUsbAudioFunction &f = _function;
    if (!data || length == 0 || !f._txDepth) {
        return 0;
    }
    ch32rv_usb_task();
    const uint16_t room = (uint16_t)(f._txDepth - f._txCount);
    uint16_t n = (uint16_t)(length < room ? length : room);
    n = (uint16_t)(n - n % frame_bytes(f._streams[1].format));
    if (n < length) {
        _overrunCount = _overrunCount + 1;
        _overrunBytes = _overrunBytes + (uint32_t)(length - n);
    }
    fifo_put(f._txFifo, f._txDepth, f._txHead, f._txCount, (const uint8_t *)data, n);
    return n;
}

size_t CH32RVUsbAudioCaptureStream::availableForWrite() const
{
    ch32rv_usb_task();
    return (size_t)(_function._txDepth - _function._txCount);
}

bool CH32RVUsbAudioCaptureStream::clearBuffer()
{
    _function._txHead = _function._txCount = 0;
    return true;
}

void CH32RVUsbAudioFunction::usbReset()
{
    for (uint8_t i = 0; i < 2; i++) {
        if (_streams[i].alternate) {
            _streams[i].alternate = 0;
            CH32RVUsbAudioEvent e;
            e.type = CH32RVUsbAudioEventType::StreamStateChanged;
            e.target = i ? CH32RVUsbAudioEventTarget::Capture : CH32RVUsbAudioEventTarget::Playback;
            pushEvent(e);
        }
    }
}

/* The FIFOs: four nominal packets of this speed's, within what fits. */
void CH32RVUsbAudioFunction::usbConfigured(CH32RVUsbSpeed speed)
{
    _highSpeed = ch32rv_usb_series_can(CH32RVUsbSpeed::High) && speed != CH32RVUsbSpeed::Full;
    _rxPacketSize = stream(CH32RVUsbAudioDirection::Playback)
                        ? packetSize(widest(0), CH32RVUsbAudioDirection::Playback, _highSpeed)
                        : 0;
    _txPacketSize = stream(CH32RVUsbAudioDirection::Capture)
                        ? packetSize(widest(1), CH32RVUsbAudioDirection::Capture, _highSpeed)
                        : 0;
    _rxDepth = _txDepth = 0;
    if (!_rxPacketSize || !ring(0, _rxPacketSize, _rxFifo, _rxDepth)) {
        _rxDepth = 0;
    }
    if (!_txPacketSize || !ring(1, _txPacketSize, _txFifo, _txDepth)) {
        _txDepth = 0;
    }
    _rxHead = _rxCount = _txHead = _txCount = 0;
    _txRemainder = 0;
}

void CH32RVUsbAudioFunction::receivePacket()
{
    if (_streams[0].alternate) {
        device().transfer(_streams[0].dataEndpoint, _rxPacket, _rxPacketSize);
    }
}

/* One (micro)frame's samples: the nominal count, its fraction carried to the
 * next packet. Short when write() fell behind. */
void CH32RVUsbAudioFunction::sendPacket()
{
    if (!_streams[1].alternate) {
        return;
    }
    const CH32RVUsbAudioFormat &f = _streams[1].format;
    const uint32_t frames = _highSpeed ? 8000u : 1000u;
    _txRemainder += _sampleRate;
    uint32_t samples = _txRemainder / frames;
    _txRemainder %= frames;
    const uint16_t frameBytes = (uint16_t)(f.channels * f.bytesPerSample);
    uint32_t want = samples * frameBytes;
    if (want > _txPacketSize) {
        want = _txPacketSize;
    }
    uint16_t n = (uint16_t)(_txCount < want ? _txCount - _txCount % frameBytes : want);
    n = fifo_get(_txFifo, _txDepth, _txHead, _txCount, _txPacket, n);
    if (n < want) {
        _capture._underrunCount = _capture._underrunCount + 1;
        _capture._underrunBytes = _capture._underrunBytes + (uint32_t)(want - n);
    }
    device().transfer(_streams[1].dataEndpoint, _txPacket, n);
}

/* UAC2 playback is asynchronous: the feedback endpoint steers the host by
 * the receive FIFO's level, around half full - samples per frame in 10.14
 * (3 bytes) at full speed, per microframe in 16.16 at high speed. */
void CH32RVUsbAudioFunction::sendFeedback()
{
    const Stream &s = _streams[0];
    if (!s.alternate || !s.feedbackEndpoint) {
        return;
    }
    const uint32_t frames = _highSpeed ? 8000u : 1000u;
    const uint8_t shift = _highSpeed ? 16 : 14;
    const uint32_t nominal = (uint32_t)(((uint64_t)_sampleRate << shift) / frames);
    const int32_t frameBytes = s.format.channels * s.format.bytesPerSample;
    /* Off from half full by k sample frames: ask for k/32 more or fewer per
     * frame, at most one. */
    int32_t k = ((int32_t)_rxDepth / 2 - (int32_t)_rxCount) / frameBytes;
    int32_t adjust = (int32_t)(((int64_t)k << shift) / 32);
    const int32_t one = (int32_t)1 << shift;
    adjust = adjust > one ? one : adjust < -one ? -one : adjust;
    put32(_feedback, (uint32_t)((int32_t)nominal + adjust));
    device().transfer(s.feedbackEndpoint, _feedback, _highSpeed ? 4 : 3);
}

void CH32RVUsbAudioFunction::usbTransferred(uint8_t address, uint16_t length)
{
    Stream &ps = _streams[0], &cs = _streams[1];
    if (_playbackEnabled && address == ps.dataEndpoint) {
        if (!ps.alternate) {
            return;
        }
        _playback._transferredBytes = _playback._transferredBytes + length;
        const uint16_t room = (uint16_t)(_rxDepth - _rxCount);
        if (length > room) {
            _playback._overrunCount = _playback._overrunCount + 1;
            _playback._overrunBytes = _playback._overrunBytes + (uint32_t)(length - room);
        }
        fifo_put(_rxFifo, _rxDepth, _rxHead, _rxCount, _rxPacket, length);
        receivePacket();
    } else if (_playbackEnabled && ps.feedbackEndpoint && address == ps.feedbackEndpoint) {
        sendFeedback();
    } else if (_captureEnabled && address == cs.dataEndpoint) {
        if (!cs.alternate) {
            return;
        }
        _capture._transferredBytes = _capture._transferredBytes + length;
        sendPacket();
    }
}

/* ---------------------------------------------------------- requests */
static uint16_t clip(uint16_t n, uint16_t wLength)
{
    return n < wLength ? n : wLength;
}

/* GET on an entity: the UAC2 clock (rate, validity) or a feature unit
 * (mute, volume); UAC1 has only the feature units. wLength is the host's
 * buffer, so the answer is cut to it rather than refused. */
bool CH32RVUsbAudioFunction::getEntity(const CH32RVUsbSetup &r, uint16_t &length)
{
    if (r.wLength == 0) {
        return false;
    }
    const uint8_t itf = (uint8_t)(r.wIndex & 0xff), entity = (uint8_t)(r.wIndex >> 8);
    const uint8_t channel = (uint8_t)(r.wValue & 0xff), selector = (uint8_t)(r.wValue >> 8);
    if (itf != _controlInterface) {
        return false;
    }
    uint8_t *buf = _ctl;
    uint16_t n = 0;
    if (_protocol == CH32RVUsbAudioProtocol::Uac2 && entity == CLOCK_ID) {
        if (channel != 0) {
            return false;
        }
        if (selector == SEL_SAMPLE_RATE && r.bRequest == UAC2_CUR) {
            put32(buf, _sampleRate);
            n = 4;
        } else if (selector == SEL_SAMPLE_RATE && r.bRequest == UAC2_RANGE) {
            /* Each rate a subrange of its own; Linux first reads only wNumSubRanges. */
            const Stream &s = clockStream();
            put16(buf, s.rateCount);
            for (uint8_t i = 0; i < s.rateCount; i++) {
                put32(buf + 2 + 12 * i, s.rates[i]);
                put32(buf + 6 + 12 * i, s.rates[i]);
                put32(buf + 10 + 12 * i, 0);
            }
            n = (uint16_t)(2 + 12 * s.rateCount);
        } else if (selector == SEL_CLOCK_VALID && r.bRequest == UAC2_CUR) {
            buf[0] = 1;
            n = 1;
        } else {
            return false;
        }
    } else {
        const Stream *s = featureStream(entity, nullptr);
        int32_t value = 0;
        if (!s || !controlValue(*s, selector, channel, value)) {
            return false;
        }
        const bool cur = r.bRequest == (_protocol == CH32RVUsbAudioProtocol::Uac2 ? UAC2_CUR : UAC1_GET_CUR);
        if (cur) {
            if (selector == SEL_MUTE) {
                buf[0] = (uint8_t)value;
                n = 1;
            } else {
                put16(buf, (uint16_t)(int16_t)value);
                n = 2;
            }
        } else if (selector != SEL_VOLUME) {
            return false;
        } else if (_protocol == CH32RVUsbAudioProtocol::Uac2) {
            if (r.bRequest != UAC2_RANGE) {
                return false;
            }
            put16(buf, 1);
            put16(buf + 2, (uint16_t)(int16_t)VOLUME_MIN);
            put16(buf + 4, (uint16_t)(int16_t)VOLUME_MAX);
            put16(buf + 6, (uint16_t)(int16_t)VOLUME_RESOLUTION);
            n = 8;
        } else {
            const int32_t v = r.bRequest == UAC1_GET_MIN   ? VOLUME_MIN
                              : r.bRequest == UAC1_GET_MAX ? VOLUME_MAX
                              : r.bRequest == UAC1_GET_RES ? VOLUME_RESOLUTION
                                                           : 1;
            if (v == 1) {
                return false;
            }
            put16(buf, (uint16_t)(int16_t)v);
            n = 2;
        }
    }
    length = clip(n, r.wLength);
    return true;
}

/* SET_CUR on an entity. The clock takes the rates it offers. */
bool CH32RVUsbAudioFunction::setEntity(const CH32RVUsbSetup &r, const uint8_t *data)
{
    if (r.bRequest != (_protocol == CH32RVUsbAudioProtocol::Uac2 ? UAC2_CUR : UAC1_SET_CUR)) {
        return false;
    }
    const uint8_t itf = (uint8_t)(r.wIndex & 0xff), entity = (uint8_t)(r.wIndex >> 8);
    const uint8_t channel = (uint8_t)(r.wValue & 0xff), selector = (uint8_t)(r.wValue >> 8);
    if (itf != _controlInterface) {
        return false;
    }
    if (_protocol == CH32RVUsbAudioProtocol::Uac2 && entity == CLOCK_ID) {
        if (channel != 0 || selector != SEL_SAMPLE_RATE || r.wLength != 4) {
            return false;
        }
        const uint32_t rate = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
                              ((uint32_t)data[3] << 24);
        return setRate(rate);
    }
    if (selector == SEL_MUTE && r.wLength == 1) {
        return applyControl(entity, SEL_MUTE, channel, data[0]);
    }
    if (selector == SEL_VOLUME && r.wLength == 2) {
        return applyControl(entity, SEL_VOLUME, channel, (int16_t)(uint16_t)(data[0] | (data[1] << 8)));
    }
    return false;
}

/* UAC1's sampling frequency control on a data endpoint: the clock's rate,
 * one of the stream's (the two streams share the clock). */
bool CH32RVUsbAudioFunction::getEndpoint(const CH32RVUsbSetup &r, uint16_t &length)
{
    if (_protocol != CH32RVUsbAudioProtocol::Uac1) {
        return false;
    }
    const uint8_t ep = (uint8_t)(r.wIndex & 0xff), selector = (uint8_t)(r.wValue >> 8);
    const Stream *s = nullptr;
    for (uint8_t i = 0; i < 2; i++) {
        if (stream(i ? CH32RVUsbAudioDirection::Capture : CH32RVUsbAudioDirection::Playback) &&
            _streams[i].dataEndpoint == ep) {
            s = &_streams[i];
        }
    }
    if (!s || selector != 1 || r.wLength != 3 || r.bRequest < UAC1_GET_CUR || r.bRequest > UAC1_GET_RES) {
        return false;
    }
    uint32_t rate = r.bRequest == UAC1_GET_RES ? 0 : _sampleRate;
    for (uint8_t i = 0; i < s->rateCount && (r.bRequest == UAC1_GET_MIN || r.bRequest == UAC1_GET_MAX); i++) {
        if (i == 0 || (r.bRequest == UAC1_GET_MIN ? s->rates[i] < rate : s->rates[i] > rate)) {
            rate = s->rates[i];
        }
    }
    _ctl[0] = (uint8_t)rate;
    _ctl[1] = (uint8_t)(rate >> 8);
    _ctl[2] = (uint8_t)(rate >> 16);
    length = 3;
    return true;
}

bool CH32RVUsbAudioFunction::setEndpoint(const CH32RVUsbSetup &r, const uint8_t *data)
{
    if (_protocol != CH32RVUsbAudioProtocol::Uac1) {
        return false;
    }
    const uint8_t ep = (uint8_t)(r.wIndex & 0xff), selector = (uint8_t)(r.wValue >> 8);
    const Stream *s = nullptr;
    for (uint8_t i = 0; i < 2; i++) {
        if (stream(i ? CH32RVUsbAudioDirection::Capture : CH32RVUsbAudioDirection::Playback) &&
            _streams[i].dataEndpoint == ep) {
            s = &_streams[i];
        }
    }
    if (!s || r.bRequest != UAC1_SET_CUR || selector != 1 || r.wLength != 3) {
        return false;
    }
    const uint32_t rate = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
    return setRate(rate);
}

bool CH32RVUsbAudioFunction::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &r)
{
    if ((r.bmRequestType & 0x60) != 0x20) {
        return false;
    }
    const bool endpoint = (r.bmRequestType & 0x1F) == 2;
    if (r.bmRequestType & 0x80) {                /* GET */
        if (stage != CH32RVUsbControlStage::Setup) {
            return true;
        }
        uint16_t n = 0;
        if (!(endpoint ? getEndpoint(r, n) : getEntity(r, n))) {
            return false;
        }
        return device().controlIn(_ctl, n);
    }
    if (stage == CH32RVUsbControlStage::Setup) {   /* SET: the value first */
        return r.wLength && r.wLength <= sizeof _ctl && device().controlOut(_ctl, r.wLength);
    }
    if (stage == CH32RVUsbControlStage::Data) {
        return endpoint ? setEndpoint(r, _ctl) : setEntity(r, _ctl);
    }
    return true;
}

/* SET_INTERFACE on a streaming interface: alt 1 opens the stream, 0 closes. */
bool CH32RVUsbAudioFunction::usbSetInterface(uint8_t itf, uint8_t alt)
{
    for (uint8_t i = 0; i < 2; i++) {
        Stream *s = stream(i ? CH32RVUsbAudioDirection::Capture : CH32RVUsbAudioDirection::Playback);
        if (!s || s->interfaceNumber != itf) {
            continue;
        }
        if (alt > 1) {
            return false;
        }
        if (s->alternate != alt) {
            s->alternate = alt;
            CH32RVUsbAudioEvent e;
            e.type = CH32RVUsbAudioEventType::StreamStateChanged;
            e.target = i ? CH32RVUsbAudioEventTarget::Capture : CH32RVUsbAudioEventTarget::Playback;
            e.alternateSetting = alt;
            e.enabled = alt != 0;
            pushEvent(e);
        }
        if (alt) {
            if (i == 0) {
                receivePacket();
                sendFeedback();
            } else {
                _txRemainder = 0;
                sendPacket();
            }
        }
        return true;
    }
    return alt == 0;
}
