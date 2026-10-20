/* USB Video Class camera on the device stack, with EspUsbDeviceVideo's
 * descriptor (the bytes pinned there by tests/single/video_descriptor):
 * probe / commit on EP0, payloads of one packet each on a bulk or
 * isochronous IN endpoint. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

namespace {

constexpr uint8_t DESC_CS_INTERFACE = 0x24;
constexpr uint8_t DESC_INTERFACE = 0x04;
constexpr uint8_t DESC_INTERFACE_ASSOCIATION = 0x0b;
constexpr uint8_t DESC_ENDPOINT = 0x05;
constexpr uint8_t CLASS_VIDEO = 0x0e;
constexpr uint8_t SUBCLASS_CONTROL = 0x01;
constexpr uint8_t SUBCLASS_STREAMING = 0x02;
constexpr uint8_t SUBCLASS_INTERFACE_COLLECTION = 0x03;
constexpr uint8_t ITF_PROTOCOL_15 = 0x01;
constexpr uint8_t ITF_PROTOCOL_UNDEFINED = 0x00;

constexpr uint8_t VC_HEADER = 0x01;
constexpr uint8_t VC_INPUT_TERMINAL = 0x02;
constexpr uint8_t VC_OUTPUT_TERMINAL = 0x03;
constexpr uint8_t VS_INPUT_HEADER = 0x01;
constexpr uint8_t VS_FORMAT_UNCOMPRESSED = 0x04;
constexpr uint8_t VS_FRAME_UNCOMPRESSED = 0x05;
constexpr uint8_t VS_FORMAT_MJPEG = 0x06;
constexpr uint8_t VS_FRAME_MJPEG = 0x07;
constexpr uint8_t VS_COLORFORMAT = 0x0d;

constexpr uint16_t BCD_UVC_1_50 = 0x0150;
constexpr uint16_t TT_STREAMING = 0x0101;
constexpr uint16_t ITT_CAMERA = 0x0201;
/* Only the unit of payload-header timestamps, which nothing here emits;
 * hosts accept any non-zero value. 27 MHz is the specification's example. */
constexpr uint32_t CLOCK_FREQUENCY = 27000000u;

/* The camera feeds the USB streaming terminal (bTerminalLink). */
constexpr uint8_t TERMINAL_CAMERA = 1;
constexpr uint8_t TERMINAL_OUTPUT = 2;

constexpr uint16_t MAX_DIMENSION = 4096;

/* YUY2 as a format GUID: the FourCC and the fixed DirectShow suffix. */
const uint8_t GUID_YUY2[16] = {0x59, 0x55, 0x59, 0x32, 0x00, 0x00, 0x10, 0x00,
                               0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};

struct Writer {
    uint8_t *dst;
    uint16_t capacity;
    uint16_t offset;
    bool overflowed;

    void u8(uint8_t value)
    {
        if (offset >= capacity) {
            overflowed = true;
            return;
        }
        dst[offset++] = value;
    }
    void u16(uint16_t value)
    {
        u8((uint8_t)(value & 0xff));
        u8((uint8_t)(value >> 8));
    }
    void u32(uint32_t value)
    {
        u16((uint16_t)(value & 0xffff));
        u16((uint16_t)(value >> 16));
    }
    void bytes(const uint8_t *data, uint16_t length)
    {
        for (uint16_t i = 0; i < length; i++) {
            u8(data[i]);
        }
    }
};

} // namespace

/* Full speed reserves one packet per 1 ms frame, at most 1023 bytes; high
 * speed one per 125 us microframe, at most 1024; the controller may hold
 * less. */
uint16_t CH32RVUsbDeviceVideo::isochronousPacketSize(bool highSpeed) const
{
    const uint16_t limit = highSpeed ? 1024 : 1023;
    const uint16_t controller = device().maxPacketSize();
    const uint16_t n = controller < limit ? controller : limit;
    return n < CH32RV_USB_MAX_PACKET ? n : CH32RV_USB_MAX_PACKET;
}

