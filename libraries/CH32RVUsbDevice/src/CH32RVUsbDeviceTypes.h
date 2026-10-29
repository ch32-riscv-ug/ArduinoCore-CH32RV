/* Constants and report types, named after EspUsbDevice's with CH32RV in place
 * of ESP (ESP_USB_HID_KEY_A -> CH32RV_USB_HID_KEY_A, EspUsbDeviceBootKeyboardReport
 * -> CH32RVUsbDeviceBootKeyboardReport ...), so a sketch moves between the two
 * by renaming. Included by CH32RVUsbDevice.h. */
#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint8_t CH32RV_USB_DEVICE_KEYBOARD_LED_NUM_LOCK = 0x01;
static constexpr uint8_t CH32RV_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK = 0x02;
static constexpr uint8_t CH32RV_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK = 0x04;
static constexpr uint8_t CH32RV_USB_DEVICE_KEYBOARD_LED_COMPOSE = 0x08;
static constexpr uint8_t CH32RV_USB_DEVICE_KEYBOARD_LED_KANA = 0x10;
static constexpr uint8_t CH32RV_USB_HID_KEY_A = 0x04;
static constexpr uint8_t CH32RV_USB_HID_KEY_B = 0x05;
static constexpr uint8_t CH32RV_USB_HID_KEY_C = 0x06;
static constexpr uint8_t CH32RV_USB_HID_KEY_D = 0x07;
static constexpr uint8_t CH32RV_USB_HID_KEY_E = 0x08;
static constexpr uint8_t CH32RV_USB_HID_KEY_F = 0x09;
static constexpr uint8_t CH32RV_USB_HID_KEY_G = 0x0a;
static constexpr uint8_t CH32RV_USB_HID_KEY_H = 0x0b;
static constexpr uint8_t CH32RV_USB_HID_KEY_I = 0x0c;
static constexpr uint8_t CH32RV_USB_HID_KEY_J = 0x0d;
static constexpr uint8_t CH32RV_USB_HID_KEY_K = 0x0e;
static constexpr uint8_t CH32RV_USB_HID_KEY_L = 0x0f;
static constexpr uint8_t CH32RV_USB_HID_KEY_M = 0x10;
static constexpr uint8_t CH32RV_USB_HID_KEY_N = 0x11;
static constexpr uint8_t CH32RV_USB_HID_KEY_O = 0x12;
static constexpr uint8_t CH32RV_USB_HID_KEY_P = 0x13;
static constexpr uint8_t CH32RV_USB_HID_KEY_Q = 0x14;
static constexpr uint8_t CH32RV_USB_HID_KEY_R = 0x15;
static constexpr uint8_t CH32RV_USB_HID_KEY_S = 0x16;
static constexpr uint8_t CH32RV_USB_HID_KEY_T = 0x17;
static constexpr uint8_t CH32RV_USB_HID_KEY_U = 0x18;
static constexpr uint8_t CH32RV_USB_HID_KEY_V = 0x19;
static constexpr uint8_t CH32RV_USB_HID_KEY_W = 0x1a;
static constexpr uint8_t CH32RV_USB_HID_KEY_X = 0x1b;
static constexpr uint8_t CH32RV_USB_HID_KEY_Y = 0x1c;
static constexpr uint8_t CH32RV_USB_HID_KEY_Z = 0x1d;
static constexpr uint8_t CH32RV_USB_HID_KEY_1 = 0x1e;
static constexpr uint8_t CH32RV_USB_HID_KEY_2 = 0x1f;
static constexpr uint8_t CH32RV_USB_HID_KEY_3 = 0x20;
static constexpr uint8_t CH32RV_USB_HID_KEY_4 = 0x21;
static constexpr uint8_t CH32RV_USB_HID_KEY_5 = 0x22;
static constexpr uint8_t CH32RV_USB_HID_KEY_6 = 0x23;
static constexpr uint8_t CH32RV_USB_HID_KEY_7 = 0x24;
static constexpr uint8_t CH32RV_USB_HID_KEY_8 = 0x25;
static constexpr uint8_t CH32RV_USB_HID_KEY_9 = 0x26;
static constexpr uint8_t CH32RV_USB_HID_KEY_0 = 0x27;
static constexpr uint8_t CH32RV_USB_HID_KEY_ENTER = 0x28;
static constexpr uint8_t CH32RV_USB_HID_KEY_ESCAPE = 0x29;
static constexpr uint8_t CH32RV_USB_HID_KEY_BACKSPACE = 0x2a;
static constexpr uint8_t CH32RV_USB_HID_KEY_TAB = 0x2b;
static constexpr uint8_t CH32RV_USB_HID_KEY_SPACE = 0x2c;
static constexpr uint8_t CH32RV_USB_HID_KEY_MINUS = 0x2d;
static constexpr uint8_t CH32RV_USB_HID_KEY_EQUAL = 0x2e;
static constexpr uint8_t CH32RV_USB_HID_KEY_LEFT_BRACKET = 0x2f;
static constexpr uint8_t CH32RV_USB_HID_KEY_RIGHT_BRACKET = 0x30;
static constexpr uint8_t CH32RV_USB_HID_KEY_BACKSLASH = 0x31;
static constexpr uint8_t CH32RV_USB_HID_KEY_SEMICOLON = 0x33;
static constexpr uint8_t CH32RV_USB_HID_KEY_APOSTROPHE = 0x34;
static constexpr uint8_t CH32RV_USB_HID_KEY_GRAVE = 0x35;
static constexpr uint8_t CH32RV_USB_HID_KEY_COMMA = 0x36;
static constexpr uint8_t CH32RV_USB_HID_KEY_DOT = 0x37;
static constexpr uint8_t CH32RV_USB_HID_KEY_SLASH = 0x38;
static constexpr uint8_t CH32RV_USB_HID_KEY_LANG1 = 0x90;
static constexpr uint8_t CH32RV_USB_HID_KEY_LANG2 = 0x91;
static constexpr uint8_t CH32RV_USB_HID_KEY_INTERNATIONAL1 = 0x87;
static constexpr uint8_t CH32RV_USB_HID_KEY_INTERNATIONAL3 = 0x89;
static constexpr uint8_t CH32RV_USB_HID_KEY_HENKAN = CH32RV_USB_HID_KEY_INTERNATIONAL3;
static constexpr uint8_t CH32RV_USB_HID_KEY_MUHENKAN = CH32RV_USB_HID_KEY_INTERNATIONAL1;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_LEFT_CTRL = 0x01;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_LEFT_SHIFT = 0x02;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_LEFT_ALT = 0x04;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_LEFT_GUI = 0x08;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_RIGHT_CTRL = 0x10;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_RIGHT_SHIFT = 0x20;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_RIGHT_ALT = 0x40;
static constexpr uint8_t CH32RV_USB_DEVICE_MOD_RIGHT_GUI = 0x80;
static constexpr uint8_t CH32RV_USB_DEVICE_MOUSE_LEFT = 0x01;
static constexpr uint8_t CH32RV_USB_DEVICE_MOUSE_RIGHT = 0x02;
static constexpr uint8_t CH32RV_USB_DEVICE_MOUSE_MIDDLE = 0x04;
static constexpr uint8_t CH32RV_USB_DEVICE_MOUSE_BACK = 0x08;
static constexpr uint8_t CH32RV_USB_DEVICE_MOUSE_FORWARD = 0x10;
static constexpr uint8_t CH32RV_USB_DEVICE_HID_REPORT_ID_KEYBOARD = 0x01;
static constexpr uint8_t CH32RV_USB_DEVICE_HID_REPORT_ID_MOUSE = 0x02;
static constexpr uint8_t CH32RV_USB_DEVICE_HID_REPORT_ID_GAMEPAD = 0x03;
static constexpr uint8_t CH32RV_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL = 0x04;
static constexpr uint8_t CH32RV_USB_DEVICE_HID_REPORT_ID_SYSTEM_CONTROL = 0x05;
static constexpr uint8_t CH32RV_USB_DEVICE_HID_REPORT_ID_VENDOR = 0x06;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_CENTER = 0x00;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_UP = 0x01;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_UP_RIGHT = 0x02;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_RIGHT = 0x03;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_DOWN_RIGHT = 0x04;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_DOWN = 0x05;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_DOWN_LEFT = 0x06;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_LEFT = 0x07;
static constexpr uint8_t CH32RV_USB_DEVICE_GAMEPAD_HAT_UP_LEFT = 0x08;
static constexpr uint16_t CH32RV_USB_DEVICE_CONSUMER_CONTROL_NEXT_TRACK = 0x00b5;
static constexpr uint16_t CH32RV_USB_DEVICE_CONSUMER_CONTROL_PREVIOUS_TRACK = 0x00b6;
static constexpr uint16_t CH32RV_USB_DEVICE_CONSUMER_CONTROL_PLAY_PAUSE = 0x00cd;
static constexpr uint16_t CH32RV_USB_DEVICE_CONSUMER_CONTROL_MUTE = 0x00e2;
static constexpr uint16_t CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP = 0x00e9;
static constexpr uint16_t CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_DOWN = 0x00ea;
static constexpr uint8_t CH32RV_USB_DEVICE_SYSTEM_CONTROL_POWER_OFF = 0x81;
static constexpr uint8_t CH32RV_USB_DEVICE_SYSTEM_CONTROL_STANDBY = 0x82;
static constexpr uint8_t CH32RV_USB_DEVICE_SYSTEM_CONTROL_WAKE_HOST = 0x83;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_NOTE_OFF = 0x08;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_NOTE_ON = 0x09;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_POLY_KEYPRESS = 0x0a;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_CONTROL_CHANGE = 0x0b;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_PROGRAM_CHANGE = 0x0c;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_CHANNEL_PRESSURE = 0x0d;
static constexpr uint8_t CH32RV_USB_DEVICE_MIDI_CIN_PITCH_BEND_CHANGE = 0x0e;

