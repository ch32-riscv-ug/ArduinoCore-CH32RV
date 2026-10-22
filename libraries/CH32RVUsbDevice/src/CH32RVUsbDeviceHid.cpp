/* HID: the shared interface every HID class of the device sits on, and the
 * classes themselves (ported from EspUsbDevice: same report descriptors, same
 * report layouts, same behaviour). */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>


/* Report descriptors, as EspUsbDevice has them. */
static const uint8_t KEYBOARD_REPORT_DESCRIPTOR[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,             /* Generic Desktop, Keyboard, Application */
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             /* modifier bits */
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,             /* reserved byte */
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01,
    0x91, 0x02,                                     /* LEDs Num..Kana */
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,             /* LED padding */
    0x05, 0x07, 0x19, 0x00, 0x29, 0xff, 0x15, 0x00, 0x25, 0xff,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x00,             /* six key slots */
    0xc0,
};

/* N-key rollover: modifier byte + a 224-bit bitmap of usages 0x00-0xDF. */
static const uint8_t NKRO_KEYBOARD_REPORT_DESCRIPTOR[] = {
    0x05, 0x01, 0x09, 0x06, 0xa1, 0x01,
    0x05, 0x07, 0x19, 0xe0, 0x29, 0xe7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,             /* modifier bits */
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01,
    0x91, 0x02,                                     /* LEDs */
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,             /* LED padding */
    0x05, 0x07, 0x19, 0x00, 0x29, 0xdf, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0xe0, 0x81, 0x02,             /* key bitmap */
    0xc0,
};

static const uint8_t MOUSE_REPORT_DESCRIPTOR[] = {
    0x05, 0x01, 0x09, 0x02, 0xa1, 0x01,             /* Generic Desktop, Mouse, Application */
    0x09, 0x01, 0xa1, 0x00,                         /* Pointer, Physical */
    0x05, 0x09, 0x19, 0x01, 0x29, 0x05, 0x15, 0x00, 0x25, 0x01,
    0x95, 0x05, 0x75, 0x01, 0x81, 0x02,             /* buttons 1-5 */
    0x95, 0x01, 0x75, 0x03, 0x81, 0x01,             /* padding */
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x38, 0x15, 0x81, 0x25, 0x7f,
    0x75, 0x08, 0x95, 0x03, 0x81, 0x06,             /* X, Y, wheel (relative) */
    0xc0, 0xc0,
};

static const uint8_t GAMEPAD_REPORT_DESCRIPTOR[] = {
    0x05, 0x01, 0x09, 0x05, 0xa1, 0x01,             /* Generic Desktop, Game Pad */
    0x85, CH32RV_USB_DEVICE_HID_REPORT_ID_GAMEPAD,
    0x15, 0x81, 0x25, 0x7f,
    0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,
    0x75, 0x08, 0x95, 0x06, 0x81, 0x02,             /* X Y Z Rz Rx Ry */
    0x15, 0x00, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3b, 0x01, 0x65, 0x14,
    0x09, 0x39, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, /* hat switch */
    0x65, 0x00,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x20, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x20, 0x81, 0x02,             /* 32 buttons */
    0xc0,
};

static const uint8_t CONSUMER_CONTROL_REPORT_DESCRIPTOR[] = {
    0x05, 0x0c, 0x09, 0x01, 0xa1, 0x01,             /* Consumer, Consumer Control */
    0x85, CH32RV_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL,
    0x15, 0x00, 0x26, 0xff, 0x03, 0x19, 0x00, 0x2a, 0xff, 0x03,
    0x75, 0x10, 0x95, 0x01, 0x81, 0x00,
    0xc0,
};

static const uint8_t SYSTEM_CONTROL_REPORT_DESCRIPTOR[] = {
    0x05, 0x01, 0x09, 0x80, 0xa1, 0x01,             /* Generic Desktop, System Control */
    0x85, CH32RV_USB_DEVICE_HID_REPORT_ID_SYSTEM_CONTROL,
    0x15, 0x00, 0x25, 0x03, 0x19, 0x00, 0x29, 0x03,
    0x75, 0x08, 0x95, 0x01, 0x81, 0x00,
    0xc0,
};

/* Length of the HID item at `item`, 0 when it runs past `available`. */
static size_t hid_item_length(const uint8_t *item, size_t available)
{
    if (available == 0) {
        return 0;
    }
    if (item[0] == 0xfe) {                          /* long item */
        if (available < 3) {
            return 0;
        }
        const size_t length = 3u + item[1];
        return length <= available ? length : 0;
    }
    static const uint8_t data_size[4] = {0, 1, 2, 4};
    const size_t length = 1u + data_size[item[0] & 0x03];
    return length <= available ? length : 0;
}

#define HID_ITEM_COLLECTION 0xa0
#define HID_ITEM_REPORT_ID  0x84

