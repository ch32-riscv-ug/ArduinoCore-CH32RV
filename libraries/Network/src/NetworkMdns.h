/* An mDNS responder (multicast DNS, "name.local", and DNS-SD services), as
 * ESP32's MDNS. It answers on every interface the Network library
 * has started, and on the ones started later.
 *
 *   MDNS.begin("sensor");                  // sensor.local
 *   MDNS.addService("http", "tcp", 80);    // _http._tcp, for browsers and Bonjour / Avahi
 *   MDNS.addServiceTxt("http", "tcp", "path", "/");
 *
 * Up to 4 services, 96 bytes of TXT items each.
 *
 *   IPAddress ip = MDNS.queryHost("printer");   // printer.local's IPv4 address, 0.0.0.0 when nobody answers
 *
 * queryHost() asks by multicast DNS, as Network.hostByName("printer.local")
 * does; it needs no begin().
 *
 *   int n = MDNS.queryService("http", "tcp");   // who offers _http._tcp (after begin())
 *   for (int i = 0; i < n; i++) Serial.println(MDNS.hostname(i) + " " + MDNS.IP(i).toString() + ":" + MDNS.port(i));
 *
 * queryService() collects the answers for `timeout` ms (up to 4 instances): each
 * instance's host and port (SRV), TXT items and IPv4 address (A, or asked with
 * queryHost() when the answers lacked it). */
#pragma once
#include <Arduino.h>

class NetworkMdns {
public:
    bool begin(const char *hostName);
    void end();
    /* The services' instance name (default: the host name). Set before addService(). */
    bool setInstanceName(const char *name);
    /* service and proto with or without the leading underscore: ("http", "tcp") or ("_http", "_tcp"). */
    bool addService(const char *service, const char *proto, uint16_t port);
    bool addServiceTxt(const char *service, const char *proto, const char *key, const char *value);
    /* ESP32's MDNS.queryHost(): `host` with or without ".local". */
    IPAddress queryHost(const char *host, uint32_t timeout = 2000);
    IPAddress queryHost(const String &host, uint32_t timeout = 2000) { return queryHost(host.c_str(), timeout); }
    /* ESP32's MDNS.queryService(): how many instances answered; the results stay until the next query. */
    int queryService(const char *service, const char *proto, uint32_t timeout = 3000);
    int queryService(const String &service, const String &proto, uint32_t timeout = 3000)
    {
        return queryService(service.c_str(), proto.c_str(), timeout);
    }
    String hostname(int idx);          /* without ".local" */
    String instanceName(int idx);
    IPAddress IP(int idx);
    uint16_t port(int idx);
    int numTxt(int idx);
    bool hasTxt(int idx, const char *key);
    String txt(int idx, const char *key);
    String txtKey(int idx, int txtIdx);
    String txt(int idx, int txtIdx);
};

extern NetworkMdns MDNS;
