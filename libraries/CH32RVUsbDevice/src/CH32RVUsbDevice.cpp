/* The device: the functions attached to it, the descriptors laid out from
 * them, the chip side of TinyUSB (clocks and PHY, the interrupt, the task
 * hook), and the reset into the factory USB bootloader. */
#include "CH32RVUsbDevice.h"

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

/* ---------------------------------------------------------- X033 / X035 PHY
 * AFIO_CTLR: USB_IOEN hands PC16/PC17 to the USB PHY, USB_PHY_V33 selects the
 * 3.3 V PHY supply (VDD = 3.3 V on every board this core targets so far), and
 * UDP_PUE = 11b puts the 1.5 kOhm full-speed pull-up on D+. Values from the
 * CH32X035 EVT (USBFS_Device_Init). */
#define X03X_AFIO_UDM_PUE_MASK 0x00000003u
#define X03X_AFIO_UDP_PUE_MASK 0x0000000Cu
#define X03X_AFIO_UDP_PUE_1K5  0x0000000Cu
#define X03X_AFIO_USB_PHY_V33  0x00000040u
#define X03X_AFIO_USB_IOEN     0x00000080u
/* USBFS: endpoints 1..7 besides EP0. */
#define X03X_USB_LAST_EP       7u

/* FLASH_STATR.BOOT_MODE: after the next software reset, run the BOOT area
 * (the factory bootloader). Unlocked by the flash keys, then the boot-mode
 * keys - the sequence ch32rv's `isp enter --via probe` uses (measured on
 * CH32X035F8U6, ch32rv docs/protocol/factory-isp.ja.md). */
#define X03X_FLASH_KEYR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x04u)
#define X03X_FLASH_STATR         CH32RV_REG32(CH32RV_FLASH_BASE + 0x0Cu)
#define X03X_FLASH_CTLR          CH32RV_REG32(CH32RV_FLASH_BASE + 0x10u)
#define X03X_FLASH_BOOT_MODEKEYR CH32RV_REG32(CH32RV_FLASH_BASE + 0x28u)
#define X03X_FLASH_KEY1          0x45670123u
#define X03X_FLASH_KEY2          0xCDEF89ABu
#define X03X_FLASH_STATR_BOOT_MODE (1u << 14)
#define X03X_FLASH_CTLR_LOCK     (1u << 7)

static void usb_phy_on(void)
{
    ch32rv_clock_enable(AFIO);
    ch32rv_clock_enable(USBFS);
    /* PC16 (D-) floating input, PC17 (D+) input with pull-up, as the EVT does;
     * the PHY takes the pads once USB_IOEN is set. */
    pinMode(PC16, INPUT);
    pinMode(PC17, INPUT_PULLUP);
    CH32RV_AFIO_CTLR = (CH32RV_AFIO_CTLR & ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK)) |
                       X03X_AFIO_USB_PHY_V33 | X03X_AFIO_UDP_PUE_1K5 | X03X_AFIO_USB_IOEN;
}

static void usb_phy_off(void)
{
    CH32RV_AFIO_CTLR &= ~(X03X_AFIO_UDP_PUE_MASK | X03X_AFIO_UDM_PUE_MASK | X03X_AFIO_USB_IOEN);
}

/* ---------------------------------------------------------- the running device
 * TinyUSB runs one device stack, and its callbacks are plain functions, so the
 * device that began is kept here. */
static CH32RVUsbDevice *active;
static usbd_class_driver_t drivers[CH32RV_USB_MAX_FUNCTIONS];
static volatile bool bootloader_requested;

/* The configuration descriptor is built here at begin(). 320 bytes hold two
 * CDC ports (66 each) + MIDI (92) + HID (32) + DFU runtime (18) + the header
 * (9); beyond that begin() fails with DescriptorTooLarge, and build_opt.h can
 * raise this. */
#ifndef CH32RV_USB_CONFIG_MAX
#define CH32RV_USB_CONFIG_MAX 320
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
    if (config.controller != CH32RVUsbController::Auto &&
        config.controller != CH32RVUsbController::FullSpeed) {
        _error = CH32RVUsbDeviceError::PortUnavailable;
        return false;
    }