uint16_t CH32RVUsbDeviceVideo::payloadBufferSize() const
{
    if (bulkStreaming()) {
        return ch32rv_usb_bulk_size(_highSpeed ? CH32RVUsbSpeed::High : CH32RVUsbSpeed::Full);
    }
    return isochronousPacketSize(_highSpeed);
}

/* ---------------------------------------------------------- settings */
bool CH32RVUsbDeviceVideo::configurable() const
{
    return !device().started();
}

bool CH32RVUsbDeviceVideo::setFormat(CH32RVUsbDeviceVideoFormat format)
{
    if (!configurable()) {
        return false;
    }
    _format = format;
    return true;
}

bool CH32RVUsbDeviceVideo::setTransferType(CH32RVUsbDeviceVideoTransfer type)
{
    if (!configurable()) {
        return false;
    }
    _transfer = type;
    return true;
}

bool CH32RVUsbDeviceVideo::setFrameSize(uint16_t width, uint16_t height)
{
    if (!configurable() || width == 0 || height == 0 || width > MAX_DIMENSION || height > MAX_DIMENSION ||
        (width & 1) != 0 || (height & 1) != 0) {
        return false;
    }
    _width = width;
    _height = height;
    return true;
}

bool CH32RVUsbDeviceVideo::setFrameRate(uint8_t framesPerSecond)
{
    if (!configurable() || framesPerSecond == 0 || framesPerSecond > 120) {
        return false;
    }
    _frameRate = framesPerSecond;
    return true;
}

bool CH32RVUsbDeviceVideo::setMaxFrameSize(uint32_t bytes)
{
    if (!configurable()) {
        return false;
    }
    _maxFrameSize = bytes;
    return true;
}

uint32_t CH32RVUsbDeviceVideo::maxFrameSize() const
{
    if (_maxFrameSize != 0) {
        return _maxFrameSize;
    }
    const uint32_t uncompressed = (uint32_t)_width * (uint32_t)_height * 2u;
    /* MJPEG: a compression ratio of 8, an advertised bound only. */
    return _format == CH32RVUsbDeviceVideoFormat::Yuy2 ? uncompressed : uncompressed / 8u;
}

/* ---------------------------------------------------------- descriptor */
uint16_t CH32RVUsbDeviceVideo::descriptorLength(bool highSpeed) const
{
    (void)highSpeed;
    /* IAD 8, VC 9, VC header 13, camera terminal 18, output terminal 9,
     * VS 9, VS input header 14, frame 38, colour matching 6; the format; the
     * endpoint 7, after a second alternate setting (9) when isochronous -
     * alternate 0 has no endpoint so an idle camera reserves no bandwidth. */
    uint16_t length = 8 + 9 + 13 + 18 + 9 + 9 + 14 + 38 + 6;
    length = (uint16_t)(length + (_format == CH32RVUsbDeviceVideoFormat::Yuy2 ? 27 : 11));
    length = (uint16_t)(length + (bulkStreaming() ? 7 : 9 + 7));
    return length;
}

