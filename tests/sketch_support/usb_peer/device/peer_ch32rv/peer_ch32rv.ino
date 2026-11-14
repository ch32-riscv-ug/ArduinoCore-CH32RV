/* Another CH32RV board as the USB host for tests/peer/usb/device: the DUT (a CH32 running CH32RVUsbDevice) is
 * the device, this board runs CH32RVUsbHost on the controller the bench record names (build_opt.h:
 * PEER_USB_HOST_PORT, written by run_hardware.py). Both sides are this core's stacks, so every function can be
 * driven from both ends; the independent stacks (peer_pico, peer_s3) are what catch a shared misreading.
 *
 * The same commands as peer_pico / peer_s3, on the debug probe's console (SerialDMSeq); every answer and event is
 * a line starting "C ". The library's callbacks only record or print: the commands run from loop(), and every
 * wait runs deliverEvents() so the callbacks keep coming.
 *
 *   ping                 C PONG
 *   dev                  C DEV ... / C ITF ... per interface / C DEVEND <n>
 *   cdc <hex>            C CDC <hex echoed back within 2 s>
 *   cdcbig <n>           C CDCBIG <n> <ok> <bytes back>  (patterned echo)
 *   baud <n>             C BAUD <ok>
 *   hidv <hex>           C HIDV_SENT <ok>     (vendor output report; the first byte is its ID)
 *   leds <bits>          C LEDS <ok>
 *   midi <ch> <n> <v>    C MIDI_SENT <ok>
 *   net / netsend / netstat   C NETOPEN 0 / C NETSENT 0 / C NETRX 0 0   (no USB networking in CH32RVUsbHost)
 *   vend <hex>           C VEND <hex echoed back within 2 s>
 *   vreq                 C VREQ <ok> <answer as text>
 *   msc                  C MSC <ok> <vendor> <product> <blocks> <block size>
 *   mscraw <lba>         C MSCRAW <ok>        (write a pattern, read it back, restore)
 *   mscwrite <text>      C MSCWRITE <ok>      (/HOST.TXT on the FAT volume)
 *   mscread              C MSCREAD <ok> <first line of /README.TXT>
 *   mscblk <lba> <n>     C MSCBLK <ok> <sense key> <asc> <ascq>  (one READ(10) of n blocks, n <= 4)
 *   msctur               C MSCTUR <ok> <sense key> <asc> <ascq>  (TEST UNIT READY; the sense when it fails)
 *   ccid                 C CCID <ok> <ATR hex>
 *   apdu <hex>           C APDU <ok> <response hex> <SW hex>
 *   dfu                  C DFU <found> <ok>   (DFU_DETACH to the runtime interface; ok 0 when stalled)
 *   hididle <d> <ms>     C HIDIDLE <ok> <vendor input reports in <ms>>  (SET_IDLE duration d x 4 ms,
 *                        report ID 0, to the HID interface)
 *   reset                C RESET
 *   cdcports             C CDCPORTS <CDC ports of the device>
 *   cdcn <idx> <hex>     C CDCN <idx> <hex back within 2 s>   (any CDC port by its index)
 *   cdcline <idx> <baud> <bits> <parity> <stop>   C CDCLINE <ok>   (SET_LINE_CODING)
 *   cdcctl <idx> <dtr> <rts>                      C CDCCTL <ok>    (SET_CONTROL_LINE_STATE)
 *
 * Events: C CONNECTED <addr> <vid> <pid> <product> / C DISCONNECTED <addr> <vid> <pid> / C KEY <p|r> <keycode>
 * <ascii> / C HIDVIN <hex> / C MIDI <cin> <status> <d1> <d2> / C NOTE <text> / C HIDIN <interface> <report hex, ID
 * first> (every HID input report as it came; 00 in front for an interface without report IDs, as peer_pico).
 *
 * The strings and interfaces are the library's (CH32RVUsbHostDeviceInfo, getInterfaces()): it keeps strings whole
 * and reads a configuration of any length, as usb_contracts' 32-character product name and 302-byte
 * configuration need. Not here: the speaker, microphone and camera (no isochronous transfers in CH32RVUsbHost) and the
 * network adapter (no CDC-NCM): place.py skips usb-peer-media and usb-peer-net for this peer. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbHost.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The DUT's RAM disks are small (FAT12, a sector or two of FAT): a few sectors of FAT in RAM are enough. */
