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
 *
 * Events: H CONNECTED <addr> <vid> <pid> <product> / H DISCONNECTED <addr>
 * <vid> <pid> / H KEY <p|r> <keycode> <ascii> / H MOUSE <x> <y> <wheel>
 * <buttons> / H CONSUMER <usage> <p|r> / H HIDVIN <hex> / H MIDI <cin>
 * <status> <d1> <d2>. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbHost.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial cdc(usb);

static bool began;
static char line[160];
static uint8_t used;
static uint8_t block[512], keep[512];

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
        while (got < want && cdc.available()) out[got++] = (uint8_t)cdc.read();
        delay(1);
    }
    return got;
}

static void cmd_cdc(const char *arg)
{
    uint8_t tx[64], rx[64];
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
    uint8_t tx[64], rx[64];
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
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
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
