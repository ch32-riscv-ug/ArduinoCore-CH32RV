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
 *   net                  C NETOPEN <ok>      (networkOpen(): the DUT's CDC-NCM function)
 *   netsend              C NETSENT <ok>      (broadcast, EtherType 0x88B5)
 *   netstat              C NETRX <frames> <frames of 0x88B5>   (since the last reset)
 *   ipping <a.b.c.d>     from 192.168.7.2 (MAC 02:00:00:00:43:48): an ARP request, then an ICMP echo to the
 *                        MAC that answered: C IPPING <ARP answered> <echo answered> <the MAC, 12 hex digits>
 *   ipudp <a.b.c.d> <port> <hex>
 *                        a UDP datagram from port 40000 (after ipping): C IPUDP <answered> <payload, hex>
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
 *   audioin <ms> [rate]  C AUDIOIN <bytes> <ramp breaks>     (rate: 32000 by default)
 *   audioout <ms> [rate] C AUDIOOUT <ok> <bytes> <byte sum>  (a 16-bit ramp, exactly ms of it)
 *   video <ms>           C VIDEO <frames> <pattern errors> <width> <height>   (YUY2 80x60)
 *                        (these three only in the PEER_MEDIA build, case usb-peer-media)
 *   reset                C RESET
 *   cdcports             C CDCPORTS <CDC ports of the device>
 *   cdcn <idx> <hex>     C CDCN <idx> <hex back within 2 s>   (any CDC port by its index)
 *   cdcline <idx> <baud> <bits> <parity> <stop>   C CDCLINE <ok>   (SET_LINE_CODING)
 *   cdcctl <idx> <dtr> <rts>                      C CDCCTL <ok>    (SET_CONTROL_LINE_STATE)
 *   fwclr / fwput <at> <hex>   C FWCLR <buffer bytes> / C FWPUT <ok> <length>   (the image to hand over)
 *   dfuinfo              C DFUINFO <DFU-mode interface or -1> <wTransferSize>
 *   dfudn <n> <at> <len> / dfust / dfuclr / dfuabort   C DFUDN|DFUST|DFUCLR|DFUABORT <ok> <bStatus> <bState>
 *   dfuall               C DFUALL <ok> <blocks> <bStatus> <bState>   (the whole buffer, then the empty DNLOAD)
 *   uf2w <lba> <family hex> <count> <reverse>   C UF2W <ok> <written> <total>
 *   rawall <lba>         C RAWALL <ok>       (the buffer as sectors)
 *   mscw <lba> <at> <n>  C MSCW <ok> <CH32RVUsbHostError>   (n sectors of the buffer from at)
 *   mscej                C MSCEJ <ok> <CH32RVUsbHostError>   (mscEject())
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

/* ------------------------------------------------------------ IPv4 over the NCM link (ipping, ipudp),
 * the Pico peer's commands on networkWriteFrame() / onNetworkFrame() */
static const uint8_t ipMac[6] = {0x02, 0x00, 0x00, 0x00, 0x43, 0x48};
static const uint8_t ipOwn[4] = {192, 168, 7, 2};
static uint8_t ipPeer[4], ipPeerMac[6];
static volatile bool ipArpGot, ipEchoGot, ipUdpGot;
static uint8_t ipUdpData[64];
static volatile uint8_t ipUdpLength;

static uint16_t ipSum(const uint8_t *p, size_t n)
{
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < n; i += 2) sum += (uint32_t)(p[i] << 8 | p[i + 1]);
    if (n & 1) sum += (uint32_t)(p[n - 1] << 8);
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void ipSeen(const uint8_t *f, size_t n)
{
    if (n >= 42 && f[12] == 0x08 && f[13] == 0x06 && f[21] == 2 && !memcmp(f + 28, ipPeer, 4)) {
        memcpy(ipPeerMac, f + 22, 6);
        ipArpGot = true;
    }
    if (n >= 34 && f[12] == 0x08 && f[13] == 0x00 && !memcmp(f + 26, ipPeer, 4)) {
        const uint8_t *ip = f + 14;
        const unsigned hl = (ip[0] & 15u) * 4u;
        const uint8_t *l4 = ip + hl;
        if (ip[9] == 1 && n >= 14 + hl + 8 && l4[0] == 0 && l4[4] == 0x43 && l4[5] == 0x48) ipEchoGot = true;
        if (ip[9] == 17 && n >= 14 + hl + 8 && l4[2] == (40000 >> 8) && l4[3] == (40000 & 255)) {
            unsigned len = (unsigned)(l4[4] << 8 | l4[5]);
            len = len >= 8 ? len - 8 : 0;
            if (len > sizeof ipUdpData) len = sizeof ipUdpData;
            memcpy(ipUdpData, l4 + 8, len);
            ipUdpLength = (uint8_t)len;
            ipUdpGot = true;
        }
    }
}

static bool ipWait(volatile bool &flag, uint32_t ms)
{
    const uint32_t t0 = millis();
    while (!flag && millis() - t0 < ms) {
        deliverEvents();
        delay(1);
    }
    return flag;
}

static bool ipParse(const char *&arg, uint8_t out[4])
{
    unsigned a, b, c, d;
    int used = 0;
    if (sscanf(arg, "%u.%u.%u.%u%n", &a, &b, &c, &d, &used) != 4) return false;
    out[0] = (uint8_t)a; out[1] = (uint8_t)b; out[2] = (uint8_t)c; out[3] = (uint8_t)d;
    arg += used;
    return true;
}

static void ipHeader(uint8_t *f, uint8_t proto, uint16_t payload)
{
    memcpy(f, ipPeerMac, 6);
    memcpy(f + 6, ipMac, 6);
    f[12] = 0x08; f[13] = 0x00;
    uint8_t *ip = f + 14;
    const uint16_t total = (uint16_t)(20 + payload);
    memset(ip, 0, 20);
    ip[0] = 0x45; ip[2] = (uint8_t)(total >> 8); ip[3] = (uint8_t)total; ip[8] = 64; ip[9] = proto;
    memcpy(ip + 12, ipOwn, 4);
    memcpy(ip + 16, ipPeer, 4);
    const uint16_t sum = ipSum(ip, 20);
    ip[10] = (uint8_t)(sum >> 8); ip[11] = (uint8_t)sum;
}

static void ipSend(const uint8_t *f, size_t n)
{
    uint8_t padded[128] = {0};
    memcpy(padded, f, n);
    usb.networkWriteFrame(padded, n < 60 ? 60 : n);
}

static void cmdIpPing(const char *arg)
{
    uint8_t f[14 + 28 + 32];
    if (!ipParse(arg, ipPeer)) { say("C IPPING 0 0 000000000000\n"); return; }
    ipArpGot = ipEchoGot = false;
    memset(f, 0xFF, 6);
    memcpy(f + 6, ipMac, 6);
    f[12] = 0x08; f[13] = 0x06;
    const uint8_t arp[8] = {0, 1, 8, 0, 6, 4, 0, 1};
    memcpy(f + 14, arp, 8);
    memcpy(f + 22, ipMac, 6);
    memcpy(f + 28, ipOwn, 4);
    memset(f + 32, 0, 6);
    memcpy(f + 38, ipPeer, 4);
    for (int i = 0; i < 3 && !ipArpGot; i++) {
        ipSend(f, 42);
        ipWait(ipArpGot, 500);
    }
    if (ipArpGot) {
        uint8_t *icmp = f + 34;
        memset(icmp, 0, 24);
        icmp[0] = 8; icmp[4] = 0x43; icmp[5] = 0x48; icmp[7] = 1;
        memcpy(icmp + 8, "CH32 peer ping..", 16);
        const uint16_t sum = ipSum(icmp, 24);
        icmp[2] = (uint8_t)(sum >> 8); icmp[3] = (uint8_t)sum;
        ipHeader(f, 1, 24);
        for (int i = 0; i < 3 && !ipEchoGot; i++) {
            ipSend(f, 14 + 20 + 24);
            ipWait(ipEchoGot, 500);
        }
    }
    say("C IPPING %u %u %02x%02x%02x%02x%02x%02x\n", ipArpGot ? 1 : 0, ipEchoGot ? 1 : 0, ipPeerMac[0], ipPeerMac[1],
        ipPeerMac[2], ipPeerMac[3], ipPeerMac[4], ipPeerMac[5]);
}

static void cmdIpUdp(const char *arg)
{
    uint8_t f[14 + 20 + 8 + 64];
    unsigned port = 0;
    int used = 0;
    uint8_t data[64];
    unsigned n = 0;
    if (!ipParse(arg, ipPeer) || sscanf(arg, "%u%n", &port, &used) != 1) { say("C IPUDP 0 -\n"); return; }
    arg += used;
    while (*arg == ' ') arg++;
    for (; arg[0] && arg[1] && n < sizeof data; arg += 2) {
        unsigned v;
        if (sscanf(arg, "%2x", &v) != 1) break;
        data[n++] = (uint8_t)v;
    }
    uint8_t *udp = f + 34;
    udp[0] = (uint8_t)(40000 >> 8); udp[1] = (uint8_t)(40000 & 255);
    udp[2] = (uint8_t)(port >> 8); udp[3] = (uint8_t)port;
    udp[4] = 0; udp[5] = (uint8_t)(8 + n);
    udp[6] = udp[7] = 0;
    memcpy(udp + 8, data, n);
    ipHeader(f, 17, (uint16_t)(8 + n));
    ipUdpGot = false;
    for (int i = 0; i < 3 && !ipUdpGot; i++) {
        ipSend(f, 14 + 20 + 8 + n);
        ipWait(ipUdpGot, 500);
    }
    say("C IPUDP %u ", ipUdpGot ? 1 : 0);
    if (ipUdpGot && ipUdpLength) {
        for (unsigned i = 0; i < ipUdpLength; i++) say("%02x", ipUdpData[i]);
    } else {
        say("-");
    }
    say("\n");
}

/* ------------------------------------------------------------ the device */
static Device dev;
static volatile uint8_t connectedAddress;   /* set by onDeviceConnected, announced from loop() */
static bool hidHasId[16];                   /* per interface: its report descriptor declares report IDs */
static volatile uint32_t hidVendorInputs;
static volatile uint32_t netFrames, netOurs;
#if defined(PEER_MEDIA)   /* run_hardware.py: case usb-peer-media only (the 64 KB parts' flash) */
static volatile uint32_t audioBytes, audioBreaks, videoFrames, videoErrors;
static volatile int32_t audioLast = -1;
static volatile uint16_t videoW, videoH;

/* Waits that keep the callbacks coming (deliverEvents()). */
static void waitMs(uint32_t ms)
{
    const uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        deliverEvents();
        delay(1);
    }
}

