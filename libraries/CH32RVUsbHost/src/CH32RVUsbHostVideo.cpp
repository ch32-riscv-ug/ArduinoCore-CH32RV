/* CH32RVUsbHost: USB Video Class cameras, in EspUsbHost's calls. One camera at
 * a time.
 *
 * getVideoStreams() reads the VideoStreaming interface (class 0x0E, subclass
 * 0x02): every Format (uncompressed - YUY2, NV12 by their GUID - and MJPEG) and
 * each of its Frames is a stream; the interface's alternate settings give the
 * bandwidth (isochronous: bytes per (micro)frame x transactions), or its one
 * bulk endpoint on alternate 0.
 *
 * videoStart() runs UVC's sequence: SET_CUR / GET_CUR on the Probe control to
 * learn what the camera will send, SET_CUR on Commit, then the narrowest
 * alternate setting that carries the committed dwMaxPayloadTransferSize. The
 * IN endpoint is kept busy with one payload per transfer (an isochronous
 * packet, or a bulk transfer of the committed payload size). Each payload's
 * header gives the frame ID, end of frame and error bits: the bytes after it
 * go into the filling buffer, and a frame ends at end-of-frame or when the
 * frame ID toggles. A finished frame waits in the second buffer for
 * onVideoFrame() (deliverEvents()). */
#include "internal/CH32RVUsbHostInternal.h"

#include <stdlib.h>
#include <string.h>

#if CH32RV_USB_HOST_STACK

