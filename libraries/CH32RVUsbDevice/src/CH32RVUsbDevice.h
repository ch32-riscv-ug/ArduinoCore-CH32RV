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
 * Runs on CH32X035/X033, V103, V203/V208 (USBFS or USBD), V303 (USBFS) and
 * V305/V307/V317 (USBFS or USBHS); only X035 is checked on hardware. On the
 * other series begin() returns false with lastError() == Unsupported, so the
 * same sketch still builds there. */
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

/* How the Microsoft OS 2.0 descriptor set names the interfaces Windows should
 * bind WinUSB to (EspUsbDeviceMsOs20Layout). Windows resolves a function
 * subset only through usbccgp.sys, which it loads for a composite device, so a
 * single-interface device needs the flat form. */
enum CH32RVUsbDeviceMsOs20Layout : uint8_t {
    /* Subsets when the configuration has more than one interface, flat with
     * exactly one. */
    CH32RV_USB_DEVICE_MS_OS_20_AUTO = 0,
    CH32RV_USB_DEVICE_MS_OS_20_FLAT,
    CH32RV_USB_DEVICE_MS_OS_20_SUBSETS,
};

/* Every default string and identity, overridable from build_opt.h
 * (-DCH32RV_USB_DEVICE_PRODUCT='"My gadget"'); most can also be changed at run
 * time - CH32RVUsbDeviceConfig, or setName() on a class object. */
#ifndef CH32RV_USB_DEVICE_VID
#define CH32RV_USB_DEVICE_VID 0x1209          /* pid.codes test VID:PID */
#endif
#ifndef CH32RV_USB_DEVICE_PID
#define CH32RV_USB_DEVICE_PID 0x0001
#endif
#ifndef CH32RV_USB_DEVICE_VERSION
#define CH32RV_USB_DEVICE_VERSION 0x0100
#endif
#ifndef CH32RV_USB_DEVICE_MANUFACTURER
#define CH32RV_USB_DEVICE_MANUFACTURER "ch32-riscv-ug"
#endif
#ifndef CH32RV_USB_DEVICE_PRODUCT
#define CH32RV_USB_DEVICE_PRODUCT "CH32RV Arduino"
#endif
/* Interface names (iInterface) - what the host shows for each function. */
#ifndef CH32RV_USB_CDC_NAME
#define CH32RV_USB_CDC_NAME "CDC"
#endif
#ifndef CH32RV_USB_HID_NAME
#define CH32RV_USB_HID_NAME "HID"
#endif
#ifndef CH32RV_USB_MIDI_NAME
#define CH32RV_USB_MIDI_NAME "MIDI"
#endif
#ifndef CH32RV_USB_VENDOR_NAME
#define CH32RV_USB_VENDOR_NAME "Vendor"
#endif
#ifndef CH32RV_USB_MSC_NAME
#define CH32RV_USB_MSC_NAME "Mass storage"
#endif
#ifndef CH32RV_USB_AUDIO_NAME
#define CH32RV_USB_AUDIO_NAME "Audio"
#endif
#ifndef CH32RV_USB_VIDEO_NAME
#define CH32RV_USB_VIDEO_NAME "Camera"
#endif
#ifndef CH32RV_USB_NET_NAME
#define CH32RV_USB_NET_NAME "Network"
#endif
#ifndef CH32RV_USB_CCID_NAME
#define CH32RV_USB_CCID_NAME "Smart card reader"
#endif
/* The DFU runtime's: ch32rv's discovery lists a device whose DFU runtime
 * interface carries "CH32RV" as an upload port. Another name keeps the
 * interface working (DFU_DETACH still enters the bootloader) but takes the
 * device out of that list. */
#ifndef CH32RV_USB_DFU_RUNTIME_NAME
#define CH32RV_USB_DFU_RUNTIME_NAME "CH32RV"
#endif
/* SCSI INQUIRY of CH32RVUsbDeviceMsc (8 / 16 / 4 characters) and the FAT RAM
 * disk's volume label (11). */
#ifndef CH32RV_USB_MSC_VENDOR
#define CH32RV_USB_MSC_VENDOR "CH32RV"
#endif
#ifndef CH32RV_USB_MSC_PRODUCT
#define CH32RV_USB_MSC_PRODUCT "MSC"
#endif
#ifndef CH32RV_USB_MSC_REVISION
#define CH32RV_USB_MSC_REVISION "1.0"
#endif
#ifndef CH32RV_USB_MSC_VOLUME_LABEL
#define CH32RV_USB_MSC_VOLUME_LABEL "CH32RV"
#endif

