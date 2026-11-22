/* CH32RVUsbHost: USB Audio Class streaming (UAC1 and UAC2 PCM), in
 * EspUsbHost's calls. One input and one output stream at a time.
 *
 * getAudioStreams() walks the configuration: each Audio Streaming interface
 * (class 0x01, subclass 0x02) alternate setting with a Type I format and an
 * isochronous data endpoint is a stream. UAC1 lists its rates in the format
 * descriptor; UAC2 keeps them in the Clock Source its terminal names, asked
 * with GET RANGE of SAM_FREQ_CONTROL.
 *
 * A started stream has its alternate setting selected, its rate set (UAC1:
 * SET_CUR SAMPLING_FREQ_CONTROL on the endpoint; UAC2: SET_CUR on the clock -
 * a device that refuses keeps its own, and a stream of one rate is not asked),
 * and its isochronous endpoint kept busy: one packet per service interval
 * (the controllers' isochronous transfer is one interval's packet), the next
 * started from the completion. Input packets go into a ring that
 * onAudioData() empties; output packets are cut from a ring that audioSend()
 * fills, as many frames as the rate gives the interval (a fraction carried
 * over), silence when it runs dry. No feedback endpoint (asynchronous
 * devices are paced at the nominal rate), no Feature Unit controls yet. */
#include "internal/CH32RVUsbHostInternal.h"

#include <stdlib.h>
#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

constexpr uint8_t AUDIO_SET_CUR = 0x01;
constexpr uint8_t AUDIO2_RANGE = 0x02;

struct AudioStream {
    bool running, input;
    uint8_t daddr, itf, ep;
    uint16_t packet;               /* bytes one interval's transfer takes at most */
    uint8_t frameBytes;            /* channels x bytes per sample */
    uint32_t rate;
    uint32_t intervalUs;
    uint32_t carry;                /* the rate's fraction of a frame, carried */
    volatile bool busy;
    uint8_t *pkt, *ring;
    uint16_t head, tail, used;
    uint32_t underruns, overruns;
};
AudioStream streams[2];                 /* 0 input, 1 output */
uint8_t ctl[64] __attribute__((aligned(4)));

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le24(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16; }
uint32_t le32(const uint8_t *p) { return le24(p) | (uint32_t)p[3] << 24; }

void ring_put(AudioStream &s, const uint8_t *data, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        if (s.used >= CH32RV_USB_HOST_AUDIO_RING_SIZE) {
            s.overruns++;
            return;
        }
        s.ring[s.head] = data[i];
        s.head = (uint16_t)((s.head + 1u) % CH32RV_USB_HOST_AUDIO_RING_SIZE);
        s.used++;
    }
}

uint16_t ring_get(AudioStream &s, uint8_t *out, uint16_t n)
{
    const uint16_t take = n < s.used ? n : s.used;
    for (uint16_t i = 0; i < take; i++) {
        out[i] = s.ring[s.tail];
        s.tail = (uint16_t)((s.tail + 1u) % CH32RV_USB_HOST_AUDIO_RING_SIZE);
    }
    s.used = (uint16_t)(s.used - take);
    return take;
}

void arm(AudioStream &s);

void done(void *user, uint8_t result, uint16_t length)
{
    AudioStream &s = *(AudioStream *)user;
    s.busy = false;
    if (!s.running) {
        return;
    }
    if (s.input && result == CH32RV_USBH_OK && length) {
        ring_put(s, s.pkt, length);
    }
    arm(s);                        /* the next interval's packet, whatever this one did */
}

/* The next packet: an input packet's room, or the output's frames for one
 * interval from the ring (silence for what it lacks). */
void arm(AudioStream &s)
{
    if (!s.running || s.busy || !ch32rv_usb_host_connected(s.daddr)) {
        return;
    }
    uint16_t n = s.packet;
    if (!s.input) {
        const uint64_t acc = (uint64_t)s.rate * s.intervalUs + s.carry;
        uint32_t frames = (uint32_t)(acc / 1000000u);
        s.carry = (uint32_t)(acc % 1000000u);
        n = (uint16_t)(frames * s.frameBytes);
        if (n > s.packet) {
            n = (uint16_t)(s.packet / s.frameBytes * s.frameBytes);
        }
        const uint16_t got = ring_get(s, s.pkt, n);
        if (got < n) {
            memset(s.pkt + got, 0, n - got);
            s.underruns++;
        }
    }
    s.busy = true;
    if (!ch32rv_usb_host_xfer(s.daddr, s.ep, s.pkt, n, done, &s)) {
        s.busy = false;
    }
}

