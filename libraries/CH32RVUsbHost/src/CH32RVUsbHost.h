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
 * full-speed controller with 20 KB of RAM can do (on a high-speed host the same, with 512-byte packets):
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
 * Differences from EspUsbHost: callbacks are CH32RVCallback - a function or a
 * lambda capturing up to two pointers, no heap (not std::function) - one per
 * event (no listener lists); there is no task to configure - the stack
 * runs from ch32rv_usb_host_task() after every loop() and inside delay(), and
 * the blocking calls below drive it while they wait. The callbacks run from
 * deliverEvents(), which the sketch calls (ch32rv_deliver.h) - never inside
 * delay() or an interrupt - and may call the blocking API. Until then what
 * they report waits: reports, bytes and packets in the stack's buffers (a
 * full one holds the device off), connections in a queue of four. The devices are
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
 * PB6/PB7), CH32V103 (its older USB IP, PA11/PA12), and CH32V303/V305/V307/V317, V205, L103/M103, M030 and H41x
 * (USBFS, PA11/PA12), on the first-generation USBHS of V305/V307/V317 as HIGH_SPEED (PB6/PB7, needs HSE),
 * and on the second-generation USBHS at high speed (CH32V407/V467: USBHS1 by default,
 * USBHS2 as HIGH_SPEED_2; X305/X315 by default; V205 and H41x as HIGH_SPEED - CH32RVUsbHostConfig::port;
 * full- and low-speed devices behind a high-speed hub by split transactions there),
 * and on the USBSS of H417/H416 at SuperSpeed (SUPER_SPEED; implemented, not yet tested on hardware);
 * begin() reports Unsupported elsewhere. The board supplies VBUS to the device - the chip cannot switch it.
 */
#pragma once

#include <Arduino.h>
#include <CH32RVCallback.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------- constants */
#define CH32RV_USB_HOST_ANY_ADDRESS 0xFF
#define CH32RV_USB_HOST_ANY_PORT    0xFF

#define CH32RV_USB_HOST_SPEED_LOW   0
#define CH32RV_USB_HOST_SPEED_FULL  1
#define CH32RV_USB_HOST_SPEED_HIGH  2
#define CH32RV_USB_HOST_SPEED_SUPER 3

/* Which controller the host runs on (EspUsbHostPort). DEFAULT is the series'
 * usual one: USBFS where the part has it, else its USBHS. HIGH_SPEED_2 is the
 * second USBHS of CH32V407 / V467. SUPER_SPEED is the USBSS of CH32H417 /
 * H416 (USB 3 at 5 Gbit/s only; a USB 2.0 device on that connector is
 * HIGH_SPEED's) - implemented, not yet tested on hardware. A port the part
 * does not have makes begin() fail with Unsupported. */
enum CH32RVUsbHostPort : uint8_t {
    CH32RV_USB_HOST_PORT_DEFAULT = 0,
    CH32RV_USB_HOST_PORT_HIGH_SPEED,
    CH32RV_USB_HOST_PORT_FULL_SPEED,
    CH32RV_USB_HOST_PORT_HIGH_SPEED_2,
    CH32RV_USB_HOST_PORT_SUPER_SPEED,
};

/* begin()'s settings (EspUsbHostConfig, less what a task-less stack has no
 * use for). */
struct CH32RVUsbHostConfig {
    CH32RVUsbHostPort port = CH32RV_USB_HOST_PORT_DEFAULT;
};

#include "CH32RVUsbHostPort.h"

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
/* The longest string a string descriptor carries (126 characters), with the
 * terminator: CH32RVUsbHostDeviceInfo keeps the strings whole, and a buffer
 * this long takes any getStringDescriptor(). */
#define CH32RV_USB_HOST_STRING_MAX 127
/* The largest bulk or interrupt packet a class's static buffers take: full
 * speed's 64 where the default port is full speed, 512 where it is high speed
 * (X305 / X315, V407 / V467). A series whose default is full speed reaches its
 * high- or SuperSpeed port by a driver that is linked only when a sketch picks
 * it (V30x's and V205 / H41x's HIGH_SPEED, H417 / H416's SUPER_SPEED); that
 * driver gives the classes heap buffers for its larger packets (512, 1024 at
 * SuperSpeed - CH32RVUsbHcd::bufferAlloc), and a HID report stays within 64
 * there. Where it is larger, the receive buffers below grow to hold more than
 * one such packet (an IN is armed only with room for a whole one). */