struct CH32RVUsbDeviceConfig {
    /* The pid.codes test VID:PID: fine on a bench, not for a product. A
     * product needs its own (pid.codes, or the USB-IF). */
    uint16_t vid = CH32RV_USB_DEVICE_VID;
    uint16_t pid = CH32RV_USB_DEVICE_PID;
    /* bcdDevice, BCD: 0x0100 = 1.00. */
    uint16_t deviceVersion = CH32RV_USB_DEVICE_VERSION;
    const char *manufacturer = CH32RV_USB_DEVICE_MANUFACTURER;
    const char *product = CH32RV_USB_DEVICE_PRODUCT;
    /* nullptr: the chip's 64-bit unique ID in hex - unique per chip, by
     * contract: two boards on one host stay apart, and ch32rv's discovery
     * names the upload port after it (ch32rv-app://<serial>), so the port
     * keeps its name on another USB socket. The strings must outlive the
     * device. */
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
    /* Names of the two interfaces a sketch has no object for: the default DFU
     * runtime (CH32RV_USB_DFU_RUNTIME_NAME - see there before changing it)
     * and the HID interface all HID classes share. nullptr keeps the default. */
    const char *dfuRuntimeName = nullptr;
    const char *hidInterfaceName = nullptr;
    /* WebUSB: a browser may reach the device, and shows webusbUrl (the
     * landing page) when it is plugged in. Needs a BOS, so a vendor interface
     * or the DFU runtime has to be there. */
    bool webusbEnabled = false;
    const char *webusbUrl = nullptr;
    /* Microsoft OS 2.0 descriptors: what makes Windows bind WinUSB to the
     * vendor and DFU runtime interfaces without a driver install. */
    CH32RVUsbDeviceMsOs20Layout msOs20Layout = CH32RV_USB_DEVICE_MS_OS_20_AUTO;
    /* The DeviceInterfaceGUIDs Windows records for the vendor interface: what
     * a host application looks the device up by. "{8-4-4-4-12}", braces
     * included; anything else fails begin() with InvalidArgument. nullptr
     * keeps EspUsbDevice's default - give a product its own. */
    const char *deviceInterfaceGuid = nullptr;
    /* Ask Windows to treat the device as composite whatever its interface
     * count (MS_OS_20_FEATURE_CCGP_DEVICE); forces the subset layout. */
    bool msOs20CcgpDevice = false;
    /* wVendorRevision. Windows re-reads the properties only when it changes;
     * 0 derives it from the set's own bytes, so it moves with them. */
    uint16_t msOs20VendorRevision = 0;
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
    DescriptorTooLarge,   /* over CH32RV_USB_CONFIG_MAX (512) bytes */
    InvalidArgument,      /* a config field is malformed (deviceInterfaceGuid) */
    Busy,                 /* the controller is a USB host right now (CH32RVUsbHost) */
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
     * advancing it); `str` is its string index; `hs` asks for the high-speed
     * form (512-byte bulk endpoints, intervals in microframes). Returns the
     * length written, 0 when it does not fit. */
    uint16_t (*descriptor)(const class CH32RVUsbDeviceFunction &self, uint8_t *out, uint16_t room,
                           uint8_t itf, uint8_t *ep, uint8_t str, bool hs);
    uint8_t interfaces;
    uint8_t maxInstances;         /* the class's CFG_TUD_* count */
    /* The config field that names this interface, when the sketch has no
     * object to call setName() on (the HID interface, the default DFU). */
    const char *CH32RVUsbDeviceConfig::*configName;
    /* Windows should bind WinUSB to this interface (MS OS 2.0): 1 with the
     * compatible ID alone, 2 with DeviceInterfaceGUIDs as well. */
    uint8_t winusb;
};

/* Base of every class object. Declaring one attaches it to the device. */
class CH32RVUsbDeviceFunction {
public:
    /* The interface name the host shows (string descriptor). Set before
     * begin(); the string must outlive the device. */
    void setName(const char *name) { _name = name; }
    const char *name() const { return _name; }
    /* What the host is shown: name(), or the config field that overrides it
     * (CH32RVUsbDeviceConfig::hidInterfaceName / dfuRuntimeName). */
    const char *interfaceName() const;
    /* Which of its class this is (0 for the first CDC port, 1 for the
     * second ...): TinyUSB's instance number. */
    uint8_t instance() const { return _instance; }
    /* Its first interface number, once the device has begun. */
    uint8_t interfaceNumber() const { return _interface; }
    uint8_t winusb() const { return _type.winusb; }
    /* False when the device refused it (see CH32RVUsbDevice::lastError()). */
    bool attached() const { return _attached; }
    CH32RVUsbDevice &device() const { return _device; }

protected:
    CH32RVUsbDeviceFunction(CH32RVUsbDevice &device, const CH32RVUsbFunctionType &type,
                            const char *name);