/* ---------------------------------------------------------- keyboard layouts
 * A layout is the table of what each key types (EspUsbDevice's keymap/*.h,
 * byte for byte: unshifted, Shift, AltGr, AltGr+Shift as Unicode). Each one is
 * an object of its own file, so a sketch links only the layouts it names -
 * all of them together would be 17 KB. `id` is the Windows language ID that
 * EspUsbDeviceKeyboardLayout used as its enum value. */
struct CH32RVUsbDeviceKeyboardLayout {
    uint16_t id;
    const uint16_t (*table)[4];
    uint16_t entries;
};

extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_ZH_TW;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_DA_DK;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_DE_DE;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_US;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_FI_FI;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_FR_FR;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_HU_HU;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_IT_IT;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_KO_KR;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_NL_NL;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_NB_NO;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_PT_BR;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_SV_SE;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_ZH_CN;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_EN_GB;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_PT_PT;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_ES_ES;
extern const CH32RVUsbDeviceKeyboardLayout CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_FR_CH;

/* ---------------------------------------------------------- reports */
struct CH32RVUsbDeviceBootKeyboardReport {
    uint8_t modifiers = 0;
    uint8_t reserved = 0;
    /* Up to six held usages, one per slot (0 = empty). */
    uint8_t keys[6] = {};
};

