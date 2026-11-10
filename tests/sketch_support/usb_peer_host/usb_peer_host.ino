/* USB host contracts (tests/peer/usb/host): the DUT runs CH32RVUsbHost on its
 * own USB port and an ESP32-S3 (EspUsbDevice) is the device. Commands arrive on
 * the debug-probe console (DMSEQ), every answer and event is a line starting
 * "H ":
 *
 *   ready <nonce>        READY <nonce> <build id> <begin ok 0/1> <last error>
 *   dev                  H DEV ... / H ITF ... per interface / H DEVEND <n>
 *   cdc <hex>            H CDC <hex echoed back within 2 s>
 *   cdcbig <n>           H CDCBIG <n> <ok> <bytes back>
 *   baud <n>             H BAUD <ok>
 *   hidv <hex>           H HIDV_SENT <ok>      (vendor output report, no ID)
 *   leds <bits>          H LEDS <ok>
 *   midi <ch> <n> <v>    H MIDI_SENT <ok>
 *   msc                  H MSC <ok> <vendor> <product> <blocks> <block size>
 *   mscraw <lba>         H MSCRAW <ok>         (pattern written, read back, restored)
 *   vend <hex>           H VEND <hex echoed back within 2 s>
 *   vreq                 H VREQ <ok> <answer as text>
 *   ccid                 H CCID <ok> <ATR hex>
 *   apdu <hex>           H APDU <ok> <response hex> <SW hex>
 *   strings              H STRINGS <ok> <manufacturer>|<product>|<serial>
 *   ports                H PORTS <CDC ports of the device>
 *   sser <port> <hex>    H SSER <ok>           (sendSerial() to that port)
 *   srx <port>           H SRX <port> <hex>    (what onSerialData() brought from it since the last srx)
 *   sconf <port> <baud> <bits> <parity> <stop>   H SCONF <ok>   (setSerialConfig())
 *   slines <port> <dtr> <rts>   H SLINES <ok>  (setSerialControlLines())
 *   hidget <itf> <type> <id> <n>   H HIDGET <ok> <hex>   (getHIDReport())
 *   hidset <itf> <type> <id> <hex> H HIDSET <ok>        (sendHIDReport())
 *   hidfeat <hex>        H HIDFEAT <ok>        (sendHIDVendorFeature())
 *   proto <itf> <boot 0/1>   H PROTO <ok>      (sendSetProtocol())
 *   layout <us|jp>       H LAYOUT              (setKeyboardLayout())
 *   locks                H LOCKS <num> <caps> <scroll>
 *   hiddesc              H HIDDESC <itf> <bytes> per report descriptor seen / H HIDDESCEND
 *   hidraw <0|1>         H HIDRAW <0|1>        (onHIDInput() events on or off)
 *   mscmaxlun            H MSCMAXLUN <ok> <max LUN>
 *   mscrd <lba>          H MSCRD <ok> <sense key> <asc> <ascq>  (one block read; the sense when it fails)
 *   msclun <n>           H MSCLUN <ok>         (mscSelectLun(): the unit the next msc* commands use)
 *   msctur               H MSCTUR <ok> <sense key> <asc> <ascq>  (the sense asked for when not ready)
 *   midicc <ch> <c> <v> / midipc <ch> <p> / midipb <ch> <v> / midisx <hex>   H MIDIOUT <ok>
 *   vreqout <req> <value> <index> <hex>   H VREQOUT <ok>   (vendorControlOut())
 *   ccidstatus           H CCIDSTATUS <ok> <icc status> <present> <active>
 *   ccidpresent          H CCIDPRESENT <present>
 *   ccidoff              H CCIDOFF <ok>
 *   ccidesc <hex>        H CCIDESC <ok> <hex>  (ccidEscape())
 *
 * Events: H CONNECTED <addr> <vid> <pid> <product> / H DISCONNECTED <addr>
 * <vid> <pid> / H KEY <p|r> <keycode> <ascii> / H MOUSE <x> <y> <wheel>
 * <buttons> / H CONSUMER <usage> <p|r> / H HIDVIN <hex> / H MIDI <cin>
 * <status> <d1> <d2> / H GAMEPAD <report hex> / H SYSTEM <usage> <p|r> /
 * H HIDIN <itf> <report hex> (with hidraw 1) / H CARD <in|out>. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbHost.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial cdc(usb);

static bool began;
static char line[400];
static uint16_t used;
static uint8_t block[512], keep[512];
static uint8_t serialRx[2][256];
static size_t serialRxLength[2];
static uint8_t hidDescItf[4];
static uint16_t hidDescLength[4];
static uint8_t hidDescCount;
static bool hidRaw;

static uint8_t any_address() { return usb.resolve(CH32RV_USB_HOST_ANY_ADDRESS); }

static void hex(const uint8_t *d, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        SerialDMSeq.print(digits[d[i] >> 4]);
        SerialDMSeq.print(digits[d[i] & 15]);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t parse_hex(const char *s, uint8_t *out, size_t cap)
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

static void print4(uint16_t v)
{
    static const char digits[] = "0123456789abcdef";
    for (int s = 12; s >= 0; s -= 4) SerialDMSeq.print(digits[(v >> s) & 15]);
}

static void cmd_dev()
{
    CH32RVUsbHostDeviceInfo list[4];
    const size_t n = usb.getDevices(list, 4);
    for (size_t i = 0; i < n; i++) {
        const CH32RVUsbHostDeviceInfo &d = list[i];
        SerialDMSeq.print("H DEV ");
        SerialDMSeq.print(d.address);
        SerialDMSeq.print(' ');
        print4(d.vid);
        SerialDMSeq.print(' ');
        print4(d.pid);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(d.speed);
        SerialDMSeq.print(' ');
        print4(d.usbVersion);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(d.deviceClass, HEX);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(d.manufacturer);
        SerialDMSeq.print('|');
        SerialDMSeq.print(d.product);
        SerialDMSeq.print('|');
        SerialDMSeq.println(d.serial);
        CH32RVUsbHostInterfaceInfo itfs[8];
        const size_t ni = usb.getInterfaces(d.address, itfs, 8);
        for (size_t k = 0; k < ni; k++) {
            SerialDMSeq.print("H ITF ");
            SerialDMSeq.print(d.address);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(itfs[k].number);
            SerialDMSeq.print(' ');
            SerialDMSeq.print(itfs[k].alternate);
            SerialDMSeq.print(' ');
            hex(&itfs[k].interfaceClass, 1);
            SerialDMSeq.print(' ');
            hex(&itfs[k].interfaceSubClass, 1);
            SerialDMSeq.print(' ');
            hex(&itfs[k].interfaceProtocol, 1);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(itfs[k].claimed ? 1 : 0);
        }
    }
    SerialDMSeq.print("H DEVEND ");
    SerialDMSeq.println((unsigned)n);
}

static size_t collect_cdc(uint8_t *out, size_t want, uint32_t ms)
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

static void cmd_cdc(const char *arg)
{
    uint8_t tx[160], rx[160];
    const size_t n = parse_hex(arg, tx, sizeof tx);
    while (cdc.available()) cdc.read();
    cdc.write(tx, n);
    const size_t got = collect_cdc(rx, n, 2000);
    SerialDMSeq.print("H CDC ");
    hex(rx, got);
    SerialDMSeq.println();
}

static void cmd_cdcbig(size_t n)
{
    if (n > 2048) n = 2048;
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
        deliverEvents();   /* with onSerialData() set, received packets move on only from here */
        while (got < n && cdc.available()) {
            if ((uint8_t)cdc.read() != (uint8_t)(got * 13 + 7)) bad++;
            got++;
        }
        delay(1);
    }
    SerialDMSeq.print("H CDCBIG ");
    SerialDMSeq.print((unsigned)n);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(got == n && !bad ? 1 : 0);
    SerialDMSeq.print(' ');
    SerialDMSeq.println((unsigned)got);
}

