/* USB host contracts (tests/peer/usb/host): the DUT runs CH32RVUsbHost on its
 * own USB port and an ESP32-S3 (EspUsbDevice) is the device. Commands arrive on
 * the debug-probe console (DMSEQ), every answer and event is a line starting
 * "H ":
 *
 *   ready <nonce>        READY <nonce> <build id> <begin ok 0/1> <last error>
 *   listen <1 / 0>       add*Listener() for the keyboard, the gamepad, system control, connections and
 *                        disconnections beside their on*() (events as H LKEY / H LGAMEPAD / H LSYSTEM /
 *                        H LCONN / H LDISC), or removeListener() for each: H LISTEN <listeners now> <ok>
 *   onkey2               onKeyboard() again, printing H KEY2 (after listen: through the listeners' table)
 *   dev                  H DEV ... / H ITF ... per interface / H DEVEND <n>
 *   cdc <hex>            H CDC <hex echoed back within 2 s>
 *   cdcbig <n>           H CDCBIG <n> <ok> <bytes back>
 *   cdcrx <size> <n>     setRxBufferSize(size), n bytes out unread (up to 2 s for them):
 *                        H CDCRX <set 1/0> <rxBufferSize()> <available()> <bytes read> <wrong bytes> <sent>
 *   baud <n>             H BAUD <ok>
 *   hidv <hex>           H HIDV_SENT <ok>      (vendor output report, no ID)
 *   leds <bits>          H LEDS <ok>
 *   midi <ch> <n> <v>    H MIDI_SENT <ok>
 *   msc                  H MSC <ok> <vendor> <product> <blocks> <block size>
 *   mscraw <lba>         H MSCRAW <ok>         (pattern written, read back, restored)
 *   msc64 <lba>          H MSC64 <ok> <blocks> <block size> <rw ok>  (mscCapacity64(), then mscraw's pattern
 *                        through mscReadBlocks64() / mscWriteBlocks64())
 *   msclast              H MSCLAST <ok> <sense key> <asc> <ascq>  (mscLastSense(): no request to the drive)
 *   epinfo               H EPINFO <addresses, comma-separated> <vendor OUT ep> <vendor IN ep> <serial OUT packet>
 *                        (getHostDeviceAddresses(), vendorOutEndpoint() / vendorInEndpoint() after vendorOpen(),
 *                        serialOutPacketSize(); hex endpoints)
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
 *   ccidslots            H CCIDSLOTS <slots>   (ccidSlotCount())
 *   ccidmsg <type> <hex> H CCIDMSG <ok> <message type> <bStatus> <bError> <hex>  (ccidMessage(), type in hex)
 *   ccidabort            H CCIDABORT <ok>      (ccidAbort(): the class request, then PC_to_RDR_Abort)
 *   suspend              H SUSPEND <ok> <last error>   (suspend(): the root port, remote wakeup armed)
 *   resume               H RESUME <ok> <last error>
 *   power                H POWER <suspended 0/1> <remote wakeups seen>
 * Built with USB_HOST_FS (case usb-peer-host-fs) only - the FS library is more than the 64 KB parts' other host
 * cases can carry; that build leaves out the commands of the other classes (CDC, HID, MIDI, CCID, vendor,
 * suspend), keeping ready, dev, where, msc, msclun, msctur and these:
 *   fsmount <lun> <fmt>  H FSMOUNT <ok> <FAT type 0..4> <total bytes>   (CH32RVUsbHostMscFS on that LUN of the
 *                        first stick; with fmt 1 a unit that holds no volume is formatted first)
 *   fswrite <name> <hex> H FSWRITE <bytes written>     (FILE_WRITE: the file made anew)
 *   fsread <name>        H FSREAD <ok> <hex>
 *   fsls                 H FSLS <name>:<size> ... (the root's files)
 *   fsrm <name>          H FSRM <ok>
 *   fsend                H FSEND                       (end(): files closed, the cache synchronized)
 *   where <addr>         H WHERE <addr> <hub address> <hub speed> (0 0: on the root port; the hub the device
 *                        hangs from - a high-speed one reaches a full-speed device by split transactions)
 * Built with USB_HOST_NET (case usb-peer-host-net) only, leaving out the other classes' commands as the FS build
 * does (ready, dev and where stay), these - a CDC-NCM / ECM device's raw frames:
 *   netopen              H NETOPEN <ok> <protocol: 1 ECM, 2 NCM> <the device's MAC, 12 hex> <NTB in> <NTB out>
 *   netsend <n>          H NETSENT <frames sent>   (broadcast, EtherType 0x88B5, the index in the payload)
 *   netstat              H NETRX <frames> <frames of 0x88B5> <link 0/1> <dropped> <malformed>
 *   netclose             H NETCLOSE
 *   netip                H NETIP <ok> <address>   (networkAttachNetif(): DHCP from the device, up to 15 s;
 *                        H NETIP 2 - where the Network library does not build)
 *   tcpecho <ip> <port> <hex>   H TCPECHO <ok> <hex back within 3 s>   (NetworkClient over the device)
 * Built with USB_HOST_AUDIO (case usb-peer-host-media) only, the same way - USB audio streams:
 *   audstreams           H AUDSTREAM <in 1/out 0> <channels> <bits> <rate> <rates listed> <itf> <alt> <ep> <mps> <protocol>
 *                        per stream / H AUDEND <n>
 *   audin <ms>           H AUDIN <ok> <bytes> <breaks in the device's counter> (audioInputStart(1, 16, 48000), ms of it)
 *   audout <ms>          H AUDOUT <ok> <bytes queued> <underruns> <feedback 0/1> <rate> (audioOutputStart(1, 16,
 *                        48000), a counter for ms)
 *   audfu                H AUDFU <unit> <source> <channels> <master controls hex> <protocol> per Feature Unit / H AUDFUEND <n>
 *   audmute <0|1>        H AUDMUTE <has> <set ok> <read back>        (the first unit with mute, master channel)
 *   audvol <v>           H AUDVOL <has> <set ok> <read back> <min> <max> <res>   (1/256 dB, the master channel)
 *   vidstreams           H VIDSTREAM <format> <w> <h> <format idx> <frame idx> <interval> <max frame> <max payload>
 *                        <isochronous> per stream / H VIDEND <n>
 *   vidrun <ms>          H VIDRUN <ok> <frames> <complete> <first frame's length> <bytes 0..3 of the last, hex>
 *                        <in sequence> <dropped> <header errors> <packet errors>  (videoStart(YUY2, 160, 120, 15))
 *
 * Events: H CONNECTED <addr> <vid> <pid> <product> / H DISCONNECTED <addr>
 * <vid> <pid> / H KEY <p|r> <keycode> <ascii> / H MOUSE <x> <y> <wheel>
 * <buttons> / H CONSUMER <usage> <p|r> / H HIDVIN <hex> / H MIDI <cin>
 * <status> <d1> <d2> / H GAMEPAD <report hex> / H SYSTEM <usage> <p|r> /
 * H HIDIN <itf> <report hex> (with hidraw 1) / H CARD <in|out> / H WAKEUP <n> (onRemoteWakeup()). */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <CH32RVUsbHost.h>
