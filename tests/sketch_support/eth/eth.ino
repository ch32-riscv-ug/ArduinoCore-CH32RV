/* Ethernet contracts (tests/instrumented/eth): the CH32H417's ETH with DHCP,
 * TCP and UDP servers, and HTTP / HTTPS clients. Console on SerialDMSeq:
 *
 *   ready <nonce>             READY <nonce> <build id> <ETH.begin 0/1> <MAC as 12 hex digits>
 *   ip                        IP <address> <link Mbit/s> <full 0/1>
 *   pin <ip> <mac>            PIN <0 or lwIP error>: a static ARP entry (a host that
 *                             answers ARP on two NICs, its Wi-Fi as well)
 *   cab / cal <pem line> / cae   a root certificate, line by line -> CA <bytes>
 *   http <ip> <port> <path>   HTTP <status> <body bytes> <CRC-32 hex> <ms>
 *   https <ip> <port> <path> <c|i>   c: verify against the CA given, i: no check
 *                             HTTPS <status> <body bytes> <CRC-32 hex> <TLS version hex> <handshake ms> <error>
 *   sink                      SINK <bytes> <ms> of the last connection on port 5001
 *   stats                     STATS <rx> <tx> <rx errors> <tx busy> <link ups> <PHY resets> <clock restarts>
 *
 * Servers, always: TCP 7 echoes, TCP 5001 takes and counts, TCP 5002 sends
 * bytes i & 0xFF (i from 0) until the peer closes, UDP 9000 echoes. */
#include <Arduino.h>
#include <SerialDMSeq.h>
#include <Network.h>
#include <NetworkClientSecure.h>
#include <lwip/etharp.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static NetworkServer echoServer(7), sinkServer(5001), sourceServer(5002);
static NetworkClient echoClient, sinkClient, sourceClient;
static NetworkUDP udp;
static bool begun;
static uint8_t buf[1460];
static uint32_t sinkBytes, sinkStart, sinkLastBytes, sinkLastMs, sourceIndex;
static char ca[4096];
static size_t caUsed;
static char line[100];
static uint8_t used;

static uint32_t crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* GET on a connected client: the status and the body's length and CRC-32. */
static void get(Client &c, const char *host, const char *path, uint32_t &status, uint32_t &bytes, uint32_t &crc)
{
    c.print("GET ");
    c.print(path);
    c.print(" HTTP/1.1\r\nHost: ");
    c.print(host);
    c.print("\r\nConnection: close\r\n\r\n");
    status = bytes = crc = 0;
    bool head = true;
    char hl[64];
    uint8_t hn = 0;
    const uint32_t t0 = millis();
    while ((c.connected() || c.available()) && millis() - t0 < 30000) {
        if (head) {
            const int ch = c.read();
            if (ch < 0) {
                continue;
            }
            if (ch == '\n') {
                hl[hn] = 0;
                if (!status && strncmp(hl, "HTTP/1.", 7) == 0) {
                    status = (uint32_t)atoi(hl + 9);
                }
                if (hn <= 1) {
                    head = false;
                }
                hn = 0;
            } else if (hn < sizeof hl - 1) {
                hl[hn++] = (char)ch;
            }
        } else {
            const int n = c.read(buf, sizeof buf);
            if (n > 0) {
                bytes += (uint32_t)n;
                crc = crc32(crc, buf, (size_t)n);
            }
        }
    }
    c.stop();
}