#define PEER_FAT_SECTORS 4
#include "peer_fat.h"   /* run_hardware.py stages it from device/peer_pico */
#include "peer_ch32rv.h"

#ifndef PEER_USB_HOST_PORT
#define PEER_USB_HOST_PORT CH32RV_USB_HOST_PORT_DEFAULT
#endif

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial cdc(usb);

/* printf to the console, lower-case hex as the other peers. */
static void say(const char *format, ...)
{
    char text[448];   /* a DEV line carries three strings of up to 126 characters */
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    SerialDMSeq.print(text);
}

/* ------------------------------------------------------------ hex */
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
    while (s[0] && s[1] && n < cap) {
        const int h = hexval(s[0]), l = hexval(s[1]);
        if (h < 0 || l < 0) break;
        out[n++] = (uint8_t)(h << 4 | l);
        s += 2;
    }
    return n;
}

static void printHex(const uint8_t *d, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        SerialDMSeq.print(digits[d[i] >> 4]);
        SerialDMSeq.print(digits[d[i] & 15]);
    }
}

/* Run the stack and the callbacks for `ms`. */
static void pause(uint32_t ms)
{
    const uint32_t t0 = millis();
    do {
        deliverEvents();
        delay(1);
    } while (millis() - t0 < ms);
}

/* ------------------------------------------------------------ the device */
static Device dev;
static volatile uint8_t connectedAddress;   /* set by onDeviceConnected, announced from loop() */
static bool hidHasId[16];                   /* per interface: its report descriptor declares report IDs */
static volatile uint32_t hidVendorInputs;

/* The device's interfaces, every alternate setting (the library's walk of its configuration). */
static CH32RVUsbHostInterfaceInfo itfs[32];

static size_t interfaces()
{
    return usb.getInterfaces(dev.address, itfs, sizeof itfs / sizeof itfs[0]);
}

/* The interface (alternate 0) of class / subclass, or -1. */
static int findInterface(uint8_t cls, int sub)
{
    const size_t n = interfaces();
    for (size_t i = 0; i < n; i++)
        if (itfs[i].alternate == 0 && itfs[i].interfaceClass == cls && (sub < 0 || itfs[i].interfaceSubClass == sub))
            return itfs[i].number;
    return -1;
}

static void announce()
{
    dev.address = connectedAddress;
    CH32RVUsbHostDeviceInfo info;
    if (!usb.getDevice(dev.address, info)) {
        connectedAddress = 0;
        dev = Device{};
        return;
    }
    dev.vid = info.vid;
    dev.pid = info.pid;
    dev.usb = info.usbVersion;
    dev.deviceClass = info.deviceClass;
    dev.speed = info.speed;
    strncpy(dev.manufacturer, info.manufacturer, sizeof dev.manufacturer - 1);
    strncpy(dev.product, info.product, sizeof dev.product - 1);
    strncpy(dev.serial, info.serial, sizeof dev.serial - 1);
    dev.announced = true;
    say("C CONNECTED %u %04x %04x %s\n", dev.address, dev.vid, dev.pid, dev.product);
}

static void cmdDev()
{
    unsigned n = 0;
    if (dev.address && dev.announced) {
        n = 1;
        say("C DEV %u %04x %04x %u %04x %02x %s|%s|%s\n", dev.address, dev.vid, dev.pid, dev.speed, dev.usb,
            dev.deviceClass, dev.manufacturer, dev.product, dev.serial);
        const size_t k = interfaces();
        for (size_t i = 0; i < k; i++)
            say("C ITF %u %u %u %02x %02x %02x %u\n", dev.address, itfs[i].number, itfs[i].alternate,
                itfs[i].interfaceClass, itfs[i].interfaceSubClass, itfs[i].interfaceProtocol, itfs[i].claimed ? 1 : 0);
    }
    say("C DEVEND %u\n", n);
}

/* ------------------------------------------------------------ CDC */
/* What onSerialData() brought from each port (cdcn), and the port the Stream follows (cdc, cdcbig). */
static uint8_t serialRx[2][160];
static size_t serialRxLength[2];

static size_t collectCdc(uint8_t *out, size_t want, uint32_t ms)
{
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < want && millis() - t0 < ms) {
        deliverEvents();   /* with onSerialData() set, received packets move on only from here */
        while (got < want && cdc.available()) out[got++] = (uint8_t)cdc.read();
        delay(1);
    }
    return got;
}

