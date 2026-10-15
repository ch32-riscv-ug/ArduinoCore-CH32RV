/* The device: the functions attached to it, the descriptors laid out from
 * them, the chip side of TinyUSB (clocks and PHY, the interrupt, the task
 * hook), and the reset into the factory USB bootloader. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>

#include "ch32rv_registers.h"
#include "tusb.h"
#include "device/usbd_pvt.h"

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
    /* TinyUSB numbers the instances of a class in interface order, which is
     * attach order here. */
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

#if CH32RV_TINYUSB_DEVICE

#include "CH32RVUsbDevicePort.h"

/* ---------------------------------------------------------- the running device
 * TinyUSB runs one device stack, and its callbacks are plain functions, so the
 * device that began is kept here. */
static CH32RVUsbDevice *active;
static usbd_class_driver_t drivers[CH32RV_USB_MAX_FUNCTIONS];
static volatile bool bootloader_requested;

/* The configuration descriptor is built here. 512 bytes hold a UAC1 headset
 * (191) + a UVC camera (158) + CDC (66) + HID (32) + DFU runtime (18) + the
 * header (9); beyond that begin() fails with DescriptorTooLarge, and
 * build_opt.h can raise (or, on a tight part, lower) this. */
#ifndef CH32RV_USB_CONFIG_MAX
#define CH32RV_USB_CONFIG_MAX 512
#endif
static uint8_t desc_configuration[CH32RV_USB_CONFIG_MAX];

extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count)
{
    *driver_count = active ? active->functionCount() : 0;
    return drivers;
}

/* String index of function i: the device's own are 1..3. */
#define FIRST_FUNCTION_STRING 4

bool CH32RVUsbDevice::begin(const CH32RVUsbDeviceConfig &config)
{
    if (_started) {
        return true;
    }
#ifndef CH32RV_USB_NO_DEFAULT_DFU
    if (config.dfuRuntime) {
        /* Best effort: a refusal here (no room) is not the sketch's error. */
        const CH32RVUsbDeviceError before = _error;
        CH32RVUsbDeviceDfuRuntime::attachDefault(*this);
        _error = before;
    }
#endif
    /* Lay the configuration out once for each speed the controller can run
     * at, so a function that does not fit fails here rather than at
     * enumeration. The descriptor itself is rebuilt for the speed the host
     * chose whenever it asks (tud_descriptor_configuration_cb). */
    _config = config;
    /* The controller first: which one decides the speed, and so the
     * descriptors. */
#if CH32RV_USB_SHARED_IRQ
    if (ch32rv_usbfs_irq_hook && ch32rv_usbfs_irq_hook != port_irq) {
        _error = CH32RVUsbDeviceError::Busy;
        return false;
    }
#endif
    const CH32RVUsbDeviceError port = port_open(config.controller);
    if (port != CH32RVUsbDeviceError::None) {
        _error = port;
        return false;
    }
    for (uint8_t i = 0; i < _count; i++) {
        drivers[i] = *(const usbd_class_driver_t *)_functions[i]->_type.driver;
    }
    for (uint8_t hs = 0; hs <= (port_high_speed() ? 1 : 0); hs++) {
        if (!buildConfiguration(desc_configuration, sizeof desc_configuration, hs, TUSB_DESC_CONFIGURATION)) {
            port_close();
            return false;
        }
    }
    _config = config;
    /* The BOS and the Microsoft OS 2.0 set need the interface numbers. */
    if (ch32rv_usb_platform.build && !ch32rv_usb_platform.build(*this)) {
        _error = CH32RVUsbDeviceError::InvalidArgument;
        port_close();
        return false;
    }
    active = this;
#if CH32RV_USB_SHARED_IRQ
    ch32rv_usbfs_irq_hook = port_irq;
#endif
    tusb_rhport_init_t init = {};
    init.role = TUSB_ROLE_DEVICE;
    init.speed = port_high_speed() ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL;
    /* tud_rhport_init, not tusb_init: tusb.c also refers to the host stack,
     * which would pull usbh and the HCD into every device sketch. */
    tud_rhport_init(0, &init);
    _started = true;
    _error = CH32RVUsbDeviceError::None;
    return true;
}