#ifndef CH32RV_USB_NO_DEFAULT_DFU
    if (config.dfuRuntime) {
        /* Best effort: a refusal here (no room) is not the sketch's error. */
        const CH32RVUsbDeviceError before = _error;
        CH32RVUsbDeviceDfuRuntime::attachDefault(*this);
        _error = before;
    }
#endif
    /* Interfaces and endpoints are numbered as the functions come. */
    uint16_t len = TUD_CONFIG_DESC_LEN;
    uint8_t itf = 0, ep = 1;
    for (uint8_t i = 0; i < _count; i++) {
        const CH32RVUsbDeviceFunction &f = *_functions[i];
        const uint16_t n = f._type.descriptor(f, desc_configuration + len,
                                              (uint16_t)(sizeof desc_configuration - len), itf, &ep,
                                              (uint8_t)(FIRST_FUNCTION_STRING + i));
        if (n == 0) {
            _error = CH32RVUsbDeviceError::DescriptorTooLarge;
            return false;
        }
        if (ep > X03X_USB_LAST_EP + 1u) {
            _error = CH32RVUsbDeviceError::OutOfEndpoints;
            return false;
        }
        len = (uint16_t)(len + n);
        itf = (uint8_t)(itf + f._type.interfaces);
        drivers[i] = *(const usbd_class_driver_t *)f._type.driver;
    }
    /* The configuration header (TUD_CONFIG_DESCRIPTOR, written out because
     * its fields are run-time values here). Bit 7 of bmAttributes is reserved
     * and set; bMaxPower counts 2 mA units. */
    uint8_t *h = desc_configuration;
    h[0] = TUD_CONFIG_DESC_LEN;
    h[1] = TUSB_DESC_CONFIGURATION;
    h[2] = (uint8_t)len;
    h[3] = (uint8_t)(len >> 8);
    h[4] = itf;
    h[5] = 1;
    h[6] = 0;
    h[7] = (uint8_t)(0x80u | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP |
                     (config.selfPowered ? TUSB_DESC_CONFIG_ATT_SELF_POWERED : 0u));
    h[8] = (uint8_t)(config.maxPowerMilliamps > 500 ? 250 : config.maxPowerMilliamps / 2);

    _config = config;
    active = this;
    usb_phy_on();
    tusb_rhport_init_t init = {};
    init.role = TUSB_ROLE_DEVICE;
    init.speed = TUSB_SPEED_FULL;
    tusb_init(0, &init);
    _started = true;
    _error = CH32RVUsbDeviceError::None;
    return true;
}

void CH32RVUsbDevice::end()
{
    if (!_started) {
        return;
    }
    tud_disconnect();
    tusb_deinit(0);
    usb_phy_off();
    _started = false;
}

bool CH32RVUsbDevice::mounted() const
{
    return _started && tud_mounted();
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
    }
    usb_phy_off();
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < 50u) {
    }

    noInterrupts();
    X03X_FLASH_KEYR = X03X_FLASH_KEY1;
    X03X_FLASH_KEYR = X03X_FLASH_KEY2;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY1;
    X03X_FLASH_BOOT_MODEKEYR = X03X_FLASH_KEY2;
    X03X_FLASH_STATR |= X03X_FLASH_STATR_BOOT_MODE;
    X03X_FLASH_CTLR |= X03X_FLASH_CTLR_LOCK;
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

extern "C" __attribute__((interrupt)) void USBFS_IRQHandler(void)
{
    tud_int_handler(0);
}

extern "C" uint32_t tusb_time_millis_api(void)
{
    return millis();
}

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

extern "C" const uint8_t *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return desc_configuration;
}

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
    } else {
        const CH32RVUsbDeviceFunction *f = active->function((uint8_t)(index - FIRST_FUNCTION_STRING));
        if (!f || !f->name()) {
            return NULL;
        }
        n = ascii_to_desc(f->name());
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
