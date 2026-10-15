/* CH32RVUsbHost: HID (keyboards, mice, consumer / system control, gamepads,
 * vendor reports). TinyUSB's HID host driver does the transfers; this file
 * turns reports into EspUsbHost's events. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#include "CH32RVUsbHostHidLayout.h"

#if CH32RV_TINYUSB_HOST && CFG_TUH_HID

namespace {

/* One mounted HID interface (TinyUSB's index). */
struct HidItf {
    bool mounted;
    uint8_t daddr, itf, protocol;            /* itf protocol: 1 keyboard, 2 mouse */
    uint8_t reportCount;
    tuh_hid_report_info_t reports[CH32RV_USB_HOST_HID_REPORTS];
    CH32RVUsbHostMouseReportLayout mouse;
    uint8_t lastKeys[8];
    uint16_t lastMouseButtons;
    uint16_t lastConsumer;
    uint8_t lastSystem;
    uint8_t lastGamepad[16];
    uint8_t lastGamepadLength;
    uint8_t leds;                            /* what setKeyboardLeds() last sent */
};
HidItf hids[CFG_TUH_HID];

CH32RVUsbHost::KeyboardCallback keyboard_cb;
CH32RVUsbHost::MouseCallback mouse_cb;
CH32RVUsbHost::ConsumerControlCallback consumer_cb;
CH32RVUsbHost::SystemControlCallback system_cb;
CH32RVUsbHost::GamepadCallback gamepad_cb;
CH32RVUsbHost::HIDVendorInputCallback vendor_cb;
CH32RVUsbHost::HIDInputCallback input_cb;
CH32RVUsbHost::HIDReportDescriptorCallback descriptor_cb;
const CH32RVUsbHostKeyboardLayout *layout;

const usbh_class_driver_t hid_driver = {
    "HID", hidh_init, hidh_deinit, hidh_open, hidh_set_config, hidh_xfer_cb, hidh_close,
};

CH32RVUsbHostGroup hid_group = {&hid_driver, nullptr, nullptr, nullptr};

/* Joins when this file is linked: something in it was called. Report
 * protocol, as a host OS runs it: the report descriptor then says what
 * arrives, report IDs included (HID 1.11 7.2.6). */
struct Join {
    Join()
    {
        tuh_hid_set_default_protocol(HID_PROTOCOL_REPORT);
        ch32rv_usb_host_join(hid_group);
    }
} join;

const CH32RVUsbHostKeyboardLayout &current_layout()
{
    return layout ? *layout : CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_US;
}

uint8_t keypad_ascii(uint8_t keycode, bool numLock)
{
    switch (keycode) {
    case 0x54: return '/';
    case 0x55: return '*';
    case 0x56: return '-';
    case 0x57: return '+';
    case 0x58: return '\r';
    case 0x67: return '=';
    default: break;
    }
    if (!numLock) {
        return 0;
    }
    if (keycode >= 0x59 && keycode <= 0x61) {
        return (uint8_t)('1' + (keycode - 0x59));
    }
    return keycode == 0x62 ? '0' : keycode == 0x63 ? '.' : 0;
}

/* EspUsbHost's espUsbHostKeycodeToUnicode(), with the layout as a table. */
uint16_t keycode_unicode(uint8_t keycode, uint8_t modifiers, bool capsLock, bool numLock)
{
    const CH32RVUsbHostKeyboardLayout &l = current_layout();
    if (keycode >= l.size) {
        return 0;
    }
    if ((keycode >= 0x54 && keycode <= 0x63) || keycode == 0x67) {
        return keypad_ascii(keycode, numLock);
    }
    const uint16_t(*table)[4] = l.table;
    const uint8_t shift = (modifiers & 0x22) ? 1 : 0;     /* left / right shift */
    uint8_t effective = shift;
    if (capsLock) {
        const uint16_t base = table[keycode][0];
        uint16_t upper = base;
        if (base >= 'a' && base <= 'z') {
            upper = (uint16_t)(base - 0x20);
        } else if (base >= 0xE0 && base <= 0xFE && base != 0xF7) {
            upper = (uint16_t)(base - 0x20);
        }
        if (upper != base && upper == table[keycode][1]) {
            effective ^= 1u;
        }
    }
    if (modifiers & 0x40) {                               /* AltGr */
        if (shift && table[keycode][3]) {
            return table[keycode][3];
        }
        if (table[keycode][2]) {
            return table[keycode][2];
        }
    }
    return table[keycode][effective];
}

