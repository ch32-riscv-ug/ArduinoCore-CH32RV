/* CH32RVUsbHost - the chip's own USB as a host, on this library's own stack
 * (CH32RVUsbHost.cpp, a controller driver per CH32RVUsbHcd.h; docs/usb.ja.md).
 *
 *     #include <CH32RVUsbHost.h>
 *
 *     CH32RVUsbHost usb;
 *
 *     void setup() {
 *         usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) { ... });
 *         usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) { ... });
 *         usb.begin();
 *     }
 *     void loop() { }        // the stack runs after loop() and inside delay()
 *
 * The API is EspUsbHost's (the same author's ESP32 library), renamed EspUsbHost
 * -> CH32RVUsbHost and ESP_USB_HOST_ -> CH32RV_USB_HOST_, and cut to what a
 * full-speed controller with 20 KB of RAM can do:
 *
 *   - devices: connect / disconnect callbacks, descriptors, strings,
 *     interfaces, endpoints, control transfers, a hub (one level);
 *   - HID: keyboard (layouts), mouse, consumer and system control, gamepad,
 *     vendor reports, raw input, report descriptors, output and feature
 *     reports, LEDs, boot / report protocol;
 *   - CDC-ACM serial (and the FTDI, CP210x, CH34x and PL2303 bridges), as
 *     CH32RVUsbHostCdcSerial (a Stream) or through the host;
 *   - MIDI;
 *   - mass storage: inquiry, capacity, sense, read and write blocks;
 *   - vendor bulk interfaces and vendor control requests;
 *   - CCID smart-card readers: power, ATR, APDU, escape, status.
 *
 * Not here: isochronous streaming (audio, video) and USB networking - they need
 * buffers and bandwidth this part does not have.
 *
 * Differences from EspUsbHost: callbacks are plain function pointers (no
 * std::function, no listener lists); there is no task to configure - the stack
 * runs from ch32rv_usb_host_task() after every loop() and inside delay(), and
 * the blocking calls below drive it while they wait. Callbacks run from there,
 * never from an interrupt, and may call the blocking API. The devices are
 * addresses 1..5 (a hub and what is behind it); CH32RV_USB_HOST_ANY_ADDRESS (the
 * default everywhere) means "the first device that has the function".
 *
 * Only what a sketch uses is linked: each function group lives in a file of its
 * own and brings its class driver with it.
 *
 * Host and device share the controller: CH32RVUsbHost::begin() fails with
 * Busy while CH32RVUsbDevice runs, and the other way round.
 *
 * Runs on CH32X035 R8T6 / C8T6 / G8U6 / G8R6 (USBFS, PC16/PC17; the F8U6,
 * F7P6, D8U6 and the X033 have no host in silicon), CH32V203/V208 (USBFS,
 * PB6/PB7), and CH32V303/V305/V307/V317, V205, L103/M103, M030 and H41x
 * (USBFS, PA11/PA12); begin() reports Unsupported elsewhere. The board supplies VBUS to the device - the chip
 * cannot switch it.
 */
#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------- constants */
#define CH32RV_USB_HOST_ANY_ADDRESS 0xFF
#define CH32RV_USB_HOST_ANY_PORT    0xFF

#define CH32RV_USB_HOST_SPEED_LOW   0
#define CH32RV_USB_HOST_SPEED_FULL  1

#define CH32RV_USB_HOST_MOUSE_LEFT    0x01
#define CH32RV_USB_HOST_MOUSE_RIGHT   0x02
#define CH32RV_USB_HOST_MOUSE_MIDDLE  0x04
#define CH32RV_USB_HOST_MOUSE_BACK    0x08
#define CH32RV_USB_HOST_MOUSE_FORWARD 0x10

#define CH32RV_USB_HOST_HID_REPORT_TYPE_INPUT   1
#define CH32RV_USB_HOST_HID_REPORT_TYPE_OUTPUT  2
#define CH32RV_USB_HOST_HID_REPORT_TYPE_FEATURE 3

#define CH32RV_USB_HOST_KEYBOARD_LED_NUM_LOCK    0x01
#define CH32RV_USB_HOST_KEYBOARD_LED_CAPS_LOCK   0x02
#define CH32RV_USB_HOST_KEYBOARD_LED_SCROLL_LOCK 0x04

/* The report IDs EspUsbDevice and CH32RVUsbDevice give their composite HID
 * functions. A vendor (0xFF00 page) report is recognised by its usage page;
 * this is what sendHIDVendorOutput() falls back to when the descriptor says
 * nothing. */
