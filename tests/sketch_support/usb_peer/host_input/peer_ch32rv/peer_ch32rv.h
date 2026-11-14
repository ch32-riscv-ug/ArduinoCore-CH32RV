/* The two HID interfaces of peer_ch32rv.ino, apart so that they come before the prototypes the Arduino build puts
 * at the top of the sketch.
 *
 * CH32RVUsbDevice gives a device one HID interface, its classes merged under the library's report IDs. peer_pico's
 * host_input has two, with report IDs of its own (the tests read IDs 1-3): HID 0 is one CH32RVUsbDeviceHidCustom
 * with peer_pico's whole report descriptor, HID 1 a boot keyboard written here as a function of this sketch's own
 * (CH32RVUsbDeviceFunction, as the library's classes are). */
#pragma once
#include <CH32RVUsbDevice.h>
#include <string.h>

enum { RID_GAMEPAD = 1, RID_SYSTEM, RID_VENDOR };

/* TinyUSB's TUD_HID_REPORT_DESC_GAMEPAD (ID 1) and TUD_HID_REPORT_DESC_SYSTEM_CONTROL (ID 2), and a vendor
 * collection (ID 3) with 8-byte input, output and feature reports: peer_pico's descriptor0, byte for byte. */
static const uint8_t descriptor0[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, RID_GAMEPAD,          /* Desktop, Gamepad, Application */
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35, 0x09, 0x33, 0x09, 0x34,   /* X Y Z Rz Rx Ry */
    0x15, 0x81, 0x25, 0x7F, 0x95, 0x06, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x35, 0x00, 0x46, 0x3B, 0x01,        /* hat */
    0x95, 0x01, 0x75, 0x08, 0x81, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x20, 0x15, 0x00, 0x25, 0x01, 0x95, 0x20, 0x75, 0x01, 0x81, 0x02,  /* 32 buttons */
    0xC0,
    0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, RID_SYSTEM,           /* Desktop, System Control, Application */
    0x15, 0x01, 0x25, 0x03, 0x95, 0x01, 0x75, 0x02,
    0x09, 0x81, 0x09, 0x82, 0x09, 0x83, 0x81, 0x00,                 /* power down, sleep, wake up: an array */
    0x95, 0x01, 0x75, 0x06, 0x81, 0x01,
    0xC0,
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, RID_VENDOR,     /* Vendor, Application */
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x08,
    0x09, 0x02, 0x81, 0x02, 0x09, 0x03, 0x91, 0x02, 0x09, 0x04, 0xB1, 0x02,
    0xC0,
};

/* TinyUSB's TUD_HID_REPORT_DESC_KEYBOARD() without a report ID: peer_pico's descriptor1. */
static const uint8_t descriptor1[] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02,  /* modifiers */
    0x95, 0x01, 0x75, 0x08, 0x81, 0x01,                                                             /* reserved */
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02,                         /* 5 LEDs */
    0x95, 0x01, 0x75, 0x03, 0x91, 0x01,
    0x05, 0x07, 0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x95, 0x06, 0x75, 0x08, 0x81, 0x00,
    0xC0,
};

/* HID 0: the custom descriptor, with an interrupt OUT, the vendor output and feature reports kept and the feature
 * report GET_REPORT answers. */
class PeerHid0 : public CH32RVUsbDeviceHidCustom {
public:
    PeerHid0() : CH32RVUsbDeviceHidCustom(USBDevice, descriptor0, sizeof descriptor0, 16) {}
    bool hidOutEndpoint() const override { return true; }
    void onHidSetReport(uint8_t reportId, uint8_t reportType, const uint8_t *data, uint16_t length) override
    {
        if (reportId == 0 && length) {      /* through the OUT endpoint: the ID is the first byte */
            reportId = data[0];
            data++;
            length--;
        }
        if (reportId != RID_VENDOR) return;
        uint8_t *to = reportType == 3 ? lastFeature : lastOutput;
        memset(to, 0, 8);
        memcpy(to, data, length > 8 ? 8 : length);
    }
    uint16_t hidGetReport(uint8_t reportType, uint8_t *out, uint16_t capacity) const override
    {
        if (reportType != 3 || capacity < sizeof feature) return 0;   /* the feature report: only ID 3 has one */
        memcpy(out, feature, sizeof feature);
        return sizeof feature;
    }
    uint8_t feature[8] = {'F', 'E', 'A', 'T', 'U', 'R', 'E', '0'};
    uint8_t lastOutput[8] = {}, lastFeature[8] = {};
};