/* Full NKRO keyboard state: modifier byte + a bitmap of usages 0x00-0xDF.
 * Modifier usages 0xE0-0xE7 live in `modifiers`; press() / release() route
 * them there. */
struct CH32RVUsbDeviceNkroKeyboardReport {
    static constexpr size_t BitmapSize = 28;
    static constexpr uint8_t MaxBitmapUsage = 0xdf;

    uint8_t modifiers = 0;
    uint8_t bitmap[BitmapSize] = {};

    void clear()
    {
        modifiers = 0;
        for (size_t i = 0; i < BitmapSize; i++) {
            bitmap[i] = 0;
        }
    }
    /* False when the usage is above MaxBitmapUsage and not a modifier. */
    bool press(uint8_t usage)
    {
        if (usage >= 0xe0 && usage <= 0xe7) {
            modifiers |= (uint8_t)(1u << (usage - 0xe0));
            return true;
        }
        if (usage > MaxBitmapUsage) {
            return false;
        }
        bitmap[usage >> 3] |= (uint8_t)(1u << (usage & 7));
        return true;
    }
    bool release(uint8_t usage)
    {
        if (usage >= 0xe0 && usage <= 0xe7) {
            modifiers = (uint8_t)(modifiers & ~(1u << (usage - 0xe0)));
            return true;
        }
        if (usage > MaxBitmapUsage) {
            return false;
        }
        bitmap[usage >> 3] = (uint8_t)(bitmap[usage >> 3] & ~(1u << (usage & 7)));
        return true;
    }
    bool isDown(uint8_t usage) const
    {
        if (usage >= 0xe0 && usage <= 0xe7) {
            return (modifiers & (uint8_t)(1u << (usage - 0xe0))) != 0;
        }
        if (usage > MaxBitmapUsage) {
            return false;
        }
        return (bitmap[usage >> 3] & (uint8_t)(1u << (usage & 7))) != 0;
    }
};