int find_index(uint8_t daddr, uint8_t itf)
{
    for (int i = 0; i < CFG_TUH_HID; i++) {
        if (hids[i].mounted && hids[i].daddr == daddr && (itf == 0xFF || hids[i].itf == itf)) {
            return i;
        }
    }
    return -1;
}

/* The first HID interface (of `address`, or any) whose reports include one of
 * `usagePage`; -1 when none. */
int find_by_page(uint8_t address, uint16_t usagePage, uint8_t *reportId)
{
    for (int i = 0; i < CFG_TUH_HID; i++) {
        const HidItf &h = hids[i];
        if (!h.mounted || (address != CH32RV_USB_HOST_ANY_ADDRESS && h.daddr != address)) {
            continue;
        }
        for (uint8_t r = 0; r < h.reportCount; r++) {
            if (h.reports[r].usage_page == usagePage ||
                (usagePage == 0xFF00 && h.reports[r].usage_page >= 0xFF00)) {
                *reportId = h.reports[r].report_id;
                return i;
            }
        }
    }
    return -1;
}

template <typename E> void fill_common(E &e, const HidItf &h, const uint8_t *raw, size_t rawLength,
                                       const uint8_t *body, size_t bodyLength)
{
    const CH32RVUsbHostDeviceInfo *d = ch32rv_usb_host_device(h.daddr);
    e.vid = d ? d->vid : 0;
    e.pid = d ? d->pid : 0;
    e.rawData = raw;
    e.rawLength = rawLength;
    e.reportData = body;
    e.reportLength = bodyLength;
    e.address = h.daddr;
    e.interfaceNumber = h.itf;
}

void keyboard_report(HidItf &h, const uint8_t *raw, size_t rawLength, const uint8_t *body, size_t len)
{
    if (len != 8) {
        return;            /* not the boot layout (an NKRO bitmap, say): onHIDInput() has it raw */
    }
    const bool num = h.leds & CH32RV_USB_HOST_KEYBOARD_LED_NUM_LOCK;
    const bool caps = h.leds & CH32RV_USB_HOST_KEYBOARD_LED_CAPS_LOCK;
    const bool scroll = h.leds & CH32RV_USB_HOST_KEYBOARD_LED_SCROLL_LOCK;
    const uint8_t modifiers = body[0];
    const uint8_t *keys = body + 2;
    const uint8_t *last = h.lastKeys + 2;
    if (keyboard_cb) {
        for (int pass = 0; pass < 2; pass++) {
            const uint8_t *now = pass == 0 ? keys : last;
            const uint8_t *before = pass == 0 ? last : keys;
            for (int i = 0; i < 6; i++) {
                const uint8_t key = now[i];
                if (key < 4) {                            /* none, or a rollover error */
                    continue;
                }
                bool there = false;
                for (int j = 0; j < 6; j++) {
                    there = there || before[j] == key;
                }
                if (there) {
                    continue;
                }
                CH32RVUsbHostKeyboardEvent e;
                fill_common(e, h, raw, rawLength, body, len);
                e.pressed = pass == 0;
                e.released = pass == 1;
                e.keycode = key;
                e.modifiers = modifiers;
                e.unicode = keycode_unicode(key, pass == 0 ? modifiers : h.lastKeys[0], caps, num);
                e.ascii = e.unicode <= 0x7F ? (uint8_t)e.unicode : 0;
                e.numLock = num;
                e.capsLock = caps;
                e.scrollLock = scroll;
                keyboard_cb(e);
            }
        }
    }
    memcpy(h.lastKeys, body, 8);
}