#define CH32RV_USB_HOST_HID_REPORT_ID_KEYBOARD         1
#define CH32RV_USB_HOST_HID_REPORT_ID_MOUSE            2
#define CH32RV_USB_HOST_HID_REPORT_ID_GAMEPAD          3
#define CH32RV_USB_HOST_HID_REPORT_ID_CONSUMER_CONTROL 4
#define CH32RV_USB_HOST_HID_REPORT_ID_SYSTEM_CONTROL   5
#define CH32RV_USB_HOST_HID_REPORT_ID_VENDOR           6

/* Fixed sizes; the receive buffers' defaults, which the sketch can replace
 * with its own (CH32RVUsbHostCdcSerial::setBuffer(), setVendorBuffer(),
 * setCcidBuffers()). */
#define CH32RV_USB_HOST_STRING_MAX 32               /* per string, with the terminator */
#define CH32RV_USB_HOST_CDC_RX_BUFFER_SIZE 128
#define CH32RV_USB_HOST_VENDOR_RX_BUFFER_SIZE 256
#define CH32RV_USB_HOST_CCID_BUFFER_SIZE 272        /* 10-byte header + a short APDU response */
#define CH32RV_USB_HOST_HID_REPORTS 8               /* report IDs remembered per HID interface */

enum class CH32RVUsbHostError : uint8_t {
    None = 0,
    Unsupported,       /* no USB host on this series */
    Busy,              /* the controller runs CH32RVUsbDevice */
    NotStarted,        /* a call before begin() */
    NotConnected,      /* no (matching) device */
    NotSupported,      /* the device lacks the function, or it is not open */
    InvalidArgument,
    Timeout,
    Stalled,           /* the device refused the request */
    TransferFailed,
    NoMemory,          /* a table or buffer is full */
    InvalidResponse,   /* the device answered something malformed */
};

/* ------------------------------------------------------------- devices */
struct CH32RVUsbHostDeviceInfo {
    uint8_t address = 0;
    uint16_t vid = 0, pid = 0;
    const char *manufacturer = "";
    const char *product = "";
    const char *serial = "";
    uint8_t parentAddress = 0;     /* 0: the root port */
    uint8_t portId = 0;            /* 0x01 root; behind a hub (hubAddress << 4) | port */
    uint8_t speed = CH32RV_USB_HOST_SPEED_FULL;
    uint16_t usbVersion = 0, deviceVersion = 0;
    uint8_t deviceClass = 0, deviceSubClass = 0, deviceProtocol = 0;
    uint8_t maxPacketSize0 = 0;
    uint8_t configurationValue = 0, configurationAttributes = 0, configurationMaxPower = 0;
    uint8_t configurationInterfaceCount = 0;
    uint16_t configurationTotalLength = 0;
    bool supported = false;        /* at least one interface found a driver */
    bool isHub = false;
};

struct CH32RVUsbHostInterfaceInfo {
    uint8_t number = 0, alternate = 0;
    uint8_t interfaceClass = 0, interfaceSubClass = 0, interfaceProtocol = 0;
    uint8_t endpointCount = 0;
    bool claimed = false;          /* a class driver (or vendorOpen/ccidOpen) took it */
};

struct CH32RVUsbHostEndpointInfo {
    uint8_t address = 0, interfaceNumber = 0, attributes = 0;
    uint16_t maxPacketSize = 0;
    uint8_t interval = 0;
};

struct CH32RVUsbHostHubInfo {
    uint8_t address = 0, portCount = 0;
    uint16_t characteristics = 0;
    uint16_t powerOnToPowerGoodMs = 0;
    uint8_t controllerCurrentMa = 0;
};

/* ------------------------------------------------------------- HID */
/* A keyboard layout: which character each key makes. US is the default; the
 * others are linked only when a sketch names one (setKeyboardLayout). */
struct CH32RVUsbHostKeyboardLayout {
    uint16_t langId;
    const uint16_t (*table)[4];    /* [keycode][plain, shift, altgr, shift+altgr] */
    uint8_t size;                  /* keycodes covered */
};
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_US;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_GB;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_DE_DE;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_FR_FR;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_FR_CH;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_ES_ES;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_IT_IT;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_PT_PT;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_PT_BR;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_NL_NL;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_DA_DK;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_NB_NO;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_SV_SE;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_FI_FI;
extern const CH32RVUsbHostKeyboardLayout CH32RV_USB_HOST_KEYBOARD_LAYOUT_HU_HU;

