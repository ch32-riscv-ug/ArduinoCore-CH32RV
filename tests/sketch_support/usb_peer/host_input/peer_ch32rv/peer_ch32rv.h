/* The first HID interface of peer_ch32rv.ino, apart so that it comes before the prototypes the Arduino build puts
 * at the top of the sketch.
 *
 * peer_pico's host_input has two HID interfaces with report IDs of its own (the tests read IDs 1-3): HID 0 here is one
 * CH32RVUsbDeviceHidCustom with peer_pico's whole report descriptor, HID 1 the library's keyboard on an interface of
 * its own (enableOwnInterface(): a boot keyboard without a report ID). */
#pragma once
#include <CH32RVUsbDevice.h>
#include <string.h>

enum { RID_GAMEPAD = 1, RID_SYSTEM, RID_VENDOR, RID_NKRO };

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
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, RID_NKRO,             /* Desktop, Keyboard, Application: NKRO */
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,     /* the modifiers E0 .. E7, */
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x19, 0x00, 0x29, 0x87, 0x95, 0x88, 0x81, 0x02,                 /* a bitmap of usages 0 .. 135 */
    0xC0,
};

/* HID 0: the custom descriptor, with an interrupt OUT, the vendor output and feature reports kept and the feature
 * report GET_REPORT answers. */
class PeerHid0 : public CH32RVUsbDeviceHidCustom {
public:
    PeerHid0() : CH32RVUsbDeviceHidCustom(USBDevice, descriptor0, sizeof descriptor0, 24) {}
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