static void cmdAudioIn(uint32_t ms, uint32_t rate)
{
    audioBytes = audioBreaks = 0;
    audioLast = -1;
    if (!usb.audioInputStart(1, 16, rate)) {
        say("C AUDIOIN 0 0 %s\n", usb.lastErrorName());
        return;
    }
    waitMs(ms);
    usb.audioInputStop();
    say("C AUDIOIN %lu %lu\n", (unsigned long)audioBytes, (unsigned long)audioBreaks);
}

/* Exactly ms of a ramp, queued as room allows; the output sends silence around it. */
static void cmdAudioOut(uint32_t ms, uint32_t rate)
{
    uint32_t left = rate * ms / 1000u, given = 0, sum = 0;
    uint16_t sample = 0;
    if (!usb.audioOutputStart(1, 16, rate)) {
        say("C AUDIOOUT 0 0 0 %s\n", usb.lastErrorName());
        return;
    }
    const uint32_t t0 = millis();
    while (left && millis() - t0 < ms + 3000) {
        uint8_t block[64];
        size_t n = 0;
        while (n + 2 <= sizeof block && left && usb.audioOutputAvailableForWrite() >= n + 2) {
            block[n++] = (uint8_t)sample;
            block[n++] = (uint8_t)(sample >> 8);
            sum += (uint8_t)sample + (uint8_t)(sample >> 8);
            sample++;
            left--;
            given++;
        }
        if (n) usb.audioSend(block, n);
        waitMs(1);
    }
    waitMs(400);                   /* the queued packets leave */
    usb.audioOutputStop();
    say("C AUDIOOUT %u %lu %lu\n", left ? 0 : 1, (unsigned long)(given * 2u), (unsigned long)sum);
}

