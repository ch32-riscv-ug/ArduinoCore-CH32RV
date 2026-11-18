/* Another CH32RV board as the USB device for tests/peer/usb/host/storage: mass storage with two logical units,
 * MIDI, a vendor interface with requests both ways, and a smart card reader (CCID) whose card comes and goes. The
 * DUT (CH32RVUsbHost) is the host; this board runs CH32RVUsbDevice on the controller the bench record names
 * (build_opt.h: PEER_USB_CONTROLLER, written by run_hardware.py).
 *
 * Commands, one per line on the debug probe's console (SerialDMSeq); answers start "CD " (peer_pico's "PD "):
 *   ?                     CD READY <configured 0/1>
 *   media <lun> <0|1>     CD MEDIA <lun> <0|1>  (the unit ready or not: 02/3A/00 while not)
 *   midiraw <hex>         CD MIDIRAW <ok>       (USB-MIDI event packets, 4 bytes each, sent as they are)
 *   midilast              CD MIDILAST <hex>     (the packets received since the last midilast)
 *   vlast                 CD VLAST <wValue> <wIndex> <hex>  (the last vendor request 0x11 OUT)
 *   card <0|1>            CD CARD <0|1>         (the card taken out or put in: NotifySlotChange)
 *   fat12                 CD FAT12 1            (unit 0 becomes an empty FAT12 volume - 64 sectors, one a
 *                                               cluster, 16 root entries - and block 13 reads like the rest:
 *                                               usb-peer-host-fs)
 *
 * Without a command: logical unit 0 is 64 blocks of (LBA ^ offset) - its block 13 fails to read, with the sense
 * peer_pico's TinyUSB reports for any failed read (02/3A/00, set here with setSense()) - and unit 1 is 16 blocks
 * of 0xA5 (each a CH32RVUsbDeviceMsc, as peer_pico's units); both keep what is written in a few blocks of RAM (the
 * smaller parts have no room for all 80); a note-on comes back with its note raised by 12 (other MIDI packets are
 * only recorded); vendor request 0x10 (IN) answers "CH32RV"; the card answers an APDU reversed then 90 00, and an
 * Escape with its bytes. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef PEER_USB_CONTROLLER
#define PEER_USB_CONTROLLER CH32RVUsbController::Auto
#endif

CH32RVUsbDeviceMsc msc;    /* LUN 0 */
CH32RVUsbDeviceMsc msc1;   /* LUN 1 */
CH32RVUsbDeviceMidi midi;
CH32RVUsbDeviceVendor vendor;
CH32RVUsbDeviceCcid ccid;

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

/* ------------------------------------------------------------ mass storage: units 0 and 1 */
static const uint32_t BLOCKS[2] = {64, 16};
static const uint8_t SLOTS = 8;   /* a FAT volume's FATs, root and two files */
static uint8_t slotData[SLOTS][512];
static uint32_t slotLba[SLOTS];
static uint8_t slotLun[SLOTS];
static bool slotUsed[SLOTS];

/* After `fat12`: unit 0 an empty FAT12 volume (peer_pico's fat12()), its block 13 readable. */
static bool fat;
static const uint8_t FAT_BOOT[] = {0xEB, 0x3C, 0x90, 'M', 'S', 'D', 'O', 'S', '5', '.', '0', 0x00, 0x02, 1, 1, 0, 2,
                                   16, 0, 64, 0, 0xF8, 1, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0x80, 0, 0x29, 0x12, 0x34, 0x56, 0x78,
                                   'N', 'O', ' ', 'N', 'A', 'M', 'E', ' ', ' ', ' ', ' ',
                                   'F', 'A', 'T', '1', '2', ' ', ' ', ' '};

static uint8_t fatByte(uint32_t lba, uint32_t k)
{
    if (lba == 0) {
        if (k < sizeof FAT_BOOT) return FAT_BOOT[k];
        return k == 510 ? 0x55 : k == 511 ? 0xAA : 0;
    }
    if (lba == 1 || lba == 2) return k == 0 ? 0xF8 : k < 3 ? 0xFF : 0;   /* the two FATs */
    return 0;
}

