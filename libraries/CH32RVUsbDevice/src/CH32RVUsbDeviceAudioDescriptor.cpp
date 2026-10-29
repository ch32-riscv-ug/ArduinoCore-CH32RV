/* USB Audio: the configuration descriptor part, UAC1 and UAC2, for full and
 * high speed (EspUsbAudioDescriptor). Plain bytes, no TinyUSB, so the layout
 * is the same on every series. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

namespace {

constexpr uint8_t DESC_INTERFACE = 0x04;
constexpr uint8_t DESC_ENDPOINT = 0x05;
constexpr uint8_t DESC_INTERFACE_ASSOCIATION = 0x0b;
constexpr uint8_t DESC_CS_INTERFACE = 0x24;
constexpr uint8_t DESC_CS_ENDPOINT = 0x25;
constexpr uint8_t CLASS_AUDIO = 0x01;
constexpr uint8_t SUBCLASS_CONTROL = 0x01;
constexpr uint8_t SUBCLASS_STREAMING = 0x02;
constexpr uint8_t PROTOCOL_UAC1 = 0x00;
constexpr uint8_t PROTOCOL_UAC2 = 0x20;

constexpr uint8_t AC_HEADER = 0x01;
constexpr uint8_t AC_INPUT_TERMINAL = 0x02;
constexpr uint8_t AC_OUTPUT_TERMINAL = 0x03;
constexpr uint8_t AC_FEATURE_UNIT = 0x06;
constexpr uint8_t AC_CLOCK_SOURCE = 0x0a;
constexpr uint8_t AS_GENERAL = 0x01;
constexpr uint8_t AS_FORMAT_TYPE = 0x02;
constexpr uint8_t EP_GENERAL = 0x01;

constexpr uint16_t TERMINAL_USB_STREAMING = 0x0101;
constexpr uint16_t TERMINAL_MICROPHONE = 0x0201;
constexpr uint16_t TERMINAL_SPEAKER = 0x0301;

constexpr uint8_t CATEGORY_SPEAKER = 0x01;
constexpr uint8_t CATEGORY_MICROPHONE = 0x03;
constexpr uint8_t CATEGORY_HEADSET = 0x04;

/* Isochronous endpoint attributes: asynchronous / adaptive data, and the
 * UAC2 explicit-feedback endpoint (usage 01). */
constexpr uint8_t ISO_ASYNC = 0x05;
constexpr uint8_t ISO_ADAPTIVE = 0x09;
constexpr uint8_t ISO_FEEDBACK = 0x11;

typedef CH32RVUsbDescriptorWriter Out;

/* Front left + right for stereo, front centre for mono (EspUsbDevice). */
uint32_t channelConfig(uint8_t channels)
{
    return channels == 1 ? 0x00000004u : 0x00000003u;
}

void interface(Out &o, uint8_t number, uint8_t alt, uint8_t endpoints, uint8_t subclass, uint8_t protocol,
               uint8_t str)
{
    o.u8(9);
    o.u8(DESC_INTERFACE);
    o.u8(number);
    o.u8(alt);
    o.u8(endpoints);
    o.u8(CLASS_AUDIO);
    o.u8(subclass);
    o.u8(protocol);
    o.u8(str);
}

void endpoint(Out &o, uint8_t address, uint8_t attributes, uint16_t size, uint8_t interval)
{
    o.u8(7);
    o.u8(DESC_ENDPOINT);
    o.u8(address);
    o.u8(attributes);
    o.u16(size);
    o.u8(interval);
}

} // namespace

uint16_t CH32RVUsbAudioFunction::packetSize(const CH32RVUsbAudioFormat &f, CH32RVUsbAudioDirection d,
                                            bool hs) const
{
    /* UAC1 carries the rate in 3 bytes; both versions cap channels at the
     * two the descriptors here describe. */
    if (f.sampleRate == 0 || f.sampleRate > 0xffffffu || f.channels < 1 || f.channels > 2 ||
        f.bytesPerSample < 2 || f.bytesPerSample > 4 ||
        (f.bitsPerSample != 16 && f.bitsPerSample != 24 && f.bitsPerSample != 32) ||
        f.bitsPerSample > f.bytesPerSample * 8u) {
        return 0;
    }
    const uint32_t frames = hs ? 8000u : 1000u;
    const uint32_t frameBytes = (uint32_t)f.channels * f.bytesPerSample;
    const uint32_t nominal = (f.sampleRate + frames - 1u) / frames;
    (void)d;
    const uint32_t limit = endpointLimit(hs);
    if (nominal * frameBytes > limit) {
        return 0;
    }
    /* One sample frame of slack for the two clocks drifting, as far as the
     * endpoint allows: 32 kHz mono 16-bit is 64 bytes at full speed with or
     * without it. */
    uint32_t size = (nominal + 1u) * frameBytes;
    const uint32_t cap = limit - limit % frameBytes;
    return (uint16_t)(size < cap ? size : cap);
}