    /* The device stack (CH32RVUsbDeviceCore.cpp) calls these in normal context
     * - from loop() / delay() through ch32rv_usb_task(), never from the
     * interrupt. */
    /* A bus reset, end(), or the host unconfigured the device: forget the
     * transfers in flight (their buffers are free again). */
    virtual void usbReset() {}
    /* The host chose the configuration; this function's endpoints (alternate
     * setting 0) are open. */
    virtual void usbConfigured(bool highSpeed) { (void)highSpeed; }
    /* A class or vendor request for one of its interfaces or endpoints, or a
     * standard GET_DESCRIPTOR to one of its interfaces. In the Setup stage
     * answer with device().controlIn / controlOut / controlStatus and return
     * true, or return false to stall. Data: the controlOut() buffer is full.
     * Ack: the status stage went out. */
    virtual bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &setup)
    {
        (void)stage;
        (void)setup;
        return false;
    }
    /* SET_INTERFACE on one of its interfaces; the stack has already opened
     * the endpoints of `alternate`. False stalls the request. */
    virtual bool usbSetInterface(uint8_t interfaceNumber, uint8_t alternate)
    {
        (void)interfaceNumber;
        return alternate == 0;
    }
    /* A transfer on one of its endpoints (address, bit 7 for IN) finished;
     * `length` is what moved (an OUT transfer ends early on a short packet). */
    virtual void usbTransferred(uint8_t address, uint16_t length)
    {
        (void)address;
        (void)length;
    }
    /* The host cleared a halt on one of its endpoints (data toggle back to DATA0). */
    virtual void usbHaltCleared(uint8_t address) { (void)address; }

private:
    friend class CH32RVUsbDevice;
    friend struct CH32RVUsbDeviceCore;
    CH32RVUsbDevice &_device;
    const CH32RVUsbFunctionType &_type;
    const char *_name;
    uint8_t _instance = 0;
    uint8_t _interface = 0;
    bool _attached = false;
};

class CH32RVUsbDevice {
    friend struct CH32RVUsbDeviceCore;

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

    /* The BOS and Microsoft OS 2.0 set in use (null / 0 without one), and
     * the wVendorRevision that applied. */
    const uint8_t *bosDescriptor() const;
    uint16_t bosDescriptorLength() const;
    const uint8_t *microsoftOs20Descriptor() const;
    uint16_t microsoftOs20DescriptorLength() const;
    uint16_t microsoftOs20VendorRevision() const;
    bool microsoftOs20UsesSubsets() const;
    /* Interfaces in the configuration (after begin()). */
    uint8_t interfaceCount() const;

    /* The configuration descriptor for one speed (CONFIGURATION or
     * OTHER_SPEED_CONFIGURATION), written into `out`; 0 when it cannot be
     * laid out (lastError()). For the USB stack's callbacks. */
    uint16_t buildConfiguration(uint8_t *out, uint16_t room, bool highSpeed, uint8_t descriptorType);

    /* For the classes: transfers on their endpoints (address, bit 7 for IN).
     * The buffer stays theirs until usbTransferred(). False while a transfer
     * is in flight there, or when the device is not configured. */
    bool transfer(uint8_t address, void *buffer, uint16_t length);
    bool transferBusy(uint8_t address) const;
    void stall(uint8_t address);
    bool stalled(uint8_t address) const;
    /* Answers to a request in usbControl()'s Setup stage. controlIn sends at
     * most wLength bytes; the data must stay valid until the Ack stage. */
    bool controlIn(const void *data, uint16_t length);
    bool controlOut(void *buffer, uint16_t length);
    bool controlStatus();
    /* The speed the host chose. */
    bool highSpeed() const;

    /* For the class objects (EspUsbDevice::addClass). */
    bool attach(CH32RVUsbDeviceFunction &f);
    bool addClass(CH32RVUsbDeviceFunction *f) { return f && attach(*f); }
    const CH32RVUsbDeviceConfig &config() const { return _config; }

private:
    CH32RVUsbDeviceFunction *_functions[CH32RV_USB_MAX_FUNCTIONS] = {};
    CH32RVUsbDeviceConfig _config;
    uint8_t _count = 0;
    uint8_t _endpoints = 8;       /* the controller's endpoint numbers 0.._endpoints-1 */
    bool _started = false;
    CH32RVUsbDeviceError _error = CH32RVUsbDeviceError::None;
};

/* The predefined device, as Serial is predefined (CH32RVUsbDeviceInstance.cpp). */
extern CH32RVUsbDevice USBDevice;

#include "CH32RVUsbDeviceCdcSerial.h"
#include "CH32RVUsbDeviceDfuRuntime.h"
#include "CH32RVUsbDeviceHid.h"
#include "CH32RVUsbDeviceMidi.h"
#include "CH32RVUsbDeviceVendor.h"
#include "CH32RVUsbDeviceMsc.h"
#include "CH32RVUsbDeviceCcid.h"
#include "CH32RVUsbDeviceNet.h"
#include "CH32RVUsbDeviceVideo.h"
#include "CH32RVUsbDeviceAudio.h"