static void cmdCdc(const char *arg)
{
    uint8_t tx[96], rx[96];
    const size_t n = parseHex(arg, tx, sizeof tx);
    while (cdc.available()) cdc.read();
    cdc.write(tx, n);
    const size_t got = collectCdc(rx, n, 2000);
    SerialDMSeq.print("C CDC ");
    printHex(rx, got);
    SerialDMSeq.println();
}

static void cmdCdcBig(size_t n)
{
    if (n > 4096) n = 4096;
    while (cdc.available()) cdc.read();
    size_t sent = 0, got = 0, bad = 0;
    const uint32_t t0 = millis();
    while ((sent < n || got < n) && millis() - t0 < 10000) {
        if (sent < n) {
            uint8_t chunk[32];
            const size_t k = n - sent < sizeof chunk ? n - sent : sizeof chunk;
            for (size_t i = 0; i < k; i++) chunk[i] = (uint8_t)((sent + i) * 13 + 7);
            if (cdc.write(chunk, k) == k) sent += k;
        }
        deliverEvents();
        while (got < n && cdc.available()) {
            if ((uint8_t)cdc.read() != (uint8_t)(got * 13 + 7)) bad++;
            got++;
        }
        delay(1);
    }
    say("C CDCBIG %u %u %u\n", (unsigned)n, (got == n && !bad) ? 1 : 0, (unsigned)got);
}

static void cmdCdcN(const char *arg)
{
    char *end;
    const uint8_t idx = (uint8_t)strtoul(arg, &end, 10);
    uint8_t tx[160];
    const size_t n = parseHex(*end ? end + 1 : end, tx, sizeof tx);
    const uint8_t port = idx & 1;
    pause(5);
    serialRxLength[port] = 0;
    if (idx < 2 && usb.sendSerial(tx, n, CH32RV_USB_HOST_ANY_ADDRESS, idx)) {
        const uint32_t t0 = millis();
        while (serialRxLength[port] < n && millis() - t0 < 2000) pause(1);
    }
    say("C CDCN %u ", idx);
    printHex(serialRx[port], idx < 2 ? serialRxLength[port] : 0);
    SerialDMSeq.println();
}

static void cmdCdcLine(const char *arg)
{
    char *end;
    const uint8_t idx = (uint8_t)strtoul(arg, &end, 10);
    CH32RVUsbHostSerialConfig c;
    c.baud = strtoul(end, &end, 10);
    c.dataBits = (uint8_t)strtoul(end, &end, 10);
    c.parity = (CH32RVUsbHostSerialParity)strtoul(end, &end, 10);
    c.stopBits = (CH32RVUsbHostSerialStopBits)strtoul(end, &end, 10);
    say("C CDCLINE %u\n", usb.setSerialConfig(c, CH32RV_USB_HOST_ANY_ADDRESS, idx) ? 1 : 0);
}

static void cmdCdcCtl(const char *arg)
{
    char *end;
    const uint8_t idx = (uint8_t)strtoul(arg, &end, 10);
    const bool dtr = strtoul(end, &end, 10) != 0, rts = strtoul(end, &end, 10) != 0;
    say("C CDCCTL %u\n", usb.setSerialControlLines(dtr, rts, CH32RV_USB_HOST_ANY_ADDRESS, idx) ? 1 : 0);
}

/* ------------------------------------------------------------ HID */
static void cmdHidv(const char *arg)
{
    uint8_t d[64];
    const size_t n = parseHex(arg, d, sizeof d);
    /* CH32RVUsbHost puts the vendor report's own ID in front: the first byte given here is that ID. */
    say("C HIDV_SENT %u\n", n >= 1 && usb.sendHIDVendorOutput(d + 1, n - 1) ? 1 : 0);
}

static void cmdHidIdle(const char *arg)
{
    unsigned duration = 0, ms = 0;
    sscanf(arg, "%u %u", &duration, &ms);
    const int itf = findInterface(0x03, -1);
    if (itf < 0) {
        SerialDMSeq.println("C HIDIDLE 0 0");
        return;
    }
    const bool ok = usb.vendorControlTransfer(0x21, 0x0A, (uint16_t)(duration << 8), (uint16_t)itf, nullptr, 0,
                                              nullptr, dev.address, 2000);
    const uint32_t before = hidVendorInputs;
    pause(ms);
    say("C HIDIDLE %u %lu\n", ok ? 1 : 0, (unsigned long)(hidVendorInputs - before));
}