/* What every HID event carries: who sent it and the report as received. */
struct CH32RVUsbHostHIDReportData {
    uint16_t vid = 0, pid = 0;
    const uint8_t *rawData = nullptr;      /* the report, report ID included */
    size_t rawLength = 0;
    const uint8_t *reportData = nullptr;   /* the payload after the report ID */
    size_t reportLength = 0;
};

struct CH32RVUsbHostKeyboardEvent : CH32RVUsbHostHIDReportData {
    uint8_t address = 0, interfaceNumber = 0;
    bool pressed = false, released = false;
    uint8_t keycode = 0;
    uint8_t ascii = 0;             /* 0 when the key makes no ASCII character */
    uint16_t unicode = 0;
    uint8_t modifiers = 0;
    bool numLock = false, capsLock = false, scrollLock = false;
};

struct CH32RVUsbHostMouseEvent : CH32RVUsbHostHIDReportData {
    uint8_t address = 0, interfaceNumber = 0;
    int16_t x = 0, y = 0, wheel = 0, pan = 0;
    uint8_t buttons = 0, previousButtons = 0;
    bool moved = false, buttonsChanged = false;
};

struct CH32RVUsbHostConsumerControlEvent : CH32RVUsbHostHIDReportData {
    uint8_t address = 0, interfaceNumber = 0;
    uint16_t usage = 0;
    bool pressed = false, released = false;
};

struct CH32RVUsbHostSystemControlEvent : CH32RVUsbHostHIDReportData {
    uint8_t address = 0, interfaceNumber = 0;
    uint8_t usage = 0;
    bool pressed = false, released = false;
};

struct CH32RVUsbHostGamepadEvent : CH32RVUsbHostHIDReportData {
    uint8_t address = 0, interfaceNumber = 0;
    bool changed = false;
};

struct CH32RVUsbHostHIDVendorInput : CH32RVUsbHostHIDReportData {
    uint8_t address = 0, interfaceNumber = 0;
};

struct CH32RVUsbHostHIDInput {
    uint8_t address = 0, interfaceNumber = 0;
    uint16_t vid = 0, pid = 0;
    uint8_t subclass = 0, protocol = 0;
    const uint8_t *data = nullptr;
    size_t length = 0;
};

struct CH32RVUsbHostHIDReportDescriptor {
    uint8_t address = 0, interfaceNumber = 0;
    const uint8_t *data = nullptr;
    uint16_t length = 0;
};

/* ------------------------------------------------------------- serial */
enum CH32RVUsbHostSerialParity : uint8_t {
    CH32RV_USB_HOST_SERIAL_PARITY_NONE = 0,
    CH32RV_USB_HOST_SERIAL_PARITY_ODD,
    CH32RV_USB_HOST_SERIAL_PARITY_EVEN,
    CH32RV_USB_HOST_SERIAL_PARITY_MARK,
    CH32RV_USB_HOST_SERIAL_PARITY_SPACE,
};
enum CH32RVUsbHostSerialStopBits : uint8_t {
    CH32RV_USB_HOST_SERIAL_STOP_BITS_1 = 0,
    CH32RV_USB_HOST_SERIAL_STOP_BITS_1_5,
    CH32RV_USB_HOST_SERIAL_STOP_BITS_2,
};
struct CH32RVUsbHostSerialConfig {
    uint32_t baud = 115200;
    uint8_t dataBits = 8;
    CH32RVUsbHostSerialParity parity = CH32RV_USB_HOST_SERIAL_PARITY_NONE;
    CH32RVUsbHostSerialStopBits stopBits = CH32RV_USB_HOST_SERIAL_STOP_BITS_1;
};
struct CH32RVUsbHostSerialData {
    uint8_t address = 0, interfaceNumber = 0, port = 0;
    const uint8_t *data = nullptr;
    size_t length = 0;
};
struct CH32RVUsbHostSerialPortInfo {
    uint8_t address = 0, port = 0;
    uint8_t controlInterfaceNumber = 0xFF, dataInterfaceNumber = 0xFF;
    bool vendorSerial = false;     /* FTDI / CP210x / CH34x / PL2303 rather than CDC-ACM */
    bool ready = false;
};