uint8_t CH32RVUsbAudioFunction::usbTerminalId(CH32RVUsbAudioDirection d) const
{
    if (d == CH32RVUsbAudioDirection::Playback) {
        return _playbackEnabled ? 2 : 0;
    }
    return _captureEnabled ? (uint8_t)(_playbackEnabled ? 7 : 4) : 0;
}

uint8_t CH32RVUsbAudioFunction::featureId(CH32RVUsbAudioDirection d) const
{
    if (d == CH32RVUsbAudioDirection::Playback) {
        return _playbackEnabled ? 3 : 0;
    }
    return _captureEnabled ? (uint8_t)(_playbackEnabled ? 6 : 3) : 0;
}

uint8_t CH32RVUsbAudioFunction::physicalTerminalId(CH32RVUsbAudioDirection d) const
{
    if (d == CH32RVUsbAudioDirection::Playback) {
        return _playbackEnabled ? 4 : 0;
    }
    return _captureEnabled ? (uint8_t)(_playbackEnabled ? 5 : 2) : 0;
}

/* Clock source 1 -> [playback] USB IT 2 -> FU 3 -> speaker OT 4,
 *                   [capture]  microphone IT -> FU -> USB OT,
 * an AC interface, then one AS interface per stream with alt 0 (no
 * endpoint) and alt 1 (the format). UAC1 has no clock entity; its ID stays
 * taken so the IDs match UAC2's. Playback takes endpoint n OUT (and n IN for
 * UAC2's feedback); capture takes the IN of the same n when it is free,
 * otherwise the next number. */