void mouse_report(HidItf &h, const uint8_t *raw, size_t rawLength, const uint8_t *body, size_t len)
{
    CH32RVUsbHostMouseEvent e;
    fill_common(e, h, raw, rawLength, body, len);
    CH32RVUsbHostMouseReportValues v;
    if (h.mouse.valid && ch32rvUsbHostDecodeMouseReport(h.mouse, raw, rawLength, v)) {
        e.x = (int16_t)v.x;
        e.y = (int16_t)v.y;
        e.wheel = (int16_t)v.wheel;
        e.pan = (int16_t)v.pan;
        e.buttons = (uint8_t)v.buttons;
    } else if (len >= 3) {                                /* boot mouse */
        e.buttons = body[0];
        e.x = (int8_t)body[1];
        e.y = (int8_t)body[2];
        e.wheel = len > 3 ? (int8_t)body[3] : 0;
        e.pan = len > 4 ? (int8_t)body[4] : 0;
    } else {
        return;
    }
    e.previousButtons = (uint8_t)h.lastMouseButtons;
    e.moved = e.x || e.y || e.wheel || e.pan;
    e.buttonsChanged = e.buttons != e.previousButtons;
    h.lastMouseButtons = e.buttons;
    if (mouse_cb && (e.moved || e.buttonsChanged)) {
        mouse_cb(e);
    }
}

void report(uint8_t daddr, uint8_t idx, const uint8_t *raw, uint16_t rawLength)
{
    HidItf &h = hids[idx];
    if (input_cb) {
        CH32RVUsbHostHIDInput in;
        const CH32RVUsbHostDeviceInfo *d = ch32rv_usb_host_device(daddr);
        in.address = daddr;
        in.interfaceNumber = h.itf;
        in.vid = d ? d->vid : 0;
        in.pid = d ? d->pid : 0;
        in.protocol = h.protocol;
        in.subclass = h.protocol ? 1 : 0;
        in.data = raw;
        in.length = rawLength;
        input_cb(in);
    }
    /* Which report this is: by ID when the descriptor uses IDs, else the one
     * (or the interface's boot protocol). */
    const tuh_hid_report_info_t *info = nullptr;
    const uint8_t *body = raw;
    size_t len = rawLength;
    const bool ids = h.reportCount && h.reports[0].report_id != 0;
    if (ids && rawLength) {
        for (uint8_t r = 0; r < h.reportCount; r++) {
            if (h.reports[r].report_id == raw[0]) {
                info = &h.reports[r];
            }
        }
        body = raw + 1;
        len = rawLength - 1u;
    } else if (h.reportCount) {
        info = &h.reports[0];
    }
    const uint16_t page = info ? info->usage_page : 0;
    const uint8_t usage = info ? info->usage : 0;
    if ((page == HID_USAGE_PAGE_DESKTOP && usage == HID_USAGE_DESKTOP_KEYBOARD) ||
        (!info && h.protocol == HID_ITF_PROTOCOL_KEYBOARD)) {
        keyboard_report(h, raw, rawLength, body, len);
    } else if ((page == HID_USAGE_PAGE_DESKTOP && usage == HID_USAGE_DESKTOP_MOUSE) ||
               (!info && h.protocol == HID_ITF_PROTOCOL_MOUSE)) {
        mouse_report(h, raw, rawLength, body, len);
    } else if (page == HID_USAGE_PAGE_CONSUMER && len >= 2) {
        const uint16_t u = (uint16_t)(body[0] | (body[1] << 8));
        if (u != h.lastConsumer && consumer_cb) {
            CH32RVUsbHostConsumerControlEvent e;
            fill_common(e, h, raw, rawLength, body, len);
            e.usage = u ? u : h.lastConsumer;
            e.pressed = u != 0;
            e.released = u == 0;
            consumer_cb(e);
        }
        h.lastConsumer = u;
    } else if (page == HID_USAGE_PAGE_DESKTOP && usage == HID_USAGE_DESKTOP_SYSTEM_CONTROL && len >= 1) {
        const uint8_t u = body[0];
        if (u != h.lastSystem && system_cb) {
            CH32RVUsbHostSystemControlEvent e;
            fill_common(e, h, raw, rawLength, body, len);
            e.usage = u ? u : h.lastSystem;
            e.pressed = u != 0;
            e.released = u == 0;
            system_cb(e);
        }
        h.lastSystem = u;
    } else if (page == HID_USAGE_PAGE_DESKTOP &&
               (usage == HID_USAGE_DESKTOP_GAMEPAD || usage == HID_USAGE_DESKTOP_JOYSTICK)) {
        const size_t n = TU_MIN(len, sizeof h.lastGamepad);
        const bool changed = n != h.lastGamepadLength || memcmp(body, h.lastGamepad, n) != 0;
        if (gamepad_cb && changed) {
            CH32RVUsbHostGamepadEvent e;
            fill_common(e, h, raw, rawLength, body, len);
            e.changed = true;
            gamepad_cb(e);
        }
        memcpy(h.lastGamepad, body, n);
        h.lastGamepadLength = (uint8_t)n;
    } else if (page >= 0xFF00 && vendor_cb) {
        CH32RVUsbHostHIDVendorInput e;
        fill_common(e, h, raw, rawLength, body, len);
        vendor_cb(e);
    }
}

}  // namespace