uint16_t video_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *dst, uint16_t room, uint8_t itf,
                          uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed)
{
    const bool hs = ch32rv_usb_series_can(CH32RVUsbSpeed::High) && speed != CH32RVUsbSpeed::Full;
    CH32RVUsbDeviceVideo &v = const_cast<CH32RVUsbDeviceVideo &>(static_cast<const CH32RVUsbDeviceVideo &>(self));
    if (v.descriptorLength(hs) > room) {
        return 0;
    }
    const uint8_t name = self.interfaceName() ? str : 0;
    const uint8_t vcInterface = itf;
    const uint8_t vsInterface = (uint8_t)(itf + 1);
    const uint8_t endpointAddress = (uint8_t)(0x80 | (*ep)++);
    v._ep = endpointAddress;
    v._vcInterface = vcInterface;
    const bool uncompressed = v.format() == CH32RVUsbDeviceVideoFormat::Yuy2;
    const bool bulk = v.bulkStreaming();
    const uint32_t frameBytes = v.maxFrameSize();
    const uint32_t frameInterval = 10000000u / v.frameRate();
    const uint32_t bitRate = frameBytes * 8u * v.frameRate();

    Writer w = {dst, room, 0, false};

    /* Interface association: the two interfaces are one function. */
    w.u8(8);
    w.u8(DESC_INTERFACE_ASSOCIATION);
    w.u8(vcInterface);
    w.u8(2);
    w.u8(CLASS_VIDEO);
    w.u8(SUBCLASS_INTERFACE_COLLECTION);
    w.u8(ITF_PROTOCOL_UNDEFINED);
    w.u8(name);

    /* Video control, no endpoint: the optional interrupt endpoint carries
     * status events this function has none of. */
    w.u8(9);
    w.u8(DESC_INTERFACE);
    w.u8(vcInterface);
    w.u8(0);
    w.u8(0);
    w.u8(CLASS_VIDEO);
    w.u8(SUBCLASS_CONTROL);
    w.u8(ITF_PROTOCOL_15);
    w.u8(name);

    /* VC header. wTotalLength covers it and the two terminals. */
    w.u8(13);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VC_HEADER);
    w.u16(BCD_UVC_1_50);
    w.u16(13 + 18 + 9);
    w.u32(CLOCK_FREQUENCY);
    w.u8(1);
    w.u8(vsInterface);

    /* Camera input terminal: fixed lens, no controls. */
    w.u8(18);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VC_INPUT_TERMINAL);
    w.u8(TERMINAL_CAMERA);
    w.u16(ITT_CAMERA);
    w.u8(0);
    w.u8(0);
    w.u16(0);
    w.u16(0);
    w.u16(0);
    w.u8(3);
    w.u8(0);
    w.u8(0);
    w.u8(0);

    /* Output terminal: the pixels leave over USB. */
    w.u8(9);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VC_OUTPUT_TERMINAL);
    w.u8(TERMINAL_OUTPUT);
    w.u16(TT_STREAMING);
    w.u8(0);
    w.u8(TERMINAL_CAMERA);
    w.u8(0);

    /* Video streaming, alternate 0: bulk carries its endpoint here,
     * isochronous parks without one. */
    w.u8(9);
    w.u8(DESC_INTERFACE);
    w.u8(vsInterface);
    w.u8(0);
    w.u8(bulk ? 1 : 0);
    w.u8(CLASS_VIDEO);
    w.u8(SUBCLASS_STREAMING);
    w.u8(ITF_PROTOCOL_15);
    w.u8(name);

    /* VS input header; wTotalLength runs through the colour matching one. */
    const uint8_t formatLength = uncompressed ? 27 : 11;
    w.u8(14);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VS_INPUT_HEADER);
    w.u8(1);
    w.u16((uint16_t)(14 + formatLength + 38 + 6));
    w.u8(endpointAddress);
    w.u8(0);                /* bmInfo: no dynamic format change */
    w.u8(TERMINAL_OUTPUT);
    w.u8(0);                /* no still capture, no trigger */
    w.u8(0);
    w.u8(0);
    w.u8(1);
    w.u8(0);                /* bmaControls */

    if (uncompressed) {
        w.u8(27);
        w.u8(DESC_CS_INTERFACE);
        w.u8(VS_FORMAT_UNCOMPRESSED);
        w.u8(1);
        w.u8(1);
        w.bytes(GUID_YUY2, sizeof GUID_YUY2);
        w.u8(16);
        w.u8(1);
        w.u8(0);
        w.u8(0);
        w.u8(0);
        w.u8(0);
    } else {
        w.u8(11);
        w.u8(DESC_CS_INTERFACE);
        w.u8(VS_FORMAT_MJPEG);
        w.u8(1);
        w.u8(1);
        w.u8(0);            /* bmFlags: sample size not fixed */
        w.u8(1);
        w.u8(0);
        w.u8(0);
        w.u8(0);
        w.u8(0);
    }

    /* Frame: continuous form with min = max and step 0, i.e. one fixed rate. */
    w.u8(38);
    w.u8(DESC_CS_INTERFACE);
    w.u8(uncompressed ? VS_FRAME_UNCOMPRESSED : VS_FRAME_MJPEG);
    w.u8(1);
    w.u8(0);
    w.u16(v.width());
    w.u16(v.height());
    w.u32(bitRate);
    w.u32(bitRate);
    w.u32(frameBytes);
    w.u32(frameInterval);
    w.u8(0);
    w.u32(frameInterval);
    w.u32(frameInterval);
    w.u32(0);

    /* Colour matching: BT.709 primaries and transfer, BT.601 matrix. */
    w.u8(6);
    w.u8(DESC_CS_INTERFACE);
    w.u8(VS_COLORFORMAT);
    w.u8(1);
    w.u8(1);
    w.u8(4);

    if (!bulk) {
        /* Alternate 1 carries the isochronous endpoint; selecting it is what
         * reserves the bandwidth. */
        w.u8(9);
        w.u8(DESC_INTERFACE);
        w.u8(vsInterface);
        w.u8(1);
        w.u8(1);
        w.u8(CLASS_VIDEO);
        w.u8(SUBCLASS_STREAMING);
        w.u8(ITF_PROTOCOL_15);
        w.u8(0);
    }

    w.u8(7);
    w.u8(DESC_ENDPOINT);
    w.u8(endpointAddress);
    if (bulk) {
        w.u8(0x02);
        w.u16(ch32rv_usb_bulk_size(speed));
        w.u8(0);
    } else {
        /* Isochronous, asynchronous; every frame (FS) / microframe (HS). */
        w.u8(0x05);
        w.u16(v.isochronousPacketSize(hs));
        w.u8(1);
    }

    if (w.overflowed || w.offset != v.descriptorLength(hs)) {
        return 0;
    }
    return w.offset;
}