/* Whether a report descriptor declares report IDs (a Report ID item, 0x85, among its short items). */
static bool declaresReportId(const uint8_t *d, uint16_t n)
{
    for (uint16_t at = 0; at < n;) {
        if (d[at] == 0xFE) {                        /* a long item */
            if (at + 2 >= n) break;
            at = (uint16_t)(at + 3 + d[at + 1]);
            continue;
        }
        const uint8_t size = (d[at] & 3) == 3 ? 4 : (d[at] & 3);
        if ((d[at] & 0xFC) == 0x84) return true;
        at = (uint16_t)(at + 1 + size);
    }
    return false;
}

/* ------------------------------------------------------------ vendor bulk */
static void cmdVend(const char *arg)
{
    uint8_t tx[128], rx[128];
    const size_t n = parseHex(arg, tx, sizeof tx);
    if (!usb.vendorReady() && !usb.vendorOpen()) {
        say("C VEND ERR %s\n", usb.lastErrorName());
        return;
    }
    while (usb.vendorRead(rx, sizeof rx)) {
    }
    if (!usb.vendorWrite(tx, n, CH32RV_USB_HOST_ANY_ADDRESS, 2000)) {
        say("C VEND ERR %s\n", usb.lastErrorName());
        return;
    }
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < n && millis() - t0 < 2000) {
        got += usb.vendorRead(rx + got, n - got);
        delay(1);
    }
    SerialDMSeq.print("C VEND ");
    printHex(rx, got);
    SerialDMSeq.println();
}

static void cmdVreq()
{
    char buf[16] = {};
    const bool ok = usb.vendorControlIn(0x10, 0, 0, (uint8_t *)buf, sizeof buf - 1);
    say("C VREQ %u %s\n", ok ? 1 : 0, buf);
}

/* ------------------------------------------------------------ mass storage */
static uint8_t blocks[4 * 512];

static void sense(uint8_t *key, uint8_t *asc, uint8_t *ascq)
{
    CH32RVUsbHostMscSense s;
    if (usb.mscRequestSense(s)) {
        *key = s.senseKey;
        *asc = s.additionalSenseCode;
        *ascq = s.additionalSenseQualifier;
    }
}

static void cmdMsc()
{
    CH32RVUsbHostMscInquiry inq;
    uint32_t count = 0, size = 0;
    bool ok = false;
    for (int i = 0; i < 10 && !(ok = usb.mscTestUnitReady(CH32RV_USB_HOST_ANY_ADDRESS, 2000)); i++) pause(200);
    ok = ok && usb.mscInquiry(inq) && usb.mscCapacity(count, size);
    say("C MSC %u %s %s %lu %lu\n", ok ? 1 : 0, ok ? inq.vendor : "-", ok ? inq.product : "-",
        (unsigned long)(ok ? count : 0), (unsigned long)(ok ? size : 0));
}

static void cmdMscRaw(uint32_t lba)
{
    uint8_t *keep = blocks, *pattern = blocks + 512, *back = blocks + 1024;
    for (size_t i = 0; i < 512; i++) pattern[i] = (uint8_t)(i ^ (lba * 31));
    bool ok = usb.mscReadBlocks(lba, keep, 1);
    ok = ok && usb.mscWriteBlocks(lba, pattern, 1);
    ok = ok && usb.mscReadBlocks(lba, back, 1) && memcmp(back, pattern, 512) == 0;
    ok = usb.mscWriteBlocks(lba, keep, 1) && ok;
    say("C MSCRAW %u\n", ok ? 1 : 0);
}

static void cmdMscBlocks(const char *arg)
{
    char *end = nullptr;
    const uint32_t lba = strtoul(arg, &end, 10);
    uint32_t n = strtoul(end, nullptr, 10);
    n = n < 1 ? 1 : n > 4 ? 4 : n;
    uint8_t key = 0, asc = 0, ascq = 0;
    const bool ok = usb.mscReadBlocks(lba, blocks, n);
    if (!ok) sense(&key, &asc, &ascq);
    say("C MSCBLK %u %02x %02x %02x\n", ok ? 1 : 0, key, asc, ascq);
}

