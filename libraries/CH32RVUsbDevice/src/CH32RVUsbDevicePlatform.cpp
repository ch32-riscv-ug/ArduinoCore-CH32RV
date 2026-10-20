/* The BOS descriptor and what it announces: WebUSB (a browser may open the
 * device, and shows the landing page) and the Microsoft OS 2.0 descriptor set
 * (Windows binds WinUSB to the vendor and DFU runtime interfaces with no
 * driver install). Laid out as EspUsbDevice does - same vendor codes, same
 * layout rules, same default GUID - so a host program written for one finds
 * the other. Linked only when a function that needs it is declared. */
#include "CH32RVUsbDeviceInternal.h"

#include <string.h>



#define WEBUSB_VENDOR_CODE   0x01
#define MS_OS_20_VENDOR_CODE 0x02

/* EspUsbDevice's default, so a host lookup written for it finds this too. */
static const char default_guid[] = "{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}";

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    put16(p, (uint16_t)v);
    put16(p + 2, (uint16_t)(v >> 16));
}

/* ASCII to UTF-16LE with a terminating NUL, plus another for REG_MULTI_SZ. */
static void put_utf16(CH32RVUsbDescriptorWriter &w, const char *s, bool multi)
{
    while (*s) {
        w.u16((uint8_t)*s++);
    }
    w.u16(0);
    if (multi) {
        w.u16(0);
    }
}

static bool is_guid(const char *g)
{
    static const uint8_t groups[] = {8, 4, 4, 4, 12};
    if (*g++ != '{') {
        return false;
    }
    for (uint8_t k = 0; k < sizeof groups; k++) {
        for (uint8_t i = 0; i < groups[k]; i++) {
            const char h = *g++;
            if (!((h >= '0' && h <= '9') || ((h | 0x20) >= 'a' && (h | 0x20) <= 'f'))) {
                return false;
            }
        }
        if (*g++ != (k + 1 < sizeof groups ? '-' : '}')) {
            return false;
        }
    }
    return *g == 0;
}

/* What the set is made of, fixed at begin(). */
static struct {
    const CH32RVUsbDevice *device;
    bool subsets, ccgp;
    const char *guid;
    uint16_t revision;             /* 0 while the revision itself is being computed */
} set;

static uint16_t function_part(const CH32RVUsbDeviceFunction &f)
{
    return (uint16_t)((set.subsets ? 8 + 6 : 0) + 20 + (f.winusb() == 2 ? 132 : 0));
}

static uint16_t functions_part(void)
{
    uint16_t n = 0;
    for (uint8_t i = 0; i < set.device->functionCount(); i++) {
        const CH32RVUsbDeviceFunction &f = *set.device->function(i);
        n = (uint16_t)(n + (f.winusb() ? function_part(f) : 0));
    }
    return n;
}

static uint16_t ms_os_20_length(void)
{
    return (uint16_t)(10 + (set.ccgp ? 4 : 0) + (set.subsets ? 8 : 6) + functions_part());
}