static void reply(char *command)
{
    if (strncmp(command, "ready ", 6) == 0) {
        static const char hex[] = "0123456789ABCDEF";
        uint8_t mac[6];
        ETH.macAddress(mac);
        SerialDMSeq.print("READY ");
        SerialDMSeq.print(command + 6);
        SerialDMSeq.print(' ');
        SerialDMSeq.print(BENCH_BUILD_ID);
        SerialDMSeq.print(begun ? " 1 " : " 0 ");
        for (uint8_t i = 0; i < 6; i++) {
            SerialDMSeq.print(hex[mac[i] >> 4]);
            SerialDMSeq.print(hex[mac[i] & 15]);
        }
        SerialDMSeq.println();
    } else if (strcmp(command, "ip") == 0) {
        SerialDMSeq.print("IP ");
        SerialDMSeq.print(ETH.hasIP() ? ETH.localIP().toString() : String("0.0.0.0"));
        SerialDMSeq.print(' ');
        SerialDMSeq.print(ETH.linkSpeed());
        SerialDMSeq.print(' ');
        SerialDMSeq.println(ETH.fullDuplex() ? 1 : 0);
    } else if (strncmp(command, "pin ", 4) == 0) {
        char ip[20];
        unsigned m[6];
        IPAddress a;
        if (sscanf(command + 4, "%19s %x:%x:%x:%x:%x:%x", ip, &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 7 ||
            !a.fromString(ip)) {
            SerialDMSeq.println("ERROR ARGS");
            return;
        }
        ip4_addr_t i4;
        ip4_addr_set_u32(&i4, (uint32_t)a);
        struct eth_addr e;
        for (int k = 0; k < 6; k++) {
            e.addr[k] = (uint8_t)m[k];
        }
        SerialDMSeq.print("PIN ");
        SerialDMSeq.println((int)etharp_add_static_entry(&i4, &e));
    } else if (strcmp(command, "cab") == 0) {
        caUsed = 0;
        ca[0] = 0;
        SerialDMSeq.println("CAB");
    } else if (strncmp(command, "cal ", 4) == 0) {
        const size_t n = strlen(command + 4);
        if (caUsed + n + 2 < sizeof ca) {
            memcpy(ca + caUsed, command + 4, n);
            caUsed += n;
            ca[caUsed++] = '\n';
            ca[caUsed] = 0;
        }
        SerialDMSeq.println("CAL");
    } else if (strcmp(command, "cae") == 0) {
        SerialDMSeq.print("CA ");
        SerialDMSeq.println((unsigned long)caUsed);
    } else if (strncmp(command, "http ", 5) == 0 || strncmp(command, "https ", 6) == 0) {
        const bool tls = command[4] == 's';
        char ip[20], path[48], mode[4] = "c";
        unsigned port = 0;
        IPAddress a;
        if (sscanf(command + (tls ? 6 : 5), "%19s %u %47s %3s", ip, &port, path, mode) < 3 || !a.fromString(ip)) {
            SerialDMSeq.println("ERROR ARGS");
            return;
        }
        uint32_t status = 0, bytes = 0, crc = 0;
        char out[96];
        if (!tls) {
            NetworkClient c;
            const uint32_t t0 = millis();
            if (c.connect(a, (uint16_t)port, 5000)) {
                get(c, ip, path, status, bytes, crc);
            }
            snprintf(out, sizeof out, "HTTP %lu %lu %08lx %lu", (unsigned long)status, (unsigned long)bytes,
                     (unsigned long)crc, (unsigned long)(millis() - t0));
        } else {
            NetworkClientSecure c;
            if (mode[0] == 'i') {
                c.setInsecure();
            } else {
                c.setCACert(ca);
            }
            const uint32_t t0 = millis();
            const int ok = c.connect(a, (uint16_t)port, 10000);
            const uint32_t hs = millis() - t0;
            const uint16_t version = c.tlsVersion();
            const int error = c.lastError();
            if (ok) {
                get(c, ip, path, status, bytes, crc);
            }
            snprintf(out, sizeof out, "HTTPS %lu %lu %08lx %04x %lu %d", (unsigned long)status, (unsigned long)bytes,
                     (unsigned long)crc, version, (unsigned long)hs, error);
        }
        SerialDMSeq.println(out);
    } else if (strcmp(command, "sink") == 0) {
        SerialDMSeq.print("SINK ");
        SerialDMSeq.print((unsigned long)sinkLastBytes);
        SerialDMSeq.print(' ');
        SerialDMSeq.println((unsigned long)sinkLastMs);
    } else if (strcmp(command, "stats") == 0) {
        char out[96];
        snprintf(out, sizeof out, "STATS %lu %lu %lu %lu %lu %lu %lu", (unsigned long)ETH.framesReceived(),
                 (unsigned long)ETH.framesSent(), (unsigned long)ETH.receiveErrors(), (unsigned long)ETH.transmitBusy(),
                 (unsigned long)ETH.linkChanges(), (unsigned long)ETH.phyResets(), (unsigned long)ETH.clockRestarts());
        SerialDMSeq.println(out);
    } else {
        SerialDMSeq.println("ERROR COMMAND");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    begun = ETH.begin();
    echoServer.begin();
    sinkServer.begin();
    sourceServer.begin();
    udp.begin(9000);
}

void loop()
{
    if (!echoClient.connected()) {
        echoClient = echoServer.accept();
    }
    while (echoClient.connected() && echoClient.available()) {
        const int n = echoClient.read(buf, sizeof buf);
        if (n > 0) {
            echoClient.write(buf, (size_t)n);
        }
    }
    if (!sinkClient.connected()) {
        if (sinkStart) {
            sinkLastBytes = sinkBytes;
            sinkLastMs = millis() - sinkStart;
            sinkStart = 0;
        }
        sinkClient = sinkServer.accept();
        if (sinkClient.connected()) {
            sinkBytes = 0;
            sinkStart = millis() | 1u;
        }
    }
    while (sinkClient.connected() && sinkClient.available()) {
        const int n = sinkClient.read(buf, sizeof buf);
        if (n <= 0) {
            break;
        }
        sinkBytes += (uint32_t)n;
    }
    if (!sourceClient.connected()) {
        sourceClient = sourceServer.accept();
        sourceIndex = 0;
    }
    if (sourceClient.connected()) {
        for (size_t i = 0; i < sizeof buf; i++) {
            buf[i] = (uint8_t)(sourceIndex + i);
        }
        sourceIndex += sourceClient.write(buf, sizeof buf);
    }
    if (udp.parsePacket() > 0) {
        const int n = udp.read(buf, sizeof buf);
        udp.beginPacket(udp.remoteIP(), udp.remotePort());
        udp.write(buf, n > 0 ? (size_t)n : 0u);
        udp.endPacket();
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
