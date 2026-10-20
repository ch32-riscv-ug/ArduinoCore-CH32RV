/* The BOS descriptor and what it announces: WebUSB (a browser may open the
 * device, and shows the landing page) and the Microsoft OS 2.0 descriptor set
 * (Windows binds WinUSB to the vendor and DFU runtime interfaces with no
 * driver install). Laid out as EspUsbDevice does - same vendor codes, same
 * layout rules, same default GUID - so a host program written for one finds
 * the other. Linked only when a function that needs it is declared. */
#include "CH32RVUsbDeviceInternal.h"

#include <ctype.h>
#include <string.h>



#define WEBUSB_VENDOR_CODE   0x01
#define MS_OS_20_VENDOR_CODE 0x02

/* EspUsbDevice's default, so a host lookup written for it finds this too. */
static const char default_guid[] = "{975F44D9-0D08-43FD-8B3E-127CA8AFFF9D}";

/* Header 10 + CCGP 4 + configuration subset 8 + per function (subset 8,
 * revision 6, compatible ID 20, GUID property 132): two functions fit. */
static uint8_t ms_os_20[10 + 4 + 8 + 2 * (8 + 6 + 20 + 132)];
/* Header 5 + WebUSB 24 + MS OS 2.0 28. */
static uint8_t bos[5 + 24 + 28];
static uint8_t url_desc[3 + 96];

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
static uint16_t put_utf16(uint8_t *p, const char *s, bool multi)
{
    uint16_t n = 0;
    while (*s) {
        put16(p + n, (uint8_t)*s++);
        n += 2;
    }
    put16(p + n, 0);
    n += 2;
    if (multi) {
        put16(p + n, 0);
        n += 2;
    }
    return n;
}

static bool is_guid(const char *g)
{
    static const uint8_t groups[] = {8, 4, 4, 4, 12};
    if (*g++ != '{') {
        return false;
    }
    for (uint8_t k = 0; k < sizeof groups; k++) {
        for (uint8_t i = 0; i < groups[k]; i++) {
            if (!isxdigit((unsigned char)*g++)) {
                return false;
            }
        }
        if (*g++ != (k + 1 < sizeof groups ? '-' : '}')) {
            return false;
        }
    }
    return *g == 0;
}

