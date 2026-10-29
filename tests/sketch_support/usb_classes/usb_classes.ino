/* USB device contracts, second set (tests/instrumented/usb): the classes that
 * do not fit beside usb_contracts' on X035's seven endpoints - vendor bulk
 * with WebUSB and the Microsoft OS 2.0 descriptors, mass storage (a FAT RAM
 * disk) and a smart card reader - with the default DFU runtime.
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   file            FILE <size> <first line of HOST.TXT>, or FILE NONE
 *   card            CARD <present 0/1> <powered 0/1> <APDUs answered>
 *   events          EVENTS <configured> <resets> <observed requests> <ACKs>
 *                   <configuration length> <VID:PID from deviceDescriptor()>
 *                   <HID report length> - onBusEvent(), onAnyControlRequest()
 *                   and the descriptor getters
 *   vmode fifo|direct   VMODE <mode> <direct transfers completed> <peeks that matched
 *                   the next byte read> <peeks that did not>: how the
 *                   vendor bulk echo goes - read() / write() through the class
 *                   FIFOs (the default), or onRxData() handing each OUT
 *                   transfer over and writeDirect() sending it back, its
 *                   completion counted by a capturing onDirectTxComplete() lambda
 *
 * The vendor interface is 0xFF / 0x4F / 0x45 (setInterfaceProtocol()).
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

/* The direct echo: one OUT transfer at a time, back as one IN transfer. */
static uint32_t configuredEvents, resetEvents, observed, acks;

static uint8_t echo[64];
static volatile bool echoBusy;
static uint32_t directDone;
static uint32_t peeksMatched, peeksMissed;   /* peek() against the byte read() returned next */
static bool directMode;

static void direct_rx(const uint8_t *data, size_t length)
{
    if (echoBusy || length > sizeof echo) {
        return;                                     /* the host sends one packet and waits for it */
    }
    memcpy(echo, data, length);
    echoBusy = (bool)vendor.writeDirect(echo, length);
}

static void set_mode(bool direct)
{
    directMode = direct;
    if (direct) {
        uint32_t *done = &directDone;
        vendor.onDirectTxComplete([done](const CH32RVUsbTransferResult &result) {
            if (result.status == CH32RVUsbTransferStatus::Success) {
                (*done)++;
            }
            echoBusy = false;
        });
        vendor.onRxData(direct_rx);
    } else {
        vendor.onRxData(nullptr);
        vendor.onDirectTxComplete(nullptr);
    }
}

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
    } else if (strcmp(command, "events") == 0) {
        const uint8_t *cfg = USBDevice.configurationDescriptor(0);
        const uint16_t cfgLen = cfg ? (uint16_t)(cfg[2] | cfg[3] << 8) : 0;
        const uint8_t *dev = USBDevice.deviceDescriptor();
        SerialDMSeq.printf("EVENTS %lu %lu %lu %lu %u %04x:%04x %u\r\n", (unsigned long)configuredEvents,
                           (unsigned long)resetEvents, (unsigned long)observed, (unsigned long)acks, cfgLen,
                           dev ? (unsigned)(dev[8] | dev[9] << 8) : 0u, dev ? (unsigned)(dev[10] | dev[11] << 8) : 0u,
                           USBDevice.hidReportDescriptorLength());
    } else if (strncmp(command, "vmode ", 6) == 0) {
        set_mode(strcmp(command + 6, "direct") == 0);
        SerialDMSeq.print("VMODE ");
        SerialDMSeq.print(directMode ? "direct " : "fifo ");
        SerialDMSeq.print((unsigned long)directDone);
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned long)peeksMatched);
        SerialDMSeq.print(' ');
        SerialDMSeq.println((unsigned long)peeksMissed);
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
    vendor.setInterfaceProtocol(0x4F, 0x45);
    config.product = "CH32RV usb-classes " BENCH_BUILD_ID;
    config.selfPowered = true;
    config.webusbEnabled = true;
    config.webusbUrl = "https://example.com/ch32rv";
    config.deviceInterfaceGuid = "{0B1C2D3E-4F50-6172-8394-A5B6C7D8E9F0}";
    USBDevice.onBusEvent([](CH32RVUsbBusEvent e) {
        if (e == CH32RVUsbBusEvent::Configured) {
            configuredEvents++;
        } else if (e == CH32RVUsbBusEvent::Reset) {
            resetEvents++;
        }
    });
    USBDevice.onAnyControlRequest([](const CH32RVUsbDeviceControlRequestInfo &r) {
        observed++;
        if (r.stage == CH32RV_USB_DEVICE_CONTROL_STAGE_ACK) {
            acks++;
        }
    });
    USBDevice.begin(config);
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    uint8_t buffer[64];
    /* read() only after peek() saw data: both run the USB task, and a packet
     * landing in read()'s would go uncounted either way. */
    const int ahead = directMode ? -1 : vendor.peek();
    const size_t n = ahead < 0 ? 0 : vendor.read(buffer, sizeof buffer);
    if (n && ahead >= 0) {
        if (buffer[0] == (uint8_t)ahead) {
            peeksMatched++;
        } else {
            peeksMissed++;
        }
    }
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