/* ------------------------------------------------------------- MIDI */
struct CH32RVUsbHostMidiMessage {
    uint8_t address = 0, interfaceNumber = 0;
    uint8_t cable = 0, codeIndex = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;
    const uint8_t *raw = nullptr;  /* the 4-byte USB-MIDI event packet */
    size_t length = 0;
};
struct CH32RVUsbHostMidiPortInfo {
    uint8_t address = 0, interfaceNumber = 0, inCableCount = 0, outCableCount = 0;
};

/* ------------------------------------------------------------- MSC */
struct CH32RVUsbHostMscInquiry {
    uint8_t peripheralDeviceType = 0;
    bool removable = false;
    char vendor[9] = {}, product[17] = {}, revision[5] = {};
};
struct CH32RVUsbHostMscSense {
    uint8_t responseCode = 0, senseKey = 0, additionalSenseCode = 0, additionalSenseQualifier = 0;
};
struct CH32RVUsbHostMscBlockDeviceInfo {
    uint8_t address = 0, interfaceNumber = 0, lun = 0, maxLun = 0;
    uint32_t blockCount = 0, blockSize = 0;
    uint64_t capacityBytes = 0;
};

/* ------------------------------------------------------------- vendor */
struct CH32RVUsbHostVendorInterface {
    uint8_t address = 0, interfaceNumber = 0, inEndpoint = 0, outEndpoint = 0;
    uint16_t inMaxPacketSize = 0, outMaxPacketSize = 0;
};
struct CH32RVUsbHostVendorData {
    uint8_t address = 0, interfaceNumber = 0, endpoint = 0;
    const uint8_t *data = nullptr;
    size_t length = 0;
};

/* ------------------------------------------------------------- CCID */
enum CH32RVUsbHostCcidIccStatus : uint8_t {
    CH32RV_USB_HOST_CCID_ICC_ACTIVE = 0,
    CH32RV_USB_HOST_CCID_ICC_INACTIVE = 1,
    CH32RV_USB_HOST_CCID_ICC_ABSENT = 2,
};
enum CH32RVUsbHostCcidCommandStatus : uint8_t {
    CH32RV_USB_HOST_CCID_COMMAND_OK = 0,
    CH32RV_USB_HOST_CCID_COMMAND_FAILED = 1,
    CH32RV_USB_HOST_CCID_COMMAND_TIME_EXTENSION = 2,
};
enum CH32RVUsbHostCcidVoltage : uint8_t {
    CH32RV_USB_HOST_CCID_VOLTAGE_AUTO = 0,
    CH32RV_USB_HOST_CCID_VOLTAGE_5V = 1,
    CH32RV_USB_HOST_CCID_VOLTAGE_3V = 2,
    CH32RV_USB_HOST_CCID_VOLTAGE_1V8 = 3,
};
struct CH32RVUsbHostCcidInterface {
    uint8_t address = 0, interfaceNumber = 0;
    uint8_t inEndpoint = 0, outEndpoint = 0, interruptEndpoint = 0;
    uint16_t inMaxPacketSize = 0, outMaxPacketSize = 0;
    bool hasClassDescriptor = false;
    uint16_t bcdCCID = 0;
    uint8_t slotCount = 1;
    uint32_t protocols = 0, features = 0, maxMessageLength = 0;
};
struct CH32RVUsbHostCcidStatus {
    uint8_t address = 0, slot = 0;
    CH32RVUsbHostCcidIccStatus iccStatus = CH32RV_USB_HOST_CCID_ICC_ABSENT;
    CH32RVUsbHostCcidCommandStatus commandStatus = CH32RV_USB_HOST_CCID_COMMAND_OK;
    uint8_t error = 0;
    bool present = false, active = false;
};
struct CH32RVUsbHostCcidSlotEvent {
    uint8_t address = 0, slot = 0;
    bool present = false;
};

