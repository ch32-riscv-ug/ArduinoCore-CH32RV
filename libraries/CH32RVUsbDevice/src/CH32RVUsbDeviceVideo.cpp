/* USB Video Class camera: TinyUSB's video_device, with EspUsbDeviceVideo's
 * descriptor.
 *
 * The descriptor is written here rather than through the TUD_VIDEO_DESC_*
 * macros because those take the frame size, rate and bit rate as macro
 * arguments, and this class decides them from what the sketch set. The bytes
 * are the same as EspUsbDevice's (pinned there by tests/single/
 * video_descriptor). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "tusb.h"

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

/* ---------------------------------------------------------- build options */
#if CH32RV_TINYUSB_DEVICE
static_assert(CFG_TUD_VIDEO_STREAMING >= CFG_TUD_VIDEO,
              "CFG_TUD_VIDEO_STREAMING must be at least CFG_TUD_VIDEO: each camera has one stream");
/* USBFS on X035/X033 and V103 has 64-byte endpoints only, isochronous ones
 * included (dcd_edpt_iso_alloc refuses more), so the payload must fit 64. */
#if !CFG_TUD_VIDEO_STREAMING_BULK && CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE > 64 && \
    (CFG_TUSB_MCU == OPT_MCU_CH32X035 || CFG_TUSB_MCU == OPT_MCU_CH32V103)
#error "this USBFS has 64-byte isochronous endpoints: CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE must be 64 or less without CFG_TUD_VIDEO_STREAMING_BULK"
#endif
#if CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE > 1023
#error "CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE must be 1023 or less: wMaxPacketSize and the full-speed isochronous limit"
#endif
#define VIDEO_EP_BUFSIZE CFG_TUD_VIDEO_STREAMING_EP_BUFSIZE
#define VIDEO_BULK       CFG_TUD_VIDEO_STREAMING_BULK
#else
#define VIDEO_EP_BUFSIZE 0
#define VIDEO_BULK       0
#endif

uint16_t CH32RVUsbDeviceVideo::payloadBufferSize()
{
    return VIDEO_EP_BUFSIZE;
}

bool CH32RVUsbDeviceVideo::bulkStreaming()
{
    return VIDEO_BULK != 0;
}

/* Full speed reserves one packet per 1 ms frame, at most 1023 bytes; high
 * speed one per 125 us microframe, at most 1024. TinyUSB opens the streaming
 * alternate setting only when the endpoint is at least the committed payload,
 * which is capped by the payload buffer - so the buffer is the size. */
uint16_t CH32RVUsbDeviceVideo::isochronousPacketSize(bool highSpeed)
{
    const uint16_t limit = highSpeed ? 1024 : 1023;
    const uint16_t staging = VIDEO_EP_BUFSIZE;
    return staging == 0 || staging > limit ? limit : staging;
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

/* The streaming endpoint each camera was given, for the commit callback. */
static uint8_t video_endpoint[CH32RV_TINYUSB_DEVICE ? CFG_TUD_VIDEO : 1];

static uint16_t video_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *dst, uint16_t room, uint8_t itf,
                                 uint8_t *ep, uint8_t str, bool hs)
{
    const CH32RVUsbDeviceVideo &v = static_cast<const CH32RVUsbDeviceVideo &>(self);
    if (v.descriptorLength(hs) > room) {
        return 0;
    }
    const uint8_t name = self.interfaceName() ? str : 0;
    const uint8_t vcInterface = itf;
    const uint8_t vsInterface = (uint8_t)(itf + 1);
    const uint8_t endpointAddress = (uint8_t)(0x80 | (*ep)++);
    if (self.instance() < sizeof video_endpoint) {
        video_endpoint[self.instance()] = endpointAddress;
    }
    const bool uncompressed = v.format() == CH32RVUsbDeviceVideoFormat::Yuy2;
    const bool bulk = CH32RVUsbDeviceVideo::bulkStreaming();
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

    /* VC header. wTotalLength covers it and the two terminals; TinyUSB finds
     * the end of the control interface by it. */
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
        w.u16(ch32rv_usb_bulk_size(hs));
        w.u8(0);
    } else {
        /* Isochronous, asynchronous; every frame (FS) / microframe (HS). */
        w.u8(0x05);
        w.u16(CH32RVUsbDeviceVideo::isochronousPacketSize(hs));
        w.u8(1);
    }

    if (w.overflowed || w.offset != v.descriptorLength(hs)) {
        return 0;
    }
    return w.offset;
}

#if CH32RV_TINYUSB_DEVICE

#include "device/usbd_pvt.h"

static const usbd_class_driver_t video_driver = {
    NULL, videod_init, videod_deinit, videod_reset, videod_open, videod_control_xfer_cb, videod_xfer_cb, NULL, NULL,
};

static const CH32RVUsbFunctionType video_type = {&video_driver, video_descriptor, 2, CFG_TUD_VIDEO, nullptr, 0};

/* TinyUSB numbers the video functions in interface order, which is the
 * declaration order, i.e. instance(). */
static CH32RVUsbDeviceVideo *cameras[CFG_TUD_VIDEO];

static CH32RVUsbDeviceVideo *camera(uint_fast8_t ctl_idx)
{
    return ctl_idx < CFG_TUD_VIDEO ? cameras[ctl_idx] : nullptr;
}