/* A descriptor that declares a Report ID item of its own. */
static bool declares_report_id(const uint8_t *desc, uint16_t length)
{
    size_t at = 0;
    while (at < length) {
        const size_t n = hid_item_length(desc + at, length - at);
        if (n == 0) {
            return false;
        }
        if ((desc[at] & 0xfc) == HID_ITEM_REPORT_ID) {
            return true;
        }
        at += n;
    }
    return false;
}

/* ---------------------------------------------------------- the shared interface */
/* The largest report (ID included) and the interrupt endpoints' packet at
 * high speed and SuperSpeed: the series' largest packet. Full speed caps the
 * endpoint at 64; a longer report goes out in several packets. */
#define HID_EP_SIZE (CH32RV_USB_MAX_PACKET > 64 ? CH32RV_USB_MAX_PACKET : 64)

#define HID_GET_REPORT   0x01
#define HID_GET_IDLE     0x02
#define HID_GET_PROTOCOL 0x03
#define HID_SET_REPORT   0x09
#define HID_SET_IDLE     0x0A
#define HID_SET_PROTOCOL 0x0B

class HidInterface;
static bool hid_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf,
                           uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed);
static const CH32RVUsbFunctionType hid_type = {hid_descriptor, 1, 1, &CH32RVUsbDeviceConfig::hidInterfaceName, 0};

class HidInterface : public CH32RVUsbDeviceFunction {
public:
    explicit HidInterface(CH32RVUsbDevice &device) : CH32RVUsbDeviceFunction(device, hid_type, CH32RV_USB_HID_NAME) {}

    bool add(CH32RVUsbDeviceHidClass *c)
    {
        if (!attached() || device().started()) {
            return false;
        }
        CH32RVUsbDeviceHidClass **p = &first;
        while (*p) {
            p = &(*p)->_hidNext;
        }
        *p = c;
        count++;
        return true;
    }
    bool composite() const { return count > 1; }

    CH32RVUsbDeviceHidClass *at(uint8_t i) const
    {
        CH32RVUsbDeviceHidClass *c = first;
        while (c && i--) {
            c = c->_hidNext;
        }
        return c;
    }

    /* The report descriptor's length: one class's own, or the merged one's. */
    uint16_t reportLength()
    {
        if (!composite()) {
            return count ? first->hidReportDescriptorLength() : 0;
        }
        CH32RVUsbDescriptorWriter c;
        return merge(c) ? (uint16_t)c.length : 0;
    }

    /* Every class under its own Report ID, inserted right after its
     * Collection (Application); a Report ID the class declares itself is
     * replaced, not duplicated (EspUsbDevice::buildDescriptors). */
    bool merge(CH32RVUsbDescriptorWriter &w)
    {
        for (uint8_t i = 0; i < count; i++) {
            const uint8_t *src = at(i)->hidReportDescriptor();
            const uint16_t length = at(i)->hidReportDescriptorLength();
            size_t at = 0;
            bool written = false, just = false;
            while (at < length) {
                const size_t n = hid_item_length(src + at, length - at);
                if (n == 0) {
                    return false;
                }
                const uint8_t prefix = src[at];
                if (just && (prefix & 0xfc) == HID_ITEM_REPORT_ID) {
                    just = false;
                    at += n;
                    continue;
                }
                just = false;
                w.put(src + at, (uint16_t)n);
                at += n;
                if (!written && (prefix & 0xfc) == HID_ITEM_COLLECTION) {
                    w.u8(0x85);
                    w.u8(idOf(i));
                    written = just = true;
                }
            }
            if (!written) {
                return false;                       /* nothing to hang an ID on */
            }
        }
        return true;
    }

    uint8_t idOf(uint8_t i) const
    {
        const uint8_t id = at(i)->hidReportId();
        return id ? id : CH32RV_USB_DEVICE_HID_REPORT_ID_KEYBOARD;
    }

    /* An output (or feature) report from the host, on the OUT endpoint
     * (report_id 0: the ID, if any, is still the first byte) or by
     * SET_REPORT (the ID from wValue, already taken off the data). */
    void dispatch(uint8_t report_id, uint8_t type, const uint8_t *buffer, uint16_t size)
    {
        if (count == 0) {
            return;
        }
        CH32RVUsbDeviceHidClass *target = nullptr;
        if (composite()) {
            if (report_id == 0 && size > 0) {
                report_id = buffer[0];
                buffer++;
                size--;
            }
            for (uint8_t i = 0; i < count; i++) {
                if (idOf(i) == report_id) {
                    target = at(i);
                }
            }
        } else {
            target = first;
            const uint8_t own = target->hidReportId();
            if (report_id == 0 && size > 0 && own && buffer[0] == own &&
                declares_report_id(target->hidReportDescriptor(), target->hidReportDescriptorLength())) {
                report_id = own;
                buffer++;
                size--;
            }
        }
        if (target) {
            target->onHidSetReport(report_id, type, buffer, size);
        }
    }