/* ------------------------------------------------------------- the host */
class CH32RVUsbHost {
public:
    typedef void (*DeviceCallback)(const CH32RVUsbHostDeviceInfo &device);
    typedef void (*KeyboardCallback)(const CH32RVUsbHostKeyboardEvent &event);
    typedef void (*MouseCallback)(const CH32RVUsbHostMouseEvent &event);
    typedef void (*ConsumerControlCallback)(const CH32RVUsbHostConsumerControlEvent &event);
    typedef void (*SystemControlCallback)(const CH32RVUsbHostSystemControlEvent &event);
    typedef void (*GamepadCallback)(const CH32RVUsbHostGamepadEvent &event);
    typedef void (*HIDVendorInputCallback)(const CH32RVUsbHostHIDVendorInput &event);
    typedef void (*HIDInputCallback)(const CH32RVUsbHostHIDInput &input);
    typedef void (*HIDReportDescriptorCallback)(const CH32RVUsbHostHIDReportDescriptor &descriptor);
    typedef void (*SerialDataCallback)(const CH32RVUsbHostSerialData &data);
    typedef void (*MidiMessageCallback)(const CH32RVUsbHostMidiMessage &message);
    typedef void (*VendorDataCallback)(const CH32RVUsbHostVendorData &data);
    typedef void (*CcidSlotChangeCallback)(const CH32RVUsbHostCcidSlotEvent &event);

    CH32RVUsbHost();

    /* ---- lifecycle ---- */
    bool begin();
    void end();
    bool ready() const;            /* begun, and the port is up */
    /* What ch32rv_usb_host_task() does; call it yourself only from code that
     * blocks for long without delay(). */
    void task();

    CH32RVUsbHostError lastError() const { return _error; }
    const char *lastErrorName() const;

    /* ---- devices ---- */
    void onDeviceConnected(DeviceCallback cb) { _connected = cb; }
    void onDeviceDisconnected(DeviceCallback cb) { _disconnected = cb; }
    size_t deviceCount() const;
    size_t getDevices(CH32RVUsbHostDeviceInfo *devices, size_t maxDevices) const;
    bool getDevice(uint8_t address, CH32RVUsbHostDeviceInfo &device) const;
    /* Read from the device's configuration descriptor (a control transfer). */
    size_t getInterfaces(uint8_t address, CH32RVUsbHostInterfaceInfo *interfaces, size_t maxInterfaces);
    size_t getEndpoints(uint8_t address, CH32RVUsbHostEndpointInfo *endpoints, size_t maxEndpoints);
    bool getHubInfo(uint8_t hubAddress, CH32RVUsbHostHubInfo &hub);
    bool getHubPortStatus(uint8_t hubAddress, uint8_t port, uint16_t &status, uint16_t &change);
    bool setHubPortPower(uint8_t hubAddress, uint8_t port, bool enable);
    void printDeviceInfo(uint8_t address, Print &out = Serial);
    void printAllDeviceInfo(Print &out = Serial);

    /* ---- control transfers (EP0) ---- */
    bool vendorControlTransfer(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                               uint8_t *data = nullptr, size_t length = 0, size_t *actualLength = nullptr,
                               uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 1000);
    bool vendorControlIn(uint8_t request, uint16_t value, uint16_t index, uint8_t *data, size_t length,
                         size_t *actualLength = nullptr, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                         uint32_t timeoutMs = 1000);
    bool vendorControlOut(uint8_t request, uint16_t value, uint16_t index, const uint8_t *data = nullptr,
                          size_t length = 0, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                          uint32_t timeoutMs = 1000);
    /* A string descriptor as ASCII (non-ASCII becomes '?'); false if absent. */
    bool getStringDescriptor(uint8_t address, uint8_t index, char *out, size_t capacity);