void poll(CH32RVUsbHost &host)
{
    (void)host;
    for (AudioStream &s : streams) {
        arm(s);                    /* a start that could not arm from the completion */
    }
}

size_t deliver(CH32RVUsbHost &host)
{
    AudioStream &s = streams[0];
    size_t ran = 0;
    uint8_t chunk[96];
    while (host._audioData && s.running && s.used) {
        const uint16_t n = ring_get(s, chunk, sizeof chunk);
        CH32RVUsbHostAudioData d;
        d.address = s.daddr;
        d.interfaceNumber = s.itf;
        d.data = chunk;
        d.length = n;
        host._audioData(d);
        ran++;
    }
    return ran;
}

void release(AudioStream &s)
{
    free(s.pkt);
    free(s.ring);
    memset(&s, 0, sizeof s);
}

void detach(uint8_t daddr)
{
    for (AudioStream &s : streams) {
        if (s.running && s.daddr == daddr) {
            release(s);
        }
    }
}

CH32RVUsbHostGroup audio_group = {nullptr, nullptr, poll, detach, deliver, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(audio_group); }
} join;

/* The Audio Control interface (the clock requests' wIndex) and, for UAC2,
 * the Clock Source each terminal names: found while walking. */
struct Terminal {
    uint8_t id, clock;
};

/* The isochronous data endpoint of (itf, alt), with its companion, into keep. */
bool find_endpoint(uint8_t daddr, uint8_t itf, uint8_t alt, uint8_t keep[13])
{
    CH32RVUsbHostConfigWalk w;
    bool here = false;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            if (here) {
                break;
            }
            here = d[2] == itf && d[3] == alt;
        } else if (here && d[1] == 5 && d[0] >= 7 && (d[3] & 3) == CH32RV_USBH_ISOCHRONOUS && ((d[3] >> 4) & 3) == 0) {
            memset(keep, 0, 13);
            memcpy(keep, d, 7);
            keep[0] = 7;
            const uint16_t room = ch32rv_usb_host_config_room(w);
            if (room >= d[0] + 6u && d[d[0]] >= 6 && d[d[0] + 1] == 0x30) {
                memcpy(keep + 7, d + d[0], 6);
            }
            return true;
        }
    }
    return false;
}

uint8_t control_interface(uint8_t daddr)
{
    CH32RVUsbHostConfigWalk w;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9 && d[5] == 0x01 && d[6] == 0x01) {
            return d[2];
        }
    }
    return 0xFF;
}

bool resolve_address(CH32RVUsbHost &host, uint8_t address, uint8_t &daddr)
{
    daddr = host.resolve(address);
    return daddr != 0;
}

bool rate_ok(const CH32RVUsbHostAudioStreamInfo &s, uint32_t rate)
{
    if (s.sampleRateCount == 0) {
        return rate >= s.sampleRateMin && rate <= s.sampleRateMax;
    }
    for (uint8_t i = 0; i < s.sampleRateCount && i < CH32RV_USB_HOST_MAX_AUDIO_SAMPLE_RATES; i++) {
        if (s.sampleRates[i] == rate) {
            return true;
        }
    }
    return false;
}