static const CH32RVUsbFunctionType video_type = {video_descriptor, 2, 2, nullptr, 0};

CH32RVUsbDeviceVideo::CH32RVUsbDeviceVideo(CH32RVUsbDevice &device, const char *name)
    : CH32RVUsbDeviceFunction(device, video_type, name ? name : CH32RV_USB_VIDEO_NAME)
{
}

bool CH32RVUsbDeviceVideo::streaming() const
{
    ch32rv_usb_task();
    return attached() && device().mounted() && _streaming;
}

/* ---------------------------------------------------------- probe and commit */
#define VS_PROBE_CONTROL  1
#define VS_COMMIT_CONTROL 2
#define VC_REQUEST_ERROR_CODE_CONTROL 2
#define UVC_SET_CUR  0x01
#define UVC_GET_CUR  0x81
#define UVC_GET_MIN  0x82
#define UVC_GET_MAX  0x83
#define UVC_GET_RES  0x84
#define UVC_GET_LEN  0x85
#define UVC_GET_INFO 0x86
#define UVC_GET_DEF  0x87

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The one format, frame and rate there is, and a payload of one packet. */
void CH32RVUsbDeviceVideo::defaultProbe(uint8_t *p) const
{
    memset(p, 0, 48);
    p[2] = 1;                                    /* bFormatIndex */
    p[3] = 1;                                    /* bFrameIndex */
    put32(p + 4, 10000000u / _frameRate);
    put32(p + 18, maxFrameSize());
    put32(p + 22, payloadBufferSize());
    put32(p + 26, CLOCK_FREQUENCY);
    p[30] = 0x03;                                /* bmFramingInfo: FID and EOF used */
    p[31] = p[32] = p[33] = 1;                   /* payload format version 1 */
}