#if defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || defined(CH32RV_SERIES_CH32V407) || \
    defined(CH32RV_SERIES_CH32V467)
#define CH32RV_USB_HOST_MAX_PACKET 512
#else
#define CH32RV_USB_HOST_MAX_PACKET 64
#endif
#define CH32RV_USB_HOST_CDC_RX_BUFFER_SIZE (CH32RV_USB_HOST_MAX_PACKET > 64 ? 2 * CH32RV_USB_HOST_MAX_PACKET : 128)
#define CH32RV_USB_HOST_VENDOR_RX_BUFFER_SIZE (CH32RV_USB_HOST_MAX_PACKET > 64 ? 2 * CH32RV_USB_HOST_MAX_PACKET : 256)
/* 10-byte header + a short APDU response; on a high-speed host also more than a packet (the ring) */
#define CH32RV_USB_HOST_CCID_BUFFER_SIZE (CH32RV_USB_HOST_MAX_PACKET > 64 ? 2 * CH32RV_USB_HOST_MAX_PACKET : 272)
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
    uint64_t blockCount = 0;        /* EspUsbHost's width: past 2^32 blocks (READ CAPACITY(16)) */
    uint32_t blockSize = 0;
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

/* ------------------------------------------------------------- network (CDC-NCM / CDC-ECM) */
/* The receive block (NCM's NTB) this host takes at most: the device is asked
 * for no bigger (SET_NTB_INPUT_SIZE) when it offers more. EspUsbHost's 3200,
 * TinyUSB's CFG_TUD_NCM_IN_NTB_MAX_SIZE. */
#ifndef CH32RV_USB_HOST_NETWORK_NTB_IN_MAX
#define CH32RV_USB_HOST_NETWORK_NTB_IN_MAX 3200
#endif
/* Received frames waiting for onNetworkFrame() / networkReadFrame(), each
 * after a 2-byte length. */
#ifndef CH32RV_USB_HOST_NETWORK_RX_RING_SIZE
#define CH32RV_USB_HOST_NETWORK_RX_RING_SIZE 4096
#endif
/* The largest Ethernet frame sent or taken (no FCS). */
#define CH32RV_USB_HOST_NETWORK_MAX_FRAME 1514
enum CH32RVUsbHostNetworkProtocol : uint8_t {
    CH32RV_USB_HOST_NETWORK_PROTOCOL_NONE = 0,
    CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_ECM,
    CH32RV_USB_HOST_NETWORK_PROTOCOL_CDC_NCM,
};
struct CH32RVUsbHostNetworkInterfaceInfo {
    uint8_t address = 0;
    CH32RVUsbHostNetworkProtocol protocol = CH32RV_USB_HOST_NETWORK_PROTOCOL_NONE;
    uint8_t controlInterfaceNumber = 0xFF, dataInterfaceNumber = 0xFF, dataInterfaceAlternate = 0;
    uint8_t macAddressStringIndex = 0;
    uint8_t macAddress[6] = {};        /* the device's, from that string */
    uint16_t maxSegmentSize = 0;
    uint8_t networkCapabilities = 0;   /* NCM: bmNetworkCapabilities */
    uint16_t ncmVersion = 0;
    uint8_t notificationEndpoint = 0, inEndpoint = 0, outEndpoint = 0;
    uint16_t notificationMaxPacketSize = 0, inMaxPacketSize = 0, outMaxPacketSize = 0;
};
/* A bare Ethernet frame (destination, source, EtherType, payload): the NCM
 * blocks and ECM transfers are made and taken apart by the library. */
struct CH32RVUsbHostNetworkFrame {
    uint8_t address = 0;
    CH32RVUsbHostNetworkProtocol protocol = CH32RV_USB_HOST_NETWORK_PROTOCOL_NONE;
    const uint8_t *data = nullptr;
    size_t length = 0;
};
/* networkAttachNetif()'s settings (EspUsbHostNetworkConfig): a DHCP client by
 * default; dhcpClient false and ip / gateway / subnet for a fixed address. */
