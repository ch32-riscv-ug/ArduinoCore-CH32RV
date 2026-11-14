/* Another CH32RV board as the USB device for tests/peer/usb/host/classes: MIDI, a vendor bulk interface with a
 * vendor request, and a smart card reader (CCID). The DUT (CH32RVUsbHost) is the host; this board runs
 * CH32RVUsbDevice on the controller the bench record names (build_opt.h: PEER_USB_CONTROLLER, written by
 * run_hardware.py).
 *
 * Commands, one per line on the debug probe's console (SerialDMSeq); answers start "CD " (peer_pico's "PD "):
 *   ?                      CD READY <configured 0/1>
 *   midisend <ch> <n> <v>  CD MIDISENT <ok>
 *   card                   CD CARD <APDUs answered>
 *
 * Without a command: a MIDI packet comes back with its note raised by 12, the vendor interface echoes every byte,
 * vendor request 0x10 (IN) answers "CH32RV", and the card answers an APDU with its bytes reversed, then 90 00. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef PEER_USB_CONTROLLER
#define PEER_USB_CONTROLLER CH32RVUsbController::Auto
#endif

CH32RVUsbDeviceMidi midi;
CH32RVUsbDeviceVendor vendor;
CH32RVUsbDeviceCcid ccid;

/* 3B 88: T=0 and 8 historical bytes, as peer_pico's ("RP2040CC" there). */
static const uint8_t atr[] = {0x3B, 0x88, 0x80, 0x01, 'C', 'H', '3', '2', 'R', 'V', 'C', 'C', 0x00};

static void say(const char *format, ...)
{
    char text[160];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    SerialDMSeq.print(text);
}

static bool vendorRequest(const CH32RVUsbDeviceVendorControlRequest &request)
{
    static const char answer[] = "CH32RV";
    if (request.bRequest != 0x10 || !(request.bmRequestType & 0x80)) return false;
    if (request.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) return true;
    const size_t n = request.wLength < sizeof answer - 1 ? request.wLength : sizeof answer - 1;
    return vendor.sendControlResponse(request, answer, n);
}

static size_t apdu(const uint8_t *in, size_t length, uint8_t *out, size_t capacity)
{
    if (length + 2 > capacity) return 0;
    for (size_t i = 0; i < length; i++) out[i] = in[length - 1 - i];
    out[length] = 0x90;
    out[length + 1] = 0x00;
    return length + 2;
}

static char line[120];
static size_t used;

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "?")) say("CD READY %u\n", USBDevice.ready() ? 1 : 0);
    else if (!strcmp(cmd, "midisend")) {
        unsigned ch = 0, n = 0, v = 0;
        sscanf(arg, "%u %u %u", &ch, &n, &v);
        say("CD MIDISENT %u\n", midi.noteOn((uint8_t)ch, (uint8_t)n, (uint8_t)v) ? 1 : 0);
    } else if (!strcmp(cmd, "card")) say("CD CARD %lu\n", (unsigned long)ccid.apduCount());
    else say("CD UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    vendor.onControlRequest(vendorRequest);
    ccid.onApdu(apdu);
    ccid.insertCard(atr, sizeof atr);
    CH32RVUsbDeviceConfig config;
    config.manufacturer = "ch32rv-tests";
    config.product = "CH32RV peer classes";
    config.serialNumber = "CH32RVPEERCLS";
    config.dfuRuntime = false;
    config.controller = PEER_USB_CONTROLLER;
    const bool began = USBDevice.begin(config);
    say("CD BOOTED %u %s\n", began ? 1 : 0, USBDevice.lastErrorName());
}

void loop()
{
    deliverEvents();
    CH32RVUsbDeviceMidiPacket p;
    while (midi.availableForWritePackets() && midi.readPacket(p)) {
        p.byte2 = (uint8_t)(p.byte2 + 12);
        midi.writePacket(p);
    }
    uint8_t buf[64];
    const size_t n = vendor.available() ? vendor.read(buf, sizeof buf) : 0;
    for (size_t sent = 0; sent < n && vendor.waitWritable(n - sent, 500);) sent += vendor.write(buf + sent, n - sent);
    if (n) vendor.flush();
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
