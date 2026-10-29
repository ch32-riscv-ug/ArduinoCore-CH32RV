/* The device: the functions attached to it and the configuration descriptor
 * laid out from them. The running stack (requests, transfers, the controller)
 * is CH32RVUsbDeviceCore.cpp. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

/* ---------------------------------------------------------- functions */
CH32RVUsbDeviceFunction::CH32RVUsbDeviceFunction(CH32RVUsbDevice &device,
                                                 const CH32RVUsbFunctionType &type,
                                                 const char *name)
    : _device(device), _type(type), _name(name)
{
    device.attach(*this);
}

/* The config names the interfaces a sketch has no object for. */
const char *CH32RVUsbDeviceFunction::interfaceName() const
{
    const char *name = _type.configName ? _device.config().*(_type.configName) : nullptr;
    return name ? name : _name;
}

const char *(*ch32rv_usb_extra_string)(uint8_t index);

/* Filled by CH32RVUsbDevicePlatform.cpp, when linked. */
CH32RVUsbPlatform ch32rv_usb_platform;

/* For a sketch that looks: the descriptors are not kept, so these produce
 * them into a buffer of their own - linked only when called. */
const uint8_t *CH32RVUsbDevice::bosDescriptor() const
{
    static uint8_t buf[5 + 24 + 28];
    const CH32RVUsbPlatform &p = ch32rv_usb_platform;
    if (!p.bosLength || p.bosLength > sizeof buf) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter w;
    w.out = buf;
    w.room = sizeof buf;
    p.bosWrite(w);
    return buf;
}

uint16_t CH32RVUsbDevice::bosDescriptorLength() const
{
    return ch32rv_usb_platform.bosLength;
}

/* Up to 512 bytes (four functions with DeviceInterfaceGUIDs); longer sets are
 * still served to the host, only not shown here. */
const uint8_t *CH32RVUsbDevice::microsoftOs20Descriptor() const
{
    static uint8_t buf[512];
    const CH32RVUsbPlatform &p = ch32rv_usb_platform;
    if (!p.msOs20Length || p.msOs20Length > sizeof buf) {
        return nullptr;
    }
    CH32RVUsbDescriptorWriter w;
    w.out = buf;
    w.room = sizeof buf;
    p.msOs20Write(w);
    return buf;
}

uint16_t CH32RVUsbDevice::microsoftOs20DescriptorLength() const
{
    return ch32rv_usb_platform.msOs20Length;
}

uint16_t CH32RVUsbDevice::microsoftOs20VendorRevision() const
{
    return ch32rv_usb_platform.msOs20Revision;
}

bool CH32RVUsbDevice::microsoftOs20UsesSubsets() const
{
    return ch32rv_usb_platform.msOs20Length && ch32rv_usb_platform.msOs20Subsets;
}

bool CH32RVUsbDevice::attach(CH32RVUsbDeviceFunction &f)
{
    if (_started) {
        _error = CH32RVUsbDeviceError::AlreadyStarted;
        return false;
    }
    /* The instances of a class are numbered in attach order. */
    uint8_t same = 0;
    CH32RVUsbDeviceFunction **at = &_first;
    for (; *at; at = &(*at)->_next) {
        if (*at == &f) {
            return true;
        }
        if (&(*at)->_type == &f._type) {
            same++;
        }
    }
    if (same >= f._type.maxInstances) {
        _error = CH32RVUsbDeviceError::TooManyInstances;
        return false;
    }
    f._instance = same;
    f._attached = true;
    f._next = nullptr;
    *at = &f;
    _count++;
    return true;
}

CH32RVUsbDeviceFunction *CH32RVUsbDevice::function(uint8_t i) const
{
    CH32RVUsbDeviceFunction *f = _first;
    while (f && i--) {
        f = f->_next;
    }
    return f;
}

const char *CH32RVUsbDevice::lastErrorName() const
{
    switch (_error) {
    case CH32RVUsbDeviceError::None: return "None";
    case CH32RVUsbDeviceError::Unsupported: return "Unsupported";
    case CH32RVUsbDeviceError::PortUnavailable: return "PortUnavailable";
    case CH32RVUsbDeviceError::NotStarted: return "NotStarted";
    case CH32RVUsbDeviceError::NotReady: return "NotReady";
    case CH32RVUsbDeviceError::AlreadyStarted: return "AlreadyStarted";
    case CH32RVUsbDeviceError::TooManyInstances: return "TooManyInstances";
    case CH32RVUsbDeviceError::OutOfEndpoints: return "OutOfEndpoints";
    case CH32RVUsbDeviceError::DescriptorTooLarge: return "DescriptorTooLarge";
    case CH32RVUsbDeviceError::InvalidArgument: return "InvalidArgument";
    case CH32RVUsbDeviceError::Busy: return "Busy";
    }
    return "?";
}

bool CH32RVUsbDevice::begin()
{
    return begin(CH32RVUsbDeviceConfig());
}

