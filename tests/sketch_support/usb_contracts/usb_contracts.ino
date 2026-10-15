/* USB device contracts (tests/instrumented/usb). The DUT is a composite
 * device - CDC, a keyboard and a vendor HID merged into one HID interface,
 * MIDI, a USB network adapter (NCM) and the default DFU runtime - named after this build, and the test
 * host drives it over USB while the debug-probe console (DMSEQ) carries the
 * commands that need the DUT's side:
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   keys            the keyboard types "ab"   -> KEYS 1 / KEYS 0
 *   replug          end() then begin(): the host sees an unplug and a plug
 *   net             NET <frames received> <MAC as 12 hex digits>
 *   netsend         one broadcast frame (EtherType 0x88B5) -> NETSEND 1 / 0
 *
 * Over USB, without a command: CDC echoes every byte, a vendor output report
 * comes back as an input report with its first byte incremented, and a MIDI
 * packet comes back with its note raised by 12. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbDeviceHidKeyboard keyboard;
CH32RVUsbDeviceHidVendor vendor;
CH32RVUsbDeviceMidi midi;
CH32RVUsbDeviceNet net;

static CH32RVUsbDeviceConfig config;
static uint8_t vendorData[63];
static uint16_t vendorLength;
static volatile bool vendorPending;
static volatile uint32_t netFrames;
static char line[80];
static uint8_t used;

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
    } else if (strcmp(command, "keys") == 0) {
        SerialDMSeq.print("KEYS ");
        SerialDMSeq.println(keyboard.write("ab", 0) ? 1 : 0);
    } else if (strcmp(command, "replug") == 0) {
        USBDevice.end();
        delay(500);
        SerialDMSeq.print("REPLUG ");
        SerialDMSeq.println(USBDevice.begin(config) ? 1 : 0);
    } else if (strcmp(command, "net") == 0) {
        static const char hex[] = "0123456789ABCDEF";
        SerialDMSeq.print("NET ");
        SerialDMSeq.print((unsigned long)netFrames);
        SerialDMSeq.print(' ');
        for (uint8_t i = 0; i < 6; i++) {
            SerialDMSeq.print(hex[net.macAddress()[i] >> 4]);
            SerialDMSeq.print(hex[net.macAddress()[i] & 15]);
        }
        SerialDMSeq.println();
    } else if (strcmp(command, "netsend") == 0) {
        static uint8_t frame[60];
        memset(frame, 0xFF, 6);
        memcpy(frame + 6, net.macAddress(), 6);
        frame[11] ^= 1;
        frame[12] = 0x88;
        frame[13] = 0xB5;
        memcpy(frame + 14, "CH32RV usb-contract", 19);
        SerialDMSeq.print("NETSEND ");
        SerialDMSeq.println(net.sendFrame(frame, sizeof frame, 500) ? 1 : 0);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    vendor.onOutputReport([](const CH32RVUsbDeviceHidReport &report) {
        vendorLength = report.length > sizeof vendorData ? sizeof vendorData : report.length;
        memcpy(vendorData, report.data, vendorLength);
        vendorData[0]++;
        vendorPending = true;
    });
    net.onFrame([](const uint8_t *, size_t) { netFrames++; });
    config.product = "CH32RV usb-contract " BENCH_BUILD_ID;
    config.selfPowered = true;       /* the bench powers the DUT; VBUS is not used */
    USBDevice.begin(config);
}

void loop()
{
    while (SerialUSB.available()) {
        SerialUSB.write(SerialUSB.read());
    }
    if (vendorPending) {
        vendorPending = false;
        vendor.sendInput(vendorData, vendorLength);
    }
    CH32RVUsbDeviceMidiPacket packet;
    while (midi.readPacket(packet)) {
        packet.byte2 = (uint8_t)(packet.byte2 + 12);
        midi.writePacket(packet);
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