struct CH32RVUsbHostNetworkConfig {
    bool dhcpClient = true;
    IPAddress ip, gateway, subnet, dns1, dns2;
};
struct CH32RVUsbHostNetworkStats {
    bool ready = false, linkUp = false;
    uint32_t rxNtb = 0;          /* blocks (ECM: transfers) received */
    uint32_t rxFrames = 0;       /* frames taken out of them */
    uint32_t rxDropped = 0;      /* frames the full ring had no room for */
    uint32_t rxOversized = 0;    /* blocks longer than ntbInSize, or malformed */
    uint32_t txFrames = 0, txFails = 0;
    uint16_t ntbInSize = 0, ntbOutSize = 0;
};

/* ------------------------------------------------------------- audio (UAC1 / UAC2 streaming) */
/* bInterfaceProtocol of an Audio Class interface (EspUsbHost's values). */
#define CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC1 0x00
#define CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC2 0x20
#define CH32RV_USB_HOST_MAX_AUDIO_SAMPLE_RATES 8
/* Received / queued PCM per direction (from the heap at the start). */
#ifndef CH32RV_USB_HOST_AUDIO_RING_SIZE
#define CH32RV_USB_HOST_AUDIO_RING_SIZE 2048
#endif
/* One alternate setting of an Audio Streaming interface: a PCM (Type I)
 * format and its isochronous endpoint (EspUsbHostAudioStreamInfo). */
struct CH32RVUsbHostAudioStreamInfo {
    uint8_t address = 0, interfaceNumber = 0, alternate = 0, endpointAddress = 0;
    bool input = false, output = false;
    uint8_t channels = 0, bytesPerSample = 0, bitsPerSample = 0;
    uint32_t sampleRate = 0;           /* the first (or the only) one */
    uint8_t sampleRateCount = 0;       /* 0: a continuous range, sampleRateMin .. Max */
    uint32_t sampleRates[CH32RV_USB_HOST_MAX_AUDIO_SAMPLE_RATES] = {};
    uint32_t sampleRateMin = 0, sampleRateMax = 0, sampleRateResolution = 0;
    uint16_t maxPacketSize = 0;
    uint8_t interval = 0;              /* bInterval */
    bool startable = true;
    uint8_t protocol = CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC1;
    uint8_t terminalLink = 0, clockSourceId = 0;   /* UAC2: the clock the rates come from */
};
#define CH32RV_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS 8
/* A Feature Unit of the Audio Control interface (EspUsbHostAudioFeatureUnitInfo):
 * which controls the master (channel 0) and each channel have. UAC1 keeps one
 * bit per control (mute bit 0, volume bit 1), UAC2 two (11: the host may set
 * it) - CH32RVUsbHost::audioHasMute() / audioHasVolume() read them. */
struct CH32RVUsbHostAudioFeatureUnitInfo {
    uint8_t address = 0, interfaceNumber = 0, unitId = 0, sourceId = 0;
    uint8_t channelCount = 0, controlSize = 0;
    uint32_t masterControls = 0;
    uint32_t channelControls[CH32RV_USB_HOST_MAX_AUDIO_FEATURE_CHANNELS] = {};
    uint8_t protocol = CH32RV_USB_HOST_AUDIO_PROTOCOL_UAC1;
};
/* Volume in 1/256 dB (both versions), as the device reports it. */
struct CH32RVUsbHostAudioVolumeRange {
    int16_t min = 0, max = 0, resolution = 0;
};
/* Received PCM, as it came in the isochronous packets. */
struct CH32RVUsbHostAudioData {
    uint8_t address = 0, interfaceNumber = 0;
    const uint8_t *data = nullptr;
    size_t length = 0;
};

/* ------------------------------------------------------------- video (UVC) */
#define CH32RV_USB_HOST_VIDEO_FORMAT_UNKNOWN 0
#define CH32RV_USB_HOST_VIDEO_FORMAT_MJPEG 1
#define CH32RV_USB_HOST_VIDEO_FORMAT_YUY2 2
#define CH32RV_USB_HOST_VIDEO_FORMAT_NV12 3
#define CH32RV_USB_HOST_VIDEO_FORMAT_H264 4
#define CH32RV_USB_HOST_VIDEO_FORMAT_UNCOMPRESSED 5
#define CH32RV_USB_HOST_MAX_VIDEO_FRAME_INTERVALS 8
/* One format / frame pair of a camera's VideoStreaming interface
 * (EspUsbHostVideoStreamInfo). Intervals in 100 ns. */