static void cmd_vend(const char *arg)
{
    uint8_t tx[160], rx[160];
    const size_t n = parse_hex(arg, tx, sizeof tx);
    if (!usb.vendorReady() && !usb.vendorOpen()) {
        SerialDMSeq.print("H VEND ERR ");
        SerialDMSeq.println(usb.lastErrorName());
        return;
    }
    while (usb.vendorRead(rx, sizeof rx)) {
    }
    if (!usb.vendorWrite(tx, n)) {
        SerialDMSeq.print("H VEND ERR ");
        SerialDMSeq.println(usb.lastErrorName());
        return;
    }
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < n && millis() - t0 < 2000) {
        got += usb.vendorRead(rx + got, n - got);
        delay(1);
    }
    SerialDMSeq.print("H VEND ");
    hex(rx, got);
    SerialDMSeq.println();
}

static void cmd_msc()
{
    CH32RVUsbHostMscInquiry inq;
    uint32_t blocks = 0, size = 0;
    const bool ok = usb.mscWaitReady() && usb.mscInquiry(inq) && usb.mscCapacity(blocks, size);
    SerialDMSeq.print("H MSC ");
    SerialDMSeq.print(ok ? 1 : 0);
    SerialDMSeq.print(' ');
    SerialDMSeq.print(ok ? inq.vendor : "-");
    SerialDMSeq.print(' ');
    SerialDMSeq.print(ok ? inq.product : "-");
    SerialDMSeq.print(' ');
    SerialDMSeq.print((unsigned long)blocks);
    SerialDMSeq.print(' ');
    SerialDMSeq.println((unsigned long)size);
}

