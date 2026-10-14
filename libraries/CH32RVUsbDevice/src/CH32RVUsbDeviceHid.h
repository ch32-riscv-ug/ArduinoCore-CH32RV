/* HID: a generic interface with the sketch's own report descriptor, and a
 * keyboard and a mouse built on it. Included by CH32RVUsbDevice.h.
 *
 * One HID interface per device by default (CFG_TUD_HID = 1); a sketch that
 * wants a keyboard and a mouse as separate interfaces raises it in build_opt.h
 * (-DCFG_TUD_HID=2), or puts both in one report descriptor with report IDs. */
#pragma once

class CH32RVUsbDeviceHid : public CH32RVUsbDeviceFunction {
public:
    /* `report` is the HID report descriptor, kept by pointer (must outlive
     * the device). `bootProtocol` is 0, 1 (keyboard) or 2 (mouse): a boot
     * device works in a PC's BIOS too. `outEndpoint` adds an interrupt OUT
     * endpoint for output reports; without it they arrive as SET_REPORT on
     * the control endpoint, which every host also supports. */
    CH32RVUsbDeviceHid(CH32RVUsbDevice &device, const uint8_t *report, uint16_t reportLength,
                       uint8_t bootProtocol = 0, bool outEndpoint = false);

    /* The host is ready for the next input report. */
    bool ready();
    /* Send an input report (reportId 0 when the descriptor has none). Waits
     * up to `timeoutMs` for the previous one to go out. */
    bool sendReport(uint8_t reportId, const void *data, uint16_t length, uint32_t timeoutMs = 50);

    /* Output / feature reports from the host (keyboard LEDs, a vendor
     * command). Called from the USB stack, not an interrupt. */
    typedef void (*OutputReportCallback)(uint8_t reportId, const uint8_t *data, uint16_t length);
    void onOutputReport(OutputReportCallback cb) { _onOutput = cb; }

    /* For the USB stack. */
    const uint8_t *reportDescriptor() const { return _report; }
    uint16_t reportLength() const { return _reportLength; }
    uint8_t bootProtocol() const { return _boot; }
    bool outEndpoint() const { return _out; }
    virtual void outputReport(uint8_t reportId, const uint8_t *data, uint16_t length);

private:
    const uint8_t *_report;
    uint16_t _reportLength;
    uint8_t _boot;
    bool _out;
    OutputReportCallback _onOutput = nullptr;
};

/* A vendor-defined HID with `size`-byte input and output reports (1..64): a
 * driverless data channel - every OS opens it from user space (hidapi, WebHID)
 * without installing anything. Output reports arrive at onOutputReport(). */
class CH32RVUsbDeviceHidGeneric : public CH32RVUsbDeviceHid {
public:
    explicit CH32RVUsbDeviceHidGeneric(CH32RVUsbDevice &device = USBDevice, uint8_t size = 64);

    uint8_t reportSize() const { return _size; }
    /* Send one input report; shorter data is zero-padded to reportSize(). */
    bool send(const void *data, uint8_t length, uint32_t timeoutMs = 50);

private:
    uint8_t _desc[34];
    uint8_t _size;
};

/* A boot keyboard. print("text") types it (US layout, ASCII only). */
class CH32RVUsbDeviceKeyboard : public CH32RVUsbDeviceHid, public Print {
public:
    explicit CH32RVUsbDeviceKeyboard(CH32RVUsbDevice &device = USBDevice);

    /* Press and release one character (ASCII, US layout). */
    size_t write(uint8_t c) override;
    using Print::write;

    /* HID usage codes (HID_KEY_* in TinyUSB's hid.h, e.g. 0x28 = Enter) and
     * modifier bits (0x01 = left Ctrl, 0x02 = left Shift, 0x04 = left Alt,
     * 0x08 = left GUI). Up to six keys at once. */
    bool press(uint8_t keycode, uint8_t modifiers = 0);
    bool release(uint8_t keycode);
    bool releaseAll();

    /* Num / Caps / Scroll Lock as the host last set them (bit 0 / 1 / 2). */
    uint8_t leds() const { return _leds; }

    void outputReport(uint8_t reportId, const uint8_t *data, uint16_t length) override;

private:
    bool send();
    uint8_t _modifiers = 0;
    uint8_t _keys[6] = {};
    uint8_t _leds = 0;
};

/* A boot mouse: relative moves, three buttons and a wheel. */
class CH32RVUsbDeviceMouse : public CH32RVUsbDeviceHid {
public:
    explicit CH32RVUsbDeviceMouse(CH32RVUsbDevice &device = USBDevice);

    static constexpr uint8_t LEFT = 0x01, RIGHT = 0x02, MIDDLE = 0x04;

    bool move(int8_t x, int8_t y, int8_t wheel = 0);
    bool press(uint8_t buttons = LEFT);
    bool release(uint8_t buttons = LEFT);
    bool click(uint8_t buttons = LEFT);

private:
    uint8_t _buttons = 0;
};
