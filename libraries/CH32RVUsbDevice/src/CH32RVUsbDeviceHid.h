/* HID classes, in EspUsbDevice's shape: keyboard (layouts, NKRO), mouse,
 * gamepad, consumer control, system control, a vendor-defined data channel and
 * a custom report descriptor. Included by CH32RVUsbDevice.h.
 *
 * As in EspUsbDevice, every HID class of a device shares one HID interface. A
 * single class is the interface as it stands (a keyboard or mouse alone is a
 * boot device, usable in a PC's BIOS); two or more are merged into one report
 * descriptor, each class under its own report ID (CH32RV_USB_DEVICE_HID_REPORT_ID_*).
 * The device therefore has one HID interface whatever the sketch declares.
 *
 * The callbacks are notifications: they run from deliverEvents(), never
 * inside the USB stack (ch32rv_deliver.h). */
#pragma once

/* Base of the HID classes: what the shared interface asks each of them. */
class CH32RVUsbDeviceHidClass {
public:
    /* EspUsbDeviceClass::begin(); nothing to do here - the device begins. */
    bool begin() { return true; }
    CH32RVUsbDevice &device() const { return _device; }
    /* False when the device refused it (declared after begin()). */
    bool attached() const { return _attached; }

    /* For the shared interface. */
    virtual uint8_t hidReportId() const { return 0; }
    virtual const uint8_t *hidReportDescriptor() const = 0;
    virtual uint16_t hidReportDescriptorLength() const = 0;
    /* Interrupt IN packet this class wants, 0 for no preference. */
    virtual uint16_t hidInEndpointSize() const { return 0; }
    /* HID boot protocol (1 keyboard, 2 mouse) when it is the only class. */
    virtual uint8_t hidBootProtocol() const { return 0; }
    /* An interrupt OUT endpoint, when it is the only class. */
    virtual bool hidOutEndpoint() const { return false; }
    virtual void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data,
                                uint16_t length)
    {
        (void)reportId, (void)reportType, (void)data, (void)length;
    }
    virtual void onHidSetProtocol(uint8_t protocol) { (void)protocol; }
    /* GET_REPORT: the class's current report of `reportType` (1 input, 3
     * feature) without the report ID, into `out`; its length, 0 to stall. */
    virtual uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const
    {
        (void)reportType, (void)out, (void)capacity;
        return 0;
    }
    virtual void onBusAttached() {}
    virtual void onBusDetached() {}
    /* From deliverEvents(): run the sketch's callbacks for what the hooks
     * above recorded; how many ran. */
    virtual size_t hidDeliver() { return 0; }

protected:
    explicit CH32RVUsbDeviceHidClass(CH32RVUsbDevice &device);
    /* The report ID this class's reports go out under: 0 when it is the only
     * class (unless its descriptor declares one), hidReportId() when merged. */
    uint8_t classReportId() const;
    bool sendHidReport(uint8_t reportId, const void *data, size_t length, uint32_t timeoutMs);

    CH32RVUsbDevice &_device;
    bool _attached = false;

private:
    friend class HidInterface;
    CH32RVUsbDeviceHidClass *_hidNext = nullptr;   /* the shared interface's next class */
};

/* ---------------------------------------------------------- keyboard */
class CH32RVUsbDeviceHidKeyboard : public CH32RVUsbDeviceHidClass {
public:
    typedef CH32RVCallback<void(const CH32RVUsbDeviceHidKeyboardOutputReport &report)> OutputReportCallback;
    typedef CH32RVCallback<void(const CH32RVUsbDeviceHidProtocolEvent &event)> ProtocolCallback;

    explicit CH32RVUsbDeviceHidKeyboard(CH32RVUsbDevice &device = USBDevice);