    bool send(uint8_t report_id, const void *data, size_t length, uint32_t timeoutMs)
    {
        const uint32_t t0 = millis();
        for (;;) {
            ch32rv_usb_task();
            if (!device().mounted()) {
                return false;
            }
            if (!device().transferBusy(epIn)) {
                break;
            }
            if ((uint32_t)(millis() - t0) >= timeoutMs) {
                return false;
            }
        }
        uint16_t n = 0;
        if (report_id) {
            in[n++] = report_id;
        }
        const size_t room = sizeof in - n;
        const size_t take = length < room ? length : room;
        memcpy(in + n, data, take);
        return device().transfer(epIn, in, (uint16_t)(n + take));
    }

    /* ------------------------------------------------ the stack's side */
    void usbReset() override
    {
        outHeld = false;
        for (uint8_t i = 0; i < count; i++) {
            at(i)->onBusDetached();
        }
    }

    bool holding() const
    {
        for (uint8_t i = 0; i < count; i++) {
            if (at(i)->hidHolding()) {
                return true;
            }
        }
        return false;
    }

    size_t usbDeliver() override
    {
        size_t ran = 0;
        for (uint8_t i = 0; i < count; i++) {
            ran += at(i)->hidDeliver();
        }
        if (outHeld && !holding()) {
            outHeld = false;
            if (device().mounted()) {
                device().transfer(epOut, out, outLength);
            }
        }
        return ran;
    }

    void usbConfigured(CH32RVUsbSpeed) override
    {
        protocol = 1;                            /* report protocol after a configuration */
        idle = 0;
        outHeld = false;
        /* Each OUT transfer asks for the longest report: one that is a whole
         * number of packets then ends the transfer by its length. */
        outLength = 64;
        for (uint8_t i = 0; i < count; i++) {
            if (at(i)->hidInEndpointSize() > outLength) {
                outLength = at(i)->hidInEndpointSize();
            }
        }
        if (outLength > sizeof out) {
            outLength = sizeof out;
        }
        if (epOut) {
            device().transfer(epOut, out, outLength);
        }
        for (uint8_t i = 0; i < count; i++) {
            at(i)->onBusAttached();
        }
    }

    void usbTransferred(uint8_t address, uint16_t length) override
    {
        if (address == epOut) {
            dispatch(0, 2, out, length);         /* an output report */
            if (holding()) {
                outHeld = true;                  /* usbDeliver() takes the next */
            } else {
                device().transfer(epOut, out, outLength);
            }
        }
    }

    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s) override
    {
        if ((s.bmRequestType & 0x60) == 0) {     /* GET_DESCRIPTOR to the interface */
            if (stage != CH32RVUsbControlStage::Setup) {
                return true;
            }
            const uint16_t rl = reportLength();
            if ((s.wValue >> 8) == 0x22) {
                if (!composite()) {
                    return count && device().controlIn(first->hidReportDescriptor(), rl);
                }
                /* Merged as each EP0 packet goes out: nothing kept in RAM. */
                return device().controlInGenerated(rl, [](void *context, uint32_t offset, uint8_t *buffer, uint16_t n) {
                    CH32RVUsbDescriptorWriter w;
                    w.out = buffer;
                    w.skip = offset;
                    w.room = n;
                    static_cast<HidInterface *>(context)->merge(w);
                }, this);
            }
            if ((s.wValue >> 8) == 0x21) {
                hidDescriptor[7] = (uint8_t)rl;
                hidDescriptor[8] = (uint8_t)(rl >> 8);
                return device().controlIn(hidDescriptor, sizeof hidDescriptor);
            }
            return false;
        }
        if ((s.bmRequestType & 0x60) != 0x20) {
            return false;
        }
        switch (s.bRequest) {
        case HID_SET_REPORT:
            if (stage == CH32RVUsbControlStage::Setup) {
                return s.wLength <= sizeof ctrl && device().controlOut(ctrl, s.wLength);
            }
            if (stage == CH32RVUsbControlStage::Data) {
                const uint8_t id = (uint8_t)s.wValue;
                const uint8_t *buf = ctrl;
                uint16_t len = s.wLength;
                if (id && len > 1 && buf[0] == id) {
                    buf++;
                    len--;
                }
                dispatch(id, (uint8_t)(s.wValue >> 8), buf, len);
            }
            return true;
        case HID_GET_REPORT:                     /* optional and rarely used: stall */
            return false;
        case HID_SET_IDLE:
            idle = (uint8_t)(s.wValue >> 8);
            return stage != CH32RVUsbControlStage::Setup || device().controlStatus();
        case HID_GET_IDLE:
            return stage != CH32RVUsbControlStage::Setup || device().controlIn(&idle, 1);
        case HID_SET_PROTOCOL:
            if (stage == CH32RVUsbControlStage::Setup) {
                return device().controlStatus();
            }
            if (stage == CH32RVUsbControlStage::Ack) {
                protocol = (uint8_t)s.wValue;
                for (uint8_t i = 0; i < count; i++) {
                    at(i)->onHidSetProtocol(protocol);
                }
            }
            return true;
        case HID_GET_PROTOCOL:
            return stage != CH32RVUsbControlStage::Setup || device().controlIn(&protocol, 1);
        }
        return false;
    }

    CH32RVUsbDeviceHidClass *first = nullptr;   /* the classes, in declaration order */
    uint8_t count = 0;
    uint8_t hidDescriptor[9] = {9, 0x21, 0x11, 0x01, 0, 1, 0x22, 0, 0};
    uint8_t epIn = 0, epOut = 0;
    uint8_t idle = 0, protocol = 1;
    uint16_t outLength = 64;
    bool outHeld = false;                        /* OUT not re-armed: a class holds a report */
    uint8_t in[HID_EP_SIZE], out[HID_EP_SIZE], ctrl[HID_EP_SIZE];
};

