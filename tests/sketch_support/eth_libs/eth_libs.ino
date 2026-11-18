/* Widely used Arduino network libraries on this core's Ethernet
 * (tests/instrumented/eth/test_eth_libs.py): PubSubClient and
 * ArduinoMqttClient (MQTT), ArduinoHttpClient (HTTP client) and aWOT (HTTP
 * server), unchanged, on NetworkClient / NetworkServer. The case's
 * sketch.yaml pins their versions (run_hardware.py CASE_LIBRARIES). Console
 * on SerialDMSeq:
 *
 *   ready <nonce>                 READY <nonce> <build id> <ETH.begin 0/1>
 *   ip                            IP <address>
 *   pin <ip> <mac>                PIN <0 or lwIP error>: a static ARP entry
 *   psc <ip> <port> <id>          PubSubClient: connect as <id>, subscribe <id>/in, publish "hello" to
 *                                 <id>/out, wait 5 s for a message on <id>/in:
 *                                 PSC <connected 0/1> <subscribed 0/1> <published 0/1> <payload or ->
 *   amc <ip> <port> <id>          the same with ArduinoMqttClient: AMC ...
 *   hget <ip> <port> <path>       ArduinoHttpClient GET: HGET <status> <body bytes> <CRC-32 hex>
 *   hpost <ip> <port> <path> <body>   POST text/plain: HPOST <status> <response body>
 *
 * Always: aWOT on TCP 80, GET /hello -> "hello from ch32rv", POST /echo -> the body. */
#include <Arduino.h>
#include <CH32RV.h>
#include <SerialDMSeq.h>
#include <Network.h>
#include <PubSubClient.h>
#include <ArduinoMqttClient.h>
#include <ArduinoHttpClient.h>
#include <aWOT.h>
#include <lwip/etharp.h>
#include <string.h>

#ifndef BENCH_BUILD_ID
#error BENCH_BUILD_ID must identify this compilation
#endif

static bool begun;
static char line[160];
static uint8_t used;
static uint8_t buf[1024];

static NetworkServer webServer(80);
static Application app;

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

/* ------------------------------------------------------------ MQTT */
static char received[64];
static bool got;

static void onPubSub(char *topic, byte *payload, unsigned int length)
{
    (void)topic;
    length = length < sizeof received - 1 ? length : sizeof received - 1;
    memcpy(received, payload, length);
    received[length] = 0;
    got = true;
}

static void psc(const char *ip, uint16_t port, const char *id)
{
    NetworkClient net;
    PubSubClient mqtt(net);
    IPAddress a;
    a.fromString(ip);
    mqtt.setServer(a, port);
    mqtt.setCallback(onPubSub);
    got = false;
    char in[40], out[40];
    snprintf(in, sizeof in, "%s/in", id);
    snprintf(out, sizeof out, "%s/out", id);
    const bool connected = mqtt.connect(id);
    const bool subscribed = connected && mqtt.subscribe(in);
    const bool published = connected && mqtt.publish(out, "hello");
    const uint32_t t0 = millis();
    while (connected && !got && millis() - t0 < 5000) {
        mqtt.loop();
        delay(1);
    }
    mqtt.disconnect();
    SerialDMSeq.printf("PSC %d %d %d %s\n", connected, subscribed, published, got ? received : "-");
}

static void amc(const char *ip, uint16_t port, const char *id)
{
    NetworkClient net;
    MqttClient mqtt(net);
    IPAddress a;
    a.fromString(ip);
    mqtt.setId(id);
    char in[40], out[40];
    snprintf(in, sizeof in, "%s/in", id);
    snprintf(out, sizeof out, "%s/out", id);
    const bool connected = mqtt.connect(a, port);
    const bool subscribed = connected && mqtt.subscribe(in);
    bool published = false;
    if (connected && mqtt.beginMessage(out)) {
        mqtt.print("hello");
        published = mqtt.endMessage();
    }
    bool have = false;
    size_t n = 0;
    const uint32_t t0 = millis();
    while (connected && !have && millis() - t0 < 5000) {
        if (mqtt.parseMessage() > 0) {
            while (mqtt.available() && n < sizeof received - 1) {
                received[n++] = (char)mqtt.read();
            }
            have = true;
        }
        delay(1);
    }
    received[n] = 0;
    mqtt.stop();
    SerialDMSeq.printf("AMC %d %d %d %s\n", connected, subscribed, published, have ? received : "-");
}