/* CRC-16/CCITT of the set, folded to 15 bits plus one: never 0. */
static uint16_t set_revision(const uint8_t *p, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (uint8_t b = 0; b < 8; b++) {
            crc = (uint16_t)(crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return (uint16_t)((crc & 0x7FFF) + 1);
}

static uint16_t build_ms_os_20(const CH32RVUsbDevice &device, const CH32RVUsbDeviceConfig &c, bool subsets)
{
    uint8_t *s = ms_os_20;
    uint16_t o = 0, revisions[3];
    uint8_t nrev = 0;
    put16(s, 10);
    put16(s + 2, 0);              /* MS_OS_20_SET_HEADER_DESCRIPTOR */
    put32(s + 4, 0x06030000);     /* Windows 8.1 and later */
    o = 10;
    if (c.msOs20CcgpDevice) {
        put16(s + o, 4);
        put16(s + o + 2, 7);      /* MS_OS_20_FEATURE_CCGP_DEVICE */
        o += 4;
    }
    if (!subsets) {
        put16(s + o, 6);
        put16(s + o + 2, 8);      /* MS_OS_20_FEATURE_VENDOR_REVISION */
        revisions[nrev++] = (uint16_t)(o + 4);
        o += 6;
    }
    uint16_t config_at = o;
    if (subsets) {
        put16(s + o, 8);
        put16(s + o + 2, 1);      /* MS_OS_20_SUBSET_HEADER_CONFIGURATION */
        s[o + 4] = 0;
        s[o + 5] = 0;
        o += 8;
    }
    /* The functions in interface order, which is declaration order. */
    for (uint8_t i = 0; i < device.functionCount(); i++) {
        const CH32RVUsbDeviceFunction &f = *device.function(i);
        if (!f.winusb()) {
            continue;
        }
        if (o + 8 + 6 + 20 + 132 > sizeof ms_os_20) {
            return 0;
        }
        const uint16_t function_at = o;
        if (subsets) {
            put16(s + o, 8);
            put16(s + o + 2, 2);  /* MS_OS_20_SUBSET_HEADER_FUNCTION */
            s[o + 4] = f.interfaceNumber();
            s[o + 5] = 0;
            o += 8;
            put16(s + o, 6);
            put16(s + o + 2, 8);
            if (nrev < 3) {
                revisions[nrev++] = (uint16_t)(o + 4);
            }
            o += 6;
        }
        static const uint8_t compat[20] = {20, 0, 3, 0, 'W', 'I', 'N', 'U', 'S', 'B'};
        memcpy(s + o, compat, sizeof compat);
        o += sizeof compat;
        if (f.winusb() == 2) {
            put16(s + o, 132);
            put16(s + o + 2, 4);  /* MS_OS_20_FEATURE_REG_PROPERTY */
            put16(s + o + 4, 7);  /* REG_MULTI_SZ */
            put16(s + o + 6, 42);
            o += 8;
            o += put_utf16(s + o, "DeviceInterfaceGUIDs", false);
            put16(s + o, 80);
            o += 2;
            o += put_utf16(s + o, c.deviceInterfaceGuid ? c.deviceInterfaceGuid : default_guid, true);
        }
        if (subsets) {
            put16(s + function_at + 6, (uint16_t)(o - function_at));
        }
    }
    put16(s + 8, o);
    if (subsets) {
        put16(s + config_at + 6, (uint16_t)(o - config_at));
    }
    const uint16_t rev = c.msOs20VendorRevision ? c.msOs20VendorRevision : set_revision(s, o);
    for (uint8_t i = 0; i < nrev; i++) {
        put16(s + revisions[i], rev);
    }
    ch32rv_usb_platform.msOs20Revision = rev;
    return o;
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
    p.bos = NULL;
    p.bosLength = 0;
    p.msOs20 = NULL;
    p.msOs20Length = 0;
    p.msOs20Revision = 0;
    p.msOs20Subsets = false;
    if (winusb) {
        bool subsets = c.msOs20Layout == CH32RV_USB_DEVICE_MS_OS_20_SUBSETS;
        if (c.msOs20Layout == CH32RV_USB_DEVICE_MS_OS_20_AUTO) {
            subsets = c.msOs20CcgpDevice || device.interfaceCount() > 1;
        }
        const uint16_t n = build_ms_os_20(device, c, subsets);
        if (n) {
            p.msOs20 = ms_os_20;
            p.msOs20Length = n;
            p.msOs20Subsets = subsets;
        }
    }
    static const uint8_t webusb_uuid[16] = {0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47,
                                            0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65};
    static const uint8_t ms_os_20_uuid[16] = {0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
                                              0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F};
    uint16_t o = 5;
    uint8_t caps = 0;
    if (c.webusbEnabled) {
        bos[o] = 24;
        bos[o + 1] = 0x10;
        bos[o + 2] = 0x05;
        bos[o + 3] = 0;
        memcpy(bos + o + 4, webusb_uuid, 16);
        put16(bos + o + 20, 0x0100);
        bos[o + 22] = WEBUSB_VENDOR_CODE;
        bos[o + 23] = 1;          /* iLandingPage */
        o += 24;
        caps++;
    }
    if (p.msOs20Length) {
        bos[o] = 28;
        bos[o + 1] = 0x10;
        bos[o + 2] = 0x05;
        bos[o + 3] = 0;
        memcpy(bos + o + 4, ms_os_20_uuid, 16);
        put32(bos + o + 20, 0x06030000);
        put16(bos + o + 24, p.msOs20Length);
        bos[o + 26] = MS_OS_20_VENDOR_CODE;
        bos[o + 27] = 0;
        o += 28;
        caps++;
    }
    if (caps) {
        bos[0] = 5;
        bos[1] = 15;
        put16(bos + 2, o);
        bos[4] = caps;
        p.bos = bos;
        p.bosLength = o;
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
        const char *url = USBDevice.config().webusbUrl;
        if (!url || s.wIndex != 2) {
            return false;
        }
        uint8_t scheme = 1;
        if (strncmp(url, "https://", 8) == 0) {
            url += 8;
        } else if (strncmp(url, "http://", 7) == 0) {
            scheme = 0;
            url += 7;
        }
        size_t n = strlen(url);
        if (n > sizeof url_desc - 3) {
            n = sizeof url_desc - 3;
        }
        url_desc[0] = (uint8_t)(n + 3);
        url_desc[1] = 3;          /* WEBUSB URL */
        url_desc[2] = scheme;
        memcpy(url_desc + 3, url, n);
        return USBDevice.controlIn(url_desc, url_desc[0]);
    }
    if (s.bRequest == MS_OS_20_VENDOR_CODE && s.wIndex == 7 && p.msOs20Length) {
        if (stage != CH32RVUsbControlStage::Setup) {
            return true;
        }
        return USBDevice.controlIn(p.msOs20, p.msOs20Length);
    }
    return p.vendorControl ? p.vendorControl(stage, s) : false;
}

void ch32rv_usb_platform_use(void)
{
    ch32rv_usb_platform.build = build;
    ch32rv_usb_platform.request = platform_request;
}
