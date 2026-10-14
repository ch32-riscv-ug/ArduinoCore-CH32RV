/* HID: TinyUSB's hid_device, with the report descriptor from the object. */
#include "CH32RVUsbDevice.h"

#include <string.h>

#include "tusb.h"
#include "device/usbd_pvt.h"

#if CH32RV_TINYUSB_DEVICE

static const usbd_class_driver_t hid_driver = {
    NULL, hidd_init, hidd_deinit, hidd_reset, hidd_open, hidd_control_xfer_cb, hidd_xfer_cb, NULL, NULL,
};

/* Input on endpoint n IN, and with outEndpoint the same number OUT. Polled
 * every 10 ms (full speed: frames). */
static uint16_t hid_descriptor(const CH32RVUsbDeviceFunction &self, uint8_t *out, uint8_t itf,
                               uint8_t *ep, uint8_t str)
{
    const CH32RVUsbDeviceHid &h = static_cast<const CH32RVUsbDeviceHid &>(self);
    const uint8_t n = (*ep)++;
    if (h.outEndpoint()) {
        const uint8_t d[] = {TUD_HID_INOUT_DESCRIPTOR(itf, str, h.bootProtocol(), h.reportLength(), n,
                                                      (uint8_t)(0x80 | n), CFG_TUD_HID_EP_BUFSIZE, 10)};
        memcpy(out, d, sizeof d);
        return sizeof d;
    }
    const uint8_t d[] = {TUD_HID_DESCRIPTOR(itf, str, h.bootProtocol(), h.reportLength(),
                                            (uint8_t)(0x80 | n), CFG_TUD_HID_EP_BUFSIZE, 10)};
    memcpy(out, d, sizeof d);
    return sizeof d;
}

static const CH32RVUsbFunctionType hid_type = {&hid_driver, hid_descriptor, 1, CFG_TUD_HID};

static CH32RVUsbDeviceHid *hids[CFG_TUD_HID];

CH32RVUsbDeviceHid::CH32RVUsbDeviceHid(CH32RVUsbDevice &device, const uint8_t *report,
                                       uint16_t reportLength, uint8_t bootProtocol, bool outEndpoint)
    : CH32RVUsbDeviceFunction(device, hid_type, "HID"), _report(report),
      _reportLength(reportLength), _boot(bootProtocol), _out(outEndpoint)
{
    if (attached()) {
        hids[instance()] = this;
    }
}

bool CH32RVUsbDeviceHid::ready()
{
    ch32rv_usb_task();
    return device().mounted() && tud_hid_n_ready(instance());
}

bool CH32RVUsbDeviceHid::sendReport(uint8_t reportId, const void *data, uint16_t length,
                                    uint32_t timeoutMs)
{
    const uint32_t t0 = millis();
    while (!ready()) {
        if (!device().mounted() || (uint32_t)(millis() - t0) >= timeoutMs) {
            return false;
        }
    }
    return tud_hid_n_report(instance(), reportId, data, length);
}

void CH32RVUsbDeviceHid::outputReport(uint8_t reportId, const uint8_t *data, uint16_t length)
{
    if (_onOutput) {
        _onOutput(reportId, data, length);
    }
}

extern "C" uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    return instance < CFG_TUD_HID && hids[instance] ? hids[instance]->reportDescriptor() : NULL;
}

/* GET_REPORT is optional and rarely used: stall it. */
extern "C" uint16_t tud_hid_get_report_cb(uint8_t, uint8_t, hid_report_type_t, uint8_t *, uint16_t)
{
    return 0;
}

extern "C" void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t type,
                                      uint8_t const *buffer, uint16_t size)
{
    (void)type;
    if (instance < CFG_TUD_HID && hids[instance]) {
        hids[instance]->outputReport(report_id, buffer, size);
    }
}

/* ---------------------------------------------------------- generic */
static uint8_t clamp_size(uint8_t size)
{
    return size < 1 ? 1 : size > CFG_TUD_HID_EP_BUFSIZE ? (uint8_t)CFG_TUD_HID_EP_BUFSIZE : size;
}

CH32RVUsbDeviceHidGeneric::CH32RVUsbDeviceHidGeneric(CH32RVUsbDevice &device, uint8_t size)
    : CH32RVUsbDeviceHid(device, _desc, sizeof _desc, 0, true), _size(clamp_size(size))
{
    const uint8_t n = _size;
    const uint8_t d[] = {TUD_HID_REPORT_DESC_GENERIC_INOUT(n)};
    static_assert(sizeof d == sizeof _desc, "generic report descriptor size");
    memcpy(_desc, d, sizeof d);
    setName("HID");
}

bool CH32RVUsbDeviceHidGeneric::send(const void *data, uint8_t length, uint32_t timeoutMs)
{
    uint8_t report[CFG_TUD_HID_EP_BUFSIZE] = {};
    memcpy(report, data, length < _size ? length : _size);
    return sendReport(0, report, _size, timeoutMs);
}

/* ---------------------------------------------------------- keyboard */
static const uint8_t keyboard_report[] = {TUD_HID_REPORT_DESC_KEYBOARD()};
static const uint8_t ascii_to_key[128][2] = {HID_ASCII_TO_KEYCODE};

