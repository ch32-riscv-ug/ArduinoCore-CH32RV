/* CH32RVUsbHost: HID (keyboards, mice, consumer / system control, gamepads,
 * vendor reports): the interfaces, their report descriptors and interrupt
 * endpoints, and the reports turned into EspUsbHost's events. */
#include "CH32RVUsbHostInternal.h"

#include <string.h>

#include "CH32RVUsbHostHidLayout.h"

#if CH32RV_USB_HOST_STACK

namespace {

#define HID_INTERFACES 4
#define HID_REPORT_MAX 64

#define HID_PAGE_DESKTOP 0x01
#define HID_PAGE_CONSUMER 0x0C
#define HID_USAGE_JOYSTICK 0x04
#define HID_USAGE_GAMEPAD 0x05
#define HID_USAGE_KEYBOARD 0x06
#define HID_USAGE_MOUSE 0x02
#define HID_USAGE_SYSTEM_CONTROL 0x80
#define HID_ITF_PROTOCOL_KEYBOARD 1
#define HID_ITF_PROTOCOL_MOUSE 2
#define HID_REQ_GET_REPORT 0x01
#define HID_REQ_SET_IDLE 0x0A
#define HID_REQ_SET_REPORT 0x09
#define HID_REQ_SET_PROTOCOL 0x0B

/* One top-level application collection of a report descriptor. */
struct ReportInfo {
    uint8_t report_id;
    uint8_t usage;
    uint16_t usage_page;
};

/* One mounted HID interface. */
struct HidItf {
    bool mounted;
    uint8_t daddr, itf, protocol;            /* itf protocol: 1 keyboard, 2 mouse */
    uint8_t subclass;
    uint8_t epIn, epOut;
    uint16_t mpsIn;
    uint16_t descriptorLength;               /* the report descriptor's */
    volatile bool inBusy, outBusy;
    uint8_t held;                            /* a report left in `in`: the queue was full */
    bool descriptorPending;                  /* onHIDReportDescriptor() still due */
    uint8_t reportCount;
    ReportInfo reports[CH32RV_USB_HOST_HID_REPORTS];
    CH32RVUsbHostMouseReportLayout mouse;
    uint8_t lastKeys[8];
    uint16_t lastMouseButtons;
    uint16_t lastConsumer;
    uint8_t lastSystem;
    uint8_t lastGamepad[16];
    uint8_t lastGamepadLength;
    uint8_t leds;                            /* what setKeyboardLeds() last sent */
    uint8_t in[HID_REPORT_MAX] __attribute__((aligned(4)));
    uint8_t out[HID_REPORT_MAX] __attribute__((aligned(4)));
};
HidItf hids[HID_INTERFACES];

/* Reports wait here for deliverEvents(), which turns them into the sketch's
 * callbacks. While the queue is full the interface's endpoint is not armed
 * again: the device keeps its reports until there is room. */
struct Queued {
    uint8_t idx;
    uint8_t length;
    uint8_t data[HID_REPORT_MAX];
};
#define QUEUE 4
Queued queue[QUEUE];
volatile uint8_t q_head, q_tail;

CH32RVUsbHost::KeyboardCallback keyboard_cb;
CH32RVUsbHost::MouseCallback mouse_cb;
CH32RVUsbHost::ConsumerControlCallback consumer_cb;
CH32RVUsbHost::SystemControlCallback system_cb;
CH32RVUsbHost::GamepadCallback gamepad_cb;
CH32RVUsbHost::HIDVendorInputCallback vendor_cb;
CH32RVUsbHost::HIDInputCallback input_cb;
CH32RVUsbHost::HIDReportDescriptorCallback descriptor_cb;
const CH32RVUsbHostKeyboardLayout *layout;

uint16_t hid_open(uint8_t daddr, const uint8_t *itf, uint16_t length);
void hid_configured(uint8_t daddr, uint8_t itf);
size_t hid_deliver(CH32RVUsbHost &host);
void hid_detach(uint8_t daddr);

CH32RVUsbHostGroup hid_group = {hid_open, hid_configured, nullptr, hid_detach, hid_deliver, nullptr};

/* Joins when this file is linked: something in it was called. */
struct Join {
    Join() { ch32rv_usb_host_join(hid_group); }
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
    for (int i = 0; i < HID_INTERFACES; i++) {
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
    for (int i = 0; i < HID_INTERFACES; i++) {
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
    const ReportInfo *info = nullptr;
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
    if ((page == HID_PAGE_DESKTOP && usage == HID_USAGE_KEYBOARD) ||
        (!info && h.protocol == HID_ITF_PROTOCOL_KEYBOARD)) {
        keyboard_report(h, raw, rawLength, body, len);
    } else if ((page == HID_PAGE_DESKTOP && usage == HID_USAGE_MOUSE) ||
               (!info && h.protocol == HID_ITF_PROTOCOL_MOUSE)) {
        mouse_report(h, raw, rawLength, body, len);
    } else if (page == HID_PAGE_CONSUMER && len >= 2) {
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
    } else if (page == HID_PAGE_DESKTOP && usage == HID_USAGE_SYSTEM_CONTROL && len >= 1) {
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
    } else if (page == HID_PAGE_DESKTOP && (usage == HID_USAGE_GAMEPAD || usage == HID_USAGE_JOYSTICK)) {
        const size_t n = ch32rv_min(len, sizeof h.lastGamepad);
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

/* The top-level application collections of a report descriptor and their
 * report IDs - which report is a keyboard, which a mouse. */
uint8_t parse_reports(ReportInfo *out, uint8_t capacity, const uint8_t *d, uint16_t length)
{
    uint8_t count = 0;
    uint8_t depth = 0;
    uint16_t page = 0;
    uint8_t usage = 0;
    for (uint16_t at = 0; at < length;) {
        const uint8_t prefix = d[at];
        if (prefix == 0xFE) {                       /* long item */
            if (at + 2 >= length) {
                break;
            }
            at = (uint16_t)(at + 3 + d[at + 1]);
            continue;
        }
        static const uint8_t sizes[4] = {0, 1, 2, 4};
        const uint8_t size = sizes[prefix & 3];
        if (at + 1u + size > length) {
            break;
        }
        uint32_t value = 0;
        for (uint8_t i = 0; i < size; i++) {
            value |= (uint32_t)d[at + 1 + i] << (8 * i);
        }
        const uint8_t tag = (uint8_t)(prefix & 0xFC);
        switch (tag) {
        case 0x04:                                  /* Usage Page */
            page = (uint16_t)value;
            break;
        case 0x08:                                  /* Usage */
            if (depth == 0) {
                usage = (uint8_t)value;
                if (size == 4) {
                    page = (uint16_t)(value >> 16);
                }
            }
            break;
        case 0xA0:                                  /* Collection */
            if (depth == 0 && count < capacity) {
                out[count].report_id = 0;
                out[count].usage = usage;
                out[count].usage_page = page;
                count++;
            }
            depth++;
            break;
        case 0xC0:                                  /* End Collection */
            if (depth) {
                depth--;
            }
            break;
        case 0x84:                                  /* Report ID */
            if (depth && count) {
                ReportInfo &r = out[count - 1];
                if (r.report_id == 0) {
                    r.report_id = (uint8_t)value;
                } else if (count < capacity) {      /* another report in the same collection */
                    out[count] = r;
                    out[count].report_id = (uint8_t)value;
                    count++;
                }
            }
            break;
        }
        at = (uint16_t)(at + 1 + size);
    }
    return count;
}

void in_done(void *user, uint8_t result, uint16_t length);

void arm_in(HidItf &h)
{
    if (h.mounted && !h.inBusy) {
        const uint16_t n = h.mpsIn < sizeof h.in ? h.mpsIn : sizeof h.in;
        h.inBusy = ch32rv_usb_host_xfer(h.daddr, h.epIn, h.in, n, in_done, &h);
    }
}

void in_done(void *user, uint8_t result, uint16_t length)
{
    HidItf &h = *(HidItf *)user;
    h.inBusy = false;
    if (!h.mounted) {
        return;
    }
    if (result == CH32RV_USBH_OK && length) {
        const uint8_t next = (uint8_t)((q_head + 1) % QUEUE);
        if (next == q_tail) {
            h.held = (uint8_t)(length < sizeof h.in ? length : sizeof h.in);
            return;                                 /* hid_deliver() queues it and re-arms */
        }
        Queued &q = queue[q_head];
        q.idx = (uint8_t)(&h - hids);
        q.length = (uint8_t)(length < sizeof q.data ? length : sizeof q.data);
        memcpy(q.data, h.in, q.length);
        q_head = next;
    }
    arm_in(h);
}

void out_done(void *user, uint8_t result, uint16_t length)
{
    (void)result;
    (void)length;
    ((HidItf *)user)->outBusy = false;
}

/* An interface of class HID: its interrupt endpoints. The report descriptor
 * waits for the configuration. */
uint16_t hid_open(uint8_t daddr, const uint8_t *itf, uint16_t length)
{
    if (itf[5] != 3) {
        return 0;
    }
    HidItf *h = nullptr;
    for (HidItf &x : hids) {
        if (!x.mounted && !x.daddr) {
            h = &x;
            break;
        }
    }
    if (!h) {
        return 0;
    }
    uint8_t epIn = 0, epOut = 0;
    uint16_t mpsIn = 0, descriptorLength = 0;
    const uint8_t *eps[2] = {};
    uint16_t at = itf[0];
    while (at + 2 <= length && itf[at] >= 2 && itf[at + 1] != 4 && itf[at + 1] != 11) {
        const uint8_t *d = itf + at;
        if (d[1] == 0x21 && d[0] >= 9) {
            descriptorLength = (uint16_t)(d[7] | d[8] << 8);
        } else if (d[1] == 5 && d[0] >= 7 && (d[3] & 3) == 3) {
            if ((d[2] & 0x80) && !epIn) {
                epIn = d[2];
                mpsIn = (uint16_t)((d[4] | d[5] << 8) & 0x7FF);
                eps[0] = d;
            } else if (!(d[2] & 0x80) && !epOut) {
                epOut = d[2];
                eps[1] = d;
            }
        }
        at = (uint16_t)(at + d[0]);
    }
    if (!epIn || !ch32rv_usb_host_ep_open(daddr, eps[0])) {
        return 0;
    }
    if (epOut && !ch32rv_usb_host_ep_open(daddr, eps[1])) {
        epOut = 0;
    }
    memset(h, 0, sizeof *h);
    h->daddr = daddr;
    h->itf = itf[2];
    h->subclass = itf[6];
    h->protocol = itf[7];
    h->epIn = epIn;
    h->epOut = epOut;
    h->mpsIn = mpsIn;
    h->descriptorLength = descriptorLength;
    return at;
}

/* Report protocol, as a host OS runs it: the report descriptor then says
 * what arrives, report IDs included (HID 1.11 7.2.6). */
void hid_configured(uint8_t daddr, uint8_t itf)
{
    const int idx = [&] {
        for (int i = 0; i < HID_INTERFACES; i++) {
            if (!hids[i].mounted && hids[i].daddr == daddr && hids[i].itf == itf) {
                return i;
            }
        }
        return -1;
    }();
    if (idx < 0) {
        return;
    }
    HidItf &h = hids[idx];
    ch32rv_usb_host_control_sync(daddr, 0x21, HID_REQ_SET_IDLE, 0, itf, nullptr, 0, nullptr, 1000);   /* may STALL */
    if (h.subclass == 1) {
        ch32rv_usb_host_control_sync(daddr, 0x21, HID_REQ_SET_PROTOCOL, 1, itf, nullptr, 0, nullptr, 1000);
    }
    uint8_t *desc = ch32rv_usb_host_scratch;
    uint16_t len = h.descriptorLength < sizeof ch32rv_usb_host_scratch ? h.descriptorLength
                                                                       : (uint16_t)sizeof ch32rv_usb_host_scratch;
    uint16_t got = 0;
    if (!len || ch32rv_usb_host_control_sync(daddr, 0x81, 6, 0x2200, itf, desc, len, &got, 1000) != CH32RV_USBH_OK) {
        got = 0;
    }
    h.reportCount = parse_reports(h.reports, CH32RV_USB_HOST_HID_REPORTS, desc, got);
    ch32rvUsbHostParseMouseReportLayout(desc, got, h.mouse);
    if (!ch32rv_usb_host_connected(daddr)) {
        return;
    }
    h.mounted = true;
    ch32rv_usb_host_mark_claimed(daddr, h.itf);
    h.descriptorPending = descriptor_cb && got;   /* read again for the sketch */
    arm_in(h);
}

/* The report descriptor is read again here: the copy the stack parsed lives
 * in the shared scratch buffer, which is not kept. */
size_t deliver_descriptor(HidItf &h)
{
    h.descriptorPending = false;
    const uint16_t len = h.descriptorLength < sizeof ch32rv_usb_host_scratch ? h.descriptorLength
                                                                             : (uint16_t)sizeof ch32rv_usb_host_scratch;
    uint16_t got = 0;
    if (!descriptor_cb || !len ||
        ch32rv_usb_host_control_sync(h.daddr, 0x81, 6, 0x2200, h.itf, ch32rv_usb_host_scratch, len, &got, 1000) !=
            CH32RV_USBH_OK ||
        !got || !h.mounted) {
        return 0;
    }
    CH32RVUsbHostHIDReportDescriptor d;
    d.address = h.daddr;
    d.interfaceNumber = h.itf;
    d.data = ch32rv_usb_host_scratch;
    d.length = got;
    descriptor_cb(d);
    return 1;
}

size_t hid_deliver(CH32RVUsbHost &host)
{
    (void)host;
    size_t ran = 0;
    for (HidItf &h : hids) {
        if (h.mounted && h.descriptorPending) {
            ran += deliver_descriptor(h);
        }
    }
    /* Only what is queued now: reports that arrive during the callbacks wait
     * for the next delivery. */
    for (uint8_t n = (uint8_t)((q_head + QUEUE - q_tail) % QUEUE); n && q_tail != q_head; n--) {
        Queued q = queue[q_tail];
        q_tail = (uint8_t)((q_tail + 1) % QUEUE);
        if (q.idx < HID_INTERFACES && hids[q.idx].mounted) {
            report(hids[q.idx].daddr, q.idx, q.data, q.length);
            ran++;
        }
    }
    /* Room again: the held reports join the queue and their endpoints re-arm. */
    for (HidItf &h : hids) {
        const uint8_t next = (uint8_t)((q_head + 1) % QUEUE);
        if (!h.held || next == q_tail) {
            continue;
        }
        if (h.mounted) {
            Queued &q = queue[q_head];
            q.idx = (uint8_t)(&h - hids);
            q.length = h.held;
            memcpy(q.data, h.in, q.length);
            q_head = next;
        }
        h.held = 0;
        arm_in(h);
    }
    return ran;
}

void hid_detach(uint8_t daddr)
{
    for (HidItf &h : hids) {
        if (h.daddr == daddr) {
            memset(&h, 0, sizeof h);
        }
    }
}

}  // namespace

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
    for (int i = 0; i < HID_INTERFACES; i++) {
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
    return ch32rv_usb_host_control(*this, daddr, 0x21, HID_REQ_SET_PROTOCOL, bootProtocol ? 0 : 1, interfaceNumber,
                                   nullptr, 0, nullptr, 1000);
}

bool CH32RVUsbHost::sendHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId,
                                  const uint8_t *data, size_t length, uint8_t address)
{
    int idx = find_index(address == CH32RV_USB_HOST_ANY_ADDRESS ? 0 : address, interfaceNumber);
    if (address == CH32RV_USB_HOST_ANY_ADDRESS) {
        for (int i = 0; i < HID_INTERFACES && idx < 0; i++) {
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
    return ch32rv_usb_host_control(*this, hids[idx].daddr, 0x21, HID_REQ_SET_REPORT,
                                   (uint16_t)((reportType << 8) | reportId), hids[idx].itf, buf, (uint16_t)n,
                                   nullptr, 1000);
}

bool CH32RVUsbHost::getHIDReport(uint8_t interfaceNumber, uint8_t reportType, uint8_t reportId, uint8_t *data,
                                 size_t length, size_t *actualLength, uint8_t address)
{
    int idx = -1;
    for (int i = 0; i < HID_INTERFACES && idx < 0; i++) {
        if (hids[i].mounted && hids[i].itf == interfaceNumber &&
            (address == CH32RV_USB_HOST_ANY_ADDRESS || hids[i].daddr == address)) {
            idx = i;
        }
    }
    if (idx < 0) {
        return fail(CH32RVUsbHostError::NotSupported);
    }
    return ch32rv_usb_host_control(*this, hids[idx].daddr, 0xA1, HID_REQ_GET_REPORT,
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
     * when it is busy). */
    HidItf &h = hids[idx];
    const size_t n = length + (id ? 1u : 0u);
    if (h.epOut && !h.outBusy && n <= sizeof h.out && (length == 0 || data)) {
        size_t k = 0;
        if (id) {
            h.out[k++] = id;
        }
        memcpy(h.out + k, data, length);
        h.outBusy = ch32rv_usb_host_xfer(h.daddr, h.epOut, h.out, (uint16_t)n, out_done, &h);
        if (h.outBusy) {
            return ok();
        }
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
    for (int i = 0; i < HID_INTERFACES; i++) {
        HidItf &h = hids[i];
        if (!h.mounted || (address != CH32RV_USB_HOST_ANY_ADDRESS && h.daddr != address)) {
            continue;
        }
        uint8_t id = 0;
        bool keyboard = !h.reportCount && h.protocol == HID_ITF_PROTOCOL_KEYBOARD;
        for (uint8_t r = 0; r < h.reportCount; r++) {
            if (h.reports[r].usage_page == HID_PAGE_DESKTOP && h.reports[r].usage == HID_USAGE_KEYBOARD) {
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
    for (int i = 0; i < HID_INTERFACES; i++) {
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
