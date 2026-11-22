/* Another CH32RV board as the USB network device for tests/peer/usb/host/net: a CDC-NCM function
 * (CH32RVUsbDeviceNet) as raw Ethernet frames. The DUT (CH32RVUsbHost) is the host; this board runs
 * CH32RVUsbDevice on the controller the bench record names (build_opt.h: PEER_USB_CONTROLLER, written by
 * run_hardware.py).
 *
 * Commands, one per line on the debug probe's console (SerialDMSeq); answers start "CD " (peer_s3's "S3D "):
 *   ?                    CD READY <configured 0/1> <link 0/1>
 *   netsend <n>          CD NETSENT <frames sent>   (broadcast, EtherType 0x88B5, the index in the payload)
 *   netstat              CD NETRX <frames> <frames of 0x88B5>
 *   netmac               CD NETMAC <its MAC, 12 hex>
 *   ipstart              CD IPSTART <ok> <address>   (the IP stack: 192.168.7.1, a DHCP server, a TCP echo on 7;
 *                        CD IPSTART 2 - where the Network library does not build) */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <CH32RVUsbDevice.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* The IP stack only where the Network library builds (its lwipopts.h: the series with an Ethernet MAC). */
#if defined(CH32RV_SERIES_CH32V208) || defined(CH32RV_PART_CH32V203RBT6) || defined(CH32RV_SERIES_CH32V307) || \
    defined(CH32RV_SERIES_CH32V317) || defined(CH32RV_SERIES_CH32V407) || defined(CH32RV_SERIES_CH32V467) || \
    defined(CH32RV_SERIES_CH32H415) || defined(CH32RV_SERIES_CH32H416) || defined(CH32RV_SERIES_CH32H417)
#define PEER_NET_IP 1
#include <Network.h>
#include <NetworkUsbNcm.h>
#endif

#ifndef PEER_USB_CONTROLLER
#define PEER_USB_CONTROLLER CH32RVUsbController::Auto
#endif

CH32RVUsbDeviceNet net;
static volatile uint32_t frames, ours;
#if defined(PEER_NET_IP)
static NetworkServer echo(7);
static NetworkClient echoClient;
#endif
static bool ipUp;
static char line[96];
static size_t used;

static void say(const char *format, ...)
{
    char text[160];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof text, format, ap);
    va_end(ap);
    SerialDMSeq.print(text);
}

static void run(char *cmd)
{
    char *arg = strchr(cmd, ' ');
    if (arg) *arg++ = '\0';
    else arg = cmd + strlen(cmd);
    if (!strcmp(cmd, "?")) say("CD READY %u %u\n", USBDevice.ready() ? 1 : 0, net.linkUp() ? 1 : 0);
    else if (!strcmp(cmd, "netsend")) {
        unsigned long n = strtoul(arg, nullptr, 10);
        if (!n) n = 1;
        unsigned long sent = 0;
        for (unsigned long i = 0; i < n; i++) {
            uint8_t f[60] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x02, 0x43, 0x48, 0x33, 0x32, 0x01, 0x88, 0xB5};
            f[14] = (uint8_t)i;
            f[15] = (uint8_t)(i >> 8);
            sent += net.sendFrame(f, sizeof f, 500) ? 1 : 0;
        }
        say("CD NETSENT %lu\n", sent);
    } else if (!strcmp(cmd, "netstat")) say("CD NETRX %lu %lu\n", (unsigned long)frames, (unsigned long)ours);
    else if (!strcmp(cmd, "netmac")) {
        const uint8_t *m = net.macAddress();
        say("CD NETMAC %02X%02X%02X%02X%02X%02X\n", m[0], m[1], m[2], m[3], m[4], m[5]);
    } else if (!strcmp(cmd, "ipstart")) {
#if defined(PEER_NET_IP)
        if (!ipUp) ipUp = net.beginNetwork();
        if (ipUp) echo.begin();
        say("CD IPSTART %u %s\n", ipUp ? 1 : 0, net.localIP().toString().c_str());
#else
        say("CD IPSTART 2 -\n");
#endif
    } else say("CD UNKNOWN %s\n", cmd);
}

void setup()
{
    SerialDMSeq.begin(115200);
    net.onFrame([](const uint8_t *data, size_t length) {
        frames++;
        if (length >= 14 && data[12] == 0x88 && data[13] == 0xB5) ours++;
    });
#if defined(PEER_NET_IP)
    net.ipConfig(IPAddress(192, 168, 7, 1), IPAddress(192, 168, 7, 1), IPAddress(255, 255, 255, 0));
    net.dhcpServer(true);   /* for ipstart; nothing runs until beginNetwork() */
#endif
    CH32RVUsbDeviceConfig config;
    config.manufacturer = "ch32rv-tests";
    config.product = "CH32RV peer net";
    config.serialNumber = "CH32RVPEERNET";
    config.dfuRuntime = false;
    config.controller = PEER_USB_CONTROLLER;
    const bool began = USBDevice.begin(config);
    say("CD BOOTED %u %s\n", began ? 1 : 0, USBDevice.lastErrorName());
}

void loop()
{
    deliverEvents();
#if defined(PEER_NET_IP)
    if (ipUp) {
        if (!echoClient || !echoClient.connected()) echoClient = echo.accept();
        while (echoClient && echoClient.available()) {
            uint8_t b[64];
            const int n = echoClient.read(b, sizeof b);
            if (n > 0) echoClient.write(b, n);
        }
    }
#endif
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