static HidInterface *hid;

/* hidReportDescriptor(): the merged or single report descriptor, written to
 * `w` (nullptr: only its length). */
static uint16_t hid_report(CH32RVUsbDescriptorWriter *w)
{
    if (!hid || hid->count == 0) {
        return 0;
    }
    const uint16_t n = hid->reportLength();
    if (w && n) {
        if (hid->composite()) {
            hid->merge(*w);
        } else {
            w->put(hid->first->hidReportDescriptor(), n);
        }
    }
    return n;
}

static HidInterface &interface_for(CH32RVUsbDevice &device)
{
    static HidInterface shared(device);             /* one device per sketch */
    hid = &shared;
    ch32rv_usb_hid_report = hid_report;
    return shared;
}

static bool hid_descriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf,
                           uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed)
{
    HidInterface &h = const_cast<HidInterface &>(static_cast<const HidInterface &>(self));
    if (h.count == 0) {
        return false;
    }
    uint16_t size = h.composite() ? 16 : 64;
    uint8_t boot = 0;
    bool out_ep = true;
    if (!h.composite()) {
        CH32RVUsbDeviceHidClass *c = h.first;
        size = c->hidInEndpointSize() ? c->hidInEndpointSize() : 64;
        boot = c->hidBootProtocol();
        out_ep = c->hidOutEndpoint();
    } else {
        for (uint8_t i = 0; i < h.count; i++) {
            if (h.at(i)->hidInEndpointSize() > size) {
                size = h.at(i)->hidInEndpointSize();
            }
        }
    }
    const uint16_t cap = speed == CH32RVUsbSpeed::Full ? 64 : HID_EP_SIZE;
    if (size > cap) {
        size = cap;
    }
    const uint8_t n = (*ep)++;
    const uint16_t rl = h.reportLength();
    if (!rl) {
        return false;                            /* a report descriptor that does not parse */
    }
    const uint8_t interval = ch32rv_usb_interval(speed, 10);
    h.epIn = (uint8_t)(0x80 | n);
    h.epOut = out_ep ? n : 0;
    const uint8_t head[] = {
        9, 4, itf, 0, (uint8_t)(out_ep ? 2 : 1), 3, (uint8_t)(boot ? 1 : 0), boot, str,   /* interface */
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, (uint8_t)rl, (uint8_t)(rl >> 8),                 /* HID 1.11 */
    };
    w.put(head, sizeof head);
    if (out_ep) {
        const uint8_t e[] = {7, 5, n, 3, (uint8_t)size, (uint8_t)(size >> 8), interval};
        w.put(e, sizeof e);
    }
    const uint8_t e[] = {7, 5, (uint8_t)(0x80 | n), 3, (uint8_t)size, (uint8_t)(size >> 8), interval};
    w.put(e, sizeof e);
    return true;
}

CH32RVUsbDeviceHidClass::CH32RVUsbDeviceHidClass(CH32RVUsbDevice &device) : _device(device)
{
    _attached = interface_for(device).add(this);
}

uint8_t CH32RVUsbDeviceHidClass::classReportId() const
{
    if (hid && hid->composite()) {
        for (uint8_t i = 0; i < hid->count; i++) {
            if (hid->at(i) == this) {
                return hid->idOf(i);
            }
        }
    }
    return declares_report_id(hidReportDescriptor(), hidReportDescriptorLength()) ? hidReportId() : 0;
}

bool CH32RVUsbDeviceHidClass::sendHidReport(uint8_t reportId, const void *data, size_t length,
                                            uint32_t timeoutMs)
{
    if (!_attached || !data || length == 0) {
        return false;
    }
    return _device.sendHidReport(0, reportId, data, length, timeoutMs);
}

/* EspUsbDevice's low-level path: an input report on the (one) HID interface. */
bool CH32RVUsbDevice::sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length,
                                    uint32_t timeoutMs)
{
    if (!_started) {
        _error = CH32RVUsbDeviceError::NotStarted;
        return false;
    }
    if (instance != 0 || !hid || !hid->send(reportId, data, length, timeoutMs)) {
        _error = CH32RVUsbDeviceError::NotReady;
        return false;
    }
    return true;
}