struct CH32RVUsbHostVideoStreamInfo {
    uint8_t address = 0, interfaceNumber = 0, alternate = 0, endpointAddress = 0;
    bool isochronous = true;
    uint8_t format = CH32RV_USB_HOST_VIDEO_FORMAT_UNKNOWN;
    uint8_t formatIndex = 0, frameIndex = 0, frameCount = 0, defaultFrameIndex = 0;
    uint16_t width = 0, height = 0;
    uint8_t bitsPerPixel = 0;
    uint32_t frameInterval = 0;          /* dwDefaultFrameInterval */
    uint8_t frameIntervalCount = 0;      /* 0: continuous, min .. max in steps */
    uint32_t frameIntervals[CH32RV_USB_HOST_MAX_VIDEO_FRAME_INTERVALS] = {};
    uint32_t frameIntervalMin = 0, frameIntervalMax = 0, frameIntervalStep = 0;
    uint32_t maxVideoFrameBufferSize = 0;
    uint32_t maxPayloadSize = 0;         /* the widest alternate's bytes per (micro)frame */
    uint8_t interval = 0;
    bool startable = true;
};
/* One assembled frame (onVideoFrame()); data is the library's until the
 * callback returns. complete is false for a frame cut short (a payload
 * flagged bad, the buffer overrun, the frame ID toggling before its end). */