static void cmdVideo(uint32_t ms)
{
    videoFrames = videoErrors = 0;
    if (!usb.videoStart(CH32RV_USB_HOST_VIDEO_FORMAT_YUY2, 80, 60, 0)) {
        say("C VIDEO 0 0 0 0 %s\n", usb.lastErrorName());
        return;
    }
    waitMs(ms);
    usb.videoStop();
    say("C VIDEO %lu %lu %u %u\n", (unsigned long)videoFrames, (unsigned long)videoErrors, videoW, videoH);
}
#else
static void cmdAudioIn(uint32_t, uint32_t) { say("C AUDIOIN 0 0 no-media-build\n"); }
static void cmdAudioOut(uint32_t, uint32_t) { say("C AUDIOOUT 0 0 0 no-media-build\n"); }
static void cmdVideo(uint32_t) { say("C VIDEO 0 0 0 0 no-media-build\n"); }
#endif

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

/* ------------------------------------------------------------ firmware update (usb-peer-firmware)
 * The test host sends an image in hex lines (fwclr, fwput) and this board hands it to the DUT: by DFU 1.1 on its
 * DFU-mode interface (class 0xFE / 1 / protocol 2), or as sectors on its drive (WRITE(10)), the raw drive ended by
 * an eject (mscEject(): START STOP UNIT, LoEj). The buffer is what the board's RAM allows (96 KiB on H41x / V4x7 /
 * X3x5, 32 KiB on V30x / V205 / V208, none to speak of below); fwclr answers its size. */