/* The Microsoft OS 2.0 descriptor set (MS OS 2.0 descriptors spec 1.0). */
static void ms_os_20_write(CH32RVUsbDescriptorWriter &w)
{
    const uint16_t total = ms_os_20_length(), parts = functions_part();
    const uint8_t r0 = (uint8_t)set.revision, r1 = (uint8_t)(set.revision >> 8);
    /* SET_HEADER, Windows 8.1 and later */
    const uint8_t head[10] = {10, 0, 0, 0, 0x00, 0x00, 0x03, 0x06, (uint8_t)total, (uint8_t)(total >> 8)};
    w.put(head, sizeof head);
    if (set.ccgp) {
        static const uint8_t ccgp[4] = {4, 0, 7, 0};               /* FEATURE_CCGP_DEVICE */
        w.put(ccgp, sizeof ccgp);
    }
    if (!set.subsets) {
        const uint8_t rev[6] = {6, 0, 8, 0, r0, r1};                /* FEATURE_VENDOR_REVISION */
        w.put(rev, sizeof rev);
    } else {
        const uint16_t n = (uint16_t)(8 + parts);
        const uint8_t config[8] = {8, 0, 1, 0, 0, 0, (uint8_t)n, (uint8_t)(n >> 8)};   /* SUBSET_HEADER_CONFIGURATION */
        w.put(config, sizeof config);
    }
    /* The functions in interface order, which is declaration order. */
    for (uint8_t i = 0; i < set.device->functionCount(); i++) {
        const CH32RVUsbDeviceFunction &f = *set.device->function(i);
        if (!f.winusb()) {
            continue;
        }
        if (set.subsets) {
            const uint16_t n = function_part(f);
            const uint8_t sub[14] = {8, 0, 2, 0, f.interfaceNumber(), 0, (uint8_t)n, (uint8_t)(n >> 8),   /* SUBSET_HEADER_FUNCTION */
                                     6, 0, 8, 0, r0, r1};                                               /* its revision */
            w.put(sub, sizeof sub);
        }
        static const uint8_t compat[20] = {20, 0, 3, 0, 'W', 'I', 'N', 'U', 'S', 'B'};
        w.put(compat, sizeof compat);
        if (f.winusb() == 2) {
            static const uint8_t prop[8] = {132, 0, 4, 0, 7, 0, 42, 0};   /* FEATURE_REG_PROPERTY, REG_MULTI_SZ */
            static const uint8_t data_length[2] = {80, 0};
            w.put(prop, sizeof prop);
            put_utf16(w, "DeviceInterfaceGUIDs", false);
            w.put(data_length, sizeof data_length);
            put_utf16(w, set.guid, true);
        }
    }
}

/* CRC-16/CCITT of the set (its revision fields 0), folded to 15 bits plus
 * one: never 0. */
static uint16_t set_revision(void)
{
    uint16_t crc = 0xFFFF;
    CH32RVUsbDescriptorWriter w;
    w.crc = &crc;
    ms_os_20_write(w);
    return (uint16_t)((crc & 0x7FFF) + 1);
}

static bool webusb_on;

static void bos_write(CH32RVUsbDescriptorWriter &w)
{
    static const uint8_t webusb_uuid[16] = {0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47,
                                            0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65};
    static const uint8_t ms_os_20_uuid[16] = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
                                              0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F};
    const CH32RVUsbPlatform &p = ch32rv_usb_platform;
    const bool ss = ch32rv_usb_series_can(CH32RVUsbSpeed::Super) && ch32rv_usb_bos_super;
    const uint16_t total = (uint16_t)(5 + (ss ? sizeof ch32rv_usb_ss_caps : 0) + (webusb_on ? 24 : 0) +
                                      (p.msOs20Length ? 28 : 0));
    const uint8_t head[5] = {5, 15, (uint8_t)total, (uint8_t)(total >> 8),
                             (uint8_t)((ss ? 2 : 0) + (webusb_on ? 1 : 0) + (p.msOs20Length ? 1 : 0))};
    w.put(head, sizeof head);
    if (ss) {
        w.put(ch32rv_usb_ss_caps, sizeof ch32rv_usb_ss_caps);
    }
    if (webusb_on) {               /* WebUSB 1.0, iLandingPage 1 */
        const uint8_t cap[4] = {24, 0x10, 0x05, 0};
        const uint8_t tail[4] = {0x00, 0x01, WEBUSB_VENDOR_CODE, 1};
        w.put(cap, 4);
        w.put(webusb_uuid, sizeof webusb_uuid);
        w.put(tail, sizeof tail);
    }
    if (p.msOs20Length) {          /* Windows 8.1 and later, the set's length, its vendor code */
        const uint8_t cap[4] = {28, 0x10, 0x05, 0};
        const uint8_t tail[8] = {0x00, 0x00, 0x03, 0x06, (uint8_t)p.msOs20Length, (uint8_t)(p.msOs20Length >> 8),
                                 MS_OS_20_VENDOR_CODE, 0};
        w.put(cap, sizeof cap);
        w.put(ms_os_20_uuid, sizeof ms_os_20_uuid);
        w.put(tail, sizeof tail);
    }
}