/* ---------------------------------------------------------- keyboard */
CH32RVUsbDeviceHidKeyboard::CH32RVUsbDeviceHidKeyboard(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHidClass(device)
{
}

const CH32RVUsbDeviceKeyboardLayout &CH32RVUsbDeviceHidKeyboard::layout() const
{
    return _layout ? *_layout : CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US;
}

const uint8_t *CH32RVUsbDeviceHidKeyboard::hidReportDescriptor() const
{
    return _nkro ? NKRO_KEYBOARD_REPORT_DESCRIPTOR : KEYBOARD_REPORT_DESCRIPTOR;
}

uint16_t CH32RVUsbDeviceHidKeyboard::hidReportDescriptorLength() const
{
    return _nkro ? sizeof NKRO_KEYBOARD_REPORT_DESCRIPTOR : sizeof KEYBOARD_REPORT_DESCRIPTOR;
}

bool CH32RVUsbDeviceHidKeyboard::sendReport(const CH32RVUsbDeviceBootKeyboardReport &report,
                                            uint32_t timeoutMs)
{
    if (_nkro) {
        /* Adopt the 6-key report as the whole held state. */
        _nkroState.clear();
        _nkroState.modifiers = report.modifiers;
        for (uint8_t k : report.keys) {
            if (k) {
                _nkroState.press(k);
            }
        }
        return sendNkroReport(timeoutMs);
    }
    _report = report;
    return sendHidReport(classReportId(), &_report, sizeof _report, timeoutMs);
}

bool CH32RVUsbDeviceHidKeyboard::sendReport(const CH32RVUsbDeviceNkroKeyboardReport &report,
                                            uint32_t timeoutMs)
{
    if (!_nkro) {
        return false;          /* a setup mistake, not something to fold down */
    }
    _nkroState = report;
    return sendNkroReport(timeoutMs);
}

bool CH32RVUsbDeviceHidKeyboard::sendNkroReport(uint32_t timeoutMs)
{
    if (_protocol == 0) {
        /* Boot protocol (a BIOS) takes only the 6-key report: the six lowest
         * held usages. */
        CH32RVUsbDeviceBootKeyboardReport boot;
        boot.modifiers = _nkroState.modifiers;
        uint8_t slot = 0;
        for (uint16_t u = 0; u <= CH32RVUsbDeviceNkroKeyboardReport::MaxBitmapUsage && slot < 6; u++) {
            if (_nkroState.isDown((uint8_t)u)) {
                boot.keys[slot++] = (uint8_t)u;
            }
        }
        _report = boot;
        return sendHidReport(classReportId(), &_report, sizeof _report, timeoutMs);
    }
    uint8_t buffer[1 + CH32RVUsbDeviceNkroKeyboardReport::BitmapSize];
    buffer[0] = _nkroState.modifiers;
    memcpy(buffer + 1, _nkroState.bitmap, sizeof _nkroState.bitmap);
    return sendHidReport(classReportId(), buffer, sizeof buffer, timeoutMs);
}

bool CH32RVUsbDeviceHidKeyboard::pressUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
    (void)holdMs;
    if (_nkro) {
        if (!_nkroState.press(usage)) {
            return false;
        }
        _nkroState.modifiers |= modifiers;
        return sendNkroReport();
    }
    _report.modifiers = modifiers;
    _report.keys[0] = usage;
    return sendReport(_report);
}

bool CH32RVUsbDeviceHidKeyboard::pressKey(char key, uint32_t timeoutMs)
{
    uint8_t usage = 0, modifiers = 0;
    if (!asciiToUsage(key, usage, modifiers)) {
        return false;
    }
    _report.modifiers = modifiers;
    _report.keys[0] = usage;
    return sendReport(_report, timeoutMs);
}

bool CH32RVUsbDeviceHidKeyboard::tapUsage(uint8_t usage, uint8_t modifiers, uint32_t holdMs)
{
    if (!pressUsage(usage, modifiers)) {
        return false;
    }
    delay(holdMs);
    return releaseAll();
}

bool CH32RVUsbDeviceHidKeyboard::tapKey(char key, uint32_t holdMs)
{
    uint8_t usage = 0, modifiers = 0;
    if (!asciiToUsage(key, usage, modifiers)) {
        return false;
    }
    return tapUsage(usage, modifiers, holdMs);
}

bool CH32RVUsbDeviceHidKeyboard::write(const char *text, uint32_t interKeyDelayMs)
{
    if (!text) {
        return false;
    }
    for (const char *p = text; *p; p++) {
        if (!tapKey(*p)) {
            return false;
        }
        if (interKeyDelayMs) {
            delay(interKeyDelayMs);
        }
    }
    return true;
}

bool CH32RVUsbDeviceHidKeyboard::releaseUsage(uint8_t usage, uint32_t timeoutMs)
{
    if (_nkro) {
        if (!_nkroState.release(usage)) {
            return false;
        }
        return sendNkroReport(timeoutMs);
    }
    for (uint8_t &k : _report.keys) {
        if (k == usage) {
            k = 0;
        }
    }
    return sendReport(_report, timeoutMs);
}

bool CH32RVUsbDeviceHidKeyboard::releaseAll(uint32_t timeoutMs)
{
    if (_nkro) {
        _nkroState.clear();
        return sendNkroReport(timeoutMs);
    }
    _report = CH32RVUsbDeviceBootKeyboardReport();
    return sendReport(_report, timeoutMs);
}