#if defined(CH32RV_SERIES_CH32H417) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H415) || \
    defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || defined(CH32RV_SERIES_CH32X305) ||  \
    defined(CH32RV_SERIES_CH32X315)
static uint8_t fwImage[96 * 1024];
#elif defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32V208)
static uint8_t fwImage[32 * 1024];
#else
static uint8_t fwImage[512];   /* 20 KB of RAM or less: no image fits, fwclr says so and the test skips */
#endif
static uint32_t fwLength;

static void cmdFwPut(const char *arg)
{
    char *hex = nullptr;
    const uint32_t at = strtoul(arg, &hex, 10);
    while (hex && *hex == ' ') hex++;
    const size_t n = at < sizeof fwImage ? parseHex(hex ? hex : "", fwImage + at, sizeof fwImage - at) : 0;
    if (n && at + n > fwLength) fwLength = at + n;
    say("C FWPUT %u %lu\n", n ? 1 : 0, (unsigned long)fwLength);
}

static bool control(uint8_t type, uint8_t request, uint16_t value, uint16_t index, uint8_t *data, size_t length,
                    size_t *actual = nullptr, uint32_t timeout = 5000)
{
    return usb.vendorControlTransfer(type, request, value, index, data, length, actual, dev.address, timeout);
}

/* The DFU-mode interface and its functional descriptor's wTransferSize, from the configuration descriptor. */
static int dfuMode(uint16_t *transfer)
{
    static uint8_t cfg[512];
    size_t got = 0;
    *transfer = 0;
    if (!control(0x80, 0x06, 0x0200, 0, cfg, 9, &got) || got < 4) return -1;
    size_t total = (size_t)(cfg[2] | cfg[3] << 8);
    if (total > sizeof cfg) total = sizeof cfg;
    if (!control(0x80, 0x06, 0x0200, 0, cfg, total, &got)) return -1;
    int found = -1;
    bool in = false;
    for (size_t at = 0; at + 2 <= got && cfg[at]; at += cfg[at]) {
        const uint8_t *d = cfg + at;
        if (d[1] == 0x04 && d[0] >= 9) {
            in = d[3] == 0 && d[5] == 0xFE && d[6] == 0x01 && d[7] == 0x02;
            if (in) found = d[2];
        } else if (in && d[1] == 0x21 && d[0] >= 7) {
            *transfer = (uint16_t)(d[5] | d[6] << 8);
            break;
        }
    }
    return found;
}

/* GETSTATUS: bStatus, bState; waits the bwPollTimeout it asks for. false when the request failed. */
static bool dfuStatus(int itf, uint8_t *status, uint8_t *state)
{
    static uint8_t st[6];
    size_t actual = 0;
    if (!control(0xA1, 0x03, 0, (uint16_t)itf, st, 6, &actual) || actual != 6) return false;
    *status = st[0];
    *state = st[4];
    pause((uint32_t)st[1] | (uint32_t)st[2] << 8 | (uint32_t)st[3] << 16);
    return true;
}

static void cmdDfuInfo()
{
    uint16_t transfer = 0;
    const int itf = dfuMode(&transfer);
    say("C DFUINFO %d %u\n", itf, transfer);
}

static void cmdDfuDn(const char *arg)
{
    unsigned block = 0;
    unsigned long at = 0, len = 0;
    sscanf(arg, "%u %lu %lu", &block, &at, &len);
    uint16_t transfer = 0;
    const int itf = dfuMode(&transfer);
    uint8_t status = 0xFF, state = 0xFF;
    bool ok = itf >= 0 && at + len <= sizeof fwImage &&
              control(0x21, 0x01, (uint16_t)block, (uint16_t)itf, len ? fwImage + at : nullptr, len);
    ok = ok && dfuStatus(itf, &status, &state);
    say("C DFUDN %u %u %u\n", ok ? 1 : 0, status, state);
}