bool start(CH32RVUsbHost &host, AudioStream &s, bool input, const CH32RVUsbHostAudioStreamInfo &info, uint32_t rate)
{
    if (info.input != input || !info.startable || !info.channels || !info.bytesPerSample) {
        return host.fail(CH32RVUsbHostError::InvalidArgument);
    }
    if (!rate) {
        rate = info.sampleRate ? info.sampleRate : info.sampleRateMin;
    }
    if (!rate || !rate_ok(info, rate)) {
        return host.fail(CH32RVUsbHostError::InvalidArgument);
    }
    const uint8_t daddr = info.address;
    const CH32RVUsbHostDeviceInfo *dev = ch32rv_usb_host_device(daddr);
    uint8_t keep[13];
    if (!dev || !find_endpoint(daddr, info.interfaceNumber, info.alternate, keep)) {
        return host.fail(CH32RVUsbHostError::NotSupported);
    }
    if (s.running) {
        release(s);
    }
    if (!ch32rv_usb_host_control(host, daddr, 0x01, 0x0B, info.alternate, info.interfaceNumber, nullptr, 0, nullptr,
                                 1000)) {
        return false;
    }
    /* The rate: a stream of one rate is left as it is. */
    if (info.sampleRateCount != 1) {
        if (info.protocol == CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC2) {
            const uint8_t ac = control_interface(daddr);
            ctl[0] = (uint8_t)rate;
            ctl[1] = (uint8_t)(rate >> 8);
            ctl[2] = (uint8_t)(rate >> 16);
            ctl[3] = (uint8_t)(rate >> 24);
            ch32rv_usb_host_control(host, daddr, 0x21, AUDIO_SET_CUR, 0x0100,
                                    (uint16_t)(info.clockSourceId << 8 | ac), ctl, 4, nullptr, 1000);
        } else {
            ctl[0] = (uint8_t)rate;
            ctl[1] = (uint8_t)(rate >> 8);
            ctl[2] = (uint8_t)(rate >> 16);
            ch32rv_usb_host_control(host, daddr, 0x22, AUDIO_SET_CUR, 0x0100, info.endpointAddress, ctl, 3, nullptr,
                                    1000);
        }
    }
    if (!ch32rv_usb_host_ep_open(daddr, keep)) {
        return host.fail(CH32RVUsbHostError::TransferFailed);
    }
    const uint16_t wmps = le16(keep + 4);
    const uint16_t mps = (uint16_t)(wmps & 0x7FF);
    const uint8_t mult = (uint8_t)(((wmps >> 11) & 3) + 1);
    const uint8_t b = keep[6] ? keep[6] : 1;
    const bool fast = dev->speed >= CH32RV_USB_HOST_SPEED_HIGH;
    memset(&s, 0, sizeof s);
    s.pkt = (uint8_t *)malloc((size_t)mps * mult);
    s.ring = (uint8_t *)malloc(CH32RV_USB_HOST_AUDIO_RING_SIZE);
    if (!s.pkt || !s.ring) {
        free(s.pkt);
        free(s.ring);
        memset(&s, 0, sizeof s);
        ch32rv_usb_host_ep_close(daddr, keep[2]);
        return host.fail(CH32RVUsbHostError::NoMemory);
    }
    s.input = input;
    s.daddr = daddr;
    s.itf = info.interfaceNumber;
    s.ep = keep[2];
    s.packet = (uint16_t)(mps * mult);
    s.frameBytes = (uint8_t)(info.channels * info.bytesPerSample);
    s.rate = rate;
    s.intervalUs = (fast ? 125u : 1000u) << (b > 16 ? 15 : b - 1);
    s.running = true;
    ch32rv_usb_host_mark_claimed(daddr, info.interfaceNumber);
    arm(s);
    return host.ok();
}

void stop(AudioStream &s)
{
    if (!s.running) {
        return;
    }
    const uint8_t daddr = s.daddr, itf = s.itf, ep = s.ep;
    s.running = false;
    if (ch32rv_usb_host_connected(daddr)) {
        ch32rv_usb_host_abort(daddr, ep);
        ch32rv_usb_host_ep_close(daddr, ep);
        ch32rv_usb_host_control_sync(daddr, 0x01, 0x0B, 0, itf, nullptr, 0, nullptr, 1000);
    }
    release(s);
}

bool matches(const AudioStream &s, uint8_t address)
{
    return s.running && (address == CH32RV_USB_HOST_ANY_ADDRESS || s.daddr == address);
}

}  // namespace

