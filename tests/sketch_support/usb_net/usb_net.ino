/* USB device contracts, network set (tests/peer/usb): a USB network adapter
 * (CDC-NCM) on its own, with the default DFU runtime - for a host that cannot
 * open NCM beside usb_contracts' other functions (an ESP32-S3 has eight host
 * channels).
 *
 *   ready <nonce>   READY <nonce> <build id> <USB started 0/1> <last error>
 *   net             NET <frames received> <frames of EtherType 0x88B5> <MAC as 12 hex digits>
 *   netsend         one broadcast frame (EtherType 0x88B5) -> NETSEND 1 / 0
 *   netburst <n>    n such frames back to back -> NETBURST <accepted> <frames
 *                   sent (stats().txFrames)> <IN block bytes> <transmit buffers>
 *                   <receive buffers>: two transmit buffers, so frames sent
 *                   while a block is out are gathered into the next
 *
 * Over USB, without a command: every frame of EtherType 0x88B5 that arrives
 * goes back out with its payload's first byte incremented.
 *
 * IP on the same function (the Network library's NetworkUsbNcm; the series
 * with 32 KB of RAM or more, USB_NET_IP - beside this sketch's 8 KB of
 * transmit and receive blocks):
 *
 *   ipstart         ncm.beginNetwork() at 192.168.7.1/24, a UDP echo on port 7
 *                   (lwIP takes the frames from here on: the raw echo stops):
 *                   IPSTART <1 / 0; 2 in a build without IP> <the address>
 *   ipstat          IPSTAT <UDP datagrams echoed> */
#include <Arduino.h>
#include <CH32RV.h>
#include "bench_console.h"
#include <CH32RVUsbDevice.h>
#include <string.h>
#include <stdlib.h>
#if defined(CH32RV_SERIES_CH32V205) || defined(CH32RV_SERIES_CH32X305) || defined(CH32RV_SERIES_CH32X315) || \
    defined(CH32RV_SERIES_CH32V303) || defined(CH32RV_SERIES_CH32V305) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || \
    defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417) || \
    defined(CH32RV_SERIES_CH32V208)
#define USB_NET_IP 1
#include <Network.h>
#include <NetworkUsbNcm.h>
static NetworkUDP udp;
static bool ipOn;
static uint32_t ipEchoed;
#endif

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

CH32RVUsbDeviceNet net;

static CH32RVUsbDeviceConfig config;
static volatile uint32_t netFrames, netOurs;
static uint8_t echo[64];
static volatile uint16_t echoLength;
static char line[80];
/* Two blocks each way (configure()): the class keeps one of its own. */
static uint8_t txRing[2 * 2048] __attribute__((aligned(4)));
static uint8_t rxRing[2 * 2048] __attribute__((aligned(4)));
static uint8_t used;

static void reply(const char *command)
{
    static const char hex[] = "0123456789ABCDEF";
    if (strncmp(command, "ready ", 6) == 0) {
        CONSOLE.print("READY ");
        CONSOLE.print(command + 6);
        CONSOLE.print(' ');
        CONSOLE.print(BENCH_BUILD_ID);
        CONSOLE.print(' ');
        CONSOLE.print(USBDevice.started() ? 1 : 0);
        CONSOLE.print(' ');
        CONSOLE.println(USBDevice.lastErrorName());
    } else if (strcmp(command, "net") == 0) {
        CONSOLE.print("NET ");
        CONSOLE.print((unsigned long)netFrames);
        CONSOLE.print(' ');
        CONSOLE.print((unsigned long)netOurs);
        CONSOLE.print(' ');
        for (uint8_t i = 0; i < 6; i++) {
            CONSOLE.print(hex[net.macAddress()[i] >> 4]);
            CONSOLE.print(hex[net.macAddress()[i] & 15]);
        }
        CONSOLE.println();
    } else if (strcmp(command, "netsend") == 0) {
        static uint8_t frame[60];
        memset(frame, 0xFF, 6);
        memcpy(frame + 6, net.macAddress(), 6);
        frame[11] ^= 1;
        frame[12] = 0x88;
        frame[13] = 0xB5;
        memcpy(frame + 14, "CH32RV usb-net", 14);
        CONSOLE.print("NETSEND ");
        CONSOLE.println(net.sendFrame(frame, sizeof frame, 500) ? 1 : 0);
    } else if (strncmp(command, "netburst ", 9) == 0) {
        static uint8_t frame[60];
        memset(frame, 0xFF, 6);
        memcpy(frame + 6, net.macAddress(), 6);
        frame[11] ^= 1;
        frame[12] = 0x88;
        frame[13] = 0xB5;
        const unsigned n = (unsigned)atoi(command + 9);
        unsigned accepted = 0;
        for (unsigned i = 0; i < n; i++) {
            frame[14] = (uint8_t)i;
            accepted += net.sendFrame(frame, sizeof frame, 500) ? 1 : 0;
        }
        delay(100);                              /* the last blocks go out */
        CONSOLE.printf("NETBURST %u %lu %u %u %u\r\n", accepted, (unsigned long)net.stats().txFrames,
                           (unsigned)net.ntbInCapacity(), (unsigned)net.txBufferCount(),
                           (unsigned)net.rxBufferCount());
    } else if (strcmp(command, "ipstart") == 0) {
#ifdef USB_NET_IP
        const bool ok = net.beginNetwork() && udp.begin(7);
        ipOn = ok;
        CONSOLE.print("IPSTART ");
        CONSOLE.print(ok ? 1 : 0);
        CONSOLE.print(' ');
        if (net.networkInterface()) {
            CONSOLE.println(net.networkInterface()->localIP());
        } else {
            CONSOLE.println("0.0.0.0");
        }
#else
        CONSOLE.println("IPSTART 2 0.0.0.0");
#endif
    } else if (strcmp(command, "ipstat") == 0) {
        CONSOLE.print("IPSTAT ");
#ifdef USB_NET_IP
        CONSOLE.println((unsigned long)ipEchoed);
#else
        CONSOLE.println(0);
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
    CH32RVUsbNetConfig netConfig;
    netConfig.txBuffers = netConfig.rxBuffers = 2;
    netConfig.txBuffer = txRing;
    netConfig.txBufferBytes = sizeof txRing;
    netConfig.rxBuffer = rxRing;
    netConfig.rxBufferBytes = sizeof rxRing;
    net.configure(netConfig);
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
#ifdef USB_TEST_CONTROLLER
    config.controller = USB_TEST_CONTROLLER;   /* run_hardware.py: the controller the record's usb names */
#endif
    USBDevice.begin(config);
}

void loop()
{
    deliverEvents();   /* the callbacks set in setup() run here */
    if (echoLength) {
        net.sendFrame(echo, echoLength, 500);
        echoLength = 0;
    }
#ifdef USB_NET_IP
    if (ipOn) {
        const int n = udp.parsePacket();
        if (n > 0) {
            uint8_t data[64];
            const int got = udp.read(data, sizeof data);
            udp.beginPacket(udp.remoteIP(), udp.remotePort());
            udp.write(data, got > 0 ? (size_t)got : 0u);
            udp.endPacket();
            ipEchoed++;
        }
    }
#endif
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
