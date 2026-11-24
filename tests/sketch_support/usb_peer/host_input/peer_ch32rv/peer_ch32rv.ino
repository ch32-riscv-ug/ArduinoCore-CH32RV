/* Another CH32RV board as the USB device for tests/peer/usb/host/input: two HID interfaces and two CDC ports. The
 * DUT (CH32RVUsbHost) is the host; this board runs CH32RVUsbDevice on the controller the bench record names
 * (build_opt.h: PEER_USB_CONTROLLER, written by run_hardware.py).
 *   HID 0  gamepad (report ID 1), system control (ID 2) and a vendor collection (ID 3) with 8-byte input, output
 *          and feature reports (peer_ch32rv.h: one custom report descriptor)
 *   HID 1  a keyboard with the boot protocol (subclass 1), no report ID (the library's, on an interface of its own)
 *   CDC 0  echoes every byte;  CDC 1  echoes every byte plus one
 *
 * Commands, one per line on the debug probe's console (SerialDMSeq); answers start "CD " (peer_pico's "PD "):
 *   ?                        CD READY <configured 0/1>
 *   gamepad <x> <y> <buttons>  CD GAMEPAD <ok>      (then the same report with all zero)
 *   system <value>           CD SYSTEM <ok>        (1 power down, 2 sleep, 3 wake up; then 0)
 *   vin <hex>                CD VIN <ok>           (vendor input report, ID 3)
 *   key <keycode>            CD KEY <ok>           (on the keyboard: pressed, then released)
 *   nkro <mods> <key> ...    CD NKRO <ok>          (HID 0's NKRO report 4 with those keys down, then all up)
 *   feature <hex>            CD FEATURE            (what GET_REPORT(feature, 3) answers from now on)
 *   last                     CD LAST <output hex> <feature hex>  (the last vendor output / feature set)
 *   proto                    CD PROTO <keyboard protocol: 0 boot, 1 report>
 *   leds                     CD LEDS <keyboard LED bits>
 *   line <port>              CD LINE <port> <baud> <data bits> <parity> <stop bits> <dtr> <rts>
 *   send <port> <hex>        CD SENT <ok>          (bytes out of that CDC port)
 *   desclen                  CD DESCLEN <HID 0 report descriptor bytes> <HID 1 report descriptor bytes>
 *   replug <ms>              CD REPLUG             (end(), wait, begin()) */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "peer_ch32rv.h"

#ifndef PEER_USB_CONTROLLER
#define PEER_USB_CONTROLLER CH32RVUsbController::Auto
#endif

/* In this order: the host numbers the CDC ports by their interfaces (SerialUSB is not used - its place among the
 * functions would follow the order the global constructors happen to run in). */
PeerHid0 hid0;
CH32RVUsbDeviceHidKeyboard keyboard;
CH32RVUsbDeviceCdcSerial port0(USBDevice, "Port A");
CH32RVUsbDeviceCdcSerial port1(USBDevice, "Port B");
static CH32RVUsbDeviceConfig config;

/* Reports wait this long for the host to take the one before (peer_pico's host_hid: a slow console). */
static const uint32_t REPORT_MS = 2000;

static void say(const char *format, ...)
{
    char text[160];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    SerialDMSeq.print(text);
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t parseHex(const char *s, uint8_t *out, size_t cap)
{
    size_t n = 0;
    while (s && s[0] && s[1] && n < cap && hexval(s[0]) >= 0 && hexval(s[1]) >= 0) {
        out[n++] = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1]));
        s += 2;
    }
    return n;
}

static void printHex(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) say("%02x", d[i]);
}

static bool gamepad(int x, int y, uint32_t buttons)
{
    /* x y z rz rx ry, hat, 32 buttons (TinyUSB's hid_gamepad_report_t) */
    uint8_t r[11] = {(uint8_t)x, (uint8_t)y, 0, 0, 0, 0, 0, (uint8_t)buttons, (uint8_t)(buttons >> 8),
                     (uint8_t)(buttons >> 16), (uint8_t)(buttons >> 24)};
    const uint8_t zero[11] = {};
    return hid0.sendReport(r, sizeof r, RID_GAMEPAD, REPORT_MS) && hid0.sendReport(zero, sizeof zero, RID_GAMEPAD, REPORT_MS);
}

static bool key(uint8_t keycode)
{
    CH32RVUsbDeviceBootKeyboardReport r;
    r.keys[0] = keycode;
    return keyboard.sendReport(r, REPORT_MS) && keyboard.sendReport(CH32RVUsbDeviceBootKeyboardReport(), REPORT_MS);
}

static bool nkro(uint8_t mods, const uint8_t *keys, size_t n)
{
    uint8_t r[18] = {mods};
    for (size_t i = 0; i < n; i++)
        if (keys[i] < 136) r[1 + keys[i] / 8] |= (uint8_t)(1u << (keys[i] % 8));
    const uint8_t none[18] = {};
    return hid0.sendReport(r, sizeof r, RID_NKRO, REPORT_MS) && hid0.sendReport(none, sizeof none, RID_NKRO, REPORT_MS);
}