size_t CH32RVUsbHost::getAudioStreams(uint8_t address, CH32RVUsbHostAudioStreamInfo *streams_out, size_t maxStreams)
{
    uint8_t daddr;
    if (!_started || !resolve_address(*this, address, daddr)) {
        fail(CH32RVUsbHostError::NotConnected);
        return 0;
    }
    Terminal terms[8];
    uint8_t nterms = 0, ac = 0xFF;
    size_t count = 0;
    CH32RVUsbHostAudioStreamInfo cur;
    bool inAs = false, haveFormat = false;
    auto finish = [&]() {
        if (inAs && haveFormat && cur.endpointAddress) {
            if (streams_out && count < maxStreams) {
                streams_out[count] = cur;
            }
            count++;
        }
        haveFormat = false;
    };
    CH32RVUsbHostConfigWalk w;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            finish();
            inAs = false;
            if (d[5] == 0x01 && d[6] == 0x01 && ac == 0xFF) {
                ac = d[2];
            }
            if (d[5] == 0x01 && d[6] == 0x02 && d[3] != 0) {
                cur = CH32RVUsbHostAudioStreamInfo();
                cur.address = daddr;
                cur.interfaceNumber = d[2];
                cur.alternate = d[3];
                cur.protocol = d[7];
                inAs = true;
            }
            continue;
        }
        if (d[1] == 0x24 && d[0] >= 3 && !inAs) {
            /* Audio Control (UAC2): the terminals and the clocks they name. */
            if (nterms < 8 && d[2] == 0x02 && d[0] >= 17) {          /* INPUT_TERMINAL (UAC2: 17 bytes) */
                terms[nterms++] = {d[3], d[7]};
            } else if (nterms < 8 && d[2] == 0x03 && d[0] >= 12) {   /* OUTPUT_TERMINAL (UAC2: 12 bytes) */
                terms[nterms++] = {d[3], d[8]};
            }
            continue;
        }
        if (!inAs) {
            continue;
        }
        const bool uac2 = cur.protocol == CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC2;
        if (d[1] == 0x24 && d[2] == 0x01 && d[0] >= 4) {             /* AS_GENERAL */
            cur.terminalLink = d[3];
            if (uac2 && d[0] >= 11) {
                cur.channels = d[10];
            }
        } else if (d[1] == 0x24 && d[2] == 0x02 && d[0] >= 6 && d[3] == 0x01) {   /* FORMAT_TYPE I */
            if (uac2) {
                cur.bytesPerSample = d[4];
                cur.bitsPerSample = d[5];
                haveFormat = true;
            } else if (d[0] >= 8) {
                cur.channels = d[4];
                cur.bytesPerSample = d[5];
                cur.bitsPerSample = d[6];
                const uint8_t n = d[7];
                if (n == 0 && d[0] >= 14) {
                    cur.sampleRateCount = 0;
                    cur.sampleRateMin = le24(d + 8);
                    cur.sampleRateMax = le24(d + 11);
                    cur.sampleRate = cur.sampleRateMin;
                } else {
                    for (uint8_t i = 0; i < n && 8u + 3u * i + 3u <= d[0]; i++) {
                        const uint32_t r = le24(d + 8 + 3 * i);
                        if (cur.sampleRateCount < CH32RV_USB_HOST_MAX_AUDIO_SAMPLE_RATES) {
                            cur.sampleRates[cur.sampleRateCount++] = r;
                        }
                        cur.sampleRateMin = cur.sampleRateMin && cur.sampleRateMin < r ? cur.sampleRateMin : r;
                        cur.sampleRateMax = cur.sampleRateMax > r ? cur.sampleRateMax : r;
                    }
                    cur.sampleRate = cur.sampleRates[0];
                }
                haveFormat = true;
            }
        } else if (d[1] == 5 && d[0] >= 7 && (d[3] & 3) == CH32RV_USBH_ISOCHRONOUS && ((d[3] >> 4) & 3) == 0 &&
                   !cur.endpointAddress) {
            cur.endpointAddress = d[2];
            cur.input = d[2] & 0x80;
            cur.output = !cur.input;
            cur.maxPacketSize = (uint16_t)(le16(d + 4) & 0x7FF);
            cur.interval = d[6];
        }
    }
    finish();
    /* UAC2: the rates of each stream's clock (GET RANGE of SAM_FREQ_CONTROL). */
    for (size_t i = 0; streams_out && i < count && i < maxStreams; i++) {
        CH32RVUsbHostAudioStreamInfo &s = streams_out[i];
        if (s.protocol != CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC2) {
            continue;
        }
        for (uint8_t t = 0; t < nterms; t++) {
            if (terms[t].id == s.terminalLink) {
                s.clockSourceId = terms[t].clock;
            }
        }
        size_t got = 0;
        if (!s.clockSourceId || ac == 0xFF ||
            !ch32rv_usb_host_control(*this, daddr, 0xA1, AUDIO2_RANGE, 0x0100, (uint16_t)(s.clockSourceId << 8 | ac),
                                     ctl, sizeof ctl, &got, 1000) ||
            got < 14) {
            s.startable = false;
            continue;
        }
        const uint16_t ranges = le16(ctl);
        for (uint16_t r = 0; r < ranges && 2u + 12u * r + 12u <= got; r++) {
            const uint32_t lo = le32(ctl + 2 + 12 * r), hi = le32(ctl + 6 + 12 * r), res = le32(ctl + 10 + 12 * r);
            if (lo == hi && s.sampleRateCount < CH32RV_USB_HOST_MAX_AUDIO_SAMPLE_RATES) {
                s.sampleRates[s.sampleRateCount++] = lo;
            } else {
                s.sampleRateResolution = res;
            }
            s.sampleRateMin = s.sampleRateMin && s.sampleRateMin < lo ? s.sampleRateMin : lo;
            s.sampleRateMax = s.sampleRateMax > hi ? s.sampleRateMax : hi;
        }
        if (s.sampleRateResolution) {
            s.sampleRateCount = 0;        /* a continuous range among them: min .. max */
        }
        s.sampleRate = s.sampleRateCount ? s.sampleRates[0] : s.sampleRateMin;
    }
    ok();
    return count;
}