namespace {

constexpr uint8_t VS_PROBE = 0x01, VS_COMMIT = 0x02;
constexpr uint8_t UVC_SET_CUR = 0x01, UVC_GET_CUR = 0x81;
constexpr uint16_t PROBE_LENGTH = 26;   /* UVC 1.0's; 1.1 adds 8 a camera may also send */
constexpr uint8_t MAX_ALTS = 12;

struct Video {
    bool running, iso, filling;
    uint8_t daddr, itf, ep;
    uint16_t width, height;
    uint8_t format;
    uint32_t payload;              /* bytes one transfer takes */
    uint32_t bufSize;
    uint8_t *pkt;
    uint8_t *buf[2];               /* [fill] fills; the other holds a finished frame */
    uint8_t fill;
    uint32_t length;               /* bytes in the filling buffer */
    bool fid, fidValid, bad;
    bool readyFull;                /* the other buffer holds a frame not yet delivered */
    uint32_t readyLength, readySequence, readyPts;
    bool readyComplete, readyHasPts;
    bool pts;
    uint32_t ptsValue;
    uint32_t sequence;
    volatile bool busy;
    CH32RVUsbHostVideoStats stats;
    CH32RVUsbHostVideoProbeControl committed;
};
Video video;
uint8_t ctl[48] __attribute__((aligned(4)));

uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

/* The filling frame is done (complete or not): to the second buffer when it
 * is free, else dropped. */
void finish_frame(bool complete)
{
    Video &v = video;
    if (!v.length) {
        v.bad = false;
        return;
    }
    if (v.readyFull) {
        v.stats.framesDropped++;
    } else {
        v.readyFull = true;
        v.readyLength = v.length;
        v.readyComplete = complete && !v.bad;
        v.readySequence = v.sequence++;
        v.readyHasPts = v.pts;
        v.readyPts = v.ptsValue;
        v.fill ^= 1u;
        v.stats.frames++;
        if (!v.readyComplete) {
            v.stats.framesIncomplete++;
        }
    }
    v.length = 0;
    v.bad = false;
    v.pts = false;
}

void take_payload(const uint8_t *p, uint16_t n)
{
    Video &v = video;
    if (n == 0) {
        return;                    /* an empty packet: nothing this interval */
    }
    if (n < 2 || p[0] < 2 || p[0] > n || !(p[1] & 0x80)) {
        v.stats.headerErrors++;
        return;
    }
    v.stats.payloads++;
    const uint8_t info = p[1];
    const bool fid = info & 0x01;
    if (v.fidValid && fid != v.fid) {
        finish_frame(false);       /* the frame ID toggled: the last one lost its end */
    }
    v.fid = fid;
    v.fidValid = true;
    if (info & 0x40) {
        v.stats.payloadErrors++;
        v.bad = true;
    }
    if ((info & 0x04) && p[0] >= 6) {
        v.pts = true;
        v.ptsValue = le32(p + 2);
    }
    const uint16_t data = (uint16_t)(n - p[0]);
    uint8_t *dst = v.buf[v.fill];
    if (v.length + data > v.bufSize) {
        v.stats.overflows++;
        v.bad = true;
        const uint32_t room = v.bufSize - v.length;
        memcpy(dst + v.length, p + p[0], room);
        v.length = v.bufSize;
    } else {
        memcpy(dst + v.length, p + p[0], data);
        v.length += data;
    }
    v.stats.bytes += data;
    if (info & 0x02) {
        finish_frame(true);        /* end of frame */
        v.fidValid = false;        /* the next frame's ID is whatever it is */
    }
}

void arm();

void done(void *user, uint8_t result, uint16_t length)
{
    (void)user;
    video.busy = false;
    if (!video.running) {
        return;
    }
    if (result == CH32RV_USBH_OK) {
        take_payload(video.pkt, length);
    } else {
        video.stats.packetErrors++;
    }
    arm();
}

void arm()
{
    Video &v = video;
    if (!v.running || v.busy || !ch32rv_usb_host_connected(v.daddr)) {
        return;
    }
    v.busy = true;
    if (!ch32rv_usb_host_xfer(v.daddr, v.ep, v.pkt, (uint16_t)(v.payload > 0xFFFF ? 0xFFFF : v.payload), done,
                              nullptr)) {
        v.busy = false;
    }
}

void poll(CH32RVUsbHost &host)
{
    (void)host;
    arm();
}

size_t deliver(CH32RVUsbHost &host)
{
    Video &v = video;
    if (!host._videoFrame || !v.running || !v.readyFull) {
        return 0;
    }
    CH32RVUsbHostVideoFrame f;
    f.address = v.daddr;
    f.interfaceNumber = v.itf;
    f.data = v.buf[v.fill ^ 1u];
    f.length = v.readyLength;
    f.width = v.width;
    f.height = v.height;
    f.format = v.format;
    f.sequence = v.readySequence;
    f.complete = v.readyComplete;
    f.hasPresentationTime = v.readyHasPts;
    f.presentationTime = v.readyPts;
    host._videoFrame(f);
    v.readyFull = false;
    return 1;
}

void release()
{
    free(video.pkt);
    free(video.buf[0]);
    free(video.buf[1]);
    memset(&video, 0, sizeof video);
}

void detach(uint8_t daddr)
{
    if (video.running && video.daddr == daddr) {
        release();
    }
}

CH32RVUsbHostGroup video_group = {nullptr, nullptr, poll, detach, deliver, nullptr};

struct Join {
    Join() { ch32rv_usb_host_join(video_group); }
} join;

/* An alternate setting of the streaming interface and its IN endpoint. */
struct Alt {
    uint8_t alt;
    uint32_t payload;
    uint8_t keep[13];
};

uint8_t guid_format(const uint8_t *g)
{
    if (!memcmp(g, "YUY2", 4)) {
        return CH32RV_USB_HOST_VIDEO_FORMAT_YUY2;
    }
    if (!memcmp(g, "NV12", 4)) {
        return CH32RV_USB_HOST_VIDEO_FORMAT_NV12;
    }
    return CH32RV_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED;
}

/* The streaming interface's alternates (isochronous) or its bulk endpoint
 * (alternate 0), with each endpoint descriptor and its companion. */
uint8_t alternates(uint8_t daddr, uint8_t itf, Alt *alts, bool &iso)
{
    uint8_t n = 0;
    uint8_t curAlt = 0;
    bool here = false;
    iso = true;
    CH32RVUsbHostConfigWalk w;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            here = d[2] == itf;
            curAlt = d[3];
            continue;
        }
        if (!here || d[1] != 5 || d[0] < 7 || !(d[2] & 0x80) || n >= MAX_ALTS) {
            continue;
        }
        const uint8_t type = d[3] & 3;
        if (type != CH32RV_USBH_ISOCHRONOUS && type != CH32RV_USBH_BULK) {
            continue;
        }
        Alt &a = alts[n++];
        a.alt = curAlt;
        const uint16_t w16 = le16(d + 4);
        a.payload = (uint32_t)(w16 & 0x7FF) * (((w16 >> 11) & 3) + 1u);
        memset(a.keep, 0, sizeof a.keep);
        memcpy(a.keep, d, 7);
        a.keep[0] = 7;
        const uint16_t room = ch32rv_usb_host_config_room(w);
        if (room >= d[0] + 6u && d[d[0]] >= 6 && d[d[0] + 1] == 0x30) {
            memcpy(a.keep + 7, d + d[0], 6);
            /* SuperSpeed isochronous: (bMaxBurst + 1) x (Mult + 1) packets. */
            a.payload = (uint32_t)(w16 & 0x7FF) * (d[d[0] + 2] + 1u) * ((d[d[0] + 3] & 3) + 1u);
        }
        if (type == CH32RV_USBH_BULK) {
            iso = false;
        }
    }
    return n;
}