static void cmd_mscraw(uint32_t lba)
{
    bool ok = usb.mscReadBlocks(lba, keep, 1);
    for (size_t i = 0; i < 512; i++) block[i] = (uint8_t)(i ^ (lba * 31));
    ok = ok && usb.mscWriteBlocks(lba, block, 1);
    memset(block, 0, sizeof block);
    ok = ok && usb.mscReadBlocks(lba, block, 1);
    for (size_t i = 0; ok && i < 512; i++) ok = block[i] == (uint8_t)(i ^ (lba * 31));
    ok = usb.mscWriteBlocks(lba, keep, 1) && ok;
    SerialDMSeq.print("H MSCRAW ");
    SerialDMSeq.println(ok ? 1 : 0);
}

static void reply(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (strcmp(cmd, "ready") == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(arg);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(began ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(usb.lastErrorName());
    } else if (strcmp(cmd, "dev") == 0) {
        cmd_dev();
    } else if (strcmp(cmd, "cdc") == 0) {
        cmd_cdc(arg);
    } else if (strcmp(cmd, "cdcbig") == 0) {
        cmd_cdcbig(strtoul(arg, nullptr, 10));
    } else if (strcmp(cmd, "baud") == 0) {
        SerialDMSeq.print("H BAUD ");
        SerialDMSeq.println(cdc.setBaudRate(strtoul(arg, nullptr, 10)) ? 1 : 0);
    } else if (strcmp(cmd, "hidv") == 0) {
        uint8_t d[63];
        const size_t n = parse_hex(arg, d, sizeof d);
        SerialDMSeq.print("H HIDV_SENT ");
        SerialDMSeq.println(usb.sendHIDVendorOutput(d, n) ? 1 : 0);
    } else if (strcmp(cmd, "leds") == 0) {
        const unsigned v = strtoul(arg, nullptr, 0);
        SerialDMSeq.print("H LEDS ");
        SerialDMSeq.println(usb.setKeyboardLeds(v & 1, v & 2, v & 4) ? 1 : 0);
    } else if (strcmp(cmd, "midi") == 0) {
        char *end;
        const unsigned ch = strtoul(arg, &end, 10);
        const unsigned note = strtoul(end, &end, 10);
        const unsigned vel = strtoul(end, &end, 10);
        SerialDMSeq.print("H MIDI_SENT ");
        SerialDMSeq.println(usb.midiSendNoteOn(ch, note, vel) ? 1 : 0);
    } else if (strcmp(cmd, "msc") == 0) {
        cmd_msc();
    } else if (strcmp(cmd, "mscraw") == 0) {
        cmd_mscraw(strtoul(arg, nullptr, 10));
    } else if (strcmp(cmd, "vend") == 0) {
        cmd_vend(arg);
    } else if (strcmp(cmd, "vreq") == 0) {
        char buf[16] = {};
        size_t got = 0;
        const bool ok = usb.vendorControlIn(0x10, 0, 0, (uint8_t *)buf, sizeof buf - 1, &got);
        SerialDMSeq.print("H VREQ ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(buf);
    } else if (strcmp(cmd, "ccid") == 0) {
        uint8_t atr[33];
        size_t n = 0;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidPowerOn(atr, sizeof atr, &n);
        SerialDMSeq.print("H CCID ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        hex(atr, n);
        SerialDMSeq.println();
    } else if (strcmp(cmd, "apdu") == 0) {
        uint8_t tx[32], rx[40];
        const size_t n = parse_hex(arg, tx, sizeof tx);
        size_t got = 0;
        uint16_t sw = 0;
        const bool ok = usb.ccidApdu(tx, n, rx, sizeof rx, &got, &sw);
        SerialDMSeq.print("H APDU ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        hex(rx, got);
        SerialDMSeq.print(' ');
        print4(sw);
        SerialDMSeq.println();
    } else if (strcmp(cmd, "strings") == 0) {
        CH32RVUsbHostDeviceInfo d;
        const bool ok = usb.getDevice(usb.resolve(CH32RV_USB_HOST_ANY_ADDRESS), d);
        SerialDMSeq.print("H STRINGS ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(d.manufacturer);
        SerialDMSeq.print('|');
        SerialDMSeq.print(d.product);
        SerialDMSeq.print('|');
        SerialDMSeq.println(d.serial);
    } else if (strcmp(cmd, "ports") == 0) {
        SerialDMSeq.print("H PORTS ");
        SerialDMSeq.println(usb.serialPortCount());
    } else if (strcmp(cmd, "sser") == 0) {
        char *end;
        const uint8_t port = (uint8_t)strtoul(arg, &end, 10);
        uint8_t d[160];
        const size_t n = parse_hex(*end ? end + 1 : end, d, sizeof d);
        SerialDMSeq.print("H SSER ");
        SerialDMSeq.println(usb.sendSerial(d, n, CH32RV_USB_HOST_ANY_ADDRESS, port) ? 1 : 0);
    } else if (strcmp(cmd, "srx") == 0) {
        const uint8_t port = (uint8_t)strtoul(arg, nullptr, 10) & 1;
        SerialDMSeq.print("H SRX ");
        SerialDMSeq.print(port);
        SerialDMSeq.print(' ');
        hex(serialRx[port], serialRxLength[port]);
        SerialDMSeq.println();
        serialRxLength[port] = 0;
    } else if (strcmp(cmd, "sconf") == 0) {
        char *end;
        const uint8_t port = (uint8_t)strtoul(arg, &end, 10);
        CH32RVUsbHostSerialConfig c;
        c.baud = strtoul(end, &end, 10);
        c.dataBits = (uint8_t)strtoul(end, &end, 10);
        c.parity = (CH32RVUsbHostSerialParity)strtoul(end, &end, 10);
        c.stopBits = (CH32RVUsbHostSerialStopBits)strtoul(end, &end, 10);
        SerialDMSeq.print("H SCONF ");
        SerialDMSeq.println(usb.setSerialConfig(c, CH32RV_USB_HOST_ANY_ADDRESS, port) ? 1 : 0);
    } else if (strcmp(cmd, "slines") == 0) {
        char *end;
        const uint8_t port = (uint8_t)strtoul(arg, &end, 10);
        const bool dtr = strtoul(end, &end, 10) != 0;
        const bool rts = strtoul(end, &end, 10) != 0;
        SerialDMSeq.print("H SLINES ");
        SerialDMSeq.println(usb.setSerialControlLines(dtr, rts, CH32RV_USB_HOST_ANY_ADDRESS, port) ? 1 : 0);
    } else if (strcmp(cmd, "hidget") == 0) {
        char *end;
        const uint8_t itf = (uint8_t)strtoul(arg, &end, 10);
        const uint8_t type = (uint8_t)strtoul(end, &end, 10);
        const uint8_t id = (uint8_t)strtoul(end, &end, 10);
        size_t n = strtoul(end, &end, 10), got = 0;
        uint8_t d[64];
        n = n > sizeof d ? sizeof d : n;
        const bool ok = usb.getHIDReport(itf, type, id, d, n, &got);
        SerialDMSeq.print("H HIDGET ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        hex(d, ok ? got : 0);
        SerialDMSeq.println();
    } else if (strcmp(cmd, "hidset") == 0) {
        char *end;
        const uint8_t itf = (uint8_t)strtoul(arg, &end, 10);
        const uint8_t type = (uint8_t)strtoul(end, &end, 10);
        const uint8_t id = (uint8_t)strtoul(end, &end, 10);
        uint8_t d[64];
        const size_t n = parse_hex(*end ? end + 1 : end, d, sizeof d);
        SerialDMSeq.print("H HIDSET ");
        SerialDMSeq.println(usb.sendHIDReport(itf, type, id, d, n) ? 1 : 0);
    } else if (strcmp(cmd, "hidfeat") == 0) {
        uint8_t d[63];
        const size_t n = parse_hex(arg, d, sizeof d);
        SerialDMSeq.print("H HIDFEAT ");
        SerialDMSeq.println(usb.sendHIDVendorFeature(d, n) ? 1 : 0);
    } else if (strcmp(cmd, "proto") == 0) {
        char *end;
        const uint8_t itf = (uint8_t)strtoul(arg, &end, 10);
        const bool boot = strtoul(end, &end, 10) != 0;
        SerialDMSeq.print("H PROTO ");
        SerialDMSeq.println(usb.sendSetProtocol(itf, any_address(), boot) ? 1 : 0);
    } else if (strcmp(cmd, "layout") == 0) {
        usb.setKeyboardLayout(strcmp(arg, "jp") == 0 ? CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP
                                                     : CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_US);
        SerialDMSeq.println("H LAYOUT");
    } else if (strcmp(cmd, "locks") == 0) {
        SerialDMSeq.print("H LOCKS ");
        SerialDMSeq.print(usb.getKeyboardNumLock() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(usb.getKeyboardCapsLock() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(usb.getKeyboardScrollLock() ? 1 : 0);
    } else if (strcmp(cmd, "hiddesc") == 0) {
        for (uint8_t i = 0; i < hidDescCount; i++) {
            SerialDMSeq.print("H HIDDESC ");
            SerialDMSeq.print(hidDescItf[i]);
            SerialDMSeq.print(' ');
            SerialDMSeq.println(hidDescLength[i]);
        }
        SerialDMSeq.println("H HIDDESCEND");
    } else if (strcmp(cmd, "hidraw") == 0) {
        hidRaw = strtoul(arg, nullptr, 10) != 0;
        SerialDMSeq.print("H HIDRAW ");
        SerialDMSeq.println(hidRaw ? 1 : 0);
    } else if (strcmp(cmd, "mscmaxlun") == 0) {
        uint8_t lun = 0;
        const bool ok = usb.mscMaxLun(lun);
        SerialDMSeq.print("H MSCMAXLUN ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(lun);
    } else if (strcmp(cmd, "mscrd") == 0) {
        const bool ok = usb.mscReadBlocks(strtoul(arg, nullptr, 10), block, 1);
        CH32RVUsbHostMscSense sense;
        if (!ok) {
            usb.mscRequestSense(sense);
        }
        SerialDMSeq.print("H MSCRD ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        hex(&sense.senseKey, 1);
        SerialDMSeq.print(' ');
        hex(&sense.additionalSenseCode, 1);
        SerialDMSeq.print(' ');
        hex(&sense.additionalSenseQualifier, 1);
        SerialDMSeq.println();
    } else if (strcmp(cmd, "msclun") == 0) {
        SerialDMSeq.print("H MSCLUN ");
        SerialDMSeq.println(usb.mscSelectLun((uint8_t)strtoul(arg, nullptr, 10)) ? 1 : 0);
    } else if (strcmp(cmd, "msctur") == 0) {
        const bool ok = usb.mscTestUnitReady(CH32RV_USB_HOST_ANY_ADDRESS, 2000);
        CH32RVUsbHostMscSense sense;
        if (!ok) {
            usb.mscRequestSense(sense);
        }
        SerialDMSeq.print("H MSCTUR ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        hex(&sense.senseKey, 1);
        SerialDMSeq.print(' ');
        hex(&sense.additionalSenseCode, 1);
        SerialDMSeq.print(' ');
        hex(&sense.additionalSenseQualifier, 1);
        SerialDMSeq.println();
    } else if (strcmp(cmd, "midicc") == 0 || strcmp(cmd, "midipc") == 0 || strcmp(cmd, "midipb") == 0) {
        char *end;
        const uint8_t ch = (uint8_t)strtoul(arg, &end, 10);
        const long a = strtol(end, &end, 10);
        const long b = strtol(end, &end, 10);
        bool ok;
        if (strcmp(cmd, "midicc") == 0) {
            ok = usb.midiSendControlChange(ch, (uint8_t)a, (uint8_t)b);
        } else if (strcmp(cmd, "midipc") == 0) {
            ok = usb.midiSendProgramChange(ch, (uint8_t)a);
        } else {
            ok = usb.midiSendPitchBend(ch, (uint16_t)a);
        }
        SerialDMSeq.print("H MIDIOUT ");
        SerialDMSeq.println(ok ? 1 : 0);
    } else if (strcmp(cmd, "midisx") == 0) {
        uint8_t d[32];
        const size_t n = parse_hex(arg, d, sizeof d);
        SerialDMSeq.print("H MIDIOUT ");
        SerialDMSeq.println(usb.midiSendSysEx(d, n) ? 1 : 0);
    } else if (strcmp(cmd, "vreqout") == 0) {
        char *end;
        const uint8_t req = (uint8_t)strtoul(arg, &end, 0);
        const uint16_t value = (uint16_t)strtoul(end, &end, 0);
        const uint16_t index = (uint16_t)strtoul(end, &end, 0);
        uint8_t d[32];
        const size_t n = parse_hex(*end ? end + 1 : end, d, sizeof d);
        SerialDMSeq.print("H VREQOUT ");
        SerialDMSeq.println(usb.vendorControlOut(req, value, index, d, n) ? 1 : 0);
    } else if (strcmp(cmd, "ccidstatus") == 0) {
        CH32RVUsbHostCcidStatus st;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidGetStatus(st);
        SerialDMSeq.print("H CCIDSTATUS ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned)st.iccStatus);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(st.present ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(st.active ? 1 : 0);
    } else if (strcmp(cmd, "ccidpresent") == 0) {
        SerialDMSeq.print("H CCIDPRESENT ");
        SerialDMSeq.println((usb.ccidReady() || usb.ccidOpen()) && usb.ccidCardPresent() ? 1 : 0);
    } else if (strcmp(cmd, "ccidoff") == 0) {
        SerialDMSeq.print("H CCIDOFF ");
        SerialDMSeq.println(usb.ccidPowerOff() ? 1 : 0);
    } else if (strcmp(cmd, "ccidesc") == 0) {
        uint8_t tx[32], rx[40];
        const size_t n = parse_hex(arg, tx, sizeof tx);
        size_t got = 0;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidEscape(tx, n, rx, sizeof rx, &got);
        SerialDMSeq.print("H CCIDESC ");
        SerialDMSeq.print(ok ? 1 : 0);
        SerialDMSeq.print(' ');
        hex(rx, ok ? got : 0);
        SerialDMSeq.println();
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    SerialDMSeq.println(CH32RV.resetReasonName());
    usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) {
        SerialDMSeq.print("H CONNECTED ");
        SerialDMSeq.print(d.address);
        SerialDMSeq.print(' ');
        print4(d.vid);
        SerialDMSeq.print(' ');
        print4(d.pid);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(d.product);
    });
    usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &d) {
        SerialDMSeq.print("H DISCONNECTED ");
        SerialDMSeq.print(d.address);
        SerialDMSeq.print(' ');
        print4(d.vid);
        SerialDMSeq.print(' ');
        print4(d.pid);
        SerialDMSeq.println();
    });
    usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
        SerialDMSeq.print("H KEY ");
        SerialDMSeq.print(e.pressed ? 'p' : 'r');
        SerialDMSeq.print(' ');
        SerialDMSeq.print(e.keycode);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(e.ascii);
    });
    usb.onMouse([](const CH32RVUsbHostMouseEvent &e) {
        SerialDMSeq.print("H MOUSE ");
        SerialDMSeq.print(e.x);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(e.y);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(e.wheel);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(e.buttons);
    });
    usb.onConsumerControl([](const CH32RVUsbHostConsumerControlEvent &e) {
        SerialDMSeq.print("H CONSUMER ");
        SerialDMSeq.print(e.usage);
        SerialDMSeq.println(e.pressed ? " p" : " r");
    });
    usb.onHIDVendorInput([](const CH32RVUsbHostHIDVendorInput &e) {
        SerialDMSeq.print("H HIDVIN ");
        hex(e.reportData, e.reportLength);
        SerialDMSeq.println();
    });
    usb.onMidiMessage([](const CH32RVUsbHostMidiMessage &m) {
        SerialDMSeq.print("H MIDI ");
        SerialDMSeq.print(m.codeIndex);
        SerialDMSeq.print(' ');
        hex(&m.status, 1);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(m.data1);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(m.data2);
    });
    usb.onGamepad([](const CH32RVUsbHostGamepadEvent &e) {
        SerialDMSeq.print("H GAMEPAD ");
        hex(e.reportData, e.reportLength);
        SerialDMSeq.println();
    });
    usb.onSystemControl([](const CH32RVUsbHostSystemControlEvent &e) {
        SerialDMSeq.print("H SYSTEM ");
        SerialDMSeq.print(e.usage);
        SerialDMSeq.println(e.pressed ? " p" : " r");
    });
    usb.onHIDInput([](const CH32RVUsbHostHIDInput &e) {
        if (!hidRaw) {
            return;
        }
        SerialDMSeq.print("H HIDIN ");
        SerialDMSeq.print(e.interfaceNumber);
        SerialDMSeq.print(' ');
        hex(e.data, e.length);
        SerialDMSeq.println();
    });
    usb.onHIDReportDescriptor([](const CH32RVUsbHostHIDReportDescriptor &e) {
        for (uint8_t i = 0; i < hidDescCount; i++) {
            if (hidDescItf[i] == e.interfaceNumber) {
                hidDescLength[i] = e.length;
                return;
            }
        }
        if (hidDescCount < sizeof hidDescItf) {
            hidDescItf[hidDescCount] = e.interfaceNumber;
            hidDescLength[hidDescCount++] = e.length;
        }
    });
    usb.onSerialData([](const CH32RVUsbHostSerialData &e) {
        const uint8_t port = e.port & 1;
        for (size_t i = 0; i < e.length && serialRxLength[port] < sizeof serialRx[port]; i++) {
            serialRx[port][serialRxLength[port]++] = e.data[i];
        }
    });
    usb.onCcidCardInserted([](const CH32RVUsbHostCcidSlotEvent &) { SerialDMSeq.println("H CARD in"); });
    usb.onCcidCardRemoved([](const CH32RVUsbHostCcidSlotEvent &) { SerialDMSeq.println("H CARD out"); });
    cdc.begin(115200);
    began = usb.begin();
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            if (used) {
                reply(line);
            }
            used = 0;
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