/* The configuration descriptor for one speed, into `out`: the functions in
 * declaration order, numbering interfaces and endpoints as they come. 0, with
 * lastError() set, when it does not fit or needs more endpoints than the
 * controller has. `type` is CONFIGURATION, or OTHER_SPEED_CONFIGURATION for
 * a high-speed device's other-speed descriptor. */
uint16_t CH32RVUsbDevice::buildConfiguration(uint8_t *out, uint16_t room, bool hs, uint8_t type)
{
    uint16_t len = TUD_CONFIG_DESC_LEN;
    uint8_t itf = 0, ep = 1;
    for (uint8_t i = 0; i < _count; i++) {
        CH32RVUsbDeviceFunction &f = *_functions[i];
        f._interface = itf;
        const uint16_t n = f._type.descriptor(f, out + len, (uint16_t)(room - len), itf, &ep,
                                              (uint8_t)(FIRST_FUNCTION_STRING + i), hs);
        if (n == 0) {
            _error = CH32RVUsbDeviceError::DescriptorTooLarge;
            return 0;
        }
        if (ep > port_last_endpoint() + 1u) {
            _error = CH32RVUsbDeviceError::OutOfEndpoints;
            return 0;
        }
        len = (uint16_t)(len + n);
        itf = (uint8_t)(itf + f._type.interfaces);
    }
    /* The header (TUD_CONFIG_DESCRIPTOR, written out because its fields are
     * run-time values). Bit 7 of bmAttributes is reserved and set; bMaxPower
     * counts 2 mA units. */
    out[0] = TUD_CONFIG_DESC_LEN;
    out[1] = type;
    out[2] = (uint8_t)len;
    out[3] = (uint8_t)(len >> 8);
    out[4] = itf;
    out[5] = 1;
    out[6] = 0;
    out[7] = (uint8_t)(0x80u | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP |
                       (_config.selfPowered ? TUSB_DESC_CONFIG_ATT_SELF_POWERED : 0u));
    out[8] = (uint8_t)(_config.maxPowerMilliamps > 500 ? 250 : _config.maxPowerMilliamps / 2);
    return len;
}

void CH32RVUsbDevice::end()
{
    if (!_started) {
        return;
    }
    tud_disconnect();
    tud_deinit(0);
    port_close();
#if CH32RV_USB_SHARED_IRQ
    ch32rv_usbfs_irq_hook = nullptr;
#endif
    _started = false;
}

bool CH32RVUsbDevice::mounted() const
{
    return _started && tud_mounted();
}

uint8_t CH32RVUsbDevice::interfaceCount() const
{
    return desc_configuration[4];
}

bool CH32RVUsbDevice::suspended() const
{
    return _started && tud_suspended();
}

bool CH32RVUsbDevice::remoteWakeup()
{
    return _started && tud_remote_wakeup();
}

void CH32RVUsbDevice::requestBootloader()
{
    bootloader_requested = true;
}

void CH32RVUsbDevice::task()
{
    ch32rv_usb_task();
}

uint8_t CH32RVUsbDevice::maxCdcPorts()
{
    return CFG_TUD_CDC;
}

bool CH32RVUsbDevice::sendHidReport(uint8_t instance, uint8_t reportId, const void *data,
                                    size_t length, uint32_t timeoutMs)
{
    if (!_started) {
        _error = CH32RVUsbDeviceError::NotStarted;
        return false;
    }
    const uint32_t t0 = millis();
    for (;;) {
        ch32rv_usb_task();
        if (tud_hid_n_ready(instance)) {
            break;
        }
        if (!tud_mounted() || (uint32_t)(millis() - t0) >= timeoutMs) {
            _error = CH32RVUsbDeviceError::NotReady;
            return false;
        }
    }
    return tud_hid_n_report(instance, reportId, data, (uint16_t)length);
}

[[noreturn]] void CH32RVUsbDevice::enterBootloader()
{
    /* Leave the bus first so the host sees the device go away now, not at
     * the next transfer timeout; the bootloader then enumerates as 1a86:55e0. */
    if (active && active->started()) {
        tud_disconnect();
        port_close();
    }
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 50u) {
    }

    noInterrupts();