bool matches(uint8_t address)
{
    return video.running && (address == CH32RV_USB_HOST_ANY_ADDRESS || video.daddr == address);
}

}  // namespace

size_t CH32RVUsbHost::getVideoStreams(uint8_t address, CH32RVUsbHostVideoStreamInfo *streams, size_t maxStreams)
{
    const uint8_t daddr = resolve(address);
    if (!_started || !daddr) {
        fail(CH32RVUsbHostError::NotConnected);
        return 0;
    }
    size_t count = 0;
    bool inVs = false;
    uint8_t itf = 0, format = 0, formatIndex = 0, frameCount = 0, defaultFrame = 0, bpp = 0;
    CH32RVUsbHostConfigWalk w;
    for (const uint8_t *d = ch32rv_usb_host_config_first(w, daddr); d; d = ch32rv_usb_host_config_next(w)) {
        if (d[1] == 4 && d[0] >= 9) {
            inVs = d[5] == 0x0E && d[6] == 0x02 && d[3] == 0;
            if (inVs) {
                itf = d[2];
            }
            continue;
        }
        if (!inVs || d[1] != 0x24 || d[0] < 4) {
            continue;
        }
        const uint8_t sub = d[2];
        if (sub == 0x04 && d[0] >= 27) {                  /* VS_FORMAT_UNCOMPRESSED */
            format = guid_format(d + 5);
            formatIndex = d[3];
            frameCount = d[4];
            bpp = d[21];
            defaultFrame = d[22];
        } else if (sub == 0x06 && d[0] >= 11) {           /* VS_FORMAT_MJPEG */
            format = CH32RV_USB_HOST_VIDEO_FORMAT_MJPEG;
            formatIndex = d[3];
            frameCount = d[4];
            bpp = 0;
            defaultFrame = d[6];
        } else if ((sub == 0x05 || sub == 0x07) && d[0] >= 26 && formatIndex) {   /* a Frame */
            CH32RVUsbHostVideoStreamInfo s;
            s.address = daddr;
            s.interfaceNumber = itf;
            s.format = format;
            s.formatIndex = formatIndex;
            s.frameIndex = d[3];
            s.frameCount = frameCount;
            s.defaultFrameIndex = defaultFrame;
            s.width = le16(d + 5);
            s.height = le16(d + 7);
            s.bitsPerPixel = bpp;
            s.maxVideoFrameBufferSize = le32(d + 17);
            s.frameInterval = le32(d + 21);
            const uint8_t type = d[25];
            if (type == 0 && d[0] >= 38) {
                s.frameIntervalMin = le32(d + 26);
                s.frameIntervalMax = le32(d + 30);
                s.frameIntervalStep = le32(d + 34);
            } else {
                for (uint8_t i = 0; i < type && 26u + 4u * i + 4u <= d[0]; i++) {
                    if (s.frameIntervalCount < CH32RV_USB_HOST_MAX_VIDEO_FRAME_INTERVALS) {
                        s.frameIntervals[s.frameIntervalCount++] = le32(d + 26 + 4 * i);
                    }
                }
            }
            if (streams && count < maxStreams) {
                streams[count] = s;
            }
            count++;
        }
    }
    /* The bandwidth: the widest alternate of each interface. */
    for (size_t i = 0; streams && i < count && i < maxStreams; i++) {
        Alt alts[MAX_ALTS];
        bool iso;
        const uint8_t n = alternates(daddr, streams[i].interfaceNumber, alts, iso);
        streams[i].isochronous = iso;
        streams[i].startable = n > 0;
        for (uint8_t a = 0; a < n; a++) {
            if (alts[a].payload > streams[i].maxPayloadSize) {
                streams[i].maxPayloadSize = alts[a].payload;
                streams[i].alternate = alts[a].alt;
                streams[i].endpointAddress = alts[a].keep[2];
                streams[i].interval = alts[a].keep[6];
            }
        }
    }
    ok();
    return count;
}