/* HID 1: a boot keyboard (subclass 1, protocol 1) with an interrupt IN, LEDs by SET_REPORT, SET_PROTOCOL and
 * SET_IDLE. */
class PeerBootKeyboard;
static bool bootKeyboardDescriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf,
                                   uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed);
static const CH32RVUsbFunctionType bootKeyboardType = {bootKeyboardDescriptor, 1, 1, nullptr, 0};

class PeerBootKeyboard : public CH32RVUsbDeviceFunction {
public:
    PeerBootKeyboard() : CH32RVUsbDeviceFunction(USBDevice, bootKeyboardType, "Boot keyboard") {}

    /* An input report once the host has taken the one before (up to `ms`). */
    bool send(const uint8_t report[8], uint32_t ms)
    {
        const uint32_t t0 = millis();
        while (!device().ready() || device().transferBusy(epIn)) {
            if (millis() - t0 > ms) return false;
            device().task();
        }
        memcpy(in, report, 8);
        return device().transfer(epIn, in, 8);
    }

    uint8_t epIn = 0;
    volatile uint8_t protocol = 1, leds = 0;

protected:
    void usbReset() override { protocol = 1; }
    void usbConfigured(CH32RVUsbSpeed) override { protocol = 1; }
    bool usbControl(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s) override
    {
        if ((s.bmRequestType & 0x60) == 0) {   /* GET_DESCRIPTOR to the interface */
            if (stage != CH32RVUsbControlStage::Setup) return true;
            if ((s.wValue >> 8) == 0x22) return device().controlIn(descriptor1, sizeof descriptor1);
            if ((s.wValue >> 8) == 0x21) return device().controlIn(hidDescriptor, sizeof hidDescriptor);
            return false;
        }
        if ((s.bmRequestType & 0x60) != 0x20) return false;
        switch (s.bRequest) {
        case 0x09:                             /* SET_REPORT: the LEDs */
            if (stage == CH32RVUsbControlStage::Setup) return s.wLength <= sizeof ctrl && device().controlOut(ctrl, s.wLength);
            if (stage == CH32RVUsbControlStage::Data && (s.wValue >> 8) == 2 && s.wLength) leds = ctrl[0];
            return true;
        case 0x01:                             /* GET_REPORT: no key held */
            memset(ctrl, 0, sizeof ctrl);
            return stage != CH32RVUsbControlStage::Setup || device().controlIn(ctrl, 8);
        case 0x0A:                             /* SET_IDLE */
            if (stage == CH32RVUsbControlStage::Setup) idle = (uint8_t)(s.wValue >> 8);
            return stage != CH32RVUsbControlStage::Setup || device().controlStatus();
        case 0x02:                             /* GET_IDLE */
            return stage != CH32RVUsbControlStage::Setup || device().controlIn((const void *)&idle, 1);
        case 0x0B:                             /* SET_PROTOCOL */
            if (stage == CH32RVUsbControlStage::Setup) protocol = (uint8_t)s.wValue;
            return stage != CH32RVUsbControlStage::Setup || device().controlStatus();
        case 0x03:                             /* GET_PROTOCOL */
            ctrl[0] = protocol;
            return stage != CH32RVUsbControlStage::Setup || device().controlIn(ctrl, 1);
        }
        return false;
    }

private:
    uint8_t hidDescriptor[9] = {9, 0x21, 0x11, 0x01, 0, 1, 0x22, (uint8_t)sizeof descriptor1, 0};
    uint8_t in[8] = {}, ctrl[8] = {};
    volatile uint8_t idle = 0;
};

static bool bootKeyboardDescriptor(const CH32RVUsbDeviceFunction &self, CH32RVUsbDescriptorWriter &w, uint8_t itf,
                                   uint8_t *ep, uint8_t str, CH32RVUsbSpeed speed)
{
    PeerBootKeyboard &k = const_cast<PeerBootKeyboard &>(static_cast<const PeerBootKeyboard &>(self));
    const uint8_t n = (*ep)++;
    k.epIn = (uint8_t)(0x80 | n);
    const uint8_t d[] = {
        9, 4, itf, 0, 1, 3, 1, 1, str,                                           /* interface: HID, boot keyboard */
        9, 0x21, 0x11, 0x01, 0, 1, 0x22, (uint8_t)sizeof descriptor1, 0,          /* HID 1.11 */
        7, 5, k.epIn, 3, 8, 0, (uint8_t)(speed == CH32RVUsbSpeed::Full ? 10 : 7), /* interrupt IN, about 10 ms */
    };
    w.put(d, sizeof d);
    return true;
}