bool CH32RVUsbHost::audioInputStart(const CH32RVUsbHostAudioStreamInfo &stream, uint32_t sampleRate, uint8_t address)
{
    if (address != CH32RV_USB_HOST_ANY_ADDRESS && address != stream.address) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    return start(*this, streams[0], true, stream, sampleRate);
}

bool CH32RVUsbHost::audioOutputStart(const CH32RVUsbHostAudioStreamInfo &stream, uint32_t sampleRate, uint8_t address)
{
    if (address != CH32RV_USB_HOST_ANY_ADDRESS && address != stream.address) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    return start(*this, streams[1], false, stream, sampleRate);
}

/* The first stream of that direction and format that has the rate. */
static bool pick(CH32RVUsbHost &host, bool input, uint8_t channels, uint8_t bits, uint32_t rate, uint8_t address,
                 CH32RVUsbHostAudioStreamInfo &out)
{
    CH32RVUsbHostAudioStreamInfo list[8];
    const size_t n = host.getAudioStreams(address, list, 8);
    for (size_t i = 0; i < n && i < 8; i++) {
        const CH32RVUsbHostAudioStreamInfo &s = list[i];
        if (s.input == input && s.channels == channels && s.bitsPerSample == bits && s.startable && rate_ok(s, rate)) {
            out = s;
            return true;
        }
    }
    return false;
}

bool CH32RVUsbHost::audioInputStart(uint8_t channels, uint8_t bitsPerSample, uint32_t sampleRate, uint8_t address)
{
    CH32RVUsbHostAudioStreamInfo s;
    if (!pick(*this, true, channels, bitsPerSample, sampleRate, address, s)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return start(*this, streams[0], true, s, sampleRate);
}

bool CH32RVUsbHost::audioOutputStart(uint8_t channels, uint8_t bitsPerSample, uint32_t sampleRate, uint8_t address)
{
    CH32RVUsbHostAudioStreamInfo s;
    if (!pick(*this, false, channels, bitsPerSample, sampleRate, address, s)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return start(*this, streams[1], false, s, sampleRate);
}

bool CH32RVUsbHost::audioInputStop(uint8_t address)
{
    if (!matches(streams[0], address)) {
        return false;
    }
    stop(streams[0]);
    return true;
}

void CH32RVUsbHost::audioOutputStop(uint8_t address)
{
    if (matches(streams[1], address)) {
        stop(streams[1]);
    }
}

bool CH32RVUsbHost::audioInputRunning(uint8_t address) const { return matches(streams[0], address); }
bool CH32RVUsbHost::audioOutputRunning(uint8_t address) const { return matches(streams[1], address); }

uint32_t CH32RVUsbHost::audioOutputUnderruns(uint8_t address) const
{
    return matches(streams[1], address) ? streams[1].underruns : 0;
}

bool CH32RVUsbHost::audioSend(const uint8_t *data, size_t length, uint8_t address)
{
    AudioStream &s = streams[1];
    if (!matches(s, address)) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    if (!data || length % s.frameBytes) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    if (length > (size_t)(CH32RV_USB_HOST_AUDIO_RING_SIZE - s.used)) {
        return fail(CH32RVUsbHostError::NoMemory);
    }
    ring_put(s, data, (uint16_t)length);
    return ok();
}

size_t CH32RVUsbHost::audioOutputAvailableForWrite(uint8_t address) const
{
    const AudioStream &s = streams[1];
    return matches(s, address) ? (size_t)(CH32RV_USB_HOST_AUDIO_RING_SIZE - s.used) : 0;
}

#else /* no host */

size_t CH32RVUsbHost::getAudioStreams(uint8_t, CH32RVUsbHostAudioStreamInfo *, size_t) { return 0; }
bool CH32RVUsbHost::audioInputStart(const CH32RVUsbHostAudioStreamInfo &, uint32_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::audioInputStart(uint8_t, uint8_t, uint32_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::audioInputStop(uint8_t) { return false; }
bool CH32RVUsbHost::audioInputRunning(uint8_t) const { return false; }
bool CH32RVUsbHost::audioOutputStart(const CH32RVUsbHostAudioStreamInfo &, uint32_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::audioOutputStart(uint8_t, uint8_t, uint32_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
void CH32RVUsbHost::audioOutputStop(uint8_t) {}
bool CH32RVUsbHost::audioOutputRunning(uint8_t) const { return false; }
uint32_t CH32RVUsbHost::audioOutputUnderruns(uint8_t) const { return 0; }
bool CH32RVUsbHost::audioSend(const uint8_t *, size_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
size_t CH32RVUsbHost::audioOutputAvailableForWrite(uint8_t) const { return 0; }

#endif