static void cmdDfuSimple(const char *what)
{
    uint16_t transfer = 0;
    const int itf = dfuMode(&transfer);
    uint8_t status = 0xFF, state = 0xFF;
    bool ok = itf >= 0;
    if (ok && !strcmp(what, "st")) ok = dfuStatus(itf, &status, &state);
    else if (ok && !strcmp(what, "clr")) ok = control(0x21, 0x04, 0, (uint16_t)itf, nullptr, 0);
    else if (ok) ok = control(0x21, 0x06, 0, (uint16_t)itf, nullptr, 0);   /* abort */
    say("C DFU%s %u %u %u\n", !strcmp(what, "st") ? "ST" : !strcmp(what, "clr") ? "CLR" : "ABORT", ok ? 1 : 0,
        status, state);
}

/* The whole buffer by DFU: wTransferSize blocks, then the zero-length DNLOAD; stops at the first block whose
 * GETSTATUS is not OK / dfuDNLOAD-IDLE. */
static void cmdDfuAll()
{
    uint16_t transfer = 0;
    const int itf = dfuMode(&transfer);
    uint8_t status = 0xFF, state = 0xFF;
    unsigned blocks = 0;
    bool ok = itf >= 0 && transfer;
    for (uint32_t at = 0; ok && at < fwLength; at += transfer, blocks++) {
        const uint32_t n = fwLength - at < transfer ? fwLength - at : transfer;
        ok = control(0x21, 0x01, (uint16_t)blocks, (uint16_t)itf, fwImage + at, n) &&
             dfuStatus(itf, &status, &state) && status == 0 && state == 5;
    }
    if (ok) ok = control(0x21, 0x01, (uint16_t)blocks, (uint16_t)itf, nullptr, 0) && dfuStatus(itf, &status, &state);
    say("C DFUALL %u %u %u %u\n", ok ? 1 : 0, blocks, status, state);
}

/* UF2 blocks made here from the buffer's raw image (uf2conv.py's layout: 256 bytes a block at 0x08000000 + n x
 * 256, the family in the header, the last padded with 0xFF), written one a sector from `lba`: `count` of them (0:
 * all), last first when `reverse`. Stops at the first refused one. */
static void cmdUf2W(const char *arg)
{
    unsigned long lba = 0, family = 0, count = 0, reverse = 0;
    sscanf(arg, "%lu %lx %lu %lu", &lba, &family, &count, &reverse);
    const uint32_t total = (fwLength + 255u) / 256u;
    if (!count || count > total) count = total;
    static uint8_t b[512];
    unsigned written = 0;
    bool ok = total > 0;
    for (uint32_t k = 0; ok && k < count; k++) {
        const uint32_t n = reverse ? total - 1u - k : k;
        memset(b, 0, sizeof b);
        const uint32_t head[8] = {0x0A324655u, 0x9E5D5157u, 0x00002000u, 0x08000000u + n * 256u, 256u, n, total,
                                  (uint32_t)family};
        memcpy(b, head, sizeof head);
        memset(b + 32, 0xFF, 256);
        const uint32_t at = n * 256u, left = fwLength - at;
        memcpy(b + 32, fwImage + at, left < 256u ? left : 256u);
        const uint32_t tail = 0x0AB16F30u;
        memcpy(b + 508, &tail, 4);
        ok = usb.mscWriteBlocks((uint32_t)(lba + k), b, 1, dev.address);
        if (ok) written++;
    }
    say("C UF2W %u %u %lu\n", ok ? 1 : 0, written, (unsigned long)total);
}

/* `count` sectors of the buffer from `at`, to the drive at `lba`; the answer's second number is the library's
 * error (peer_pico's is the CSW status). */
static void cmdMscW(const char *arg)
{
    unsigned long lba = 0, at = 0, count = 0;
    sscanf(arg, "%lu %lu %lu", &lba, &at, &count);
    const bool fits = count && at + count * 512u <= sizeof fwImage;
    const bool ok = fits && usb.mscWriteBlocks((uint32_t)lba, fwImage + at, (uint32_t)count, dev.address);
    say("C MSCW %u %u\n", ok ? 1 : 0, (unsigned)usb.lastError());
}