    bool sendReport(const CH32RVUsbDeviceBootKeyboardReport &report, uint32_t timeoutMs = 100);
    /* The whole held-key state as one report; needs enableNkro() before the
     * device begins (false otherwise). Folds to 6 keys when the host chose
     * boot protocol. */
    bool sendReport(const CH32RVUsbDeviceNkroKeyboardReport &report, uint32_t timeoutMs = 100);
    bool pressUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
    bool pressKey(char key, uint32_t timeoutMs = 100);
    bool tapUsage(uint8_t usage, uint8_t modifiers = 0, uint32_t holdMs = 10);
    bool tapKey(char key, uint32_t holdMs = 10);
    /* Type text with the current layout. Characters the layout cannot type
     * stop it with false. */
    bool write(const char *text, uint32_t interKeyDelayMs = 5);
    bool releaseUsage(uint8_t usage, uint32_t timeoutMs = 100);
    bool releaseAll(uint32_t timeoutMs = 100);
    /* The host's keyboard layout, which decides the keys write()/tapKey()
     * press: CH32RV_USB_DEVICE_KEYBOARD_LAYOUT_JA_JP and so on. US by default. */
    void setLayout(const CH32RVUsbDeviceKeyboardLayout &layout) { _layout = &layout; }
    const CH32RVUsbDeviceKeyboardLayout &layout() const;
    /* From deliverEvents(), with the LEDs then. */
    void onOutputReport(OutputReportCallback callback) { _outputCallback = callback; }
    /* Lock LEDs as the host last set them; cleared on bus reset / unplug. */
    CH32RVUsbDeviceHidKeyboardOutputReport ledState() const;
    /* From deliverEvents(), with the protocol then. */
    void onProtocol(ProtocolCallback callback) { _protocolCallback = callback; }
    /* 1 report protocol, 0 boot protocol (a BIOS). */
    uint8_t protocol() const { return _protocol; }

    /* N-key rollover, before the device begins: a bitmap of usages 0x00-0xDF
     * (JIS International / LANG keys included). Default off (6 keys). False
     * once the device has begun (the report descriptor would change). */
    bool enableNkro(bool enable = true);
    bool nkroEnabled() const { return _nkro; }
    /* What the host was last told (NKRO only). */
    const CH32RVUsbDeviceNkroKeyboardReport &heldState() const { return _nkroState; }

    uint8_t hidReportId() const override { return CH32RV_USB_DEVICE_HID_REPORT_ID_KEYBOARD; }
    const uint8_t *hidReportDescriptor() const override;
    uint16_t hidReportDescriptorLength() const override;
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override;
    uint16_t hidInEndpointSize() const override { return _nkro ? 32 : 8; }
    uint8_t hidBootProtocol() const override { return 1; }
    void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data,
                        uint16_t length) override;
    void onHidSetProtocol(uint8_t protocol) override;
    void onBusAttached() override;
    void onBusDetached() override;
    size_t hidDeliver() override;

private:
    bool asciiToUsage(char key, uint8_t &usage, uint8_t &modifiers) const;
    bool sendNkroReport(uint32_t timeoutMs = 100);

    CH32RVUsbDeviceBootKeyboardReport _report;
    CH32RVUsbDeviceNkroKeyboardReport _nkroState;
    const CH32RVUsbDeviceKeyboardLayout *_layout = nullptr;
    uint8_t _protocol = 1;
    bool _nkro = false;
    volatile uint8_t _leds = 0;
    bool _ledsPending = false, _protocolPending = false;
    OutputReportCallback _outputCallback = nullptr;
    ProtocolCallback _protocolCallback = nullptr;
};

/* ---------------------------------------------------------- mouse */
class CH32RVUsbDeviceHidMouse : public CH32RVUsbDeviceHidClass {
public:
    explicit CH32RVUsbDeviceHidMouse(CH32RVUsbDevice &device = USBDevice);

    bool sendReport(const CH32RVUsbDeviceBootMouseReport &report, uint32_t timeoutMs = 100);
    bool sendState(uint32_t timeoutMs = 100);
    bool move(int8_t x, int8_t y, int8_t wheel = 0, uint8_t buttons = 0, uint32_t timeoutMs = 100);
    bool wheel(int8_t wheel, uint32_t timeoutMs = 100);
    bool press(uint8_t buttons, uint32_t timeoutMs = 100);
    bool release(uint8_t buttons, uint32_t timeoutMs = 100);
    bool releaseAll(uint32_t timeoutMs = 100);
    bool click(uint8_t button, uint32_t holdMs = 10);
    uint8_t buttons() const { return _buttons; }