struct CH32RVUsbHostVideoFrame {
    uint8_t address = 0, interfaceNumber = 0;
    const uint8_t *data = nullptr;
    size_t length = 0;
    uint16_t width = 0, height = 0;
    uint8_t format = CH32RV_USB_HOST_VIDEO_FORMAT_UNKNOWN;
    uint32_t sequence = 0;
    bool complete = true;
    bool hasPresentationTime = false;
    uint32_t presentationTime = 0;
};
struct CH32RVUsbHostVideoStats {
    uint32_t frames = 0, framesIncomplete = 0, framesDropped = 0, payloads = 0;
    uint32_t headerErrors = 0, payloadErrors = 0, packetErrors = 0, overflows = 0;
    uint64_t bytes = 0;
};
/* The Probe / Commit control as the camera answered (EspUsbHostVideoProbeControl). */
struct CH32RVUsbHostVideoProbeControl {
    uint16_t hint = 0x0001;
    uint8_t formatIndex = 0, frameIndex = 0;
    uint32_t frameInterval = 0;
    uint32_t maxVideoFrameSize = 0, maxPayloadTransferSize = 0;
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
/* ccidMessage()'s answer: the RDR_to_PC header and its data, which points into the library's message buffer and
 * stays valid until the next CCID call (EspUsbHostCcidResponse). */
struct CH32RVUsbHostCcidResponse {
    uint8_t messageType = 0, slot = 0, sequence = 0;
    uint8_t status = 0;             /* bStatus as it came */
    uint8_t error = 0;              /* bError */
    uint8_t chainParameter = 0;     /* bChainParameter / bClockStatus / bRFU */
    CH32RVUsbHostCcidIccStatus iccStatus = CH32RV_USB_HOST_CCID_ICC_ABSENT;
    CH32RVUsbHostCcidCommandStatus commandStatus = CH32RV_USB_HOST_CCID_COMMAND_OK;
    const uint8_t *data = nullptr;
    size_t length = 0;
};
struct CH32RVUsbHostCcidSlotEvent {
    uint8_t address = 0, slot = 0;
    bool present = false;
};

/* ------------------------------------------------------------- the host */
class NetworkUsbHost;   /* the Network library's interface on a USB network device */

/* add*Listener()'s ids (EspUsbHost's EspUsbHostListenerId): 0 is none. */
typedef uint32_t CH32RVUsbHostListenerId;
static constexpr CH32RVUsbHostListenerId CH32RV_USB_HOST_INVALID_LISTENER_ID = 0;
/* The listeners' file takes onDeviceConnected() / onDeviceDisconnected() over
 * once a listener of theirs is added (CH32RVUsbHostListeners.cpp). */
extern void (*ch32rv_usb_host_connected_primary)(const CH32RVCallback<void(const CH32RVUsbHostDeviceInfo &)> &cb);
extern void (*ch32rv_usb_host_disconnected_primary)(const CH32RVCallback<void(const CH32RVUsbHostDeviceInfo &)> &cb);

class CH32RVUsbHost {
public:
    typedef CH32RVCallback<void(const CH32RVUsbHostDeviceInfo &device)> DeviceCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostKeyboardEvent &event)> KeyboardCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostMouseEvent &event)> MouseCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostConsumerControlEvent &event)> ConsumerControlCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostSystemControlEvent &event)> SystemControlCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostGamepadEvent &event)> GamepadCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostHIDVendorInput &event)> HIDVendorInputCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostHIDInput &input)> HIDInputCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostHIDReportDescriptor &descriptor)> HIDReportDescriptorCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostSerialData &data)> SerialDataCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostMidiMessage &message)> MidiMessageCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostVendorData &data)> VendorDataCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostCcidSlotEvent &event)> CcidSlotChangeCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostNetworkFrame &frame)> NetworkFrameCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostAudioData &data)> AudioDataCallback;
    typedef CH32RVCallback<void(const CH32RVUsbHostVideoFrame &frame)> VideoFrameCallback;
    typedef CH32RVCallback<void()> RemoteWakeupCallback;

    CH32RVUsbHost();

    /* ---- lifecycle ---- */
    /* Inline: the controller's driver is picked where the port is known, so
     * only that driver is linked (CH32RVUsbHostPort.h). */
    bool begin() { return beginOn(ch32rv_usb_host_port_hcd(CH32RV_USB_HOST_PORT_DEFAULT)); }
    bool begin(const CH32RVUsbHostConfig &config) { return beginOn(ch32rv_usb_host_port_hcd(config.port)); }
    void end();
    bool ready() const;            /* begun, and the port is up */
    /* What ch32rv_usb_host_task() does; call it yourself only from code that
     * blocks for long without delay(). */
    void task();

    CH32RVUsbHostError lastError() const { return _error; }
    const char *lastErrorName() const;

    /* ---- bus power (the root port) ---- */
    /* suspend(): the device on the root port (and everything behind a hub
     * there) suspends; its remote wakeup is enabled first when its
     * configuration offers it. Transfers wait until resume(). Unsupported
     * where the controller's driver cannot suspend. resume() wakes it; a
     * remote wakeup from the device resumes the port by itself and then
     * calls onRemoteWakeup() (from delay() / after loop(), like the other
     * callbacks). */
    bool suspend();
    bool resume();
    bool suspended() const;
    void onRemoteWakeup(RemoteWakeupCallback cb);

    /* ---- devices ---- */
    void onDeviceConnected(DeviceCallback cb)
    {
        if (ch32rv_usb_host_connected_primary) {
            ch32rv_usb_host_connected_primary(cb);
        } else {
            _connected = cb;
        }
    }
    void onDeviceDisconnected(DeviceCallback cb)
    {
        if (ch32rv_usb_host_disconnected_primary) {
            ch32rv_usb_host_disconnected_primary(cb);
        } else {
            _disconnected = cb;
        }
    }

    /* More receivers of one event beside its on*() (EspUsbHost 2.6's
     * add*Listener()): each listener gets every event, after the on*()
     * callback, in the order added; at most CH32RV_USB_HOST_MAX_LISTENERS_PER_EVENT
     * (4) of an event (0 when full or the callback is empty). The id takes it
     * out again (removeListener()). For a library that wants the events
     * without taking the sketch's on*() slot. A sketch that never adds one
     * links none of it; the first of an event adds its table (about 260 bytes
     * of flash and 80 of RAM) and puts its fan-out in the event's one slot -
     * on*() keeps working through it. Listeners run where on*() does (from
     * delay() / after loop()); the event they get is valid only during the
     * call. */
    CH32RVUsbHostListenerId addKeyboardListener(KeyboardCallback callback);
    CH32RVUsbHostListenerId addMouseListener(MouseCallback callback);
    CH32RVUsbHostListenerId addConsumerControlListener(ConsumerControlCallback callback);
    CH32RVUsbHostListenerId addSystemControlListener(SystemControlCallback callback);
    CH32RVUsbHostListenerId addGamepadListener(GamepadCallback callback);
    CH32RVUsbHostListenerId addMidiMessageListener(MidiMessageCallback callback);
    CH32RVUsbHostListenerId addDeviceConnectedListener(DeviceCallback callback);
    CH32RVUsbHostListenerId addDeviceDisconnectedListener(DeviceCallback callback);
    bool removeListener(CH32RVUsbHostListenerId listenerId);
    /* Connections and disconnections that did not fit before a
     * deliverEvents(), since begin(). */
    uint32_t droppedDeviceEvents() const;
    size_t deviceCount() const;
    size_t getDevices(CH32RVUsbHostDeviceInfo *devices, size_t maxDevices) const;
    /* The addresses of the enumerated devices, as getDevices() lists them (EspUsbHost). */
    size_t getHostDeviceAddresses(uint8_t *addresses, size_t maxAddresses) const;
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
    /* The data OUT endpoint's packet size (0 when no such serial port is ready): for a writer that ends a transfer
     * on a packet boundary (EspUsbHost). */
    uint16_t serialOutPacketSize(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                                 uint8_t port = CH32RV_USB_HOST_ANY_PORT) const;

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
    /* The sense the last mscRequestSense() (or mscWaitReady()) got, without asking the drive. */
    bool mscLastSense(CH32RVUsbHostMscSense &sense, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool mscMaxLun(uint8_t &maxLun, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool mscSelectLun(uint8_t lun, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool mscTestUnitReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscWaitReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t readyTimeoutMs = 5000);
    /* START STOP UNIT with LoEj: the medium is ejected (a drive whose eject
     * ends a write - a firmware-update disk - acts on it). */
    bool mscEject(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    /* SYNCHRONIZE CACHE(10): what the drive holds in its cache is written to the medium. */
    bool mscSynchronizeCache(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscCapacity(uint32_t &blockCount, uint32_t &blockSize, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                     uint32_t timeoutMs = 5000);
    /* EspUsbHost's 64-bit forms: READ CAPACITY(16) when (10) says the medium has 2^32 blocks or more, READ(16) /
     * WRITE(16) when the range passes block 2^32 - 1 (else the (10) commands, which every drive has). */
    bool mscCapacity64(uint64_t &blockCount, uint32_t &blockSize, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                       uint32_t timeoutMs = 5000);
    bool mscGetBlockDeviceInfo(CH32RVUsbHostMscBlockDeviceInfo &info,
                               uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool mscReadBlocks(uint32_t lba, uint8_t *data, uint32_t blockCount,
                       uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscWriteBlocks(uint32_t lba, const uint8_t *data, uint32_t blockCount,
                        uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscReadBlocks64(uint64_t lba, uint8_t *data, uint32_t blockCount,
                         uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    bool mscWriteBlocks64(uint64_t lba, const uint8_t *data, uint32_t blockCount,
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
    /* The endpoint addresses vendorOpen() took (0 when none is open). */
    uint8_t vendorOutEndpoint(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    uint8_t vendorInEndpoint(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;

    /* ---- USB audio: UAC1 / UAC2 PCM streams (EspUsbHost's calls) ---- */
    /* The device's Audio Streaming alternate settings with a PCM format and
     * an isochronous endpoint (UAC2: the rates asked of its clock); how many,
     * up to maxStreams filled. */
    size_t getAudioStreams(uint8_t address, CH32RVUsbHostAudioStreamInfo *streams, size_t maxStreams);
    /* Start a stream: its alternate setting, the sample rate (0: the stream's
     * first; UAC1 on the endpoint, UAC2 on its clock), the isochronous
     * endpoint kept busy every service interval. One input and one output at
     * a time. Received PCM goes to onAudioData() (from deliverEvents());
     * audioSend() queues PCM for the output, which sends silence and counts
     * an underrun when the queue runs dry. The rings
     * (CH32RV_USB_HOST_AUDIO_RING_SIZE) come from the heap. */
    void onAudioData(AudioDataCallback cb) { _audioData = cb; }
    bool audioInputStart(const CH32RVUsbHostAudioStreamInfo &stream, uint32_t sampleRate = 0,
                         uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool audioInputStart(uint8_t channels, uint8_t bitsPerSample, uint32_t sampleRate,
                         uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool audioInputStop(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool audioInputRunning(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool audioOutputStart(const CH32RVUsbHostAudioStreamInfo &stream, uint32_t sampleRate = 0,
                          uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool audioOutputStart(uint8_t channels, uint8_t bitsPerSample, uint32_t sampleRate,
                          uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    void audioOutputStop(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool audioOutputRunning(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    uint32_t audioOutputUnderruns(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    /* Queue PCM for the output (whole frames of it); false when the queue has
     * no room for all of it (none is taken then). */
    bool audioSend(const uint8_t *data, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    size_t audioOutputAvailableForWrite(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    /* An asynchronous output's explicit feedback endpoint (polled while the
     * output runs): the rate it asks for paces the output; without one, the
     * rate set at the start. */
    bool audioOutputHasFeedback(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    uint32_t audioOutputFeedbackRate(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    uint32_t audioOutputRate(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    /* Feature Units: mute and volume. unitId 0: the first unit that has the
     * control on that channel (0: the master). Volume in 1/256 dB. */
    size_t getAudioFeatureUnits(uint8_t address, CH32RVUsbHostAudioFeatureUnitInfo *units, size_t maxUnits);
    bool audioHasMute(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0, uint8_t channel = 0);
    bool audioHasVolume(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0, uint8_t channel = 0);
    bool audioGetMute(bool &mute, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0,
                      uint8_t channel = 0, uint32_t timeoutMs = 1000);
    bool audioSetMute(bool mute, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0,
                      uint8_t channel = 0, uint32_t timeoutMs = 1000);
    bool audioGetVolume(int16_t &volume, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0,
                        uint8_t channel = 0, uint32_t timeoutMs = 1000);
    bool audioSetVolume(int16_t volume, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint8_t unitId = 0,
                        uint8_t channel = 0, uint32_t timeoutMs = 1000);
    bool audioGetVolumeRange(CH32RVUsbHostAudioVolumeRange &range, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS,
                             uint8_t unitId = 0, uint8_t channel = 0, uint32_t timeoutMs = 1000);

    /* ---- USB video: UVC cameras (EspUsbHost's calls) ---- */
    /* Every format / frame pair (uncompressed YUY2 / NV12, MJPEG ...). */
    size_t getVideoStreams(uint8_t address, CH32RVUsbHostVideoStreamInfo *streams, size_t maxStreams);
    /* How many getVideoStreams() would give, nothing copied. */
    size_t getVideoStreamCount(uint8_t address) { return getVideoStreams(address, nullptr, 0); }
    /* Probe, Commit, then the alternate setting whose bandwidth covers what
     * the camera committed to (isochronous; a bulk camera has none), and the
     * stream kept running. fps 0: the frame's default. Two frame buffers of
     * the committed size come from the heap: one fills while the other waits
     * for onVideoFrame() (from deliverEvents()); a frame that finds both busy
     * is dropped (framesDropped). One camera at a time. */
    void onVideoFrame(VideoFrameCallback cb) { _videoFrame = cb; }
    bool videoStart(const CH32RVUsbHostVideoStreamInfo &stream, uint32_t fps = 0,
                    uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool videoStart(uint8_t format, uint16_t width, uint16_t height, uint32_t fps,
                    uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool videoStop(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool videoStreaming(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool videoCommitted(CH32RVUsbHostVideoProbeControl &control, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool videoStats(CH32RVUsbHostVideoStats &stats, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;

    /* ---- USB network: CDC-NCM and CDC-ECM (EspUsbHost's calls) ---- */
    /* Open the network function of a device (the first with one): its
     * control and data interfaces, the block sizes (NCM), the packet filter,
     * the data interface's alternate setting 1. One open at a time; the
     * buffers (about CH32RV_USB_HOST_NETWORK_NTB_IN_MAX + 2 KB +
     * CH32RV_USB_HOST_NETWORK_RX_RING_SIZE) come from the heap. */
    bool networkOpen(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    void networkClose(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool networkReady(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool getNetworkInterface(CH32RVUsbHostNetworkInterfaceInfo &info) const;
    /* EspUsbHost's list form: the one network interface open here (0 or 1). */
    size_t getNetworkInterfaces(uint8_t address, CH32RVUsbHostNetworkInterfaceInfo *interfaces, size_t maxInterfaces)
    {
        if (!interfaces || !maxInterfaces || !getNetworkInterface(interfaces[0]) ||
            (address != CH32RV_USB_HOST_ANY_ADDRESS && interfaces[0].address != address)) {
            return 0;
        }
        return 1;
    }
    /* Received frames go to onNetworkFrame() (from deliverEvents()) or, with
     * no callback, wait for networkReadFrame() (a frame longer than `length`
     * is cut). networkWriteFrame() sends one frame (14 .. 1514 bytes) and
     * waits for the transfer. */
    void onNetworkFrame(NetworkFrameCallback cb) { _networkFrame = cb; }
    bool networkWriteFrame(const uint8_t *frame, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    size_t networkReadFrame(uint8_t *buffer, size_t length, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    /* The device's NETWORK_CONNECTION notification (up when it has no
     * notification endpoint). */
    bool networkLinkUp(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    bool networkStats(CH32RVUsbHostNetworkStats &stats, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    /* The IP stack on it (EspUsbHost's networkAttachNetif(); the Network
     * library, #include <NetworkUsbHost.h>): a NetworkInterface whose frames go over
     * the device - NetworkClient, NetworkUDP, DNS and the rest then run over
     * USB. Opens the device when it is not open. This end takes the device's
     * MAC with the last bit flipped, and becomes the default route when no
     * other interface is up. Its frames are the interface's: no
     * onNetworkFrame() / networkReadFrame() while it is attached. */
    bool networkAttachNetif(const CH32RVUsbHostNetworkConfig &config = CH32RVUsbHostNetworkConfig(),
                            uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    bool networkDetachNetif(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS);
    IPAddress networkLocalIP(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
    NetworkUsbHost *networkInterface() const { return _networkIf; }

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
    /* The reader's slots (bMaxSlotIndex + 1), 0 when none is open. */
    uint8_t ccidSlotCount(uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS) const;
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
    /* Any PC_to_RDR message the calls above do not wrap (SetParameters, GetParameters, IccClock, T0APDU ...):
     * messageSpecific is header bytes 7 .. 9 (nullptr: zeros). EspUsbHost's ccidMessage(). */
    bool ccidMessage(uint8_t messageType, const uint8_t *messageSpecific, const uint8_t *data, size_t length,
                     CH32RVUsbHostCcidResponse &response, uint8_t slot = 0,
                     uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 5000);
    /* The class request ABORT, then PC_to_RDR_Abort with the same bSeq (CCID 1.1 5.3.1). */
    bool ccidAbort(uint8_t slot = 0, uint8_t address = CH32RV_USB_HOST_ANY_ADDRESS, uint32_t timeoutMs = 1000);
    uint8_t ccidLastError() const { return _ccidError; }

    /* ---- internal: the function groups' state hangs off these ---- */
    DeviceCallback _connected = nullptr, _disconnected = nullptr;
    VendorDataCallback _vendorData = nullptr;
    NetworkFrameCallback _networkFrame = nullptr;
    NetworkUsbHost *_networkIf = nullptr;
    AudioDataCallback _audioData = nullptr;
    VideoFrameCallback _videoFrame = nullptr;
    CcidSlotChangeCallback _ccidInserted = nullptr, _ccidRemoved = nullptr;
    CH32RVUsbHostError _error = CH32RVUsbHostError::None;
    uint8_t _ccidError = 0;
    bool _started = false;

    /* begin() on that controller's driver (nullptr: the part has no such port). */
    bool beginOn(const CH32RVUsbHcd *driver);
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
    /* EspUsbHost's: a ring of `size` bytes from the heap (66 .. 65535),
     * before begin() or after end(); false if it cannot be had. */
    bool setRxBufferSize(size_t size);
    size_t rxBufferSize() const;
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
    /* DTR and RTS in one SET_CONTROL_LINE_STATE (EspUsbHost 2.10); setDtr() / setRts() each send one with the
     * other line's last value. */
    bool setControlLines(bool dtr, bool rts);
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
    bool _rxOwned = false;          /* _rxBuffer came from setRxBufferSize() */

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
