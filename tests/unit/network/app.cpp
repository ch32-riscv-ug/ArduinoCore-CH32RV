/* The host test sketch: the Network library on a tap device, serving what
 * test_network_host.py exercises. Only the library's public API is used.
 *
 *   STACK_MODE=static  10.9.0.2/24, gateway and DNS 10.9.0.1
 *   STACK_MODE=dhcpc   DHCP (the test runs dnsmasq)
 *   STACK_MODE=dhcps   static 10.9.0.2 and a DHCP server: 10.9.0.3-6, DNS 10.9.0.53 offered, no router
 *   STACK_MODE=ncm     the same as dhcps through CH32RVUsbDeviceNet's EspUsbDevice-style calls,
 *                      on host/CH32RVUsbDevice.h's stand-in for the USB function
 *
 *   stdout "IP a.b.c.d" once there is an address
 *   UDP 7 echo; TCP 7 echo; TCP 9 discard ("DISCARD <bytes>" when the peer closes);
 *   TCP 19 sends SOURCE_BYTES of PATTERN and closes
 *   UDP 9999: "C <port>" connects back and sends 256 KiB of PATTERN -> "CDONE 262144" / "CFAIL"
 *             "D <name>" -> "DNS a.b.c.d" / "DNSFAIL"
 */
#include <Network.h>
#include <NetworkUsbNcm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host/TapInterface.h"
#include "host/link.h"

static TapInterface tap;
static CH32RVUsbDeviceNet ncm;
static NetworkInterface *ifc = &tap;
static NetworkUDP udpEcho, command;
static NetworkServer echoServer(7, 4), discardServer(9, 2), sourceServer(19, 2);
static NetworkClient echoClients[4];
static NetworkClient discardClient;
static uint32_t discarded;
static bool reported;

void setup()
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    const char *mode = getenv("STACK_MODE");
    if (mode && !strcmp(mode, "ncm")) {
        /* As an EspUsbDevice sketch would. */
        ncm.ipConfig(IPAddress(10, 9, 0, 2), IPAddress(10, 9, 0, 1), IPAddress(255, 255, 255, 0));
        ncm.dhcpServer(true);
        ncm.dhcpDns(IPAddress(10, 9, 0, 53));
        ncm.begin();
        if (!ncm.beginNetwork()) {
            printf("BEGIN FAIL\n");
            exit(1);
        }
        ifc = ncm.networkInterface();
        ifc->dnsIP(0, IPAddress(10, 9, 0, 1));   /* the sketch's own lookups go to the test's dnsmasq */
    } else {
        if (mode && (!strcmp(mode, "static") || !strcmp(mode, "dhcps"))) {
            tap.config(IPAddress(10, 9, 0, 2), IPAddress(10, 9, 0, 1), IPAddress(255, 255, 255, 0), IPAddress(10, 9, 0, 1));
        }
        if (mode && !strcmp(mode, "dhcps")) {
            tap.dhcpDns(IPAddress(10, 9, 0, 53));
            if (!tap.dhcpServer(true, 4)) {
                printf("DHCPS FAIL\n");
            }
        }
        static const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
        if (!tap.begin("tap0", mac)) {
            printf("BEGIN FAIL\n");
            exit(1);
        }
    }
    udpEcho.begin(7);
    command.begin(9999);
    echoServer.begin();
    discardServer.begin();
    sourceServer.begin();
}

static void serveEcho()
{
    NetworkClient incoming = echoServer.accept();
    if (incoming) {
        for (auto &c : echoClients) {
            if (!c.connected()) {
                c = incoming;
                break;
            }
        }
    }
    uint8_t buf[1460];
    for (auto &c : echoClients) {
        if (!c) {
            continue;
        }
        if (c.available() > 0) {
            const int n = c.read(buf, sizeof buf);
            if (n > 0 && c.write(buf, (size_t)n) != (size_t)n) {
                printf("ECHO SHORT WRITE\n");
            }
        } else if (!c.connected()) {
            c.stop();
        }
    }
}

static void serveDiscard()
{
    if (!discardClient) {
        discardClient = discardServer.accept();
        discarded = 0;
        return;
    }
    uint8_t buf[1460];
    while (discardClient.available() > 0) {
        const int n = discardClient.read(buf, sizeof buf);
        if (n > 0) {
            discarded += (uint32_t)n;
        }
    }
    if (!discardClient.connected()) {
        printf("DISCARD %u\n", (unsigned)discarded);
        discardClient.stop();
    }
}

static bool sendPattern(NetworkClient &c, uint32_t total)
{
    uint8_t buf[1460];
    for (uint32_t off = 0; off < total;) {
        const uint32_t n = total - off < sizeof buf ? total - off : (uint32_t)sizeof buf;
        for (uint32_t i = 0; i < n; i++) {
            buf[i] = PATTERN(off + i);
        }
        const size_t w = c.write(buf, n);
        if (w == 0) {
            return false;
        }
        off += (uint32_t)w;
    }
    return true;
}

static void serveSource()
{
    NetworkClient c = sourceServer.accept();
    if (c) {
        c.setTimeout(10000);
        if (!sendPattern(c, SOURCE_BYTES)) {
            printf("SOURCE FAIL\n");
        }
        c.stop();
    }
}

static void reply(const char *text)
{
    command.beginPacket();
    command.write((const uint8_t *)text, strlen(text));
    command.endPacket();
}

static void serveCommands()
{
    if (!command.parsePacket()) {
        return;
    }
    char line[80] = {0};
    command.read((unsigned char *)line, sizeof line - 1);
    char *nl = strpbrk(line, "\r\n");
    if (nl) {
        *nl = 0;
    }
    if (line[0] == 'C') {
        NetworkClient c;
        c.setTimeout(10000);
        if (c.connect(command.remoteIP(), (uint16_t)atoi(line + 2)) && sendPattern(c, 256 * 1024)) {
            c.stop();
            reply("CDONE 262144");
        } else {
            reply("CFAIL");
        }
    } else if (line[0] == 'D') {
        IPAddress ip;
        char text[40];
        if (Network.hostByName(line + 2, ip)) {
            snprintf(text, sizeof text, "DNS %s", ip.toString().c_str());
        } else {
            snprintf(text, sizeof text, "DNSFAIL");
        }
        reply(text);
    }
}

void loop()
{
    if (!reported && ifc->hasIP()) {
        reported = true;
        printf("IP %s\n", ifc->localIP().toString().c_str());
    }
    if (udpEcho.parsePacket() > 0) {
        uint8_t buf[1500];
        const int got = udpEcho.read(buf, sizeof buf);
        udpEcho.beginPacket();
        udpEcho.write(buf, (size_t)got);
        udpEcho.endPacket();
    }
    serveEcho();
    serveDiscard();
    serveSource();
    serveCommands();
}
