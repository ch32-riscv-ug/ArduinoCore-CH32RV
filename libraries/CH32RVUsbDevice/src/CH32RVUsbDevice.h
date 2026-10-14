/* CH32RVUsbDevice - the chip's own USB as a device, on TinyUSB (ADR-0012,
 * docs/usb.ja.md).
 *
 * One device object, and the functions it offers declared against it:
 *
 *     #include <CH32RVUsbDevice.h>
 *
 *     CH32RVUsbDeviceHidKeyboard keyboard;            // SerialUSB is already there
 *
 *     void setup() {
 *         CH32RVUsbDeviceConfig config;
 *         config.product = "My gadget";
 *         USBDevice.begin(config);                   // or just SerialUSB.begin()
 *     }
 *
 * A function joins the device when it is declared (its constructor), in
 * declaration order, and the configuration descriptor is laid out from them
 * when begin() runs. A function the sketch never declares is not linked: the
 * TinyUSB class drivers come in only through these objects. USBDevice and
 * SerialUSB are predefined the way Serial is; a sketch that names neither
 * links neither, and may declare its own CH32RVUsbDevice instead (one device
 * per sketch - TinyUSB runs one device stack).
 *
 * The stack runs from ch32rv_usb_task(): after every loop(), inside delay(),
 * and in each SerialUSB call, and only does work when the controller raised an
 * event. A sketch that blocks elsewhere for long starves it; the host then sees
 * a stalled device, not a lost one.
 *
 * Runs on CH32X035/X033 (USBFS). On the other series begin() returns false
 * with lastError() == Unsupported, so the same sketch still builds there. */
#pragma once

#include <Arduino.h>

#include "CH32RVUsbDeviceTypes.h"

class CH32RVUsbDevice;

/* Which USB controller the device runs on (EspUsbController). Auto is the
 * series' usual one; the others name a controller on series that have more
 * than one (V30x: FS and HS, V20x: USBD and USBFS - not supported yet, see
 * docs/usb.ja.md). */
enum class CH32RVUsbController : uint8_t {
    Auto = 0,
    FullSpeed,     /* USBFS / OTG_FS */
    HighSpeed,     /* USBHS */
    Usbd,          /* the device-only USBD of CH32V20x */
};

struct CH32RVUsbDeviceConfig {
    /* The pid.codes test VID:PID: fine on a bench, not for a product. A
     * product needs its own (pid.codes, or the USB-IF). */
    uint16_t vid = 0x1209;
    uint16_t pid = 0x0001;
    /* bcdDevice, BCD: 0x0100 = 1.00. */
    uint16_t deviceVersion = 0x0100;
    const char *manufacturer = "ch32-riscv-ug";
    const char *product = "CH32RV Arduino";
    /* nullptr: the chip's 64-bit unique ID in hex, so two boards on one host
     * stay apart. The strings must outlive the device. */
    const char *serialNumber = nullptr;
    uint16_t maxPowerMilliamps = 100;
    bool selfPowered = false;
    /* A DFU runtime interface, so a host can send DFU_DETACH and re-upload
     * over USB alone (ch32rv isp --entry auto): added at begin() unless the
     * sketch declared its own CH32RVUsbDeviceDfuRuntime. About 0.5 KB of flash
     * and 20 bytes of RAM. false leaves the interface out; the code stays
     * linked, since this is a run-time choice - -DCH32RV_USB_NO_DEFAULT_DFU in
     * build_opt.h removes it from the build as well. */
    bool dfuRuntime = true;
    CH32RVUsbController controller = CH32RVUsbController::Auto;
};

enum class CH32RVUsbDeviceError : uint8_t {
    None = 0,
    Unsupported,          /* no USB device support on this series (yet) */
    PortUnavailable,      /* config.controller names one this part lacks */
    NotStarted,           /* sendHidReport() before begin() */
    NotReady,             /* the host has not configured the device, or the endpoint stayed busy */
    AlreadyStarted,       /* begin() again, or a function added after begin() */
    TooManyFunctions,     /* more than CH32RV_USB_MAX_FUNCTIONS (6) declared */
    TooManyInstances,     /* more of one class than its CFG_TUD_* allows */
    OutOfEndpoints,       /* the functions need more endpoints than exist */
    DescriptorTooLarge,   /* over CH32RV_USB_CONFIG_MAX (320) bytes */
};

#ifndef CH32RV_USB_MAX_FUNCTIONS
#define CH32RV_USB_MAX_FUNCTIONS 6
#endif

/* What one class contributes. Each class file has one, as a constant; the
 * device reads it at begin(). */