void CH32RVUsbDeviceHidKeyboard::enableNkro(bool enable)
{
    _nkro = enable;
    if (!enable) {
        _nkroState.clear();
    }
}

CH32RVUsbDeviceHidKeyboardOutputReport CH32RVUsbDeviceHidKeyboard::ledState() const
{
    CH32RVUsbDeviceHidKeyboardOutputReport report;
    report.setLeds(_leds);
    return report;
}

void CH32RVUsbDeviceHidKeyboard::onHidSetReport(uint8_t reportId, uint8_t reportType,
                                                const uint8_t *data, uint16_t length)
{
    (void)reportId;
    if (reportType != CH32RV_USB_DEVICE_HID_REPORT_TYPE_OUTPUT || !data || length < 1) {
        return;
    }
    _leds = data[0];
    _ledsPending = true;
}

void CH32RVUsbDeviceHidKeyboard::onHidSetProtocol(uint8_t protocol)
{
    _protocol = protocol;
    _protocolPending = true;
}

size_t CH32RVUsbDeviceHidKeyboard::hidDeliver()
{
    size_t ran = 0;
    if (_ledsPending) {
        _ledsPending = false;
        if (_outputCallback) {
            _outputCallback(ledState());
            ran++;
        }
    }
    if (_protocolPending) {
        _protocolPending = false;
        if (_protocolCallback) {
            CH32RVUsbDeviceHidProtocolEvent event;
            event.instance = 0;
            event.protocol = _protocol;
            _protocolCallback(event);
            ran++;
        }
    }
    return ran;
}

/* The host knows nothing is held and has set no LEDs: a chord surviving a
 * replug would never be released. */
void CH32RVUsbDeviceHidKeyboard::onBusAttached()
{
    _leds = 0;
    _protocol = 1;
    _nkroState.clear();
    _report = CH32RVUsbDeviceBootKeyboardReport();
}

void CH32RVUsbDeviceHidKeyboard::onBusDetached()
{
    onBusAttached();
}

/* Reverse lookup in the layout's table (EspUsbDevice's espUsbDeviceAsciiToUsage):
 * base and Shift first, AltGr (Right Alt) only for what those cannot reach. */
bool CH32RVUsbDeviceHidKeyboard::asciiToUsage(char key, uint8_t &usage, uint8_t &modifiers) const
{
    const CH32RVUsbDeviceKeyboardLayout &l = layout();
    const uint16_t c = (uint8_t)key;
    usage = 0;
    modifiers = 0;
    for (uint16_t k = 0; k < l.entries; k++) {
        if (l.table[k][0] == c) {
            usage = (uint8_t)k;
            return true;
        }
        if (l.table[k][1] == c) {
            usage = (uint8_t)k;
            modifiers = CH32RV_USB_DEVICE_MOD_LEFT_SHIFT;
            return true;
        }
    }
    for (uint16_t k = 0; k < l.entries; k++) {
        if (l.table[k][2] == c) {
            usage = (uint8_t)k;
            modifiers = CH32RV_USB_DEVICE_MOD_RIGHT_ALT;
            return true;
        }
        if (l.table[k][3] == c) {
            usage = (uint8_t)k;
            modifiers = CH32RV_USB_DEVICE_MOD_RIGHT_ALT | CH32RV_USB_DEVICE_MOD_LEFT_SHIFT;
            return true;
        }
    }
    return false;
}

/* ---------------------------------------------------------- mouse */
CH32RVUsbDeviceHidMouse::CH32RVUsbDeviceHidMouse(CH32RVUsbDevice &device) : CH32RVUsbDeviceHidClass(device)
{
}

const uint8_t *CH32RVUsbDeviceHidMouse::hidReportDescriptor() const
{
    return MOUSE_REPORT_DESCRIPTOR;
}

uint16_t CH32RVUsbDeviceHidMouse::hidReportDescriptorLength() const
{
    return sizeof MOUSE_REPORT_DESCRIPTOR;
}

