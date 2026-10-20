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

const uint8_t *CH32RVUsbDevice::bosDescriptor() const
{
    return ch32rv_usb_platform.bosLength ? ch32rv_usb_platform.bos : nullptr;
}

uint16_t CH32RVUsbDevice::bosDescriptorLength() const
{
    return ch32rv_usb_platform.bosLength;
}

const uint8_t *CH32RVUsbDevice::microsoftOs20Descriptor() const
{
    return ch32rv_usb_platform.msOs20Length ? ch32rv_usb_platform.msOs20 : nullptr;
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
    if (_count >= CH32RV_USB_MAX_FUNCTIONS) {
        _error = CH32RVUsbDeviceError::TooManyFunctions;
        return false;
    }
    /* The instances of a class are numbered in attach order. */
    uint8_t same = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (&_functions[i]->_type == &f._type) {
            same++;
        }
    }
    if (same >= f._type.maxInstances) {
        _error = CH32RVUsbDeviceError::TooManyInstances;
        return false;
    }
    f._instance = same;
    f._attached = true;
    _functions[_count++] = &f;
    return true;
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
    case CH32RVUsbDeviceError::TooManyFunctions: return "TooManyFunctions";
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
    if (_started) {
        return true;
    }
    return begin(CH32RVUsbDeviceConfig());
}

/* The configuration descriptor for one speed, into `out`: the functions in
 * declaration order, numbering interfaces and endpoints as they come. 0, with
 * lastError() set, when it does not fit or needs more endpoints than the
 * controller has (`endpoints`: numbers 0..endpoints-1). `type` is
 * CONFIGURATION (2), or OTHER_SPEED_CONFIGURATION (7) for a high-speed
 * device's other-speed descriptor. */
uint16_t CH32RVUsbDevice::buildConfiguration(uint8_t *out, uint16_t room, CH32RVUsbSpeed speed, uint8_t type)
{
    if (room < 9) {
        _error = CH32RVUsbDeviceError::DescriptorTooLarge;
        return 0;
    }
    uint16_t len = 9;
    uint8_t itf = 0, ep = 1;
    for (uint8_t i = 0; i < _count; i++) {
        CH32RVUsbDeviceFunction &f = *_functions[i];
        f._interface = itf;
        const uint16_t n = f._type.descriptor(f, out + len, (uint16_t)(room - len), itf, &ep,
                                              (uint8_t)(CH32RV_USB_FIRST_FUNCTION_STRING + i), speed);
        if (n == 0) {
            _error = CH32RVUsbDeviceError::DescriptorTooLarge;
            return 0;
        }
        if (ep > _endpoints) {
            _error = CH32RVUsbDeviceError::OutOfEndpoints;
            return 0;
        }
        len = (uint16_t)(len + n);
        itf = (uint8_t)(itf + f._type.interfaces);
    }
    /* SuperSpeed: an endpoint companion after every endpoint (USB 3.2 9.6.7):
     * no bursts, no streams; a periodic endpoint moves one packet per
     * interval. */
    if (ch32rv_usb_at(speed, CH32RVUsbSpeed::Super)) {
        for (uint16_t at = 9; at + 1 < len && out[at]; at = (uint16_t)(at + out[at])) {
            if (out[at + 1] != 5) {
                continue;
            }
            if ((uint16_t)(len + 6) > room) {
                _error = CH32RVUsbDeviceError::DescriptorTooLarge;
                return 0;
            }
            const uint16_t after = (uint16_t)(at + out[at]);
            const bool periodic = (out[at + 3] & 1) != 0;      /* isochronous or interrupt */
            const uint16_t mps = (uint16_t)(out[at + 4] | out[at + 5] << 8);
            memmove(out + after + 6, out + after, (size_t)(len - after));
            const uint8_t companion[6] = {6, 0x30, 0, 0, (uint8_t)(periodic ? mps : 0),
                                          (uint8_t)(periodic ? mps >> 8 : 0)};
            memcpy(out + after, companion, 6);
            len = (uint16_t)(len + 6);
            at = after;                                        /* the companion is skipped next */
        }
    }
    /* The header. Bit 7 of bmAttributes is reserved and set, bit 5 offers
     * remote wakeup, bit 6 says self-powered; bMaxPower counts 2 mA units. */
    out[0] = 9;
    out[1] = type;
    out[2] = (uint8_t)len;
    out[3] = (uint8_t)(len >> 8);
    out[4] = itf;
    out[5] = 1;
    out[6] = 0;
    out[7] = (uint8_t)(0x80u | 0x20u | (_config.selfPowered ? 0x40u : 0u));
    out[8] = (uint8_t)(_config.maxPowerMilliamps > 500 ? 250 : _config.maxPowerMilliamps / 2);
    return len;
}