/* ---------------------------------------------------------- descriptors */
void CH32RVUsbDescriptorWriter::emit(uint8_t v)
{
    if (out && length >= skip && length - skip < room) {
        out[length - skip] = v;
    }
    if (crc) {
        uint16_t c = (uint16_t)(*crc ^ (v << 8));
        for (uint8_t b = 0; b < 8; b++) {
            c = (uint16_t)(c & 0x8000 ? (c << 1) ^ 0x1021 : c << 1);
        }
        *crc = c;
    }
    length++;
}

void CH32RVUsbDescriptorWriter::put(const uint8_t *data, uint16_t n)
{
    if (done) {
        return;
    }
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t b = data[i];
        emit(b);
        if (_left == 0) {
            _left = b ? b : 1;         /* a zero length would never end */
            _have = 0;
        }
        if (_have < sizeof _head) {
            _head[_have++] = b;
        }
        if (--_left) {
            continue;
        }
        if (visit) {
            visit(context, _head, _have);
            if (done) {
                return;
            }
        }
        if (companions && _have >= 6 && _head[1] == 5) {
            const bool periodic = (_head[3] & 1) != 0;      /* isochronous or interrupt */
            const bool bulk = (_head[3] & 3) == 2;
            const uint8_t c[6] = {6, 0x30, (uint8_t)(bulk ? CH32RV_USB_SS_BULK_BURST - 1u : 0), 0,
                                  (uint8_t)(periodic ? _head[4] : 0), (uint8_t)(periodic ? _head[5] : 0)};
            for (uint8_t k = 0; k < sizeof c; k++) {
                emit(c[k]);
            }
        }
    }
}

void CH32RVUsbDescriptorWriter::u8(uint8_t v)
{
    put(&v, 1);
}

void CH32RVUsbDescriptorWriter::u16(uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    put(b, 2);
}

void CH32RVUsbDescriptorWriter::u24(uint32_t v)
{
    const uint8_t b[3] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16)};
    put(b, 3);
}

void CH32RVUsbDescriptorWriter::u32(uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    put(b, 4);
}

namespace {
/* The functions' part of the configuration, interfaces and endpoints
 * numbered as they come. */
struct Layout {
    uint8_t interfaces;
    bool ok;
};
}  // namespace

/* The configuration descriptor for one speed: the functions in declaration
 * order. Counted first - the header carries the total and the interface
 * count - then written. 0, with lastError() set, when the functions need more
 * endpoints than the controller has (`_endpoints`: numbers 0.._endpoints-1)
 * or one cannot be described. `type` is CONFIGURATION (2), or
 * OTHER_SPEED_CONFIGURATION (7) for a high-speed device's other-speed
 * descriptor. */
uint16_t CH32RVUsbDevice::buildConfiguration(CH32RVUsbDescriptorWriter &w, CH32RVUsbSpeed speed, uint8_t type)
{
    const bool companions = ch32rv_usb_at(speed, CH32RVUsbSpeed::Super);
    auto functions = [&](CH32RVUsbDescriptorWriter &to) -> Layout {
        uint8_t itf = 0, ep = 1, i = 0;
        for (CH32RVUsbDeviceFunction *f = _first; f; f = f->_next, i++) {
            f->_interface = itf;
            if (!f->_type.descriptor(*f, to, itf, &ep, (uint8_t)(CH32RV_USB_FIRST_FUNCTION_STRING + i), speed)) {
                _error = CH32RVUsbDeviceError::DescriptorTooLarge;
                return {itf, false};
            }
            if (ep > _endpoints) {
                _error = CH32RVUsbDeviceError::OutOfEndpoints;
                return {itf, false};
            }
            itf = (uint8_t)(itf + f->_type.interfaces);
        }
        return {itf, true};
    };
    if (w.visit) {                 /* walking: no header totals needed */
        const uint8_t head[9] = {9, type};
        w.companions = companions;
        w.put(head, sizeof head);
        return functions(w).ok ? 9 : 0;
    }
    CH32RVUsbDescriptorWriter count;
    count.companions = companions;
    const Layout l = functions(count);
    const uint32_t len = 9 + count.length;
    if (!l.ok || len > 0xFFFFu) {
        if (l.ok) {
            _error = CH32RVUsbDeviceError::DescriptorTooLarge;
        }
        return 0;
    }
    /* The header. Bit 7 of bmAttributes is reserved and set, bit 5 offers
     * remote wakeup, bit 6 says self-powered; bMaxPower counts 2 mA units. */
    const uint8_t head[9] = {9, type, (uint8_t)len, (uint8_t)(len >> 8), l.interfaces, 1, 0,
                             (uint8_t)(0x80u | (_config.remoteWakeupSupported ? 0x20u : 0u) | (_config.selfPowered ? 0x40u : 0u)),
                             (uint8_t)(_config.maxPowerMilliamps > 500 ? 250 : _config.maxPowerMilliamps / 2)};
    w.companions = companions;
    w.put(head, sizeof head);
    functions(w);
    return (uint16_t)len;
}