/* ------------------------------------------------------------- TinyUSB */
extern "C" void tuh_hid_mount_cb(uint8_t daddr, uint8_t idx, const uint8_t *desc, uint16_t len)
{
    if (idx >= CFG_TUH_HID) {
        return;
    }
    HidItf &h = hids[idx];
    memset(&h, 0, sizeof h);
    tuh_itf_info_t info;
    h.daddr = daddr;
    h.itf = tuh_hid_itf_get_info(daddr, idx, &info) ? info.desc.bInterfaceNumber : idx;
    h.protocol = tuh_hid_interface_protocol(daddr, idx);
    h.reportCount = tuh_hid_parse_report_descriptor(h.reports, CH32RV_USB_HOST_HID_REPORTS, desc, len);
    ch32rvUsbHostParseMouseReportLayout(desc, len, h.mouse);
    h.mounted = true;
    ch32rv_usb_host_mark_claimed(daddr, h.itf);
    if (descriptor_cb) {
        CH32RVUsbHostHIDReportDescriptor d;
        d.address = daddr;
        d.interfaceNumber = h.itf;
        d.data = desc;
        d.length = len;
        descriptor_cb(d);
    }
    tuh_hid_receive_report(daddr, idx);
}

extern "C" void tuh_hid_umount_cb(uint8_t daddr, uint8_t idx)
{
    (void)daddr;
    if (idx < CFG_TUH_HID) {
        hids[idx].mounted = false;
    }
}

/* The report is copied and the endpoint armed again before the sketch's
 * callbacks run: they may take a while (print to a slow console), and the
 * device cannot send its next report until the host asks for it - a mouse
 * press and release in a row timed out on the device that way. */
extern "C" void tuh_hid_report_received_cb(uint8_t daddr, uint8_t idx, const uint8_t *data, uint16_t len)
{
    uint8_t copy[CFG_TUH_HID_EPIN_BUFSIZE];
    const bool use = idx < CFG_TUH_HID && hids[idx].mounted && len;
    if (len > sizeof copy) {
        len = sizeof copy;
    }
    if (use) {
        memcpy(copy, data, len);
    }
    tuh_hid_receive_report(daddr, idx);
    if (use) {
        report(daddr, idx, copy, len);
    }
}

/* ------------------------------------------------------------- the API */
void CH32RVUsbHost::onKeyboard(KeyboardCallback cb) { keyboard_cb = cb; }
void CH32RVUsbHost::onMouse(MouseCallback cb) { mouse_cb = cb; }
void CH32RVUsbHost::onConsumerControl(ConsumerControlCallback cb) { consumer_cb = cb; }
void CH32RVUsbHost::onSystemControl(SystemControlCallback cb) { system_cb = cb; }
void CH32RVUsbHost::onGamepad(GamepadCallback cb) { gamepad_cb = cb; }
void CH32RVUsbHost::onHIDVendorInput(HIDVendorInputCallback cb) { vendor_cb = cb; }
void CH32RVUsbHost::onHIDInput(HIDInputCallback cb) { input_cb = cb; }
void CH32RVUsbHost::onHIDReportDescriptor(HIDReportDescriptorCallback cb) { descriptor_cb = cb; }
void CH32RVUsbHost::setKeyboardLayout(const CH32RVUsbHostKeyboardLayout &l) { layout = &l; }

bool CH32RVUsbHost::hidReady(uint8_t address) const
{
    for (int i = 0; i < CFG_TUH_HID; i++) {
        if (hids[i].mounted && (address == CH32RV_USB_HOST_ANY_ADDRESS || hids[i].daddr == address)) {
            return true;
        }
    }
    return false;
}