    uint8_t hidReportId() const override { return CH32RV_USB_DEVICE_HID_REPORT_ID_MOUSE; }
    const uint8_t *hidReportDescriptor() const override;
    uint16_t hidReportDescriptorLength() const override;
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override;
    uint16_t hidInEndpointSize() const override { return 8; }
    uint8_t hidBootProtocol() const override { return 2; }

private:
    uint8_t _buttons = 0;
};

/* ---------------------------------------------------------- custom */
/* Your own report descriptor. When it is merged with other classes, give its
 * reports IDs of their own and send with them. */
class CH32RVUsbDeviceHidCustom : public CH32RVUsbDeviceHidClass {
public:
    CH32RVUsbDeviceHidCustom(CH32RVUsbDevice &device, const uint8_t *reportDescriptor,
                             uint16_t reportDescriptorLength, uint16_t inputReportSize = 64);

    bool sendReport(const void *data, size_t length, uint8_t reportId = 0, uint32_t timeoutMs = 100);

    const uint8_t *hidReportDescriptor() const override { return _desc; }
    uint16_t hidReportDescriptorLength() const override { return _descLength; }
    uint16_t hidInEndpointSize() const override { return _inputReportSize; }

private:
    const uint8_t *_desc;
    uint16_t _descLength;
    uint16_t _inputReportSize;
};

/* ---------------------------------------------------------- vendor */
/* A raw byte pipe over HID: needs no driver on any OS (hidapi, WebHID).
 * reportSize is the payload of one report, at most maxReportSize() (63 at full
 * speed: the report ID takes the first byte of the 64-byte packet). Reports go
 * out under report ID CH32RV_USB_DEVICE_HID_REPORT_ID_VENDOR (6). */
class CH32RVUsbDeviceHidVendor : public CH32RVUsbDeviceHidClass {
public:
    typedef CH32RVCallback<void(const CH32RVUsbDeviceHidReport &report)> ReportCallback;

    /* reportSize: up to maxReportSize() - 63 on full-speed-only series, the
     * largest packet less the report ID on high speed / SuperSpeed ones (511
     * or 1023). At full speed a report longer than 63 goes in several 64-byte
     * packets. */
    explicit CH32RVUsbDeviceHidVendor(CH32RVUsbDevice &device = USBDevice, uint16_t reportSize = 63);
    /* The top-level usage the report descriptor declares (Usage Page,
     * Usage): what a host looks the interface up by - an OEP probe is
     * 0xFF4F / 0x45. Before begin(). */
    void setUsage(uint16_t usagePage, uint8_t usage);

    bool sendInput(const void *data, size_t length, uint32_t timeoutMs = 100);
    /* The Feature report GET_REPORT answers from now on (copied; at most
     * reportSize() bytes). No interrupt report goes out. */
    bool setFeatureReport(const void *data, size_t length);
    /* From deliverEvents(). One report is kept until then: from the OUT
     * endpoint the next waits in the host; by SET_REPORT (a feature report,
     * or an output report on a composite interface) it is dropped
     * (droppedReports()). The data is valid during the call. */
    void onOutputReport(ReportCallback callback) { _outputCallback = callback; }
    void onFeatureReport(ReportCallback callback) { _featureCallback = callback; }
    uint32_t droppedReports() const { return _dropped; }
    uint16_t reportSize() const { return _reportSize; }
    static uint16_t maxReportSize();

    uint8_t hidReportId() const override { return CH32RV_USB_DEVICE_HID_REPORT_ID_VENDOR; }
    const uint8_t *hidReportDescriptor() const override { return _desc; }
    uint16_t hidReportDescriptorLength() const override { return _descLength; }
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override;
    uint16_t hidInEndpointSize() const override;
    bool hidOutEndpoint() const override { return true; }
    void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data,
                        uint16_t length) override;
    size_t hidDeliver() override;
    void onBusDetached() override;