/* Host LED (Lock) state. */
struct CH32RVUsbDeviceHidKeyboardOutputReport {
    uint8_t leds = 0;
    bool numLock = false;
    bool capsLock = false;
    bool scrollLock = false;
    bool compose = false;
    bool kana = false;

    void setLeds(uint8_t value)
    {
        leds = value;
        numLock = value & CH32RV_USB_DEVICE_KEYBOARD_LED_NUM_LOCK;
        capsLock = value & CH32RV_USB_DEVICE_KEYBOARD_LED_CAPS_LOCK;
        scrollLock = value & CH32RV_USB_DEVICE_KEYBOARD_LED_SCROLL_LOCK;
        compose = value & CH32RV_USB_DEVICE_KEYBOARD_LED_COMPOSE;
        kana = value & CH32RV_USB_DEVICE_KEYBOARD_LED_KANA;
    }
};

struct CH32RVUsbDeviceBootMouseReport {
    uint8_t buttons = 0;
    int8_t x = 0;
    int8_t y = 0;
    int8_t wheel = 0;
};

struct CH32RVUsbDeviceGamepadReport {
    int8_t x = 0;
    int8_t y = 0;
    int8_t z = 0;
    int8_t rz = 0;
    int8_t rx = 0;
    int8_t ry = 0;
    uint8_t hat = CH32RV_USB_DEVICE_GAMEPAD_HAT_CENTER;
    uint32_t buttons = 0;
};

enum CH32RVUsbDeviceHidReportType : uint8_t {
    CH32RV_USB_DEVICE_HID_REPORT_TYPE_INPUT = 0x01,
    CH32RV_USB_DEVICE_HID_REPORT_TYPE_OUTPUT = 0x02,
    CH32RV_USB_DEVICE_HID_REPORT_TYPE_FEATURE = 0x03,
};

struct CH32RVUsbDeviceHidReport {
    uint8_t reportId = 0;
    uint8_t reportType = 0;
    const uint8_t *data = nullptr;
    uint16_t length = 0;
};

struct CH32RVUsbDeviceHidProtocolEvent {
    uint8_t instance = 0;
    uint8_t protocol = 0;
};

struct CH32RVUsbDeviceCdcLineCoding {
    uint32_t baud = 0;
    uint8_t stopBits = 0;
    uint8_t parity = 0;
    uint8_t dataBits = 0;
};

struct CH32RVUsbDeviceCdcLineState {
    bool dtr = false;
    bool rts = false;
};

struct CH32RVUsbDeviceMidiPacket {
    uint8_t header = 0;
    uint8_t byte1 = 0;
    uint8_t byte2 = 0;
    uint8_t byte3 = 0;
};

/* A USB control request (SETUP packet), as the device stack hands it to a
 * class (CH32RVUsbDeviceFunction::usbControl). */
struct CH32RVUsbSetup {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
};

enum class CH32RVUsbControlStage : uint8_t {
    Setup,
    Data,
    Ack,
};

/* The bus speed the host chose, and what the descriptors are laid out for. */
/* The largest bulk packet this series' controllers reach (full speed 64,
 * high speed 512, SuperSpeed 1024): what the classes' default buffers are
 * sized from. A property of the chip, not a setting. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32X315)
#define CH32RV_USB_MAX_PACKET 1024           /* USBSS; CH32H415 and X305 have USBHS only (features.csv) */
#elif defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || \
    defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || \
    defined(CH32RV_SERIES_CH32X305)
#define CH32RV_USB_MAX_PACKET 512
#else
#define CH32RV_USB_MAX_PACKET 64
#endif

/* SuperSpeed bulk endpoints: packets a burst takes (bMaxBurst + 1 in the
 * endpoint companion). The USBSS driver offers the same OUT ERDY window. 4
 * measured on CH32H417 (2026-10-21, 32 KB transfers): IN 205 -> 234 MB/s, OUT
 * 138 -> 231 MB/s, data checked; 16 was no faster. */
#define CH32RV_USB_SS_BULK_BURST 4u

enum class CH32RVUsbSpeed : uint8_t {
    Full,                         /* 12 Mbit/s: bulk 64, EP0 64 */
    High,                         /* 480 Mbit/s: bulk 512, intervals in microframes */
    Super,                        /* 5 Gbit/s and up: bulk 1024, EP0 512, endpoint companions */
};