bool CH32RVUsbHost::sendSetProtocol(uint8_t interfaceNumber, uint8_t address, bool bootProtocol)
{
    const uint8_t daddr = address == CH32RV_USB_HOST_ANY_ADDRESS ? resolve(address) : address;
    /* SET_PROTOCOL: class, interface; wValue 0 boot, 1 report. */
    return ch32rv_usb_host_control(*this, daddr, 0x21, HID_REQ_CONTROL_SET_PROTOCOL,
                                   bootProtocol ? HID_PROTOCOL_BOOT : HID_PROTOCOL_REPORT, interfaceNumber,
                                   nullptr, 0, nullptr, 1000);
}

bool CH32RVUsbHost::sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId,
                                  const uint8_t *data, size_t length, uint8_t address)
{
    int idx = find_index(address == CH32RV_USB_HOST_ANY_ADDRESS ? 0 : address, interfaceNumber);
    if (address == CH32RV_USB_HOST_ANY_ADDRESS) {
        for (int i = 0; i < CFG_TUH_HID && idx < 0; i++) {
            if (hids[i].mounted && hids[i].itf == interfaceNumber) {
                idx = i;
            }
        }
    }
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    if (length > 63 || (length && !data)) {
        return fail(CH32RVUsbHostError::InvalidArgument);
    }
    /* SET_REPORT over EP0, the report ID in front when there is one. */
    uint8_t buf[64];
    size_t n = 0;
    if (reportId) {
        buf[n++] = reportId;
    }
    memcpy(buf + n, data, length);
    n += length;
    return ch32rv_usb_host_control(*this, hids[idx].daddr, 0x21, HID_REQ_CONTROL_SET_REPORT,
                                   (uint16_t)((reportType << 8) | reportId), hids[idx].itf, buf, (uint16_t)n,
                                   nullptr, 1000);
}

bool CH32RVUsbHost::getHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId, uint8_t *data,
                                 size_t length, size_t *actualLength, uint8_t address)
{
    int idx = -1;
    for (int i = 0; i < CFG_TUH_HID && idx < 0; i++) {
        if (hids[i].mounted && hids[i].itf == interfaceNumber &&
            (address == CH32RV_USB_HOST_ANY_ADDRESS || hids[i].daddr == address)) {
            idx = i;
        }
    }
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return ch32rv_usb_host_control(*this, hids[idx].daddr, 0xA1, HID_REQ_CONTROL_GET_REPORT,
                                   (uint16_t)((reportType << 8) | reportId), hids[idx].itf, data,
                                   (uint16_t)length, actualLength, 1000);
}

bool CH32RVUsbHost::sendHIDVendorOutput(const uint8_t *data, size_t length, uint8_t address)
{
    uint8_t id = CH32RV_USB_HOST_HID_REPORT_ID_VENDOR;
    const int idx = find_by_page(address, 0xFF00, &id);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    /* The interrupt OUT endpoint when there is one, SET_REPORT otherwise (or
     * when it is busy). tuh_hid_send_ready() is also true for an interface
     * without an OUT endpoint, so the send itself decides. */
    if (tuh_hid_send_ready(hids[idx].daddr, (uint8_t)idx) &&
        tuh_hid_send_report(hids[idx].daddr, (uint8_t)idx, id, data, (uint16_t)length)) {
        return ok();
    }
    return sendHIDReport(hids[idx].itf, CH32RV_USB_HOST_HID_REPORT_TYPE_OUTPUT, id, data, length,
                         hids[idx].daddr);
}

bool CH32RVUsbHost::sendHIDVendorFeature(const uint8_t *data, size_t length, uint8_t address)
{
    uint8_t id = CH32RV_USB_HOST_HID_REPORT_ID_VENDOR;
    const int idx = find_by_page(address, 0xFF00, &id);
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return sendHIDReport(hids[idx].itf, CH32RV_USB_HOST_HID_REPORT_TYPE_FEATURE, id, data, length,
                         hids[idx].daddr);
}