static void cmdMscTur()
{
    uint8_t key = 0, asc = 0, ascq = 0;
    const bool ok = usb.mscTestUnitReady(CH32RV_USB_HOST_ANY_ADDRESS, 2000);
    if (!ok) sense(&key, &asc, &ascq);
    say("C MSCTUR %u %02x %02x %02x\n", ok ? 1 : 0, key, asc, ascq);
}

static bool fatRead(uint32_t lba, uint8_t *buffer, uint16_t count) { return usb.mscReadBlocks(lba, buffer, count); }
static bool fatWrite(uint32_t lba, const uint8_t *buffer, uint16_t count)
{
    return usb.mscWriteBlocks(lba, buffer, count);
}
static PeerFat fat(fatRead, fatWrite);

static bool mountFat()
{
    /* UNIT ATTENTION once after a change */
    if (!usb.mscTestUnitReady(CH32RV_USB_HOST_ANY_ADDRESS, 2000) && !usb.mscTestUnitReady(CH32RV_USB_HOST_ANY_ADDRESS, 2000))
        return false;
    return fat.mount();
}

static void cmdMscWrite(const char *text)
{
    char body[200];
    const int n = snprintf(body, sizeof body, "%s\r\n", text);
    const bool ok = mountFat() && fat.writeFile("HOST.TXT", (const uint8_t *)body, (uint32_t)n);
    say("C MSCWRITE %u\n", ok ? 1 : 0);
}

static void cmdMscRead()
{
    char first[96] = {};
    const bool ok = mountFat() && fat.firstLine("README.TXT", first, sizeof first);
    say("C MSCREAD %u %s\n", ok ? 1 : 0, first);
}

/* ------------------------------------------------------------ smart card (CCID) */
static void cmdCcid()
{
    uint8_t atr[33];
    size_t n = 0;
    const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidPowerOn(atr, sizeof atr, &n);
    SerialDMSeq.print(ok ? "C CCID 1 " : "C CCID 0 ");
    printHex(atr, ok ? n : 0);
    SerialDMSeq.println();
}

static void cmdApdu(const char *arg)
{
    uint8_t tx[64], rx[72];
    const size_t n = parseHex(arg, tx, sizeof tx);
    size_t got = 0;
    uint16_t sw = 0;
    const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidApdu(tx, n, rx, sizeof rx, &got, &sw);
    SerialDMSeq.print(ok ? "C APDU 1 " : "C APDU 0 ");
    printHex(rx, ok ? got : 0);
    say(" %04x\n", ok ? sw : 0);
}

/* ------------------------------------------------------------ DFU */
static void cmdDfu()
{
    const int itf = findInterface(0xFE, 0x01);
    if (itf < 0) {
        SerialDMSeq.println("C DFU 0 0");
        return;
    }
    /* DFU_DETACH, wTimeout 1000 ms. The device may leave before the status stage completes: only a stall is a no. */
    const bool ok = usb.vendorControlTransfer(0x21, 0x00, 1000, (uint16_t)itf, nullptr, 0, nullptr, dev.address, 2000);
    say("C DFU 1 %u\n", ok || usb.lastError() != CH32RVUsbHostError::Stalled ? 1 : 0);
}