/* Whether a speed can happen on this series at all: none of its controllers
 * goes faster than CH32RV_USB_MAX_PACKET says. Constant, so the code for a
 * speed the chip cannot reach drops out. */
static constexpr bool ch32rv_usb_series_can(CH32RVUsbSpeed speed)
{
    return speed == CH32RVUsbSpeed::Full || (speed == CH32RVUsbSpeed::High && CH32RV_USB_MAX_PACKET >= 512) ||
           (speed == CH32RVUsbSpeed::Super && CH32RV_USB_MAX_PACKET >= 1024);
}

/* `actual == speed`, known false at build time when the series cannot reach it. */
static inline bool ch32rv_usb_at(CH32RVUsbSpeed actual, CH32RVUsbSpeed speed)
{
    return ch32rv_usb_series_can(speed) && actual == speed;
}


/* A borrowed transfer (Vendor writeDirect() / readDirect(), as EspUsbDevice's): why it was
 * refused, and how it ended. A refusal borrows nothing and reports nothing;
 * an accepted one is reported exactly once, whatever ends it. */
enum class CH32RVUsbTransferError : uint8_t {
    None = 0,
    NotSupported,
    NotMounted,      /* the host has not configured the interface */
    BadArgument,     /* no buffer, no length, beyond maxDirectTransfer(), or
                        a receive not whole packets */
    NotAligned,      /* not used: any alignment is taken */
    NotDmaCapable,   /* not used */
    Busy,            /* a transfer is already out on the endpoint */
    TransferFailed,  /* the controller refused it */
};

struct CH32RVUsbTransferSubmission {
    CH32RVUsbTransferError error = CH32RVUsbTransferError::None;
    uint32_t id = 0;     /* nonzero for an accepted loan */
    explicit operator bool() const { return error == CH32RVUsbTransferError::None; }
};

/* Continue: a transfer that is whole packets ends with no zero-length packet
 * (the host reads on); EndWithShortPacket adds one. */
enum class CH32RVUsbTransferTermination : uint8_t { Continue, EndWithShortPacket };

enum class CH32RVUsbTransferStatus : uint8_t { Success, BusReset, Stopped, TransferError, Cancelled };

struct CH32RVUsbTransferResult {
    const void *buffer = nullptr;
    size_t requestedBytes = 0;
    size_t actualBytes = 0;
    CH32RVUsbTransferStatus status = CH32RVUsbTransferStatus::Success;
    /* False after a reset, end() or a cancel: the controller may have moved part of it. */
    bool actualBytesKnown = true;
    uint32_t id = 0;
};

/* How write()'s data ends on the wire when the queue empties on a whole
 * packet: FunctionDefault is EndWithShortPacket for CDC (a terminal reads
 * line by line) and Continue for Vendor (a byte stream the host reads in
 * large requests). */
enum class CH32RVUsbStreamTermination : uint8_t { FunctionDefault, Continue, EndWithShortPacket };

/* CDC and Vendor buffers (EspUsbBulkStreamConfig's names). A capacity of 0
 * keeps the class's own buffer (twice the largest packet, 256 bytes at
 * least); a buffer is the sketch's, used from begin() to end(), and its
 * capacity says how big it is. A bigger capacity with no buffer is refused:
 * nothing is allocated. Receive needs room for two packets at the speed the
 * host picks (128 / 1024 / 2048 bytes). Vendor's onRxData() transfers land
 * in the receive buffer, whole packets of it. */
/* How Vendor receives: Stream (available() / read(); onRxData() switches to
 * handing each transfer over, at any time), Callback (the same, but begin()
 * fails without onRxData()), Borrowed (only into readDirect()'s buffers;
 * begin() fails without onDirectRxComplete()) or Disabled (the OUT endpoint
 * is never armed: the host's writes wait). CDC takes Stream only. */
enum class CH32RVUsbBulkRxMode : uint8_t { Stream, Callback, Borrowed, Disabled };

struct CH32RVUsbBulkStreamConfig {
    size_t rxCapacity = 0;
    size_t txCapacity = 0;
    uint8_t *rxBuffer = nullptr;
    uint8_t *txBuffer = nullptr;
    CH32RVUsbStreamTermination streamTermination = CH32RVUsbStreamTermination::FunctionDefault;
    CH32RVUsbBulkRxMode rxMode = CH32RVUsbBulkRxMode::Stream;
};