bool CH32RVUsbHost::setKeyboardLeds(bool numLock, bool capsLock, bool scrollLock, uint8_t address)
{
    const uint8_t leds = (uint8_t)((numLock ? CH32RV_USB_HOST_KEYBOARD_LED_NUM_LOCK : 0) |
                                   (capsLock ? CH32RV_USB_HOST_KEYBOARD_LED_CAPS_LOCK : 0) |
                                   (scrollLock ? CH32RV_USB_HOST_KEYBOARD_LED_SCROLL_LOCK : 0));
    bool any = false;
    for (int i = 0; i < CFG_TUH_HID; i++) {
        HidItf &h = hids[i];
        if (!h.mounted || (address != CH32RV_USB_HOST_ANY_ADDRESS && h.daddr != address)) {
            continue;
        }
        uint8_t id = 0;
        bool keyboard = !h.reportCount && h.protocol == HID_ITF_PROTOCOL_KEYBOARD;
        for (uint8_t r = 0; r < h.reportCount; r++) {
            if (h.reports[r].usage_page == HID_USAGE_PAGE_DESKTOP &&
                h.reports[r].usage == HID_USAGE_DESKTOP_KEYBOARD) {
                keyboard = true;
                id = h.reports[r].report_id;
            }
        }
        if (!keyboard) {
            continue;
        }
        if (!sendHIDReport(h.itf, CH32RV_USB_HOST_HID_REPORT_TYPE_OUTPUT, id, &leds, 1, h.daddr)) {
            return false;
        }
        h.leds = leds;
        any = true;
    }
    return any ? ok() : fail(CH32RVUsbHostError::NotSupported);
}

static uint8_t keyboard_leds(uint8_t address)
{
    for (int i = 0; i < CFG_TUH_HID; i++) {
        if (hids[i].mounted && (address == CH32RV_USB_HOST_ANY_ADDRESS || hids[i].daddr == address) &&
            hids[i].leds) {
            return hids[i].leds;
        }
    }
    return 0;
}

bool CH32RVUsbHost::getKeyboardNumLock(uint8_t address) const
{
    return keyboard_leds(address) & CH32RV_USB_HOST_KEYBOARD_LED_NUM_LOCK;
}
bool CH32RVUsbHost::getKeyboardCapsLock(uint8_t address) const
{
    return keyboard_leds(address) & CH32RV_USB_HOST_KEYBOARD_LED_CAPS_LOCK;
}
bool CH32RVUsbHost::getKeyboardScrollLock(uint8_t address) const
{
    return keyboard_leds(address) & CH32RV_USB_HOST_KEYBOARD_LED_SCROLL_LOCK;
}

#else /* no host */

void CH32RVUsbHost::onKeyboard(KeyboardCallback) {}
void CH32RVUsbHost::onMouse(MouseCallback) {}
void CH32RVUsbHost::onConsumerControl(ConsumerControlCallback) {}
void CH32RVUsbHost::onSystemControl(SystemControlCallback) {}
void CH32RVUsbHost::onGamepad(GamepadCallback) {}
void CH32RVUsbHost::onHIDVendorInput(HIDVendorInputCallback) {}
void CH32RVUsbHost::onHIDInput(HIDInputCallback) {}
void CH32RVUsbHost::onHIDReportDescriptor(HIDReportDescriptorCallback) {}
void CH32RVUsbHost::setKeyboardLayout(const CH32RVUsbHostKeyboardLayout &) {}
bool CH32RVUsbHost::hidReady(uint8_t) const { return false; }
bool CH32RVUsbHost::sendSetProtocol(uint8_t, uint8_t, bool) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::sendHIDReport(uint8_t, uint8_t, uint8_t, const uint8_t *, size_t, uint8_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::getHIDReport(uint8_t, uint8_t, uint8_t, uint8_t *, size_t, size_t *, uint8_t)
{
    return fail(CH32RVUsbHostError::Unsupported);
}
bool CH32RVUsbHost::sendHIDVendorOutput(const uint8_t *, size_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::sendHIDVendorFeature(const uint8_t *, size_t, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::setKeyboardLeds(bool, bool, bool, uint8_t) { return fail(CH32RVUsbHostError::Unsupported); }
bool CH32RVUsbHost::getKeyboardNumLock(uint8_t) const { return false; }
bool CH32RVUsbHost::getKeyboardCapsLock(uint8_t) const { return false; }
bool CH32RVUsbHost::getKeyboardScrollLock(uint8_t) const { return false; }

#endif

const char *ch32rvUsbHostConsumerControlUsageName(uint16_t usage)
{
    switch (usage) {
    case 0xB5: return "NEXT_TRACK";
    case 0xB6: return "PREVIOUS_TRACK";
    case 0xB7: return "STOP";
    case 0xCD: return "PLAY_PAUSE";
    case 0xE2: return "MUTE";
    case 0xE9: return "VOLUME_UP";
    case 0xEA: return "VOLUME_DOWN";
    default: return "UNKNOWN";
    }
}