#if defined(USB_HOST_FS)
#include <CH32RVUsbHostMscFS.h>
#endif
/* The IP stack only where the Network library builds (its lwipopts.h: the series with an Ethernet MAC). */
#if defined(USB_HOST_NET) && (defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_PART_CH32V203RBT6) || \
                              defined(CH32RV_SERIES_CH32V307) || defined(CH32RV_SERIES_CH32V317) || \
                              defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || \
                              defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || \
                              defined(CH32RV_SERIES_CH32H417))
#define USB_HOST_NET_IP 1
#include <Network.h>
#include <NetworkUsbHost.h>
#endif
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbHost usb;
CH32RVUsbHostCdcSerial cdc(usb);

static bool began;
static uint32_t wakeups;          /* onRemoteWakeup() calls */
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
        CONSOLE.print(digits[d[i] >> 4]);
        CONSOLE.print(digits[d[i] & 15]);
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
    for (int s = 12; s >= 0; s -= 4) CONSOLE.print(digits[(v >> s) & 15]);
}

static void cmd_dev()
{
    CH32RVUsbHostDeviceInfo list[4];
    const size_t n = usb.getDevices(list, 4);
    for (size_t i = 0; i < n; i++) {
        const CH32RVUsbHostDeviceInfo &d = list[i];
        CONSOLE.print("H DEV ");
        CONSOLE.print(d.address);
        CONSOLE.print(' ');
        print4(d.vid);
        CONSOLE.print(' ');
        print4(d.pid);
        CONSOLE.print(' ');
        CONSOLE.print(d.speed);
        CONSOLE.print(' ');
        print4(d.usbVersion);
        CONSOLE.print(' ');
        CONSOLE.print(d.deviceClass, HEX);
        CONSOLE.print(' ');
        CONSOLE.print(d.manufacturer);
        CONSOLE.print('|');
        CONSOLE.print(d.product);
        CONSOLE.print('|');
        CONSOLE.println(d.serial);
        CH32RVUsbHostInterfaceInfo itfs[8];
        const size_t ni = usb.getInterfaces(d.address, itfs, 8);
        for (size_t k = 0; k < ni; k++) {
            CONSOLE.print("H ITF ");
            CONSOLE.print(d.address);
            CONSOLE.print(' ');
            CONSOLE.print(itfs[k].number);
            CONSOLE.print(' ');
            CONSOLE.print(itfs[k].alternate);
            CONSOLE.print(' ');
            hex(&itfs[k].interfaceClass, 1);
            CONSOLE.print(' ');
            hex(&itfs[k].interfaceSubClass, 1);
            CONSOLE.print(' ');
            hex(&itfs[k].interfaceProtocol, 1);
            CONSOLE.print(' ');
            CONSOLE.println(itfs[k].claimed ? 1 : 0);
        }
    }
    CONSOLE.print("H DEVEND ");
    CONSOLE.println((unsigned)n);
}