bool CH32RVUsbHost::videoStart(const CH32RVUsbHostVideoStreamInfo &stream, uint32_t fps, uint8_t address)
{
    const uint8_t daddr = stream.address;
    if (!_started || (address != CH32RV_USB_HOST_ANY_ADDRESS && address != daddr) || !ch32rv_usb_host_device(daddr)) {
        return fail(CH32RVUsbHostError::NotConnected);
    }
    if (!stream.startable || !stream.formatIndex || !stream.frameIndex) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    videoStop(CH32RV_USB_HOST_ANY_ADDRESS);
    const uint8_t itf = stream.interfaceNumber;
    const uint32_t interval = fps ? 10000000u / fps : stream.frameInterval;
    /* Probe: what is asked, then what the camera will do. */
    memset(ctl, 0, sizeof ctl);
    ctl[0] = 0x01;                 /* bmHint: dwFrameInterval fixed */
    ctl[2] = stream.formatIndex;
    ctl[3] = stream.frameIndex;
    put32(ctl + 4, interval);
    if (!ch32rv_usb_host_control(*this, daddr, 0x21, UVC_SET_CUR, VS_PROBE << 8, itf, ctl, PROBE_LENGTH, nullptr,
                                 1000)) {
        return false;
    }
    size_t got = 0;
    if (!ch32rv_usb_host_control(*this, daddr, 0xA1, UVC_GET_CUR, VS_PROBE << 8, itf, ctl, PROBE_LENGTH, &got, 1000)) {
        return false;
    }
    if (got < PROBE_LENGTH) {
        return fail(CH32RVUsbHostError::InvalidResponse);
    }
    if (!ch32rv_usb_host_control(*this, daddr, 0x21, UVC_SET_CUR, VS_COMMIT << 8, itf, ctl, PROBE_LENGTH, nullptr,
                                 1000)) {
        return false;
    }
    CH32RVUsbHostVideoProbeControl committed;
    committed.hint = le16(ctl);
    committed.formatIndex = ctl[2];
    committed.frameIndex = ctl[3];
    committed.frameInterval = le32(ctl + 4);
    committed.maxVideoFrameSize = le32(ctl + 18);
    committed.maxPayloadTransferSize = le32(ctl + 22);
    /* The narrowest alternate that carries the committed payload. */
    Alt alts[MAX_ALTS];
    bool iso;
    const uint8_t n = alternates(daddr, itf, alts, iso);
    int best = -1;
    for (uint8_t a = 0; a < n; a++) {
        if (!iso || alts[a].payload >= committed.maxPayloadTransferSize) {
            if (best < 0 || alts[a].payload < alts[best].payload) {
                best = a;
            }
        }
    }
    if (best < 0) {
        return fail(CH32RVUsbHostError::NotSupported);   /* no alternate wide enough */
    }
    const uint32_t frame = committed.maxVideoFrameSize ? committed.maxVideoFrameSize : stream.maxVideoFrameBufferSize;
    const uint32_t payload = iso ? alts[best].payload
                                 : (committed.maxPayloadTransferSize ? committed.maxPayloadTransferSize : 512u);
    if (!frame || payload > 0xFFFF) {
        return fail(CH32RVUsbHostError::InvalidResponse);
    }
    video.pkt = (uint8_t *)malloc(payload);
    video.buf[0] = (uint8_t *)malloc(frame);
    video.buf[1] = (uint8_t *)malloc(frame);
    if (!video.pkt || !video.buf[0] || !video.buf[1]) {
        release();
        return fail(CH32RVUsbHostError::NoMemory);
    }
    if (iso && !ch32rv_usb_host_control(*this, daddr, 0x01, 0x0B, alts[best].alt, itf, nullptr, 0, nullptr, 1000)) {
        release();
        return false;
    }
    if (!ch32rv_usb_host_ep_open(daddr, alts[best].keep)) {
        release();
        return fail(CH32RVUsbHostError::TransferFailed);
    }
    video.daddr = daddr;
    video.itf = itf;
    video.ep = alts[best].keep[2];
    video.iso = iso;
    video.width = stream.width;
    video.height = stream.height;
    video.format = stream.format;
    video.payload = payload;
    video.bufSize = frame;
    video.committed = committed;
    video.running = true;
    ch32rv_usb_host_mark_claimed(daddr, itf);
    arm();
    return ok();
}