/* ------------------------------------------------------------ commands */
static char line[400];
static size_t used;

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "ping")) SerialDMSeq.println("C PONG");
    else if (!strcmp(cmd, "dev")) cmdDev();
    else if (!strcmp(cmd, "cdc")) cmdCdc(arg);
    else if (!strcmp(cmd, "cdcbig")) cmdCdcBig(strtoul(arg, nullptr, 10));
    else if (!strcmp(cmd, "baud")) say("C BAUD %u\n", cdc.setBaudRate(strtoul(arg, nullptr, 10)) ? 1 : 0);
    else if (!strcmp(cmd, "hidv")) cmdHidv(arg);
    else if (!strcmp(cmd, "leds")) {
        const unsigned v = strtoul(arg, nullptr, 0);
        say("C LEDS %u\n", usb.setKeyboardLeds(v & 1, v & 2, v & 4) ? 1 : 0);
    } else if (!strcmp(cmd, "midi")) {
        unsigned ch = 0, n = 0, v = 0;
        sscanf(arg, "%u %u %u", &ch, &n, &v);
        say("C MIDI_SENT %u\n", usb.midiSendNoteOn((uint8_t)ch, (uint8_t)n, (uint8_t)v) ? 1 : 0);
    } else if (!strcmp(cmd, "net")) {
        SerialDMSeq.println("C NOTE CH32RVUsbHost has no USB networking (CDC-NCM)");
        SerialDMSeq.println("C NETOPEN 0");
    } else if (!strcmp(cmd, "netsend")) SerialDMSeq.println("C NETSENT 0");
    else if (!strcmp(cmd, "netstat")) SerialDMSeq.println("C NETRX 0 0");
    else if (!strcmp(cmd, "vend")) cmdVend(arg);
    else if (!strcmp(cmd, "vreq")) cmdVreq();
    else if (!strcmp(cmd, "hididle")) cmdHidIdle(arg);
    else if (!strcmp(cmd, "msc")) cmdMsc();
    else if (!strcmp(cmd, "mscraw")) cmdMscRaw(strtoul(arg, nullptr, 10));
    else if (!strcmp(cmd, "mscwrite")) cmdMscWrite(arg);
    else if (!strcmp(cmd, "mscread")) cmdMscRead();
    else if (!strcmp(cmd, "msctur")) cmdMscTur();
    else if (!strcmp(cmd, "mscblk")) cmdMscBlocks(arg);
    else if (!strcmp(cmd, "ccid")) cmdCcid();
    else if (!strcmp(cmd, "apdu")) cmdApdu(arg);
    else if (!strcmp(cmd, "dfu")) cmdDfu();
    else if (!strcmp(cmd, "cdcports")) say("C CDCPORTS %u\n", usb.serialPortCount());
    else if (!strcmp(cmd, "cdcn")) cmdCdcN(arg);
    else if (!strcmp(cmd, "cdcline")) cmdCdcLine(arg);
    else if (!strcmp(cmd, "cdcctl")) cmdCdcCtl(arg);
    else if (!strcmp(cmd, "reset")) SerialDMSeq.println("C RESET");
    else say("C UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) {
        if (!connectedAddress) connectedAddress = d.address;
    });
    usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &d) {
        if (d.address != dev.address && d.address != connectedAddress) return;
        say("C DISCONNECTED %u %04x %04x\n", d.address, d.vid, d.pid);
        connectedAddress = 0;
        dev = Device{};
        memset(hidHasId, 0, sizeof hidHasId);
    });
    usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
        say("C KEY %c %u %u\n", e.pressed ? 'p' : 'r', e.keycode, e.ascii);
    });
    usb.onHIDVendorInput([](const CH32RVUsbHostHIDVendorInput &e) {
        hidVendorInputs++;
        SerialDMSeq.print("C HIDVIN ");
        printHex(e.reportData, e.reportLength);
        SerialDMSeq.println();
    });
    usb.onHIDReportDescriptor([](const CH32RVUsbHostHIDReportDescriptor &e) {
        if (e.interfaceNumber < sizeof hidHasId) hidHasId[e.interfaceNumber] = declaresReportId(e.data, e.length);
    });
    usb.onHIDInput([](const CH32RVUsbHostHIDInput &e) {
        say("C HIDIN %u ", e.interfaceNumber);
        if (e.interfaceNumber < sizeof hidHasId && !hidHasId[e.interfaceNumber]) SerialDMSeq.print("00");
        printHex(e.data, e.length);
        SerialDMSeq.println();
    });
    usb.onMidiMessage([](const CH32RVUsbHostMidiMessage &m) {
        say("C MIDI %u %02x %u %u\n", m.codeIndex, m.status, m.data1, m.data2);
    });
    usb.onSerialData([](const CH32RVUsbHostSerialData &e) {
        const uint8_t port = e.port & 1;
        for (size_t i = 0; i < e.length && serialRxLength[port] < sizeof serialRx[port]; i++)
            serialRx[port][serialRxLength[port]++] = e.data[i];
    });
    cdc.begin(115200);
    CH32RVUsbHostConfig config;
    config.port = PEER_USB_HOST_PORT;
    const bool began = usb.begin(config);
    say("C READY %u %s\n", began ? 1 : 0, usb.lastErrorName());
}

void loop()
{
    deliverEvents();
    if (connectedAddress && !dev.announced) announce();
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