bool CH32RVUsbDeviceHidMouse::sendReport(const CH32RVUsbDeviceBootMouseReport &report, uint32_t timeoutMs)
{
    return sendHidReport(classReportId(), &report, sizeof report, timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::sendState(uint32_t timeoutMs)
{
    CH32RVUsbDeviceBootMouseReport report;
    report.buttons = _buttons;
    return sendReport(report, timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::move(int8_t x, int8_t y, int8_t wheel, uint8_t buttons, uint32_t timeoutMs)
{
    _buttons = buttons;
    CH32RVUsbDeviceBootMouseReport report;
    report.buttons = _buttons;
    report.x = x;
    report.y = y;
    report.wheel = wheel;
    return sendReport(report, timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::wheel(int8_t wheel, uint32_t timeoutMs)
{
    return move(0, 0, wheel, _buttons, timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::press(uint8_t buttons, uint32_t timeoutMs)
{
    _buttons |= buttons;
    return sendState(timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::release(uint8_t buttons, uint32_t timeoutMs)
{
    _buttons &= (uint8_t)~buttons;
    return sendState(timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::releaseAll(uint32_t timeoutMs)
{
    _buttons = 0;
    return sendState(timeoutMs);
}

bool CH32RVUsbDeviceHidMouse::click(uint8_t button, uint32_t holdMs)
{
    if (!press(button)) {
        return false;
    }
    delay(holdMs);
    return release(button);
}

/* ---------------------------------------------------------- custom */
CH32RVUsbDeviceHidCustom::CH32RVUsbDeviceHidCustom(CH32RVUsbDevice &device, const uint8_t *reportDescriptor,
                                                   uint16_t reportDescriptorLength, uint16_t inputReportSize)
    : CH32RVUsbDeviceHidClass(device), _desc(reportDescriptor), _descLength(reportDescriptorLength),
      _inputReportSize(inputReportSize)
{
}

bool CH32RVUsbDeviceHidCustom::sendReport(const void *data, size_t length, uint8_t reportId,
                                          uint32_t timeoutMs)
{
    return sendHidReport(reportId, data, length, timeoutMs);
}

/* ---------------------------------------------------------- vendor */
CH32RVUsbDeviceHidVendor::CH32RVUsbDeviceHidVendor(CH32RVUsbDevice &device, uint16_t reportSize)
    : CH32RVUsbDeviceHidClass(device), _reportSize(reportSize)
{
    if (_reportSize == 0 || _reportSize > maxReportSize()) {
        _reportSize = maxReportSize();
    }
    build();
}

void CH32RVUsbDeviceHidVendor::setUsage(uint16_t usagePage, uint8_t usage)
{
    _usagePage = usagePage;
    _usage = usage;
    build();
}

void CH32RVUsbDeviceHidVendor::build()
{
    uint8_t *d = _desc;
    uint8_t at = 0;
    const uint8_t head[] = {
        0x06, (uint8_t)_usagePage, (uint8_t)(_usagePage >> 8),   /* Usage Page (vendor 0xff00 by default) */
        0x09, _usage, 0xa1, 0x01,                   /* Usage, Collection (Application) */
        0x85, CH32RV_USB_DEVICE_HID_REPORT_ID_VENDOR,
        0x15, 0x00, 0x26, 0xff, 0x00, 0x75, 0x08,   /* 0..255, 8 bits */
    };
    memcpy(d, head, sizeof head);
    at = sizeof head;
    if (_reportSize > 255) {
        d[at++] = 0x96;                             /* Report Count, 2 bytes */
        d[at++] = (uint8_t)_reportSize;
        d[at++] = (uint8_t)(_reportSize >> 8);
    } else {
        d[at++] = 0x95;                             /* Report Count */
        d[at++] = (uint8_t)_reportSize;
    }
    const uint8_t tail[] = {
        0x09, _usage, 0x81, 0x02,                   /* Input */
        0x09, _usage, 0x91, 0x02,                   /* Output */
        0x09, _usage, 0xb1, 0x02,                   /* Feature */
        0xc0,
    };
    memcpy(d + at, tail, sizeof tail);
    _descLength = (uint8_t)(at + sizeof tail);
}

uint16_t CH32RVUsbDeviceHidVendor::maxReportSize()
{
    return HID_EP_SIZE - 1;                       /* the report ID takes a byte */
}

uint16_t CH32RVUsbDeviceHidVendor::hidInEndpointSize() const
{
    return (uint16_t)(_reportSize + 1);
}

bool CH32RVUsbDeviceHidVendor::sendInput(const void *data, size_t length, uint32_t timeoutMs)
{
    if (length > _reportSize) {
        length = _reportSize;
    }
    return sendHidReport(CH32RV_USB_DEVICE_HID_REPORT_ID_VENDOR, data, length, timeoutMs);
}

void CH32RVUsbDeviceHidVendor::onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data,
                                              uint16_t length)
{
    const bool wanted = (reportType == CH32RV_USB_DEVICE_HID_REPORT_TYPE_OUTPUT && _outputCallback) ||
                        (reportType == CH32RV_USB_DEVICE_HID_REPORT_TYPE_FEATURE && _featureCallback);
    if (!wanted) {
        return;
    }
    if (_heldLength >= 0) {
        _dropped++;
        return;
    }
    if (length > sizeof _held) {
        length = sizeof _held;
    }
    memcpy(_held, data, length);
    _heldLength = (int16_t)length;
    _heldId = reportId;
    _heldType = reportType;
}

size_t CH32RVUsbDeviceHidVendor::hidDeliver()
{
    if (_heldLength < 0) {
        return 0;
    }
    CH32RVUsbDeviceHidReport report;
    report.reportId = _heldId;
    report.reportType = _heldType;
    report.data = _held;
    report.length = (uint16_t)_heldLength;
    const ReportCallback &cb = _heldType == CH32RV_USB_DEVICE_HID_REPORT_TYPE_OUTPUT ? _outputCallback
                                                                                      : _featureCallback;
    size_t ran = 0;
    if (cb) {
        cb(report);
        ran = 1;
    }
    _heldLength = -1;                            /* free once the call returns */
    return ran;
}

/* ---------------------------------------------------------- gamepad */
CH32RVUsbDeviceHidGamepad::CH32RVUsbDeviceHidGamepad(CH32RVUsbDevice &device) : CH32RVUsbDeviceHidClass(device)
{
}

const uint8_t *CH32RVUsbDeviceHidGamepad::hidReportDescriptor() const
{
    return GAMEPAD_REPORT_DESCRIPTOR;
}

uint16_t CH32RVUsbDeviceHidGamepad::hidReportDescriptorLength() const
{
    return sizeof GAMEPAD_REPORT_DESCRIPTOR;
}

bool CH32RVUsbDeviceHidGamepad::sendReport(const CH32RVUsbDeviceGamepadReport &report, uint32_t timeoutMs)
{
    _report = report;
    const uint8_t data[11] = {
        (uint8_t)report.x, (uint8_t)report.y, (uint8_t)report.z, (uint8_t)report.rz,
        (uint8_t)report.rx, (uint8_t)report.ry, report.hat,
        (uint8_t)(report.buttons & 0xff), (uint8_t)((report.buttons >> 8) & 0xff),
        (uint8_t)((report.buttons >> 16) & 0xff), (uint8_t)((report.buttons >> 24) & 0xff),
    };
    return sendHidReport(classReportId(), data, sizeof data, timeoutMs);
}

bool CH32RVUsbDeviceHidGamepad::send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry,
                                     uint8_t hat, uint32_t buttons, uint32_t timeoutMs)
{
    CH32RVUsbDeviceGamepadReport report;
    report.x = x;
    report.y = y;
    report.z = z;
    report.rz = rz;
    report.rx = rx;
    report.ry = ry;
    report.hat = hat;
    report.buttons = buttons;
    return sendReport(report, timeoutMs);
}

bool CH32RVUsbDeviceHidGamepad::releaseAll(uint32_t timeoutMs)
{
    return sendReport(CH32RVUsbDeviceGamepadReport(), timeoutMs);
}

/* ---------------------------------------------------------- consumer control */
CH32RVUsbDeviceHidConsumerControl::CH32RVUsbDeviceHidConsumerControl(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHidClass(device)
{
}

const uint8_t *CH32RVUsbDeviceHidConsumerControl::hidReportDescriptor() const
{
    return CONSUMER_CONTROL_REPORT_DESCRIPTOR;
}

uint16_t CH32RVUsbDeviceHidConsumerControl::hidReportDescriptorLength() const
{
    return sizeof CONSUMER_CONTROL_REPORT_DESCRIPTOR;
}

bool CH32RVUsbDeviceHidConsumerControl::sendUsage(uint16_t usage, uint32_t timeoutMs)
{
    _usage = usage;
    const uint8_t report[2] = {(uint8_t)(usage & 0xff), (uint8_t)(usage >> 8)};
    return sendHidReport(classReportId(), report, sizeof report, timeoutMs);
}

bool CH32RVUsbDeviceHidConsumerControl::press(uint16_t usage, uint32_t timeoutMs)
{
    return sendUsage(usage, timeoutMs);
}

bool CH32RVUsbDeviceHidConsumerControl::release(uint32_t timeoutMs)
{
    return sendUsage(0, timeoutMs);
}

bool CH32RVUsbDeviceHidConsumerControl::click(uint16_t usage, uint32_t holdMs)
{
    if (!press(usage)) {
        return false;
    }
    delay(holdMs);
    return release();
}

/* ---------------------------------------------------------- system control */
CH32RVUsbDeviceHidSystemControl::CH32RVUsbDeviceHidSystemControl(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHidClass(device)
{
}

const uint8_t *CH32RVUsbDeviceHidSystemControl::hidReportDescriptor() const
{
    return SYSTEM_CONTROL_REPORT_DESCRIPTOR;
}

uint16_t CH32RVUsbDeviceHidSystemControl::hidReportDescriptorLength() const
{
    return sizeof SYSTEM_CONTROL_REPORT_DESCRIPTOR;
}

bool CH32RVUsbDeviceHidSystemControl::sendUsage(uint8_t usage, uint32_t timeoutMs)
{
    _usage = usage;
    return sendHidReport(classReportId(), &_usage, 1, timeoutMs);
}

bool CH32RVUsbDeviceHidSystemControl::press(uint8_t usage, uint32_t timeoutMs)
{
    return sendUsage(usage, timeoutMs);
}

bool CH32RVUsbDeviceHidSystemControl::release(uint32_t timeoutMs)
{
    return sendUsage(0, timeoutMs);
}

bool CH32RVUsbDeviceHidSystemControl::click(uint8_t usage, uint32_t holdMs)
{
    if (!press(usage)) {
        return false;
    }
    delay(holdMs);
    return release();
}