bool CH32RVUsbDeviceVideo::usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    if ((s.bmRequestType & 0x60) != 0x20 || (s.bmRequestType & 0x1F) != 1) {
        return false;
    }
    const uint8_t itf = (uint8_t)s.wIndex, selector = (uint8_t)(s.wValue >> 8);
    const bool setup = stage == CH32RVUsbControlStage::Setup;
    if (itf == _vcInterface) {
        /* The control interface has no controls; the error code of the last
         * request (none) is all a host may read. */
        if (selector == VC_REQUEST_ERROR_CODE_CONTROL && s.bRequest == UVC_GET_CUR && (s.wValue & 0xff) == 0) {
            if (setup) {
                _probe[47] = 0;
                return device().controlIn(&_probe[47], 1);
            }
            return true;
        }
        return false;
    }
    if (itf != (uint8_t)(_vcInterface + 1) || (selector != VS_PROBE_CONTROL && selector != VS_COMMIT_CONTROL)) {
        return false;
    }
    static uint8_t answer[48] __attribute__((aligned(4)));
    switch (s.bRequest) {
    case UVC_SET_CUR:
        if (setup) {
            return s.wLength >= 26 && s.wLength <= sizeof answer && device().controlOut(answer, s.wLength);
        }
        if (stage == CH32RVUsbControlStage::Data) {
            /* Only what there is: format 1, frame 1, and the host's frame
             * interval when it gives one (a pacing hint). */
            const uint32_t interval = get32(answer + 4);
            defaultProbe(_probe);
            if (interval) {
                put32(_probe + 4, interval);
            }
            if (selector == VS_COMMIT_CONTROL) {
                CH32RVUsbDeviceVideoCommit c;
                c.formatIndex = _probe[2];
                c.frameIndex = _probe[3];
                c.frameInterval = get32(_probe + 4);
                c.maxVideoFrameSize = get32(_probe + 18);
                c.maxPayloadTransferSize = get32(_probe + 22);
                /* A commit drops the frame in flight. Bulk: it also starts the
                 * stream; isochronous streams while alternate 1 is selected,
                 * which the host may set before or after the commit. */
                const bool selected = _streaming && !bulkStreaming();
                stop();
                _streaming = bulkStreaming() || selected;
                handleCommit(c);
            }
        }
        return true;
    case UVC_GET_CUR:
    case UVC_GET_MIN:
    case UVC_GET_MAX:
    case UVC_GET_DEF:
        if (setup) {
            if (s.bRequest == UVC_GET_CUR && _probe[2]) {
                memcpy(answer, _probe, sizeof answer);
            } else {
                defaultProbe(answer);
            }
            return device().controlIn(answer, s.wLength < sizeof answer ? s.wLength : sizeof answer);
        }
        return true;
    case UVC_GET_RES:
        if (setup) {
            memset(answer, 0, sizeof answer);
            return device().controlIn(answer, s.wLength < sizeof answer ? s.wLength : sizeof answer);
        }
        return true;
    case UVC_GET_LEN:
        if (setup) {
            put16(answer, sizeof answer);
            return device().controlIn(answer, 2);
        }
        return true;
    case UVC_GET_INFO:
        if (setup) {
            answer[0] = 0x03;                    /* GET and SET supported */
            return device().controlIn(answer, 1);
        }
        return true;
    }
    return false;
}

/* ---------------------------------------------------------- the stream */
void CH32RVUsbDeviceVideo::usbReset()
{
    _streaming = false;
    _inFlight = false;
    _frame = nullptr;
    memset(_probe, 0, sizeof _probe);
}

void CH32RVUsbDeviceVideo::usbConfigured(CH32RVUsbSpeed speed)
{
    _highSpeed = ch32rv_usb_series_can(CH32RVUsbSpeed::High) && speed != CH32RVUsbSpeed::Full;
    usbReset();
}