/* The buffer as it is, padded to whole sectors with 0xFF, from `lba` in 4 KiB writes. */
static void cmdRawAll(const char *arg)
{
    const uint32_t lba = strtoul(arg, nullptr, 10);
    const uint32_t length = (fwLength + 511u) & ~511u;
    bool ok = length && length <= sizeof fwImage;
    if (ok) memset(fwImage + fwLength, 0xFF, length - fwLength);
    for (uint32_t at = 0; ok && at < length; at += 4096u) {
        const uint32_t n = length - at < 4096u ? length - at : 4096u;
        ok = usb.mscWriteBlocks(lba + at / 512u, fwImage + at, n / 512u, dev.address);
    }
    say("C RAWALL %u\n", ok ? 1 : 0);
}

/* ------------------------------------------------------------ commands */
static char line[1200];   /* fwput: 512 bytes of hex a line */
static size_t used;

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "ping")) SerialDMSeq.println("C PONG");
    else if (!strcmp(cmd, "fwclr")) {
        fwLength = 0;
        say("C FWCLR %u\n", (unsigned)sizeof fwImage);
    } else if (!strcmp(cmd, "fwput")) cmdFwPut(arg);
    else if (!strcmp(cmd, "dfuinfo")) cmdDfuInfo();
    else if (!strcmp(cmd, "dfudn")) cmdDfuDn(arg);
    else if (!strcmp(cmd, "dfust")) cmdDfuSimple("st");
    else if (!strcmp(cmd, "dfuclr")) cmdDfuSimple("clr");
    else if (!strcmp(cmd, "dfuabort")) cmdDfuSimple("abort");
    else if (!strcmp(cmd, "dfuall")) cmdDfuAll();
    else if (!strcmp(cmd, "uf2w")) cmdUf2W(arg);
    else if (!strcmp(cmd, "rawall")) cmdRawAll(arg);
    else if (!strcmp(cmd, "mscw")) cmdMscW(arg);
    else if (!strcmp(cmd, "mscej")) say("C MSCEJ %u %u\n", usb.mscEject(dev.address) ? 1 : 0, (unsigned)usb.lastError());
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
        say("C NETOPEN %u\n", usb.networkOpen() ? 1 : 0);
    } else if (!strcmp(cmd, "netsend")) {
        uint8_t f[60] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x00, 0x00, 0x00, 0x43, 0x48, 0x88, 0xB5};
        memcpy(f + 14, "CH32RV peer frame", 17);
        say("C NETSENT %u\n", usb.networkWriteFrame(f, sizeof f) ? 1 : 0);
    } else if (!strcmp(cmd, "netstat")) {
        say("C NETRX %lu %lu\n", (unsigned long)netFrames, (unsigned long)netOurs);
    } else if (!strcmp(cmd, "ipping")) {
        cmdIpPing(arg);
    } else if (!strcmp(cmd, "ipudp")) {
        cmdIpUdp(arg);
    }
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
    else if (!strcmp(cmd, "audioin") || !strcmp(cmd, "audioout")) {
        char *rest = nullptr;
        const uint32_t ms = strtoul(arg, &rest, 10);
        const uint32_t rate = strtoul(rest, nullptr, 10);
        if (!strcmp(cmd, "audioin")) cmdAudioIn(ms, rate ? rate : 32000);
        else cmdAudioOut(ms, rate ? rate : 32000);
    } else if (!strcmp(cmd, "video")) cmdVideo(strtoul(arg, nullptr, 10));
    else if (!strcmp(cmd, "reset")) {
        netFrames = netOurs = 0;
        SerialDMSeq.println("C RESET");
    }
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
#if defined(PEER_MEDIA)
    usb.onAudioData([](const CH32RVUsbHostAudioData &d) {
        for (size_t i = 0; i + 1 < d.length; i += 2) {
            const uint16_t s = (uint16_t)(d.data[i] | d.data[i + 1] << 8);
            /* The first 10 ms may still hold what the device queued before; judge from there (peer_s3's rule). */
            if (audioLast >= 0 && s != (uint16_t)(audioLast + 1) && audioBytes >= 640) audioBreaks++;
            audioLast = s;
        }
        audioBytes += d.length;
    });
    usb.onVideoFrame([](const CH32RVUsbHostVideoFrame &f) {
        videoFrames++;
        videoW = f.width;
        videoH = f.height;
        for (size_t i = 1; i < f.length; i++) {
            if ((uint8_t)(f.data[i] - f.data[0]) != (uint8_t)(i * 7)) {
                videoErrors++;
                break;
            }
        }
    });
#endif
    usb.onNetworkFrame([](const CH32RVUsbHostNetworkFrame &f) {
        netFrames++;
        if (f.length >= 14 && f.data[12] == 0x88 && f.data[13] == 0xB5) {
            netOurs++;
        }
        ipSeen(f.data, f.length);
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