CH32RVUsbDeviceKeyboard::CH32RVUsbDeviceKeyboard(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHid(device, keyboard_report, sizeof keyboard_report, HID_ITF_PROTOCOL_KEYBOARD)
{
    setName("Keyboard");
}

bool CH32RVUsbDeviceKeyboard::send()
{
    const uint8_t report[8] = {_modifiers, 0, _keys[0], _keys[1], _keys[2], _keys[3], _keys[4], _keys[5]};
    return sendReport(0, report, sizeof report);
}

bool CH32RVUsbDeviceKeyboard::press(uint8_t keycode, uint8_t modifiers)
{
    _modifiers |= modifiers;
    if (keycode) {
        uint8_t *slot = nullptr;
        for (uint8_t &k : _keys) {
            if (k == keycode) {
                slot = &k;
                break;
            }
            if (!k && !slot) {
                slot = &k;
            }
        }
        if (!slot) {
            return false;          /* six keys already down */
        }
        *slot = keycode;
    }
    return send();
}

bool CH32RVUsbDeviceKeyboard::release(uint8_t keycode)
{
    for (uint8_t &k : _keys) {
        if (k == keycode) {
            k = 0;
        }
    }
    return send();
}

bool CH32RVUsbDeviceKeyboard::releaseAll()
{
    _modifiers = 0;
    memset(_keys, 0, sizeof _keys);
    return send();
}

size_t CH32RVUsbDeviceKeyboard::write(uint8_t c)
{
    if (c >= 128 || !ascii_to_key[c][1]) {
        return 0;
    }
    const uint8_t mod = ascii_to_key[c][0] ? KEYBOARD_MODIFIER_LEFTSHIFT : 0;
    const bool ok = press(ascii_to_key[c][1], mod) && releaseAll();
    return ok ? 1 : 0;
}

void CH32RVUsbDeviceKeyboard::outputReport(uint8_t reportId, const uint8_t *data, uint16_t length)
{
    if (length >= 1) {
        _leds = data[0];
    }
    CH32RVUsbDeviceHid::outputReport(reportId, data, length);
}

/* ---------------------------------------------------------- mouse */
static const uint8_t mouse_report[] = {TUD_HID_REPORT_DESC_MOUSE()};

CH32RVUsbDeviceMouse::CH32RVUsbDeviceMouse(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHid(device, mouse_report, sizeof mouse_report, HID_ITF_PROTOCOL_MOUSE)
{
    setName("Mouse");
}

bool CH32RVUsbDeviceMouse::move(int8_t x, int8_t y, int8_t wheel)
{
    const int8_t report[5] = {(int8_t)_buttons, x, y, wheel, 0};
    return sendReport(0, report, sizeof report);
}

bool CH32RVUsbDeviceMouse::press(uint8_t buttons)
{
    _buttons |= buttons;
    return move(0, 0);
}

bool CH32RVUsbDeviceMouse::release(uint8_t buttons)
{
    _buttons &= (uint8_t)~buttons;
    return move(0, 0);
}

bool CH32RVUsbDeviceMouse::click(uint8_t buttons)
{
    return press(buttons) && release(buttons);
}

#else /* !CH32RV_TINYUSB_DEVICE */

static uint16_t hid_descriptor(const CH32RVUsbDeviceFunction &, uint8_t *, uint8_t, uint8_t *, uint8_t)
{
    return 0;
}

static const CH32RVUsbFunctionType hid_type = {nullptr, hid_descriptor, 1, 1};

CH32RVUsbDeviceHid::CH32RVUsbDeviceHid(CH32RVUsbDevice &device, const uint8_t *report,
                                       uint16_t reportLength, uint8_t bootProtocol, bool outEndpoint)
    : CH32RVUsbDeviceFunction(device, hid_type, "HID"), _report(report),
      _reportLength(reportLength), _boot(bootProtocol), _out(outEndpoint)
{
}

bool CH32RVUsbDeviceHid::ready() { return false; }
bool CH32RVUsbDeviceHid::sendReport(uint8_t, const void *, uint16_t, uint32_t) { return false; }
void CH32RVUsbDeviceHid::outputReport(uint8_t, const uint8_t *, uint16_t) {}

CH32RVUsbDeviceHidGeneric::CH32RVUsbDeviceHidGeneric(CH32RVUsbDevice &device, uint8_t size)
    : CH32RVUsbDeviceHid(device, _desc, sizeof _desc, 0, true), _size(size) {}
bool CH32RVUsbDeviceHidGeneric::send(const void *, uint8_t, uint32_t) { return false; }

CH32RVUsbDeviceKeyboard::CH32RVUsbDeviceKeyboard(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHid(device, nullptr, 0, 1) {}
bool CH32RVUsbDeviceKeyboard::send() { return false; }
bool CH32RVUsbDeviceKeyboard::press(uint8_t, uint8_t) { return false; }
bool CH32RVUsbDeviceKeyboard::release(uint8_t) { return false; }
bool CH32RVUsbDeviceKeyboard::releaseAll() { return false; }
size_t CH32RVUsbDeviceKeyboard::write(uint8_t) { return 0; }
void CH32RVUsbDeviceKeyboard::outputReport(uint8_t, const uint8_t *, uint16_t) {}

CH32RVUsbDeviceMouse::CH32RVUsbDeviceMouse(CH32RVUsbDevice &device)
    : CH32RVUsbDeviceHid(device, nullptr, 0, 2) {}
bool CH32RVUsbDeviceMouse::move(int8_t, int8_t, int8_t) { return false; }
bool CH32RVUsbDeviceMouse::press(uint8_t) { return false; }
bool CH32RVUsbDeviceMouse::release(uint8_t) { return false; }
bool CH32RVUsbDeviceMouse::click(uint8_t) { return false; }

#endif
