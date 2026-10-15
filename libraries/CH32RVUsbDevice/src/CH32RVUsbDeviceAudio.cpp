/* USB Audio (UAC1 / UAC2): TinyUSB's audio_device with EspUsbDevice's
 * topology, controls and events (EspUsbAudio.cpp and its internal model,
 * control, request and event files, folded into one). The descriptor is in
 * CH32RVUsbDeviceAudioDescriptor.cpp.
 *
 * Data moves in the USB interrupt (TinyUSB's xfer_isr hook): the transfer
 * callbacks below run there and touch only the volatile counters and the
 * FIFOs, which read() / write() reach with the interrupt masked. Control
 * requests, and so every event, come from the USB task, i.e. the sketch's
 * own thread (after loop(), inside delay()). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

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

uint16_t audio_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room, uint8_t itf,
                          uint8_t *ep, uint8_t str, bool hs)
{
    return static_cast<const CH32RVUsbAudioFunction &>(self).writeDescriptor(out, room, itf, ep, str, hs);
}

#if CH32RV_TINYUSB_DEVICE
const usbd_class_driver_t audio_driver = {
    NULL, audiod_init, audiod_deinit, audiod_reset, audiod_open, audiod_control_xfer_cb, audiod_xfer_cb,
    audiod_xfer_isr, audiod_sof_isr,
};
#define AUDIO_DRIVER (&audio_driver)
#else
#define AUDIO_DRIVER nullptr
#endif

/* Not const: the interface count follows the streams the sketch adds (one
 * audio function per device, so one type object serves it). */
CH32RVUsbFunctionType audio_type = {AUDIO_DRIVER, audio_descriptor, 1, 1, nullptr, 0};

} // namespace

/* ---------------------------------------------------------- setup */
CH32RVUsbAudioFunction::CH32RVUsbAudioFunction(CH32RVUsbDevice &device, CH32RVUsbAudioProtocol protocol)
    : CH32RVUsbDeviceFunction(device, audio_type, CH32RV_USB_AUDIO_NAME), _protocol(protocol)
{
    if (attached() && !g_audio) {
        g_audio = this;
    }
}