#ifdef CH32RV_USB_FACTORY_ISP_ENTRY
    port_set_boot_mode();
#endif
    CH32RV_PFIC_CFGR = CH32RV_PFIC_KEY3 | CH32RV_PFIC_SYSRST;
    for (;;) {
    }
}

/* main() and delay() call this (Arduino.h weak hook); defining it here means
 * a sketch without a device links none of it. Work happens only when the
 * controller raised an event - calling tud_task() on every loop() costs about
 * 10 us at 48 MHz even when idle (measured on X035: loop() 428k/s without
 * USB, 78k/s polling, 207k/s with this check). A callback that calls delay()
 * would re-enter tud_task(); the guard makes that a no-op instead. */
extern "C" void ch32rv_usb_task(void)
{
    static volatile bool busy;
    if (!active || !active->started() || busy) {
        return;
    }
    /* Leave only once the reply to the request that asked for it is out: the
     * status stage of a DFU_DETACH, the host's close after a 1200 bps touch.
     * Leaving at once makes the host see the request fail. */
    static uint32_t leave_at;
    if (bootloader_requested) {
        if (!leave_at) {
            leave_at = millis() | 1u;
        } else if ((uint32_t)(millis() - leave_at) >= 20u) {
            CH32RVUsbDevice::enterBootloader();
        }
    }
    if (!tud_task_event_ready()) {
        return;
    }
    busy = true;
    tud_task();
    busy = false;
}

/* tusb_time_millis_api(): CH32RVTinyUSB's ch32rv_port.c, shared with the host. */

/* ---------------------------------------------------------- descriptors */
static tusb_desc_device_t desc_device;

extern "C" const uint8_t *tud_descriptor_device_cb(void)
{
    const CH32RVUsbDeviceConfig &c = active->config();
    desc_device.bLength = sizeof(tusb_desc_device_t);
    desc_device.bDescriptorType = TUSB_DESC_DEVICE;
    /* 2.10 when there is a BOS (CH32RVUsbDeviceDfuRuntime's, for Windows):
     * a host reads the BOS only from a 2.1 device. */
    desc_device.bcdUSB = tud_descriptor_bos_cb() ? 0x0210 : 0x0200;
    /* IAD: CDC and MIDI are two interfaces bound together. */
    desc_device.bDeviceClass = TUSB_CLASS_MISC;
    desc_device.bDeviceSubClass = MISC_SUBCLASS_COMMON;
    desc_device.bDeviceProtocol = MISC_PROTOCOL_IAD;
    desc_device.bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE;
    desc_device.idVendor = c.vid;
    desc_device.idProduct = c.pid;
    desc_device.bcdDevice = c.deviceVersion;
    desc_device.iManufacturer = 1;
    desc_device.iProduct = 2;
    desc_device.iSerialNumber = 3;
    desc_device.bNumConfigurations = 1;
    return (const uint8_t *)&desc_device;
}

static bool running_high_speed(void)
{
#if TUD_OPT_HIGH_SPEED
    return tud_speed_get() == TUSB_SPEED_HIGH;
#else
    return false;
#endif
}

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    active->buildConfiguration(desc_configuration, sizeof desc_configuration, running_high_speed(),
                               TUSB_DESC_CONFIGURATION);
    return desc_configuration;
}

#if TUD_OPT_HIGH_SPEED
/* A high-speed capable device describes itself at the other speed too
 * (USB 2.0 9.6.2, 9.6.4); a full-speed only controller stalls both. */
extern "C" const uint8_t *tud_descriptor_other_speed_configuration_cb(uint8_t index)
{
    (void)index;
    if (!port_high_speed()) {
        return NULL;
    }
    active->buildConfiguration(desc_configuration, sizeof desc_configuration, !running_high_speed(),
                               TUSB_DESC_OTHER_SPEED_CONFIG);
    return desc_configuration;
}