/* A block as it is until written: unit 0 (LBA ^ offset), or the FAT12 volume; unit 1 0xA5. */
static uint8_t initial(uint8_t lun, uint32_t lba, uint32_t k)
{
    return lun ? 0xA5 : fat ? fatByte(lba, k) : (uint8_t)(lba ^ k);
}

static int slotOf(uint8_t lun, uint32_t lba, bool make)
{
    for (uint8_t i = 0; i < SLOTS; i++)
        if (slotUsed[i] && slotLun[i] == lun && slotLba[i] == lba) return i;
    if (!make) return -1;
    for (uint8_t i = 0; i < SLOTS; i++)
        if (!slotUsed[i]) {
            slotUsed[i] = true;
            slotLun[i] = lun;
            slotLba[i] = lba;
            for (uint32_t k = 0; k < 512; k++) slotData[i][k] = initial(lun, lba, k);
            return i;
        }
    return -1;
}

static int32_t readUnit(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (lba >= BLOCKS[lun] || offset + size > 512) return -1;
    const int s = slotOf(lun, lba, false);
    uint8_t *out = (uint8_t *)buffer;
    for (uint32_t k = 0; k < size; k++) out[k] = s < 0 ? initial(lun, lba, offset + k) : slotData[s][offset + k];
    return (int32_t)size;
}

static int32_t writeUnit(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    if (lba >= BLOCKS[lun] || offset + size > 512) return -1;
    const int s = slotOf(lun, lba, true);
    if (s < 0) return -1;
    memcpy(slotData[s] + offset, buffer, size);
    return (int32_t)size;
}

static int32_t read0(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    if (lba == 13 && !fat) {   /* the data stage stalls; the sense is the one peer_pico's TinyUSB gives any failed read */
        msc.setSense(0x02, 0x3A, 0x00);
        return -1;
    }
    return readUnit(0, lba, offset, buffer, size);
}
static int32_t write0(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    return writeUnit(0, lba, offset, buffer, size);
}

static int32_t read1(uint32_t lba, uint32_t offset, void *buffer, uint32_t size)
{
    return readUnit(1, lba, offset, buffer, size);
}

static int32_t write1(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t size)
{
    return writeUnit(1, lba, offset, buffer, size);
}

/* ------------------------------------------------------------ MIDI received */
static uint8_t midiSeen[64];
static size_t midiSeenLength;

/* ------------------------------------------------------------ vendor requests 0x10 (IN) and 0x11 (OUT) */
static uint8_t vendorOut[64];
static uint16_t vendorOutLength, vendorOutValue, vendorOutIndex;

static bool vendorRequest(const CH32RVUsbDeviceVendorControlRequest &r)
{
    static const char answer[] = "CH32RV";
    if (r.bRequest == 0x10 && (r.bmRequestType & 0x80)) {
        if (r.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) return true;
        const size_t n = r.wLength < sizeof answer - 1 ? r.wLength : sizeof answer - 1;
        return vendor.sendControlResponse(r, answer, n);
    }
    if (r.bRequest == 0x11 && !(r.bmRequestType & 0x80)) {
        if (r.stage == CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
            if (r.wLength > sizeof vendorOut) return false;
            vendorOutLength = r.wLength;
            vendorOutValue = r.wValue;
            vendorOutIndex = r.wIndex;
            /* The data stage into vendorOut: the device's own OUT answer (sendControlResponse() sends IN data). */
            return r.wLength ? vendor.device().controlOut(vendorOut, r.wLength) : vendor.sendControlResponse(r);
        }
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ the card */
static size_t apdu(const uint8_t *in, size_t length, uint8_t *out, size_t capacity)
{
    if (length + 2 > capacity) return 0;
    for (size_t i = 0; i < length; i++) out[i] = in[length - 1 - i];
    out[length] = 0x90;
    out[length + 1] = 0x00;
    return length + 2;
}

static size_t escape(const uint8_t *in, size_t length, uint8_t *out, size_t capacity)
{
    if (length > capacity) return 0;
    memcpy(out, in, length);
    return length;
}

/* ------------------------------------------------------------ the console */
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
    while (s[0] && s[1] && n < cap && hexval(s[0]) >= 0 && hexval(s[1]) >= 0) {
        out[n++] = (uint8_t)(hexval(s[0]) << 4 | hexval(s[1]));
        s += 2;
    }
    return n;
}

static void printHex(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) say("%02x", d[i]);
}