/* ------------------------------------------------------------ HTTP client */
static void hget(const char *ip, uint16_t port, const char *path)
{
    NetworkClient net;
    HttpClient http(net, ip, port);
    http.get(path);
    const int status = http.responseStatusCode();
    http.skipResponseHeaders();
    uint32_t bytes = 0, crc = 0;
    const uint32_t t0 = millis();
    while ((http.connected() || http.available()) && millis() - t0 < 30000) {
        const int n = http.read(buf, sizeof buf);
        if (n > 0) {
            bytes += (uint32_t)n;
            crc = crc32(crc, buf, (size_t)n);
        }
    }
    http.stop();
    SerialDMSeq.printf("HGET %d %lu %08lx\n", status, (unsigned long)bytes, (unsigned long)crc);
}

static void hpost(const char *ip, uint16_t port, const char *path, const char *body)
{
    NetworkClient net;
    HttpClient http(net, ip, port);
    http.post(path, "text/plain", body);
    const int status = http.responseStatusCode();
    const String answer = http.responseBody();
    http.stop();
    SerialDMSeq.printf("HPOST %d %s\n", status, answer.length() ? answer.c_str() : "-");
}

/* ------------------------------------------------------------ HTTP server */
static void hello(Request &req, Response &res)
{
    (void)req;
    res.set("Content-Type", "text/plain");
    res.print("hello from ch32rv");
}

static void echo(Request &req, Response &res)
{
    res.set("Content-Type", "text/plain");
    int c;
    while ((c = req.read()) >= 0) {
        res.write((uint8_t)c);
    }
}

/* ------------------------------------------------------------ console */
static void reply(char *command)
{
    char ip[20], path[64];
    unsigned port = 0;
    if (strncmp(command, "ready ", 6) == 0) {
        SerialDMSeq.printf("READY %s %s %d\n", command + 6, BENCH_BUILD_ID, begun ? 1 : 0);
    } else if (strcmp(command, "ip") == 0) {
        SerialDMSeq.print("IP ");
        SerialDMSeq.println(ETH.hasIP() ? ETH.localIP().toString() : String("0.0.0.0"));
    } else if (strncmp(command, "pin ", 4) == 0) {
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
    } else if (sscanf(command, "psc %19s %u %39s", ip, &port, path) == 3) {
        psc(ip, (uint16_t)port, path);
    } else if (sscanf(command, "amc %19s %u %39s", ip, &port, path) == 3) {
        amc(ip, (uint16_t)port, path);
    } else if (sscanf(command, "hget %19s %u %63s", ip, &port, path) == 3) {
        hget(ip, (uint16_t)port, path);
    } else if (strncmp(command, "hpost ", 6) == 0) {
        int at = 0;
        if (sscanf(command, "hpost %19s %u %63s %n", ip, &port, path, &at) == 3 && at > 0) {
            hpost(ip, (uint16_t)port, path, command + at);
        } else {
            SerialDMSeq.println("ERROR ARGS");
        }
    } else {
        SerialDMSeq.println("ERROR UNKNOWN");
    }
}

void setup()
{
    SerialDMSeq.begin(115200);
    SerialDMSeq.print("STARTUP ");
    SerialDMSeq.println(CH32RV.resetReasonName());
    begun = ETH.begin();
    app.get("/hello", &hello);
    app.post("/echo", &echo);
    webServer.begin();
}

void loop()
{
    NetworkClient web = webServer.accept();
    if (web.connected()) {
        app.process(&web);
        web.stop();
    }
    while (SerialDMSeq.available()) {
        const int c = SerialDMSeq.read();
        if (c == '\n') {
            line[used] = 0;
            used = 0;
            reply(line);
        } else if (c != '\r' && used < sizeof line - 1) {
            line[used++] = (char)c;
        }
    }
}