bool CH32RVUsbHost::videoStart(uint8_t format, uint16_t width, uint16_t height, uint32_t fps, uint8_t address)
{
    CH32RVUsbHostVideoStreamInfo list[8];
    const size_t n = getVideoStreams(address, list, 8);
    for (size_t i = 0; i < n && i < 8; i++) {
        const CH32RVUsbHostVideoStreamInfo &s = list[i];
        if ((!format || s.format == format) && (!width || s.width == width) && (!height || s.height == height)) {
            return videoStart(s, fps, address);
        }
    }
    return fail(CH32RVUsbHostError::NotSupported);
}

bool CH32RVUsbHost::videoStop(uint8_t address)
{
    if (!matches(address)) {
        return false;
    }
    const uint8_t daddr = video.daddr, itf = video.itf, ep = video.ep;
    const bool iso = video.iso;
    video.running = false;
    if (ch32rv_usb_host_connected(daddr)) {
        ch32rv_usb_host_abort(daddr, ep);
        ch32rv_usb_host_ep_close(daddr, ep);
        if (iso) {
            ch32rv_usb_host_control_sync(daddr, 0x01, 0x0B, 0, itf, nullptr, 0, nullptr, 1000);
        }
    }
    release();
    return true;
}

bool CH32RVUsbHost::videoStreaming(uint8_t address) const
{
    return matches(address);
}

bool CH32RVUsbHost::videoCommitted(CH32RVUsbHostVideoProbeControl &control, uint8_t address) const
{
    if (!matches(address)) {
        return false;
    }
    control = video.committed;
    return true;
}

bool CH32RVUsbHost::videoStats(CH32RVUsbHostVideoStats &stats, uint8_t address) const
{
    if (!matches(address)) {
        stats = CH32RVUsbHostVideoStats();
        return false;
    }
    stats = video.stats;
    return true;
}

#else /* no host */

size_t CH32RVUsbHost::getVideoStreams(uint8_t, CH32RVUsbHostVideoStreamInfo *, size_t) { return 0; }
bool CH32RVUsbHost::videoStart(const CH32RVUsbHostVideoStreamInfo &, uint32_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::videoStart(uint8_t, uint16_t, uint16_t, uint32_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::videoStop(uint8_t) { return false; }
bool CH32RVUsbHost::videoStreaming(uint8_t) const { return false; }
bool CH32RVUsbHost::videoCommitted(CH32RVUsbHostVideoProbeControl &, uint8_t) const { return false; }
bool CH32RVUsbHost::videoStats(CH32RVUsbHostVideoStats &stats, uint8_t) const
{
    stats = CH32RVUsbHostVideoStats();
    return false;
}

#endif