struct CH32RVUsbFunctionType {
    const void *driver;           /* TinyUSB usbd_class_driver_t */
    /* Write this function's descriptors for interface `itf` onward into
     * `out` (`room` bytes free), taking endpoint numbers from *ep (and
     * advancing it); `str` is its string index. Returns the length written,
     * 0 when it does not fit. */
    uint16_t (*descriptor)(const class CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room,
                           uint8_t itf, uint8_t *ep, uint8_t str);
    uint8_t interfaces;
    uint8_t maxInstances;         /* the class's CFG_TUD_* count */
};

/* Base of every class object. Declaring one attaches it to the device. */
class CH32RVUsbDeviceFunction {
public:
    /* The interface name the host shows (string descriptor). Set before
     * begin(); the string must outlive the device. */
    void setName(const char *name) { _name = name; }
    const char *name() const { return _name; }
    /* Which of its class this is (0 for the first CDC port, 1 for the
     * second ...): TinyUSB's instance number. */
    uint8_t instance() const { return _instance; }
    /* False when the device refused it (see CH32RVUsbDevice::lastError()). */
    bool attached() const { return _attached; }
    CH32RVUsbDevice &device() const { return _device; }

protected:
    CH32RVUsbDeviceFunction(CH32RVUsbDevice &device, const CH32RVUsbFunctionType &type,
                            const char *name);

private:
    friend class CH32RVUsbDevice;
    CH32RVUsbDevice &_device;
    const CH32RVUsbFunctionType &_type;
    const char *_name;
    uint8_t _instance = 0;
    bool _attached = false;
};

class CH32RVUsbDevice {
public:
    constexpr CH32RVUsbDevice() {}

    /* Lay out the descriptors from the declared functions and connect.
     * False, with lastError() set, when that is impossible. A second call
     * after success returns true and does nothing. */
    bool begin();
    bool begin(const CH32RVUsbDeviceConfig &config);
    /* Leave the bus. begin() may be called again afterwards. */
    void end();

    bool started() const { return _started; }
    /* The host has configured the device. ready() is EspUsbDevice's name. */
    bool mounted() const;
    bool ready() const { return mounted(); }
    /* Run the USB stack once. The core already does after every loop() and
     * inside delay(); a sketch busy elsewhere for long can call it. */
    void task();
    /* The bus is suspended (the host is asleep or the cable is unplugged
     * while VBUS stays). */
    bool suspended() const;
    /* Wake a suspended host, if it allowed remote wakeup. */
    bool remoteWakeup();

    CH32RVUsbDeviceError lastError() const { return _error; }
    const char *lastErrorName() const;

    /* Leave the bus and reset into the factory USB bootloader (1a86:55e0
     * on X035/X033), where ch32rv's `isp` writes the chip. Does not return.
     * On a series without one, a plain reset. */
    [[noreturn]] static void enterBootloader();
    /* The same, once the transfer being answered has gone out - for code
     * running inside a USB callback (a DFU_DETACH, a 1200 bps touch). */
    static void requestBootloader();

    /* CDC ports this build holds (CFG_TUD_CDC; build_opt.h raises it). */
    static uint8_t maxCdcPorts();

    /* How many functions are attached, and the one at `i`. */
    uint8_t functionCount() const { return _count; }
    CH32RVUsbDeviceFunction *function(uint8_t i) const { return i < _count ? _functions[i] : nullptr; }

    /* Send an input report on HID interface `instance` (0: this library has
     * one), waiting up to timeoutMs for the endpoint. EspUsbDevice's
     * low-level path; the HID classes use it. */
    bool sendHidReport(uint8_t instance, uint8_t reportId, const void *data, size_t length,
                       uint32_t timeoutMs = 100);

    /* For the class objects (EspUsbDevice::addClass). */
    bool attach(CH32RVUsbDeviceFunction &f);
    bool addClass(CH32RVUsbDeviceFunction *f) { return f && attach(*f); }
    const CH32RVUsbDeviceConfig &config() const { return _config; }

private:
    CH32RVUsbDeviceFunction *_functions[CH32RV_USB_MAX_FUNCTIONS] = {};
    CH32RVUsbDeviceConfig _config;
    uint8_t _count = 0;
    bool _started = false;
    CH32RVUsbDeviceError _error = CH32RVUsbDeviceError::None;
};

/* The predefined device, as Serial is predefined (CH32RVUsbDeviceInstance.cpp). */
extern CH32RVUsbDevice USBDevice;

#include "CH32RVUsbDeviceCdcSerial.h"
#include "CH32RVUsbDeviceDfuRuntime.h"
#include "CH32RVUsbDeviceHid.h"
#include "CH32RVUsbDeviceMidi.h"
