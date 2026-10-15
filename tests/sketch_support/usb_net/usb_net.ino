/* USB device contracts, network set (tests/peer/usb): a USB network adapter
 * (CDC-NCM) on its own, with the default DFU runtime - for a host that cannot
 * open NCM beside usb_contracts' other functions (an ESP32-S3 has eight host
 * channels).
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   net             NET <frames received> <frames of EtherType 0x88B5> <MAC as 12 hex digits>
 *   netsend         one broadcast frame (EtherType 0x88B5) -> NETSEND 1 / 0
 *
 * Over USB, without a command: every frame of EtherType 0x88B5 that arrives
 * goes back out with its payload's first byte incremented. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbDeviceNet net;

static CH32RVUsbDeviceConfig config;
static volatile uint32_t netFrames, netOurs;
static uint8_t echo[64];
static volatile uint16_t echoLength;
static char line[80];
static uint8_t used;

static void reply(const char *command)
{
    static const char hex[] = "0123456789ABCDEF";
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(USBDevice.started() ? 1 : 0);
        SerialDMSeq.print(' ');
        SerialDMSeq.println(USBDevice.lastErrorName());
    } else if (strcmp(command, "net") == 0) {
        SerialDMSeq.print("NET ");
        SerialDMSeq.print((unsigned long)netFrames);
        SerialDMSeq.print(' ');
        SerialDMSeq.print((unsigned long)netOurs);
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
        memcpy(frame + 14, "CH32RV usb-net", 14);
        SerialDMSeq.print("NETSEND ");
        SerialDMSeq.println(net.sendFrame(frame, sizeof frame, 500) ? 1 : 0);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    net.onFrame([](const uint8_t *frame, size_t length) {
        netFrames++;
        if (length >= 15 && frame[12] == 0x88 && frame[13] == 0xB5) {
            netOurs++;
            if (!echoLength) {
                const size_t n = length > sizeof echo ? sizeof echo : length;
                memcpy(echo, frame, n);
                echo[14]++;
                echoLength = (uint16_t)n;
            }
        }
    });
    config.product = "CH32RV usb-net " BENCH_BUILD_ID;
    config.selfPowered = true;
    USBDevice.begin(config);
}

void loop()
{
    if (echoLength) {
        net.sendFrame(echo, echoLength, 500);
        echoLength = 0;
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