extern "C" void tud_video_frame_xfer_complete_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx)
{
    (void)stm_idx;
    if (CH32RVUsbDeviceVideo *v = camera(ctl_idx)) {
        v->handleFrameComplete();
    }
}

/* Accepting whatever the host committed to is right for a device that
 * advertises one format at one size: it can only have chosen that, and the
 * frame interval is a pacing hint. */
extern "C" int tud_video_commit_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
                                   video_probe_and_commit_control_t const *parameters)
{
    (void)stm_idx;
    CH32RVUsbDeviceVideo *v = camera(ctl_idx);
    if (v && parameters) {
        CH32RVUsbDeviceVideoCommit c;
        c.formatIndex = parameters->bFormatIndex;
        c.frameIndex = parameters->bFrameIndex;
        c.frameInterval = parameters->dwFrameInterval;
        c.maxVideoFrameSize = parameters->dwMaxVideoFrameSize;
        c.maxPayloadTransferSize = parameters->dwMaxPayloadTransferSize;
        /* Linux stops a bulk stream with CLEAR_FEATURE(ENDPOINT_HALT), which
         * drops the transfer in flight but leaves the endpoint claimed, so
         * every later frame would be refused. A commit starts a new stream:
         * let go of a claim no transfer holds (a busy endpoint keeps it). */
        usbd_edpt_release(0, video_endpoint[ctl_idx]);
        v->handleCommit(c);
    }
    return VIDEO_ERROR_NONE;
}

extern "C" void tud_video_prepare_payload_cb(uint_fast8_t ctl_idx, uint_fast8_t stm_idx,
                                             tud_video_payload_request_t *request)
{
    (void)stm_idx;
    CH32RVUsbDeviceVideo *v = camera(ctl_idx);
    uint8_t *dst = (uint8_t *)request->buf;
    const size_t length = request->length;
    if (v) {
        v->handleFill(dst, length, (uint32_t)request->offset);
    } else {
        memset(dst, 0, length);
    }
}

CH32RVUsbDeviceVideo::CH32RVUsbDeviceVideo(CH32RVUsbDevice &device, const char *name)
    : CH32RVUsbDeviceFunction(device, video_type, name ? name : CH32RV_USB_VIDEO_NAME)
{
    if (attached()) {
        cameras[instance()] = this;
    }
}

bool CH32RVUsbDeviceVideo::streaming() const
{
    ch32rv_usb_task();
    return attached() && device().started() && tud_video_n_streaming(instance(), 0);
}

/* A bulk payload larger than one packet ends only on a short packet: the host
 * reads dwMaxPayloadTransferSize per transfer, and a last payload that is a
 * whole number of packets but shorter than that runs on into the next
 * frame's first payload (TinyUSB sends no zero-length packet). Such a length
 * is refused rather than sent torn. Never the case with the default payload
 * buffer, which is one packet. */
static bool bulk_length_ok(uint32_t payload, size_t length)
{
    if (!CH32RVUsbDeviceVideo::bulkStreaming() || payload <= 2) {
        return true;
    }
    const uint32_t packet = ch32rv_usb_bulk_size(tud_speed_get() == TUSB_SPEED_HIGH);
    if (payload <= packet) {
        return true;
    }
    const uint32_t rest = (uint32_t)(length % (payload - 2u));
    const uint32_t last = rest ? rest + 2u : payload;
    return last == payload || last % packet != 0;
}

bool CH32RVUsbDeviceVideo::arm(void *frame, size_t length)
{
    if (length == 0 || length > maxFrameSize() || _inFlight || !streaming() ||
        !bulk_length_ok(_commit.maxPayloadTransferSize, length)) {
        return false;
    }
    _inFlight = true;
    if (!tud_video_n_frame_xfer(instance(), 0, frame, length)) {
        _inFlight = false;
        return false;
    }
    return true;
}

bool CH32RVUsbDeviceVideo::sendFrame(const void *frame, size_t length)
{
    /* const_cast: TinyUSB only reads a frame it is given. */
    return frame && arm(const_cast<void *>(frame), length);
}

bool CH32RVUsbDeviceVideo::sendGeneratedFrame(size_t length)
{
    return _onFill && arm(nullptr, length);
}

#else

static const CH32RVUsbFunctionType video_type = {nullptr, video_descriptor, 2, 1, nullptr, 0};

CH32RVUsbDeviceVideo::CH32RVUsbDeviceVideo(CH32RVUsbDevice &device, const char *name)
    : CH32RVUsbDeviceFunction(device, video_type, name ? name : CH32RV_USB_VIDEO_NAME)
{
}

bool CH32RVUsbDeviceVideo::streaming() const { return false; }
bool CH32RVUsbDeviceVideo::arm(void *, size_t) { return false; }
bool CH32RVUsbDeviceVideo::sendFrame(const void *, size_t) { return false; }
bool CH32RVUsbDeviceVideo::sendGeneratedFrame(size_t) { return false; }

#endif

void CH32RVUsbDeviceVideo::end()
{
    _inFlight = false;
    _commit = CH32RVUsbDeviceVideoCommit();
}

void CH32RVUsbDeviceVideo::handleFrameComplete()
{
    _inFlight = false;
    if (_onFrameComplete) {
        _onFrameComplete();
    }
}

/* A commit restarts the stream: TinyUSB has dropped any frame it held. */
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