uint16_t CH32RVUsbAudioFunction::endpointLimit(CH32RVUsbAudioDirection d, bool hs)
{
    const uint32_t buffer = d == CH32RVUsbAudioDirection::Playback ? CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX
                                                                   : CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX;
    const uint32_t bus = hs ? 1024u : 1023u;
    return (uint16_t)(buffer < bus ? buffer : bus);
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
    if (device().started() || s.hasFormat || !packetSize(format, d, false)) {
        return false;
    }
    const Stream &other = _streams[1 - slot(d)];
    if (other.hasFormat && other.format.sampleRate != format.sampleRate) {
        return false;
    }
    s.format = format;
    s.hasFormat = true;
    _sampleRate = format.sampleRate;
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
    return (uint8_t)((_playbackEnabled ? 1 : 0) +
                     (_playbackEnabled && _protocol == CH32RVUsbAudioProtocol::Uac2 ? 1 : 0) +
                     (_captureEnabled ? 1 : 0));
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
    if ((uint8_t)(head - _eventTail) >= 8) {
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

#if CH32RV_TINYUSB_DEVICE

/* ---------------------------------------------------------- data */
int CH32RVUsbAudioPlaybackStream::available() const
{
    return (int)tud_audio_available();
}

size_t CH32RVUsbAudioPlaybackStream::read(void *data, size_t length)
{
    if (!data || length == 0) {
        return 0;
    }
    /* The receive interrupt may move the read index (overflow repair). */
    const uint32_t irq = ch32rv_irq_save();
    const uint16_t n = tud_audio_read(data, (uint16_t)(length > 0xffffu ? 0xffffu : length));
    ch32rv_irq_restore(irq);
    return n;
}

bool CH32RVUsbAudioPlaybackStream::clearBuffer()
{
    const uint32_t irq = ch32rv_irq_save();
    const bool ok = tud_audio_clear_ep_out_ff();
    ch32rv_irq_restore(irq);
    return ok;
}

size_t CH32RVUsbAudioCaptureStream::write(const void *data, size_t length)
{
    if (!data || length == 0) {
        return 0;
    }
    const uint16_t requested = (uint16_t)(length > 0xffffu ? 0xffffu : length);
    const uint32_t irq = ch32rv_irq_save();
    tu_fifo_t *fifo = tud_audio_get_ep_in_ff();
    uint32_t overwritten = 0;
    if (fifo) {
        const uint16_t count = tu_fifo_count(fifo);
        const uint16_t remaining = (uint16_t)(fifo->depth - count);
        overwritten = requested >= fifo->depth ? (uint32_t)count + requested - fifo->depth
                                               : (requested > remaining ? (uint32_t)(requested - remaining) : 0u);
    }
    const uint16_t written = tud_audio_write(data, requested);
    if (overwritten) {
        _overrunCount = _overrunCount + 1;
        _overrunBytes = _overrunBytes + overwritten;
    }
    ch32rv_irq_restore(irq);
    return written;
}

int CH32RVUsbAudioCaptureStream::availableForWrite()
{
    const uint32_t irq = ch32rv_irq_save();
    tu_fifo_t *fifo = tud_audio_get_ep_in_ff();
    const int room = fifo ? (int)(fifo->depth - tu_fifo_count(fifo)) : 0;
    ch32rv_irq_restore(irq);
    return room;
}

bool CH32RVUsbAudioCaptureStream::clearBuffer()
{
    const uint32_t irq = ch32rv_irq_save();
    const bool ok = tud_audio_clear_ep_in_ff();
    ch32rv_irq_restore(irq);
    return ok;
}

/* In the USB interrupt: count, and when the host outran read() put the
 * FIFO's read index back on the oldest byte still there. */
bool CH32RVUsbAudioFunction::handlePlaybackTransfer(uint16_t bytes, uint8_t alternateSetting)
{
    if (alternateSetting == 0 || !_playbackEnabled) {
        return true;
    }
    _playback._transferredBytes = _playback._transferredBytes + bytes;
    tu_fifo_t *fifo = tud_audio_get_ep_out_ff();
    if (fifo) {
        const uint16_t count = tu_ff_overflow_count(fifo->depth, fifo->wr_idx, fifo->rd_idx);
        if (count > fifo->depth) {
            tu_fifo_correct_read_pointer(fifo);
            _playback._overrunCount = _playback._overrunCount + 1;
            _playback._overrunBytes = _playback._overrunBytes + (uint32_t)(count - fifo->depth);
        }
    }
    return true;
}

/* In the USB interrupt: a packet shorter than one (micro)frame's nominal
 * samples means write() fell behind. */
bool CH32RVUsbAudioFunction::handleCaptureTransfer(uint16_t bytes, uint8_t alternateSetting)
{
    if (alternateSetting == 0 || !_captureEnabled) {
        return true;
    }
    _capture._transferredBytes = _capture._transferredBytes + bytes;
    const CH32RVUsbAudioFormat &f = _streams[1].format;
    const uint32_t perSecond = f.sampleRate * f.channels * f.bytesPerSample;
    const uint32_t minimum = perSecond / (tud_speed_get() == TUSB_SPEED_HIGH ? 8000u : 1000u);
    if (bytes < minimum) {
        _capture._underrunCount = _capture._underrunCount + 1;
        _capture._underrunBytes = _capture._underrunBytes + (minimum - bytes);
    }
    return true;
}

/* ---------------------------------------------------------- requests */
static uint16_t clip(uint16_t n, uint16_t wLength)
{
    return n < wLength ? n : wLength;
}

/* GET on an entity: the UAC2 clock (rate, validity) or a feature unit
 * (mute, volume); UAC1 has only the feature units. wLength is the host's
 * buffer, so the answer is cut to it rather than refused. */
bool CH32RVUsbAudioFunction::handleGetEntityRequest(uint8_t rhport, const void *raw)
{
    const tusb_control_request_t *r = static_cast<const tusb_control_request_t *>(raw);
    if (!r || r->wLength == 0) {
        return false;
    }
    const uint8_t itf = (uint8_t)(r->wIndex & 0xff), entity = (uint8_t)(r->wIndex >> 8);
    const uint8_t channel = (uint8_t)(r->wValue & 0xff), selector = (uint8_t)(r->wValue >> 8);
    if (itf != _controlInterface) {
        return false;
    }
    uint8_t buf[16];
    uint16_t n = 0;
    if (_protocol == CH32RVUsbAudioProtocol::Uac2 && entity == CLOCK_ID) {
        if (channel != 0) {
            return false;
        }
        if (selector == SEL_SAMPLE_RATE && r->bRequest == UAC2_CUR) {
            put32(buf, _sampleRate);
            n = 4;
        } else if (selector == SEL_SAMPLE_RATE && r->bRequest == UAC2_RANGE) {
            /* One discrete rate; Linux first reads only wNumSubRanges. */
            put16(buf, 1);
            put32(buf + 2, _sampleRate);
            put32(buf + 6, _sampleRate);
            put32(buf + 10, 0);
            n = 14;
        } else if (selector == SEL_CLOCK_VALID && r->bRequest == UAC2_CUR) {
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
        const bool cur = r->bRequest == (_protocol == CH32RVUsbAudioProtocol::Uac2 ? UAC2_CUR : UAC1_GET_CUR);
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
            if (r->bRequest != UAC2_RANGE) {
                return false;
            }
            put16(buf, 1);
            put16(buf + 2, (uint16_t)(int16_t)VOLUME_MIN);
            put16(buf + 4, (uint16_t)(int16_t)VOLUME_MAX);
            put16(buf + 6, (uint16_t)(int16_t)VOLUME_RESOLUTION);
            n = 8;
        } else {
            const int32_t v = r->bRequest == UAC1_GET_MIN   ? VOLUME_MIN
                              : r->bRequest == UAC1_GET_MAX ? VOLUME_MAX
                              : r->bRequest == UAC1_GET_RES ? VOLUME_RESOLUTION
                                                            : 1;
            if (v == 1) {
                return false;
            }
            put16(buf, (uint16_t)(int16_t)v);
            n = 2;
        }
    }
    return tud_audio_buffer_and_schedule_control_xfer(rhport, r, buf, clip(n, r->wLength));
}

/* SET_CUR on an entity. The clock takes only the one rate it has. */
bool CH32RVUsbAudioFunction::handleSetEntityRequest(const void *raw, const uint8_t *data)
{
    const tusb_control_request_t *r = static_cast<const tusb_control_request_t *>(raw);
    if (!r || !data || r->bRequest != (_protocol == CH32RVUsbAudioProtocol::Uac2 ? UAC2_CUR : UAC1_SET_CUR)) {
        return false;
    }
    const uint8_t itf = (uint8_t)(r->wIndex & 0xff), entity = (uint8_t)(r->wIndex >> 8);
    const uint8_t channel = (uint8_t)(r->wValue & 0xff), selector = (uint8_t)(r->wValue >> 8);
    if (itf != _controlInterface) {
        return false;
    }
    if (_protocol == CH32RVUsbAudioProtocol::Uac2 && entity == CLOCK_ID) {
        if (channel != 0 || selector != SEL_SAMPLE_RATE || r->wLength != 4) {
            return false;
        }
        const uint32_t rate = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
                              ((uint32_t)data[3] << 24);
        return rate == _sampleRate;
    }
    if (selector == SEL_MUTE && r->wLength == 1) {
        return applyControl(entity, SEL_MUTE, channel, data[0]);
    }
    if (selector == SEL_VOLUME && r->wLength == 2) {
        return applyControl(entity, SEL_VOLUME, channel, (int16_t)(uint16_t)(data[0] | (data[1] << 8)));
    }
    return false;
}

/* UAC1's sampling frequency control on a data endpoint: one fixed rate. */
bool CH32RVUsbAudioFunction::handleGetEndpointRequest(uint8_t rhport, const void *raw)
{
    const tusb_control_request_t *r = static_cast<const tusb_control_request_t *>(raw);
    if (_protocol != CH32RVUsbAudioProtocol::Uac1 || !r) {
        return false;
    }
    const uint8_t ep = (uint8_t)(r->wIndex & 0xff), selector = (uint8_t)(r->wValue >> 8);
    const Stream *s = nullptr;
    for (uint8_t i = 0; i < 2; i++) {
        if (stream(i ? CH32RVUsbAudioDirection::Capture : CH32RVUsbAudioDirection::Playback) &&
            _streams[i].dataEndpoint == ep) {
            s = &_streams[i];
        }
    }
    if (!s || selector != 1 || r->wLength != 3 || r->bRequest < UAC1_GET_CUR || r->bRequest > UAC1_GET_RES) {
        return false;
    }
    const uint32_t rate = r->bRequest == UAC1_GET_RES ? 0 : s->format.sampleRate;
    uint8_t buf[3] = {(uint8_t)rate, (uint8_t)(rate >> 8), (uint8_t)(rate >> 16)};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, r, buf, sizeof buf);
}

bool CH32RVUsbAudioFunction::handleSetEndpointRequest(const void *raw, const uint8_t *data)
{
    const tusb_control_request_t *r = static_cast<const tusb_control_request_t *>(raw);
    if (_protocol != CH32RVUsbAudioProtocol::Uac1 || !r || !data) {
        return false;
    }
    const uint8_t ep = (uint8_t)(r->wIndex & 0xff), selector = (uint8_t)(r->wValue >> 8);
    const Stream *s = nullptr;
    for (uint8_t i = 0; i < 2; i++) {
        if (stream(i ? CH32RVUsbAudioDirection::Capture : CH32RVUsbAudioDirection::Playback) &&
            _streams[i].dataEndpoint == ep) {
            s = &_streams[i];
        }
    }
    if (!s || r->bRequest != UAC1_SET_CUR || selector != 1 || r->wLength != 3) {
        return false;
    }
    const uint32_t rate = (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
    return rate == s->format.sampleRate;
}

/* SET_INTERFACE on a streaming interface: alt 1 opens the stream, 0 closes. */
bool CH32RVUsbAudioFunction::handleSetInterface(const void *raw)
{
    const tusb_control_request_t *r = static_cast<const tusb_control_request_t *>(raw);
    if (!r) {
        return false;
    }
    const uint8_t itf = (uint8_t)(r->wIndex & 0xff), alt = (uint8_t)(r->wValue & 0xff);
    for (uint8_t i = 0; i < 2; i++) {
        Stream *s = stream(i ? CH32RVUsbAudioDirection::Capture : CH32RVUsbAudioDirection::Playback);
        if (!s || s->interfaceNumber != itf) {
            continue;
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
        break;
    }
    return true;
}

/* ---------------------------------------------------------- TinyUSB callbacks
 * Here, in the file the class object pulls in, so that they replace
 * audio_device.c's weak ones whenever the audio driver is linked. */
extern "C" bool tud_audio_get_req_entity_cb(uint8_t rhport, const tusb_control_request_t *request)
{
    return g_audio && g_audio->handleGetEntityRequest(rhport, request);
}

extern "C" bool tud_audio_set_req_entity_cb(uint8_t, const tusb_control_request_t *request, uint8_t *data)
{
    return g_audio && g_audio->handleSetEntityRequest(request, data);
}

extern "C" bool tud_audio_get_req_ep_cb(uint8_t rhport, const tusb_control_request_t *request)
{
    return g_audio && g_audio->handleGetEndpointRequest(rhport, request);
}

extern "C" bool tud_audio_set_req_ep_cb(uint8_t, const tusb_control_request_t *request, uint8_t *data)
{
    return g_audio && g_audio->handleSetEndpointRequest(request, data);
}

extern "C" bool tud_audio_get_req_itf_cb(uint8_t, const tusb_control_request_t *)
{
    return false;
}

extern "C" bool tud_audio_set_req_itf_cb(uint8_t, const tusb_control_request_t *, uint8_t *)
{
    return false;
}

extern "C" bool tud_audio_set_itf_cb(uint8_t, const tusb_control_request_t *request)
{
    return g_audio && g_audio->handleSetInterface(request);
}

extern "C" bool tud_audio_set_itf_close_ep_cb(uint8_t, const tusb_control_request_t *)
{
    return g_audio != nullptr;
}

extern "C" bool tud_audio_rx_done_isr(uint8_t, uint16_t bytes, uint8_t, uint8_t, uint8_t alternateSetting)
{
    return g_audio && g_audio->handlePlaybackTransfer(bytes, alternateSetting);
}

extern "C" bool tud_audio_tx_done_isr(uint8_t, uint16_t bytes, uint8_t, uint8_t, uint8_t alternateSetting)
{
    return g_audio && g_audio->handleCaptureTransfer(bytes, alternateSetting);
}

/* UAC2 playback is asynchronous: the feedback endpoint steers the host by
 * the receive FIFO's level, around half full. */
extern "C" void tud_audio_feedback_params_cb(uint8_t, uint8_t, audio_feedback_params_t *params)
{
    if (!params || !g_audio) {
        return;
    }
    params->method = AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
    params->sample_freq = g_audio->currentSampleRate();
}

#else /* !CH32RV_TINYUSB_DEVICE: begin() of the device fails, nothing moves */

int CH32RVUsbAudioPlaybackStream::available() const { return 0; }
size_t CH32RVUsbAudioPlaybackStream::read(void *, size_t) { return 0; }
bool CH32RVUsbAudioPlaybackStream::clearBuffer() { return false; }
size_t CH32RVUsbAudioCaptureStream::write(const void *, size_t) { return 0; }
int CH32RVUsbAudioCaptureStream::availableForWrite() { return 0; }
bool CH32RVUsbAudioCaptureStream::clearBuffer() { return false; }
bool CH32RVUsbAudioFunction::handlePlaybackTransfer(uint16_t, uint8_t) { return false; }
bool CH32RVUsbAudioFunction::handleCaptureTransfer(uint16_t, uint8_t) { return false; }
bool CH32RVUsbAudioFunction::handleGetEntityRequest(uint8_t, const void *) { return false; }
bool CH32RVUsbAudioFunction::handleSetEntityRequest(const void *, const uint8_t *) { return false; }
bool CH32RVUsbAudioFunction::handleGetEndpointRequest(uint8_t, const void *) { return false; }
bool CH32RVUsbAudioFunction::handleSetEndpointRequest(const void *, const uint8_t *) { return false; }
bool CH32RVUsbAudioFunction::handleSetInterface(const void *) { return false; }

#endif