bool CH32RVUsbAudioFunction::writeDescriptor(CH32RVUsbDescriptorWriter &o, uint8_t itf, uint8_t *ep, uint8_t str,
                                             CH32RVUsbSpeed speed) const
{
    const bool hs = ch32rv_usb_series_can(CH32RVUsbSpeed::High) && speed != CH32RVUsbSpeed::Full;
    if (!describable()) {
        return false;
    }
    for (uint8_t i = 0; i < 2; i++) {
        uint8_t *buffer;
        uint16_t depth;
        if ((i == 0 ? _playbackEnabled : _captureEnabled) &&
            !ring(i, packetSize(widest(i), i == 0 ? CH32RVUsbAudioDirection::Playback
                                                  : CH32RVUsbAudioDirection::Capture, hs),
                  buffer, depth)) {
            return refuseSettings();
        }
    }
    const bool uac2 = _protocol == CH32RVUsbAudioProtocol::Uac2;
    const bool play = _playbackEnabled, cap = _captureEnabled;
    Stream &ps = _streams[0], &cs = _streams[1];

    _controlInterface = itf;
    uint8_t next = (uint8_t)(itf + 1);
    if (play) {
        const uint8_t n = (*ep)++;
        ps.interfaceNumber = next++;
        ps.dataEndpoint = n;
        ps.feedbackEndpoint = uac2 ? (uint8_t)(0x80 | n) : 0;
        if (cap) {
            cs.interfaceNumber = next++;
            cs.dataEndpoint = (uint8_t)(0x80 | (uac2 ? (*ep)++ : n));
            cs.feedbackEndpoint = 0;
        }
    } else {
        cs.interfaceNumber = next++;
        cs.dataEndpoint = (uint8_t)(0x80 | (*ep)++);
        cs.feedbackEndpoint = 0;
    }
    const uint8_t streams = (uint8_t)((play ? 1 : 0) + (cap ? 1 : 0));
    const uint8_t protocol = uac2 ? PROTOCOL_UAC2 : PROTOCOL_UAC1;

    /* The AC interface's class-specific part, for its header's wTotalLength. */
    uint16_t acLength = uac2 ? (uint16_t)(9 + 8) : (uint16_t)(8 + streams);
    for (uint8_t i = 0; i < 2; i++) {
        if (!(i == 0 ? play : cap)) {
            continue;
        }
        const uint8_t ch = _streams[i].format.channels;
        acLength = (uint16_t)(acLength + (uac2 ? 17 + 6 + (ch + 1) * 4 + 12 : 12 + 8 + ch + 9));
    }
    /* IAD and the AC interface. */
    o.u8(8);
    o.u8(DESC_INTERFACE_ASSOCIATION);
    o.u8(itf);
    o.u8((uint8_t)(1 + streams));
    o.u8(CLASS_AUDIO);
    o.u8(0);
    o.u8(protocol);
    o.u8(str);
    interface(o, itf, 0, 0, SUBCLASS_CONTROL, protocol, str);

    const CH32RVUsbAudioDirection P = CH32RVUsbAudioDirection::Playback, C = CH32RVUsbAudioDirection::Capture;
    if (uac2) {
        o.u8(9);
        o.u8(DESC_CS_INTERFACE);
        o.u8(AC_HEADER);
        o.u16(0x0200);
        o.u8(play && cap ? CATEGORY_HEADSET : play ? CATEGORY_SPEAKER : CATEGORY_MICROPHONE);
        o.u16(acLength);
        o.u8(0);
        /* Clock source 1: internal programmable; frequency read/write,
         * validity read-only. */
        o.u8(8);
        o.u8(DESC_CS_INTERFACE);
        o.u8(AC_CLOCK_SOURCE);
        o.u8(1);
        o.u8(0x03);
        o.u8(0x07);
        o.u8(0);
        o.u8(0);
    } else {
        o.u8((uint8_t)(8 + streams));
        o.u8(DESC_CS_INTERFACE);
        o.u8(AC_HEADER);
        o.u16(0x0100);
        o.u16(acLength);
        o.u8(streams);
        if (play) {
            o.u8(ps.interfaceNumber);
        }
        if (cap) {
            o.u8(cs.interfaceNumber);
        }
    }

    /* Terminals and feature units in ID order. */
    auto inputTerminal = [&](uint8_t id, uint16_t type, uint8_t channels) {
        if (uac2) {
            o.u8(17);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AC_INPUT_TERMINAL);
            o.u8(id);
            o.u16(type);
            o.u8(0);
            o.u8(1);                        /* bCSourceID */
            o.u8(channels);
            o.u32(channelConfig(channels));
            o.u8(0);
            o.u16(0);
            o.u8(0);
        } else {
            o.u8(12);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AC_INPUT_TERMINAL);
            o.u8(id);
            o.u16(type);
            o.u8(0);
            o.u8(channels);
            o.u16((uint16_t)channelConfig(channels));
            o.u8(0);
            o.u8(0);
        }
    };
    auto outputTerminal = [&](uint8_t id, uint16_t type, uint8_t source) {
        if (uac2) {
            o.u8(12);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AC_OUTPUT_TERMINAL);
            o.u8(id);
            o.u16(type);
            o.u8(0);
            o.u8(source);
            o.u8(1);                        /* bCSourceID */
            o.u16(0);
            o.u8(0);
        } else {
            o.u8(9);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AC_OUTPUT_TERMINAL);
            o.u8(id);
            o.u16(type);
            o.u8(0);
            o.u8(source);
            o.u8(0);
        }
    };
    /* Mute and volume on the master and on every channel. */
    auto featureUnit = [&](uint8_t id, uint8_t source, uint8_t channels) {
        if (uac2) {
            o.u8((uint8_t)(6 + (channels + 1) * 4));
            o.u8(DESC_CS_INTERFACE);
            o.u8(AC_FEATURE_UNIT);
            o.u8(id);
            o.u8(source);
            for (uint8_t c = 0; c <= channels; c++) {
                o.u32(0x0000000fu);         /* mute r/w, volume r/w */
            }
            o.u8(0);
        } else {
            o.u8((uint8_t)(7 + channels + 1));
            o.u8(DESC_CS_INTERFACE);
            o.u8(AC_FEATURE_UNIT);
            o.u8(id);
            o.u8(source);
            o.u8(1);                        /* bControlSize */
            for (uint8_t c = 0; c <= channels; c++) {
                o.u8(0x03);                 /* mute, volume */
            }
            o.u8(0);
        }
    };
    if (play) {
        const uint8_t ch = ps.format.channels;
        inputTerminal(usbTerminalId(P), TERMINAL_USB_STREAMING, ch);
        featureUnit(featureId(P), usbTerminalId(P), ch);
        outputTerminal(physicalTerminalId(P), TERMINAL_SPEAKER, featureId(P));
    }
    if (cap) {
        const uint8_t ch = cs.format.channels;
        inputTerminal(physicalTerminalId(C), TERMINAL_MICROPHONE, ch);
        featureUnit(featureId(C), physicalTerminalId(C), ch);
        outputTerminal(usbTerminalId(C), TERMINAL_USB_STREAMING, featureId(C));
    }

    /* One AS interface per stream. */
    for (uint8_t i = 0; i < 2; i++) {
        if (!(i == 0 ? play : cap)) {
            continue;
        }
        const Stream &s = _streams[i];
        const CH32RVUsbAudioDirection d = i == 0 ? P : C;
        const CH32RVUsbAudioFormat &f = s.format;
        const uint16_t size = packetSize(widest(i), d, hs);
        if (!size) {
            return false;
        }
        const uint8_t endpoints = (uint8_t)(1 + (s.feedbackEndpoint ? 1 : 0));
        interface(o, s.interfaceNumber, 0, 0, SUBCLASS_STREAMING, protocol, 0);
        interface(o, s.interfaceNumber, 1, endpoints, SUBCLASS_STREAMING, protocol, 0);
        if (uac2) {
            o.u8(16);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AS_GENERAL);
            o.u8(usbTerminalId(d));
            o.u8(0);                        /* bmControls */
            o.u8(0x01);                     /* format type I */
            o.u32(0x00000001u);             /* PCM */
            o.u8(f.channels);
            o.u32(channelConfig(f.channels));
            o.u8(0);
            o.u8(6);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AS_FORMAT_TYPE);
            o.u8(0x01);
            o.u8(f.bytesPerSample);
            o.u8(f.bitsPerSample);
            /* Data every (micro)frame; asynchronous, the device's clock. */
            endpoint(o, s.dataEndpoint, ISO_ASYNC, size, 1);
            o.u8(8);
            o.u8(DESC_CS_ENDPOINT);
            o.u8(EP_GENERAL);
            o.u8(0);
            o.u8(0);
            o.u8(0);
            o.u16(0);
            if (s.feedbackEndpoint) {
                /* 16.16 samples per (micro)frame, every 1 ms: bInterval 1
                 * frame at full speed, 2^(4-1) microframes at high speed. */
                endpoint(o, s.feedbackEndpoint, ISO_FEEDBACK, 4, hs ? 4 : 1);
            }
        } else {
            o.u8(7);
            o.u8(DESC_CS_INTERFACE);
            o.u8(AS_GENERAL);
            o.u8(usbTerminalId(d));
            o.u8(1);                        /* bDelay */
            o.u16(0x0001);                  /* PCM */
            o.u8((uint8_t)(8 + 3 * s.rateCount));
            o.u8(DESC_CS_INTERFACE);
            o.u8(AS_FORMAT_TYPE);
            o.u8(0x01);
            o.u8(f.channels);
            o.u8(f.bytesPerSample);
            o.u8(f.bitsPerSample);
            o.u8(s.rateCount);              /* discrete rates */
            for (uint8_t r = 0; r < s.rateCount; r++) {
                o.u24(s.rates[r]);
            }
            /* Playback adaptive (follows the host's frames), capture
             * asynchronous; no feedback endpoint. */
            o.u8(9);
            o.u8(DESC_ENDPOINT);
            o.u8(s.dataEndpoint);
            o.u8(i == 0 ? ISO_ADAPTIVE : ISO_ASYNC);
            o.u16(size);
            o.u8(1);
            o.u8(0);                        /* bRefresh */
            o.u8(0);                        /* bSynchAddress */
            o.u8(7);
            o.u8(DESC_CS_ENDPOINT);
            o.u8(EP_GENERAL);
            o.u8(0x01);                     /* sampling frequency control */
            o.u8(0);
            o.u16(0);
        }
    }
    return true;
}

bool CH32RVUsbAudioFunction::describable() const
{
    if (!_playbackEnabled && !_captureEnabled) {
        return false;
    }
    if (_playbackEnabled && !_streams[0].hasFormat) {
        return false;
    }
    if (_captureEnabled && !_streams[1].hasFormat) {
        return false;
    }
    /* One clock: a headset's two streams offer the same rates. */
    if (!(_playbackEnabled && _captureEnabled)) {
        return true;
    }
    if (_streams[0].rateCount != _streams[1].rateCount) {
        return false;
    }
    for (uint8_t i = 0; i < _streams[0].rateCount; i++) {
        bool found = false;
        for (uint8_t j = 0; j < _streams[1].rateCount; j++) {
            found = found || _streams[0].rates[i] == _streams[1].rates[j];
        }
        if (!found) {
            return false;
        }
    }
    return true;
}