    /* ---- HID ---- */
    void onKeyboard(KeyboardCallback cb);
    void onMouse(MouseCallback cb);
    void onConsumerControl(ConsumerControlCallback cb);
    void onSystemControl(SystemControlCallback cb);
    void onGamepad(GamepadCallback cb);
    void onHIDVendorInput(HIDVendorInputCallback cb);
    void onHIDInput(HIDInputCallback cb);
    void onHIDReportDescriptor(HIDReportDescriptorCallback cb);
    void setKeyboardLayout(const CH32RVUsbHostKeyboardLayout &layout);
    bool sendSetProtocol(uint8_t interfaceNumber, uint8_t address, bool bootProtocol = false);
    bool sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId, const uint8_t *data,
                       size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool getHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId, uint8_t *data,
                      size_t length, size_t *actualLength = nullptr,
                      uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool sendHIDVendorOutput(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool sendHIDVendorFeature(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock,
                         uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool getKeyboardNumLock(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool getKeyboardCapsLock(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool getKeyboardScrollLock(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool hidReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;

    /* ---- CDC serial ---- */
    void onSerialData(SerialDataCallback cb);
    bool sendSerial(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                    uint8_t port = CH32RV_USB_HOST_ANY_PORT);
    bool sendSerial(const char *text, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                    uint8_t port = CH32RV_USB_HOST_ANY_PORT);
    bool serialReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t port = CH32RV_USB_HOST_ANY_PORT) const;
    uint8_t serialPortCount(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool getSerialPortInfo(CH32RVUsbHostSerialPortInfo &info, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                           uint8_t port = CH32RV_USB_HOST_ANY_PORT) const;
    bool setSerialBaudRate(uint32_t baud, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                           uint8_t port = CH32RV_USB_HOST_ANY_PORT);
    bool setSerialConfig(const CH32RVUsbHostSerialConfig &config, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                         uint8_t port = CH32RV_USB_HOST_ANY_PORT);
    bool setSerialControlLines(bool dtr, bool rts, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                               uint8_t port = CH32RV_USB_HOST_ANY_PORT);

    /* ---- MIDI ---- */
    void onMidiMessage(MidiMessageCallback cb);
    bool midiReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool getMidiPortInfo(CH32RVUsbHostMidiPortInfo &info, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool midiSend(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendNoteOn(uint8_t channel, uint8_t note, uint8_t velocity, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendNoteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0,
                         uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendControlChange(uint8_t channel, uint8_t control, uint8_t value,
                               uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendProgramChange(uint8_t channel, uint8_t program, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendPolyPressure(uint8_t channel, uint8_t note, uint8_t pressure,
                              uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendChannelPressure(uint8_t channel, uint8_t pressure, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendPitchBend(uint8_t channel, uint16_t value, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendPitchBendSigned(uint8_t channel, int16_t value, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool midiSendSysEx(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);

    /* ---- mass storage (one logical unit at a time) ---- */
    bool mscReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool mscInquiry(CH32RVUsbHostMscInquiry &inquiry, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                    uint32_t timeoutMs = 5000);
    bool mscRequestSense(CH32RVUsbHostMscSense &sense, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                         uint32_t timeoutMs = 5000);
    bool mscMaxLun(uint8_t &maxLun, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool mscSelectLun(uint8_t lun, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool mscTestUnitReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscWaitReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t readyTimeoutMs = 5000);
    bool mscCapacity(uint32_t &blockCount, uint32_t &blockSize, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = 5000);
    bool mscGetBlockDeviceInfo(CH32RVUsbHostMscBlockDeviceInfo &info,
                               uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount,
                       uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount,
                        uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);

    /* ---- vendor bulk ---- */
    void onVendorData(VendorDataCallback cb) { _vendorData = cb; }
    /* Open a vendor (0xFF) interface with a bulk IN and/or OUT endpoint - the
     * first one, or `interfaceNumber`. Received data then goes to
     * onVendorData() and to vendorRead(). One open interface at a time. */
    /* The sketch's receive ring for the next vendorOpen() (default 256 bytes). */
    bool setVendorBuffer(uint8_t *buffer, size_t size);
    bool vendorOpen(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t interfaceNumber = 0xFF);
    void vendorClose();
    bool vendorReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool getVendorInterface(CH32RVUsbHostVendorInterface &info) const;
    bool vendorWrite(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = 1000);
    size_t vendorAvailable() const;
    size_t vendorRead(uint8_t *buffer, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    uint16_t vendorOutPacketSize(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    uint16_t vendorInPacketSize(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;

    /* ---- CCID smart-card readers ---- */
    void onCcidCardInserted(CcidSlotChangeCallback cb) { _ccidInserted = cb; }
    void onCcidCardRemoved(CcidSlotChangeCallback cb) { _ccidRemoved = cb; }
    /* The sketch's receive ring and response buffer for the next ccidOpen(),
     * `size` bytes each (default 272: a short APDU). */
    bool setCcidBuffers(uint8_t *ring, uint8_t *message, size_t size);
    bool ccidOpen(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t interfaceNumber = 0xFF);
    void ccidClose();
    bool ccidReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool ccidGetInterface(CH32RVUsbHostCcidInterface &info) const;
    bool ccidGetStatus(CH32RVUsbHostCcidStatus &status, uint8_t slot = 0,
                       uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 1000);
    bool ccidCardPresent(uint8_t slot = 0, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool ccidPowerOn(uint8_t *atr = nullptr, size_t atrCapacity = 0, size_t *atrLength = nullptr,
                     CH32RVUsbHostCcidVoltage voltage = CH32RV_USB_HOST_CCID_VOLTAGE_AUTO, uint8_t slot = 0,
                     uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool ccidPowerOff(uint8_t slot = 0, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 2000);
    size_t ccidGetAtr(uint8_t *buffer, size_t capacity) const;
    bool ccidTransfer(const uint8_t *tx, size_t txLength, uint8_t *rx, size_t rxCapacity, size_t *rxLength,
                      uint8_t slot = 0, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    /* `response` gets the answer without SW1 SW2 (they go to *statusWord);
     * responseCapacity has to hold them too. */
    bool ccidApdu(const uint8_t *apdu, size_t apduLength, uint8_t *response, size_t responseCapacity,
                  size_t *responseLength, uint16_t *statusWord = nullptr, uint8_t slot = 0,
                  uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool ccidEscape(const uint8_t *tx, size_t txLength, uint8_t *rx, size_t rxCapacity, size_t *rxLength,
                    uint8_t slot = 0, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    uint8_t ccidLastError() const { return _ccidError; }

    /* ---- internal: the function groups' state hangs off these ---- */
    DeviceCallback _connected = nullptr, _disconnected = nullptr;
    VendorDataCallback _vendorData = nullptr;
    CcidSlotChangeCallback _ccidInserted = nullptr, _ccidRemoved = nullptr;
    CH32RVUsbHostError _error = CH32RVUsbHostError::None;
    uint8_t _ccidError = 0;
    bool _started = false;

    /* Resolve CH32RV_USB_HOST_ANY_ADDRESS; 0 when nothing is mounted. */
    uint8_t resolve(uint8_t address) const;
    bool fail(CH32RVUsbHostError error)
    {
        _error = error;
        return false;
    }
    bool ok()
    {
        _error = CH32RVUsbHostError::None;
        return true;
    }
};

/* The one host the stack runs (begin() makes it current). */
extern CH32RVUsbHost *ch32rv_usb_host_active;

/* ------------------------------------------------------------- CDC serial */
/* A CDC (or bridge) serial port as a Stream:
 *
 *     CH32RVUsbHost usb;
 *     CH32RVUsbHostCdcSerial port(usb);
 *     ... port.begin(115200); port.println("hi"); while (port.available()) ...
 *
 * It follows the first port that appears unless setAddress() / setPort() pin
 * it to one. */
class CH32RVUsbHostCdcSerial : public Stream {
public:
    explicit CH32RVUsbHostCdcSerial(CH32RVUsbHost &host);
    /* The sketch's receive ring instead of the port's own 128 bytes. */
    bool setBuffer(uint8_t *buffer, uint16_t size);
    bool begin(uint32_t baud = 115200);
    void end();
    bool connected() const;

    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *data, size_t length) override;
    using Print::write;

    bool setBaudRate(uint32_t baud);
    bool setConfig(const CH32RVUsbHostSerialConfig &config);
    bool setDtr(bool on);
    bool setRts(bool on);
    void setAddress(uint8_t address) { _address = address; }
    uint8_t address() const { return _address; }
    void clearAddress() { _address = CH32RV_USB_HOST_ANY_ADDRESS; }
    void setPort(uint8_t port) { _port = port; }
    uint8_t port() const { return _port; }
    void clearPort() { _port = CH32RV_USB_HOST_ANY_PORT; }

private:
    CH32RVUsbHost &_host;
    uint8_t _address = CH32RV_USB_HOST_ANY_ADDRESS;
    uint8_t _port = CH32RV_USB_HOST_ANY_PORT;
    uint32_t _baud = 115200;
    bool _begun = false;
    bool _dtr = true, _rts = true;
    uint8_t *_rxBuffer = nullptr;
    uint16_t _rxSize = 0;

public:
    /* For the port it is bound to. */
    uint8_t *rxBuffer() const { return _rxBuffer; }
    uint16_t rxSize() const { return _rxSize; }
};

/* Printing helpers, EspUsbHost's espUsbHostPrint* under this library's name. */
void ch32rvUsbHostPrint(const CH32RVUsbHostDeviceInfo &device, Print &out = Serial);
void ch32rvUsbHostPrint(const CH32RVUsbHostInterfaceInfo &interface, Print &out = Serial);
void ch32rvUsbHostPrint(const CH32RVUsbHostEndpointInfo &endpoint, Print &out = Serial);
void ch32rvUsbHostPrintHex(const uint8_t *data, size_t length, Print &out = Serial);
const char *ch32rvUsbHostConsumerControlUsageName(uint16_t usage);
