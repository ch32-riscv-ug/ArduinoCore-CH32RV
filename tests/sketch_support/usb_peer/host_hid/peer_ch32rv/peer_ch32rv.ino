/* Another CH32RV board as the USB device for tests/peer/usb/host/hid: one HID interface with a keyboard, a mouse,
 * consumer control and a vendor HID (CH32RVUsbDevice merges them under its own report IDs), CDC serial and a RAM
 * disk. The DUT (a CH32 running CH32RVUsbHost) is the host; this board runs CH32RVUsbDevice on the controller the
 * bench record names (build_opt.h: PEER_USB_CONTROLLER, written by run_hardware.py).
 *
 * Commands, one per line on the debug probe's console (SerialDMSeq); answers start "CD " (peer_pico's "PD "):
 *   ?                    CD READY <configured 0/1>
 *   type <text>          CD TYPED <ok>
 *   mouse <dx> <dy> <b>  CD MOUSE <ok>        (moved with the buttons, then released)
 *   consumer <usage>     CD CONSUMER <ok>     (pressed, then released)
 *   leds                 CD LEDS <LED bits the host last set>
 *   cdcsend <hex>        CD CDCSENT <ok>
 *
 * Without a command: CDC echoes every byte, a vendor output report comes back as an input report with its first
 * byte incremented. The disk is 128 blocks of 512 bytes, too many for the RAM of the smaller parts: a block reads
 * as zeros until written, and a few written blocks are kept (enough for the host's write, read back, restore). */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef PEER_USB_CONTROLLER
#define PEER_USB_CONTROLLER CH32RVUsbController::Auto
#endif

CH32RVUsbDeviceHidKeyboard keyboard;
CH32RVUsbDeviceHidMouse mouse;
CH32RVUsbDeviceHidConsumerControl consumer;
CH32RVUsbDeviceHidVendor vendor;
CH32RVUsbDeviceMsc msc;

/* Reports wait this long for the host to take the one before: the DUT's host stack runs between its sketch's
 * prints, and over a slow console it can leave the endpoint unpolled for a few hundred ms (peer_pico's host_hid). */
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

/* ------------------------------------------------------------ the sparse disk */
static const uint32_t DISK_BLOCKS = 128;
static const uint8_t SLOTS = 4;
static uint8_t slotData[SLOTS][512];
static uint32_t slotLba[SLOTS];
static bool slotUsed[SLOTS];

static int slotOf(uint32_t lba, bool make)
{
    for (uint8_t i = 0; i < SLOTS; i++)
        if (slotUsed[i] && slotLba[i] == lba) return i;
    if (!make) return -1;
    for (uint8_t i = 0; i < SLOTS; i++)
        if (!slotUsed[i]) {
            slotUsed[i] = true;
            slotLba[i] = lba;
            memset(slotData[i], 0, 512);
            return i;
        }
    return -1;
}

static int32_t diskRead(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (lba >= DISK_BLOCKS || offset + size > 512) return -1;
    const int s = slotOf(lba, false);
    if (s < 0) memset(buffer, 0, size);
    else memcpy(buffer, slotData[s] + offset, size);
    return (int32_t)size;
}

static int32_t diskWrite(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (lba >= DISK_BLOCKS || offset + size > 512) return -1;
    const int s = slotOf(lba, true);
    if (s < 0) return -1;              /* out of slots */
    memcpy(slotData[s] + offset, buffer, size);
    return (int32_t)size;
}

/* ------------------------------------------------------------ HID */
static bool typeText(const char *text)
{
    for (const char *p = text; *p; p++) {
        uint8_t usage;
        if (*p >= 'a' && *p <= 'z') usage = (uint8_t)(4 + *p - 'a');
        else if (*p >= '1' && *p <= '9') usage = (uint8_t)(30 + *p - '1');
        else if (*p == '0') usage = 39;
        else return false;             /* letters and digits only: no shift */
        CH32RVUsbDeviceBootKeyboardReport r, none;
        r.keys[0] = usage;
        if (!keyboard.sendReport(r, REPORT_MS) || !keyboard.sendReport(none, REPORT_MS)) return false;
    }
    return true;
}

static uint8_t vendorEcho[64];
static volatile size_t vendorEchoLength;

/* ------------------------------------------------------------ the console */
static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char line[200];
static size_t used;

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "?")) say("CD READY %u\n", USBDevice.ready() ? 1 : 0);
    else if (!strcmp(cmd, "type")) say("CD TYPED %u\n", typeText(arg) ? 1 : 0);
    else if (!strcmp(cmd, "mouse")) {
        int dx = 0, dy = 0, b = 0;
        sscanf(arg, "%d %d %d", &dx, &dy, &b);
        const bool ok = mouse.move((int8_t)dx, (int8_t)dy, 0, (uint8_t)b, REPORT_MS) && mouse.move(0, 0, 0, 0, REPORT_MS);
        say("CD MOUSE %u\n", ok ? 1 : 0);
    } else if (!strcmp(cmd, "consumer")) {
        const uint16_t usage = (uint16_t)strtoul(arg, nullptr, 0);
        say("CD CONSUMER %u\n", consumer.press(usage, REPORT_MS) && consumer.release(REPORT_MS) ? 1 : 0);
    } else if (!strcmp(cmd, "leds")) say("CD LEDS %u\n", keyboard.ledState().leds);
    else if (!strcmp(cmd, "cdcsend")) {
        uint8_t d[96];
        size_t n = 0;
        for (const char *s = arg; s[0] && s[1] && n < sizeof d; s += 2) d[n++] = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1]));
        const bool ok = SerialUSB.write(d, n) == n;
        SerialUSB.flush();
        say("CD CDCSENT %u\n", ok ? 1 : 0);
    } else say("CD UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    vendor.onOutputReport([](const CH32RVUsbDeviceHidReport &r) {
        const size_t n = r.length > sizeof vendorEcho ? sizeof vendorEcho : r.length;
        memcpy(vendorEcho, r.data, n);
        if (n) vendorEcho[0]++;
        vendorEchoLength = n;
    });
    msc.vendorID("ch32rv");
    msc.productID("CH32RV RAM disk");
    msc.productRevision("1.0");
    msc.onRead(diskRead);
    msc.onWrite(diskWrite);
    msc.begin(DISK_BLOCKS, 512);
    msc.mediaPresent(true);
    CH32RVUsbDeviceConfig config;
    config.manufacturer = "ch32rv-tests";
    config.product = "CH32RV peer hid";
    config.serialNumber = "CH32RVPEERHID";
    config.dfuRuntime = false;
    config.controller = PEER_USB_CONTROLLER;
    const bool began = USBDevice.begin(config);
    say("CD BOOTED %u %s\n", began ? 1 : 0, USBDevice.lastErrorName());
}

void loop()
{
    deliverEvents();
    while (SerialUSB.available()) SerialUSB.write((uint8_t)SerialUSB.read());
    if (vendorEchoLength) {
        vendor.sendInput(vendorEcho, vendorEchoLength, REPORT_MS);
        vendorEchoLength = 0;
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