static bool build(const CH32RVUsbDevice &device)
{
    const CH32RVUsbDeviceConfig &c = device.config();
    CH32RVUsbPlatform &p = ch32rv_usb_platform;
    if (c.deviceInterfaceGuid && !is_guid(c.deviceInterfaceGuid)) {
        return false;
    }
    bool winusb = false;
    for (uint8_t i = 0; i < device.functionCount(); i++) {
        winusb = winusb || device.function(i)->winusb();
    }
    p.bosLength = 0;
    p.msOs20Length = 0;
    p.msOs20Revision = 0;
    p.msOs20Subsets = false;
    if (winusb) {
        set.device = &device;
        set.subsets = c.msOs20Layout == CH32RV_USB_DEVICE_MS_OS_20_SUBSETS;
        if (c.msOs20Layout == CH32RV_USB_DEVICE_MS_OS_20_AUTO) {
            set.subsets = c.msOs20CcgpDevice || device.interfaceCount() > 1;
        }
        set.ccgp = c.msOs20CcgpDevice;
        set.guid = c.deviceInterfaceGuid ? c.deviceInterfaceGuid : default_guid;
        set.revision = 0;
        const uint16_t n = ms_os_20_length();
        set.revision = c.msOs20VendorRevision ? c.msOs20VendorRevision : set_revision();
        p.msOs20Length = n;
        p.msOs20Revision = set.revision;
        p.msOs20Subsets = set.subsets;
    }
    webusb_on = c.webusbEnabled;
    if (webusb_on || p.msOs20Length) {
        p.bosLength = (uint16_t)(5 + (webusb_on ? 24 : 0) + (p.msOs20Length ? 28 : 0) +
                                 (ch32rv_usb_series_can(CH32RVUsbSpeed::Super) && ch32rv_usb_bos_super
                                      ? sizeof ch32rv_usb_ss_caps : 0));
    }
    return true;
}

/* Vendor requests to the device: the WebUSB landing page, the MS OS 2.0
 * set, and the rest to CH32RVUsbDeviceVendor::onControlRequest. A request
 * nobody answers is stalled. Every device has one stack; the active device's
 * config is what applies, and that is the one begin() built from. */
static bool platform_request(CH32RVUsbControlStage stage, const CH32RVUsbSetup &s)
{
    const CH32RVUsbPlatform &p = ch32rv_usb_platform;
    if (s.bRequest == WEBUSB_VENDOR_CODE && p.bosLength && USBDevice.config().webusbEnabled) {
        if (stage != CH32RVUsbControlStage::Setup) {
            return true;
        }
        static const char *url;
        static uint8_t url_head[3];
        url = USBDevice.config().webusbUrl;
        if (!url || s.wIndex != 2) {
            return false;
        }
        url_head[2] = 1;           /* https:// */
        if (strncmp(url, "https://", 8) == 0) {
            url += 8;
        } else if (strncmp(url, "http://", 7) == 0) {
            url_head[2] = 0;
            url += 7;
        }
        const size_t n = strlen(url);
        url_head[0] = (uint8_t)(n > 252 ? 255 : n + 3);
        url_head[1] = 3;           /* WEBUSB URL */
        return ch32rv_usb_control_in_written(USBDevice, url_head[0], [](CH32RVUsbDescriptorWriter &w) {
            w.put(url_head, 3);
            w.put((const uint8_t *)url, (uint16_t)(url_head[0] - 3));
        });
    }
    if (s.bRequest == MS_OS_20_VENDOR_CODE && s.wIndex == 7 && p.msOs20Length) {
        if (stage != CH32RVUsbControlStage::Setup) {
            return true;
        }
        return ch32rv_usb_control_in_written(USBDevice, p.msOs20Length, ms_os_20_write);
    }
    return p.vendorControl ? p.vendorControl(stage, s) : false;
}

void ch32rv_usb_platform_use(void)
{
    ch32rv_usb_platform.build = build;
    ch32rv_usb_platform.bosWrite = bos_write;
    ch32rv_usb_platform.msOs20Write = ms_os_20_write;
    ch32rv_usb_platform.request = platform_request;
}