extern "C" const uint8_t *tud_descriptor_device_qualifier_cb(void)
{
    static tusb_desc_device_qualifier_t q;
    if (!port_high_speed()) {
        return NULL;
    }
    const tusb_desc_device_t *d = (const tusb_desc_device_t *)tud_descriptor_device_cb();
    q.bLength = sizeof q;
    q.bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER;
    q.bcdUSB = d->bcdUSB;
    q.bDeviceClass = d->bDeviceClass;
    q.bDeviceSubClass = d->bDeviceSubClass;
    q.bDeviceProtocol = d->bDeviceProtocol;
    q.bMaxPacketSize0 = d->bMaxPacketSize0;
    q.bNumConfigurations = 1;
    q.bReserved = 0;
    return (const uint8_t *)&q;
}
#endif

/* String descriptors: UTF-16LE built on request, ASCII only. */
static uint16_t desc_str[1 + 32];

static uint8_t ascii_to_desc(const char *s)
{
    uint8_t n = 0;
    while (s && s[n] && n < 32) {
        desc_str[1 + n] = (uint8_t)s[n];
        n++;
    }
    return n;
}

extern "C" const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    const CH32RVUsbDeviceConfig &c = active->config();
    uint8_t n;
    if (index == 0) {
        desc_str[1] = 0x0409;
        n = 1;
    } else if (index == 1) {
        n = ascii_to_desc(c.manufacturer);
    } else if (index == 2) {
        n = ascii_to_desc(c.product);
    } else if (index == 3 && c.serialNumber) {
        n = ascii_to_desc(c.serialNumber);
    } else if (index == 3) {
        /* The chip's 64-bit unique ID in hex. */
        static const char hex[] = "0123456789ABCDEF";
        const uint32_t lo = *(volatile const uint32_t *)CH32RV_ESIG_UNIID1_ADDR;
        const uint32_t hi = *(volatile const uint32_t *)CH32RV_ESIG_UNIID2_ADDR;
        const uint64_t uid = ((uint64_t)hi << 32) | lo;
        for (n = 0; n < 16; n++) {
            desc_str[1 + n] = (uint8_t)hex[(uid >> (60 - 4 * n)) & 0xF];
        }
    } else if (index >= CH32RV_USB_EXTRA_STRING) {
        const char *text = ch32rv_usb_extra_string ? ch32rv_usb_extra_string(index) : nullptr;
        if (!text) {
            return NULL;
        }
        n = ascii_to_desc(text);
    } else {
        const CH32RVUsbDeviceFunction *f = active->function((uint8_t)(index - FIRST_FUNCTION_STRING));
        if (!f) {
            return NULL;
        }
        const char *name = f->interfaceName();
        if (!name) {
            return NULL;
        }
        n = ascii_to_desc(name);
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * n + 2));
    return desc_str;
}

#else /* !CH32RV_TINYUSB_DEVICE: no USB device on this series (tusb_config.h) */

bool CH32RVUsbDevice::begin(const CH32RVUsbDeviceConfig &config)
{
    (void)config;
    _error = CH32RVUsbDeviceError::Unsupported;
    return false;
}

void CH32RVUsbDevice::end()
{
}

bool CH32RVUsbDevice::mounted() const
{
    return false;
}

uint8_t CH32RVUsbDevice::interfaceCount() const
{
    return 0;
}

bool CH32RVUsbDevice::suspended() const
{
    return false;
}

bool CH32RVUsbDevice::remoteWakeup()
{
    return false;
}

void CH32RVUsbDevice::requestBootloader()
{
}

void CH32RVUsbDevice::task()
{
}

uint8_t CH32RVUsbDevice::maxCdcPorts()
{
    return 0;
}

bool CH32RVUsbDevice::sendHidReport(uint8_t, uint8_t, const void *, size_t, uint32_t)
{
    _error = CH32RVUsbDeviceError::Unsupported;
    return false;
}

/* No factory USB bootloader to go to: a plain system reset. */
[[noreturn]] void CH32RVUsbDevice::enterBootloader()
{
    noInterrupts();
    CH32RV_PFIC_CFGR = CH32RV_PFIC_KEY3 | CH32RV_PFIC_SYSRST;
    for (;;) {
    }
}

#endif