static CH32RVUsbDeviceCdcSerial &port(uint8_t n) { return n ? port1 : port0; }

static char line[200];
static size_t used;

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "?")) say("CD READY %u\n", USBDevice.ready() ? 1 : 0);
    else if (!strcmp(cmd, "gamepad")) {
        int x = 0, y = 0;
        unsigned long b = 0;
        sscanf(arg, "%d %d %lu", &x, &y, &b);
        say("CD GAMEPAD %u\n", gamepad(x, y, (uint32_t)b) ? 1 : 0);
    } else if (!strcmp(cmd, "system")) {
        const uint8_t v = (uint8_t)strtoul(arg, nullptr, 0), zero = 0;
        const bool ok = hid0.sendReport(&v, 1, RID_SYSTEM, REPORT_MS) && hid0.sendReport(&zero, 1, RID_SYSTEM, REPORT_MS);
        say("CD SYSTEM %u\n", ok ? 1 : 0);
    } else if (!strcmp(cmd, "vin")) {
        uint8_t d[8] = {};
        parseHex(arg, d, sizeof d);
        say("CD VIN %u\n", hid0.sendReport(d, sizeof d, RID_VENDOR, REPORT_MS) ? 1 : 0);
    } else if (!strcmp(cmd, "key")) say("CD KEY %u\n", key((uint8_t)strtoul(arg, nullptr, 0)) ? 1 : 0);
    else if (!strcmp(cmd, "nkro")) {
        char *p = arg;
        const uint8_t mods = (uint8_t)strtoul(p, &p, 0);
        uint8_t keys[16];
        size_t n = 0;
        while (*p && n < sizeof keys) {
            char *q;
            const unsigned long k = strtoul(p, &q, 0);
            if (q == p) break;
            keys[n++] = (uint8_t)k;
            p = q;
        }
        say("CD NKRO %u\n", nkro(mods, keys, n) ? 1 : 0);
    }
    else if (!strcmp(cmd, "feature")) {
        memset(hid0.feature, 0, sizeof hid0.feature);
        parseHex(arg, hid0.feature, sizeof hid0.feature);
        say("CD FEATURE\n");
    } else if (!strcmp(cmd, "last")) {
        say("CD LAST ");
        printHex(hid0.lastOutput, 8);
        say(" ");
        printHex(hid0.lastFeature, 8);
        say("\n");
    } else if (!strcmp(cmd, "proto")) say("CD PROTO %u\n", keyboard.protocol());
    else if (!strcmp(cmd, "leds")) say("CD LEDS %u\n", keyboard.ledState().leds);
    else if (!strcmp(cmd, "line")) {
        const uint8_t n = (uint8_t)strtoul(arg, nullptr, 10) & 1;
        const CH32RVUsbDeviceCdcLineCoding c = port(n).lineCoding();
        const CH32RVUsbDeviceCdcLineState s = port(n).lineState();
        say("CD LINE %u %lu %u %u %u %u %u\n", n, (unsigned long)c.baud, c.dataBits, c.parity, c.stopBits, s.dtr ? 1 : 0,
            s.rts ? 1 : 0);
    } else if (!strcmp(cmd, "send")) {
        char *rest = nullptr;
        const uint8_t n = (uint8_t)strtoul(arg, &rest, 10) & 1;
        uint8_t d[64];
        const size_t k = parseHex(rest && *rest ? rest + 1 : rest, d, sizeof d);
        const bool ok = port(n).write(d, k) == k;
        port(n).flush();
        say("CD SENT %u\n", ok ? 1 : 0);
    } else if (!strcmp(cmd, "desclen"))
        say("CD DESCLEN %u %u\n", (unsigned)sizeof descriptor0, (unsigned)keyboard.hidReportDescriptorLength());
    else if (!strcmp(cmd, "replug")) {
        const uint32_t ms = strtoul(arg, nullptr, 10);
        USBDevice.end();
        delay(ms ? ms : 300);
        USBDevice.begin(config);
        say("CD REPLUG\n");
    } else say("CD UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    config.manufacturer = "ch32rv-tests";
    config.product = "CH32RV peer input";
    config.serialNumber = "CH32RVPEERINPUT";
    config.dfuRuntime = false;
    config.controller = PEER_USB_CONTROLLER;
    keyboard.enableOwnInterface();
    const bool began = USBDevice.begin(config);
    say("CD BOOTED %u %s\n", began ? 1 : 0, USBDevice.lastErrorName());
}

void loop()
{
    deliverEvents();
    for (uint8_t n = 0; n < 2; n++) {
        uint8_t buf[64];
        const size_t k = port(n).available() ? port(n).read(buf, sizeof buf) : 0;
        if (!k) continue;
        if (n == 1)
            for (size_t i = 0; i < k; i++) buf[i]++;
        port(n).write(buf, k);
        port(n).flush();
    }
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\r') continue;
        if (c == '\n') {
            line[used] = '\0';
            if (used) run(line);
            used = 0;
        } else if (used + 1 < sizeof line) {
            line[used++] = (char)c;
        }
    }
}