static char line[400];
static size_t used;

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "?")) say("CD READY %u\n", USBDevice.ready() ? 1 : 0);
    else if (!strcmp(cmd, "media")) {
        char *end;
        const uint8_t lun = (uint8_t)strtoul(arg, &end, 10);
        const bool ready = strtoul(end, nullptr, 10) != 0;
        if (lun == 0) msc.mediaPresent(ready);
        else if (lun == 1) msc1.mediaPresent(ready);
        say("CD MEDIA %u %u\n", lun, ready ? 1 : 0);
    } else if (!strcmp(cmd, "fat12")) {
        for (uint8_t i = 0; i < SLOTS; i++)
            if (slotLun[i] == 0) slotUsed[i] = false;
        fat = true;
        say("CD FAT12 1\n");
    } else if (!strcmp(cmd, "midiraw")) {
        uint8_t d[48];
        const size_t n = parseHex(arg, d, sizeof d) / 4 * 4;
        bool ok = n > 0;
        for (size_t i = 0; ok && i < n; i += 4) {
            CH32RVUsbDeviceMidiPacket p;
            p.header = d[i];
            p.byte1 = d[i + 1];
            p.byte2 = d[i + 2];
            p.byte3 = d[i + 3];
            ok = midi.writePacket(p);
        }
        say("CD MIDIRAW %u\n", ok ? 1 : 0);
    } else if (!strcmp(cmd, "midilast")) {
        say("CD MIDILAST ");
        printHex(midiSeen, midiSeenLength);
        say("\n");
        midiSeenLength = 0;
    } else if (!strcmp(cmd, "vlast")) {
        say("CD VLAST %u %u ", vendorOutValue, vendorOutIndex);
        printHex(vendorOut, vendorOutLength);
        say("\n");
    } else if (!strcmp(cmd, "card")) {
        const bool in = strtoul(arg, nullptr, 10) != 0;
        if (in) ccid.insertCard(atr, sizeof atr);
        else ccid.removeCard();
        say("CD CARD %u\n", in ? 1 : 0);
    } else say("CD UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    msc.vendorID("ch32rv");
    msc.productID("CH32RV LUN 0");
    msc.productRevision("1.0");
    msc.onRead(read0);
    msc.onWrite(write0);
    msc.begin(BLOCKS[0], 512);
    msc.mediaPresent(true);
    msc1.vendorID("ch32rv");
    msc1.productID("CH32RV LUN 1");
    msc1.productRevision("1.0");
    msc1.onRead(read1);
    msc1.onWrite(write1);
    msc1.begin(BLOCKS[1], 512);
    msc1.mediaPresent(true);
    vendor.onControlRequest(vendorRequest);
    ccid.onApdu(apdu);
    ccid.onEscape(escape);
    ccid.insertCard(atr, sizeof atr);
    CH32RVUsbDeviceConfig config;
    config.manufacturer = "ch32rv-tests";
    config.product = "CH32RV peer storage";
    config.serialNumber = "CH32RVPEERSTORE";
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
        if (midiSeenLength + 4 <= sizeof midiSeen) {
            const uint8_t raw[4] = {p.header, p.byte1, p.byte2, p.byte3};
            memcpy(midiSeen + midiSeenLength, raw, 4);
            midiSeenLength += 4;
        }
        if ((p.header & 0x0F) == 0x09) {
            p.byte2 = (uint8_t)(p.byte2 + 12);
            midi.writePacket(p);
        }
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