static void cmd_where(const char *arg)
{
    const uint8_t address = (uint8_t)strtoul(arg, nullptr, 10);
    CH32RVUsbHostDeviceInfo d, hub;
    uint8_t parent = 0, speed = 0;
    if (usb.getDevice(address, d) && d.parentAddress && usb.getDevice(d.parentAddress, hub)) {
        parent = d.parentAddress;
        speed = hub.speed;
    }
    CONSOLE.print("H WHERE ");
    CONSOLE.print(address);
    CONSOLE.print(' ');
    CONSOLE.print(parent);
    CONSOLE.print(' ');
    CONSOLE.println(speed);
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
    CONSOLE.print("H CDC ");
    hex(rx, got);
    CONSOLE.println();
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
    CONSOLE.print("H CDCBIG ");
    CONSOLE.print((unsigned)n);
    CONSOLE.print(' ');
    CONSOLE.print(got == n && !bad ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.println((unsigned)got);
}

/* setRxBufferSize(size) between end() and begin(), then n bytes out unread: the echo waits in the ring. */
static void cmd_cdcrx(const char *arg)
{
    char *end = nullptr;
    const size_t size = strtoul(arg, &end, 10);
    size_t n = strtoul(end, nullptr, 10);
    if (n > 1024) n = 1024;
    cdc.end();
    const bool set = cdc.setRxBufferSize(size);
    cdc.begin(115200);
    const uint32_t t0 = millis();
    while (!cdc.connected() && millis() - t0 < 3000) deliverEvents();
    while (cdc.available()) cdc.read();
    size_t sent = 0;
    while (sent < n && millis() - t0 < 6000) {
        uint8_t chunk[32];
        const size_t k = n - sent < sizeof chunk ? n - sent : sizeof chunk;
        for (size_t i = 0; i < k; i++) chunk[i] = (uint8_t)((sent + i) * 7 + 3);
        if (cdc.write(chunk, k) == k) sent += k;
        deliverEvents();
    }
    const uint32_t t1 = millis();
    while ((size_t)cdc.available() < sent && millis() - t1 < 2000) deliverEvents();
    const int waiting = cdc.available();
    size_t got = 0, bad = 0;
    while (cdc.available()) {
        if ((uint8_t)cdc.read() != (uint8_t)(got * 7 + 3)) bad++;
        got++;
    }
    CONSOLE.printf("H CDCRX %d %u %d %u %u %u\n", set ? 1 : 0, (unsigned)cdc.rxBufferSize(), waiting, (unsigned)got,
                       (unsigned)bad, (unsigned)sent);
}

static void cmd_vend(const char *arg)
{
    uint8_t tx[160], rx[160];
    const size_t n = parse_hex(arg, tx, sizeof tx);
    if (!usb.vendorReady() && !usb.vendorOpen()) {
        CONSOLE.print("H VEND ERR ");
        CONSOLE.println(usb.lastErrorName());
        return;
    }
    while (usb.vendorRead(rx, sizeof rx)) {
    }
    if (!usb.vendorWrite(tx, n)) {
        CONSOLE.print("H VEND ERR ");
        CONSOLE.println(usb.lastErrorName());
        return;
    }
    size_t got = 0;
    const uint32_t t0 = millis();
    while (got < n && millis() - t0 < 2000) {
        got += usb.vendorRead(rx + got, n - got);
        delay(1);
    }
    CONSOLE.print("H VEND ");
    hex(rx, got);
    CONSOLE.println();
}

static void cmd_msc()
{
    CH32RVUsbHostMscInquiry inq;
    uint32_t blocks = 0, size = 0;
    const bool ok = usb.mscWaitReady() && usb.mscInquiry(inq) && usb.mscCapacity(blocks, size);
    CONSOLE.print("H MSC ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.print(ok ? inq.vendor : "-");
    CONSOLE.print(' ');
    CONSOLE.print(ok ? inq.product : "-");
    CONSOLE.print(' ');
    CONSOLE.print((unsigned long)blocks);
    CONSOLE.print(' ');
    CONSOLE.println((unsigned long)size);
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
    CONSOLE.print("H MSCRAW ");
    CONSOLE.println(ok ? 1 : 0);
}

static void cmd_msc64(uint32_t lba)
{
    uint64_t blocks = 0;
    uint32_t size = 0;
    const bool cap = usb.mscCapacity64(blocks, size);
    bool ok = usb.mscReadBlocks64(lba, keep, 1);
    for (size_t i = 0; i < 512; i++) block[i] = (uint8_t)(i ^ (lba * 57));
    ok = ok && usb.mscWriteBlocks64(lba, block, 1);
    memset(block, 0, sizeof block);
    ok = ok && usb.mscReadBlocks64(lba, block, 1);
    for (size_t i = 0; ok && i < 512; i++) ok = block[i] == (uint8_t)(i ^ (lba * 57));
    ok = usb.mscWriteBlocks64(lba, keep, 1) && ok;
    CONSOLE.printf("H MSC64 %d %lu %lu %d\r\n", cap ? 1 : 0, (unsigned long)blocks, (unsigned long)size, ok ? 1 : 0);
}

static void cmd_epinfo()
{
    uint8_t a[8];
    const size_t n = usb.getHostDeviceAddresses(a, sizeof a);
    CONSOLE.print("H EPINFO ");
    for (size_t i = 0; i < n; i++) {
        CONSOLE.print(i ? "," : "");
        CONSOLE.print(a[i]);
    }
    if (!usb.vendorReady()) {
        usb.vendorOpen();
    }
    CONSOLE.printf(" %02x %02x %u\r\n", usb.vendorOutEndpoint(), usb.vendorInEndpoint(), usb.serialOutPacketSize());
}

static void cmd_msclast()
{
    CH32RVUsbHostMscSense s;
    const bool ok = usb.mscLastSense(s);
    CONSOLE.printf("H MSCLAST %d %02x %02x %02x\r\n", ok ? 1 : 0, s.senseKey, s.additionalSenseCode,
                   s.additionalSenseQualifier);
}

#if defined(USB_HOST_FS)
static CH32RVUsbHostMscFS stick;

static void cmd_fsmount(char *arg)
{
    const uint8_t lun = (uint8_t)strtoul(arg, &arg, 10);
    const bool fmt = strtoul(arg, &arg, 10) != 0;
    bool ok = stick.begin(usb, "/usb", CH32RV_USB_HOST_ANY_ADDRESS, lun);
    if (!ok && fmt && stick.device().blockCount()) {
        ok = stick.format(stick.device()) && stick.begin(usb, "/usb", CH32RV_USB_HOST_ANY_ADDRESS, lun);
    }
    CONSOLE.print("H FSMOUNT ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.print((unsigned)stick.fatType());
    CONSOLE.print(' ');
    CONSOLE.println((unsigned long)(ok ? stick.totalBytes() : 0));
}

static void cmd_fswrite(char *arg)
{
    char *data = strchr(arg, ' ');
    size_t n = 0;
    if (data) {
        *data++ = 0;
        n = parse_hex(data, block, sizeof block);
    }
    char path[24] = "/";
    strncat(path, arg, sizeof path - 2);
    File f = stick.open(path, FILE_WRITE);
    const size_t wrote = f ? f.write(block, n) : 0;
    f.close();
    CONSOLE.print("H FSWRITE ");
    CONSOLE.println((unsigned long)wrote);
}

static void cmd_fsread(char *arg)
{
    char path[24] = "/";
    strncat(path, arg, sizeof path - 2);
    File f = stick.open(path, FILE_READ);
    const int n = f ? f.read(block, sizeof block) : -1;
    f.close();
    CONSOLE.print("H FSREAD ");
    CONSOLE.print(n >= 0 ? 1 : 0);
    CONSOLE.print(' ');
    hex(block, n > 0 ? (size_t)n : 0);
    CONSOLE.println();
}

static void cmd_fsls()
{
    CONSOLE.print("H FSLS");
    File root = stick.open("/");
    for (File e = root.openNextFile(); e; e = root.openNextFile()) {
        CONSOLE.print(' ');
        CONSOLE.print(e.name());
        CONSOLE.print(':');
        CONSOLE.print((unsigned long)e.size());
        e.close();
    }
    root.close();
    CONSOLE.println();
}
#endif

#if defined(USB_HOST_NET)
static volatile uint32_t netFrames, netOurs;

static void cmd_netopen()
{
    const bool ok = usb.networkOpen();
    CH32RVUsbHostNetworkInterfaceInfo info;
    CH32RVUsbHostNetworkStats s;
    usb.getNetworkInterface(info);
    usb.networkStats(s);
    netFrames = netOurs = 0;
    CONSOLE.print("H NETOPEN ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.print((unsigned)info.protocol);
    CONSOLE.print(' ');
    hex(info.macAddress, 6);
    CONSOLE.print(' ');
    CONSOLE.print(s.ntbInSize);
    CONSOLE.print(' ');
    CONSOLE.println(s.ntbOutSize);
}

/* n broadcast frames, EtherType 0x88B5, 60 bytes, the index in the payload. */
static void cmd_netsend(char *arg)
{
    const unsigned long n = strtoul(arg, nullptr, 10) ? strtoul(arg, nullptr, 10) : 1;
    CH32RVUsbHostNetworkInterfaceInfo info;
    usb.getNetworkInterface(info);
    unsigned long sent = 0;
    for (unsigned long i = 0; i < n; i++) {
        uint8_t f[60] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x48, 0x4F, 0x53, 0x54, 0x01, 0x88, 0xB5};
        f[14] = (uint8_t)i;
        f[15] = (uint8_t)(i >> 8);
        sent += usb.networkWriteFrame(f, sizeof f) ? 1 : 0;
    }
    CONSOLE.print("H NETSENT ");
    CONSOLE.println(sent);
}

#if defined(USB_HOST_NET_IP)
static void cmd_netip()
{
    bool ok = usb.networkAttachNetif();
    const uint32_t t0 = millis();
    while (ok && !usb.networkInterface()->hasIP() && millis() - t0 < 15000) {
        delay(10);
    }
    ok = ok && usb.networkInterface()->hasIP();
    CONSOLE.print("H NETIP ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    CONSOLE.println(usb.networkLocalIP());
}

static void cmd_tcpecho(char *arg)
{
    char *port = strchr(arg, ' ');
    char *data = port ? strchr(port + 1, ' ') : nullptr;
    size_t n = 0;
    bool ok = false;
    size_t got = 0;
    if (port && data) {
        *port++ = 0;
        *data++ = 0;
        n = parse_hex(data, block, sizeof block / 2);
        IPAddress ip;
        NetworkClient c;
        if (ip.fromString(arg) && c.connect(ip, (uint16_t)strtoul(port, nullptr, 10))) {
            c.write(block, n);
            const uint32_t t0 = millis();
            while (got < n && millis() - t0 < 3000) {
                const int b = c.read();
                if (b >= 0) {
                    block[sizeof block / 2 + got++] = (uint8_t)b;
                } else {
                    delay(1);
                }
            }
            ok = got == n;
            c.stop();
        }
    }
    CONSOLE.print("H TCPECHO ");
    CONSOLE.print(ok ? 1 : 0);
    CONSOLE.print(' ');
    hex(block + sizeof block / 2, got);
    CONSOLE.println();
}
#else
static void cmd_netip() { CONSOLE.println("H NETIP 2 -"); }
static void cmd_tcpecho(char *) { CONSOLE.println("H TCPECHO 0 "); }
#endif
#endif

#if defined(USB_HOST_AUDIO)
static uint32_t audBytes, audBreaks;
static int16_t audExpect;
static bool audSynced;

static void cmd_audstreams()
{
    CH32RVUsbHostAudioStreamInfo list[6];
    const size_t n = usb.getAudioStreams(CH32RV_USB_HOST_ANY_ADDRESS, list, 6);
    for (size_t i = 0; i < n && i < 6; i++) {
        const CH32RVUsbHostAudioStreamInfo &a = list[i];
        CONSOLE.printf("H AUDSTREAM %u %u %u %lu %u %u %u %02x %u %02x\n", a.input ? 1 : 0, a.channels, a.bitsPerSample,
                       (unsigned long)a.sampleRate, a.sampleRateCount, a.interfaceNumber, a.alternate,
                       a.endpointAddress, a.maxPacketSize, a.protocol);
    }
    CONSOLE.print("H AUDEND ");
    CONSOLE.println((unsigned long)n);
}

static void cmd_audin(char *arg)
{
    const uint32_t ms = strtoul(arg, nullptr, 10) ? strtoul(arg, nullptr, 10) : 1000;
    audBytes = audBreaks = 0;
    audSynced = false;
    const bool ok = usb.audioInputStart(1, 16, 48000);
    const uint32_t t0 = millis();
    while (ok && millis() - t0 < ms) {
        deliverEvents();
        delay(1);
    }
    usb.audioInputStop();
    CONSOLE.printf("H AUDIN %u %lu %lu\n", ok ? 1 : 0, (unsigned long)audBytes, (unsigned long)audBreaks);
}

static void cmd_audout(char *arg)
{
    const uint32_t ms = strtoul(arg, nullptr, 10) ? strtoul(arg, nullptr, 10) : 1000;
    const bool ok = usb.audioOutputStart(1, 16, 48000);
    uint32_t queued = 0;
    int16_t next = 0, block[48];
    const uint32_t t0 = millis();
    while (ok && millis() - t0 < ms) {
        while (usb.audioOutputAvailableForWrite() >= sizeof block) {
            for (int16_t &v : block) v = next++;
            usb.audioSend((const uint8_t *)block, sizeof block);
            queued += sizeof block;
        }
        delay(1);
    }
    const uint32_t under = usb.audioOutputUnderruns();
    const bool fb = usb.audioOutputHasFeedback();
    const uint32_t rate = usb.audioOutputRate();
    usb.audioOutputStop();
    CONSOLE.printf("H AUDOUT %u %lu %lu %u %lu\n", ok ? 1 : 0, (unsigned long)queued, (unsigned long)under, fb ? 1 : 0,
                   (unsigned long)rate);
}

static uint32_t vidFrames, vidComplete, vidLength, vidInOrder;
static uint8_t vidHead[4];
static int vidLastIndex = -1;

static void cmd_vidstreams()
{
    CH32RVUsbHostVideoStreamInfo list[6];
    const size_t n = usb.getVideoStreams(CH32RV_USB_HOST_ANY_ADDRESS, list, 6);
    for (size_t i = 0; i < n && i < 6; i++) {
        const CH32RVUsbHostVideoStreamInfo &v = list[i];
        CONSOLE.printf("H VIDSTREAM %u %u %u %u %u %lu %lu %lu %u\n", v.format, v.width, v.height, v.formatIndex,
                       v.frameIndex, (unsigned long)v.frameInterval, (unsigned long)v.maxVideoFrameBufferSize,
                       (unsigned long)v.maxPayloadSize, v.isochronous ? 1 : 0);
    }
    CONSOLE.print("H VIDEND ");
    CONSOLE.println((unsigned long)n);
}

static void cmd_vidrun(char *arg)
{
    const uint32_t ms = strtoul(arg, nullptr, 10) ? strtoul(arg, nullptr, 10) : 2000;
    vidFrames = vidComplete = vidLength = vidInOrder = 0;
    vidLastIndex = -1;
    memset(vidHead, 0, sizeof vidHead);
    const bool ok = usb.videoStart(CH32RV_USB_HOST_VIDEO_FORMAT_YUY2, 160, 120, 15);
    const uint32_t t0 = millis();
    while (ok && millis() - t0 < ms) {
        deliverEvents();
        delay(1);
    }
    CH32RVUsbHostVideoStats st;
    usb.videoStats(st);
    usb.videoStop();
    CONSOLE.printf("H VIDRUN %u %lu %lu %lu %02x%02x%02x%02x %lu %lu %lu %lu\n", ok ? 1 : 0, (unsigned long)vidFrames,
                   (unsigned long)vidComplete, (unsigned long)vidLength, vidHead[0], vidHead[1], vidHead[2], vidHead[3],
                   (unsigned long)vidInOrder, (unsigned long)st.framesDropped, (unsigned long)st.headerErrors,
                   (unsigned long)st.packetErrors);
}

static void cmd_audfu()
{
    CH32RVUsbHostAudioFeatureUnitInfo list[4];
    const size_t n = usb.getAudioFeatureUnits(CH32RV_USB_HOST_ANY_ADDRESS, list, 4);
    for (size_t i = 0; i < n && i < 4; i++) {
        CONSOLE.printf("H AUDFU %u %u %u %lx %02x\n", list[i].unitId, list[i].sourceId, list[i].channelCount,
                       (unsigned long)list[i].masterControls, list[i].protocol);
    }
    CONSOLE.print("H AUDFUEND ");
    CONSOLE.println((unsigned long)n);
}

static void cmd_audmute(char *arg)
{
    const bool want = strtoul(arg, nullptr, 10) != 0;
    const bool has = usb.audioHasMute();
    const bool set = has && usb.audioSetMute(want);
    bool back = !want;
    const bool got = has && usb.audioGetMute(back);
    CONSOLE.printf("H AUDMUTE %u %u %d\n", has ? 1 : 0, set ? 1 : 0, got ? (back ? 1 : 0) : -1);
}

static void cmd_audvol(char *arg)
{
    const int16_t want = (int16_t)strtol(arg, nullptr, 10);
    const bool has = usb.audioHasVolume();
    const bool set = has && usb.audioSetVolume(want);
    int16_t back = 0;
    const bool got = has && usb.audioGetVolume(back);
    CH32RVUsbHostAudioVolumeRange r;
    if (has) {
        usb.audioGetVolumeRange(r);
    }
    CONSOLE.printf("H AUDVOL %u %u %d %d %d %d\n", has ? 1 : 0, set ? 1 : 0, got ? back : -32768, r.min, r.max,
                   r.resolution);
}
#endif

static CH32RVUsbHostListenerId listenIds[5];

static void cmd_listen(bool on)
{
    bool ok = true;
    if (on) {
        listenIds[0] = usb.addKeyboardListener([](const CH32RVUsbHostKeyboardEvent &e) {
            CONSOLE.printf("H LKEY %c %u\n", e.pressed ? 'p' : 'r', e.keycode);
        });
        listenIds[1] = usb.addGamepadListener([](const CH32RVUsbHostGamepadEvent &e) {
            CONSOLE.print("H LGAMEPAD ");
            hex(e.reportData, e.reportLength);
            CONSOLE.println();
        });
        listenIds[2] = usb.addSystemControlListener([](const CH32RVUsbHostSystemControlEvent &e) {
            CONSOLE.printf("H LSYSTEM %u %c\n", e.usage, e.pressed ? 'p' : 'r');
        });
        listenIds[3] = usb.addDeviceConnectedListener([](const CH32RVUsbHostDeviceInfo &d) {
            CONSOLE.printf("H LCONN %u\n", d.address);
        });
        listenIds[4] = usb.addDeviceDisconnectedListener([](const CH32RVUsbHostDeviceInfo &d) {
            CONSOLE.printf("H LDISC %u\n", d.address);
        });
        for (CH32RVUsbHostListenerId id : listenIds) {
            ok = ok && id != CH32RV_USB_HOST_INVALID_LISTENER_ID;
        }
    } else {
        for (CH32RVUsbHostListenerId &id : listenIds) {
            ok = usb.removeListener(id) && ok;
            id = CH32RV_USB_HOST_INVALID_LISTENER_ID;
        }
    }
    unsigned n = 0;
    for (CH32RVUsbHostListenerId id : listenIds) {
        n += id != CH32RV_USB_HOST_INVALID_LISTENER_ID;
    }
    CONSOLE.printf("H LISTEN %u %d\n", n, ok ? 1 : 0);
}

static void reply(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (strcmp(cmd, "ready") == 0) {
        CONSOLE.print("READY ");
        CONSOLE.print(arg);
        CONSOLE.print(' ');
        CONSOLE.print(BENCH_BUILD_ID);
        CONSOLE.print(' ');
        CONSOLE.print(began ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(usb.lastErrorName());
    } else if (strcmp(cmd, "listen") == 0) {
        cmd_listen(atoi(arg) != 0);
    } else if (strcmp(cmd, "onkey2") == 0) {
        usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
            CONSOLE.printf("H KEY2 %c %u\n", e.pressed ? 'p' : 'r', e.keycode);
        });
        CONSOLE.println("H ONKEY2");
    } else if (strcmp(cmd, "dev") == 0) {
        cmd_dev();
    } else if (strcmp(cmd, "where") == 0) {
        cmd_where(arg);
#if !defined(USB_HOST_FS) && !defined(USB_HOST_NET) && !defined(USB_HOST_AUDIO)   /* the FS / NET / AUDIO builds carry their own commands only */
    } else if (strcmp(cmd, "suspend") == 0 || strcmp(cmd, "resume") == 0) {
        const bool s = cmd[0] == 's';
        const bool ok = s ? usb.suspend() : usb.resume();
        CONSOLE.print(s ? "H SUSPEND " : "H RESUME ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(usb.lastErrorName());
    } else if (strcmp(cmd, "power") == 0) {
        CONSOLE.print("H POWER ");
        CONSOLE.print(usb.suspended() ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(wakeups);
    } else if (strcmp(cmd, "cdc") == 0) {
        cmd_cdc(arg);
    } else if (strcmp(cmd, "cdcrx") == 0) {
        cmd_cdcrx(arg);
    } else if (strcmp(cmd, "cdcbig") == 0) {
        cmd_cdcbig(strtoul(arg, nullptr, 10));
    } else if (strcmp(cmd, "baud") == 0) {
        CONSOLE.print("H BAUD ");
        CONSOLE.println(cdc.setBaudRate(strtoul(arg, nullptr, 10)) ? 1 : 0);
    } else if (strcmp(cmd, "hidv") == 0) {
        uint8_t d[63];
        const size_t n = parse_hex(arg, d, sizeof d);
        CONSOLE.print("H HIDV_SENT ");
        CONSOLE.println(usb.sendHIDVendorOutput(d, n) ? 1 : 0);
    } else if (strcmp(cmd, "leds") == 0) {
        const unsigned v = strtoul(arg, nullptr, 0);
        CONSOLE.print("H LEDS ");
        CONSOLE.println(usb.setKeyboardLeds(v & 1, v & 2, v & 4) ? 1 : 0);
    } else if (strcmp(cmd, "midi") == 0) {
        char *end;
        const unsigned ch = strtoul(arg, &end, 10);
        const unsigned note = strtoul(end, &end, 10);
        const unsigned vel = strtoul(end, &end, 10);
        CONSOLE.print("H MIDI_SENT ");
        CONSOLE.println(usb.midiSendNoteOn(ch, note, vel) ? 1 : 0);
#endif
    } else if (strcmp(cmd, "msc") == 0) {
        cmd_msc();
#if defined(USB_HOST_FS)
    } else if (strcmp(cmd, "fsmount") == 0) {
        cmd_fsmount(arg);
    } else if (strcmp(cmd, "fswrite") == 0) {
        cmd_fswrite(arg);
    } else if (strcmp(cmd, "fsread") == 0) {
        cmd_fsread(arg);
    } else if (strcmp(cmd, "fsls") == 0) {
        cmd_fsls();
    } else if (strcmp(cmd, "fsrm") == 0) {
        char path[24] = "/";
        strncat(path, arg, sizeof path - 2);
        CONSOLE.print("H FSRM ");
        CONSOLE.println(stick.remove(path) ? 1 : 0);
    } else if (strcmp(cmd, "fsend") == 0) {
        stick.end();
        CONSOLE.println("H FSEND");
#endif
#if !defined(USB_HOST_FS) && !defined(USB_HOST_NET) && !defined(USB_HOST_AUDIO)   /* the FS / NET / AUDIO builds carry their own commands only */
    } else if (strcmp(cmd, "mscraw") == 0) {
        cmd_mscraw(strtoul(arg, nullptr, 10));
    } else if (strcmp(cmd, "msc64") == 0) {
        cmd_msc64(strtoul(arg, nullptr, 10));
    } else if (strcmp(cmd, "msclast") == 0) {
        cmd_msclast();
    } else if (strcmp(cmd, "epinfo") == 0) {
        cmd_epinfo();
    } else if (strcmp(cmd, "vend") == 0) {
        cmd_vend(arg);
    } else if (strcmp(cmd, "vreq") == 0) {
        char buf[16] = {};
        size_t got = 0;
        const bool ok = usb.vendorControlIn(0x10, 0, 0, (uint8_t *)buf, sizeof buf - 1, &got);
        CONSOLE.print("H VREQ ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(buf);
    } else if (strcmp(cmd, "ccid") == 0) {
        uint8_t atr[33];
        size_t n = 0;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidPowerOn(atr, sizeof atr, &n);
        CONSOLE.print("H CCID ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        hex(atr, n);
        CONSOLE.println();
    } else if (strcmp(cmd, "apdu") == 0) {
        uint8_t tx[32], rx[40];
        const size_t n = parse_hex(arg, tx, sizeof tx);
        size_t got = 0;
        uint16_t sw = 0;
        const bool ok = usb.ccidApdu(tx, n, rx, sizeof rx, &got, &sw);
        CONSOLE.print("H APDU ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        hex(rx, got);
        CONSOLE.print(' ');
        print4(sw);
        CONSOLE.println();
    } else if (strcmp(cmd, "strings") == 0) {
        CH32RVUsbHostDeviceInfo d;
        const bool ok = usb.getDevice(usb.resolve(CH32RV_USB_HOST_ANY_ADDRESS), d);
        CONSOLE.print("H STRINGS ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.print(d.manufacturer);
        CONSOLE.print('|');
        CONSOLE.print(d.product);
        CONSOLE.print('|');
        CONSOLE.println(d.serial);
    } else if (strcmp(cmd, "ports") == 0) {
        CONSOLE.print("H PORTS ");
        CONSOLE.println(usb.serialPortCount());
    } else if (strcmp(cmd, "sser") == 0) {
        char *end;
        const uint8_t port = (uint8_t)strtoul(arg, &end, 10);
        uint8_t d[160];
        const size_t n = parse_hex(*end ? end + 1 : end, d, sizeof d);
        CONSOLE.print("H SSER ");
        CONSOLE.println(usb.sendSerial(d, n, CH32RV_USB_HOST_ANY_ADDRESS, port) ? 1 : 0);
    } else if (strcmp(cmd, "srx") == 0) {
        const uint8_t port = (uint8_t)strtoul(arg, nullptr, 10) & 1;
        CONSOLE.print("H SRX ");
        CONSOLE.print(port);
        CONSOLE.print(' ');
        hex(serialRx[port], serialRxLength[port]);
        CONSOLE.println();
        serialRxLength[port] = 0;
    } else if (strcmp(cmd, "sconf") == 0) {
        char *end;
        const uint8_t port = (uint8_t)strtoul(arg, &end, 10);
        CH32RVUsbHostSerialConfig c;
        c.baud = strtoul(end, &end, 10);
        c.dataBits = (uint8_t)strtoul(end, &end, 10);
        c.parity = (CH32RVUsbHostSerialParity)strtoul(end, &end, 10);
        c.stopBits = (CH32RVUsbHostSerialStopBits)strtoul(end, &end, 10);
        CONSOLE.print("H SCONF ");
        CONSOLE.println(usb.setSerialConfig(c, CH32RV_USB_HOST_ANY_ADDRESS, port) ? 1 : 0);
    } else if (strcmp(cmd, "slines") == 0) {
        char *end;
        const uint8_t port = (uint8_t)strtoul(arg, &end, 10);
        const bool dtr = strtoul(end, &end, 10) != 0;
        const bool rts = strtoul(end, &end, 10) != 0;
        CONSOLE.print("H SLINES ");
        CONSOLE.println(usb.setSerialControlLines(dtr, rts, CH32RV_USB_HOST_ANY_ADDRESS, port) ? 1 : 0);
    } else if (strcmp(cmd, "hidget") == 0) {
        char *end;
        const uint8_t itf = (uint8_t)strtoul(arg, &end, 10);
        const uint8_t type = (uint8_t)strtoul(end, &end, 10);
        const uint8_t id = (uint8_t)strtoul(end, &end, 10);
        size_t n = strtoul(end, &end, 10), got = 0;
        uint8_t d[64];
        n = n > sizeof d ? sizeof d : n;
        const bool ok = usb.getHIDReport(itf, type, id, d, n, &got);
        CONSOLE.print("H HIDGET ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        hex(d, ok ? got : 0);
        CONSOLE.println();
    } else if (strcmp(cmd, "hidset") == 0) {
        char *end;
        const uint8_t itf = (uint8_t)strtoul(arg, &end, 10);
        const uint8_t type = (uint8_t)strtoul(end, &end, 10);
        const uint8_t id = (uint8_t)strtoul(end, &end, 10);
        uint8_t d[64];
        const size_t n = parse_hex(*end ? end + 1 : end, d, sizeof d);
        CONSOLE.print("H HIDSET ");
        CONSOLE.println(usb.sendHIDReport(itf, type, id, d, n) ? 1 : 0);
    } else if (strcmp(cmd, "hidfeat") == 0) {
        uint8_t d[63];
        const size_t n = parse_hex(arg, d, sizeof d);
        CONSOLE.print("H HIDFEAT ");
        CONSOLE.println(usb.sendHIDVendorFeature(d, n) ? 1 : 0);
    } else if (strcmp(cmd, "proto") == 0) {
        char *end;
        const uint8_t itf = (uint8_t)strtoul(arg, &end, 10);
        const bool boot = strtoul(end, &end, 10) != 0;
        CONSOLE.print("H PROTO ");
        CONSOLE.println(usb.sendSetProtocol(itf, any_address(), boot) ? 1 : 0);
    } else if (strcmp(cmd, "layout") == 0) {
        usb.setKeyboardLayout(strcmp(arg, "jp") == 0 ? CH32RV_USB_HOST_KEYBOARD_LAYOUT_JA_JP
                                                     : CH32RV_USB_HOST_KEYBOARD_LAYOUT_EN_US);
        CONSOLE.println("H LAYOUT");
    } else if (strcmp(cmd, "locks") == 0) {
        CONSOLE.print("H LOCKS ");
        CONSOLE.print(usb.getKeyboardNumLock() ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.print(usb.getKeyboardCapsLock() ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(usb.getKeyboardScrollLock() ? 1 : 0);
    } else if (strcmp(cmd, "hiddesc") == 0) {
        for (uint8_t i = 0; i < hidDescCount; i++) {
            CONSOLE.print("H HIDDESC ");
            CONSOLE.print(hidDescItf[i]);
            CONSOLE.print(' ');
            CONSOLE.println(hidDescLength[i]);
        }
        CONSOLE.println("H HIDDESCEND");
    } else if (strcmp(cmd, "hidraw") == 0) {
        hidRaw = strtoul(arg, nullptr, 10) != 0;
        CONSOLE.print("H HIDRAW ");
        CONSOLE.println(hidRaw ? 1 : 0);
    } else if (strcmp(cmd, "mscmaxlun") == 0) {
        uint8_t lun = 0;
        const bool ok = usb.mscMaxLun(lun);
        CONSOLE.print("H MSCMAXLUN ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(lun);
    } else if (strcmp(cmd, "mscrd") == 0) {
        const bool ok = usb.mscReadBlocks(strtoul(arg, nullptr, 10), block, 1);
        CH32RVUsbHostMscSense sense;
        if (!ok) {
            usb.mscRequestSense(sense);
        }
        CONSOLE.print("H MSCRD ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        hex(&sense.senseKey, 1);
        CONSOLE.print(' ');
        hex(&sense.additionalSenseCode, 1);
        CONSOLE.print(' ');
        hex(&sense.additionalSenseQualifier, 1);
        CONSOLE.println();
#endif
    } else if (strcmp(cmd, "msclun") == 0) {
        CONSOLE.print("H MSCLUN ");
        CONSOLE.println(usb.mscSelectLun((uint8_t)strtoul(arg, nullptr, 10)) ? 1 : 0);
    } else if (strcmp(cmd, "msctur") == 0) {
        const bool ok = usb.mscTestUnitReady(CH32RV_USB_HOST_ANY_ADDRESS, 2000);
        CH32RVUsbHostMscSense sense;
        if (!ok) {
            usb.mscRequestSense(sense);
        }
        CONSOLE.print("H MSCTUR ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        hex(&sense.senseKey, 1);
        CONSOLE.print(' ');
        hex(&sense.additionalSenseCode, 1);
        CONSOLE.print(' ');
        hex(&sense.additionalSenseQualifier, 1);
        CONSOLE.println();
#if !defined(USB_HOST_FS) && !defined(USB_HOST_NET) && !defined(USB_HOST_AUDIO)   /* the FS / NET / AUDIO builds carry their own commands only */
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
        CONSOLE.print("H MIDIOUT ");
        CONSOLE.println(ok ? 1 : 0);
    } else if (strcmp(cmd, "midisx") == 0) {
        uint8_t d[32];
        const size_t n = parse_hex(arg, d, sizeof d);
        CONSOLE.print("H MIDIOUT ");
        CONSOLE.println(usb.midiSendSysEx(d, n) ? 1 : 0);
    } else if (strcmp(cmd, "vreqout") == 0) {
        char *end;
        const uint8_t req = (uint8_t)strtoul(arg, &end, 0);
        const uint16_t value = (uint16_t)strtoul(end, &end, 0);
        const uint16_t index = (uint16_t)strtoul(end, &end, 0);
        uint8_t d[32];
        const size_t n = parse_hex(*end ? end + 1 : end, d, sizeof d);
        CONSOLE.print("H VREQOUT ");
        CONSOLE.println(usb.vendorControlOut(req, value, index, d, n) ? 1 : 0);
    } else if (strcmp(cmd, "ccidstatus") == 0) {
        CH32RVUsbHostCcidStatus st;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidGetStatus(st);
        CONSOLE.print("H CCIDSTATUS ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned)st.iccStatus);
        CONSOLE.print(' ');
        CONSOLE.print(st.present ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(st.active ? 1 : 0);
    } else if (strcmp(cmd, "ccidpresent") == 0) {
        CONSOLE.print("H CCIDPRESENT ");
        CONSOLE.println((usb.ccidReady() || usb.ccidOpen()) && usb.ccidCardPresent() ? 1 : 0);
    } else if (strcmp(cmd, "ccidoff") == 0) {
        CONSOLE.print("H CCIDOFF ");
        CONSOLE.println(usb.ccidPowerOff() ? 1 : 0);
    } else if (strcmp(cmd, "ccidesc") == 0) {
        uint8_t tx[32], rx[40];
        const size_t n = parse_hex(arg, tx, sizeof tx);
        size_t got = 0;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidEscape(tx, n, rx, sizeof rx, &got);
        CONSOLE.print("H CCIDESC ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        hex(rx, ok ? got : 0);
        CONSOLE.println();
    } else if (strcmp(cmd, "ccidslots") == 0) {
        CONSOLE.print("H CCIDSLOTS ");
        CONSOLE.println(usb.ccidReady() || usb.ccidOpen() ? usb.ccidSlotCount() : 0);
    } else if (strcmp(cmd, "ccidmsg") == 0) {
        char *rest = arg;
        const uint8_t type = (uint8_t)strtoul(arg, &rest, 16);
        while (*rest == ' ') rest++;
        uint8_t tx[32];
        const size_t n = parse_hex(rest, tx, sizeof tx);
        CH32RVUsbHostCcidResponse r;
        const bool ok = (usb.ccidReady() || usb.ccidOpen()) && usb.ccidMessage(type, nullptr, tx, n, r);
        CONSOLE.printf("H CCIDMSG %d %02x %02x %02x ", ok ? 1 : 0, r.messageType, r.status, r.error);
        hex(r.data, ok ? r.length : 0);
        CONSOLE.println();
    } else if (strcmp(cmd, "ccidabort") == 0) {
        CONSOLE.print("H CCIDABORT ");
        CONSOLE.println((usb.ccidReady() || usb.ccidOpen()) && usb.ccidAbort() ? 1 : 0);
#endif
#if defined(USB_HOST_AUDIO)
    } else if (strcmp(cmd, "audstreams") == 0) {
        cmd_audstreams();
    } else if (strcmp(cmd, "audin") == 0) {
        cmd_audin(arg);
    } else if (strcmp(cmd, "audout") == 0) {
        cmd_audout(arg);
    } else if (strcmp(cmd, "audfu") == 0) {
        cmd_audfu();
    } else if (strcmp(cmd, "vidstreams") == 0) {
        cmd_vidstreams();
    } else if (strcmp(cmd, "vidrun") == 0) {
        cmd_vidrun(arg);
    } else if (strcmp(cmd, "audmute") == 0) {
        cmd_audmute(arg);
    } else if (strcmp(cmd, "audvol") == 0) {
        cmd_audvol(arg);
#endif
#if defined(USB_HOST_NET)
    } else if (strcmp(cmd, "netopen") == 0) {
        cmd_netopen();
    } else if (strcmp(cmd, "netsend") == 0) {
        cmd_netsend(arg);
    } else if (strcmp(cmd, "netstat") == 0) {
        CH32RVUsbHostNetworkStats s;
        usb.networkStats(s);
        CONSOLE.print("H NETRX ");
        CONSOLE.print((unsigned long)netFrames);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)netOurs);
        CONSOLE.print(' ');
        CONSOLE.print(usb.networkLinkUp() ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)s.rxDropped);
        CONSOLE.print(' ');
        CONSOLE.println((unsigned long)s.rxOversized);
    } else if (strcmp(cmd, "netip") == 0) {
        cmd_netip();
    } else if (strcmp(cmd, "tcpecho") == 0) {
        cmd_tcpecho(arg);
    } else if (strcmp(cmd, "netclose") == 0) {
        usb.networkClose();
        CONSOLE.println("H NETCLOSE");
#endif
    } else {
        CONSOLE.println("ERROR COMMAND");
    }
}

void setup()
{
    CONSOLE_BEGIN(115200);
    CONSOLE.print("STARTUP ");  // a restart in the middle of a test shows (run_hardware.py)
    CONSOLE.println(CH32RV.resetReasonName());
    usb.onRemoteWakeup([] {
        CONSOLE.print("H WAKEUP ");
        CONSOLE.println(++wakeups);
    });
    usb.onDeviceConnected([](const CH32RVUsbHostDeviceInfo &d) {
        CONSOLE.print("H CONNECTED ");
        CONSOLE.print(d.address);
        CONSOLE.print(' ');
        print4(d.vid);
        CONSOLE.print(' ');
        print4(d.pid);
        CONSOLE.print(' ');
        CONSOLE.println(d.product);
    });
    usb.onDeviceDisconnected([](const CH32RVUsbHostDeviceInfo &d) {
        CONSOLE.print("H DISCONNECTED ");
        CONSOLE.print(d.address);
        CONSOLE.print(' ');
        print4(d.vid);
        CONSOLE.print(' ');
        print4(d.pid);
        CONSOLE.println();
    });
    usb.onKeyboard([](const CH32RVUsbHostKeyboardEvent &e) {
        CONSOLE.print("H KEY ");
        CONSOLE.print(e.pressed ? 'p' : 'r');
        CONSOLE.print(' ');
        CONSOLE.print(e.keycode);
        CONSOLE.print(' ');
        CONSOLE.println(e.ascii);
    });
    usb.onMouse([](const CH32RVUsbHostMouseEvent &e) {
        CONSOLE.print("H MOUSE ");
        CONSOLE.print(e.x);
        CONSOLE.print(' ');
        CONSOLE.print(e.y);
        CONSOLE.print(' ');
        CONSOLE.print(e.wheel);
        CONSOLE.print(' ');
        CONSOLE.println(e.buttons);
    });
    usb.onConsumerControl([](const CH32RVUsbHostConsumerControlEvent &e) {
        CONSOLE.print("H CONSUMER ");
        CONSOLE.print(e.usage);
        CONSOLE.println(e.pressed ? " p" : " r");
    });
    usb.onHIDVendorInput([](const CH32RVUsbHostHIDVendorInput &e) {
        CONSOLE.print("H HIDVIN ");
        hex(e.reportData, e.reportLength);
        CONSOLE.println();
    });
    usb.onMidiMessage([](const CH32RVUsbHostMidiMessage &m) {
        CONSOLE.print("H MIDI ");
        CONSOLE.print(m.codeIndex);
        CONSOLE.print(' ');
        hex(&m.status, 1);
        CONSOLE.print(' ');
        CONSOLE.print(m.data1);
        CONSOLE.print(' ');
        CONSOLE.println(m.data2);
    });
    usb.onGamepad([](const CH32RVUsbHostGamepadEvent &e) {
        CONSOLE.print("H GAMEPAD ");
        hex(e.reportData, e.reportLength);
        CONSOLE.println();
    });
    usb.onSystemControl([](const CH32RVUsbHostSystemControlEvent &e) {
        CONSOLE.print("H SYSTEM ");
        CONSOLE.print(e.usage);
        CONSOLE.println(e.pressed ? " p" : " r");
    });
    usb.onHIDInput([](const CH32RVUsbHostHIDInput &e) {
        if (!hidRaw) {
            return;
        }
        CONSOLE.print("H HIDIN ");
        CONSOLE.print(e.interfaceNumber);
        CONSOLE.print(' ');
        hex(e.data, e.length);
        CONSOLE.println();
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
#if defined(USB_HOST_AUDIO)
    usb.onVideoFrame([](const CH32RVUsbHostVideoFrame &f) {
        vidFrames++;
        if (f.complete) {
            vidComplete++;
        }
        if (!vidLength) {
            vidLength = (uint32_t)f.length;
        }
        if (f.length >= 4) {
            memcpy(vidHead, f.data, 4);
            /* The camera's frame index: one more than the last, frame after frame. */
            if (f.data[1] == (uint8_t)~f.data[0] && vidLastIndex >= 0 && f.data[0] == (uint8_t)(vidLastIndex + 1)) {
                vidInOrder++;
            }
            vidLastIndex = f.data[0];
        }
    });
    usb.onAudioData([](const CH32RVUsbHostAudioData &d) {
        audBytes += d.length;
        for (size_t i = 0; i + 1 < d.length; i += 2) {
            const int16_t v = (int16_t)(d.data[i] | d.data[i + 1] << 8);
            if (audSynced && v != audExpect) {
                audBreaks++;
            }
            audExpect = (int16_t)(v + 1);
            audSynced = true;
        }
    });
#endif
#if defined(USB_HOST_NET)
    usb.onNetworkFrame([](const CH32RVUsbHostNetworkFrame &f) {
        netFrames++;
        if (f.length >= 14 && f.data[12] == 0x88 && f.data[13] == 0xB5) {
            netOurs++;
        }
    });
#endif
    usb.onCcidCardInserted([](const CH32RVUsbHostCcidSlotEvent &) { CONSOLE.println("H CARD in"); });
    usb.onCcidCardRemoved([](const CH32RVUsbHostCcidSlotEvent &) { CONSOLE.println("H CARD out"); });
    cdc.begin(115200);
#ifdef USB_PEER_HOST_PORT
    CH32RVUsbHostConfig config;
    config.port = USB_PEER_HOST_PORT;   /* run_hardware.py: the ESP32-P4 is on the USBHS */
    began = usb.begin(config);
#else
    began = usb.begin();
#endif
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    while (CONSOLE.available()) {
        const int c = CONSOLE.read();
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