/* Drop a frame in flight; no completion for it. */
void CH32RVUsbDeviceVideo::stop()
{
    if (_inFlight || device().transferBusy(_ep)) {
        device().abort(_ep);
    }
    _streaming = false;
    _inFlight = false;
    _frame = nullptr;
}

bool CH32RVUsbDeviceVideo::usbSetInterface(uint8_t interfaceNumber, uint8_t alternate)
{
    if (interfaceNumber != (uint8_t)(_vcInterface + 1) || alternate > (bulkStreaming() ? 0 : 1)) {
        return interfaceNumber == _vcInterface && alternate == 0;
    }
    stop();
    _streaming = alternate == 1;                 /* isochronous: the bandwidth is reserved now */
    return true;
}

/* Linux stops a bulk stream with CLEAR_FEATURE(ENDPOINT_HALT): what was in
 * flight is stale, and the next commit starts over. */
void CH32RVUsbDeviceVideo::usbHaltCleared(uint8_t address)
{
    if (address == _ep) {
        stop();
    }
}

/* The next payload: the header, then as much of the frame as one packet
 * holds. The last payload carries end-of-frame. */
void CH32RVUsbDeviceVideo::sendPayload()
{
    const uint16_t size = payloadBufferSize();
    const uint32_t left = _length - _offset;
    _sending = (uint16_t)(left < (uint32_t)(size - 2) ? left : (uint32_t)(size - 2));
    const bool last = _offset + _sending >= _length;
    _payload[0] = 2;
    _payload[1] = (uint8_t)(0x80 | _fid | (last ? 0x02 : 0));   /* EOH, FID, EOF */
    if (_frame) {
        memcpy(_payload + 2, _frame + _offset, _sending);
    } else {
        handleFill(_payload + 2, _sending, _offset);
    }
    if (!device().transfer(_ep, _payload, (uint16_t)(2 + _sending))) {
        stop();
    }
}

void CH32RVUsbDeviceVideo::usbTransferred(uint8_t address, uint16_t length)
{
    (void)length;
    if (address != _ep || !_inFlight) {
        return;
    }
    _offset += _sending;
    if (_offset < _length) {
        sendPayload();
        return;
    }
    _fid ^= 1;
    _frame = nullptr;
    handleFrameComplete();
}

bool CH32RVUsbDeviceVideo::arm(const uint8_t *frame, size_t length)
{
    if (length == 0 || length > maxFrameSize() || _inFlight || !streaming()) {
        return false;
    }
    _frame = frame;
    _length = (uint32_t)length;
    _offset = 0;
    _inFlight = true;
    sendPayload();
    return _inFlight;
}

bool CH32RVUsbDeviceVideo::sendFrame(const void *frame, size_t length)
{
    return frame && arm(static_cast<const uint8_t *>(frame), length);
}

bool CH32RVUsbDeviceVideo::sendGeneratedFrame(size_t length)
{
    return _onFill && arm(nullptr, length);
}

void CH32RVUsbDeviceVideo::end()
{
    if (device().mounted()) {
        stop();
    }
    _inFlight = false;
    _frame = nullptr;
    _commit = CH32RVUsbDeviceVideoCommit();
}

void CH32RVUsbDeviceVideo::handleFrameComplete()
{
    _inFlight = false;
    if (_onFrameComplete) {
        _onFrameComplete();
    }
}

/* A commit restarts the stream: any frame in flight was dropped. */
void CH32RVUsbDeviceVideo::handleCommit(const CH32RVUsbDeviceVideoCommit &commit)
{
    _commit = commit;
    _inFlight = false;
    if (_onCommit) {
        _onCommit(commit);
    }
}

void CH32RVUsbDeviceVideo::handleFill(uint8_t *dst, size_t length, uint32_t offset)
{
    if (_onFill) {
        _onFill(dst, length, offset);
    } else {
        memset(dst, 0, length);
    }
}
