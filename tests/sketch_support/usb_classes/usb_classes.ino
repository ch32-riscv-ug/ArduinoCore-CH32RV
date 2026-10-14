/* USB device contracts, second set (tests/instrumented/usb): the classes that
 * do not fit beside usb_contracts' on X035's seven endpoints - vendor bulk
 * with WebUSB and the Microsoft OS 2.0 descriptors, mass storage (a FAT RAM
 * disk) and a smart card reader - with the default DFU runtime.
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   file            FILE <size> <first line of HOST.TXT>, or FILE NONE
 *   card            CARD <present 0/1> <powered 0/1> <APDUs answered>
 *
 * Over USB, without a command: vendor bulk echoes every byte; vendor request
 * 0x10 (IN) answers "CH32"; the FAT disk holds README.TXT; the card answers
 * an APDU with its bytes reversed followed by 90 00. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbDeviceVendor vendor;
CH32RVUsbDeviceMsc msc;
CH32RVUsbDeviceCcid ccid;

static uint8_t storage[20 * 512];   /* X035 has 20 KB of RAM */
CH32RVUsbDeviceMscFatRamDisk disk(storage, sizeof storage);

static const uint8_t atr[] = {0x3B, 0x8A, 0x80, 0x01, 'C', 'H', '3', '2', 'R', 'V', 'C', 'C', 'I', 'D', 0x00};
static CH32RVUsbDeviceConfig config;
static char line[80];
static uint8_t used;

static bool vendorRequest(const CH32RVUsbDeviceVendorControlRequest &request)
{
    static const char answer[] = "CH32";
    if (request.bRequest != 0x10) {
        return false;
    }
    if (request.stage != CH32RV_USB_DEVICE_CONTROL_STAGE_SETUP) {
        return true;
    }
    return vendor.sendControlResponse(request, answer, 4);
}

static size_t apdu(const uint8_t *in, size_t length, uint8_t *out, size_t capacity)
{
    if (length + 2 > capacity) {
        return 0;
    }
    for (size_t i = 0; i < length; i++) {
        out[i] = in[length - 1 - i];
    }
    out[length] = 0x90;
    out[length + 1] = 0x00;
    return length + 2;
}

static void reply(const char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.started() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(USBDevice.lastErrorName());
    } else if (strcmp(command, "file") == 0) {
        static uint8_t text[64];
        const size_t n = disk.readFile("HOST.TXT", text, sizeof text - 1);
        if (!disk.exists("HOST.TXT")) {
            SerialDMSeq.println("FILE NONE");
            return;
        }
        text[n] = 0;
        char *eol = strpbrk((char *)text, "\r\n");
        if (eol) {
            *eol = 0;
        }
        SerialDMSeq.print("FILE ");
        SerialDMSeq.print((unsigned)disk.fileSize("HOST.TXT"));
        SerialDMSeq.print(' ');
        SerialDMSeq.println((const char *)text);
    } else if (strcmp(command, "card") == 0) {
        SerialDMSeq.print("CARD ");
        SerialDMSeq.print(ccid.cardPresent() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(ccid.cardPowered() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println((unsigned long)ccid.apduCount());
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    disk.format("CONTRACT");
    disk.addTextFile("README.TXT", "CH32RV usb-classes " BENCH_BUILD_ID "\r\n");
    msc.vendorID("CH32RV");
    msc.productID("usb-classes");
    msc.mediaPresent(true);
    disk.attach(msc);
    ccid.onApdu(apdu);
    ccid.insertCard(atr, sizeof atr);
    vendor.onControlRequest(vendorRequest);
    config.product = "CH32RV usb-classes " BENCH_BUILD_ID;
    config.selfPowered = true;
    config.webusbEnabled = true;
    config.webusbUrl = "https://example.com/ch32rv";
    config.deviceInterfaceGuid = "{0B1C2D3E-4F50-6172-8394-A5B6C7D8E9F0}";
    USBDevice.begin(config);
}

void loop()
{
    uint8_t buffer[64];
    const size_t n = vendor.read(buffer, sizeof buffer);
    if (n) {
        size_t sent = 0;
        while (sent < n && vendor.waitWritable(n - sent, 500)) {
            sent += vendor.write(buffer + sent, n - sent);
        }
        vendor.flush();
    }
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