private:
    void build();
    uint16_t _reportSize;
    uint16_t _usagePage = 0xff00;
    uint8_t _usage = 0x01;
    uint8_t _desc[32] = {};
    uint8_t _descLength = 0;
    ReportCallback _outputCallback = nullptr;
    ReportCallback _featureCallback = nullptr;
    /* The report waiting for hidDeliver(); _heldLength -1: none. */
    uint8_t _held[CH32RV_USB_MAX_PACKET > 64 ? CH32RV_USB_MAX_PACKET : 64];
    uint8_t _feature[CH32RV_USB_MAX_PACKET > 64 ? CH32RV_USB_MAX_PACKET : 64] = {};
    uint16_t _featureLength = 0;
    int16_t _heldLength = -1;
    uint8_t _heldId = 0, _heldType = 0;
    uint32_t _dropped = 0;
};

/* ---------------------------------------------------------- gamepad */
class CH32RVUsbDeviceHidGamepad : public CH32RVUsbDeviceHidClass {
public:
    explicit CH32RVUsbDeviceHidGamepad(CH32RVUsbDevice &device = USBDevice);

    bool sendReport(const CH32RVUsbDeviceGamepadReport &report, uint32_t timeoutMs = 100);
    bool send(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry, uint8_t hat,
              uint32_t buttons, uint32_t timeoutMs = 100);
    bool releaseAll(uint32_t timeoutMs = 100);

    uint8_t hidReportId() const override { return CH32RV_USB_DEVICE_HID_REPORT_ID_GAMEPAD; }
    const uint8_t *hidReportDescriptor() const override;
    uint16_t hidReportDescriptorLength() const override;
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override;
    uint16_t hidInEndpointSize() const override { return 16; }

private:
    CH32RVUsbDeviceGamepadReport _report;
};

/* ---------------------------------------------------------- consumer control */
/* Media keys: CH32RV_USB_DEVICE_CONSUMER_CONTROL_VOLUME_UP and other Consumer
 * page usages (0..0x3FF). */
class CH32RVUsbDeviceHidConsumerControl : public CH32RVUsbDeviceHidClass {
public:
    explicit CH32RVUsbDeviceHidConsumerControl(CH32RVUsbDevice &device = USBDevice);

    bool press(uint16_t usage, uint32_t timeoutMs = 100);
    bool release(uint32_t timeoutMs = 100);
    bool click(uint16_t usage, uint32_t holdMs = 10);
    bool sendUsage(uint16_t usage, uint32_t timeoutMs = 100);
    uint16_t usage() const { return _usage; }

    uint8_t hidReportId() const override { return CH32RV_USB_DEVICE_HID_REPORT_ID_CONSUMER_CONTROL; }
    const uint8_t *hidReportDescriptor() const override;
    uint16_t hidReportDescriptorLength() const override;
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override;
    uint16_t hidInEndpointSize() const override { return 8; }

private:
    uint16_t _usage = 0;
};

/* ---------------------------------------------------------- system control */
/* Power off / standby / wake host (CH32RV_USB_DEVICE_SYSTEM_CONTROL_*). */
class CH32RVUsbDeviceHidSystemControl : public CH32RVUsbDeviceHidClass {
public:
    explicit CH32RVUsbDeviceHidSystemControl(CH32RVUsbDevice &device = USBDevice);

    bool press(uint8_t usage, uint32_t timeoutMs = 100);
    bool release(uint32_t timeoutMs = 100);
    bool click(uint8_t usage, uint32_t holdMs = 10);
    bool sendUsage(uint8_t usage, uint32_t timeoutMs = 100);
    uint8_t usage() const { return _usage; }

    uint8_t hidReportId() const override { return CH32RV_USB_DEVICE_HID_REPORT_ID_SYSTEM_CONTROL; }
    const uint8_t *hidReportDescriptor() const override;
    uint16_t hidReportDescriptorLength() const override;
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override;
    uint16_t hidInEndpointSize() const override { return 8; }

private:
    uint8_t _usage = 0;
};
